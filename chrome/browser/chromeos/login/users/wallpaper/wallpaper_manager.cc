// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/login/users/wallpaper/wallpaper_manager.h"

#include <utility>

#include "ash/ash_constants.h"
#include "ash/public/cpp/shelf_types.h"
#include "ash/public/cpp/window_properties.h"
#include "ash/public/interfaces/constants.mojom.h"
#include "ash/shell.h"
#include "ash/wallpaper/wallpaper_controller.h"
#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/logging.h"
#include "base/memory/ptr_util.h"
#include "base/metrics/histogram_macros.h"
#include "base/path_service.h"
#include "base/sequenced_task_runner.h"
#include "base/sha1.h"
#include "base/strings/string_number_conversions.h"
#include "base/sys_info.h"
#include "base/task_scheduler/post_task.h"
#include "base/threading/thread_task_runner_handle.h"
#include "base/time/time.h"
#include "base/trace_event/trace_event.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/chromeos/customization/customization_document.h"
#include "chrome/browser/chromeos/extensions/wallpaper_manager_util.h"
#include "chrome/browser/chromeos/login/startup_utils.h"
#include "chrome/browser/chromeos/login/users/avatar/user_image_loader.h"
#include "chrome/browser/chromeos/login/users/wallpaper/wallpaper_window_state_manager.h"
#include "chrome/browser/chromeos/login/wizard_controller.h"
#include "chrome/browser/chromeos/policy/browser_policy_connector_chromeos.h"
#include "chrome/browser/chromeos/profiles/profile_helper.h"
#include "chrome/browser/image_decoder.h"
#include "chrome/browser/ui/app_list/arc/arc_app_list_prefs.h"
#include "chrome/browser/ui/ash/ash_util.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/pref_names.h"
#include "chromeos/chromeos_switches.h"
#include "chromeos/cryptohome/system_salt_getter.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "components/signin/core/account_id/account_id.h"
#include "components/user_manager/known_user.h"
#include "components/user_manager/user_names.h"
#include "components/user_manager/user_type.h"
#include "components/wallpaper/wallpaper_files_id.h"
#include "components/wallpaper/wallpaper_layout.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/notification_service.h"
#include "content/public/common/content_switches.h"
#include "content/public/common/service_manager_connection.h"
#include "crypto/sha2.h"
#include "services/service_manager/public/cpp/connector.h"
#include "url/gurl.h"

using content::BrowserThread;
using wallpaper::WallpaperManagerBase;
using wallpaper::WallpaperInfo;
using wallpaper::MovableOnDestroyCallback;
using wallpaper::MovableOnDestroyCallbackHolder;

namespace chromeos {

namespace {

WallpaperManager* wallpaper_manager = nullptr;

// The amount of delay before starts to move custom wallpapers to the new place.
const int kMoveCustomWallpaperDelaySeconds = 30;

const int kCacheWallpaperDelayMs = 500;

// Names of nodes with info about wallpaper in |kUserWallpapersProperties|
// dictionary.
const char kNewWallpaperDateNodeName[] = "date";
const char kNewWallpaperLayoutNodeName[] = "layout";
const char kNewWallpaperLocationNodeName[] = "file";
const char kNewWallpaperTypeNodeName[] = "type";

// Known user keys.
const char kWallpaperFilesId[] = "wallpaper-files-id";

// The directory and file name to save the downloaded device policy controlled
// wallpaper.
const char kDeviceWallpaperDir[] = "device_wallpaper";
const char kDeviceWallpaperFile[] = "device_wallpaper_image.jpg";

// The Wallpaper App that the user is using right now on Chrome OS. It's the
// app that is used when the user right clicks on desktop and selects "Set
// wallpaper" or when the user selects "Set wallpaper" from chrome://settings
// page. This is recorded at user login. It's used to back an UMA histogram so
// it should be treated as append-only.
enum WallpaperAppsType {
  WALLPAPERS_PICKER_APP_CHROMEOS = 0,
  WALLPAPERS_APP_ANDROID = 1,
  WALLPAPERS_APPS_NUM = 2,
};

// These global default values are used to set customized default
// wallpaper path in WallpaperManager::InitializeWallpaper().
base::FilePath GetCustomizedWallpaperDefaultRescaledFileName(
    const std::string& suffix) {
  const base::FilePath default_downloaded_file_name =
      ServicesCustomizationDocument::GetCustomizedWallpaperDownloadedFileName();
  const base::FilePath default_cache_dir =
      ServicesCustomizationDocument::GetCustomizedWallpaperCacheDir();
  if (default_downloaded_file_name.empty() || default_cache_dir.empty())
    return base::FilePath();
  return default_cache_dir.Append(
      default_downloaded_file_name.BaseName().value() + suffix);
}

// Whether WallpaperController should start with customized default
// wallpaper in WallpaperManager::InitializeWallpaper() or not.
bool ShouldUseCustomizedDefaultWallpaper() {
  PrefService* pref_service = g_browser_process->local_state();

  return !(pref_service->FindPreference(
      prefs::kCustomizationDefaultWallpaperURL)->IsDefaultValue());
}

// Returns index of the first public session user found in |users|
// or -1 otherwise.
int FindPublicSession(const user_manager::UserList& users) {
  int index = -1;
  int i = 0;
  for (user_manager::UserList::const_iterator it = users.begin();
       it != users.end();
       ++it, ++i) {
    if ((*it)->GetType() == user_manager::USER_TYPE_PUBLIC_ACCOUNT) {
      index = i;
      break;
    }
  }

  return index;
}

// This has once been copied from
// brillo::cryptohome::home::SanitizeUserName(username) to be used for
// wallpaper identification purpose only.
//
// Historic note: We need some way to identify users wallpaper files in
// the device filesystem. Historically User::username_hash() was used for this
// purpose, but it has two caveats:
// 1. username_hash() is defined only after user has logged in.
// 2. If cryptohome identifier changes, username_hash() will also change,
//    and we may loose user => wallpaper files mapping at that point.
// So this function gives WallpaperManager independent hashing method to break
// this dependency.
//
wallpaper::WallpaperFilesId HashWallpaperFilesIdStr(
    const std::string& files_id_unhashed) {
  SystemSaltGetter* salt_getter = SystemSaltGetter::Get();
  DCHECK(salt_getter);

  // System salt must be defined at this point.
  const SystemSaltGetter::RawSalt* salt = salt_getter->GetRawSalt();
  if (!salt)
    LOG(FATAL) << "WallpaperManager HashWallpaperFilesIdStr(): no salt!";

  unsigned char binmd[base::kSHA1Length];
  std::string lowercase(files_id_unhashed);
  std::transform(lowercase.begin(), lowercase.end(), lowercase.begin(),
                 ::tolower);
  std::vector<uint8_t> data = *salt;
  std::copy(files_id_unhashed.begin(), files_id_unhashed.end(),
            std::back_inserter(data));
  base::SHA1HashBytes(data.data(), data.size(), binmd);
  std::string result = base::HexEncode(binmd, sizeof(binmd));
  std::transform(result.begin(), result.end(), result.begin(), ::tolower);
  return wallpaper::WallpaperFilesId::FromString(result);
}

// Returns true if HashWallpaperFilesIdStr will not assert().
bool CanGetFilesId() {
  return SystemSaltGetter::Get()->GetRawSalt();
}

// Call |closure| when HashWallpaperFilesIdStr will not assert().
void CallWhenCanGetFilesId(const base::Closure& closure) {
  SystemSaltGetter::Get()->AddOnSystemSaltReady(closure);
}

void SetKnownUserWallpaperFilesId(
    const AccountId& account_id,
    const wallpaper::WallpaperFilesId& wallpaper_files_id) {
  user_manager::known_user::SetStringPref(account_id, kWallpaperFilesId,
                                          wallpaper_files_id.id());
}

// A helper to set the wallpaper image for Classic Ash and Mash.
void SetWallpaper(const gfx::ImageSkia& image,
                  wallpaper::WallpaperLayout layout) {
  if (ash_util::IsRunningInMash()) {
    // In mash, connect to the WallpaperController interface via mojo.
    service_manager::Connector* connector =
        content::ServiceManagerConnection::GetForProcess()->GetConnector();
    if (!connector)
      return;

    ash::mojom::WallpaperControllerPtr wallpaper_controller;
    connector->BindInterface(ash::mojom::kServiceName, &wallpaper_controller);
    // TODO(crbug.com/655875): Optimize ash wallpaper transport; avoid sending
    // large bitmaps over Mojo; use shared memory like BitmapUploader, etc.
    wallpaper_controller->SetWallpaper(*image.bitmap(), layout);
  } else if (ash::Shell::HasInstance()) {
    // Note: Wallpaper setting is skipped in unit tests without shell instances.
    // In classic ash, interact with the WallpaperController class directly.
    ash::Shell::Get()->wallpaper_controller()->SetWallpaperImage(image, layout);
  }
}

// A helper function to check the existing/downloaded device wallpaper file's
// hash value matches with the hash value provided in the policy settings.
bool CheckDeviceWallpaperMatchHash(const base::FilePath& device_wallpaper_file,
                                   const std::string& hash) {
  std::string image_data;
  if (base::ReadFileToString(device_wallpaper_file, &image_data)) {
    std::string sha_hash = crypto::SHA256HashString(image_data);
    if (base::ToLowerASCII(base::HexEncode(
            sha_hash.c_str(), sha_hash.size())) == base::ToLowerASCII(hash)) {
      return true;
    }
  }
  return false;
}

}  // namespace

// This is "wallpaper either scheduled to load, or loading right now".
//
// While enqueued, it defines moment in the future, when it will be loaded.
// Enqueued but not started request might be updated by subsequent load
// request. Therefore it's created empty, and updated being enqueued.
//
// PendingWallpaper is owned by WallpaperManager, but reference to this object
// is passed to other threads by PostTask() calls, therefore it is
// RefCountedThreadSafe.
class WallpaperManager::PendingWallpaper :
    public base::RefCountedThreadSafe<PendingWallpaper> {
 public:
  // Do LoadWallpaper() - image not found in cache.
  PendingWallpaper(const base::TimeDelta delay, const AccountId& account_id)
      : account_id_(account_id),
        default_(false),
        on_finish_(new MovableOnDestroyCallback(
            base::Bind(&WallpaperManager::PendingWallpaper::OnWallpaperSet,
                       this))) {
    timer.Start(
        FROM_HERE,
        delay,
        base::Bind(&WallpaperManager::PendingWallpaper::ProcessRequest, this));
  }

  // There are 4 cases in SetUserWallpaper:
  // 1) gfx::ImageSkia is found in cache.
  //    - Schedule task to (probably) resize it and install:
  //    call SetWallpaper(user_wallpaper, layout);
  // 2) WallpaperInfo is found in cache
  //    - need to LoadWallpaper(), resize and install.
  // 3) wallpaper path is not NULL, load image URL, then resize, etc...
  // 4) SetDefaultWallpaper (either on some error, or when user is new).
  void ResetSetWallpaperImage(const gfx::ImageSkia& image,
                              const wallpaper::WallpaperInfo& info) {
    SetMode(image, info, base::FilePath(), false);
  }

  void ResetLoadWallpaper(const wallpaper::WallpaperInfo& info) {
    SetMode(gfx::ImageSkia(), info, base::FilePath(), false);
  }

  void ResetSetCustomWallpaper(const wallpaper::WallpaperInfo& info,
                               const base::FilePath& wallpaper_path) {
    SetMode(gfx::ImageSkia(), info, wallpaper_path, false);
  }

  void ResetSetDefaultWallpaper() {
    SetMode(gfx::ImageSkia(), WallpaperInfo(), base::FilePath(), true);
  }

 private:
  friend class base::RefCountedThreadSafe<PendingWallpaper>;

  ~PendingWallpaper() {}

  // All Reset*() methods use SetMode() to set object to new state.
  void SetMode(const gfx::ImageSkia& image,
               const wallpaper::WallpaperInfo& info,
               const base::FilePath& wallpaper_path,
               const bool is_default) {
    user_wallpaper_ = image;
    info_ = info;
    wallpaper_path_ = wallpaper_path;
    default_ = is_default;
  }

  // This method is usually triggered by timer to actually load request.
  void ProcessRequest() {
    DCHECK_CURRENTLY_ON(BrowserThread::UI);

    timer.Stop();  // Erase reference to self.

    WallpaperManager* manager = WallpaperManager::Get();
    if (manager->pending_inactive_ == this)
      manager->pending_inactive_ = NULL;

    started_load_at_ = base::Time::Now();

    if (default_) {
      manager->DoSetDefaultWallpaper(account_id_, std::move(on_finish_));
    } else if (!user_wallpaper_.isNull()) {
      SetWallpaper(user_wallpaper_, info_.layout);
    } else if (!wallpaper_path_.empty()) {
      manager->task_runner_->PostTask(
          FROM_HERE,
          base::Bind(&WallpaperManager::GetCustomWallpaperInternal, account_id_,
                     info_, wallpaper_path_, true /* update wallpaper */,
                     base::ThreadTaskRunnerHandle::Get(),
                     base::Passed(std::move(on_finish_)),
                     manager->weak_factory_.GetWeakPtr()));
    } else if (!info_.location.empty()) {
      manager->LoadWallpaper(account_id_, info_, true, std::move(on_finish_));
    } else {
      // PendingWallpaper was created and never initialized?
      NOTREACHED();
      // Error. Do not record time.
      started_load_at_ = base::Time();
    }
    on_finish_.reset();
  }

  // This method is called by callback, when load request is finished.
  void OnWallpaperSet() {
    DCHECK_CURRENTLY_ON(BrowserThread::UI);

    // The only known case for this check to fail is global destruction during
    // wallpaper load. It should never happen.
    if (!BrowserThread::CurrentlyOn(BrowserThread::UI))
      return;  // We are in a process of global destruction.

    timer.Stop();  // Erase reference to self.

    WallpaperManager* manager = WallpaperManager::Get();
    if (!started_load_at_.is_null()) {
      const base::TimeDelta elapsed = base::Time::Now() - started_load_at_;
      manager->SaveLastLoadTime(elapsed);
    }
    if (manager->pending_inactive_ == this) {
      // ProcessRequest() was never executed.
      manager->pending_inactive_ = NULL;
    }

    // Destroy self.
    manager->RemovePendingWallpaperFromList(this);
  }

  AccountId account_id_;
  wallpaper::WallpaperInfo info_;
  gfx::ImageSkia user_wallpaper_;
  base::FilePath wallpaper_path_;

  // Load default wallpaper instead of user image.
  bool default_;

  // This is "on destroy" callback that will call OnWallpaperSet() when
  // image will be loaded.
  wallpaper::MovableOnDestroyCallbackHolder on_finish_;
  base::OneShotTimer timer;

  // Load start time to calculate duration.
  base::Time started_load_at_;

  DISALLOW_COPY_AND_ASSIGN(PendingWallpaper);
};

// WallpaperManager, public: ---------------------------------------------------

WallpaperManager::~WallpaperManager() {
  show_user_name_on_signin_subscription_.reset();
  device_wallpaper_image_subscription_.reset();
  user_manager::UserManager::Get()->RemoveSessionStateObserver(this);
  weak_factory_.InvalidateWeakPtrs();
}

// static
void WallpaperManager::Initialize() {
  CHECK(!wallpaper_manager);
  wallpaper_manager = new WallpaperManager();
}

// static
WallpaperManager* WallpaperManager::Get() {
  DCHECK(wallpaper_manager);
  return wallpaper_manager;
}

// static
void WallpaperManager::Shutdown() {
  CHECK(wallpaper_manager);
  delete wallpaper_manager;
  wallpaper_manager = nullptr;
}

WallpaperManager::WallpaperResolution
WallpaperManager::GetAppropriateResolution() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  gfx::Size size = ash::WallpaperController::GetMaxDisplaySizeInNative();
  return (size.width() > wallpaper::kSmallWallpaperMaxWidth ||
          size.height() > wallpaper::kSmallWallpaperMaxHeight)
             ? WALLPAPER_RESOLUTION_LARGE
             : WALLPAPER_RESOLUTION_SMALL;
}

void WallpaperManager::AddObservers() {
  show_user_name_on_signin_subscription_ =
      CrosSettings::Get()->AddSettingsObserver(
          kAccountsPrefShowUserNamesOnSignIn,
          base::Bind(&WallpaperManager::InitializeRegisteredDeviceWallpaper,
                     weak_factory_.GetWeakPtr()));
  device_wallpaper_image_subscription_ =
      CrosSettings::Get()->AddSettingsObserver(
          kDeviceWallpaperImage,
          base::Bind(&WallpaperManager::OnDeviceWallpaperPolicyChanged,
                     weak_factory_.GetWeakPtr()));
}

void WallpaperManager::EnsureLoggedInUserWallpaperLoaded() {
  WallpaperInfo info;
  if (GetLoggedInUserWallpaperInfo(&info)) {
    UMA_HISTOGRAM_ENUMERATION("Ash.Wallpaper.Type", info.type,
                              user_manager::User::WALLPAPER_TYPE_COUNT);
    RecordWallpaperAppType();
    if (info == current_user_wallpaper_info_)
      return;
  }
  SetUserWallpaperNow(
      user_manager::UserManager::Get()->GetActiveUser()->GetAccountId());
}

void WallpaperManager::InitializeWallpaper() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  user_manager::UserManager* user_manager = user_manager::UserManager::Get();

  // Apply device customization.
  if (ShouldUseCustomizedDefaultWallpaper()) {
    SetDefaultWallpaperPath(GetCustomizedWallpaperDefaultRescaledFileName(
                                wallpaper::kSmallWallpaperSuffix),
                            std::unique_ptr<gfx::ImageSkia>(),
                            GetCustomizedWallpaperDefaultRescaledFileName(
                                wallpaper::kLargeWallpaperSuffix),
                            std::unique_ptr<gfx::ImageSkia>());
  }

  base::CommandLine* command_line = GetCommandLine();
  if (command_line->HasSwitch(chromeos::switches::kGuestSession)) {
    // Guest wallpaper should be initialized when guest login.
    // Note: This maybe called before login. So IsLoggedInAsGuest can not be
    // used here to determine if current user is guest.
    return;
  }

  if (command_line->HasSwitch(::switches::kTestType))
    WizardController::SetZeroDelays();

  // Zero delays is also set in autotests.
  if (WizardController::IsZeroDelayEnabled()) {
    // Ensure tests have some sort of wallpaper.
    ash::Shell::Get()->wallpaper_controller()->CreateEmptyWallpaper();
    return;
  }

  if (!user_manager->IsUserLoggedIn()) {
    if (!StartupUtils::IsDeviceRegistered())
      SetDefaultWallpaperDelayed(user_manager::SignInAccountId());
    else
      InitializeRegisteredDeviceWallpaper();
    return;
  }
  SetUserWallpaperDelayed(user_manager->GetActiveUser()->GetAccountId());
}

void WallpaperManager::Open() {
  if (wallpaper_manager_util::ShouldUseAndroidWallpapersApp(
          ProfileHelper::Get()->GetProfileByUser(
              user_manager::UserManager::Get()->GetActiveUser())) &&
      !ash_util::IsRunningInMash()) {
    // Window activation watch to minimize all inactive windows is only needed
    // by Android Wallpaper app. Legacy Chrome OS Wallpaper Picker app does that
    // via extension API.
    activation_client_observer_.Add(ash::Shell::Get()->activation_client());
  }
  wallpaper_manager_util::OpenWallpaperManager();
}

void WallpaperManager::Observe(int type,
                               const content::NotificationSource& source,
                               const content::NotificationDetails& details) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  switch (type) {
    case chrome::NOTIFICATION_LOGIN_USER_CHANGED: {
      ClearDisposableWallpaperCache();
      BrowserThread::PostDelayedTask(
          BrowserThread::UI,
          FROM_HERE,
          base::Bind(&WallpaperManager::MoveLoggedInUserCustomWallpaper,
                     weak_factory_.GetWeakPtr()),
          base::TimeDelta::FromSeconds(kMoveCustomWallpaperDelaySeconds));
      break;
    }
    case chrome::NOTIFICATION_LOGIN_OR_LOCK_WEBUI_VISIBLE: {
      if (!GetCommandLine()->HasSwitch(switches::kDisableBootAnimation)) {
        BrowserThread::PostDelayedTask(
            BrowserThread::UI, FROM_HERE,
            base::Bind(&WallpaperManager::CacheUsersWallpapers,
                       weak_factory_.GetWeakPtr()),
            base::TimeDelta::FromMilliseconds(kCacheWallpaperDelayMs));
      } else {
        should_cache_wallpaper_ = true;
      }
      break;
    }
    case chrome::NOTIFICATION_WALLPAPER_ANIMATION_FINISHED: {
      NotifyAnimationFinished();
      if (should_cache_wallpaper_) {
        BrowserThread::PostDelayedTask(
            BrowserThread::UI, FROM_HERE,
            base::Bind(&WallpaperManager::CacheUsersWallpapers,
                       weak_factory_.GetWeakPtr()),
            base::TimeDelta::FromMilliseconds(kCacheWallpaperDelayMs));
        should_cache_wallpaper_ = false;
      }
      break;
    }
    default:
      NOTREACHED() << "Unexpected notification " << type;
  }
}

void WallpaperManager::RemoveUserWallpaperInfo(const AccountId& account_id) {
  if (wallpaper_cache_.find(account_id) != wallpaper_cache_.end())
    wallpaper_cache_.erase(account_id);

  PrefService* prefs = g_browser_process->local_state();
  // PrefService could be NULL in tests.
  if (!prefs)
    return;
  WallpaperInfo info;
  GetUserWallpaperInfo(account_id, &info);
  DictionaryPrefUpdate prefs_wallpapers_info_update(
      prefs, wallpaper::kUsersWallpaperInfo);
  prefs_wallpapers_info_update->RemoveWithoutPathExpansion(
      account_id.GetUserEmail(), NULL);
  DeleteUserWallpapers(account_id, info.location);
}

void WallpaperManager::OnPolicyFetched(const std::string& policy,
                                       const AccountId& account_id,
                                       std::unique_ptr<std::string> data) {
  if (!data)
    return;

  user_image_loader::StartWithData(
      task_runner_, std::move(data), ImageDecoder::ROBUST_JPEG_CODEC,
      0,  // Do not crop.
      base::Bind(&WallpaperManager::SetPolicyControlledWallpaper,
                 weak_factory_.GetWeakPtr(), account_id));
}

void WallpaperManager::SetCustomWallpaper(
    const AccountId& account_id,
    const wallpaper::WallpaperFilesId& wallpaper_files_id,
    const std::string& file,
    wallpaper::WallpaperLayout layout,
    user_manager::User::WallpaperType type,
    const gfx::ImageSkia& image,
    bool update_wallpaper) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  // There is no visible wallpaper in kiosk mode.
  if (user_manager::UserManager::Get()->IsLoggedInAsKioskApp())
    return;

  // Don't allow custom wallpapers while policy is in effect.
  if (type != user_manager::User::POLICY && IsPolicyControlled(account_id))
    return;

  base::FilePath wallpaper_path = GetCustomWallpaperPath(
      wallpaper::kOriginalWallpaperSubDir, wallpaper_files_id, file);

  // If decoded wallpaper is empty, we have probably failed to decode the file.
  // Use default wallpaper in this case.
  if (image.isNull()) {
    SetDefaultWallpaperDelayed(account_id);
    return;
  }

  const user_manager::User* user =
      user_manager::UserManager::Get()->FindUser(account_id);
  CHECK(user);
  const bool is_persistent =
      !user_manager::UserManager::Get()->IsUserNonCryptohomeDataEphemeral(
          account_id) ||
      (type == user_manager::User::POLICY &&
       user->GetType() == user_manager::USER_TYPE_PUBLIC_ACCOUNT);

  WallpaperInfo wallpaper_info = {
      wallpaper_path.value(),
      layout,
      type,
      // Date field is not used.
      base::Time::Now().LocalMidnight()
  };
  if (is_persistent) {
    image.EnsureRepsForSupportedScales();
    std::unique_ptr<gfx::ImageSkia> deep_copy(image.DeepCopy());
    // Block shutdown on this task. Otherwise, we may lose the custom wallpaper
    // that the user selected.
    scoped_refptr<base::SequencedTaskRunner> blocking_task_runner =
        BrowserThread::GetBlockingPool()
            ->GetSequencedTaskRunnerWithShutdownBehavior(
                sequence_token_, base::SequencedWorkerPool::BLOCK_SHUTDOWN);
    // TODO(bshe): This may break if RawImage becomes RefCountedMemory.
    blocking_task_runner->PostTask(
        FROM_HERE,
        base::Bind(&WallpaperManager::SaveCustomWallpaper, wallpaper_files_id,
                   base::FilePath(wallpaper_info.location),
                   wallpaper_info.layout, base::Passed(std::move(deep_copy))));
  }

  std::string relative_path =
      base::FilePath(wallpaper_files_id.id()).Append(file).value();
  // User's custom wallpaper path is determined by relative path and the
  // appropriate wallpaper resolution in GetCustomWallpaperInternal.
  WallpaperInfo info = {
      relative_path,
      layout,
      type,
      base::Time::Now().LocalMidnight()
  };
  SetUserWallpaperInfo(account_id, info, is_persistent);
  if (update_wallpaper) {
    GetPendingWallpaper(account_id, false)->ResetSetWallpaperImage(image, info);
  }

  wallpaper_cache_[account_id] = CustomWallpaperElement(wallpaper_path, image);
}

void WallpaperManager::SetDefaultWallpaperNow(const AccountId& account_id) {
  GetPendingWallpaper(account_id, false)->ResetSetDefaultWallpaper();
}

void WallpaperManager::SetDefaultWallpaperDelayed(const AccountId& account_id) {
  GetPendingWallpaper(account_id, true)->ResetSetDefaultWallpaper();
}

void WallpaperManager::DoSetDefaultWallpaper(
    const AccountId& account_id,
    MovableOnDestroyCallbackHolder on_finish) {
  // There is no visible wallpaper in kiosk mode.
  if (user_manager::UserManager::Get()->IsLoggedInAsKioskApp())
    return;
  wallpaper_cache_.erase(account_id);

  WallpaperResolution resolution = GetAppropriateResolution();
  const bool use_small = (resolution == WALLPAPER_RESOLUTION_SMALL);

  const base::FilePath* file = NULL;

  const user_manager::User* user =
      user_manager::UserManager::Get()->FindUser(account_id);

  if (user_manager::UserManager::Get()->IsLoggedInAsGuest()) {
    file =
        use_small ? &guest_small_wallpaper_file_ : &guest_large_wallpaper_file_;
  } else if (user && user->GetType() == user_manager::USER_TYPE_CHILD) {
    file =
        use_small ? &child_small_wallpaper_file_ : &child_large_wallpaper_file_;
  } else {
    file = use_small ? &default_small_wallpaper_file_
                     : &default_large_wallpaper_file_;
  }
  wallpaper::WallpaperLayout layout =
      use_small ? wallpaper::WALLPAPER_LAYOUT_CENTER
                : wallpaper::WALLPAPER_LAYOUT_CENTER_CROPPED;
  DCHECK(file);
  if (!default_wallpaper_image_.get() ||
      default_wallpaper_image_->file_path() != *file) {
    default_wallpaper_image_.reset();
    if (!file->empty()) {
      loaded_wallpapers_for_test_++;
      StartLoadAndSetDefaultWallpaper(*file, layout, std::move(on_finish),
                                      &default_wallpaper_image_);
      return;
    }

    CreateSolidDefaultWallpaper();
  }
  // 1x1 wallpaper is actually solid color, so it should be stretched.
  if (default_wallpaper_image_->image().width() == 1 &&
      default_wallpaper_image_->image().height() == 1)
    layout = wallpaper::WALLPAPER_LAYOUT_STRETCH;

  SetWallpaper(default_wallpaper_image_->image(), layout);
}

void WallpaperManager::SetUserWallpaperInfo(const AccountId& account_id,
                                            const WallpaperInfo& info,
                                            bool is_persistent) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  current_user_wallpaper_info_ = info;
  if (!is_persistent)
    return;

  PrefService* local_state = g_browser_process->local_state();
  DictionaryPrefUpdate wallpaper_update(local_state,
                                        wallpaper::kUsersWallpaperInfo);

  auto wallpaper_info_dict = base::MakeUnique<base::DictionaryValue>();
  wallpaper_info_dict->SetString(kNewWallpaperDateNodeName,
      base::Int64ToString(info.date.ToInternalValue()));
  wallpaper_info_dict->SetString(kNewWallpaperLocationNodeName, info.location);
  wallpaper_info_dict->SetInteger(kNewWallpaperLayoutNodeName, info.layout);
  wallpaper_info_dict->SetInteger(kNewWallpaperTypeNodeName, info.type);
  wallpaper_update->SetWithoutPathExpansion(account_id.GetUserEmail(),
                                            std::move(wallpaper_info_dict));
}

void WallpaperManager::ScheduleSetUserWallpaper(const AccountId& account_id,
                                                bool delayed) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  // Some unit tests come here without a UserManager or without a pref system.
  if (!user_manager::UserManager::IsInitialized() ||
      !g_browser_process->local_state()) {
    return;
  }

  const user_manager::User* user =
      user_manager::UserManager::Get()->FindUser(account_id);

  // User is unknown or there is no visible wallpaper in kiosk mode.
  if (!user || user->GetType() == user_manager::USER_TYPE_KIOSK_APP ||
      user->GetType() == user_manager::USER_TYPE_ARC_KIOSK_APP) {
    return;
  }

  // For a enterprise managed user, set the device wallpaper if we're at the
  // login screen.
  if (!user_manager::UserManager::Get()->IsUserLoggedIn() &&
      SetDeviceWallpaperIfApplicable(account_id))
    return;

  // Guest user or regular user in ephemeral mode.
  if ((user_manager::UserManager::Get()->IsUserNonCryptohomeDataEphemeral(
           account_id) &&
       user->HasGaiaAccount()) ||
      user->GetType() == user_manager::USER_TYPE_GUEST) {
    InitInitialUserWallpaper(account_id, false);
    GetPendingWallpaper(account_id, delayed)->ResetSetDefaultWallpaper();
    if (base::SysInfo::IsRunningOnChromeOS()) {
      LOG(ERROR)
          << "User is ephemeral or guest! Fallback to default wallpaper.";
    }
    return;
  }

  last_selected_user_ = account_id;

  WallpaperInfo info;

  if (!GetUserWallpaperInfo(account_id, &info)) {
    InitInitialUserWallpaper(account_id, true);
    GetUserWallpaperInfo(account_id, &info);
  }

  gfx::ImageSkia user_wallpaper;
  current_user_wallpaper_info_ = info;
  if (GetWallpaperFromCache(account_id, &user_wallpaper)) {
    GetPendingWallpaper(account_id, delayed)
        ->ResetSetWallpaperImage(user_wallpaper, info);
  } else {
    if (info.location.empty()) {
      // Uses default built-in wallpaper when file is empty. Eventually, we
      // will only ship one built-in wallpaper in ChromeOS image.
      GetPendingWallpaper(account_id, delayed)->ResetSetDefaultWallpaper();
      return;
    }

    if (info.type == user_manager::User::CUSTOMIZED ||
        info.type == user_manager::User::POLICY ||
        info.type == user_manager::User::DEVICE) {
      base::FilePath wallpaper_path;
      if (info.type != user_manager::User::DEVICE) {
        const char* sub_dir = GetCustomWallpaperSubdirForCurrentResolution();
        // Wallpaper is not resized when layout is
        // wallpaper::WALLPAPER_LAYOUT_CENTER.
        // Original wallpaper should be used in this case.
        // TODO(bshe): Generates cropped custom wallpaper for CENTER layout.
        if (info.layout == wallpaper::WALLPAPER_LAYOUT_CENTER)
          sub_dir = wallpaper::kOriginalWallpaperSubDir;
        wallpaper_path = GetCustomWallpaperDir(sub_dir);
        wallpaper_path = wallpaper_path.Append(info.location);
      } else {
        wallpaper_path = GetDeviceWallpaperFilePath();
      }

      CustomWallpaperMap::iterator it = wallpaper_cache_.find(account_id);
      // Do not try to load the wallpaper if the path is the same. Since loading
      // could still be in progress, we ignore the existence of the image.
      if (it != wallpaper_cache_.end() && it->second.first == wallpaper_path)
        return;

      // Set the new path and reset the existing image - the image will be
      // added once it becomes available.
      wallpaper_cache_[account_id] =
          CustomWallpaperElement(wallpaper_path, gfx::ImageSkia());
      loaded_wallpapers_for_test_++;

      GetPendingWallpaper(account_id, delayed)
          ->ResetSetCustomWallpaper(info, wallpaper_path);
      return;
    }

    // Load downloaded ONLINE or converted DEFAULT wallpapers.
    GetPendingWallpaper(account_id, delayed)->ResetLoadWallpaper(info);
  }
}

void WallpaperManager::SetWallpaperFromImageSkia(
    const AccountId& account_id,
    const gfx::ImageSkia& image,
    wallpaper::WallpaperLayout layout,
    bool update_wallpaper) {
  DCHECK(user_manager::UserManager::Get()->IsUserLoggedIn());

  // There is no visible wallpaper in kiosk mode.
  if (user_manager::UserManager::Get()->IsLoggedInAsKioskApp())
    return;
  WallpaperInfo info;
  info.layout = layout;

  // This is an API call and we do not know the path. So we set the image, but
  // no path.
  wallpaper_cache_[account_id] =
      CustomWallpaperElement(base::FilePath(), image);

  if (update_wallpaper) {
    GetPendingWallpaper(last_selected_user_, false /* Not delayed */)
        ->ResetSetWallpaperImage(image, info);
  }
}

void WallpaperManager::UpdateWallpaper(bool clear_cache) {
  // For GAIA login flow, the last_selected_user_ may not be set before user
  // login. If UpdateWallpaper is called at GAIA login screen, no wallpaper will
  // be set. It could result a black screen on external monitors.
  // See http://crbug.com/265689 for detail.
  if (last_selected_user_.empty())
    SetDefaultWallpaperNow(user_manager::SignInAccountId());
  WallpaperManagerBase::UpdateWallpaper(clear_cache);
}

void WallpaperManager::OnWindowActivated(ActivationReason reason,
                                         aura::Window* gained_active,
                                         aura::Window* lost_active) {
  if (!gained_active)
    return;

  const std::string arc_wallpapers_app_id = ArcAppListPrefs::GetAppId(
      wallpaper_manager_util::kAndroidWallpapersAppPackage,
      wallpaper_manager_util::kAndroidWallpapersAppActivity);
  ash::ShelfID shelf_id =
      ash::ShelfID::Deserialize(gained_active->GetProperty(ash::kShelfIDKey));
  if (shelf_id.app_id == arc_wallpapers_app_id) {
    chromeos::WallpaperWindowStateManager::MinimizeInactiveWindows(
        user_manager::UserManager::Get()->GetActiveUser()->username_hash());
    DCHECK(!ash_util::IsRunningInMash() && ash::Shell::Get());
    activation_client_observer_.Remove(ash::Shell::Get()->activation_client());
    window_observer_.Add(gained_active);
  }
}

void WallpaperManager::OnWindowDestroying(aura::Window* window) {
  window_observer_.Remove(window);
  chromeos::WallpaperWindowStateManager::RestoreWindows(
      user_manager::UserManager::Get()->GetActiveUser()->username_hash());
}

// WallpaperManager, private: --------------------------------------------------

WallpaperManager::WallpaperManager()
    : binding_(this),
      pending_inactive_(nullptr),
      activation_client_observer_(this),
      window_observer_(this),
      weak_factory_(this) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  wallpaper::WallpaperManagerBase::SetPathIds(
      chrome::DIR_USER_DATA, chrome::DIR_CHROMEOS_WALLPAPERS,
      chrome::DIR_CHROMEOS_CUSTOM_WALLPAPERS);
  SetDefaultWallpaperPathsFromCommandLine(
      base::CommandLine::ForCurrentProcess());
  registrar_.Add(this, chrome::NOTIFICATION_LOGIN_USER_CHANGED,
                 content::NotificationService::AllSources());
  registrar_.Add(this, chrome::NOTIFICATION_LOGIN_OR_LOCK_WEBUI_VISIBLE,
                 content::NotificationService::AllSources());
  registrar_.Add(this, chrome::NOTIFICATION_WALLPAPER_ANIMATION_FINISHED,
                 content::NotificationService::AllSources());
  sequence_token_ = BrowserThread::GetBlockingPool()->GetNamedSequenceToken(
      wallpaper::kWallpaperSequenceTokenName);
  task_runner_ =
      BrowserThread::GetBlockingPool()
          ->GetSequencedTaskRunnerWithShutdownBehavior(
              sequence_token_, base::SequencedWorkerPool::CONTINUE_ON_SHUTDOWN);

  user_manager::UserManager::Get()->AddSessionStateObserver(this);

  content::ServiceManagerConnection* connection =
      content::ServiceManagerConnection::GetForProcess();
  if (connection && connection->GetConnector()) {
    // Connect to the wallpaper controller interface in the ash service.
    ash::mojom::WallpaperControllerPtr wallpaper_controller_ptr;
    connection->GetConnector()->BindInterface(ash::mojom::kServiceName,
                                              &wallpaper_controller_ptr);
    // Register this object as the wallpaper picker.
    wallpaper_controller_ptr->SetWallpaperPicker(
        binding_.CreateInterfacePtrAndBind());
  }
}

WallpaperManager::PendingWallpaper* WallpaperManager::GetPendingWallpaper(
    const AccountId& account_id,
    bool delayed) {
  if (!pending_inactive_) {
    loading_.push_back(new WallpaperManager::PendingWallpaper(
        (delayed ? GetWallpaperLoadDelay()
                 : base::TimeDelta::FromMilliseconds(0)),
        account_id));
    pending_inactive_ = loading_.back().get();
  }
  return pending_inactive_;
}

void WallpaperManager::RemovePendingWallpaperFromList(
    PendingWallpaper* pending) {
  DCHECK(loading_.size() > 0);
  for (WallpaperManager::PendingList::iterator i = loading_.begin();
       i != loading_.end();
       ++i) {
    if (i->get() == pending) {
      loading_.erase(i);
      break;
    }
  }

  if (loading_.empty()) {
    for (auto& observer : observers_)
      observer.OnPendingListEmptyForTesting();
  }
}

void WallpaperManager::SetPolicyControlledWallpaper(
    const AccountId& account_id,
    std::unique_ptr<user_manager::UserImage> user_image) {
  if (!CanGetFilesId()) {
    CallWhenCanGetFilesId(
        base::Bind(&WallpaperManager::SetPolicyControlledWallpaper,
                   weak_factory_.GetWeakPtr(), account_id,
                   base::Passed(std::move(user_image))));
    return;
  }

  const wallpaper::WallpaperFilesId wallpaper_files_id = GetFilesId(account_id);

  if (!wallpaper_files_id.is_valid())
    LOG(FATAL) << "Wallpaper flies id if invalid!";

  SetCustomWallpaper(account_id, wallpaper_files_id, "policy-controlled.jpeg",
                     wallpaper::WALLPAPER_LAYOUT_CENTER_CROPPED,
                     user_manager::User::POLICY, user_image->image(),
                     true /* update wallpaper */);
}

void WallpaperManager::OnDeviceWallpaperPolicyChanged() {
  SetDeviceWallpaperIfApplicable(
      user_manager::UserManager::Get()->IsUserLoggedIn()
          ? user_manager::UserManager::Get()->GetActiveUser()->GetAccountId()
          : user_manager::SignInAccountId());
}

void WallpaperManager::OnDeviceWallpaperExists(const AccountId& account_id,
                                               const std::string& url,
                                               const std::string& hash,
                                               bool exist) {
  if (exist) {
    base::PostTaskWithTraitsAndReplyWithResult(
        FROM_HERE, {base::MayBlock()},
        base::Bind(&CheckDeviceWallpaperMatchHash, GetDeviceWallpaperFilePath(),
                   hash),
        base::Bind(&WallpaperManager::OnCheckDeviceWallpaperMatchHash,
                   weak_factory_.GetWeakPtr(), account_id, url, hash));
  } else {
    GURL device_wallpaper_url(url);
    device_wallpaper_downloader_.reset(new CustomizationWallpaperDownloader(
        g_browser_process->system_request_context(), device_wallpaper_url,
        GetDeviceWallpaperDir(), GetDeviceWallpaperFilePath(),
        base::Bind(&WallpaperManager::OnDeviceWallpaperDownloaded,
                   weak_factory_.GetWeakPtr(), account_id, hash)));
    device_wallpaper_downloader_->Start();
  }
}

void WallpaperManager::OnDeviceWallpaperDownloaded(const AccountId& account_id,
                                                   const std::string& hash,
                                                   bool success,
                                                   const GURL& url) {
  if (!success) {
    LOG(ERROR) << "Failed to download the device wallpaper. Fallback to "
                  "default wallpaper.";
    SetDefaultWallpaperDelayed(account_id);
    return;
  }

  base::PostTaskWithTraitsAndReplyWithResult(
      FROM_HERE, {base::MayBlock()},
      base::Bind(&CheckDeviceWallpaperMatchHash, GetDeviceWallpaperFilePath(),
                 hash),
      base::Bind(&WallpaperManager::OnCheckDeviceWallpaperMatchHash,
                 weak_factory_.GetWeakPtr(), account_id, url.spec(), hash));
}

void WallpaperManager::OnCheckDeviceWallpaperMatchHash(
    const AccountId& account_id,
    const std::string& url,
    const std::string& hash,
    bool match) {
  if (!match) {
    if (retry_download_if_failed_) {
      // We only retry to download the device wallpaper one more time if the
      // hash doesn't match.
      retry_download_if_failed_ = false;
      GURL device_wallpaper_url(url);
      device_wallpaper_downloader_.reset(new CustomizationWallpaperDownloader(
          g_browser_process->system_request_context(), device_wallpaper_url,
          GetDeviceWallpaperDir(), GetDeviceWallpaperFilePath(),
          base::Bind(&WallpaperManager::OnDeviceWallpaperDownloaded,
                     weak_factory_.GetWeakPtr(), account_id, hash)));
      device_wallpaper_downloader_->Start();
    } else {
      LOG(ERROR) << "The device wallpaper hash doesn't match with provided "
                    "hash value. Fallback to default wallpaper! ";
      SetDefaultWallpaperDelayed(account_id);

      // Reset the boolean variable so that it can retry to download when the
      // next device wallpaper request comes in.
      retry_download_if_failed_ = true;
    }
    return;
  }

  user_image_loader::StartWithFilePath(
      task_runner_, GetDeviceWallpaperFilePath(),
      ImageDecoder::ROBUST_JPEG_CODEC,
      0,  // Do not crop.
      base::Bind(&WallpaperManager::OnDeviceWallpaperDecoded,
                 weak_factory_.GetWeakPtr(), account_id));
}

void WallpaperManager::OnDeviceWallpaperDecoded(
    const AccountId& account_id,
    std::unique_ptr<user_manager::UserImage> user_image) {
  WallpaperInfo wallpaper_info = {GetDeviceWallpaperFilePath().value(),
                                  wallpaper::WALLPAPER_LAYOUT_CENTER_CROPPED,
                                  user_manager::User::DEVICE,
                                  base::Time::Now().LocalMidnight()};
  DCHECK(!user_manager::UserManager::Get()->IsUserLoggedIn());
  // In the login screen set the device wallpaper as the wallpaper.
  GetPendingWallpaper(user_manager::SignInAccountId(), false)
      ->ResetSetWallpaperImage(user_image->image(), wallpaper_info);
}

void WallpaperManager::InitializeRegisteredDeviceWallpaper() {
  if (user_manager::UserManager::Get()->IsUserLoggedIn())
    return;

  bool disable_boot_animation =
      GetCommandLine()->HasSwitch(switches::kDisableBootAnimation);
  bool show_users = true;
  bool result = CrosSettings::Get()->GetBoolean(
      kAccountsPrefShowUserNamesOnSignIn, &show_users);
  DCHECK(result) << "Unable to fetch setting "
                 << kAccountsPrefShowUserNamesOnSignIn;
  const user_manager::UserList& users =
      user_manager::UserManager::Get()->GetUsers();
  int public_session_user_index = FindPublicSession(users);
  if ((!show_users && public_session_user_index == -1) || users.empty()) {
    // Boot into sign in form, preload default wallpaper.
    if (!SetDeviceWallpaperIfApplicable(user_manager::SignInAccountId()))
      SetDefaultWallpaperDelayed(user_manager::SignInAccountId());
    return;
  }

  if (!disable_boot_animation) {
    int index = public_session_user_index != -1 ? public_session_user_index : 0;
    // Normal boot, load user wallpaper.
    // If normal boot animation is disabled wallpaper would be set
    // asynchronously once user pods are loaded.
    SetUserWallpaperDelayed(users[index]->GetAccountId());
  }
}

bool WallpaperManager::GetUserWallpaperInfo(const AccountId& account_id,
                                            WallpaperInfo* info) const {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  if (user_manager::UserManager::Get()->IsUserNonCryptohomeDataEphemeral(
          account_id)) {
    // Default to the values cached in memory.
    *info = current_user_wallpaper_info_;

    // Ephemeral users do not save anything to local state. But we have got
    // wallpaper info from memory. Returns true.
    return true;
  }

  const base::DictionaryValue* info_dict;
  if (!g_browser_process->local_state()
           ->GetDictionary(wallpaper::kUsersWallpaperInfo)
           ->GetDictionaryWithoutPathExpansion(account_id.GetUserEmail(),
                                               &info_dict)) {
    return false;
  }

  // Use temporary variables to keep |info| untouched in the error case.
  std::string location;
  if (!info_dict->GetString(kNewWallpaperLocationNodeName, &location))
    return false;
  int layout;
  if (!info_dict->GetInteger(kNewWallpaperLayoutNodeName, &layout))
    return false;
  int type;
  if (!info_dict->GetInteger(kNewWallpaperTypeNodeName, &type))
    return false;
  std::string date_string;
  if (!info_dict->GetString(kNewWallpaperDateNodeName, &date_string))
    return false;
  int64_t date_val;
  if (!base::StringToInt64(date_string, &date_val))
    return false;

  info->location = location;
  info->layout = static_cast<wallpaper::WallpaperLayout>(layout);
  info->type = static_cast<user_manager::User::WallpaperType>(type);
  info->date = base::Time::FromInternalValue(date_val);
  return true;
}

bool WallpaperManager::ShouldSetDeviceWallpaper(const AccountId& account_id,
                                                std::string* url,
                                                std::string* hash) {
  // Only allow the device wallpaper for enterprise managed devices.
  if (!g_browser_process->platform_part()
           ->browser_policy_connector_chromeos()
           ->IsEnterpriseManaged()) {
    return false;
  }

  const base::DictionaryValue* dict = nullptr;
  if (!CrosSettings::Get()->GetDictionary(kDeviceWallpaperImage, &dict) ||
      !dict->GetStringWithoutPathExpansion("url", url) ||
      !dict->GetStringWithoutPathExpansion("hash", hash)) {
    return false;
  }

  // Only set the device wallpaper if we're at the login screen.
  if (user_manager::UserManager::Get()->IsUserLoggedIn())
    return false;

  return true;
}

base::FilePath WallpaperManager::GetDeviceWallpaperDir() {
  base::FilePath wallpaper_dir;
  if (!PathService::Get(chrome::DIR_CHROMEOS_WALLPAPERS, &wallpaper_dir)) {
    LOG(ERROR) << "Unable to get wallpaper dir.";
    return base::FilePath();
  }
  return wallpaper_dir.Append(kDeviceWallpaperDir);
}

base::FilePath WallpaperManager::GetDeviceWallpaperFilePath() {
  return GetDeviceWallpaperDir().Append(kDeviceWallpaperFile);
}

void WallpaperManager::OnWallpaperDecoded(
    const AccountId& account_id,
    wallpaper::WallpaperLayout layout,
    bool update_wallpaper,
    MovableOnDestroyCallbackHolder on_finish,
    std::unique_ptr<user_manager::UserImage> user_image) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  TRACE_EVENT_ASYNC_END0("ui", "LoadAndDecodeWallpaper", this);

  // If decoded wallpaper is empty, we have probably failed to decode the file.
  // Use default wallpaper in this case.
  if (user_image->image().isNull()) {
    // Updates user pref to default wallpaper.
    WallpaperInfo info = {"",
                          wallpaper::WALLPAPER_LAYOUT_CENTER_CROPPED,
                          user_manager::User::DEFAULT,
                          base::Time::Now().LocalMidnight()};
    SetUserWallpaperInfo(account_id, info, true);

    if (update_wallpaper)
      DoSetDefaultWallpaper(account_id, std::move(on_finish));
    return;
  }

  // Update the image, but keep the path which was set earlier.
  wallpaper_cache_[account_id].second = user_image->image();

  if (update_wallpaper)
    SetWallpaper(user_image->image(), layout);
}

void WallpaperManager::StartLoad(const AccountId& account_id,
                                 const WallpaperInfo& info,
                                 bool update_wallpaper,
                                 const base::FilePath& wallpaper_path,
                                 MovableOnDestroyCallbackHolder on_finish) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  TRACE_EVENT_ASYNC_BEGIN0("ui", "LoadAndDecodeWallpaper", this);
  if (update_wallpaper) {
    // We are now about to change the wallpaper, so update the path and remove
    // the existing image.
    wallpaper_cache_[account_id] =
        CustomWallpaperElement(wallpaper_path, gfx::ImageSkia());
  }
  user_image_loader::StartWithFilePath(
      task_runner_, wallpaper_path, ImageDecoder::ROBUST_JPEG_CODEC,
      0,  // Do not crop.
      base::Bind(&WallpaperManager::OnWallpaperDecoded,
                 weak_factory_.GetWeakPtr(), account_id, info.layout,
                 update_wallpaper, base::Passed(std::move(on_finish))));
}

void WallpaperManager::SetCustomizedDefaultWallpaperAfterCheck(
    const GURL& wallpaper_url,
    const base::FilePath& downloaded_file,
    std::unique_ptr<CustomizedWallpaperRescaledFiles> rescaled_files) {
  PrefService* pref_service = g_browser_process->local_state();

  std::string current_url =
      pref_service->GetString(prefs::kCustomizationDefaultWallpaperURL);
  if (current_url != wallpaper_url.spec() || !rescaled_files->AllSizesExist()) {
    DCHECK(rescaled_files->downloaded_exists());

    // Either resized images do not exist or cached version is incorrect.
    // Need to start resize again.
    user_image_loader::StartWithFilePath(
        task_runner_, downloaded_file, ImageDecoder::ROBUST_JPEG_CODEC,
        0,  // Do not crop.
        base::Bind(&WallpaperManager::OnCustomizedDefaultWallpaperDecoded,
                   weak_factory_.GetWeakPtr(), wallpaper_url,
                   base::Passed(std::move(rescaled_files))));
  } else {
    SetDefaultWallpaperPath(rescaled_files->path_rescaled_small(),
                            std::unique_ptr<gfx::ImageSkia>(),
                            rescaled_files->path_rescaled_large(),
                            std::unique_ptr<gfx::ImageSkia>());
  }
}

void WallpaperManager::OnCustomizedDefaultWallpaperResized(
    const GURL& wallpaper_url,
    std::unique_ptr<CustomizedWallpaperRescaledFiles> rescaled_files,
    std::unique_ptr<bool> success,
    std::unique_ptr<gfx::ImageSkia> small_wallpaper_image,
    std::unique_ptr<gfx::ImageSkia> large_wallpaper_image) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  DCHECK(rescaled_files);
  DCHECK(success.get());
  if (!*success) {
    LOG(WARNING) << "Failed to save resized customized default wallpaper";
    return;
  }
  PrefService* pref_service = g_browser_process->local_state();
  pref_service->SetString(prefs::kCustomizationDefaultWallpaperURL,
                          wallpaper_url.spec());
  SetDefaultWallpaperPath(
      rescaled_files->path_rescaled_small(), std::move(small_wallpaper_image),
      rescaled_files->path_rescaled_large(), std::move(large_wallpaper_image));
  VLOG(1) << "Customized default wallpaper applied.";
}

size_t WallpaperManager::GetPendingListSizeForTesting() const {
  return loading_.size();
}

wallpaper::WallpaperFilesId WallpaperManager::GetFilesId(
    const AccountId& account_id) const {
  std::string stored_value;
  if (user_manager::known_user::GetStringPref(account_id, kWallpaperFilesId,
                                              &stored_value)) {
    return wallpaper::WallpaperFilesId::FromString(stored_value);
  }
  const std::string& old_id = account_id.GetUserEmail();  // Migrated
  const wallpaper::WallpaperFilesId files_id = HashWallpaperFilesIdStr(old_id);
  SetKnownUserWallpaperFilesId(account_id, files_id);
  return files_id;
}

bool WallpaperManager::SetDeviceWallpaperIfApplicable(
    const AccountId& account_id) {
  std::string url;
  std::string hash;
  if (ShouldSetDeviceWallpaper(account_id, &url, &hash)) {
    // Check if the device wallpaper exists and matches the hash. If so, use it
    // directly. Otherwise download it first.
    base::PostTaskWithTraitsAndReplyWithResult(
        FROM_HERE, {base::MayBlock()},
        base::Bind(&base::PathExists, GetDeviceWallpaperFilePath()),
        base::Bind(&WallpaperManager::OnDeviceWallpaperExists,
                   weak_factory_.GetWeakPtr(), account_id, url, hash));
    return true;
  }
  return false;
}

void WallpaperManager::UserChangedChildStatus(user_manager::User* user) {
  SetUserWallpaperNow(user->GetAccountId());
}

void WallpaperManager::SetDefaultWallpaperPathsFromCommandLine(
    base::CommandLine* command_line) {
  default_small_wallpaper_file_ = command_line->GetSwitchValuePath(
      chromeos::switches::kDefaultWallpaperSmall);
  default_large_wallpaper_file_ = command_line->GetSwitchValuePath(
      chromeos::switches::kDefaultWallpaperLarge);
  guest_small_wallpaper_file_ = command_line->GetSwitchValuePath(
      chromeos::switches::kGuestWallpaperSmall);
  guest_large_wallpaper_file_ = command_line->GetSwitchValuePath(
      chromeos::switches::kGuestWallpaperLarge);
  child_small_wallpaper_file_ = command_line->GetSwitchValuePath(
      chromeos::switches::kChildWallpaperSmall);
  child_large_wallpaper_file_ = command_line->GetSwitchValuePath(
      chromeos::switches::kChildWallpaperLarge);
  default_wallpaper_image_.reset();
}

void WallpaperManager::OnDefaultWallpaperDecoded(
    const base::FilePath& path,
    const wallpaper::WallpaperLayout layout,
    std::unique_ptr<user_manager::UserImage>* result_out,
    MovableOnDestroyCallbackHolder on_finish,
    std::unique_ptr<user_manager::UserImage> user_image) {
  *result_out = std::move(user_image);
  SetWallpaper((*result_out)->image(), layout);
}

void WallpaperManager::StartLoadAndSetDefaultWallpaper(
    const base::FilePath& path,
    const wallpaper::WallpaperLayout layout,
    MovableOnDestroyCallbackHolder on_finish,
    std::unique_ptr<user_manager::UserImage>* result_out) {
  user_image_loader::StartWithFilePath(
      task_runner_, path, ImageDecoder::ROBUST_JPEG_CODEC,
      0,  // Do not crop.
      base::Bind(&WallpaperManager::OnDefaultWallpaperDecoded,
                 weak_factory_.GetWeakPtr(), path, layout,
                 base::Unretained(result_out),
                 base::Passed(std::move(on_finish))));
}

void WallpaperManager::SetDefaultWallpaperPath(
    const base::FilePath& default_small_wallpaper_file,
    std::unique_ptr<gfx::ImageSkia> small_wallpaper_image,
    const base::FilePath& default_large_wallpaper_file,
    std::unique_ptr<gfx::ImageSkia> large_wallpaper_image) {
  default_small_wallpaper_file_ = default_small_wallpaper_file;
  default_large_wallpaper_file_ = default_large_wallpaper_file;

  ash::WallpaperController* controller =
      ash::Shell::Get()->wallpaper_controller();

  // |need_update_screen| is true if the previous default wallpaper is visible
  // now, so we need to update wallpaper on the screen.
  //
  // Layout is ignored here, so wallpaper::WALLPAPER_LAYOUT_CENTER is used
  // as a placeholder only.
  const bool need_update_screen =
      default_wallpaper_image_.get() &&
      controller->WallpaperIsAlreadyLoaded(default_wallpaper_image_->image(),
                                           false /* compare_layouts */,
                                           wallpaper::WALLPAPER_LAYOUT_CENTER);

  default_wallpaper_image_.reset();
  if (GetAppropriateResolution() == WALLPAPER_RESOLUTION_SMALL) {
    if (small_wallpaper_image) {
      default_wallpaper_image_.reset(
          new user_manager::UserImage(*small_wallpaper_image));
      default_wallpaper_image_->set_file_path(default_small_wallpaper_file);
    }
  } else {
    if (large_wallpaper_image) {
      default_wallpaper_image_.reset(
          new user_manager::UserImage(*large_wallpaper_image));
      default_wallpaper_image_->set_file_path(default_large_wallpaper_file);
    }
  }

  if (need_update_screen)
    DoSetDefaultWallpaper(EmptyAccountId(), MovableOnDestroyCallbackHolder());
}

void WallpaperManager::RecordWallpaperAppType() {
  user_manager::User* user = user_manager::UserManager::Get()->GetActiveUser();
  Profile* profile = ProfileHelper::Get()->GetProfileByUser(user);

  UMA_HISTOGRAM_ENUMERATION(
      "Ash.Wallpaper.Apps",
      wallpaper_manager_util::ShouldUseAndroidWallpapersApp(profile)
          ? WALLPAPERS_APP_ANDROID
          : WALLPAPERS_PICKER_APP_CHROMEOS,
      WALLPAPERS_APPS_NUM);
}

}  // namespace chromeos
