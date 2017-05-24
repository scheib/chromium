// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/settings/safe_browsing_handler.h"

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/values.h"
#include "components/prefs/pref_service.h"
#include "components/safe_browsing/common/safe_browsing_prefs.h"
#include "content/public/browser/web_ui.h"

namespace settings {

SafeBrowsingHandler::SafeBrowsingHandler(PrefService* prefs) : prefs_(prefs) {}
SafeBrowsingHandler::~SafeBrowsingHandler() {}

void SafeBrowsingHandler::RegisterMessages() {
  web_ui()->RegisterMessageCallback(
      "getSafeBrowsingExtendedReporting",
      base::Bind(&SafeBrowsingHandler::HandleGetSafeBrowsingExtendedReporting,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "setSafeBrowsingExtendedReportingEnabled",
      base::Bind(
          &SafeBrowsingHandler::HandleSetSafeBrowsingExtendedReportingEnabled,
          base::Unretained(this)));
}

void SafeBrowsingHandler::OnJavascriptAllowed() {
  profile_pref_registrar_.Init(prefs_);
  profile_pref_registrar_.Add(
      prefs::kSafeBrowsingExtendedReportingEnabled,
      base::Bind(&SafeBrowsingHandler::OnPrefChanged, base::Unretained(this)));
  profile_pref_registrar_.Add(
      prefs::kSafeBrowsingScoutReportingEnabled,
      base::Bind(&SafeBrowsingHandler::OnPrefChanged, base::Unretained(this)));
}

void SafeBrowsingHandler::OnJavascriptDisallowed() {
  profile_pref_registrar_.RemoveAll();
}

void SafeBrowsingHandler::HandleGetSafeBrowsingExtendedReporting(
    const base::ListValue* args) {
  AllowJavascript();
  const base::Value* callback_id;
  CHECK(args->Get(0, &callback_id));
  base::Value is_enabled(safe_browsing::IsExtendedReportingEnabled(*prefs_));
  ResolveJavascriptCallback(*callback_id, is_enabled);
}

void SafeBrowsingHandler::HandleSetSafeBrowsingExtendedReportingEnabled(
    const base::ListValue* args) {
  bool enabled;
  CHECK(args->GetBoolean(0, &enabled));
  safe_browsing::SetExtendedReportingPrefAndMetric(
      prefs_, enabled, safe_browsing::SBER_OPTIN_SITE_CHROME_SETTINGS);
}

void SafeBrowsingHandler::OnPrefChanged(const std::string& pref_name) {
  DCHECK(pref_name == prefs::kSafeBrowsingExtendedReportingEnabled ||
         pref_name == prefs::kSafeBrowsingScoutReportingEnabled);

  base::Value is_enabled(safe_browsing::IsExtendedReportingEnabled(*prefs_));
  FireWebUIListener("safe-browsing-extended-reporting-change", is_enabled);
}

}  // namespace settings
