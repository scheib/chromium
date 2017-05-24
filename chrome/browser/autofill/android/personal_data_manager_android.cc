// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/autofill/android/personal_data_manager_android.h"

#include <stddef.h>
#include <algorithm>
#include <memory>
#include <utility>

#include "base/android/jni_array.h"
#include "base/android/jni_string.h"
#include "base/command_line.h"
#include "base/format_macros.h"
#include "base/memory/ptr_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "chrome/browser/android/resource_mapper.h"
#include "chrome/browser/autofill/personal_data_manager_factory.h"
#include "chrome/browser/autofill/validation_rules_storage_factory.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/common/pref_names.h"
#include "components/autofill/content/browser/content_autofill_driver.h"
#include "components/autofill/content/browser/content_autofill_driver_factory.h"
#include "components/autofill/core/browser/address_i18n.h"
#include "components/autofill/core/browser/autofill_country.h"
#include "components/autofill/core/browser/autofill_data_util.h"
#include "components/autofill/core/browser/autofill_type.h"
#include "components/autofill/core/browser/country_names.h"
#include "components/autofill/core/browser/field_types.h"
#include "components/autofill/core/browser/payments/full_card_request.h"
#include "components/autofill/core/browser/personal_data_manager.h"
#include "components/autofill/core/browser/validation.h"
#include "components/autofill/core/common/autofill_constants.h"
#include "components/autofill/core/common/autofill_pref_names.h"
#include "components/autofill/core/common/autofill_switches.h"
#include "components/grit/components_scaled_resources.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_contents.h"
#include "jni/PersonalDataManager_jni.h"
#include "third_party/libaddressinput/chromium/chrome_metadata_source.h"
#include "third_party/libaddressinput/chromium/chrome_storage_impl.h"

using base::android::ConvertJavaStringToUTF8;
using base::android::ConvertUTF16ToJavaString;
using base::android::ConvertUTF8ToJavaString;
using base::android::JavaParamRef;
using base::android::ScopedJavaGlobalRef;
using base::android::ScopedJavaLocalRef;

namespace autofill {
namespace {

Profile* GetProfile() {
  return ProfileManager::GetActiveUserProfile()->GetOriginalProfile();
}

PrefService* GetPrefs() {
  return GetProfile()->GetPrefs();
}

ScopedJavaLocalRef<jobject> CreateJavaProfileFromNative(
    JNIEnv* env,
    const AutofillProfile& profile) {
  return Java_AutofillProfile_create(
      env, ConvertUTF8ToJavaString(env, profile.guid()),
      ConvertUTF8ToJavaString(env, profile.origin()),
      profile.record_type() == AutofillProfile::LOCAL_PROFILE,
      ConvertUTF16ToJavaString(
          env, profile.GetInfo(AutofillType(NAME_FULL),
                               g_browser_process->GetApplicationLocale())),
      ConvertUTF16ToJavaString(env, profile.GetRawInfo(COMPANY_NAME)),
      ConvertUTF16ToJavaString(env,
                               profile.GetRawInfo(ADDRESS_HOME_STREET_ADDRESS)),
      ConvertUTF16ToJavaString(env, profile.GetRawInfo(ADDRESS_HOME_STATE)),
      ConvertUTF16ToJavaString(env, profile.GetRawInfo(ADDRESS_HOME_CITY)),
      ConvertUTF16ToJavaString(
          env, profile.GetRawInfo(ADDRESS_HOME_DEPENDENT_LOCALITY)),
      ConvertUTF16ToJavaString(env, profile.GetRawInfo(ADDRESS_HOME_ZIP)),
      ConvertUTF16ToJavaString(env,
                               profile.GetRawInfo(ADDRESS_HOME_SORTING_CODE)),
      ConvertUTF16ToJavaString(env, profile.GetRawInfo(ADDRESS_HOME_COUNTRY)),
      ConvertUTF16ToJavaString(env,
                               profile.GetRawInfo(PHONE_HOME_WHOLE_NUMBER)),
      ConvertUTF16ToJavaString(env, profile.GetRawInfo(EMAIL_ADDRESS)),
      ConvertUTF8ToJavaString(env, profile.language_code()));
}

void MaybeSetRawInfo(AutofillProfile* profile,
                     autofill::ServerFieldType type,
                     const base::android::JavaRef<jstring>& jstr) {
  if (!jstr.is_null())
    profile->SetRawInfo(type, ConvertJavaStringToUTF16(jstr));
}

void PopulateNativeProfileFromJava(
    const JavaParamRef<jobject>& jprofile,
    JNIEnv* env,
    AutofillProfile* profile) {
  profile->set_origin(
      ConvertJavaStringToUTF8(Java_AutofillProfile_getOrigin(env, jprofile)));
  profile->SetInfo(
      AutofillType(NAME_FULL),
      ConvertJavaStringToUTF16(Java_AutofillProfile_getFullName(env, jprofile)),
      g_browser_process->GetApplicationLocale());
  MaybeSetRawInfo(profile, autofill::COMPANY_NAME,
                  Java_AutofillProfile_getCompanyName(env, jprofile));
  MaybeSetRawInfo(profile, autofill::ADDRESS_HOME_STREET_ADDRESS,
                  Java_AutofillProfile_getStreetAddress(env, jprofile));
  MaybeSetRawInfo(profile, autofill::ADDRESS_HOME_STATE,
                  Java_AutofillProfile_getRegion(env, jprofile));
  MaybeSetRawInfo(profile, autofill::ADDRESS_HOME_CITY,
                  Java_AutofillProfile_getLocality(env, jprofile));
  MaybeSetRawInfo(profile, autofill::ADDRESS_HOME_DEPENDENT_LOCALITY,
                  Java_AutofillProfile_getDependentLocality(env, jprofile));
  MaybeSetRawInfo(profile, autofill::ADDRESS_HOME_ZIP,
                  Java_AutofillProfile_getPostalCode(env, jprofile));
  MaybeSetRawInfo(profile, autofill::ADDRESS_HOME_SORTING_CODE,
                  Java_AutofillProfile_getSortingCode(env, jprofile));
  ScopedJavaLocalRef<jstring> country_code =
      Java_AutofillProfile_getCountryCode(env, jprofile);
  if (!country_code.is_null()) {
    profile->SetInfo(AutofillType(ADDRESS_HOME_COUNTRY),
                     ConvertJavaStringToUTF16(country_code),
                     g_browser_process->GetApplicationLocale());
  }
  MaybeSetRawInfo(profile, autofill::PHONE_HOME_WHOLE_NUMBER,
                  Java_AutofillProfile_getPhoneNumber(env, jprofile));
  MaybeSetRawInfo(profile, autofill::EMAIL_ADDRESS,
                  Java_AutofillProfile_getEmailAddress(env, jprofile));
  profile->set_language_code(ConvertJavaStringToUTF8(
      Java_AutofillProfile_getLanguageCode(env, jprofile)));
}

ScopedJavaLocalRef<jobject> CreateJavaCreditCardFromNative(
    JNIEnv* env,
    const CreditCard& card) {
  const data_util::PaymentRequestData& payment_request_data =
      data_util::GetPaymentRequestData(card.network());
  return Java_CreditCard_create(
      env, ConvertUTF8ToJavaString(env, card.guid()),
      ConvertUTF8ToJavaString(env, card.origin()),
      card.record_type() == CreditCard::LOCAL_CARD,
      card.record_type() == CreditCard::FULL_SERVER_CARD,
      ConvertUTF16ToJavaString(env, card.GetRawInfo(CREDIT_CARD_NAME_FULL)),
      ConvertUTF16ToJavaString(env, card.GetRawInfo(CREDIT_CARD_NUMBER)),
      ConvertUTF16ToJavaString(env, card.NetworkAndLastFourDigits()),
      ConvertUTF16ToJavaString(env, card.GetRawInfo(CREDIT_CARD_EXP_MONTH)),
      ConvertUTF16ToJavaString(env,
                               card.GetRawInfo(CREDIT_CARD_EXP_4_DIGIT_YEAR)),
      ConvertUTF8ToJavaString(env,
                              payment_request_data.basic_card_issuer_network),
      ResourceMapper::MapFromChromiumId(payment_request_data.icon_resource_id),
      ConvertUTF8ToJavaString(env, card.billing_address_id()),
      ConvertUTF8ToJavaString(env, card.server_id()));
}

void PopulateNativeCreditCardFromJava(
    const jobject& jcard,
    JNIEnv* env,
    CreditCard* card) {
  card->set_origin(
      ConvertJavaStringToUTF8(Java_CreditCard_getOrigin(env, jcard)));
  card->SetRawInfo(
      CREDIT_CARD_NAME_FULL,
      ConvertJavaStringToUTF16(Java_CreditCard_getName(env, jcard)));
  card->SetRawInfo(
      CREDIT_CARD_NUMBER,
      ConvertJavaStringToUTF16(Java_CreditCard_getNumber(env, jcard)));
  card->SetRawInfo(
      CREDIT_CARD_EXP_MONTH,
      ConvertJavaStringToUTF16(Java_CreditCard_getMonth(env, jcard)));
  card->SetRawInfo(
      CREDIT_CARD_EXP_4_DIGIT_YEAR,
      ConvertJavaStringToUTF16(Java_CreditCard_getYear(env, jcard)));
  card->set_billing_address_id(
      ConvertJavaStringToUTF8(Java_CreditCard_getBillingAddressId(env, jcard)));
  card->set_server_id(
      ConvertJavaStringToUTF8(Java_CreditCard_getServerId(env, jcard)));

  // Only set the guid if it is an existing card (java guid not empty).
  // Otherwise, keep the generated one.
  std::string guid =
      ConvertJavaStringToUTF8(Java_CreditCard_getGUID(env, jcard));
  if (!guid.empty())
    card->set_guid(guid);

  if (Java_CreditCard_getIsLocal(env, jcard)) {
    card->set_record_type(CreditCard::LOCAL_CARD);
  } else {
    if (Java_CreditCard_getIsCached(env, jcard)) {
      card->set_record_type(CreditCard::FULL_SERVER_CARD);
    } else {
      card->set_record_type(CreditCard::MASKED_SERVER_CARD);
      card->SetNetworkForMaskedCard(
          data_util::GetIssuerNetworkForBasicCardIssuerNetwork(
              ConvertJavaStringToUTF8(
                  env, Java_CreditCard_getBasicCardIssuerNetwork(env, jcard))));
    }
  }
}

// Self-deleting requester of full card details, including full PAN and the CVC
// number.
class FullCardRequester : public payments::FullCardRequest::ResultDelegate,
                          public base::SupportsWeakPtr<FullCardRequester> {
 public:
  FullCardRequester() {}

  // Takes ownership of |card|.
  void GetFullCard(JNIEnv* env,
                   const base::android::JavaParamRef<jobject>& jweb_contents,
                   const base::android::JavaParamRef<jobject>& jdelegate,
                   std::unique_ptr<CreditCard> card) {
    card_ = std::move(card);
    jdelegate_.Reset(env, jdelegate);

    if (!card_) {
      OnFullCardRequestFailed();
      return;
    }

    content::WebContents* contents =
        content::WebContents::FromJavaWebContents(jweb_contents);
    if (!contents) {
      OnFullCardRequestFailed();
      return;
    }

    ContentAutofillDriverFactory* factory =
        ContentAutofillDriverFactory::FromWebContents(contents);
    if (!factory) {
      OnFullCardRequestFailed();
      return;
    }

    ContentAutofillDriver* driver =
        factory->DriverForFrame(contents->GetMainFrame());
    if (!driver) {
      OnFullCardRequestFailed();
      return;
    }

    driver->autofill_manager()->GetOrCreateFullCardRequest()->GetFullCard(
        *card_, AutofillClient::UNMASK_FOR_PAYMENT_REQUEST, AsWeakPtr(),
        driver->autofill_manager()->GetAsFullCardRequestUIDelegate());
  }

 private:
  ~FullCardRequester() override {}

  // payments::FullCardRequest::ResultDelegate:
  void OnFullCardRequestSucceeded(const CreditCard& card,
                                  const base::string16& cvc) override {
    JNIEnv* env = base::android::AttachCurrentThread();
    Java_FullCardRequestDelegate_onFullCardDetails(
        env, jdelegate_, CreateJavaCreditCardFromNative(env, card),
        base::android::ConvertUTF16ToJavaString(env, cvc));
    delete this;
  }

  // payments::FullCardRequest::ResultDelegate:
  void OnFullCardRequestFailed() override {
    JNIEnv* env = base::android::AttachCurrentThread();
    Java_FullCardRequestDelegate_onFullCardError(env, jdelegate_);
    delete this;
  }

  std::unique_ptr<CreditCard> card_;
  ScopedJavaGlobalRef<jobject> jdelegate_;

  DISALLOW_COPY_AND_ASSIGN(FullCardRequester);
};

class AndroidAddressNormalizerDelegate
    : public ::payments::AddressNormalizer::Delegate,
      public base::SupportsWeakPtr<AndroidAddressNormalizerDelegate> {
 public:
  AndroidAddressNormalizerDelegate(
      JNIEnv* env,
      const base::android::JavaParamRef<jobject>& jdelegate) {
    jdelegate_.Reset(env, jdelegate);
  }

  void OnAddressNormalized(const AutofillProfile& normalized_profile) override {
    JNIEnv* env = base::android::AttachCurrentThread();
    Java_NormalizedAddressRequestDelegate_onAddressNormalized(
        env, jdelegate_, CreateJavaProfileFromNative(env, normalized_profile));
    delete this;
  }

  void OnCouldNotNormalize(const AutofillProfile& profile) override {
    JNIEnv* env = base::android::AttachCurrentThread();
    Java_NormalizedAddressRequestDelegate_onCouldNotNormalize(
        env, jdelegate_, CreateJavaProfileFromNative(env, profile));
    delete this;
  }

 private:
  ~AndroidAddressNormalizerDelegate() override {}

  ScopedJavaGlobalRef<jobject> jdelegate_;

  DISALLOW_COPY_AND_ASSIGN(AndroidAddressNormalizerDelegate);
};

class AndroidSubKeyRequesterDelegate
    : public PersonalDataManagerAndroid::SubKeyRequestDelegate,
      public base::SupportsWeakPtr<AndroidSubKeyRequesterDelegate> {
 public:
  AndroidSubKeyRequesterDelegate(
      JNIEnv* env,
      const base::android::JavaParamRef<jobject>& jdelegate,
      const std::string& region_code,
      base::WeakPtr<PersonalDataManagerAndroid> personal_data_manager_android) {
    jdelegate_.Reset(env, jdelegate);
    region_code_ = region_code;
    personal_data_manager_android_ = personal_data_manager_android;
  }

  ~AndroidSubKeyRequesterDelegate() override {}

 private:
  // PersonalDataManagerAndroid::SubKeyRequestDelegate:
  void OnRulesSuccessfullyLoaded() override {
    if (personal_data_manager_android_) {
      JNIEnv* env = base::android::AttachCurrentThread();
      Java_GetSubKeysRequestDelegate_onSubKeysReceived(
          env, jdelegate_,
          personal_data_manager_android_->GetSubKeys(env, region_code_));
    }
  }

  ScopedJavaGlobalRef<jobject> jdelegate_;
  std::string region_code_;
  base::WeakPtr<PersonalDataManagerAndroid> personal_data_manager_android_;

  DISALLOW_COPY_AND_ASSIGN(AndroidSubKeyRequesterDelegate);
};

}  // namespace

PersonalDataManagerAndroid::PersonalDataManagerAndroid(JNIEnv* env, jobject obj)
    : weak_java_obj_(env, obj),
      personal_data_manager_(PersonalDataManagerFactory::GetForProfile(
          ProfileManager::GetActiveUserProfile())),
      address_normalizer_(
          std::unique_ptr<::i18n::addressinput::Source>(
              new autofill::ChromeMetadataSource(
                  I18N_ADDRESS_VALIDATION_DATA_URL,
                  personal_data_manager_->GetURLRequestContextGetter())),
          ValidationRulesStorageFactory::CreateStorage()),
      address_validator_(
          base::MakeUnique<autofill::ChromeMetadataSource>(
              I18N_ADDRESS_VALIDATION_DATA_URL,
              personal_data_manager_->GetURLRequestContextGetter()),
          ValidationRulesStorageFactory::CreateStorage(),
          this) {
  personal_data_manager_->AddObserver(this);
}

PersonalDataManagerAndroid::~PersonalDataManagerAndroid() {
  personal_data_manager_->RemoveObserver(this);
}

jboolean PersonalDataManagerAndroid::IsDataLoaded(
    JNIEnv* env,
    const base::android::JavaParamRef<jobject>& unused_obj) const {
  return personal_data_manager_->IsDataLoaded();
}

ScopedJavaLocalRef<jobjectArray>
PersonalDataManagerAndroid::GetProfileGUIDsForSettings(
    JNIEnv* env,
    const JavaParamRef<jobject>& unused_obj) {
  return GetProfileGUIDs(env, personal_data_manager_->GetProfiles());
}

ScopedJavaLocalRef<jobjectArray>
PersonalDataManagerAndroid::GetProfileGUIDsToSuggest(
    JNIEnv* env,
    const JavaParamRef<jobject>& unused_obj) {
  return GetProfileGUIDs(env, personal_data_manager_->GetProfilesToSuggest());
}

ScopedJavaLocalRef<jobject> PersonalDataManagerAndroid::GetProfileByGUID(
    JNIEnv* env,
    const JavaParamRef<jobject>& unused_obj,
    const JavaParamRef<jstring>& jguid) {
  AutofillProfile* profile = personal_data_manager_->GetProfileByGUID(
      ConvertJavaStringToUTF8(env, jguid));
  if (!profile)
    return ScopedJavaLocalRef<jobject>();

  return CreateJavaProfileFromNative(env, *profile);
}

ScopedJavaLocalRef<jstring> PersonalDataManagerAndroid::SetProfile(
    JNIEnv* env,
    const JavaParamRef<jobject>& unused_obj,
    const JavaParamRef<jobject>& jprofile) {
  std::string guid = ConvertJavaStringToUTF8(
      env,
      Java_AutofillProfile_getGUID(env, jprofile).obj());

  AutofillProfile profile;
  PopulateNativeProfileFromJava(jprofile, env, &profile);

  if (guid.empty()) {
    personal_data_manager_->AddProfile(profile);
  } else {
    profile.set_guid(guid);
    personal_data_manager_->UpdateProfile(profile);
  }

  return ConvertUTF8ToJavaString(env, profile.guid());
}

ScopedJavaLocalRef<jstring> PersonalDataManagerAndroid::SetProfileToLocal(
    JNIEnv* env,
    const JavaParamRef<jobject>& unused_obj,
    const JavaParamRef<jobject>& jprofile) {
  AutofillProfile profile;
  PopulateNativeProfileFromJava(jprofile, env, &profile);

  AutofillProfile* target_profile =
      personal_data_manager_->GetProfileByGUID(ConvertJavaStringToUTF8(
          env, Java_AutofillProfile_getGUID(env, jprofile).obj()));

  if (target_profile != nullptr &&
      target_profile->record_type() == AutofillProfile::LOCAL_PROFILE) {
    profile.set_guid(target_profile->guid());
    personal_data_manager_->UpdateProfile(profile);
  } else {
    personal_data_manager_->AddProfile(profile);
  }

  return ConvertUTF8ToJavaString(env, profile.guid());
}

ScopedJavaLocalRef<jobjectArray>
PersonalDataManagerAndroid::GetProfileLabelsForSettings(
    JNIEnv* env,
    const JavaParamRef<jobject>& unused_obj) {
  return GetProfileLabels(env, false /* address_only */,
                          false /* include_name_in_label */,
                          true /* include_organization_in_label */,
                          true /* include_country_in_label */,
                          personal_data_manager_->GetProfiles());
}

ScopedJavaLocalRef<jobjectArray>
PersonalDataManagerAndroid::GetProfileLabelsToSuggest(
    JNIEnv* env,
    const JavaParamRef<jobject>& unused_obj,
    jboolean include_name_in_label,
    jboolean include_organization_in_label,
    jboolean include_country_in_label) {
  return GetProfileLabels(env, true /* address_only */, include_name_in_label,
                          include_organization_in_label,
                          include_country_in_label,
                          personal_data_manager_->GetProfilesToSuggest());
}

base::android::ScopedJavaLocalRef<jstring>
PersonalDataManagerAndroid::GetShippingAddressLabelWithCountryForPaymentRequest(
    JNIEnv* env,
    const base::android::JavaParamRef<jobject>& unused_obj,
    const base::android::JavaParamRef<jobject>& jprofile) {
  return GetShippingAddressLabelForPaymentRequest(
      env, jprofile, true /* include_country_in_label */);
}

base::android::ScopedJavaLocalRef<jstring> PersonalDataManagerAndroid::
    GetShippingAddressLabelWithoutCountryForPaymentRequest(
        JNIEnv* env,
        const base::android::JavaParamRef<jobject>& unused_obj,
        const base::android::JavaParamRef<jobject>& jprofile) {
  return GetShippingAddressLabelForPaymentRequest(
      env, jprofile, false /* include_country_in_label */);
}

base::android::ScopedJavaLocalRef<jstring>
PersonalDataManagerAndroid::GetBillingAddressLabelForPaymentRequest(
    JNIEnv* env,
    const base::android::JavaParamRef<jobject>& unused_obj,
    const base::android::JavaParamRef<jobject>& jprofile) {
  // The company name and country are not included in the billing address label.
  std::vector<ServerFieldType> label_fields;
  label_fields.push_back(NAME_FULL);
  label_fields.push_back(ADDRESS_HOME_LINE1);
  label_fields.push_back(ADDRESS_HOME_LINE2);
  label_fields.push_back(ADDRESS_HOME_DEPENDENT_LOCALITY);
  label_fields.push_back(ADDRESS_HOME_CITY);
  label_fields.push_back(ADDRESS_HOME_STATE);
  label_fields.push_back(ADDRESS_HOME_ZIP);
  label_fields.push_back(ADDRESS_HOME_SORTING_CODE);

  AutofillProfile profile;
  PopulateNativeProfileFromJava(jprofile, env, &profile);

  return ConvertUTF16ToJavaString(
      env, profile.ConstructInferredLabel(
               label_fields, label_fields.size(),
               g_browser_process->GetApplicationLocale()));
}

base::android::ScopedJavaLocalRef<jobjectArray>
PersonalDataManagerAndroid::GetCreditCardGUIDsForSettings(
    JNIEnv* env,
    const base::android::JavaParamRef<jobject>& unused_obj) {
  return GetCreditCardGUIDs(env, personal_data_manager_->GetCreditCards());
}

base::android::ScopedJavaLocalRef<jobjectArray>
PersonalDataManagerAndroid::GetCreditCardGUIDsToSuggest(
    JNIEnv* env,
    const base::android::JavaParamRef<jobject>& unused_obj) {
  return GetCreditCardGUIDs(env,
                            personal_data_manager_->GetCreditCardsToSuggest());
}

ScopedJavaLocalRef<jobject> PersonalDataManagerAndroid::GetCreditCardByGUID(
    JNIEnv* env,
    const JavaParamRef<jobject>& unused_obj,
    const JavaParamRef<jstring>& jguid) {
  CreditCard* card = personal_data_manager_->GetCreditCardByGUID(
          ConvertJavaStringToUTF8(env, jguid));
  if (!card)
    return ScopedJavaLocalRef<jobject>();

  return CreateJavaCreditCardFromNative(env, *card);
}

ScopedJavaLocalRef<jobject> PersonalDataManagerAndroid::GetCreditCardForNumber(
    JNIEnv* env,
    const JavaParamRef<jobject>& unused_obj,
    const JavaParamRef<jstring>& jcard_number) {
  // A local card with empty GUID.
  CreditCard card("", "");
  card.SetNumber(ConvertJavaStringToUTF16(env, jcard_number));
  return CreateJavaCreditCardFromNative(env, card);
}

ScopedJavaLocalRef<jstring> PersonalDataManagerAndroid::SetCreditCard(
    JNIEnv* env,
    const JavaParamRef<jobject>& unused_obj,
    const JavaParamRef<jobject>& jcard) {
  std::string guid = ConvertJavaStringToUTF8(
       env,
       Java_CreditCard_getGUID(env, jcard).obj());

  CreditCard card;
  PopulateNativeCreditCardFromJava(jcard, env, &card);

  if (guid.empty()) {
    personal_data_manager_->AddCreditCard(card);
  } else {
    card.set_guid(guid);
    personal_data_manager_->UpdateCreditCard(card);
  }
  return ConvertUTF8ToJavaString(env, card.guid());
}

void PersonalDataManagerAndroid::UpdateServerCardBillingAddress(
    JNIEnv* env,
    const JavaParamRef<jobject>& unused_obj,
    const JavaParamRef<jobject>& jcard) {
  CreditCard card;
  PopulateNativeCreditCardFromJava(jcard, env, &card);

  personal_data_manager_->UpdateServerCardMetadata(card);
}

ScopedJavaLocalRef<jstring>
PersonalDataManagerAndroid::GetBasicCardIssuerNetwork(
    JNIEnv* env,
    const JavaParamRef<jobject>& unused_obj,
    const JavaParamRef<jstring>& jcard_number,
    const jboolean jempty_if_invalid) {
  base::string16 card_number = ConvertJavaStringToUTF16(env, jcard_number);

  if (static_cast<bool>(jempty_if_invalid) &&
      !IsValidCreditCardNumber(card_number)) {
    return ConvertUTF8ToJavaString(env, "");
  }
  return ConvertUTF8ToJavaString(
      env,
      data_util::GetPaymentRequestData(CreditCard::GetCardNetwork(card_number))
          .basic_card_issuer_network);
}

void PersonalDataManagerAndroid::AddServerCreditCardForTest(
    JNIEnv* env,
    const base::android::JavaParamRef<jobject>& unused_obj,
    const base::android::JavaParamRef<jobject>& jcard) {
  std::unique_ptr<CreditCard> card(new CreditCard);
  PopulateNativeCreditCardFromJava(jcard, env, card.get());
  card->set_record_type(CreditCard::MASKED_SERVER_CARD);
  personal_data_manager_->AddServerCreditCardForTest(std::move(card));
  personal_data_manager_->NotifyPersonalDataChangedForTest();
}

void PersonalDataManagerAndroid::RemoveByGUID(
    JNIEnv* env,
    const JavaParamRef<jobject>& unused_obj,
    const JavaParamRef<jstring>& jguid) {
  personal_data_manager_->RemoveByGUID(ConvertJavaStringToUTF8(env, jguid));
}

void PersonalDataManagerAndroid::ClearUnmaskedCache(
    JNIEnv* env,
    const JavaParamRef<jobject>& unused_obj,
    const JavaParamRef<jstring>& guid) {
  personal_data_manager_->ResetFullServerCard(
      ConvertJavaStringToUTF8(env, guid));
}

void PersonalDataManagerAndroid::GetFullCardForPaymentRequest(
    JNIEnv* env,
    const JavaParamRef<jobject>& unused_obj,
    const JavaParamRef<jobject>& jweb_contents,
    const JavaParamRef<jobject>& jcard,
    const JavaParamRef<jobject>& jdelegate) {
  std::unique_ptr<CreditCard> card(new CreditCard);
  PopulateNativeCreditCardFromJava(jcard, env, card.get());
  // Self-deleting object.
  (new FullCardRequester())->GetFullCard(
      env, jweb_contents, jdelegate, std::move(card));
}

void PersonalDataManagerAndroid::OnPersonalDataChanged() {
  JNIEnv* env = base::android::AttachCurrentThread();
  auto java_obj = weak_java_obj_.get(env);
  if (java_obj.is_null())
    return;

  Java_PersonalDataManager_personalDataChanged(env, java_obj);
}

// static
bool PersonalDataManagerAndroid::Register(JNIEnv* env) {
  return RegisterNativesImpl(env);
}

void PersonalDataManagerAndroid::RecordAndLogProfileUse(
    JNIEnv* env,
    const JavaParamRef<jobject>& unused_obj,
    const JavaParamRef<jstring>& jguid) {
  AutofillProfile* profile = personal_data_manager_->GetProfileByGUID(
      ConvertJavaStringToUTF8(env, jguid));
  if (profile)
    personal_data_manager_->RecordUseOf(*profile);
}

void PersonalDataManagerAndroid::SetProfileUseStatsForTesting(
    JNIEnv* env,
    const JavaParamRef<jobject>& unused_obj,
    const JavaParamRef<jstring>& jguid,
    jint count,
    jint date) {
  DCHECK(count >= 0 && date >= 0);

  AutofillProfile* profile = personal_data_manager_->GetProfileByGUID(
      ConvertJavaStringToUTF8(env, jguid));
  profile->set_use_count(static_cast<size_t>(count));
  profile->set_use_date(base::Time::FromTimeT(date));

  personal_data_manager_->NotifyPersonalDataChangedForTest();
}

jint PersonalDataManagerAndroid::GetProfileUseCountForTesting(
    JNIEnv* env,
    const base::android::JavaParamRef<jobject>& unused_obj,
    const base::android::JavaParamRef<jstring>& jguid) {
  AutofillProfile* profile = personal_data_manager_->GetProfileByGUID(
      ConvertJavaStringToUTF8(env, jguid));
  return profile->use_count();
}

jlong PersonalDataManagerAndroid::GetProfileUseDateForTesting(
    JNIEnv* env,
    const base::android::JavaParamRef<jobject>& unused_obj,
    const base::android::JavaParamRef<jstring>& jguid) {
  AutofillProfile* profile = personal_data_manager_->GetProfileByGUID(
      ConvertJavaStringToUTF8(env, jguid));
  return profile->use_date().ToTimeT();
}

void PersonalDataManagerAndroid::RecordAndLogCreditCardUse(
    JNIEnv* env,
    const JavaParamRef<jobject>& unused_obj,
    const JavaParamRef<jstring>& jguid) {
  CreditCard* card = personal_data_manager_->GetCreditCardByGUID(
      ConvertJavaStringToUTF8(env, jguid));
  if (card)
    personal_data_manager_->RecordUseOf(*card);
}

void PersonalDataManagerAndroid::SetCreditCardUseStatsForTesting(
    JNIEnv* env,
    const JavaParamRef<jobject>& unused_obj,
    const JavaParamRef<jstring>& jguid,
    jint count,
    jint date) {
  DCHECK(count >= 0 && date >= 0);

  CreditCard* card = personal_data_manager_->GetCreditCardByGUID(
      ConvertJavaStringToUTF8(env, jguid));
  card->set_use_count(static_cast<size_t>(count));
  card->set_use_date(base::Time::FromTimeT(date));

  personal_data_manager_->NotifyPersonalDataChangedForTest();
}

jint PersonalDataManagerAndroid::GetCreditCardUseCountForTesting(
    JNIEnv* env,
    const base::android::JavaParamRef<jobject>& unused_obj,
    const base::android::JavaParamRef<jstring>& jguid) {
  CreditCard* card = personal_data_manager_->GetCreditCardByGUID(
      ConvertJavaStringToUTF8(env, jguid));
  return card->use_count();
}

jlong PersonalDataManagerAndroid::GetCreditCardUseDateForTesting(
    JNIEnv* env,
    const base::android::JavaParamRef<jobject>& unused_obj,
    const base::android::JavaParamRef<jstring>& jguid) {
  CreditCard* card = personal_data_manager_->GetCreditCardByGUID(
      ConvertJavaStringToUTF8(env, jguid));
  return card->use_date().ToTimeT();
}

// TODO(crbug.com/629507): Use a mock clock for testing.
jlong PersonalDataManagerAndroid::GetCurrentDateForTesting(
    JNIEnv* env,
    const base::android::JavaParamRef<jobject>& unused_obj) {
  return base::Time::Now().ToTimeT();
}

void PersonalDataManagerAndroid::LoadRulesForAddressNormalization(
    JNIEnv* env,
    const base::android::JavaParamRef<jobject>& unused_obj,
    const base::android::JavaParamRef<jstring>& jregion_code) {
  address_normalizer_.LoadRulesForRegion(
      ConvertJavaStringToUTF8(env, jregion_code));
}

void PersonalDataManagerAndroid::LoadRulesForSubKeys(
    JNIEnv* env,
    const base::android::JavaParamRef<jobject>& unused_obj,
    const base::android::JavaParamRef<jstring>& jregion_code) {
  address_validator_.LoadRules(ConvertJavaStringToUTF8(env, jregion_code));
}

void PersonalDataManagerAndroid::StartAddressNormalization(
    JNIEnv* env,
    const JavaParamRef<jobject>& unused_obj,
    const JavaParamRef<jobject>& jprofile,
    const JavaParamRef<jstring>& jregion_code,
    jint jtimeout_seconds,
    const JavaParamRef<jobject>& jdelegate) {
  const std::string region_code = ConvertJavaStringToUTF8(env, jregion_code);

  AutofillProfile profile;
  PopulateNativeProfileFromJava(jprofile, env, &profile);

  // Self-deleting object.
  AndroidAddressNormalizerDelegate* requester =
      new AndroidAddressNormalizerDelegate(env, jdelegate);

  // Start the normalization.
  address_normalizer_.StartAddressNormalization(profile, region_code,
                                                jtimeout_seconds, requester);
}

jboolean PersonalDataManagerAndroid::HasProfiles(
    JNIEnv* env,
    const base::android::JavaParamRef<jobject>& unused_obj) {
  return !personal_data_manager_->GetProfiles().empty();
}

jboolean PersonalDataManagerAndroid::HasCreditCards(
    JNIEnv* env,
    const base::android::JavaParamRef<jobject>& unused_obj) {
  return !personal_data_manager_->GetCreditCards().empty();
}

base::android::ScopedJavaLocalRef<jobjectArray>
PersonalDataManagerAndroid::GetSubKeys(JNIEnv* env,
                                       const std::string& region_code) {
  std::vector<std::string> sub_keys =
      address_validator_.GetRegionSubKeys(region_code);
  return base::android::ToJavaArrayOfStrings(env, sub_keys);
}

void PersonalDataManagerAndroid::OnAddressValidationRulesLoaded(
    const std::string& region_code,
    bool success) {
  // if |success| == false, AddressValidator::GetRegionSubKeys will return an
  // empty list of sub-keys. => No need to check for |success|.
  // Check if there is any sub-key request for that region code.
  if (!pending_subkey_region_code_.compare(region_code))
    pending_subkey_request_->OnRulesSuccessfullyLoaded();
  pending_subkey_region_code_.clear();
  pending_subkey_request_.reset();
}

void PersonalDataManagerAndroid::StartRegionSubKeysRequest(
    JNIEnv* env,
    const JavaParamRef<jobject>& unused_obj,
    const JavaParamRef<jstring>& jregion_code,
    const JavaParamRef<jobject>& jdelegate) {
  const std::string region_code = ConvertJavaStringToUTF8(env, jregion_code);
  std::unique_ptr<SubKeyRequestDelegate> requester =
      base::MakeUnique<AndroidSubKeyRequesterDelegate>(
          env, jdelegate, region_code, AsWeakPtr());

  if (AreRulesLoadedForRegion(region_code)) {
    requester->OnRulesSuccessfullyLoaded();
  } else {
    // Setup the variables so that the sub-keys request is sent, when the rules
    // are loaded.
    pending_subkey_region_code_ = region_code;
    pending_subkey_request_ = std::move(requester);
  }
}

void PersonalDataManagerAndroid::CancelPendingGetSubKeys(
    JNIEnv* env,
    const base::android::JavaParamRef<jobject>& unused_obj) {
  pending_subkey_region_code_.clear();
  pending_subkey_request_.reset();
}

ScopedJavaLocalRef<jobjectArray> PersonalDataManagerAndroid::GetProfileGUIDs(
    JNIEnv* env,
    const std::vector<AutofillProfile*>& profiles) {
  std::vector<base::string16> guids;
  for (AutofillProfile* profile : profiles)
    guids.push_back(base::UTF8ToUTF16(profile->guid()));

  return base::android::ToJavaArrayOfStrings(env, guids);
}

ScopedJavaLocalRef<jobjectArray> PersonalDataManagerAndroid::GetCreditCardGUIDs(
    JNIEnv* env,
    const std::vector<CreditCard*>& credit_cards) {
  std::vector<base::string16> guids;
  for (CreditCard* credit_card : credit_cards)
    guids.push_back(base::UTF8ToUTF16(credit_card->guid()));

  return base::android::ToJavaArrayOfStrings(env, guids);
}

bool PersonalDataManagerAndroid::AreRulesLoadedForRegion(
    const std::string& region_code) {
  return address_validator_.AreRulesLoadedForRegion(region_code);
}

ScopedJavaLocalRef<jobjectArray> PersonalDataManagerAndroid::GetProfileLabels(
    JNIEnv* env,
    bool address_only,
    bool include_name_in_label,
    bool include_organization_in_label,
    bool include_country_in_label,
    std::vector<AutofillProfile*> profiles) {
  std::unique_ptr<std::vector<ServerFieldType>> suggested_fields;
  size_t minimal_fields_shown = 2;
  if (address_only) {
    suggested_fields.reset(new std::vector<ServerFieldType>);
    if (include_name_in_label)
      suggested_fields->push_back(NAME_FULL);
    if (include_organization_in_label)
      suggested_fields->push_back(COMPANY_NAME);
    suggested_fields->push_back(ADDRESS_HOME_LINE1);
    suggested_fields->push_back(ADDRESS_HOME_LINE2);
    suggested_fields->push_back(ADDRESS_HOME_DEPENDENT_LOCALITY);
    suggested_fields->push_back(ADDRESS_HOME_CITY);
    suggested_fields->push_back(ADDRESS_HOME_STATE);
    suggested_fields->push_back(ADDRESS_HOME_ZIP);
    suggested_fields->push_back(ADDRESS_HOME_SORTING_CODE);
    if (include_country_in_label)
      suggested_fields->push_back(ADDRESS_HOME_COUNTRY);
    minimal_fields_shown = suggested_fields->size();
  }

  ServerFieldType excluded_field =
      include_name_in_label ? UNKNOWN_TYPE : NAME_FULL;

  std::vector<base::string16> labels;
  AutofillProfile::CreateInferredLabels(
      profiles, suggested_fields.get(), excluded_field, minimal_fields_shown,
      g_browser_process->GetApplicationLocale(), &labels);

  return base::android::ToJavaArrayOfStrings(env, labels);
}

base::android::ScopedJavaLocalRef<jstring>
PersonalDataManagerAndroid::GetShippingAddressLabelForPaymentRequest(
    JNIEnv* env,
    const base::android::JavaParamRef<jobject>& jprofile,
    bool include_country_in_label) {
  // The full name is not included in the label for shipping address. It is
  // added separately instead.
  std::vector<ServerFieldType> label_fields;
  label_fields.push_back(COMPANY_NAME);
  label_fields.push_back(ADDRESS_HOME_LINE1);
  label_fields.push_back(ADDRESS_HOME_LINE2);
  label_fields.push_back(ADDRESS_HOME_DEPENDENT_LOCALITY);
  label_fields.push_back(ADDRESS_HOME_CITY);
  label_fields.push_back(ADDRESS_HOME_STATE);
  label_fields.push_back(ADDRESS_HOME_ZIP);
  label_fields.push_back(ADDRESS_HOME_SORTING_CODE);
  if (include_country_in_label)
    label_fields.push_back(ADDRESS_HOME_COUNTRY);

  AutofillProfile profile;
  PopulateNativeProfileFromJava(jprofile, env, &profile);

  return ConvertUTF16ToJavaString(
      env, profile.ConstructInferredLabel(
               label_fields, label_fields.size(),
               g_browser_process->GetApplicationLocale()));
}

// Returns whether the Autofill feature is enabled.
static jboolean IsAutofillEnabled(JNIEnv* env,
                                  const JavaParamRef<jclass>& clazz) {
  return GetPrefs()->GetBoolean(autofill::prefs::kAutofillEnabled);
}

// Enables or disables the Autofill feature.
static void SetAutofillEnabled(JNIEnv* env,
                               const JavaParamRef<jclass>& clazz,
                               jboolean enable) {
  GetPrefs()->SetBoolean(autofill::prefs::kAutofillEnabled, enable);
}

// Returns whether the Autofill feature is managed.
static jboolean IsAutofillManaged(JNIEnv* env,
                                  const JavaParamRef<jclass>& clazz) {
  return GetPrefs()->IsManagedPreference(autofill::prefs::kAutofillEnabled);
}

// Returns whether the Payments integration feature is enabled.
static jboolean IsPaymentsIntegrationEnabled(
    JNIEnv* env,
    const JavaParamRef<jclass>& clazz) {
  return GetPrefs()->GetBoolean(autofill::prefs::kAutofillWalletImportEnabled);
}

// Enables or disables the Payments integration feature.
static void SetPaymentsIntegrationEnabled(JNIEnv* env,
                                          const JavaParamRef<jclass>& clazz,
                                          jboolean enable) {
  GetPrefs()->SetBoolean(autofill::prefs::kAutofillWalletImportEnabled, enable);
}

// Returns an ISO 3166-1-alpha-2 country code for a |jcountry_name| using
// the application locale, or an empty string.
static ScopedJavaLocalRef<jstring> ToCountryCode(
    JNIEnv* env,
    const JavaParamRef<jclass>& clazz,
    const JavaParamRef<jstring>& jcountry_name) {
  return ConvertUTF8ToJavaString(
      env, CountryNames::GetInstance()->GetCountryCode(
               base::android::ConvertJavaStringToUTF16(env, jcountry_name)));
}

static jlong Init(JNIEnv* env, const JavaParamRef<jobject>& obj) {
  PersonalDataManagerAndroid* personal_data_manager_android =
      new PersonalDataManagerAndroid(env, obj);
  return reinterpret_cast<intptr_t>(personal_data_manager_android);
}

}  // namespace autofill
