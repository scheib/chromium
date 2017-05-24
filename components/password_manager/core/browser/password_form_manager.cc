// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/password_manager/core/browser/password_form_manager.h"

#include <stddef.h>

#include <algorithm>
#include <map>
#include <utility>

#include "base/feature_list.h"
#include "base/memory/ptr_util.h"
#include "base/metrics/histogram_macros.h"
#include "base/metrics/user_metrics.h"
#include "base/stl_util.h"
#include "base/strings/string16.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/time/time.h"
#include "components/autofill/core/browser/autofill_manager.h"
#include "components/autofill/core/browser/proto/server.pb.h"
#include "components/autofill/core/browser/validation.h"
#include "components/autofill/core/common/password_form.h"
#include "components/password_manager/core/browser/affiliation_utils.h"
#include "components/password_manager/core/browser/browser_save_password_progress_logger.h"
#include "components/password_manager/core/browser/form_fetcher_impl.h"
#include "components/password_manager/core/browser/form_saver.h"
#include "components/password_manager/core/browser/log_manager.h"
#include "components/password_manager/core/browser/password_manager.h"
#include "components/password_manager/core/browser/password_manager_client.h"
#include "components/password_manager/core/browser/password_manager_driver.h"
#include "components/password_manager/core/browser/password_manager_metrics_util.h"
#include "components/password_manager/core/browser/password_manager_util.h"
#include "components/password_manager/core/browser/password_store.h"
#include "components/password_manager/core/browser/statistics_table.h"
#include "components/password_manager/core/common/password_manager_features.h"

using autofill::FormStructure;
using autofill::PasswordForm;
using autofill::PossibleUsernamePair;
using base::Time;

// Shorten the name to spare line breaks. The code provides enough context
// already.
typedef autofill::SavePasswordProgressLogger Logger;

namespace password_manager {

namespace {

std::vector<std::string> SplitPathToSegments(const std::string& path) {
  return base::SplitString(path, "/", base::TRIM_WHITESPACE,
                           base::SPLIT_WANT_ALL);
}

// Return false iff the strings are neither empty nor equal.
bool AreStringsEqualOrEmpty(const base::string16& s1,
                            const base::string16& s2) {
  return s1.empty() || s2.empty() || s1 == s2;
}

bool DoesStringContainOnlyDigits(const base::string16& s) {
  for (auto c : s) {
    if (!base::IsAsciiDigit(c))
      return false;
  }
  return true;
}

// Heuristics to determine that a string is very unlikely to be a username.
bool IsProbablyNotUsername(const base::string16& s) {
  return !s.empty() && DoesStringContainOnlyDigits(s) && s.size() < 3;
}

bool ShouldShowInitialPasswordAccountSuggestions() {
  return base::FeatureList::IsEnabled(
      password_manager::features::kFillOnAccountSelect);
}

// Update |credential| to reflect usage.
void UpdateMetadataForUsage(PasswordForm* credential) {
  ++credential->times_used;

  // Remove alternate usernames. At this point we assume that we have found
  // the right username.
  credential->other_possible_usernames.clear();
}

// Returns true iff |best_matches| contain a preferred credential with a
// username other than |preferred_username|.
bool DidPreferenceChange(
    const std::map<base::string16, const PasswordForm*>& best_matches,
    const base::string16& preferred_username) {
  for (const auto& key_value_pair : best_matches) {
    const PasswordForm& form = *key_value_pair.second;
    if (form.preferred && !form.is_public_suffix_match &&
        form.username_value != preferred_username) {
      return true;
    }
  }
  return false;
}

// Filter sensitive information, duplicates and |username_value| out from
// |form->other_possible_usernames|.
void SanitizePossibleUsernames(PasswordForm* form) {
  auto& usernames = form->other_possible_usernames;

  // Deduplicate.
  std::sort(usernames.begin(), usernames.end());
  usernames.erase(std::unique(usernames.begin(), usernames.end()),
                  usernames.end());

  // Filter out |form->username_value| and sensitive information.
  const base::string16& username_value = form->username_value;
  base::EraseIf(usernames, [&username_value](const PossibleUsernamePair& pair) {
    return pair.first == username_value ||
           autofill::IsValidCreditCardNumber(pair.first) ||
           autofill::IsSSN(pair.first);
  });
}

// Copies field properties masks from the form |from| to the form |to|.
void CopyFieldPropertiesMasks(const PasswordForm& from, PasswordForm* to) {
  // Skip copying if the number of fields is different.
  if (from.form_data.fields.size() != to->form_data.fields.size())
    return;

  for (size_t i = 0; i < from.form_data.fields.size(); ++i) {
    to->form_data.fields[i].properties_mask =
        to->form_data.fields[i].name == from.form_data.fields[i].name
            ? from.form_data.fields[i].properties_mask
            : autofill::FieldPropertiesFlags::ERROR_OCCURRED;
  }
}

// Sets autofill types of password and new password fields in |field_types|.
// |password_type| (the autofill type of new password field) should be equal to
// NEW_PASSWORD, PROBABLY_NEW_PASSWORD or NOT_NEW_PASSWORD. These values
// correspond to cases when the user confirmed password update, did nothing or
// declined to update password respectively.
void SetFieldLabelsOnUpdate(const autofill::ServerFieldType password_type,
                            const autofill::PasswordForm& submitted_form,
                            FieldTypeMap* field_types) {
  DCHECK(password_type == autofill::NEW_PASSWORD ||
         password_type == autofill::PROBABLY_NEW_PASSWORD ||
         password_type == autofill::NOT_NEW_PASSWORD)
      << password_type;
  DCHECK(!submitted_form.new_password_element.empty());

  (*field_types)[submitted_form.password_element] = autofill::PASSWORD;
  (*field_types)[submitted_form.new_password_element] = password_type;
}

// Sets the autofill type of the password field stored in |submitted_form| to
// |password_type| in |field_types| map.
void SetFieldLabelsOnSave(const autofill::ServerFieldType password_type,
                          const autofill::PasswordForm& submitted_form,
                          FieldTypeMap* field_types) {
  DCHECK(password_type == autofill::PASSWORD ||
         password_type == autofill::PROBABLY_ACCOUNT_CREATION_PASSWORD ||
         password_type == autofill::ACCOUNT_CREATION_PASSWORD ||
         password_type == autofill::NOT_ACCOUNT_CREATION_PASSWORD)
      << password_type;

  if (!submitted_form.new_password_element.empty()) {
    (*field_types)[submitted_form.new_password_element] = password_type;
  } else {
    DCHECK(!submitted_form.password_element.empty());
    (*field_types)[submitted_form.password_element] = password_type;
  }
}

// Label username and password fields with autofill types in |form_structure|
// based on |field_types|. The function also adds the types to
// |available_field_types|.
void LabelFields(const FieldTypeMap& field_types,
                 FormStructure* form_structure,
                 autofill::ServerFieldTypeSet* available_field_types) {
  for (size_t i = 0; i < form_structure->field_count(); ++i) {
    autofill::AutofillField* field = form_structure->field(i);

    autofill::ServerFieldType type = autofill::UNKNOWN_TYPE;
    if (!field->name.empty()) {
      auto iter = field_types.find(field->name);
      if (iter != field_types.end()) {
        type = iter->second;
        available_field_types->insert(type);
      }
    }

    autofill::ServerFieldTypeSet types;
    types.insert(type);
    field->set_possible_types(types);
  }
}

}  // namespace

PasswordFormManager::PasswordFormManager(
    PasswordManager* password_manager,
    PasswordManagerClient* client,
    const base::WeakPtr<PasswordManagerDriver>& driver,
    const PasswordForm& observed_form,
    std::unique_ptr<FormSaver> form_saver,
    FormFetcher* form_fetcher)
    : observed_form_(observed_form),
      other_possible_username_action_(
          PasswordFormManager::IGNORE_OTHER_POSSIBLE_USERNAMES),
      form_path_segments_(
          observed_form_.origin.is_valid()
              ? SplitPathToSegments(observed_form_.origin.path())
              : std::vector<std::string>()),
      is_new_login_(true),
      has_generated_password_(false),
      is_manual_generation_(false),
      generation_popup_was_shown_(false),
      form_classifier_outcome_(kNoOutcome),
      password_overridden_(false),
      retry_password_form_password_update_(false),
      generation_available_(false),
      password_manager_(password_manager),
      preferred_match_(nullptr),
      is_possible_change_password_form_without_username_(
          observed_form.IsPossibleChangePasswordFormWithoutUsername()),
      client_(client),
      manager_action_(kManagerActionNone),
      user_action_(kUserActionNone),
      submit_result_(kSubmitResultNotSubmitted),
      form_type_(kFormTypeUnspecified),
      form_saver_(std::move(form_saver)),
      owned_form_fetcher_(
          form_fetcher ? nullptr
                       : base::MakeUnique<FormFetcherImpl>(
                             PasswordStore::FormDigest(observed_form),
                             client,
                             true /* should_migrate_http_passwords */,
                             false /* should_query_suppressed_https_forms */)),
      form_fetcher_(form_fetcher ? form_fetcher : owned_form_fetcher_.get()),
      is_main_frame_secure_(client->IsMainFrameSecure()) {
  if (owned_form_fetcher_)
    owned_form_fetcher_->Fetch();
  DCHECK_EQ(observed_form.scheme == PasswordForm::SCHEME_HTML,
            driver != nullptr);
  if (driver)
    drivers_.push_back(driver);
  form_fetcher_->AddConsumer(this);
}

PasswordFormManager::~PasswordFormManager() {
  form_fetcher_->RemoveConsumer(this);

  UMA_HISTOGRAM_ENUMERATION("PasswordManager.ActionsTakenV3", GetActionsTaken(),
                            kMaxNumActionsTaken);
  // Use the visible main frame URL at the time the PasswordFormManager
  // is created, in case a navigation has already started and the
  // visible URL has changed.
  if (!is_main_frame_secure_) {
    UMA_HISTOGRAM_ENUMERATION("PasswordManager.ActionsTakenOnNonSecureForm",
                              GetActionsTaken(), kMaxNumActionsTaken);
  }
  if (submit_result_ == kSubmitResultNotSubmitted) {
    if (has_generated_password_)
      metrics_util::LogPasswordGenerationSubmissionEvent(
          metrics_util::PASSWORD_NOT_SUBMITTED);
    else if (generation_available_)
      metrics_util::LogPasswordGenerationAvailableSubmissionEvent(
          metrics_util::PASSWORD_NOT_SUBMITTED);
  }
  if (form_type_ != kFormTypeUnspecified) {
    UMA_HISTOGRAM_ENUMERATION("PasswordManager.SubmittedFormType", form_type_,
                              kFormTypeMax);
    if (!is_main_frame_secure_) {
      UMA_HISTOGRAM_ENUMERATION("PasswordManager.SubmittedNonSecureFormType",
                                form_type_, kFormTypeMax);
    }
  }
}

int PasswordFormManager::GetActionsTaken() const {
  return user_action_ +
         kUserActionMax *
             (manager_action_ + kManagerActionMax * submit_result_);
}

// static
base::string16 PasswordFormManager::PasswordToSave(const PasswordForm& form) {
  if (form.new_password_element.empty() || form.new_password_value.empty())
    return form.password_value;
  return form.new_password_value;
}

// TODO(crbug.com/700420): Refactor this function, to make comparison more
// reliable.
PasswordFormManager::MatchResultMask PasswordFormManager::DoesManage(
    const PasswordForm& form,
    const password_manager::PasswordManagerDriver* driver) const {
  // Non-HTML form case.
  if (observed_form_.scheme != PasswordForm::SCHEME_HTML ||
      form.scheme != PasswordForm::SCHEME_HTML) {
    const bool forms_match = observed_form_.signon_realm == form.signon_realm &&
                             observed_form_.scheme == form.scheme;
    return forms_match ? RESULT_COMPLETE_MATCH : RESULT_NO_MATCH;
  }

  // HTML form case.
  MatchResultMask result = RESULT_NO_MATCH;

  if (observed_form_.signon_realm != form.signon_realm)
    return result;

  // Easiest case of matching origins.
  bool origins_match = form.origin == observed_form_.origin;
  // If this is a replay of the same form in the case a user entered an invalid
  // password, the origin of the new form may equal the action of the "first"
  // form instead.
  origins_match = origins_match || (form.origin == observed_form_.action);
  // Otherwise, if action hosts are the same, the old URL scheme is HTTP while
  // the new one is HTTPS, and the new path equals to or extends the old path,
  // we also consider the actions a match. This is to accommodate cases where
  // the original login form is on an HTTP page, but a failed login attempt
  // redirects to HTTPS (as in http://example.org -> https://example.org/auth).
  if (!origins_match && !observed_form_.origin.SchemeIsCryptographic() &&
      form.origin.SchemeIsCryptographic()) {
    const base::StringPiece& old_path = observed_form_.origin.path_piece();
    const base::StringPiece& new_path = form.origin.path_piece();
    origins_match =
        observed_form_.origin.host_piece() == form.origin.host_piece() &&
        observed_form_.origin.port() == form.origin.port() &&
        base::StartsWith(new_path, old_path, base::CompareCase::SENSITIVE);
  }

  if (driver)
    origins_match =
        origins_match ||
        std::any_of(drivers_.begin(), drivers_.end(),
                    [driver](const base::WeakPtr<PasswordManagerDriver>& d) {
                      return d.get() == driver;
                    });

  if (!origins_match)
    return result;

  result |= RESULT_ORIGINS_OR_FRAMES_MATCH;

  // Autofill predictions can overwrite our default username selection so
  // if this form was parsed with autofill predictions then allow the username
  // element to be different.
  if ((form.was_parsed_using_autofill_predictions ||
       form.username_element == observed_form_.username_element) &&
      form.password_element == observed_form_.password_element) {
    result |= RESULT_HTML_ATTRIBUTES_MATCH;
  }

  // Note: although saved password forms might actually have an empty action
  // URL if they were imported (see bug 1107719), the |form| we see here comes
  // never from the password store, and should have an exactly matching action.
  if (form.action == observed_form_.action)
    result |= RESULT_ACTION_MATCH;

  return result;
}

bool PasswordFormManager::IsBlacklisted() const {
  DCHECK_EQ(FormFetcher::State::NOT_WAITING, form_fetcher_->GetState());
  return !blacklisted_matches_.empty();
}

void PasswordFormManager::PermanentlyBlacklist() {
  DCHECK_EQ(FormFetcher::State::NOT_WAITING, form_fetcher_->GetState());
  DCHECK(!client_->IsIncognito());

  if (!new_blacklisted_) {
    new_blacklisted_ = base::MakeUnique<PasswordForm>(observed_form_);
    blacklisted_matches_.push_back(new_blacklisted_.get());
  }
  form_saver_->PermanentlyBlacklist(new_blacklisted_.get());
}

bool PasswordFormManager::IsNewLogin() const {
  DCHECK_EQ(FormFetcher::State::NOT_WAITING, form_fetcher_->GetState());
  return is_new_login_;
}

bool PasswordFormManager::IsPendingCredentialsPublicSuffixMatch() const {
  return pending_credentials_.is_public_suffix_match;
}

void PasswordFormManager::ProvisionallySave(
    const PasswordForm& credentials,
    OtherPossibleUsernamesAction action) {
  std::unique_ptr<autofill::PasswordForm> mutable_submitted_form(
      new PasswordForm(credentials));
  if (credentials.IsPossibleChangePasswordForm() &&
      !credentials.username_value.empty() &&
      IsProbablyNotUsername(credentials.username_value)) {
    mutable_submitted_form->username_value.clear();
    mutable_submitted_form->username_element.clear();
    is_possible_change_password_form_without_username_ = true;
  }
  submitted_form_ = std::move(mutable_submitted_form);
  other_possible_username_action_ = action;
  does_look_like_signup_form_ = credentials.does_look_like_signup_form;

  if (form_fetcher_->GetState() == FormFetcher::State::NOT_WAITING)
    CreatePendingCredentials();
}

void PasswordFormManager::Save() {
  DCHECK_EQ(FormFetcher::State::NOT_WAITING, form_fetcher_->GetState());
  DCHECK(!client_->IsIncognito());

  metrics_util::LogPasswordAcceptedSaveUpdateSubmissionIndicatorEvent(
      submitted_form_->submission_event);

  if ((user_action_ == kUserActionNone) &&
      DidPreferenceChange(best_matches_, pending_credentials_.username_value)) {
    SetUserAction(kUserActionChoose);
  }
  base::Optional<PasswordForm> old_primary_key;
  if (is_new_login_) {
    SanitizePossibleUsernames(&pending_credentials_);
    pending_credentials_.date_created = base::Time::Now();
    SendVotesOnSave();
    form_saver_->Save(pending_credentials_, best_matches_,
                      old_primary_key ? &old_primary_key.value() : nullptr);
  } else {
    ProcessUpdate();
    std::vector<PasswordForm> credentials_to_update;
    old_primary_key = UpdatePendingAndGetOldKey(&credentials_to_update);
    form_saver_->Update(pending_credentials_, best_matches_,
                        &credentials_to_update,
                        old_primary_key ? &old_primary_key.value() : nullptr);
  }

  // This is not in ProcessUpdate() to catch PSL matched credentials.
  if (pending_credentials_.times_used != 0 &&
      pending_credentials_.type == PasswordForm::TYPE_GENERATED) {
    metrics_util::LogPasswordGenerationSubmissionEvent(
        metrics_util::PASSWORD_USED);
  }

  password_manager_->UpdateFormManagers();
}

void PasswordFormManager::Update(
    const autofill::PasswordForm& credentials_to_update) {
  metrics_util::LogPasswordAcceptedSaveUpdateSubmissionIndicatorEvent(
      submitted_form_->submission_event);
  if (observed_form_.IsPossibleChangePasswordForm()) {
    FormStructure form_structure(credentials_to_update.form_data);
    UploadPasswordVote(observed_form_, autofill::NEW_PASSWORD,
                       form_structure.FormSignatureAsStr());
  }
  base::string16 password_to_save = pending_credentials_.password_value;
  bool skip_zero_click = pending_credentials_.skip_zero_click;
  pending_credentials_ = credentials_to_update;
  pending_credentials_.password_value = password_to_save;
  pending_credentials_.skip_zero_click = skip_zero_click;
  pending_credentials_.preferred = true;
  is_new_login_ = false;
  ProcessUpdate();
  std::vector<PasswordForm> more_credentials_to_update;
  base::Optional<PasswordForm> old_primary_key =
      UpdatePendingAndGetOldKey(&more_credentials_to_update);
  form_saver_->Update(pending_credentials_, best_matches_,
                      &more_credentials_to_update,
                      old_primary_key ? &old_primary_key.value() : nullptr);
}

void PasswordFormManager::SetSubmittedForm(const autofill::PasswordForm& form) {
  bool is_change_password_form =
      !form.new_password_value.empty() && !form.password_value.empty();
  bool is_signup_form =
      !form.new_password_value.empty() && form.password_value.empty();
  bool no_username = form.username_element.empty();

  if (form.layout == PasswordForm::Layout::LAYOUT_LOGIN_AND_SIGNUP) {
    form_type_ = kFormTypeLoginAndSignup;
  } else if (is_change_password_form) {
    form_type_ = kFormTypeChangePasswordEnabled;
  } else if (is_signup_form) {
    if (no_username)
      form_type_ = kFormTypeSignupNoUsername;
    else
      form_type_ = kFormTypeSignup;
  } else if (no_username) {
    form_type_ = kFormTypeLoginNoUsername;
  } else {
    form_type_ = kFormTypeLogin;
  }
}

void PasswordFormManager::ScoreMatches(
    const std::vector<const PasswordForm*>& matches) {
  DCHECK(std::all_of(
      matches.begin(), matches.end(),
      [](const PasswordForm* match) { return !match->blacklisted_by_user; }));

  preferred_match_ = nullptr;
  best_matches_.clear();
  not_best_matches_.clear();

  if (matches.empty())
    return;

  // Compute scores.
  std::vector<uint32_t> credential_scores(matches.size());
  std::transform(
      matches.begin(), matches.end(), credential_scores.begin(),
      [this](const PasswordForm* match) { return ScoreResult(*match); });

  const uint32_t best_score =
      *std::max_element(credential_scores.begin(), credential_scores.end());

  std::map<base::string16, uint32_t> best_scores;  // best scores for usernames

  for (size_t i = 0; i < matches.size(); ++i) {
    uint32_t& score = best_scores[matches[i]->username_value];
    score = std::max(score, credential_scores[i]);
  }

  // Assign best, non-best and preferred matches.
  not_best_matches_.reserve(matches.size() - best_scores.size());
  // Fill |best_matches_| with the best-scoring credentials for each username.
  for (size_t i = 0; i < matches.size(); ++i) {
    const PasswordForm* const match = matches[i];
    const base::string16& username = match->username_value;

    if (credential_scores[i] < best_scores[username]) {
      not_best_matches_.push_back(match);
      continue;
    }

    if (!preferred_match_ && credential_scores[i] == best_score)
      preferred_match_ = match;

    // If there is another best-score match for the same username then leave it
    // and add the current form to |not_best_matches_|.
    auto best_match_username = best_matches_.find(username);
    if (best_match_username == best_matches_.end()) {
      best_matches_.insert(std::make_pair(username, match));
    } else {
      not_best_matches_.push_back(match);
    }
  }
}

void PasswordFormManager::ProcessMatches(
    const std::vector<const PasswordForm*>& non_federated,
    size_t filtered_count) {
  blacklisted_matches_.clear();
  new_blacklisted_.reset();

  std::unique_ptr<BrowserSavePasswordProgressLogger> logger;
  if (password_manager_util::IsLoggingActive(client_)) {
    logger.reset(
        new BrowserSavePasswordProgressLogger(client_->GetLogManager()));
    logger->LogMessage(Logger::STRING_PROCESS_MATCHES_METHOD);
  }

  // Copy out and score non-blacklisted matches.
  std::vector<const PasswordForm*> matches(std::count_if(
      non_federated.begin(), non_federated.end(),
      [this](const PasswordForm* form) { return IsMatch(*form); }));
  std::copy_if(non_federated.begin(), non_federated.end(), matches.begin(),
               [this](const PasswordForm* form) { return IsMatch(*form); });
  ScoreMatches(matches);

  // Copy out blacklisted matches.
  blacklisted_matches_.resize(std::count_if(
      non_federated.begin(), non_federated.end(),
      [this](const PasswordForm* form) { return IsBlacklistMatch(*form); }));
  std::copy_if(
      non_federated.begin(), non_federated.end(), blacklisted_matches_.begin(),
      [this](const PasswordForm* form) { return IsBlacklistMatch(*form); });

  UMA_HISTOGRAM_COUNTS(
      "PasswordManager.NumPasswordsNotShown",
      non_federated.size() + filtered_count - best_matches_.size());

  // If password store was slow and provisionally saved form is already here
  // then create pending credentials (see http://crbug.com/470322).
  if (submitted_form_)
    CreatePendingCredentials();

  for (auto const& driver : drivers_)
    ProcessFrameInternal(driver);
  if (observed_form_.scheme != PasswordForm::SCHEME_HTML)
    ProcessLoginPrompt();
}

void PasswordFormManager::ProcessFrame(
    const base::WeakPtr<PasswordManagerDriver>& driver) {
  DCHECK_EQ(PasswordForm::SCHEME_HTML, observed_form_.scheme);
  if (form_fetcher_->GetState() == FormFetcher::State::NOT_WAITING)
    ProcessFrameInternal(driver);

  for (auto const& old_driver : drivers_) {
    // |drivers_| is not a set because WeakPtr has no good candidate for a key
    // (the address may change to null). So let's weed out duplicates in O(N).
    if (old_driver.get() == driver.get())
      return;
  }

  drivers_.push_back(driver);
}

void PasswordFormManager::ProcessFrameInternal(
    const base::WeakPtr<PasswordManagerDriver>& driver) {
  DCHECK_EQ(PasswordForm::SCHEME_HTML, observed_form_.scheme);
  if (!driver)
    return;

  driver->AllowPasswordGenerationForForm(observed_form_);

  if (best_matches_.empty())
    return;

  // Proceed to autofill.
  // Note that we provide the choices but don't actually prefill a value if:
  // (1) we are in Incognito mode, (2) the ACTION paths don't match,
  // (3) if it matched using public suffix domain matching, or
  // (4) the form is change password form.
  // However, 2 and 3 should not apply to Android-based credentials found
  // via affiliation-based matching (we want to autofill them).
  // TODO(engedy): Clean this up. See: https://crbug.com/476519.
  bool wait_for_username =
      client_->IsIncognito() ||
      (!IsValidAndroidFacetURI(preferred_match_->signon_realm) &&
       (observed_form_.action.GetWithEmptyPath() !=
            preferred_match_->action.GetWithEmptyPath() ||
        preferred_match_->is_public_suffix_match ||
        observed_form_.IsPossibleChangePasswordForm()));
  if (wait_for_username) {
    manager_action_ = kManagerActionNone;
  } else {
    manager_action_ = kManagerActionAutofilled;
    base::RecordAction(base::UserMetricsAction("PasswordManager_Autofilled"));
  }
  if (ShouldShowInitialPasswordAccountSuggestions()) {
    // This is for the fill-on-account-select experiment. Instead of autofilling
    // found usernames and passwords on load, this instructs the renderer to
    // return with any found password forms so a list of password account
    // suggestions can be drawn.
    password_manager_->ShowInitialPasswordAccountSuggestions(
        driver.get(), observed_form_, best_matches_, *preferred_match_,
        wait_for_username);
  } else {
    // If fill-on-account-select is not enabled, continue with autofilling any
    // password forms as traditionally has been done.
    password_manager_->Autofill(driver.get(), observed_form_, best_matches_,
                                form_fetcher_->GetFederatedMatches(),
                                *preferred_match_, wait_for_username);
  }
}

void PasswordFormManager::ProcessLoginPrompt() {
  DCHECK_NE(PasswordForm::SCHEME_HTML, observed_form_.scheme);
  if (!preferred_match_)
    return;

  manager_action_ = kManagerActionAutofilled;
  password_manager_->AutofillHttpAuth(best_matches_, *preferred_match_);
}

void PasswordFormManager::ProcessUpdate() {
  DCHECK_EQ(FormFetcher::State::NOT_WAITING, form_fetcher_->GetState());
  DCHECK(preferred_match_ || !pending_credentials_.federation_origin.unique());
  // If we're doing an Update, we either autofilled correctly and need to
  // update the stats, or the user typed in a new password for autofilled
  // username, or the user selected one of the non-preferred matches,
  // thus requiring a swap of preferred bits.
  DCHECK(!IsNewLogin() && pending_credentials_.preferred);
  DCHECK(!client_->IsIncognito());

  UpdateMetadataForUsage(&pending_credentials_);

  base::RecordAction(
      base::UserMetricsAction("PasswordManager_LoginFollowingAutofill"));

  // Check to see if this form is a candidate for password generation.
  // Do not send votes on change password forms, since they were already sent in
  // Update() method.
  if (!observed_form_.IsPossibleChangePasswordForm())
    SendVoteOnCredentialsReuse(observed_form_, &pending_credentials_);
}

bool PasswordFormManager::UpdatePendingCredentialsIfOtherPossibleUsername(
    const base::string16& username) {
  for (const auto& key_value : best_matches_) {
    const PasswordForm& match = *key_value.second;
    for (size_t i = 0; i < match.other_possible_usernames.size(); ++i) {
      if (match.other_possible_usernames[i].first == username) {
        pending_credentials_ = match;
        return true;
      }
    }
  }
  return false;
}

bool PasswordFormManager::FindUsernameInOtherPossibleUsernames(
    const autofill::PasswordForm& match,
    const base::string16& username) {
  DCHECK(!username_correction_vote_);

  for (const PossibleUsernamePair& pair : match.other_possible_usernames) {
    if (pair.first == username) {
      username_correction_vote_.reset(new autofill::PasswordForm(match));
      username_correction_vote_->username_element = pair.second;
      return true;
    }
  }
  return false;
}

void PasswordFormManager::FindCorrectedUsernameElement(
    const base::string16& username,
    const base::string16& password) {
  for (const auto& key_value : best_matches_) {
    const PasswordForm* match = key_value.second;
    if ((match->password_value == password) &&
        FindUsernameInOtherPossibleUsernames(*match, username))
      return;
  }
  for (const autofill::PasswordForm* match : not_best_matches_) {
    if ((match->password_value == password) &&
        FindUsernameInOtherPossibleUsernames(*match, username))
      return;
  }
}

void PasswordFormManager::SendVoteOnCredentialsReuse(
    const PasswordForm& observed,
    PasswordForm* pending) {
  // Ignore |pending_structure| if its FormData has no fields. This is to
  // weed out those credentials that were saved before FormData was added
  // to PasswordForm. Even without this check, these FormStructure's won't
  // be uploaded, but it makes it hard to see if we are encountering
  // unexpected errors.
  if (pending->form_data.fields.empty())
    return;

  FormStructure pending_structure(pending->form_data);
  FormStructure observed_structure(observed.form_data);

  if (pending_structure.FormSignatureAsStr() !=
      observed_structure.FormSignatureAsStr()) {
    // Only upload if this is the first time the password has been used.
    // Otherwise the credentials have been used on the same field before so
    // they aren't from an account creation form.
    // Also bypass uploading if the username was edited. Offering generation
    // in cases where we currently save the wrong username isn't great.
    // TODO(gcasto): Determine if generation should be offered in this case.
    if (pending->times_used == 1 && selected_username_.empty()) {
      if (UploadPasswordVote(*pending, autofill::ACCOUNT_CREATION_PASSWORD,
                             observed_structure.FormSignatureAsStr())) {
        pending->generation_upload_status =
            autofill::PasswordForm::POSITIVE_SIGNAL_SENT;
      }
    }
  } else if (pending->generation_upload_status ==
             autofill::PasswordForm::POSITIVE_SIGNAL_SENT) {
    // A signal was sent that this was an account creation form, but the
    // credential is now being used on the same form again. This cancels out
    // the previous vote.
    if (UploadPasswordVote(*pending, autofill::NOT_ACCOUNT_CREATION_PASSWORD,
                           std::string())) {
      pending->generation_upload_status =
          autofill::PasswordForm::NEGATIVE_SIGNAL_SENT;
    }
  } else if (generation_popup_was_shown_) {
    // Even if there is no autofill vote to be sent, send the vote about the
    // usage of the generation popup.
    UploadPasswordVote(*pending, autofill::UNKNOWN_TYPE, std::string());
  }
}

bool PasswordFormManager::UploadPasswordVote(
    const autofill::PasswordForm& form_to_upload,
    const autofill::ServerFieldType& password_type,
    const std::string& login_form_signature) {
  // Check if there is any vote to be sent.
  bool has_autofill_vote = password_type != autofill::UNKNOWN_TYPE;
  bool has_password_generation_vote = generation_popup_was_shown_;
  if (!has_autofill_vote && !has_password_generation_vote)
    return false;

  autofill::AutofillManager* autofill_manager =
      client_->GetAutofillManagerForMainFrame();
  if (!autofill_manager || !autofill_manager->download_manager())
    return false;

  // If this is an update, a vote about the observed form is sent. If the user
  // re-uses credentials, a vote about the saved form is sent. If the user saves
  // credentials, the observed and pending forms are the same.
  FormStructure form_structure(form_to_upload.form_data);
  if (!autofill_manager->ShouldUploadForm(form_structure) ||
      !form_structure.ShouldBeCrowdsourced()) {
    UMA_HISTOGRAM_BOOLEAN("PasswordGeneration.UploadStarted", false);
    return false;
  }

  autofill::ServerFieldTypeSet available_field_types;
  if (password_type != autofill::USERNAME) {
    if (has_autofill_vote) {
      // A map from field names to field types.
      FieldTypeMap field_types;
      DCHECK(submitted_form_);
      bool is_update = password_type == autofill::NEW_PASSWORD ||
                       password_type == autofill::PROBABLY_NEW_PASSWORD ||
                       password_type == autofill::NOT_NEW_PASSWORD;
      if (is_update) {
        if (submitted_form_->new_password_element.empty())
          return false;
        SetFieldLabelsOnUpdate(password_type, *submitted_form_, &field_types);
      } else {  // Saving.
        SetFieldLabelsOnSave(password_type, *submitted_form_, &field_types);
      }
      field_types[submitted_form_->confirmation_password_element] =
          autofill::CONFIRMATION_PASSWORD;
      LabelFields(field_types, &form_structure, &available_field_types);
    }
    if (password_type != autofill::ACCOUNT_CREATION_PASSWORD) {
      if (generation_popup_was_shown_)
        AddGeneratedVote(&form_structure);
      if (form_classifier_outcome_ != kNoOutcome)
        AddFormClassifierVote(&form_structure);
    }
  } else {  // Username correction vote.
    FieldTypeMap field_types;
    field_types[form_to_upload.username_element] = autofill::USERNAME;
    field_types[form_to_upload.password_element] =
        autofill::ACCOUNT_CREATION_PASSWORD;
    LabelFields(field_types, &form_structure, &available_field_types);
  }

  // Force uploading as these events are relatively rare and we want to make
  // sure to receive them.
  form_structure.set_upload_required(UPLOAD_REQUIRED);

  if (password_manager_util::IsLoggingActive(client_)) {
    BrowserSavePasswordProgressLogger logger(client_->GetLogManager());
    logger.LogFormStructure(Logger::STRING_FORM_VOTES, form_structure);
  }

  bool success = autofill_manager->download_manager()->StartUploadRequest(
      form_structure, false /* was_autofilled */, available_field_types,
      login_form_signature, true /* observed_submission */);

  UMA_HISTOGRAM_BOOLEAN("PasswordGeneration.UploadStarted", success);
  return success;
}

void PasswordFormManager::AddGeneratedVote(
    autofill::FormStructure* form_structure) {
  DCHECK(form_structure);
  DCHECK(generation_popup_was_shown_);

  if (generation_element_.empty())
    return;

  autofill::AutofillUploadContents::Field::PasswordGenerationType type =
      autofill::AutofillUploadContents::Field::NO_GENERATION;
  if (has_generated_password_) {
    if (is_manual_generation_) {
      type = observed_form_.IsPossibleChangePasswordForm()
                 ? autofill::AutofillUploadContents::Field::
                       MANUALLY_TRIGGERED_GENERATION_ON_CHANGE_PASSWORD_FORM
                 : autofill::AutofillUploadContents::Field::
                       MANUALLY_TRIGGERED_GENERATION_ON_SIGN_UP_FORM;
    } else {
      type =
          observed_form_.IsPossibleChangePasswordForm()
              ? autofill::AutofillUploadContents::Field::
                    AUTOMATICALLY_TRIGGERED_GENERATION_ON_CHANGE_PASSWORD_FORM
              : autofill::AutofillUploadContents::Field::
                    AUTOMATICALLY_TRIGGERED_GENERATION_ON_SIGN_UP_FORM;
    }
  } else
    type = autofill::AutofillUploadContents::Field::IGNORED_GENERATION_POPUP;

  for (size_t i = 0; i < form_structure->field_count(); ++i) {
    autofill::AutofillField* field = form_structure->field(i);
    if (field->name == generation_element_) {
      field->set_generation_type(type);
      break;
    }
  }
}

void PasswordFormManager::AddFormClassifierVote(
    autofill::FormStructure* form_structure) {
  DCHECK(form_structure);
  DCHECK(form_classifier_outcome_ != kNoOutcome);

  for (size_t i = 0; i < form_structure->field_count(); ++i) {
    autofill::AutofillField* field = form_structure->field(i);
    if (form_classifier_outcome_ == kFoundGenerationElement &&
        field->name == generation_element_detected_by_classifier_) {
      field->set_form_classifier_outcome(
          autofill::AutofillUploadContents::Field::GENERATION_ELEMENT);
    } else {
      field->set_form_classifier_outcome(
          autofill::AutofillUploadContents::Field::NON_GENERATION_ELEMENT);
    }
  }
}

void PasswordFormManager::CreatePendingCredentials() {
  DCHECK(submitted_form_);
  base::string16 password_to_save(PasswordToSave(*submitted_form_));

  // Make sure the important fields stay the same as the initially observed or
  // autofilled ones, as they may have changed if the user experienced a login
  // failure.
  // Look for these credentials in the list containing auto-fill entries.
  const PasswordForm* saved_form = FindBestSavedMatch(submitted_form_.get());
  if (saved_form != nullptr) {
    // The user signed in with a login we autofilled.
    pending_credentials_ = *saved_form;
    password_overridden_ =
        pending_credentials_.password_value != password_to_save;
    if (IsPendingCredentialsPublicSuffixMatch()) {
      // If the autofilled credentials were a PSL match or credentials stored
      // from Android apps store a copy with the current origin and signon
      // realm. This ensures that on the next visit, a precise match is found.
      is_new_login_ = true;
      SetUserAction(password_overridden_ ? kUserActionOverridePassword
                                         : kUserActionChoosePslMatch);

      // Since this credential will not overwrite a previously saved credential,
      // username_value can be updated now.
      if (!selected_username_.empty())
        pending_credentials_.username_value = selected_username_;

      // Update credential to reflect that it has been used for submission.
      // If this isn't updated, then password generation uploads are off for
      // sites where PSL matching is required to fill the login form, as two
      // PASSWORD votes are uploaded per saved password instead of one.
      //
      // TODO(gcasto): It would be nice if other state were shared such that if
      // say a password was updated on one match it would update on all related
      // passwords. This is a much larger change.
      UpdateMetadataForUsage(&pending_credentials_);

      // Update |pending_credentials_| in order to be able correctly save it.
      pending_credentials_.origin = submitted_form_->origin;
      pending_credentials_.signon_realm = submitted_form_->signon_realm;

      // Normally, the copy of the PSL matched credentials, adapted for the
      // current domain, is saved automatically without asking the user, because
      // the copy likely represents the same account, i.e., the one for which
      // the user already agreed to store a password.
      //
      // However, if the user changes the suggested password, it might indicate
      // that the autofilled credentials and |submitted_form_|
      // actually correspond  to two different accounts (see
      // http://crbug.com/385619). In that case the user should be asked again
      // before saving the password. This is ensured by setting
      // |password_overriden_| on |pending_credentials_| to false and setting
      // |origin| and |signon_realm| to correct values.
      //
      // There is still the edge case when the autofilled credentials represent
      // the same account as |submitted_form_| but the stored password
      // was out of date. In that case, the user just had to manually enter the
      // new password, which is now in |submitted_form_|. The best
      // thing would be to save automatically, and also update the original
      // credentials. However, we have no way to tell if this is the case.
      // This will likely happen infrequently, and the inconvenience put on the
      // user by asking them is not significant, so we are fine with asking
      // here again.
      if (password_overridden_) {
        pending_credentials_.is_public_suffix_match = false;
        password_overridden_ = false;
      }
    } else {  // Not a PSL match.
      is_new_login_ = false;
      if (password_overridden_)
        SetUserAction(kUserActionOverridePassword);
    }
  } else if (other_possible_username_action_ ==
                 ALLOW_OTHER_POSSIBLE_USERNAMES &&
             UpdatePendingCredentialsIfOtherPossibleUsername(
                 submitted_form_->username_value)) {
    // |pending_credentials_| is now set. Note we don't update
    // |pending_credentials_.username_value| to |credentials.username_value|
    // yet because we need to keep the original username to modify the stored
    // credential.
    selected_username_ = submitted_form_->username_value;
    is_new_login_ = false;
  } else if (!best_matches_.empty() &&
             submitted_form_->type != autofill::PasswordForm::TYPE_API &&
             (submitted_form_->IsPossibleChangePasswordFormWithoutUsername() ||
              submitted_form_->username_element.empty())) {
    const PasswordForm* best_update_match =
        FindBestMatchForUpdatePassword(submitted_form_->password_value);

    retry_password_form_password_update_ =
        submitted_form_->username_element.empty() &&
        submitted_form_->new_password_element.empty();

    is_new_login_ = false;
    if (best_update_match) {
      pending_credentials_ = *best_update_match;
    } else if (has_generated_password_) {
      // If a password was generated and we didn't find match we have to save it
      // in separate entry since we have to store it but we don't know where.
      CreatePendingCredentialsForNewCredentials();
      is_new_login_ = true;
    } else {
      // We don't care about |pending_credentials_| if we didn't find the best
      // match, since the user will select the correct one.
      pending_credentials_.origin = submitted_form_->origin;
    }
  } else {
    CreatePendingCredentialsForNewCredentials();
    FindCorrectedUsernameElement(submitted_form_->username_value,
                                 submitted_form_->password_value);
  }

  if (!IsValidAndroidFacetURI(pending_credentials_.signon_realm)) {
    pending_credentials_.action = submitted_form_->action;
    // If the user selected credentials we autofilled from a PasswordForm
    // that contained no action URL (IE6/7 imported passwords, for example),
    // bless it with the action URL from the observed form. See bug 1107719.
    if (pending_credentials_.action.is_empty())
      pending_credentials_.action = observed_form_.action;
  }

  pending_credentials_.password_value = password_to_save;
  pending_credentials_.preferred = submitted_form_->preferred;
  CopyFieldPropertiesMasks(*submitted_form_, &pending_credentials_);

  // If we're dealing with an API-driven provisionally saved form, then take
  // the server provided values. We don't do this for non-API forms, as
  // those will never have those members set.
  if (submitted_form_->type == autofill::PasswordForm::TYPE_API) {
    pending_credentials_.skip_zero_click = submitted_form_->skip_zero_click;
    pending_credentials_.display_name = submitted_form_->display_name;
    pending_credentials_.federation_origin = submitted_form_->federation_origin;
    pending_credentials_.icon_url = submitted_form_->icon_url;
    // Take the correct signon_realm for federated credentials.
    pending_credentials_.signon_realm = submitted_form_->signon_realm;
  }

  if (user_action_ == kUserActionOverridePassword &&
      pending_credentials_.type == PasswordForm::TYPE_GENERATED &&
      !has_generated_password_) {
    metrics_util::LogPasswordGenerationSubmissionEvent(
        metrics_util::PASSWORD_OVERRIDDEN);
    pending_credentials_.type = PasswordForm::TYPE_MANUAL;
  }

  if (has_generated_password_)
    pending_credentials_.type = PasswordForm::TYPE_GENERATED;
}

uint32_t PasswordFormManager::ScoreResult(const PasswordForm& candidate) const {
  DCHECK(!candidate.blacklisted_by_user);
  // For scoring of candidate login data:
  // The most important element that should match is the signon_realm followed
  // by the origin, the action, the password name, the submit button name, and
  // finally the username input field name.
  // If public suffix origin match was not used, it gives an addition of
  // 128 (1 << 7).
  // Exact origin match gives an addition of 64 (1 << 6) + # of matching url
  // dirs.
  // Partial match gives an addition of 32 (1 << 5) + # matching url dirs
  // That way, a partial match cannot trump an exact match even if
  // the partial one matches all other attributes (action, elements) (and
  // regardless of the matching depth in the URL path).

  // When comparing path segments, only consider at most 63 of them, so that the
  // potential gain from shared path prefix is not more than from an exact
  // origin match.
  const size_t kSegmentCountCap = 63;
  const size_t capped_form_path_segment_count =
      std::min(form_path_segments_.size(), kSegmentCountCap);

  uint32_t score = 0u;
  if (!candidate.is_public_suffix_match) {
    score += 1u << 8;
  }

  if (candidate.preferred)
    score += 1u << 7;

  if (candidate.origin == observed_form_.origin) {
    // This check is here for the most common case which
    // is we have a single match in the db for the given host,
    // so we don't generally need to walk the entire URL path (the else
    // clause).
    score += (1u << 6) + static_cast<uint32_t>(capped_form_path_segment_count);
  } else {
    // Walk the origin URL paths one directory at a time to see how
    // deep the two match.
    std::vector<std::string> candidate_path_segments =
        SplitPathToSegments(candidate.origin.path());
    size_t depth = 0u;
    const size_t max_dirs = std::min(capped_form_path_segment_count,
                                     candidate_path_segments.size());
    while ((depth < max_dirs) &&
           (form_path_segments_[depth] == candidate_path_segments[depth])) {
      depth++;
      score++;
    }
    // do we have a partial match?
    score += (depth > 0u) ? 1u << 5 : 0u;
  }
  if (observed_form_.scheme == PasswordForm::SCHEME_HTML) {
    if (candidate.action == observed_form_.action)
      score += 1u << 3;
    if (candidate.password_element == observed_form_.password_element)
      score += 1u << 2;
    if (candidate.submit_element == observed_form_.submit_element)
      score += 1u << 1;
    if (candidate.username_element == observed_form_.username_element)
      score += 1u << 0;
  }

  return score;
}

bool PasswordFormManager::IsMatch(const autofill::PasswordForm& form) const {
  return !form.blacklisted_by_user && form.scheme == observed_form_.scheme;
}

bool PasswordFormManager::IsBlacklistMatch(
    const autofill::PasswordForm& blacklisted_form) const {
  if (!blacklisted_form.blacklisted_by_user ||
      blacklisted_form.is_public_suffix_match ||
      blacklisted_form.scheme != observed_form_.scheme ||
      blacklisted_form.origin.GetOrigin() !=
          observed_form_.origin.GetOrigin()) {
    return false;
  }

  if (observed_form_.scheme == PasswordForm::SCHEME_HTML) {
    return (blacklisted_form.origin.path_piece() ==
            observed_form_.origin.path_piece()) ||
           (AreStringsEqualOrEmpty(blacklisted_form.submit_element,
                                   observed_form_.submit_element) &&
            AreStringsEqualOrEmpty(blacklisted_form.password_element,
                                   observed_form_.password_element) &&
            AreStringsEqualOrEmpty(blacklisted_form.username_element,
                                   observed_form_.username_element));
  }
  return true;
}

const PasswordForm* PasswordFormManager::FindBestMatchForUpdatePassword(
    const base::string16& password) const {
  if (best_matches_.size() == 1 && !has_generated_password_) {
    // In case when the user has only one credential and the current password is
    // not generated, consider it the same as is being saved.
    return best_matches_.begin()->second;
  }
  if (password.empty())
    return nullptr;

  for (const auto& key_value : best_matches_) {
    if (key_value.second->password_value == password)
      return key_value.second;
  }
  return nullptr;
}

const PasswordForm* PasswordFormManager::FindBestSavedMatch(
    const PasswordForm* form) const {
  if (!form->federation_origin.unique())
    return nullptr;
  auto it = best_matches_.find(form->username_value);
  if (it != best_matches_.end())
    return it->second;
  if (form->type == autofill::PasswordForm::TYPE_API)
    // Match Credential API forms only by username.
    return nullptr;
  if (!form->username_element.empty() || !form->new_password_element.empty())
    return nullptr;
  for (const auto& stored_match : best_matches_) {
    if (stored_match.second->password_value == form->password_value)
      return stored_match.second;
  }
  return nullptr;
}

void PasswordFormManager::CreatePendingCredentialsForNewCredentials() {
  // User typed in a new, unknown username.
  SetUserAction(kUserActionOverrideUsernameAndPassword);
  pending_credentials_ = observed_form_;
  if (submitted_form_->was_parsed_using_autofill_predictions)
    pending_credentials_.username_element = submitted_form_->username_element;
  pending_credentials_.username_value = submitted_form_->username_value;
  pending_credentials_.other_possible_usernames =
      submitted_form_->other_possible_usernames;

  // The password value will be filled in later, remove any garbage for now.
  pending_credentials_.password_value.clear();
  pending_credentials_.new_password_value.clear();

  // If this was a sign-up or change password form, the names of the elements
  // are likely different than those on a login form, so do not bother saving
  // them. We will fill them with meaningful values during update when the user
  // goes onto a real login form for the first time.
  if (!submitted_form_->new_password_element.empty()) {
    pending_credentials_.password_element.clear();
  }
}

void PasswordFormManager::OnNopeUpdateClicked() {
  UploadPasswordVote(observed_form_, autofill::NOT_NEW_PASSWORD, std::string());
}

void PasswordFormManager::OnNeverClicked() {
  UploadPasswordVote(pending_credentials_, autofill::UNKNOWN_TYPE,
                     std::string());
  PermanentlyBlacklist();
}

void PasswordFormManager::OnNoInteraction(bool is_update) {
  if (is_update)
    UploadPasswordVote(observed_form_, autofill::PROBABLY_NEW_PASSWORD,
                       std::string());
  else {
    UploadPasswordVote(pending_credentials_, autofill::UNKNOWN_TYPE,
                       std::string());
  }
}

void PasswordFormManager::LogSubmitPassed() {
  if (submit_result_ != kSubmitResultFailed) {
    if (has_generated_password_) {
      metrics_util::LogPasswordGenerationSubmissionEvent(
          metrics_util::PASSWORD_SUBMITTED);
    } else if (generation_available_) {
      metrics_util::LogPasswordGenerationAvailableSubmissionEvent(
          metrics_util::PASSWORD_SUBMITTED);
    }
  }
  base::RecordAction(base::UserMetricsAction("PasswordManager_LoginPassed"));
  submit_result_ = kSubmitResultPassed;
}

void PasswordFormManager::LogSubmitFailed() {
  if (has_generated_password_) {
    metrics_util::LogPasswordGenerationSubmissionEvent(
        metrics_util::GENERATED_PASSWORD_FORCE_SAVED);
  } else if (generation_available_) {
    metrics_util::LogPasswordGenerationAvailableSubmissionEvent(
        metrics_util::PASSWORD_SUBMISSION_FAILED);
  }
  base::RecordAction(base::UserMetricsAction("PasswordManager_LoginFailed"));
  submit_result_ = kSubmitResultFailed;
}

void PasswordFormManager::WipeStoreCopyIfOutdated() {
  UMA_HISTOGRAM_BOOLEAN(
      "PasswordManager.StoreReadyWhenWiping",
      form_fetcher_->GetState() == FormFetcher::State::NOT_WAITING);

  form_saver_->WipeOutdatedCopies(pending_credentials_, &best_matches_,
                                  &preferred_match_);
}

void PasswordFormManager::SaveGenerationFieldDetectedByClassifier(
    const base::string16& generation_field) {
  form_classifier_outcome_ =
      generation_field.empty() ? kNoGenerationElement : kFoundGenerationElement;
  generation_element_detected_by_classifier_ = generation_field;
}

void PasswordFormManager::ResetStoredMatches() {
  preferred_match_ = nullptr;
  best_matches_.clear();
  not_best_matches_.clear();
  blacklisted_matches_.clear();
  new_blacklisted_.reset();
}

void PasswordFormManager::GrabFetcher(std::unique_ptr<FormFetcher> fetcher) {
  DCHECK(!owned_form_fetcher_);
  owned_form_fetcher_ = std::move(fetcher);
  if (owned_form_fetcher_.get() == form_fetcher_)
    return;
  ResetStoredMatches();
  form_fetcher_->RemoveConsumer(this);
  form_fetcher_ = owned_form_fetcher_.get();
  form_fetcher_->AddConsumer(this);
}

void PasswordFormManager::SendVotesOnSave() {
  if (observed_form_.IsPossibleChangePasswordFormWithoutUsername())
    return;

  // Send votes for sign-in form.
  autofill::FormData& form_data = pending_credentials_.form_data;
  if (form_data.fields.size() == 2 &&
      form_data.fields[0].form_control_type == "text" &&
      form_data.fields[1].form_control_type == "password") {
    // |form_data| is received from the renderer and does not contain field
    // values. Fill username field value with username to allow AutofillManager
    // to detect username autofill type.
    form_data.fields[0].value = pending_credentials_.username_value;
    SendSignInVote(form_data);
    return;
  }

  // Upload credentials the first time they are saved. This data is used
  // by password generation to help determine account creation sites.
  // Credentials that have been previously used (e.g., PSL matches) are checked
  // to see if they are valid account creation forms.
  if (pending_credentials_.times_used == 0) {
    autofill::ServerFieldType password_type = autofill::PASSWORD;
    if (does_look_like_signup_form_)
      password_type = autofill::PROBABLY_ACCOUNT_CREATION_PASSWORD;
    UploadPasswordVote(pending_credentials_, password_type, std::string());
    if (username_correction_vote_) {
      UploadPasswordVote(
          *username_correction_vote_, autofill::USERNAME,
          FormStructure(observed_form_.form_data).FormSignatureAsStr());
    }
  } else
    SendVoteOnCredentialsReuse(observed_form_, &pending_credentials_);
}

void PasswordFormManager::SendSignInVote(const FormData& form_data) {
  autofill::AutofillManager* autofill_manager =
      client_->GetAutofillManagerForMainFrame();
  if (!autofill_manager)
    return;
  std::unique_ptr<FormStructure> form_structure(new FormStructure(form_data));
  form_structure->set_is_signin_upload(true);
  DCHECK(form_structure->ShouldBeCrowdsourced());
  DCHECK_EQ(2u, form_structure->field_count());
  form_structure->field(1)->set_possible_types({autofill::PASSWORD});
  autofill_manager->StartUploadProcess(std::move(form_structure),
                                       base::TimeTicks::Now(), true);
}

void PasswordFormManager::SetUserAction(UserAction user_action) {
  if (user_action == kUserActionChoose) {
    base::RecordAction(
        base::UserMetricsAction("PasswordManager_UsedNonDefaultUsername"));
  } else if (user_action == kUserActionChoosePslMatch) {
    base::RecordAction(
        base::UserMetricsAction("PasswordManager_ChoseSubdomainPassword"));
  } else if (user_action == kUserActionOverridePassword) {
    base::RecordAction(
        base::UserMetricsAction("PasswordManager_LoggedInWithNewPassword"));
  } else if (user_action == kUserActionOverrideUsernameAndPassword) {
    base::RecordAction(
        base::UserMetricsAction("PasswordManager_LoggedInWithNewUsername"));
  } else {
    NOTREACHED();
  }
  user_action_ = user_action;
}

base::Optional<PasswordForm> PasswordFormManager::UpdatePendingAndGetOldKey(
    std::vector<PasswordForm>* credentials_to_update) {
  base::Optional<PasswordForm> old_primary_key;
  bool update_related_credentials = false;

  if (!selected_username_.empty()) {
    // Username has changed. We set this selected username as the real
    // username. Given that |username_value| is part of the Sync and
    // PasswordStore primary key, the old primary key must be supplied.
    old_primary_key = pending_credentials_;
    pending_credentials_.username_value = selected_username_;
    // TODO(crbug.com/188908) This branch currently never executes (bound to
    // the other usernames experiment). Updating related credentials would be
    // complicated, so we skip that, given it influences no users.
    update_related_credentials = false;
  } else if (observed_form_.new_password_element.empty() &&
             pending_credentials_.federation_origin.unique() &&
             !IsValidAndroidFacetURI(pending_credentials_.signon_realm) &&
             (pending_credentials_.password_element.empty() ||
              pending_credentials_.username_element.empty() ||
              pending_credentials_.submit_element.empty())) {
    // If |observed_form_| is a sign-in form and some of the element names are
    // empty, it is likely the first time a credential saved on a
    // sign-up/change password form is used.  Given that |password_element| and
    // |username_element| are part of Sync and PasswordStore primary key, the
    // old primary key must be used if the new names shal be saved.
    old_primary_key = pending_credentials_;
    pending_credentials_.password_element = observed_form_.password_element;
    pending_credentials_.username_element = observed_form_.username_element;
    pending_credentials_.submit_element = observed_form_.submit_element;
    update_related_credentials = true;
  } else {
    update_related_credentials =
        pending_credentials_.federation_origin.unique();
  }

  // If this was a password update, then update all non-best matches entries
  // with the same username and the same old password.
  if (update_related_credentials) {
    auto updated_password_it =
        best_matches_.find(pending_credentials_.username_value);
    DCHECK(best_matches_.end() != updated_password_it);
    const base::string16& old_password =
        updated_password_it->second->password_value;
    for (auto* not_best_match : not_best_matches_) {
      if (not_best_match->username_value ==
              pending_credentials_.username_value &&
          not_best_match->password_value == old_password) {
        credentials_to_update->push_back(*not_best_match);
        credentials_to_update->back().password_value =
            pending_credentials_.password_value;
      }
    }
  }

  return old_primary_key;
}

}  // namespace password_manager
