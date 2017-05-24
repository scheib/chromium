// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/settings/chromeos/fingerprint_handler.h"

#include <algorithm>

#include "base/bind.h"
#include "base/memory/ptr_util.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/chromeos/profiles/profile_helper.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/pref_names.h"
#include "chrome/grit/generated_resources.h"
#include "components/prefs/pref_service.h"
#include "components/session_manager/core/session_manager.h"
#include "content/public/common/service_manager_connection.h"
#include "services/device/public/interfaces/constants.mojom.h"
#include "services/service_manager/public/cpp/connector.h"
#include "ui/base/l10n/l10n_util.h"

using session_manager::SessionManager;
using session_manager::SessionState;

namespace chromeos {
namespace settings {
namespace {

// The max number of fingerprints that can be stored.
const int kMaxAllowedFingerprints = 5;

std::unique_ptr<base::DictionaryValue> GetFingerprintsInfo(
    const std::vector<std::string>& fingerprints_list) {
  auto response = base::MakeUnique<base::DictionaryValue>();
  auto fingerprints = base::MakeUnique<base::ListValue>();

  DCHECK_LE(static_cast<int>(fingerprints_list.size()),
            kMaxAllowedFingerprints);
  for (auto& fingerprint_name: fingerprints_list) {
    std::unique_ptr<base::Value> str =
        base::MakeUnique<base::Value>(fingerprint_name);
    fingerprints->Append(std::move(str));
  }

  response->Set("fingerprintsList", std::move(fingerprints));
  response->SetBoolean("isMaxed", static_cast<int>(fingerprints_list.size()) >=
                                      kMaxAllowedFingerprints);
  return response;
}

}  // namespace

FingerprintHandler::FingerprintHandler(Profile* profile)
    : profile_(profile), binding_(this), weak_ptr_factory_(this) {
  service_manager::Connector* connector =
      content::ServiceManagerConnection::GetForProcess()->GetConnector();
  connector->BindInterface(device::mojom::kServiceName, &fp_service_);
  fp_service_->AddFingerprintObserver(binding_.CreateInterfacePtrAndBind());
  user_id_ = ProfileHelper::Get()->GetUserIdHashFromProfile(profile);
  // SessionManager may not exist in some tests.
  if (SessionManager::Get())
    SessionManager::Get()->AddObserver(this);
}

FingerprintHandler::~FingerprintHandler() {
  if (SessionManager::Get())
    SessionManager::Get()->RemoveObserver(this);
}

void FingerprintHandler::RegisterMessages() {
  web_ui()->RegisterMessageCallback(
      "getFingerprintsList",
      base::Bind(&FingerprintHandler::HandleGetFingerprintsList,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "getNumFingerprints",
      base::Bind(&FingerprintHandler::HandleGetNumFingerprints,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "startEnroll",
      base::Bind(&FingerprintHandler::HandleStartEnroll,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "cancelCurrentEnroll",
      base::Bind(&FingerprintHandler::HandleCancelCurrentEnroll,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "getEnrollmentLabel",
      base::Bind(&FingerprintHandler::HandleGetEnrollmentLabel,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "removeEnrollment",
      base::Bind(&FingerprintHandler::HandleRemoveEnrollment,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "changeEnrollmentLabel",
      base::Bind(&FingerprintHandler::HandleChangeEnrollmentLabel,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "startAuthentication",
      base::Bind(&FingerprintHandler::HandleStartAuthentication,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "endCurrentAuthentication",
      base::Bind(&FingerprintHandler::HandleEndCurrentAuthentication,
                 base::Unretained(this)));
}

void FingerprintHandler::OnJavascriptAllowed() {}

void FingerprintHandler::OnJavascriptDisallowed() {}

void FingerprintHandler::OnRestarted() {}

void FingerprintHandler::OnEnrollScanDone(uint32_t scan_result,
                                          bool enroll_session_complete,
                                          int percent_complete) {
  AllowJavascript();
  auto scan_attempt = base::MakeUnique<base::DictionaryValue>();
  scan_attempt->SetInteger("result", scan_result);
  scan_attempt->SetBoolean("isComplete", enroll_session_complete);
  scan_attempt->SetInteger("percentComplete", percent_complete);

  FireWebUIListener("on-fingerprint-scan-received", *scan_attempt);
}

void FingerprintHandler::OnAuthScanDone(
    uint32_t scan_result,
    const std::unordered_map<std::string, std::vector<std::string>>& matches) {
  if (SessionManager::Get()->session_state() == SessionState::LOCKED)
    return;

  // When the user touches the sensor, highlight the label(s) that finger is
  // associated with, if it is registered with this user.
  auto it = matches.find(user_id_);
  if (it == matches.end() || it->second.size() < 1)
    return;

  AllowJavascript();
  auto fingerprint_ids = base::MakeUnique<base::ListValue>();

  for (const std::string& matched_path : it->second) {
    auto path_it = std::find(fingerprints_paths_.begin(),
                             fingerprints_paths_.end(), matched_path);
    DCHECK(path_it != fingerprints_paths_.end());
    fingerprint_ids->AppendInteger(
        static_cast<int>(path_it - fingerprints_paths_.begin()));
  }

  auto fingerprint_attempt = base::MakeUnique<base::DictionaryValue>();
  fingerprint_attempt->SetInteger("result", scan_result);
  fingerprint_attempt->Set("indexes", std::move(fingerprint_ids));

  FireWebUIListener("on-fingerprint-attempt-received", *fingerprint_attempt);
}

void FingerprintHandler::OnSessionFailed() {}

void FingerprintHandler::OnSessionStateChanged() {
  SessionState state = SessionManager::Get()->session_state();

  AllowJavascript();
  FireWebUIListener("on-screen-locked",
                    base::Value(state == SessionState::LOCKED));
}

void FingerprintHandler::HandleGetFingerprintsList(
    const base::ListValue* args) {
  CHECK_EQ(1U, args->GetSize());
  std::string callback_id;
  CHECK(args->GetString(0, &callback_id));

  fp_service_->GetRecordsForUser(
      user_id_, base::Bind(&FingerprintHandler::OnGetFingerprintsList,
                           weak_ptr_factory_.GetWeakPtr(), callback_id));
}

void FingerprintHandler::OnGetFingerprintsList(
    const std::string& callback_id,
    const std::unordered_map<std::string, std::string>&
        fingerprints_list_mapping) {
  AllowJavascript();
  fingerprints_labels_.clear();
  fingerprints_paths_.clear();
  for (auto it = fingerprints_list_mapping.begin();
       it != fingerprints_list_mapping.end(); ++it) {
    fingerprints_paths_.push_back(it->first);
    fingerprints_labels_.push_back(it->second);
  }

  profile_->GetPrefs()->SetInteger(prefs::kQuickUnlockFingerprintRecord,
                                   fingerprints_list_mapping.size());

  std::unique_ptr<base::DictionaryValue> fingerprint_info =
      GetFingerprintsInfo(fingerprints_labels_);
  ResolveJavascriptCallback(base::Value(callback_id), *fingerprint_info);
}

void FingerprintHandler::HandleGetNumFingerprints(const base::ListValue* args) {
  AllowJavascript();

  CHECK_EQ(1U, args->GetSize());
  std::string callback_id;
  CHECK(args->GetString(0, &callback_id));

  int fingerprints_num =
      profile_->GetPrefs()->GetInteger(prefs::kQuickUnlockFingerprintRecord);

  ResolveJavascriptCallback(base::Value(callback_id),
                            base::Value(fingerprints_num));
}

void FingerprintHandler::HandleStartEnroll(const base::ListValue* args) {
  // Determines what the newly added fingerprint's name should be.
  for (int i = 1; i <= kMaxAllowedFingerprints; ++i) {
    std::string fingerprint_name = l10n_util::GetStringFUTF8(
        IDS_SETTINGS_PEOPLE_LOCK_SCREEN_NEW_FINGERPRINT_DEFAULT_NAME,
        base::IntToString16(i));
    if (std::find(fingerprints_labels_.begin(), fingerprints_labels_.end(),
                  fingerprint_name) == fingerprints_labels_.end()) {
      fp_service_->StartEnrollSession(user_id_, fingerprint_name);
      break;
    }
  }
}

void FingerprintHandler::HandleCancelCurrentEnroll(
    const base::ListValue* args) {
  fp_service_->CancelCurrentEnrollSession(
      base::Bind(&FingerprintHandler::OnCancelCurrentEnrollSession,
                 weak_ptr_factory_.GetWeakPtr()));
}

void FingerprintHandler::OnCancelCurrentEnrollSession(bool success) {
  if (!success)
    LOG(ERROR) << "Failed to cancel current fingerprint enroll session.";
}

void FingerprintHandler::HandleGetEnrollmentLabel(const base::ListValue* args) {
  CHECK_EQ(2U, args->GetSize());
  std::string callback_id;
  int index;
  CHECK(args->GetString(0, &callback_id));
  CHECK(args->GetInteger(1, &index));

  DCHECK_LT(index, static_cast<int>(fingerprints_labels_.size()));
  fp_service_->RequestRecordLabel(
      fingerprints_paths_[index],
      base::Bind(&FingerprintHandler::OnRequestRecordLabel,
                 weak_ptr_factory_.GetWeakPtr(), callback_id));
}

void FingerprintHandler::OnRequestRecordLabel(const std::string& callback_id,
                                              const std::string& label) {
  AllowJavascript();
  ResolveJavascriptCallback(base::Value(callback_id), base::Value(label));
}

void FingerprintHandler::HandleRemoveEnrollment(const base::ListValue* args) {
  CHECK_EQ(2U, args->GetSize());
  std::string callback_id;
  int index;
  CHECK(args->GetString(0, &callback_id));
  CHECK(args->GetInteger(1, &index));

  DCHECK_LT(index, static_cast<int>(fingerprints_paths_.size()));
  fp_service_->RemoveRecord(
      fingerprints_paths_[index],
      base::Bind(&FingerprintHandler::OnRemoveRecord,
                 weak_ptr_factory_.GetWeakPtr(), callback_id));
}

void FingerprintHandler::OnRemoveRecord(const std::string& callback_id,
                                        bool success) {
  if (!success)
    LOG(ERROR) << "Failed to remove fingerprint record.";
  AllowJavascript();
  ResolveJavascriptCallback(base::Value(callback_id), base::Value(success));
}

void FingerprintHandler::HandleChangeEnrollmentLabel(
    const base::ListValue* args) {
  CHECK_EQ(3U, args->GetSize());
  std::string callback_id;
  int index;
  std::string new_label;

  CHECK(args->GetString(0, &callback_id));
  CHECK(args->GetInteger(1, &index));
  CHECK(args->GetString(2, &new_label));

  fp_service_->SetRecordLabel(
      new_label, fingerprints_paths_[index],
      base::Bind(&FingerprintHandler::OnSetRecordLabel,
                 weak_ptr_factory_.GetWeakPtr(), callback_id));
}

void FingerprintHandler::OnSetRecordLabel(const std::string& callback_id,
                                          bool success) {
  if (!success)
    LOG(ERROR) << "Failed to set fingerprint record label.";
  AllowJavascript();
  ResolveJavascriptCallback(base::Value(callback_id), base::Value(success));
}

void FingerprintHandler::HandleStartAuthentication(
    const base::ListValue* args) {
  fp_service_->StartAuthSession();
}

void FingerprintHandler::HandleEndCurrentAuthentication(
    const base::ListValue* args) {
  fp_service_->EndCurrentAuthSession(
      base::Bind(&FingerprintHandler::OnEndCurrentAuthSession,
                 weak_ptr_factory_.GetWeakPtr()));
}

void FingerprintHandler::OnEndCurrentAuthSession(bool success) {
  if (!success)
    LOG(ERROR) << "Failed to end current fingerprint authentication session.";
}

}  // namespace settings
}  // namespace chromeos
