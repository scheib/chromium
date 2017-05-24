// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/signin/sync_confirmation_ui.h"

#include "base/memory/ptr_util.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sync/profile_sync_service_factory.h"
#include "chrome/browser/ui/webui/signin/sync_confirmation_handler.h"
#include "chrome/common/url_constants.h"
#include "chrome/grit/browser_resources.h"
#include "chrome/grit/generated_resources.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"
#include "ui/base/webui/web_ui_util.h"

SyncConfirmationUI::SyncConfirmationUI(content::WebUI* web_ui)
    : SigninWebDialogUI(web_ui) {
  Profile* profile = Profile::FromWebUI(web_ui);
  bool is_sync_allowed = profile->IsSyncAllowed();

  content::WebUIDataSource* source =
      content::WebUIDataSource::Create(chrome::kChromeUISyncConfirmationHost);
  source->SetJsonPath("strings.js");
  source->SetDefaultResource(IDR_SYNC_CONFIRMATION_HTML);
  source->AddResourcePath("sync_confirmation.css", IDR_SYNC_CONFIRMATION_CSS);
  source->AddResourcePath("sync_confirmation.js", IDR_SYNC_CONFIRMATION_JS);
  source->AddResourcePath("signin_shared_css.html", IDR_SIGNIN_SHARED_CSS_HTML);
  source->AddBoolean("isSyncAllowed", is_sync_allowed);

  source->AddLocalizedString("syncConfirmationChromeSyncTitle",
      IDS_SYNC_CONFIRMATION_CHROME_SYNC_TITLE);
  source->AddLocalizedString("syncConfirmationChromeSyncBody",
      IDS_SYNC_CONFIRMATION_CHROME_SYNC_MESSAGE);
  source->AddLocalizedString("syncConfirmationPersonalizeServicesTitle",
      IDS_SYNC_CONFIRMATION_PERSONALIZE_SERVICES_TITLE);
  source->AddLocalizedString("syncConfirmationPersonalizeServicesBody",
      IDS_SYNC_CONFIRMATION_PERSONALIZE_SERVICES_BODY);
  source->AddLocalizedString("syncConfirmationSyncSettingsLabel",
                             IDS_SYNC_CONFIRMATION_SYNC_SETTINGS_LABEL);
  source->AddLocalizedString("syncDisabledConfirmationDetails",
                             IDS_SYNC_DISABLED_CONFIRMATION_DETAILS);

  int title_ids = IDS_SYNC_CONFIRMATION_TITLE;
  int confirm_button_ids = IDS_SYNC_CONFIRMATION_CONFIRM_BUTTON_LABEL;
  int undo_button_ids = IDS_SYNC_CONFIRMATION_UNDO_BUTTON_LABEL;
  if (!is_sync_allowed) {
    title_ids = IDS_SYNC_DISABLED_CONFIRMATION_CHROME_SYNC_TITLE;
    confirm_button_ids = IDS_SYNC_DISABLED_CONFIRMATION_CONFIRM_BUTTON_LABEL;
    undo_button_ids = IDS_SYNC_DISABLED_CONFIRMATION_UNDO_BUTTON_LABEL;
  }
  source->AddLocalizedString("syncConfirmationTitle", title_ids);
  source->AddLocalizedString("syncConfirmationConfirmLabel",
                             confirm_button_ids);
  source->AddLocalizedString("syncConfirmationUndoLabel", undo_button_ids);

  base::DictionaryValue strings;
  webui::SetLoadTimeDataDefaults(
      g_browser_process->GetApplicationLocale(), &strings);
  source->AddLocalizedStrings(strings);

  content::WebUIDataSource::Add(profile, source);
}

void SyncConfirmationUI::InitializeMessageHandlerWithBrowser(Browser* browser) {
  web_ui()->AddMessageHandler(
      base::MakeUnique<SyncConfirmationHandler>(browser));
}
