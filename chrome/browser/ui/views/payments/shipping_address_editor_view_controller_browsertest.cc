// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>

#include "base/memory/ptr_util.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/ui/views/payments/editor_view_controller.h"
#include "chrome/browser/ui/views/payments/payment_request_browsertest_base.h"
#include "chrome/browser/ui/views/payments/payment_request_dialog_view_ids.h"
#include "chrome/browser/ui/views/payments/validating_textfield.h"
#include "components/autofill/core/browser/autofill_country.h"
#include "components/autofill/core/browser/country_combobox_model.h"
#include "components/autofill/core/browser/personal_data_manager.h"
#include "components/autofill/core/browser/region_combobox_model.h"
#include "components/autofill/core/browser/test_region_data_loader.h"
#include "components/payments/content/payment_request_spec.h"
#include "content/public/test/browser_test_utils.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/libaddressinput/src/cpp/include/libaddressinput/null_storage.h"
#include "third_party/libaddressinput/src/cpp/include/libaddressinput/source.h"
#include "ui/views/controls/combobox/combobox.h"

namespace payments {

namespace {

const char kLocale[] = "en_US";
const char kNameFull[] = "Bob Jones";
const char kHomeAddress[] = "42 Answers-All Avenue";
const char kHomeCity[] = "Question-City";
const char kHomeZip[] = "ziiiiiip";
const char kHomePhone[] = "5755555555";  // 5555555555 is invalid :-(.
const char kFormattedHomePhone[] = "(575) 555-5555";
const char kAnyState[] = "any state";
const char kCountryWithoutStates[] = "Albania";
const char kCountryWithoutStatesPhoneNumber[] = "42223446";

}  // namespace

class PaymentRequestShippingAddressEditorTest
    : public PaymentRequestBrowserTestBase {
 protected:
  PaymentRequestShippingAddressEditorTest()
      : PaymentRequestBrowserTestBase(
            "/payment_request_dynamic_shipping_test.html") {}

  void SetFieldTestValue(autofill::ServerFieldType type) {
    base::string16 textfield_text;
    switch (type) {
      case (autofill::NAME_FULL): {
        textfield_text = base::ASCIIToUTF16(kNameFull);
        break;
      }
      case (autofill::ADDRESS_HOME_STREET_ADDRESS): {
        textfield_text = base::ASCIIToUTF16(kHomeAddress);
        break;
      }
      case (autofill::ADDRESS_HOME_CITY): {
        textfield_text = base::ASCIIToUTF16(kHomeCity);
        break;
      }
      case (autofill::ADDRESS_HOME_STATE): {
        textfield_text = base::ASCIIToUTF16(kAnyState);
        break;
      }
      case (autofill::ADDRESS_HOME_ZIP): {
        textfield_text = base::ASCIIToUTF16(kHomeZip);
        break;
      }
      case (autofill::PHONE_HOME_WHOLE_NUMBER): {
        textfield_text = base::ASCIIToUTF16(kHomePhone);
        break;
      }
      default:
        ADD_FAILURE() << "Unexpected type: " << type;
    }
    SetEditorTextfieldValue(textfield_text, type);
  }

  void SetCommonFields() {
    SetFieldTestValue(autofill::NAME_FULL);
    SetFieldTestValue(autofill::ADDRESS_HOME_STREET_ADDRESS);
    SetFieldTestValue(autofill::ADDRESS_HOME_CITY);
    SetFieldTestValue(autofill::ADDRESS_HOME_ZIP);
    SetFieldTestValue(autofill::PHONE_HOME_WHOLE_NUMBER);
  }

  // First check if the requested field of |type| exists, if so, set its value
  // in |textfield_text| if it's not null, and return true.
  bool GetEditorTextfieldValueIfExists(autofill::ServerFieldType type,
                                       base::string16* textfield_text) {
    ValidatingTextfield* textfield =
        static_cast<ValidatingTextfield*>(dialog_view()->GetViewByID(
            EditorViewController::GetInputFieldViewId(type)));
    if (!textfield)
      return false;
    if (textfield_text)
      *textfield_text = textfield->text();
    return true;
  }

  // |unset_types| can be null, but when it's not, the fields that are not set
  // get their type added to it, so that the caller can tell which types are not
  // set for a given country. |accept_empty_phone_number| can be set to true to
  // accept a phone type field set to empty, and mark it as unset.
  void ExpectExistingRequiredFields(
      std::set<autofill::ServerFieldType>* unset_types,
      bool accept_empty_phone_number) {
    base::string16 textfield_text;
    if (GetEditorTextfieldValueIfExists(autofill::NAME_FULL, &textfield_text)) {
      EXPECT_EQ(base::ASCIIToUTF16(kNameFull), textfield_text);
    } else if (unset_types) {
      unset_types->insert(autofill::NAME_FULL);
    }

    if (GetEditorTextfieldValueIfExists(autofill::ADDRESS_HOME_STREET_ADDRESS,
                                        &textfield_text)) {
      EXPECT_EQ(base::ASCIIToUTF16(kHomeAddress), textfield_text);
    } else if (unset_types) {
      unset_types->insert(autofill::ADDRESS_HOME_STREET_ADDRESS);
    }

    if (GetEditorTextfieldValueIfExists(autofill::ADDRESS_HOME_CITY,
                                        &textfield_text)) {
      EXPECT_EQ(base::ASCIIToUTF16(kHomeCity), textfield_text);
    } else if (unset_types) {
      unset_types->insert(autofill::ADDRESS_HOME_CITY);
    }

    if (GetEditorTextfieldValueIfExists(autofill::ADDRESS_HOME_ZIP,
                                        &textfield_text)) {
      EXPECT_EQ(base::ASCIIToUTF16(kHomeZip), textfield_text);
    } else if (unset_types) {
      unset_types->insert(autofill::ADDRESS_HOME_ZIP);
    }

    if (GetEditorTextfieldValueIfExists(autofill::PHONE_HOME_WHOLE_NUMBER,
                                        &textfield_text)) {
      // The phone can be empty when restored from a saved state, or it may be
      // formatted based on the currently selected country.
      if (!accept_empty_phone_number) {
        EXPECT_EQ(base::ASCIIToUTF16(kHomePhone), textfield_text);
      } else if (textfield_text.empty()) {
        if (unset_types)
          unset_types->insert(autofill::PHONE_HOME_WHOLE_NUMBER);
      } else if (textfield_text != base::ASCIIToUTF16(kHomePhone)) {
        EXPECT_EQ(base::ASCIIToUTF16(kFormattedHomePhone), textfield_text);
      }
    } else if (unset_types) {
      unset_types->insert(autofill::PHONE_HOME_WHOLE_NUMBER);
    }
  }

  std::string GetSelectedCountryCode() {
    views::Combobox* country_combobox = static_cast<views::Combobox*>(
        dialog_view()->GetViewByID(EditorViewController::GetInputFieldViewId(
            autofill::ADDRESS_HOME_COUNTRY)));
    DCHECK(country_combobox);
    int selected_country_row = country_combobox->GetSelectedRow();
    autofill::CountryComboboxModel* country_model =
        static_cast<autofill::CountryComboboxModel*>(country_combobox->model());

    return country_model->countries()[selected_country_row]->country_code();
  }

  PersonalDataLoadedObserverMock personal_data_observer_;
  autofill::TestRegionDataLoader test_region_data_loader_;

 private:
  DISALLOW_COPY_AND_ASSIGN(PaymentRequestShippingAddressEditorTest);
};

IN_PROC_BROWSER_TEST_F(PaymentRequestShippingAddressEditorTest, SyncData) {
  InvokePaymentRequestUI();
  SetRegionDataLoader(&test_region_data_loader_);

  // No shipping profiles are available.
  PaymentRequest* request = GetPaymentRequests(GetActiveWebContents()).front();
  EXPECT_EQ(0U, request->state()->shipping_profiles().size());
  EXPECT_EQ(nullptr, request->state()->selected_shipping_profile());

  test_region_data_loader_.set_synchronous_callback(true);
  OpenShippingAddressEditorScreen();

  std::string country_code(GetSelectedCountryCode());

  SetCommonFields();
  // We also need to set the state when no region data is provided.
  SetFieldTestValue(autofill::ADDRESS_HOME_STATE);

  ResetEventObserver(DialogEvent::BACK_TO_PAYMENT_SHEET_NAVIGATION);

  // Verifying the data is in the DB.
  autofill::PersonalDataManager* personal_data_manager = GetDataManager();
  personal_data_manager->AddObserver(&personal_data_observer_);

  // Wait until the web database has been updated and the notification sent.
  base::RunLoop data_loop;
  EXPECT_CALL(personal_data_observer_, OnPersonalDataChanged())
      .WillOnce(QuitMessageLoop(&data_loop));
  ClickOnDialogViewAndWait(DialogViewID::SAVE_ADDRESS_BUTTON);
  data_loop.Run();

  ASSERT_EQ(1UL, personal_data_manager->GetProfiles().size());
  autofill::AutofillProfile* profile = personal_data_manager->GetProfiles()[0];
  DCHECK(profile);
  EXPECT_EQ(base::ASCIIToUTF16(country_code),
            profile->GetRawInfo(autofill::ADDRESS_HOME_COUNTRY));
  EXPECT_EQ(base::ASCIIToUTF16(kAnyState),
            profile->GetRawInfo(autofill::ADDRESS_HOME_STATE));
  ExpectExistingRequiredFields(/*unset_types=*/nullptr,
                               /*accept_empty_phone_number=*/false);
}

IN_PROC_BROWSER_TEST_F(PaymentRequestShippingAddressEditorTest,
                       EnterAcceleratorSyncData) {
  InvokePaymentRequestUI();
  SetRegionDataLoader(&test_region_data_loader_);

  // No shipping profiles are available.
  PaymentRequest* request = GetPaymentRequests(GetActiveWebContents()).front();
  EXPECT_EQ(0U, request->state()->shipping_profiles().size());
  EXPECT_EQ(nullptr, request->state()->selected_shipping_profile());

  test_region_data_loader_.set_synchronous_callback(true);
  OpenShippingAddressEditorScreen();

  std::string country_code(GetSelectedCountryCode());

  SetCommonFields();
  // We also need to set the state when no region data is provided.
  SetFieldTestValue(autofill::ADDRESS_HOME_STATE);

  ResetEventObserver(DialogEvent::BACK_TO_PAYMENT_SHEET_NAVIGATION);

  // Verifying the data is in the DB.
  autofill::PersonalDataManager* personal_data_manager = GetDataManager();
  personal_data_manager->AddObserver(&personal_data_observer_);

  // Wait until the web database has been updated and the notification sent.
  base::RunLoop data_loop;
  EXPECT_CALL(personal_data_observer_, OnPersonalDataChanged())
      .WillOnce(QuitMessageLoop(&data_loop));
  views::View* editor_sheet = dialog_view()->GetViewByID(
      static_cast<int>(DialogViewID::SHIPPING_ADDRESS_EDITOR_SHEET));
  editor_sheet->AcceleratorPressed(
      ui::Accelerator(ui::VKEY_RETURN, ui::EF_NONE));
  data_loop.Run();

  ASSERT_EQ(1UL, personal_data_manager->GetProfiles().size());
  autofill::AutofillProfile* profile = personal_data_manager->GetProfiles()[0];
  DCHECK(profile);
  EXPECT_EQ(base::ASCIIToUTF16(country_code),
            profile->GetRawInfo(autofill::ADDRESS_HOME_COUNTRY));
  EXPECT_EQ(base::ASCIIToUTF16(kAnyState),
            profile->GetRawInfo(autofill::ADDRESS_HOME_STATE));
  ExpectExistingRequiredFields(/*unset_types=*/nullptr,
                               /*accept_empty_phone_number=*/false);
}

IN_PROC_BROWSER_TEST_F(PaymentRequestShippingAddressEditorTest, AsyncData) {
  InvokePaymentRequestUI();
  SetRegionDataLoader(&test_region_data_loader_);

  test_region_data_loader_.set_synchronous_callback(false);
  OpenShippingAddressEditorScreen();
  // Complete the async fetch of region data.
  std::vector<std::pair<std::string, std::string>> regions;
  regions.push_back(std::make_pair("code", kAnyState));
  test_region_data_loader_.SendAsynchronousData(regions);

  std::string country_code(GetSelectedCountryCode());

  SetCommonFields();

  ResetEventObserver(DialogEvent::BACK_TO_PAYMENT_SHEET_NAVIGATION);

  // Verifying the data is in the DB.
  autofill::PersonalDataManager* personal_data_manager = GetDataManager();
  personal_data_manager->AddObserver(&personal_data_observer_);

  // Wait until the web database has been updated and the notification sent.
  base::RunLoop data_loop;
  EXPECT_CALL(personal_data_observer_, OnPersonalDataChanged())
      .WillOnce(QuitMessageLoop(&data_loop));
  ClickOnDialogViewAndWait(DialogViewID::SAVE_ADDRESS_BUTTON);
  data_loop.Run();

  ASSERT_EQ(1UL, personal_data_manager->GetProfiles().size());
  autofill::AutofillProfile* profile = personal_data_manager->GetProfiles()[0];
  DCHECK(profile);
  EXPECT_EQ(base::ASCIIToUTF16(country_code),
            profile->GetRawInfo(autofill::ADDRESS_HOME_COUNTRY));
  EXPECT_EQ(base::ASCIIToUTF16(kAnyState),
            profile->GetRawInfo(autofill::ADDRESS_HOME_STATE));
  ExpectExistingRequiredFields(/*unset_types=*/nullptr,
                               /*accept_empty_phone_number=*/false);

  // One shipping profile is available and selected.
  PaymentRequest* request = GetPaymentRequests(GetActiveWebContents()).front();
  EXPECT_EQ(1UL, request->state()->shipping_profiles().size());
  EXPECT_EQ(request->state()->shipping_profiles().back(),
            request->state()->selected_shipping_profile());
}

IN_PROC_BROWSER_TEST_F(PaymentRequestShippingAddressEditorTest,
                       SwitchingCountryUpdatesViewAndKeepsValues) {
  InvokePaymentRequestUI();
  SetRegionDataLoader(&test_region_data_loader_);

  test_region_data_loader_.set_synchronous_callback(false);
  OpenShippingAddressEditorScreen();
  std::vector<std::pair<std::string, std::string>> regions1;
  regions1.push_back(std::make_pair("1a", "region1a"));
  test_region_data_loader_.SendAsynchronousData(regions1);

  SetCommonFields();

  views::Combobox* country_combobox = static_cast<views::Combobox*>(
      dialog_view()->GetViewByID(EditorViewController::GetInputFieldViewId(
          autofill::ADDRESS_HOME_COUNTRY)));
  ASSERT_NE(nullptr, country_combobox);
  ASSERT_EQ(0, country_combobox->GetSelectedRow());
  autofill::CountryComboboxModel* country_model =
      static_cast<autofill::CountryComboboxModel*>(country_combobox->model());
  size_t num_countries = country_model->countries().size();
  ASSERT_GT(num_countries, 10UL);

  bool use_regions1 = true;
  std::vector<std::pair<std::string, std::string>> regions2;
  regions2.push_back(std::make_pair("2a", "region2a"));
  regions2.push_back(std::make_pair("2b", "region2b"));
  std::set<autofill::ServerFieldType> unset_types;
  for (size_t country_index = 10; country_index < num_countries;
       country_index += num_countries / 10) {
    // The editor updates asynchronously when the country changes.
    ResetEventObserver(DialogEvent::EDITOR_VIEW_UPDATED);

    views::Combobox* region_combobox = static_cast<views::Combobox*>(
        dialog_view()->GetViewByID(EditorViewController::GetInputFieldViewId(
            autofill::ADDRESS_HOME_STATE)));
    autofill::RegionComboboxModel* region_model = nullptr;
    // Some countries don't have a state combobox.
    if (region_combobox) {
      autofill::RegionComboboxModel* region_model =
          static_cast<autofill::RegionComboboxModel*>(region_combobox->model());
      if (use_regions1) {
        ASSERT_EQ(1, region_model->GetItemCount());
        EXPECT_EQ(base::ASCIIToUTF16("region1a"), region_model->GetItemAt(0));
      } else {
        ASSERT_EQ(2, region_model->GetItemCount());
        EXPECT_EQ(base::ASCIIToUTF16("region2a"), region_model->GetItemAt(0));
        EXPECT_EQ(base::ASCIIToUTF16("region2b"), region_model->GetItemAt(1));
      }
      use_regions1 = !use_regions1;
    }

    country_combobox->SetSelectedRow(country_index);
    country_combobox->OnBlur();

    // The view update will invalidate the country_combobox / model pointers.
    country_combobox = nullptr;
    country_model = nullptr;
    region_combobox = nullptr;
    region_model = nullptr;
    WaitForObservedEvent();

    // Some types could have been lost in previous countries and may now
    // available in this country.
    std::set<autofill::ServerFieldType> set_types;
    for (auto type : unset_types) {
      ValidatingTextfield* textfield =
          static_cast<ValidatingTextfield*>(dialog_view()->GetViewByID(
              EditorViewController::GetInputFieldViewId(type)));
      if (textfield) {
        EXPECT_TRUE(textfield->text().empty()) << type;
        SetFieldTestValue(type);
        set_types.insert(type);
      }
    }
    for (auto type : set_types) {
      unset_types.erase(type);
    }

    // Make sure the country combobox was properly reset to the chosen country.
    country_combobox = static_cast<views::Combobox*>(
        dialog_view()->GetViewByID(EditorViewController::GetInputFieldViewId(
            autofill::ADDRESS_HOME_COUNTRY)));
    DCHECK(country_combobox);
    EXPECT_EQ(country_index,
              static_cast<size_t>(country_combobox->GetSelectedRow()));
    country_model =
        static_cast<autofill::CountryComboboxModel*>(country_combobox->model());
    ASSERT_EQ(num_countries, country_model->countries().size());

    // Update regions.
    test_region_data_loader_.SendAsynchronousData(use_regions1 ? regions1
                                                               : regions2);
    // Make sure the fields common between previous and new country have been
    // properly restored. Note that some country don't support the test phone
    // number so the phone field will be present, but the value will not have
    // been restored from the profile.
    ExpectExistingRequiredFields(&unset_types,
                                 /*accept_empty_phone_number=*/true);
  }
}

IN_PROC_BROWSER_TEST_F(PaymentRequestShippingAddressEditorTest,
                       FailToLoadRegionData) {
  InvokePaymentRequestUI();
  SetRegionDataLoader(&test_region_data_loader_);

  // The synchronous callback is made with no data, which causes a failure.
  test_region_data_loader_.set_synchronous_callback(true);
  OpenShippingAddressEditorScreen();
  // Even though the editor updates asynchronously when the regions fail to
  // load, the update is always completed before the runloop.quit() takes effect
  // when the OpenShippingAddressEditorScreen completes. So now any textual
  // value can be set as the state.
  SetFieldTestValue(autofill::ADDRESS_HOME_STATE);
  SetCommonFields();
  ResetEventObserver(DialogEvent::BACK_TO_PAYMENT_SHEET_NAVIGATION);

  // Verifying the data is in the DB.
  autofill::PersonalDataManager* personal_data_manager = GetDataManager();
  personal_data_manager->AddObserver(&personal_data_observer_);

  // Wait until the web database has been updated and the notification sent.
  base::RunLoop data_loop;
  EXPECT_CALL(personal_data_observer_, OnPersonalDataChanged())
      .WillOnce(QuitMessageLoop(&data_loop));
  ClickOnDialogViewAndWait(DialogViewID::SAVE_ADDRESS_BUTTON);
  data_loop.Run();

  ASSERT_EQ(1UL, personal_data_manager->GetProfiles().size());
  autofill::AutofillProfile* profile = personal_data_manager->GetProfiles()[0];
  DCHECK(profile);

  EXPECT_EQ(base::ASCIIToUTF16(kAnyState),
            profile->GetRawInfo(autofill::ADDRESS_HOME_STATE));
  ExpectExistingRequiredFields(/*unset_types=*/nullptr,
                               /*accept_empty_phone_number=*/false);
}

IN_PROC_BROWSER_TEST_F(PaymentRequestShippingAddressEditorTest,
                       TimingOutRegionData) {
  InvokePaymentRequestUI();
  SetRegionDataLoader(&test_region_data_loader_);

  test_region_data_loader_.set_synchronous_callback(false);
  OpenShippingAddressEditorScreen();

  // The editor updates asynchronously when the regions data load times out.
  ResetEventObserver(DialogEvent::EDITOR_VIEW_UPDATED);
  test_region_data_loader_.SendAsynchronousData(
      std::vector<std::pair<std::string, std::string>>());
  WaitForObservedEvent();

  // Now any textual value can be set for the ADDRESS_HOME_STATE.
  SetFieldTestValue(autofill::ADDRESS_HOME_STATE);
  SetCommonFields();
  ResetEventObserver(DialogEvent::BACK_TO_PAYMENT_SHEET_NAVIGATION);

  // Verifying the data is in the DB.
  autofill::PersonalDataManager* personal_data_manager = GetDataManager();
  personal_data_manager->AddObserver(&personal_data_observer_);

  // Wait until the web database has been updated and the notification sent.
  base::RunLoop data_loop;
  EXPECT_CALL(personal_data_observer_, OnPersonalDataChanged())
      .WillOnce(QuitMessageLoop(&data_loop));
  ClickOnDialogViewAndWait(DialogViewID::SAVE_ADDRESS_BUTTON);
  data_loop.Run();

  ASSERT_EQ(1UL, personal_data_manager->GetProfiles().size());
  autofill::AutofillProfile* profile = personal_data_manager->GetProfiles()[0];
  DCHECK(profile);

  EXPECT_EQ(base::ASCIIToUTF16(kAnyState),
            profile->GetRawInfo(autofill::ADDRESS_HOME_STATE));
  ExpectExistingRequiredFields(/*unset_types=*/nullptr,
                               /*accept_empty_phone_number=*/false);
}

IN_PROC_BROWSER_TEST_F(PaymentRequestShippingAddressEditorTest,
                       SelectingIncompleteAddress) {
  // Add incomplete address.
  autofill::AutofillProfile profile;
  profile.SetInfo(autofill::AutofillType(autofill::NAME_FULL),
                  base::ASCIIToUTF16(kNameFull), kLocale);
  // Also set non-default country, to make sure proper fields will be used.
  profile.SetInfo(autofill::AutofillType(autofill::ADDRESS_HOME_COUNTRY),
                  base::ASCIIToUTF16(kCountryWithoutStates), kLocale);
  AddAutofillProfile(profile);

  InvokePaymentRequestUI();
  SetRegionDataLoader(&test_region_data_loader_);

  // One shipping address is available, but it's not selected.
  PaymentRequest* request = GetPaymentRequests(GetActiveWebContents()).front();
  EXPECT_EQ(1U, request->state()->shipping_profiles().size());
  EXPECT_EQ(nullptr, request->state()->selected_shipping_profile());

  test_region_data_loader_.set_synchronous_callback(true);
  OpenShippingAddressSectionScreen();

  ResetEventObserver(DialogEvent::SHIPPING_ADDRESS_EDITOR_OPENED);
  ClickOnChildInListViewAndWait(/*child_index=*/0, /*num_children=*/1,
                                DialogViewID::SHIPPING_ADDRESS_SHEET_LIST_VIEW);

  EXPECT_EQ(base::ASCIIToUTF16(kNameFull),
            GetEditorTextfieldValue(autofill::NAME_FULL));
  // There are no state field in |kCountryWithoutStates|.
  EXPECT_FALSE(
      GetEditorTextfieldValueIfExists(autofill::ADDRESS_HOME_STATE, nullptr));

  // Set all required fields.
  SetCommonFields();
  // The phone number must be replaced by one that is valid for
  // |kCountryWithoutStates|.
  SetEditorTextfieldValue(base::ASCIIToUTF16(kCountryWithoutStatesPhoneNumber),
                          autofill::PHONE_HOME_WHOLE_NUMBER);

  // Verifying the data is in the DB.
  autofill::PersonalDataManager* personal_data_manager = GetDataManager();
  personal_data_manager->AddObserver(&personal_data_observer_);

  ResetEventObserver(DialogEvent::BACK_TO_PAYMENT_SHEET_NAVIGATION);

  // Wait until the web database has been updated and the notification sent.
  base::RunLoop data_loop;
  EXPECT_CALL(personal_data_observer_, OnPersonalDataChanged())
      .WillOnce(QuitMessageLoop(&data_loop));
  ClickOnDialogViewAndWait(DialogViewID::SAVE_ADDRESS_BUTTON);
  data_loop.Run();

  ASSERT_EQ(1UL, personal_data_manager->GetProfiles().size());
  autofill::AutofillProfile* saved_profile =
      personal_data_manager->GetProfiles()[0];
  DCHECK(saved_profile);
  EXPECT_EQ(
      base::ASCIIToUTF16(kCountryWithoutStates),
      saved_profile->GetInfo(
          autofill::AutofillType(autofill::ADDRESS_HOME_COUNTRY), kLocale));
  ExpectExistingRequiredFields(/*unset_types=*/nullptr,
                               /*accept_empty_phone_number=*/false);

  // Still have one shipping address, but now it's selected.
  EXPECT_EQ(1U, request->state()->shipping_profiles().size());
  EXPECT_EQ(request->state()->shipping_profiles().back(),
            request->state()->selected_shipping_profile());
}

IN_PROC_BROWSER_TEST_F(PaymentRequestShippingAddressEditorTest,
                       FocusFirstInvalidField_Name) {
  InvokePaymentRequestUI();
  SetRegionDataLoader(&test_region_data_loader_);

  test_region_data_loader_.set_synchronous_callback(true);
  OpenShippingAddressEditorScreen();

  // We know that the name field is always the first one in a shipping address.
  views::Textfield* textfield =
      static_cast<views::Textfield*>(dialog_view()->GetViewByID(
          EditorViewController::GetInputFieldViewId(autofill::NAME_FULL)));
  DCHECK(textfield);
  EXPECT_TRUE(textfield->text().empty());
  EXPECT_TRUE(textfield->invalid());
  EXPECT_TRUE(textfield->HasFocus());
}

IN_PROC_BROWSER_TEST_F(PaymentRequestShippingAddressEditorTest,
                       FocusFirstInvalidField_NotName) {
  // Add address with the name set, so that another view takes focus.
  autofill::AutofillProfile profile;
  profile.SetInfo(autofill::AutofillType(autofill::NAME_FULL),
                  base::ASCIIToUTF16(kNameFull), "fr_CA");
  AddAutofillProfile(profile);

  InvokePaymentRequestUI();
  SetRegionDataLoader(&test_region_data_loader_);
  test_region_data_loader_.set_synchronous_callback(true);
  OpenShippingAddressSectionScreen();
  ResetEventObserver(DialogEvent::SHIPPING_ADDRESS_EDITOR_OPENED);
  ClickOnChildInListViewAndWait(/*child_index=*/0, /*num_children=*/1,
                                DialogViewID::SHIPPING_ADDRESS_SHEET_LIST_VIEW);

  views::Textfield* textfield =
      static_cast<views::Textfield*>(dialog_view()->GetViewByID(
          EditorViewController::GetInputFieldViewId(autofill::NAME_FULL)));
  DCHECK(textfield);
  EXPECT_FALSE(textfield->text().empty());
  EXPECT_FALSE(textfield->invalid());
  EXPECT_FALSE(textfield->HasFocus());

  // Since we can't easily tell which field is after name, let's just make sure
  // that a view has focus. Unfortunately, we can't cast it to a specific type
  // that we could query for validity (it could be either text or combobox).
  EXPECT_NE(textfield->GetFocusManager()->GetFocusedView(), nullptr);
}

}  // namespace payments
