// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/extension_uninstall_dialog.h"

#include "base/bind.h"
#include "base/logging.h"
#include "base/message_loop/message_loop.h"
#include "base/metrics/histogram_macros.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/extensions/extension_util.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_navigator.h"
#include "chrome/browser/ui/browser_navigator_params.h"
#include "chrome/grit/generated_resources.h"
#include "extensions/browser/extension_dialog_auto_confirm.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/extension_system.h"
#include "extensions/browser/image_loader.h"
#include "extensions/common/constants.h"
#include "extensions/common/extension.h"
#include "extensions/common/extension_icon_set.h"
#include "extensions/common/extension_resource.h"
#include "extensions/common/extension_urls.h"
#include "extensions/common/manifest_handlers/icons_handler.h"
#include "extensions/common/manifest_url_handlers.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/window_open_disposition.h"
#include "ui/gfx/image/image.h"
#include "ui/gfx/image/image_skia.h"

namespace extensions {

namespace {

const char kExtensionRemovedError[] =
    "Extension was removed before dialog closed.";

const char kReferrerId[] = "chrome-remove-extension-dialog";

// Returns gfx::ImageSkia for the default icon.
gfx::ImageSkia GetDefaultIconImage(bool is_app) {
  return  is_app ? util::GetDefaultAppIcon() : util::GetDefaultExtensionIcon();
}

}  // namespace

ExtensionUninstallDialog::ExtensionUninstallDialog(
    Profile* profile,
    ExtensionUninstallDialog::Delegate* delegate)
    : profile_(profile),
      delegate_(delegate),
      uninstall_reason_(UNINSTALL_REASON_FOR_TESTING) {
}

ExtensionUninstallDialog::~ExtensionUninstallDialog() {
}

void ExtensionUninstallDialog::ConfirmUninstallByExtension(
    const scoped_refptr<const Extension>& extension,
    const scoped_refptr<const Extension>& triggering_extension,
    UninstallReason reason,
    UninstallSource source) {
  triggering_extension_ = triggering_extension;
  ConfirmUninstall(extension, reason, source);
}

void ExtensionUninstallDialog::ConfirmUninstall(
    const scoped_refptr<const Extension>& extension,
    UninstallReason reason,
    UninstallSource source) {
  DCHECK(thread_checker_.CalledOnValidThread());

  UMA_HISTOGRAM_ENUMERATION("Extensions.UninstallSource", source,
                            NUM_UNINSTALL_SOURCES);

  extension_ = extension;
  uninstall_reason_ = reason;
  // Bookmark apps may not have 128x128 icons so accept 64x64 icons.
  const int icon_size = extension_->from_bookmark()
                            ? extension_misc::EXTENSION_ICON_SMALL * 2
                            : extension_misc::EXTENSION_ICON_LARGE;
  ExtensionResource image = IconsInfo::GetIconResource(
      extension_.get(), icon_size, ExtensionIconSet::MATCH_BIGGER);

  // Load the image asynchronously. The response will be sent to OnImageLoaded.
  ImageLoader* loader = ImageLoader::Get(profile_);

  SetIcon(gfx::Image());
  std::vector<ImageLoader::ImageRepresentation> images_list;
  images_list.push_back(ImageLoader::ImageRepresentation(
      image,
      ImageLoader::ImageRepresentation::NEVER_RESIZE,
      gfx::Size(),
      ui::SCALE_FACTOR_100P));
  loader->LoadImagesAsync(extension_.get(), images_list,
                          base::Bind(&ExtensionUninstallDialog::OnImageLoaded,
                                     AsWeakPtr(), extension_->id()));
}

void ExtensionUninstallDialog::SetIcon(const gfx::Image& image) {
  if (image.IsEmpty()) {
    icon_ = GetDefaultIconImage(extension_->is_app());
  } else {
    icon_ = *image.ToImageSkia();
  }
}

void ExtensionUninstallDialog::OnImageLoaded(const std::string& extension_id,
                                             const gfx::Image& image) {
  const Extension* target_extension =
      ExtensionRegistry::Get(profile_)
          ->GetExtensionById(extension_id, ExtensionRegistry::EVERYTHING);
  if (!target_extension) {
    delegate_->OnExtensionUninstallDialogClosed(
        false, base::ASCIIToUTF16(kExtensionRemovedError));
    return;
  }

  SetIcon(image);

  switch (ScopedTestDialogAutoConfirm::GetAutoConfirmValue()) {
    case ScopedTestDialogAutoConfirm::NONE:
      Show();
      break;
    case ScopedTestDialogAutoConfirm::ACCEPT_AND_OPTION:
      OnDialogClosed(CLOSE_ACTION_UNINSTALL_AND_REPORT_ABUSE);
      break;
    case ScopedTestDialogAutoConfirm::ACCEPT:
      OnDialogClosed(CLOSE_ACTION_UNINSTALL);
      break;
    case ScopedTestDialogAutoConfirm::CANCEL:
      OnDialogClosed(CLOSE_ACTION_CANCELED);
      break;
  }
}

std::string ExtensionUninstallDialog::GetHeadingText() {
  if (triggering_extension_) {
    return l10n_util::GetStringFUTF8(
        IDS_EXTENSION_PROGRAMMATIC_UNINSTALL_PROMPT_HEADING,
        base::UTF8ToUTF16(triggering_extension_->name()),
        base::UTF8ToUTF16(extension_->name()));
  }
  return l10n_util::GetStringFUTF8(IDS_EXTENSION_UNINSTALL_PROMPT_HEADING,
                                   base::UTF8ToUTF16(extension_->name()));
}

bool ExtensionUninstallDialog::ShouldShowReportAbuseCheckbox() const {
  return ManifestURL::UpdatesFromGallery(extension_.get());
}

void ExtensionUninstallDialog::OnDialogClosed(CloseAction action) {
  // We don't want to artificially weight any of the options, so only record if
  // reporting abuse was available.
  if (ShouldShowReportAbuseCheckbox()) {
    UMA_HISTOGRAM_ENUMERATION("Extensions.UninstallDialogAction",
                              action,
                              CLOSE_ACTION_LAST);
  }

  bool success = false;
  base::string16 error;
  switch (action) {
    case CLOSE_ACTION_UNINSTALL_AND_REPORT_ABUSE:
      // If the extension specifies a custom uninstall page via
      // chrome.runtime.setUninstallURL, then at uninstallation its uninstall
      // page opens. To ensure that the CWS Report Abuse page is the active tab
      // at uninstallation, HandleReportAbuse() is called after Uninstall().
      success = Uninstall(&error);
      HandleReportAbuse();
      break;
    case CLOSE_ACTION_UNINSTALL: {
      success = Uninstall(&error);
      break;
    }
    case CLOSE_ACTION_CANCELED:
      error = base::ASCIIToUTF16("User canceled uninstall dialog");
      break;
    case CLOSE_ACTION_LAST:
      NOTREACHED();
  }
  delegate_->OnExtensionUninstallDialogClosed(success, error);
}

bool ExtensionUninstallDialog::Uninstall(base::string16* error) {
  const Extension* current_extension =
      ExtensionRegistry::Get(profile_)->GetExtensionById(
          extension_->id(), ExtensionRegistry::EVERYTHING);
  if (current_extension) {
    return ExtensionSystem::Get(profile_)
        ->extension_service()
        ->UninstallExtension(extension_->id(), uninstall_reason_,
                             base::Bind(&base::DoNothing), error);
  }
  *error = base::ASCIIToUTF16(kExtensionRemovedError);
  return false;
}

void ExtensionUninstallDialog::HandleReportAbuse() {
  chrome::NavigateParams params(
      profile_,
      extension_urls::GetWebstoreReportAbuseUrl(extension_->id(), kReferrerId),
      ui::PAGE_TRANSITION_LINK);
  params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
  chrome::Navigate(&params);
}

}  // namespace extensions
