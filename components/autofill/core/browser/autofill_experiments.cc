// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/autofill/core/browser/autofill_experiments.h"

#include "base/command_line.h"
#include "base/feature_list.h"
#include "base/metrics/field_trial.h"
#include "base/strings/string16.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "build/build_config.h"
#include "components/autofill/core/browser/suggestion.h"
#include "components/autofill/core/common/autofill_pref_names.h"
#include "components/autofill/core/common/autofill_switches.h"
#include "components/prefs/pref_service.h"
#include "components/strings/grit/components_strings.h"
#include "components/sync/driver/sync_service.h"
#include "components/variations/variations_associated_data.h"
#include "google_apis/gaia/gaia_auth_util.h"
#include "ui/base/l10n/l10n_util.h"

namespace autofill {

const base::Feature kAutofillCreditCardAssist{
    "AutofillCreditCardAssist", base::FEATURE_DISABLED_BY_DEFAULT};
const base::Feature kAutofillScanCardholderName{
    "AutofillScanCardholderName", base::FEATURE_DISABLED_BY_DEFAULT};
const base::Feature kAutofillCreditCardPopupLayout{
    "AutofillCreditCardPopupLayout", base::FEATURE_DISABLED_BY_DEFAULT};
const base::Feature kAutofillCreditCardLastUsedDateDisplay{
    "AutofillCreditCardLastUsedDateDisplay", base::FEATURE_DISABLED_BY_DEFAULT};
const base::Feature kAutofillUpstreamRequestCvcIfMissing{
    "AutofillUpstreamRequestCvcIfMissing", base::FEATURE_DISABLED_BY_DEFAULT};
const base::Feature kAutofillUpstreamUseAutofillProfileComparatorForName{
    "AutofillUpstreamUseAutofillProfileComparatorForName",
    base::FEATURE_ENABLED_BY_DEFAULT};
const base::Feature kAutofillUpstreamUseNotRecentlyUsedAutofillProfile{
    "AutofillUpstreamUseNotRecentlyUsedAutofillProfile",
    base::FEATURE_DISABLED_BY_DEFAULT};
const char kCreditCardSigninPromoImpressionLimitParamKey[] = "impression_limit";
const char kAutofillCreditCardPopupBackgroundColorKey[] = "background_color";
const char kAutofillCreditCardPopupDividerColorKey[] = "dropdown_divider_color";
const char kAutofillCreditCardPopupValueBoldKey[] = "is_value_bold";
const char kAutofillCreditCardPopupIsValueAndLabelInSingleLineKey[] =
    "is_value_and_label_in_single_line";
const char kAutofillPopupDropdownItemHeightKey[] = "dropdown_item_height";
const char kAutofillCreditCardPopupIsIconAtStartKey[] =
    "is_credit_card_icon_at_start";
const char kAutofillPopupMarginKey[] = "margin";
const char kAutofillCreditCardLastUsedDateShowExpirationDateKey[] =
    "show_expiration_date";
const char kAutofillUpstreamMaxMinutesSinceAutofillProfileUseKey[] =
    "max_minutes_since_autofill_profile_use";

namespace {

// Returns parameter value in |kAutofillCreditCardPopupLayout| feature, or 0 if
// parameter is not specified.
unsigned int GetCreditCardPopupParameterUintValue(
    const std::string& param_name) {
  unsigned int value;
  const std::string param_value = variations::GetVariationParamValueByFeature(
      kAutofillCreditCardPopupLayout, param_name);
  if (!param_value.empty() && base::StringToUint(param_value, &value))
    return value;
  return 0;
}

}  // namespace

bool IsAutofillEnabled(const PrefService* pref_service) {
  return pref_service->GetBoolean(prefs::kAutofillEnabled);
}

bool IsInAutofillSuggestionsDisabledExperiment() {
  std::string group_name =
      base::FieldTrialList::FindFullName("AutofillEnabled");
  return group_name == "Disabled";
}

bool IsAutofillCreditCardAssistEnabled() {
#if !defined(OS_ANDROID) && !defined(OS_IOS)
  return false;
#else
  return base::FeatureList::IsEnabled(kAutofillCreditCardAssist);
#endif
}

bool IsAutofillCreditCardPopupLayoutExperimentEnabled() {
  return base::FeatureList::IsEnabled(kAutofillCreditCardPopupLayout);
}

bool IsAutofillCreditCardLastUsedDateDisplayExperimentEnabled() {
  return base::FeatureList::IsEnabled(kAutofillCreditCardLastUsedDateDisplay);
}

// |GetCreditCardPopupParameterUintValue| returns 0 if experiment parameter is
// not specified. 0 == |SK_ColorTRANSPARENT|.
SkColor GetCreditCardPopupBackgroundColor() {
  return GetCreditCardPopupParameterUintValue(
      kAutofillCreditCardPopupBackgroundColorKey);
}

SkColor GetCreditCardPopupDividerColor() {
  return GetCreditCardPopupParameterUintValue(
      kAutofillCreditCardPopupDividerColorKey);
}

bool IsCreditCardPopupValueBold() {
  const std::string param_value = variations::GetVariationParamValueByFeature(
      kAutofillCreditCardPopupLayout, kAutofillCreditCardPopupValueBoldKey);
  return param_value == "true";
}

unsigned int GetPopupDropdownItemHeight() {
  return GetCreditCardPopupParameterUintValue(
      kAutofillPopupDropdownItemHeightKey);
}

bool IsIconInCreditCardPopupAtStart() {
  const std::string param_value = variations::GetVariationParamValueByFeature(
      kAutofillCreditCardPopupLayout, kAutofillCreditCardPopupIsIconAtStartKey);
  return param_value == "true";
}

bool ShowExpirationDateInAutofillCreditCardLastUsedDate() {
  const std::string param_value = variations::GetVariationParamValueByFeature(
      kAutofillCreditCardLastUsedDateDisplay,
      kAutofillCreditCardLastUsedDateShowExpirationDateKey);
  return param_value == "true";
}

// Modifies |suggestion| as follows if experiment to display value and label in
// a single line is enabled.
// Say, |value| is 'Visa ....1111' and |label| is '01/18' (expiration date).
// Modifies |value| to 'Visa ....1111, exp 01/18' and clears |label|.
void ModifyAutofillCreditCardSuggestion(Suggestion* suggestion) {
  DCHECK(IsAutofillCreditCardPopupLayoutExperimentEnabled());
  const std::string param_value = variations::GetVariationParamValueByFeature(
      kAutofillCreditCardPopupLayout,
      kAutofillCreditCardPopupIsValueAndLabelInSingleLineKey);
  if (param_value == "true") {
    const base::string16 format_string = l10n_util::GetStringUTF16(
        IDS_AUTOFILL_CREDIT_CARD_EXPIRATION_DATE_LABEL_AND_ABBR);
    if (!format_string.empty()) {
      suggestion->value.append(l10n_util::GetStringFUTF16(
          IDS_AUTOFILL_CREDIT_CARD_EXPIRATION_DATE_LABEL_AND_ABBR,
          suggestion->label));
    }
    suggestion->label.clear();
  }
}

unsigned int GetPopupMargin() {
  return GetCreditCardPopupParameterUintValue(kAutofillPopupMarginKey);
}

bool OfferStoreUnmaskedCards() {
#if defined(OS_LINUX) && !defined(OS_CHROMEOS)
  // The checkbox can be forced on with a flag, but by default we don't store
  // on Linux due to lack of system keychain integration. See crbug.com/162735
  return base::CommandLine::ForCurrentProcess()->HasSwitch(
      switches::kEnableOfferStoreUnmaskedWalletCards);
#else
  // Query the field trial before checking command line flags to ensure UMA
  // reports the correct group.
  std::string group_name =
      base::FieldTrialList::FindFullName("OfferStoreUnmaskedWalletCards");

  // The checkbox can be forced on or off with flags.
  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kEnableOfferStoreUnmaskedWalletCards))
    return true;
  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kDisableOfferStoreUnmaskedWalletCards))
    return false;

  // Otherwise use the field trial to show the checkbox or not.
  return group_name != "Disabled";
#endif
}

bool IsCreditCardUploadEnabled(const PrefService* pref_service,
                               const syncer::SyncService* sync_service,
                               const std::string& user_email) {
  // Query the field trial first to ensure UMA always reports the correct group.
  std::string group_name =
      base::FieldTrialList::FindFullName("OfferUploadCreditCards");

  // Check Autofill sync setting.
  if (!(sync_service && sync_service->CanSyncStart() &&
        sync_service->GetPreferredDataTypes().Has(syncer::AUTOFILL_PROFILE))) {
    return false;
  }

  // Users who have enabled a passphrase have chosen to not make their sync
  // information accessible to Google. Since upload makes credit card data
  // available to other Google systems, disable it for passphrase users.
  // We can't determine the passphrase state until the sync engine is
  // initialized so disable upload if sync is not yet available.
  if (!sync_service->IsEngineInitialized() ||
      sync_service->IsUsingSecondaryPassphrase()) {
    return false;
  }

  // Check Payments integration user setting.
  if (!pref_service->GetBoolean(prefs::kAutofillWalletImportEnabled))
    return false;

  // Check that the user is logged into a supported domain.
  if (user_email.empty())
    return false;
  std::string domain = gaia::ExtractDomainName(user_email);
  if (!(domain == "googlemail.com" || domain == "gmail.com" ||
        domain == "google.com")) {
    return false;
  }

  // With the user settings and logged in state verified, now we can consult the
  // command-line flags and experiment settings.
  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kEnableOfferUploadCreditCards)) {
    return true;
  }
  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kDisableOfferUploadCreditCards)) {
    return false;
  }

  return !group_name.empty() && group_name != "Disabled";
}

bool IsAutofillUpstreamRequestCvcIfMissingExperimentEnabled() {
#if defined(OS_ANDROID)
  return false;
#else
  return base::FeatureList::IsEnabled(kAutofillUpstreamRequestCvcIfMissing);
#endif
}

base::TimeDelta GetMaxTimeSinceAutofillProfileUseForCardUpload() {
  int value;
  const std::string param_value = variations::GetVariationParamValueByFeature(
      kAutofillUpstreamUseNotRecentlyUsedAutofillProfile,
      kAutofillUpstreamMaxMinutesSinceAutofillProfileUseKey);
  if (!param_value.empty() && base::StringToInt(param_value, &value))
    return base::TimeDelta::FromMinutes(value);
  return base::TimeDelta();
}

}  // namespace autofill
