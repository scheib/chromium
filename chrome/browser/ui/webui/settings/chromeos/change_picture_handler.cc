// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/settings/chromeos/change_picture_handler.h"

#include <utility>

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/command_line.h"
#include "base/metrics/histogram_macros.h"
#include "base/path_service.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/chromeos/accessibility/accessibility_manager.h"
#include "chrome/browser/chromeos/camera_presence_notifier.h"
#include "chrome/browser/chromeos/login/users/avatar/user_image_manager.h"
#include "chrome/browser/chromeos/login/users/chrome_user_manager.h"
#include "chrome/browser/chromeos/login/users/default_user_image/default_user_images.h"
#include "chrome/browser/chromeos/profiles/profile_helper.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/chrome_select_file_policy.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/url_constants.h"
#include "chrome/grit/browser_resources.h"
#include "chrome/grit/generated_resources.h"
#include "chromeos/audio/chromeos_sounds.h"
#include "components/user_manager/user.h"
#include "components/user_manager/user_image/user_image.h"
#include "components/user_manager/user_manager.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/web_ui.h"
#include "content/public/common/url_constants.h"
#include "media/audio/sounds/sounds_manager.h"
#include "net/base/data_url.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/base/webui/web_ui_util.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

using content::BrowserThread;

namespace chromeos {
namespace settings {

namespace {

// Returns info about extensions for files we support as user images.
ui::SelectFileDialog::FileTypeInfo GetUserImageFileTypeInfo() {
  ui::SelectFileDialog::FileTypeInfo file_type_info;
  file_type_info.extensions.resize(1);

  file_type_info.extensions[0].push_back(FILE_PATH_LITERAL("bmp"));

  file_type_info.extensions[0].push_back(FILE_PATH_LITERAL("jpg"));
  file_type_info.extensions[0].push_back(FILE_PATH_LITERAL("jpeg"));

  file_type_info.extensions[0].push_back(FILE_PATH_LITERAL("png"));

  file_type_info.extensions[0].push_back(FILE_PATH_LITERAL("tif"));
  file_type_info.extensions[0].push_back(FILE_PATH_LITERAL("tiff"));

  file_type_info.extension_description_overrides.resize(1);
  file_type_info.extension_description_overrides[0] =
      l10n_util::GetStringUTF16(IDS_IMAGE_FILES);

  return file_type_info;
}

// Time histogram suffix for profile image download.
const char kProfileDownloadReason[] = "Preferences";

}  // namespace

ChangePictureHandler::ChangePictureHandler()
    : previous_image_url_(url::kAboutBlankURL),
      previous_image_index_(user_manager::User::USER_IMAGE_INVALID),
      user_manager_observer_(this),
      camera_observer_(this) {
  ui::ResourceBundle& bundle = ui::ResourceBundle::GetSharedInstance();
  media::SoundsManager* manager = media::SoundsManager::Get();
  manager->Initialize(SOUND_OBJECT_DELETE,
                      bundle.GetRawDataResource(IDR_SOUND_OBJECT_DELETE_WAV));
  manager->Initialize(SOUND_CAMERA_SNAP,
                      bundle.GetRawDataResource(IDR_SOUND_CAMERA_SNAP_WAV));
}

ChangePictureHandler::~ChangePictureHandler() {
  if (select_file_dialog_.get())
    select_file_dialog_->ListenerDestroyed();
}

void ChangePictureHandler::RegisterMessages() {
  web_ui()->RegisterMessageCallback(
      "chooseFile", base::Bind(&ChangePictureHandler::HandleChooseFile,
                               base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "photoTaken", base::Bind(&ChangePictureHandler::HandlePhotoTaken,
                               base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "discardPhoto", base::Bind(&ChangePictureHandler::HandleDiscardPhoto,
                                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "onChangePicturePageInitialized",
      base::Bind(&ChangePictureHandler::HandlePageInitialized,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "selectImage", base::Bind(&ChangePictureHandler::HandleSelectImage,
                                base::Unretained(this)));
}

void ChangePictureHandler::OnJavascriptAllowed() {
  user_manager_observer_.Add(user_manager::UserManager::Get());
  camera_observer_.Add(CameraPresenceNotifier::GetInstance());
}

void ChangePictureHandler::OnJavascriptDisallowed() {
  user_manager_observer_.Remove(user_manager::UserManager::Get());
  camera_observer_.Remove(CameraPresenceNotifier::GetInstance());
}

void ChangePictureHandler::SendDefaultImages() {
  base::ListValue image_urls;
  for (int i = default_user_image::kFirstDefaultImageIndex;
       i < default_user_image::kDefaultImagesCount; ++i) {
    std::unique_ptr<base::DictionaryValue> image_data(
        new base::DictionaryValue);
    image_data->SetString("url", default_user_image::GetDefaultImageUrl(i));
    image_data->SetString("author",
                          l10n_util::GetStringUTF16(
                              default_user_image::kDefaultImageAuthorIDs[i]));
    image_data->SetString("website",
                          l10n_util::GetStringUTF16(
                              default_user_image::kDefaultImageWebsiteIDs[i]));
    image_data->SetString("title",
                          default_user_image::GetDefaultImageDescription(i));
    image_urls.Append(std::move(image_data));
  }
  FireWebUIListener("default-images-changed", image_urls);
}

void ChangePictureHandler::HandleChooseFile(const base::ListValue* args) {
  DCHECK(args && args->empty());
  select_file_dialog_ = ui::SelectFileDialog::Create(
      this, new ChromeSelectFilePolicy(web_ui()->GetWebContents()));

  base::FilePath downloads_path;
  if (!PathService::Get(chrome::DIR_DEFAULT_DOWNLOADS, &downloads_path)) {
    NOTREACHED();
    return;
  }

  // Static so we initialize it only once.
  CR_DEFINE_STATIC_LOCAL(ui::SelectFileDialog::FileTypeInfo, file_type_info,
                         (GetUserImageFileTypeInfo()));

  select_file_dialog_->SelectFile(
      ui::SelectFileDialog::SELECT_OPEN_FILE,
      l10n_util::GetStringUTF16(IDS_DOWNLOAD_TITLE), downloads_path,
      &file_type_info, 0, FILE_PATH_LITERAL(""), GetBrowserWindow(), NULL);
}

void ChangePictureHandler::HandleDiscardPhoto(const base::ListValue* args) {
  DCHECK(args->empty());
  AccessibilityManager::Get()->PlayEarcon(
      SOUND_OBJECT_DELETE, PlaySoundOption::SPOKEN_FEEDBACK_ENABLED);
}

void ChangePictureHandler::HandlePhotoTaken(const base::ListValue* args) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  AccessibilityManager::Get()->PlayEarcon(
      SOUND_CAMERA_SNAP, PlaySoundOption::SPOKEN_FEEDBACK_ENABLED);

  std::string image_url;
  if (!args || args->GetSize() != 1 || !args->GetString(0, &image_url))
    NOTREACHED();
  DCHECK(!image_url.empty());

  std::string mime_type, charset, raw_data;
  if (!net::DataURL::Parse(GURL(image_url), &mime_type, &charset, &raw_data))
    NOTREACHED();
  DCHECK_EQ("image/png", mime_type);

  user_photo_ = gfx::ImageSkia();
  user_photo_data_url_ = image_url;

  ImageDecoder::Cancel(this);
  ImageDecoder::Start(this, raw_data);
}

void ChangePictureHandler::HandlePageInitialized(const base::ListValue* args) {
  DCHECK(args && args->empty());

  AllowJavascript();

  SendDefaultImages();
  SendSelectedImage();
  UpdateProfileImage();
}

void ChangePictureHandler::SendSelectedImage() {
  const user_manager::User* user = GetUser();
  DCHECK(user->GetAccountId().is_valid());

  previous_image_index_ = user->image_index();
  switch (previous_image_index_) {
    case user_manager::User::USER_IMAGE_EXTERNAL: {
      // User has image from camera/file, record it and add to the image list.
      previous_image_ = user->GetImage();
      SendOldImage(webui::GetBitmapDataUrl(*previous_image_.bitmap()));
      break;
    }
    case user_manager::User::USER_IMAGE_PROFILE: {
      // User has their Profile image as the current image.
      SendProfileImage(user->GetImage(), true);
      break;
    }
    default: {
      DCHECK(previous_image_index_ >= 0 &&
             previous_image_index_ < default_user_image::kDefaultImagesCount);
      if (previous_image_index_ >=
          default_user_image::kFirstDefaultImageIndex) {
        // User has image from the current set of default images.
        base::Value image_url(
            default_user_image::GetDefaultImageUrl(previous_image_index_));
        FireWebUIListener("selected-image-changed", image_url);
      } else {
        // User has an old default image, so present it in the same manner as a
        // previous image from file.
        SendOldImage(
            default_user_image::GetDefaultImageUrl(previous_image_index_));
      }
    }
  }
}

void ChangePictureHandler::SendProfileImage(const gfx::ImageSkia& image,
                                            bool should_select) {
  base::Value data_url(webui::GetBitmapDataUrl(*image.bitmap()));
  base::Value select(should_select);
  FireWebUIListener("profile-image-changed", data_url, select);
}

void ChangePictureHandler::UpdateProfileImage() {
  UserImageManager* user_image_manager =
      ChromeUserManager::Get()->GetUserImageManager(GetUser()->GetAccountId());
  // If we have a downloaded profile image and haven't sent it in
  // |SendSelectedImage|, send it now (without selecting).
  if (previous_image_index_ != user_manager::User::USER_IMAGE_PROFILE &&
      !user_image_manager->DownloadedProfileImage().isNull())
    SendProfileImage(user_image_manager->DownloadedProfileImage(), false);

  user_image_manager->DownloadProfileImage(kProfileDownloadReason);
}

void ChangePictureHandler::SendOldImage(const std::string& image_url) {
  previous_image_url_ = image_url;
  base::Value url(image_url);
  FireWebUIListener("old-image-changed", url);
}

void ChangePictureHandler::HandleSelectImage(const base::ListValue* args) {
  std::string image_url;
  std::string image_type;
  if (!args || args->GetSize() != 2 || !args->GetString(0, &image_url) ||
      !args->GetString(1, &image_type)) {
    NOTREACHED();
    return;
  }
  // |image_url| may be empty unless |image_type| is "default".
  DCHECK(!image_type.empty());

  UserImageManager* user_image_manager =
      ChromeUserManager::Get()->GetUserImageManager(GetUser()->GetAccountId());
  int image_index = user_manager::User::USER_IMAGE_INVALID;
  bool waiting_for_camera_photo = false;

  if (image_type == "old") {
    // Previous image (from camera or manually uploaded) re-selected.
    DCHECK(!previous_image_.isNull());
    user_image_manager->SaveUserImage(
        user_manager::UserImage::CreateAndEncode(
            previous_image_,
            user_manager::UserImage::FORMAT_JPEG));

    UMA_HISTOGRAM_EXACT_LINEAR("UserImage.ChangeChoice",
                               default_user_image::kHistogramImageOld,
                               default_user_image::kHistogramImagesCount);
    VLOG(1) << "Selected old user image";
  } else if (image_type == "default" &&
             default_user_image::IsDefaultImageUrl(image_url, &image_index)) {
    // One of the default user images.
    user_image_manager->SaveUserDefaultImageIndex(image_index);

    UMA_HISTOGRAM_EXACT_LINEAR(
        "UserImage.ChangeChoice",
        default_user_image::GetDefaultImageHistogramValue(image_index),
        default_user_image::kHistogramImagesCount);
    VLOG(1) << "Selected default user image: " << image_index;
  } else if (image_type == "camera") {
    // Camera image is selected.
    if (user_photo_.isNull()) {
      waiting_for_camera_photo = true;
      VLOG(1) << "Still waiting for camera image to decode";
    } else {
      SetImageFromCamera(user_photo_);
    }
  } else if (image_type == "profile") {
    // Profile image selected. Could be previous (old) user image.
    user_image_manager->SaveUserImageFromProfileImage();

    if (previous_image_index_ == user_manager::User::USER_IMAGE_PROFILE) {
      UMA_HISTOGRAM_EXACT_LINEAR("UserImage.ChangeChoice",
                                 default_user_image::kHistogramImageOld,
                                 default_user_image::kHistogramImagesCount);
      VLOG(1) << "Selected old (profile) user image";
    } else {
      UMA_HISTOGRAM_EXACT_LINEAR("UserImage.ChangeChoice",
                                 default_user_image::kHistogramImageFromProfile,
                                 default_user_image::kHistogramImagesCount);
      VLOG(1) << "Selected profile image";
    }
  } else {
    NOTREACHED() << "Unexpected image type: " << image_type;
  }

  // Ignore the result of the previous decoding if it's no longer needed.
  if (!waiting_for_camera_photo)
    ImageDecoder::Cancel(this);
}

void ChangePictureHandler::FileSelected(const base::FilePath& path,
                                        int index,
                                        void* params) {
  ChromeUserManager::Get()
      ->GetUserImageManager(GetUser()->GetAccountId())
      ->SaveUserImageFromFile(path);
  UMA_HISTOGRAM_EXACT_LINEAR("UserImage.ChangeChoice",
                             default_user_image::kHistogramImageFromFile,
                             default_user_image::kHistogramImagesCount);
  VLOG(1) << "Selected image from file";
}

void ChangePictureHandler::SetImageFromCamera(const gfx::ImageSkia& photo) {
  ChromeUserManager::Get()
      ->GetUserImageManager(GetUser()->GetAccountId())
      ->SaveUserImage(user_manager::UserImage::CreateAndEncode(
          photo, user_manager::UserImage::FORMAT_JPEG));
  UMA_HISTOGRAM_EXACT_LINEAR("UserImage.ChangeChoice",
                             default_user_image::kHistogramImageFromCamera,
                             default_user_image::kHistogramImagesCount);
  VLOG(1) << "Selected camera photo";
}

void ChangePictureHandler::SetCameraPresent(bool present) {
  FireWebUIListener("camera-presence-changed", base::Value(present));
}

void ChangePictureHandler::OnCameraPresenceCheckDone(bool is_camera_present) {
  SetCameraPresent(is_camera_present);
}

void ChangePictureHandler::OnUserImageChanged(const user_manager::User& user) {
  // Not initialized yet.
  if (previous_image_index_ == user_manager::User::USER_IMAGE_INVALID)
    return;
  SendSelectedImage();
}

void ChangePictureHandler::OnUserProfileImageUpdated(
    const user_manager::User& user,
    const gfx::ImageSkia& profile_image) {
  // User profile image has been updated.
  SendProfileImage(profile_image, false);
}

gfx::NativeWindow ChangePictureHandler::GetBrowserWindow() const {
  Browser* browser =
      chrome::FindBrowserWithWebContents(web_ui()->GetWebContents());
  return browser->window()->GetNativeWindow();
}

void ChangePictureHandler::OnImageDecoded(const SkBitmap& decoded_image) {
  user_photo_ = gfx::ImageSkia::CreateFrom1xBitmap(decoded_image);
  SetImageFromCamera(user_photo_);
}

void ChangePictureHandler::OnDecodeImageFailed() {
  NOTREACHED() << "Failed to decode PNG image from WebUI";
}

const user_manager::User* ChangePictureHandler::GetUser() const {
  Profile* profile = Profile::FromWebUI(web_ui());
  const user_manager::User* user =
      ProfileHelper::Get()->GetUserByProfile(profile);
  if (!user)
    return user_manager::UserManager::Get()->GetActiveUser();
  return user;
}

}  // namespace settings
}  // namespace chromeos
