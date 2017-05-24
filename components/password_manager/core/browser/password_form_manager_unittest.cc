// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/password_manager/core/browser/password_form_manager.h"

#include <map>
#include <memory>
#include <set>
#include <utility>

#include "base/feature_list.h"
#include "base/macros.h"
#include "base/memory/ptr_util.h"
#include "base/message_loop/message_loop.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/histogram_tester.h"
#include "base/test/scoped_feature_list.h"
#include "base/test/user_action_tester.h"
#include "components/autofill/core/browser/autofill_manager.h"
#include "components/autofill/core/browser/proto/server.pb.h"
#include "components/autofill/core/browser/test_autofill_client.h"
#include "components/autofill/core/browser/test_autofill_driver.h"
#include "components/autofill/core/browser/test_personal_data_manager.h"
#include "components/autofill/core/common/autofill_pref_names.h"
#include "components/autofill/core/common/password_form.h"
#include "components/password_manager/core/browser/fake_form_fetcher.h"
#include "components/password_manager/core/browser/password_manager.h"
#include "components/password_manager/core/browser/password_manager_driver.h"
#include "components/password_manager/core/browser/password_manager_metrics_util.h"
#include "components/password_manager/core/browser/password_manager_test_utils.h"
#include "components/password_manager/core/browser/statistics_table.h"
#include "components/password_manager/core/browser/stub_credentials_filter.h"
#include "components/password_manager/core/browser/stub_form_saver.h"
#include "components/password_manager/core/browser/stub_password_manager_client.h"
#include "components/password_manager/core/browser/stub_password_manager_driver.h"
#include "components/password_manager/core/common/password_manager_features.h"
#include "components/password_manager/core/common/password_manager_pref_names.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/testing_pref_service.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"
#include "url/origin.h"

using autofill::FieldPropertiesFlags;
using autofill::FieldPropertiesMask;
using autofill::PasswordForm;
using autofill::PossibleUsernamePair;
using base::ASCIIToUTF16;
using ::testing::_;
using ::testing::IsEmpty;
using ::testing::Mock;
using ::testing::NiceMock;
using ::testing::Pointee;
using ::testing::Return;
using ::testing::SaveArg;
using ::testing::SaveArgPointee;
using ::testing::UnorderedElementsAre;
using ::testing::WithArg;

namespace password_manager {

namespace {

// Enum that describes what button the user pressed on the save prompt.
enum SavePromptInteraction { SAVE, NEVER, NO_INTERACTION };

class MockFormSaver : public StubFormSaver {
 public:
  MockFormSaver() = default;

  ~MockFormSaver() override = default;

  // FormSaver:
  MOCK_METHOD1(PermanentlyBlacklist, void(autofill::PasswordForm* observed));
  MOCK_METHOD3(
      Save,
      void(const autofill::PasswordForm& pending,
           const std::map<base::string16, const PasswordForm*>& best_matches,
           const autofill::PasswordForm* old_primary_key));
  MOCK_METHOD4(
      Update,
      void(const autofill::PasswordForm& pending,
           const std::map<base::string16, const PasswordForm*>& best_matches,
           const std::vector<autofill::PasswordForm>* credentials_to_update,
           const autofill::PasswordForm* old_primary_key));
  MOCK_METHOD3(WipeOutdatedCopies,
               void(const autofill::PasswordForm& pending,
                    std::map<base::string16, const PasswordForm*>* best_matches,
                    const autofill::PasswordForm** preferred_match));

  // Convenience downcasting method.
  static MockFormSaver& Get(PasswordFormManager* form_manager) {
    return *static_cast<MockFormSaver*>(form_manager->form_saver());
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(MockFormSaver);
};

class MockFormFetcher : public FakeFormFetcher {
 public:
  MOCK_METHOD1(AddConsumer, void(Consumer*));
  MOCK_METHOD1(RemoveConsumer, void(Consumer*));
};

MATCHER_P(CheckUsername, username_value, "Username incorrect") {
  return arg.username_value == username_value;
}

MATCHER_P(CheckUsernamePtr, username_value, "Username incorrect") {
  return arg && arg->username_value == username_value;
}

MATCHER_P3(CheckUploadedAutofillTypesAndSignature,
           form_signature,
           expected_types,
           expect_generation_vote,
           "Unexpected autofill types or form signature") {
  if (form_signature != arg.FormSignatureAsStr()) {
    // Unexpected form's signature.
    ADD_FAILURE() << "Expected form signature is " << form_signature
                  << ", but found " << arg.FormSignatureAsStr();
    return false;
  }
  bool found_generation_vote = false;
  for (const auto& field : arg) {
    if (field->possible_types().size() > 1) {
      ADD_FAILURE() << field->name << " field has several possible types";
      return false;
    }

    found_generation_vote |=
        field->generation_type() !=
        autofill::AutofillUploadContents::Field::NO_GENERATION;

    autofill::ServerFieldType expected_vote =
        expected_types.find(field->name) == expected_types.end()
            ? autofill::NO_SERVER_DATA
            : expected_types.find(field->name)->second;
    autofill::ServerFieldType actual_vote =
        field->possible_types().empty() ? autofill::NO_SERVER_DATA
                                        : *field->possible_types().begin();
    if (expected_vote != actual_vote) {
      ADD_FAILURE() << field->name << " field: expected vote " << expected_vote
                    << ", but found " << actual_vote;
      return false;
    }
  }
  EXPECT_EQ(expect_generation_vote, found_generation_vote);
  return true;
}

MATCHER_P2(CheckUploadedGenerationTypesAndSignature,
           form_signature,
           expected_generation_types,
           "Unexpected generation types or form signature") {
  if (form_signature != arg.FormSignatureAsStr()) {
    // Unexpected form's signature.
    ADD_FAILURE() << "Expected form signature is " << form_signature
                  << ", but found " << arg.FormSignatureAsStr();
    return false;
  }
  for (const auto& field : arg) {
    if (expected_generation_types.find(field->name) ==
        expected_generation_types.end()) {
      if (field->generation_type() !=
          autofill::AutofillUploadContents::Field::NO_GENERATION) {
        // Unexpected generation type.
        ADD_FAILURE() << "Expected no generation type for the field "
                      << field->name << ", but found "
                      << field->generation_type();
        return false;
      }
    } else {
      if (expected_generation_types.find(field->name)->second !=
          field->generation_type()) {
        // Wrong generation type.
        ADD_FAILURE() << "Expected generation type for the field "
                      << field->name << " is "
                      << expected_generation_types.find(field->name)->second
                      << ", but found " << field->generation_type();
        return false;
      }
    }
  }
  return true;
}

MATCHER_P2(CheckUploadedFormClassifierVote,
           found_generation_element,
           generation_element,
           "Wrong form classifier votes") {
  for (const auto& field : arg) {
    if (found_generation_element && field->name == generation_element) {
      EXPECT_EQ(field->form_classifier_outcome(),
                autofill::AutofillUploadContents::Field::GENERATION_ELEMENT);
    } else {
      EXPECT_EQ(
          field->form_classifier_outcome(),
          autofill::AutofillUploadContents::Field::NON_GENERATION_ELEMENT);
    }
  }
  return true;
}

MATCHER_P(CheckFieldPropertiesMasksUpload,
          expected_field_properties,
          "Wrong field properties flags") {
  for (const auto& field : arg) {
    autofill::FieldPropertiesMask expected_mask =
        expected_field_properties.find(field->name) !=
                expected_field_properties.end()
            ? FieldPropertiesFlags::USER_TYPED
            : 0;
    if (field->properties_mask != expected_mask) {
      ADD_FAILURE() << "Wrong field properties flags for field " << field->name
                    << ": expected mask " << expected_mask << ", but found "
                    << field->properties_mask;
      return false;
    }
  }
  return true;
}

class MockAutofillDownloadManager : public autofill::AutofillDownloadManager {
 public:
  MockAutofillDownloadManager(
      autofill::AutofillDriver* driver,
      autofill::AutofillDownloadManager::Observer* observer)
      : AutofillDownloadManager(driver, observer) {}

  MOCK_METHOD5(StartUploadRequest,
               bool(const autofill::FormStructure&,
                    bool,
                    const autofill::ServerFieldTypeSet&,
                    const std::string&,
                    bool));

 private:
  DISALLOW_COPY_AND_ASSIGN(MockAutofillDownloadManager);
};

class MockAutofillManager : public autofill::AutofillManager {
 public:
  MockAutofillManager(autofill::AutofillDriver* driver,
                      autofill::AutofillClient* client,
                      autofill::PersonalDataManager* data_manager)
      : AutofillManager(driver, client, data_manager) {}

  void SetDownloadManager(autofill::AutofillDownloadManager* manager) {
    set_download_manager(manager);
  }

  // Workaround for std::unique_ptr<> lacking a copy constructor.
  void StartUploadProcess(std::unique_ptr<FormStructure> form_structure,
                          const base::TimeTicks& timestamp,
                          bool observed_submission) {
    StartUploadProcessPtr(form_structure.release(), timestamp,
                          observed_submission);
  }

  MOCK_METHOD3(StartUploadProcessPtr,
               void(FormStructure*, const base::TimeTicks&, bool));

 private:
  DISALLOW_COPY_AND_ASSIGN(MockAutofillManager);
};

class MockPasswordManagerDriver : public StubPasswordManagerDriver {
 public:
  MockPasswordManagerDriver()
      : mock_autofill_manager_(&test_autofill_driver_,
                               &test_autofill_client_,
                               &test_personal_data_manager_) {
    std::unique_ptr<TestingPrefServiceSimple> prefs(
        new TestingPrefServiceSimple());
    prefs->registry()->RegisterBooleanPref(autofill::prefs::kAutofillEnabled,
                                           true);
    test_autofill_client_.SetPrefs(std::move(prefs));
    mock_autofill_download_manager_ = new MockAutofillDownloadManager(
        &test_autofill_driver_, &mock_autofill_manager_);
    // AutofillManager takes ownership of |mock_autofill_download_manager_|.
    mock_autofill_manager_.SetDownloadManager(mock_autofill_download_manager_);
  }

  ~MockPasswordManagerDriver() {}

  MOCK_METHOD1(FillPasswordForm, void(const autofill::PasswordFormFillData&));
  MOCK_METHOD1(ShowInitialPasswordAccountSuggestions,
               void(const autofill::PasswordFormFillData&));
  MOCK_METHOD1(AllowPasswordGenerationForForm,
               void(const autofill::PasswordForm&));

  MockAutofillManager* mock_autofill_manager() {
    return &mock_autofill_manager_;
  }

  MockAutofillDownloadManager* mock_autofill_download_manager() {
    return mock_autofill_download_manager_;
  }

 private:
  autofill::TestAutofillDriver test_autofill_driver_;
  autofill::TestAutofillClient test_autofill_client_;
  autofill::TestPersonalDataManager test_personal_data_manager_;
  MockAutofillDownloadManager* mock_autofill_download_manager_;

  NiceMock<MockAutofillManager> mock_autofill_manager_;
};

class TestPasswordManagerClient : public StubPasswordManagerClient {
 public:
  TestPasswordManagerClient()
      : driver_(new NiceMock<MockPasswordManagerDriver>) {
    prefs_.registry()->RegisterBooleanPref(prefs::kCredentialsEnableService,
                                           true);
  }

  PrefService* GetPrefs() override { return &prefs_; }

  MockPasswordManagerDriver* mock_driver() { return driver_.get(); }

  base::WeakPtr<PasswordManagerDriver> driver() { return driver_->AsWeakPtr(); }

  autofill::AutofillManager* GetAutofillManagerForMainFrame() override {
    return mock_driver()->mock_autofill_manager();
  }

  void KillDriver() { driver_.reset(); }

 private:
  TestingPrefServiceSimple prefs_;
  std::unique_ptr<MockPasswordManagerDriver> driver_;
};

ACTION_P(SaveToUniquePtr, scoped) {
  scoped->reset(arg0);
}

}  // namespace

class PasswordFormManagerTest : public testing::Test {
 public:
  PasswordFormManagerTest() { fake_form_fetcher_.Fetch(); }

  void SetUp() override {
    observed_form_.origin = GURL("http://accounts.google.com/a/LoginAuth");
    observed_form_.action = GURL("http://accounts.google.com/a/Login");
    observed_form_.username_element = ASCIIToUTF16("Email");
    observed_form_.password_element = ASCIIToUTF16("Passwd");
    observed_form_.submit_element = ASCIIToUTF16("signIn");
    observed_form_.signon_realm = "http://accounts.google.com";

    saved_match_ = observed_form_;
    saved_match_.origin = GURL("http://accounts.google.com/a/ServiceLoginAuth");
    saved_match_.action = GURL("http://accounts.google.com/a/ServiceLogin");
    saved_match_.preferred = true;
    saved_match_.username_value = ASCIIToUTF16("test@gmail.com");
    saved_match_.password_value = ASCIIToUTF16("test1");
    saved_match_.other_possible_usernames.push_back(PossibleUsernamePair(
        ASCIIToUTF16("test2@gmail.com"), ASCIIToUTF16("full_name")));

    psl_saved_match_ = saved_match_;
    psl_saved_match_.is_public_suffix_match = true;
    psl_saved_match_.origin =
        GURL("http://m.accounts.google.com/a/ServiceLoginAuth");
    psl_saved_match_.action = GURL("http://m.accounts.google.com/a/Login");
    psl_saved_match_.signon_realm = "http://m.accounts.google.com";

    autofill::FormFieldData field;
    field.label = ASCIIToUTF16("full_name");
    field.name = ASCIIToUTF16("full_name");
    field.form_control_type = "text";
    saved_match_.form_data.fields.push_back(field);

    field.label = ASCIIToUTF16("Email");
    field.name = ASCIIToUTF16("Email");
    field.form_control_type = "text";
    saved_match_.form_data.fields.push_back(field);

    field.label = ASCIIToUTF16("password");
    field.name = ASCIIToUTF16("Passwd");
    field.form_control_type = "password";
    saved_match_.form_data.fields.push_back(field);

    password_manager_.reset(new PasswordManager(&client_));
    form_manager_.reset(new PasswordFormManager(
        password_manager_.get(), &client_, client_.driver(), observed_form_,
        base::MakeUnique<NiceMock<MockFormSaver>>(), &fake_form_fetcher_));
  }

  // Save saved_match() for observed_form() where |observed_form_data|,
  // |times_used|, and |status| are used to overwrite the default values for
  // observed_form(). |field_type| is the upload that we expect from saving,
  // with nullptr meaning no upload expected.
  void AccountCreationUploadTest(const autofill::FormData& observed_form_data,
                                 int times_used,
                                 PasswordForm::GenerationUploadStatus status,
                                 const autofill::ServerFieldType* field_type) {
    PasswordForm form(*observed_form());

    form.form_data = observed_form_data;

    FakeFormFetcher fetcher;
    fetcher.Fetch();
    PasswordFormManager form_manager(
        password_manager(), client(), client()->driver(), form,
        base::MakeUnique<NiceMock<MockFormSaver>>(), &fetcher);
    PasswordForm match = CreateSavedMatch(false);
    match.generation_upload_status = status;
    match.times_used = times_used;

    PasswordForm form_to_save(form);
    form_to_save.preferred = true;
    form_to_save.username_element = ASCIIToUTF16("observed-username-field");
    form_to_save.username_value = match.username_value;
    form_to_save.password_value = match.password_value;

    fetcher.SetNonFederated({&match}, 0u);
    std::string expected_login_signature;
    autofill::FormStructure observed_structure(observed_form_data);
    autofill::FormStructure pending_structure(saved_match()->form_data);
    if (observed_structure.FormSignatureAsStr() !=
            pending_structure.FormSignatureAsStr() &&
        times_used == 0) {
      expected_login_signature = observed_structure.FormSignatureAsStr();
    }
    autofill::ServerFieldTypeSet expected_available_field_types;
    FieldTypeMap expected_types;
    expected_types[ASCIIToUTF16("full_name")] = autofill::UNKNOWN_TYPE;
    expected_types[match.username_element] = autofill::UNKNOWN_TYPE;

    bool expect_generation_vote = false;
    if (field_type) {
      // Show the password generation popup to check that the generation vote
      // would be ignored.
      form_manager.set_generation_element(saved_match()->password_element);
      form_manager.set_generation_popup_was_shown(true);
      expect_generation_vote =
          *field_type != autofill::ACCOUNT_CREATION_PASSWORD;

      expected_available_field_types.insert(*field_type);
      expected_types[saved_match()->password_element] = *field_type;
    }

    if (field_type) {
      EXPECT_CALL(
          *client()->mock_driver()->mock_autofill_download_manager(),
          StartUploadRequest(CheckUploadedAutofillTypesAndSignature(
                                 pending_structure.FormSignatureAsStr(),
                                 expected_types, expect_generation_vote),
                             false, expected_available_field_types,
                             expected_login_signature, true));
    } else {
      EXPECT_CALL(*client()->mock_driver()->mock_autofill_download_manager(),
                  StartUploadRequest(_, _, _, _, _))
          .Times(0);
    }
    form_manager.ProvisionallySave(
        form_to_save, PasswordFormManager::IGNORE_OTHER_POSSIBLE_USERNAMES);
    form_manager.Save();
    Mock::VerifyAndClearExpectations(
        client()->mock_driver()->mock_autofill_download_manager());
  }

  // Test upload votes on change password forms. |field_type| is a vote that we
  // expect to be uploaded.
  void ChangePasswordUploadTest(autofill::ServerFieldType field_type,
                                bool has_confirmation_field) {
    SCOPED_TRACE(testing::Message()
                 << "field_type=" << field_type
                 << " has_confirmation_field=" << has_confirmation_field);

    // |observed_form_| should have |form_data| in order to be uploaded.
    observed_form()->form_data = saved_match()->form_data;
    // Turn |observed_form_| and  into change password form.
    observed_form()->new_password_element = ASCIIToUTF16("NewPasswd");
    observed_form()->confirmation_password_element = ASCIIToUTF16("ConfPwd");
    autofill::FormFieldData field;
    field.label = ASCIIToUTF16("NewPasswd");
    field.name = ASCIIToUTF16("NewPasswd");
    field.form_control_type = "password";
    observed_form()->form_data.fields.push_back(field);
    autofill::FormFieldData empty_field;
    observed_form()->form_data.fields.push_back(empty_field);
    if (has_confirmation_field) {
      field.label = ASCIIToUTF16("ConfPwd");
      field.name = ASCIIToUTF16("ConfPwd");
      field.form_control_type = "password";
      observed_form()->form_data.fields.push_back(field);
    }

    FakeFormFetcher fetcher;
    fetcher.Fetch();
    PasswordFormManager form_manager(
        password_manager(), client(), client()->driver(), *observed_form(),
        base::MakeUnique<NiceMock<MockFormSaver>>(), &fetcher);
    fetcher.SetNonFederated({saved_match()}, 0u);

    // User submits current and new credentials to the observed form.
    PasswordForm submitted_form(*observed_form());
    // credentials.username_element.clear();
    submitted_form.username_value = saved_match()->username_value;
    submitted_form.password_value = saved_match()->password_value;
    submitted_form.new_password_value = ASCIIToUTF16("test2");
    submitted_form.preferred = true;
    form_manager.ProvisionallySave(
        submitted_form, PasswordFormManager::IGNORE_OTHER_POSSIBLE_USERNAMES);

    // Successful login. The PasswordManager would instruct PasswordFormManager
    // to update.
    EXPECT_FALSE(form_manager.IsNewLogin());
    EXPECT_FALSE(
        form_manager.is_possible_change_password_form_without_username());

    // By now, the PasswordFormManager should have promoted the new password
    // value already to be the current password, and should no longer maintain
    // any info about the new password value.
    EXPECT_EQ(submitted_form.new_password_value,
              form_manager.pending_credentials().password_value);
    EXPECT_TRUE(form_manager.pending_credentials().new_password_value.empty());

    std::map<base::string16, autofill::ServerFieldType> expected_types;
    expected_types[ASCIIToUTF16("full_name")] = autofill::UNKNOWN_TYPE;
    expected_types[observed_form_.username_element] = autofill::UNKNOWN_TYPE;
    expected_types[observed_form_.password_element] = autofill::PASSWORD;
    expected_types[observed_form_.new_password_element] = field_type;
    expected_types[base::string16()] = autofill::UNKNOWN_TYPE;

    autofill::ServerFieldTypeSet expected_available_field_types;
    expected_available_field_types.insert(autofill::PASSWORD);
    expected_available_field_types.insert(field_type);
    if (has_confirmation_field) {
      expected_types[observed_form_.confirmation_password_element] =
          autofill::CONFIRMATION_PASSWORD;
      expected_available_field_types.insert(autofill::CONFIRMATION_PASSWORD);
    }

    std::string observed_form_signature =
        autofill::FormStructure(observed_form()->form_data)
            .FormSignatureAsStr();

    std::string expected_login_signature;
    if (field_type == autofill::NEW_PASSWORD) {
      autofill::FormStructure pending_structure(saved_match()->form_data);
      expected_login_signature = pending_structure.FormSignatureAsStr();
    }
    EXPECT_CALL(*client()->mock_driver()->mock_autofill_download_manager(),
                StartUploadRequest(CheckUploadedAutofillTypesAndSignature(
                                       observed_form_signature, expected_types,
                                       false /* expect_generation_vote */),
                                   false, expected_available_field_types,
                                   expected_login_signature, true));

    switch (field_type) {
      case autofill::NEW_PASSWORD:
        form_manager.Update(*saved_match());
        break;
      case autofill::PROBABLY_NEW_PASSWORD:
        form_manager.OnNoInteraction(true /* it is an update */);
        break;
      case autofill::NOT_NEW_PASSWORD:
        form_manager.OnNopeUpdateClicked();
        break;
      default:
        NOTREACHED();
    }
    Mock::VerifyAndClearExpectations(
        client()->mock_driver()->mock_autofill_download_manager());
  }

  autofill::AutofillUploadContents::Field::PasswordGenerationType
  GetExpectedPasswordGenerationType(bool is_manual_generation,
                                    bool is_change_password_form,
                                    bool has_generated_password) {
    if (!has_generated_password)
      return autofill::AutofillUploadContents::Field::IGNORED_GENERATION_POPUP;

    if (is_manual_generation) {
      if (is_change_password_form) {
        return autofill::AutofillUploadContents::Field::
            MANUALLY_TRIGGERED_GENERATION_ON_CHANGE_PASSWORD_FORM;
      } else {
        return autofill::AutofillUploadContents::Field::
            MANUALLY_TRIGGERED_GENERATION_ON_SIGN_UP_FORM;
      }
    } else {
      if (is_change_password_form) {
        return autofill::AutofillUploadContents::Field::
            AUTOMATICALLY_TRIGGERED_GENERATION_ON_CHANGE_PASSWORD_FORM;
      } else {
        return autofill::AutofillUploadContents::Field::
            AUTOMATICALLY_TRIGGERED_GENERATION_ON_SIGN_UP_FORM;
      }
    }
  }

  // The user types username and generates password on SignUp or change password
  // form. The password generation might be triggered automatically or manually.
  // This function checks that correct vote is uploaded on server. The vote must
  // be uploaded regardless of the user's interaction with the prompt.
  void GeneratedVoteUploadTest(bool is_manual_generation,
                               bool is_change_password_form,
                               bool has_generated_password,
                               SavePromptInteraction interaction) {
    SCOPED_TRACE(testing::Message()
                 << "is_manual_generation=" << is_manual_generation
                 << " is_change_password_form=" << is_change_password_form
                 << " has_generated_password=" << has_generated_password
                 << " interaction=" << interaction);
    PasswordForm form(*observed_form());
    form.form_data = saved_match()->form_data;

    if (is_change_password_form) {
      // Turn |form| to a change password form.
      form.new_password_element = ASCIIToUTF16("NewPasswd");

      autofill::FormFieldData field;
      field.label = ASCIIToUTF16("password");
      field.name = ASCIIToUTF16("NewPasswd");
      field.form_control_type = "password";
      form.form_data.fields.push_back(field);
    }

    // Create submitted form.
    PasswordForm submitted_form(form);
    submitted_form.preferred = true;
    submitted_form.username_value = saved_match()->username_value;
    submitted_form.password_value = saved_match()->password_value;

    if (is_change_password_form) {
      submitted_form.new_password_value =
          saved_match()->password_value + ASCIIToUTF16("1");
    }

    FakeFormFetcher fetcher;
    fetcher.Fetch();
    PasswordFormManager form_manager(
        password_manager(), client(), client()->driver(), form,
        base::MakeUnique<NiceMock<MockFormSaver>>(), &fetcher);

    fetcher.SetNonFederated(std::vector<const PasswordForm*>(), 0u);

    autofill::ServerFieldTypeSet expected_available_field_types;
    // Don't send autofill votes if the user didn't press "Save" button.
    if (interaction == SAVE)
      expected_available_field_types.insert(autofill::PASSWORD);

    form_manager.set_is_manual_generation(is_manual_generation);
    base::string16 generation_element = is_change_password_form
                                            ? form.new_password_element
                                            : form.password_element;
    form_manager.set_generation_element(generation_element);
    form_manager.set_generation_popup_was_shown(true);
    form_manager.set_has_generated_password(has_generated_password);

    // Figure out expected generation event type.
    autofill::AutofillUploadContents::Field::PasswordGenerationType
        expected_generation_type = GetExpectedPasswordGenerationType(
            is_manual_generation, is_change_password_form,
            has_generated_password);
    std::map<base::string16,
             autofill::AutofillUploadContents::Field::PasswordGenerationType>
        expected_generation_types;
    expected_generation_types[generation_element] = expected_generation_type;

    autofill::FormStructure form_structure(submitted_form.form_data);

    EXPECT_CALL(
        *client()->mock_driver()->mock_autofill_download_manager(),
        StartUploadRequest(
            CheckUploadedGenerationTypesAndSignature(
                form_structure.FormSignatureAsStr(), expected_generation_types),
            false, expected_available_field_types, std::string(), true));

    form_manager.ProvisionallySave(
        submitted_form, PasswordFormManager::IGNORE_OTHER_POSSIBLE_USERNAMES);
    switch (interaction) {
      case SAVE:
        form_manager.Save();
        break;
      case NEVER:
        form_manager.OnNeverClicked();
        break;
      case NO_INTERACTION:
        form_manager.OnNoInteraction(false /* not an update prompt*/);
        break;
    }
    Mock::VerifyAndClearExpectations(
        client()->mock_driver()->mock_autofill_download_manager());
  }

  PasswordForm* observed_form() { return &observed_form_; }
  PasswordForm* saved_match() { return &saved_match_; }
  PasswordForm* psl_saved_match() { return &psl_saved_match_; }
  PasswordForm CreateSavedMatch(bool blacklisted) {
    PasswordForm match = saved_match_;
    match.blacklisted_by_user = blacklisted;
    return match;
  }

  TestPasswordManagerClient* client() { return &client_; }

  PasswordManager* password_manager() { return password_manager_.get(); }

  PasswordFormManager* form_manager() { return form_manager_.get(); }

  FakeFormFetcher* fake_form_fetcher() { return &fake_form_fetcher_; }

  // To spare typing for PasswordFormManager instances which need no driver.
  const base::WeakPtr<PasswordManagerDriver> kNoDriver;

 private:
  // Necessary for callbacks, and for TestAutofillDriver.
  base::MessageLoop message_loop_;

  PasswordForm observed_form_;
  PasswordForm saved_match_;
  PasswordForm psl_saved_match_;
  TestPasswordManagerClient client_;
  std::unique_ptr<PasswordManager> password_manager_;
  // Define |fake_form_fetcher_| before |form_manager_|, because the former
  // needs to outlive the latter.
  FakeFormFetcher fake_form_fetcher_;
  std::unique_ptr<PasswordFormManager> form_manager_;
};

class PasswordFormManagerFillOnAccountSelectTest
    : public PasswordFormManagerTest {
 public:
  PasswordFormManagerFillOnAccountSelectTest() {
    scoped_feature_list_.InitAndEnableFeature(features::kFillOnAccountSelect);
  }

  base::test::ScopedFeatureList scoped_feature_list_;
};

// Test provisionally saving a new login.
TEST_F(PasswordFormManagerTest, TestNewLogin) {
  fake_form_fetcher()->SetNonFederated(std::vector<const PasswordForm*>(), 0u);

  // User submits credentials for the observed form.
  PasswordForm credentials = *observed_form();
  credentials.username_value = saved_match()->username_value;
  credentials.password_value = saved_match()->password_value;
  credentials.preferred = true;
  form_manager()->ProvisionallySave(
      credentials, PasswordFormManager::IGNORE_OTHER_POSSIBLE_USERNAMES);

  // Successful login. The PasswordManager would instruct PasswordFormManager
  // to save, which should know this is a new login.
  EXPECT_TRUE(form_manager()->IsNewLogin());
  // Make sure the credentials that would be submitted on successful login
  // are going to match the stored entry in the db.
  EXPECT_EQ(observed_form()->origin.spec(),
            form_manager()->pending_credentials().origin.spec());
  EXPECT_EQ(observed_form()->signon_realm,
            form_manager()->pending_credentials().signon_realm);
  EXPECT_EQ(observed_form()->action,
            form_manager()->pending_credentials().action);
  EXPECT_TRUE(form_manager()->pending_credentials().preferred);
  EXPECT_EQ(saved_match()->password_value,
            form_manager()->pending_credentials().password_value);
  EXPECT_EQ(saved_match()->username_value,
            form_manager()->pending_credentials().username_value);
  EXPECT_TRUE(
      form_manager()->pending_credentials().new_password_element.empty());
  EXPECT_TRUE(form_manager()->pending_credentials().new_password_value.empty());
}

// Test provisionally saving a new login in presence of other saved logins.
TEST_F(PasswordFormManagerTest, TestAdditionalLogin) {
  fake_form_fetcher()->SetNonFederated({saved_match()}, 0u);

  base::string16 new_user = ASCIIToUTF16("newuser");
  base::string16 new_pass = ASCIIToUTF16("newpass");
  ASSERT_NE(new_user, saved_match()->username_value);

  PasswordForm new_login = *observed_form();
  new_login.username_value = new_user;
  new_login.password_value = new_pass;
  new_login.preferred = true;

  form_manager()->ProvisionallySave(
      new_login, PasswordFormManager::IGNORE_OTHER_POSSIBLE_USERNAMES);

  // The username value differs from the saved match, so this is a new login.
  EXPECT_TRUE(form_manager()->IsNewLogin());

  EXPECT_EQ(observed_form()->origin.spec(),
            form_manager()->pending_credentials().origin.spec());
  EXPECT_EQ(observed_form()->signon_realm,
            form_manager()->pending_credentials().signon_realm);
  EXPECT_TRUE(form_manager()->pending_credentials().preferred);
  EXPECT_EQ(new_pass, form_manager()->pending_credentials().password_value);
  EXPECT_EQ(new_user, form_manager()->pending_credentials().username_value);
  EXPECT_TRUE(
      form_manager()->pending_credentials().new_password_element.empty());
  EXPECT_TRUE(form_manager()->pending_credentials().new_password_value.empty());
}

// Test blacklisting in the presence of saved results.
TEST_F(PasswordFormManagerTest, TestBlacklist) {
  saved_match()->origin = observed_form()->origin;
  saved_match()->action = observed_form()->action;
  fake_form_fetcher()->SetNonFederated({saved_match()}, 0u);

  PasswordForm new_login = *observed_form();
  new_login.username_value = ASCIIToUTF16("newuser");
  new_login.password_value = ASCIIToUTF16("newpass");
  // Pretend Chrome detected a form submission with |new_login|.
  form_manager()->ProvisionallySave(
      new_login, PasswordFormManager::IGNORE_OTHER_POSSIBLE_USERNAMES);

  EXPECT_TRUE(form_manager()->IsNewLogin());
  EXPECT_EQ(observed_form()->origin.spec(),
            form_manager()->pending_credentials().origin.spec());
  EXPECT_EQ(observed_form()->signon_realm,
            form_manager()->pending_credentials().signon_realm);

  const PasswordForm pending_form = form_manager()->pending_credentials();
  PasswordForm actual_add_form;
  // Now pretend the user wants to never save passwords on this origin. Chrome
  // is supposed to only request blacklisting of a single form.
  EXPECT_CALL(MockFormSaver::Get(form_manager()), PermanentlyBlacklist(_))
      .WillOnce(SaveArgPointee<0>(&actual_add_form));
  form_manager()->PermanentlyBlacklist();
  EXPECT_EQ(pending_form, form_manager()->pending_credentials());
  // The PasswordFormManager should have updated its knowledge of blacklisting
  // without waiting for PasswordStore updates.
  EXPECT_TRUE(form_manager()->IsBlacklisted());
  EXPECT_THAT(form_manager()->blacklisted_matches(),
              UnorderedElementsAre(Pointee(actual_add_form)));
}

// Test that stored blacklisted forms are correctly evaluated for whether they
// apply to the observed form.
TEST_F(PasswordFormManagerTest, TestBlacklistMatching) {
  observed_form()->origin = GURL("http://accounts.google.com/a/LoginAuth");
  observed_form()->action = GURL("http://accounts.google.com/a/Login");
  observed_form()->signon_realm = "http://accounts.google.com";
  FakeFormFetcher fetcher;
  fetcher.Fetch();
  PasswordFormManager form_manager(password_manager(), client(),
                                   client()->driver(), *observed_form(),
                                   base::MakeUnique<MockFormSaver>(), &fetcher);

  // Doesn't apply because it is just a PSL match of the observed form.
  PasswordForm blacklisted_psl = *observed_form();
  blacklisted_psl.signon_realm = "http://m.accounts.google.com";
  blacklisted_psl.is_public_suffix_match = true;
  blacklisted_psl.blacklisted_by_user = true;

  // Doesn't apply because of different origin.
  PasswordForm blacklisted_not_match = *observed_form();
  blacklisted_not_match.origin = GURL("http://google.com/a/LoginAuth");
  blacklisted_not_match.blacklisted_by_user = true;

  // Doesn't apply because of different username element and different page.
  PasswordForm blacklisted_not_match2 = *observed_form();
  blacklisted_not_match2.origin = GURL("http://accounts.google.com/a/Login123");
  blacklisted_not_match2.username_element = ASCIIToUTF16("Element");
  blacklisted_not_match2.blacklisted_by_user = true;

  // Doesn't apply because of different PasswordForm::Scheme.
  PasswordForm blacklisted_not_match3 = *observed_form();
  blacklisted_not_match3.scheme = PasswordForm::SCHEME_BASIC;

  // Applies because of same element names, despite different page
  PasswordForm blacklisted_match = *observed_form();
  blacklisted_match.origin = GURL("http://accounts.google.com/a/LoginAuth1234");
  blacklisted_match.blacklisted_by_user = true;

  // Applies because of same page, despite different element names
  PasswordForm blacklisted_match2 = *observed_form();
  blacklisted_match2.origin = GURL("http://accounts.google.com/a/LoginAuth");
  blacklisted_match2.username_element = ASCIIToUTF16("Element");
  blacklisted_match2.blacklisted_by_user = true;

  std::vector<const PasswordForm*> matches = {&blacklisted_psl,
                                              &blacklisted_not_match,
                                              &blacklisted_not_match2,
                                              &blacklisted_not_match3,
                                              &blacklisted_match,
                                              &blacklisted_match2,
                                              saved_match()};
  fetcher.SetNonFederated(matches, 0u);

  EXPECT_TRUE(form_manager.IsBlacklisted());
  EXPECT_THAT(form_manager.blacklisted_matches(),
              UnorderedElementsAre(Pointee(blacklisted_match),
                                   Pointee(blacklisted_match2)));
  EXPECT_EQ(1u, form_manager.best_matches().size());
  EXPECT_EQ(*saved_match(), *form_manager.preferred_match());
}

// Test that even in the presence of blacklisted matches, the non-blacklisted
// ones are still autofilled.
TEST_F(PasswordFormManagerTest, AutofillBlacklisted) {
  PasswordForm saved_form = *observed_form();
  saved_form.username_value = ASCIIToUTF16("user");
  saved_form.password_value = ASCIIToUTF16("pass");

  PasswordForm blacklisted = *observed_form();
  blacklisted.blacklisted_by_user = true;
  blacklisted.username_value.clear();

  autofill::PasswordFormFillData fill_data;
  EXPECT_CALL(*client()->mock_driver(), FillPasswordForm(_))
      .WillOnce(SaveArg<0>(&fill_data));

  fake_form_fetcher()->SetNonFederated({&saved_form, &blacklisted}, 0u);
  EXPECT_EQ(1u, form_manager()->blacklisted_matches().size());
  EXPECT_TRUE(form_manager()->IsBlacklisted());
  EXPECT_EQ(1u, form_manager()->best_matches().size());
  EXPECT_TRUE(fill_data.additional_logins.empty());
}

// If PSL-matched credentials had been suggested, but the user has overwritten
// the password, the provisionally saved credentials should no longer be
// considered as PSL-matched, so that the exception for not prompting before
// saving PSL-matched credentials should no longer apply.
TEST_F(PasswordFormManagerTest,
       OverriddenPSLMatchedCredentialsNotMarkedAsPSLMatched) {
  // The suggestion needs to be PSL-matched.
  fake_form_fetcher()->SetNonFederated({psl_saved_match()}, 0u);

  // User modifies the suggested password and submits the form.
  PasswordForm credentials(*observed_form());
  credentials.username_value = saved_match()->username_value;
  credentials.password_value =
      saved_match()->password_value + ASCIIToUTF16("modify");
  form_manager()->ProvisionallySave(
      credentials, PasswordFormManager::IGNORE_OTHER_POSSIBLE_USERNAMES);

  EXPECT_TRUE(form_manager()->IsNewLogin());
  EXPECT_FALSE(form_manager()->IsPendingCredentialsPublicSuffixMatch());
}

// Test that if a PSL-matched suggestion is saved on a new origin, its metadata
// are correctly updated.
TEST_F(PasswordFormManagerTest, PSLMatchedCredentialsMetadataUpdated) {
  PasswordForm psl_suggestion = *saved_match();
  psl_suggestion.is_public_suffix_match = true;
  fake_form_fetcher()->SetNonFederated({&psl_suggestion}, 0u);

  PasswordForm submitted_form(*observed_form());
  submitted_form.preferred = true;
  submitted_form.username_value = saved_match()->username_value;
  submitted_form.password_value = saved_match()->password_value;
  form_manager()->ProvisionallySave(
      submitted_form, PasswordFormManager::IGNORE_OTHER_POSSIBLE_USERNAMES);

  PasswordForm expected_saved_form(submitted_form);
  expected_saved_form.times_used = 1;
  expected_saved_form.other_possible_usernames.clear();
  expected_saved_form.form_data = saved_match()->form_data;
  expected_saved_form.origin = observed_form()->origin;
  expected_saved_form.is_public_suffix_match = true;
  PasswordForm actual_saved_form;

  autofill::ServerFieldTypeSet expected_available_field_types;
  expected_available_field_types.insert(autofill::ACCOUNT_CREATION_PASSWORD);
  EXPECT_CALL(
      *client()->mock_driver()->mock_autofill_download_manager(),
      StartUploadRequest(_, false, expected_available_field_types, _, true));
  EXPECT_CALL(MockFormSaver::Get(form_manager()), Save(_, _, nullptr))
      .WillOnce(SaveArg<0>(&actual_saved_form));
  form_manager()->Save();

  // Can't verify time, so ignore it.
  actual_saved_form.date_created = base::Time();
  EXPECT_EQ(expected_saved_form, actual_saved_form);
}

// Test that when the submitted form contains a "new-password" field, then the
// password value is taken from there.
TEST_F(PasswordFormManagerTest, TestNewLoginFromNewPasswordElement) {
  observed_form()->new_password_element = ASCIIToUTF16("NewPasswd");
  observed_form()->username_marked_by_site = true;

  FakeFormFetcher fetcher;
  fetcher.Fetch();
  PasswordFormManager form_manager(password_manager(), client(),
                                   client()->driver(), *observed_form(),
                                   base::MakeUnique<MockFormSaver>(), &fetcher);
  fetcher.SetNonFederated(std::vector<const PasswordForm*>(), 0u);

  // User enters current and new credentials to the observed form.
  PasswordForm credentials(*observed_form());
  credentials.username_value = saved_match()->username_value;
  credentials.password_value = ASCIIToUTF16("oldpassword");
  credentials.new_password_value = ASCIIToUTF16("newpassword");
  credentials.preferred = true;
  form_manager.ProvisionallySave(
      credentials, PasswordFormManager::IGNORE_OTHER_POSSIBLE_USERNAMES);

  // Successful login. The PasswordManager would instruct PasswordFormManager
  // to save, which should know this is a new login.
  EXPECT_TRUE(form_manager.IsNewLogin());
  EXPECT_EQ(credentials.origin, form_manager.pending_credentials().origin);
  EXPECT_EQ(credentials.signon_realm,
            form_manager.pending_credentials().signon_realm);
  EXPECT_EQ(credentials.action, form_manager.pending_credentials().action);
  EXPECT_TRUE(form_manager.pending_credentials().preferred);
  EXPECT_EQ(credentials.username_value,
            form_manager.pending_credentials().username_value);

  // By this point, the PasswordFormManager should have promoted the new
  // password value to be the current password, and should have wiped the
  // password element name: it is likely going to be different on a login
  // form, so it is not worth remembering them.
  EXPECT_EQ(credentials.new_password_value,
            form_manager.pending_credentials().password_value);
  EXPECT_TRUE(form_manager.pending_credentials().password_element.empty());
  EXPECT_TRUE(form_manager.pending_credentials().new_password_value.empty());
}

TEST_F(PasswordFormManagerTest, TestUpdatePassword) {
  fake_form_fetcher()->SetNonFederated({saved_match()}, 0u);

  // User submits credentials for the observed form using a username previously
  // stored, but a new password. Note that the observed form may have different
  // origin URL (as it does in this case) than the saved_match, but we want to
  // make sure the updated password is reflected in saved_match, because that is
  // what we autofilled.
  base::string16 new_pass = ASCIIToUTF16("test2");
  PasswordForm credentials = *observed_form();
  credentials.username_value = saved_match()->username_value;
  credentials.password_value = new_pass;
  credentials.preferred = true;
  form_manager()->ProvisionallySave(
      credentials, PasswordFormManager::IGNORE_OTHER_POSSIBLE_USERNAMES);

  // Successful login. The PasswordManager would instruct PasswordFormManager
  // to save, and since this is an update, it should know not to save as a new
  // login.
  EXPECT_FALSE(form_manager()->IsNewLogin());

  // Make sure the credentials that would be submitted on successful login
  // are going to match the stored entry in the db. (This verifies correct
  // behaviour for bug 1074420).
  EXPECT_EQ(form_manager()->pending_credentials().origin.spec(),
            saved_match()->origin.spec());
  EXPECT_EQ(form_manager()->pending_credentials().signon_realm,
            saved_match()->signon_realm);
  EXPECT_TRUE(form_manager()->pending_credentials().preferred);
  EXPECT_EQ(new_pass, form_manager()->pending_credentials().password_value);
}

TEST_F(PasswordFormManagerTest, TestUpdatePasswordFromNewPasswordElement) {
  // Add a new password field to the test form. The PasswordFormManager should
  // save the password from this field, instead of the current password field.
  observed_form()->new_password_element = ASCIIToUTF16("NewPasswd");

  // Given that |observed_form| was most likely a change password form, it
  // should not serve as a source for updating meta-information stored with the
  // old credentials, such as element names, as they are likely going to be
  // different between change password and login forms. To test this in depth,
  // forcibly wipe |submit_element|, which should normally trigger updating
  // this field from |observed_form| during updating as a special case. We will
  // verify in the end that this did not happen.
  saved_match()->submit_element.clear();

  FakeFormFetcher fetcher;
  fetcher.Fetch();
  PasswordFormManager form_manager(password_manager(), client(),
                                   client()->driver(), *observed_form(),
                                   base::MakeUnique<MockFormSaver>(), &fetcher);
  fetcher.SetNonFederated({saved_match()}, 0u);

  // User submits current and new credentials to the observed form.
  PasswordForm credentials(*observed_form());
  credentials.username_value = saved_match()->username_value;
  credentials.password_value = saved_match()->password_value;
  credentials.new_password_value = ASCIIToUTF16("test2");
  credentials.preferred = true;
  form_manager.ProvisionallySave(
      credentials, PasswordFormManager::IGNORE_OTHER_POSSIBLE_USERNAMES);

  // Successful login. The PasswordManager would instruct PasswordFormManager
  // to save, and since this is an update, it should know not to save as a new
  // login.
  EXPECT_FALSE(form_manager.IsNewLogin());

  // By now, the PasswordFormManager should have promoted the new password value
  // already to be the current password, and should no longer maintain any info
  // about the new password.
  EXPECT_EQ(credentials.new_password_value,
            form_manager.pending_credentials().password_value);
  EXPECT_TRUE(form_manager.pending_credentials().new_password_element.empty());
  EXPECT_TRUE(form_manager.pending_credentials().new_password_value.empty());

  // Trigger saving to exercise some special case handling for updating.
  PasswordForm new_credentials;
  EXPECT_CALL(MockFormSaver::Get(&form_manager), Update(_, _, _, nullptr))
      .WillOnce(testing::SaveArg<0>(&new_credentials));

  form_manager.Save();

  // No meta-information should be updated, only the password.
  EXPECT_EQ(credentials.new_password_value, new_credentials.password_value);
  EXPECT_EQ(saved_match()->username_element, new_credentials.username_element);
  EXPECT_EQ(saved_match()->password_element, new_credentials.password_element);
  EXPECT_EQ(saved_match()->submit_element, new_credentials.submit_element);
}

// Test that saved results are not ignored if they differ in paths for action or
// origin.
TEST_F(PasswordFormManagerTest, TestIgnoreResult_Paths) {
  PasswordForm observed(*observed_form());
  observed.origin = GURL("https://accounts.google.com/a/LoginAuth");
  observed.action = GURL("https://accounts.google.com/a/Login");
  observed.signon_realm = "https://accounts.google.com";

  FakeFormFetcher fetcher;
  fetcher.Fetch();
  PasswordFormManager form_manager(password_manager(), client(),
                                   client()->driver(), observed,
                                   base::MakeUnique<MockFormSaver>(), &fetcher);

  PasswordForm saved_form = observed;
  saved_form.origin = GURL("https://accounts.google.com/a/OtherLoginAuth");
  saved_form.action = GURL("https://accounts.google.com/a/OtherLogin");
  fetcher.SetNonFederated({&saved_form}, 0u);

  // Different paths for action / origin are okay.
  EXPECT_EQ(1u, form_manager.best_matches().size());
  EXPECT_EQ(*form_manager.best_matches().begin()->second, saved_form);
}

// Test that saved empty action URL is updated with the submitted action URL.
TEST_F(PasswordFormManagerTest, TestEmptyAction) {
  saved_match()->action = GURL();
  fake_form_fetcher()->SetNonFederated({saved_match()}, 0u);

  // User logs in with the autofilled username / password from saved_match.
  PasswordForm login = *observed_form();
  login.username_value = saved_match()->username_value;
  login.password_value = saved_match()->password_value;
  form_manager()->ProvisionallySave(
      login, PasswordFormManager::IGNORE_OTHER_POSSIBLE_USERNAMES);
  EXPECT_FALSE(form_manager()->IsNewLogin());
  // Chrome updates the saved PasswordForm entry with the action URL of the
  // observed form.
  EXPECT_EQ(observed_form()->action,
            form_manager()->pending_credentials().action);
}

TEST_F(PasswordFormManagerTest, TestUpdateAction) {
  saved_match()->action = GURL("http://accounts.google.com/a/ServiceLogin");
  fake_form_fetcher()->SetNonFederated({saved_match()}, 0u);

  // User logs in with the autofilled username / password from saved_match.
  observed_form()->action = GURL("http://accounts.google.com/a/Login");
  PasswordForm login = *observed_form();
  login.username_value = saved_match()->username_value;
  login.password_value = saved_match()->password_value;

  form_manager()->ProvisionallySave(
      login, PasswordFormManager::IGNORE_OTHER_POSSIBLE_USERNAMES);
  EXPECT_FALSE(form_manager()->IsNewLogin());
  // The observed action URL is different from the previously saved one. Chrome
  // should update the store by setting the pending credential's action URL to
  // be that of the currently observed form.
  EXPECT_EQ(observed_form()->action,
            form_manager()->pending_credentials().action);
}

TEST_F(PasswordFormManagerTest, TestDynamicAction) {
  fake_form_fetcher()->SetNonFederated(std::vector<const PasswordForm*>(), 0u);

  observed_form()->action = GURL("http://accounts.google.com/a/Login");
  PasswordForm login(*observed_form());
  // The submitted action URL is different from the one observed on page load.
  login.action = GURL("http://www.google.com/new_action");

  form_manager()->ProvisionallySave(
      login, PasswordFormManager::IGNORE_OTHER_POSSIBLE_USERNAMES);
  EXPECT_TRUE(form_manager()->IsNewLogin());
  // Check that the provisionally saved action URL is the same as the submitted
  // action URL, not the one observed on page load.
  EXPECT_EQ(login.action, form_manager()->pending_credentials().action);
}

// Test that if the saved match has other possible usernames stored, and the
// user chooses the main one, then the other possible usernames are dropped on
// update.
TEST_F(PasswordFormManagerTest, TestAlternateUsername_NoChange) {
  EXPECT_CALL(*client()->mock_driver(), AllowPasswordGenerationForForm(_));

  PasswordForm saved_form = *saved_match();
  saved_form.other_possible_usernames.push_back(
      PossibleUsernamePair(ASCIIToUTF16("other_possible@gmail.com"),
                           ASCIIToUTF16("other_username")));

  fake_form_fetcher()->SetNonFederated({&saved_form}, 0u);

  // The saved match has the right username already.
  PasswordForm login(*observed_form());
  login.preferred = true;
  login.username_value = saved_match()->username_value;
  login.password_value = saved_match()->password_value;

  form_manager()->ProvisionallySave(
      login, PasswordFormManager::ALLOW_OTHER_POSSIBLE_USERNAMES);

  EXPECT_FALSE(form_manager()->IsNewLogin());

  PasswordForm saved_result;
  EXPECT_CALL(MockFormSaver::Get(form_manager()), Update(_, _, _, nullptr))
      .WillOnce(SaveArg<0>(&saved_result));
  EXPECT_CALL(*client()->mock_driver()->mock_autofill_download_manager(),
              StartUploadRequest(_, false, _, _, true));

  form_manager()->Save();
  // Should be only one password stored, and should not have
  // |other_possible_usernames| set anymore.
  EXPECT_EQ(saved_match()->username_value, saved_result.username_value);
  EXPECT_TRUE(saved_result.other_possible_usernames.empty());
}

// Test that if the saved match has other possible usernames stored, and the
// user chooses an alternative one, then the other possible usernames are
// dropped on update, but the main username is changed to the one chosen by the
// user.
TEST_F(PasswordFormManagerTest, TestAlternateUsername_OtherUsername) {
  EXPECT_CALL(*client()->mock_driver(), AllowPasswordGenerationForForm(_));

  const PossibleUsernamePair kOtherUsername(
      ASCIIToUTF16("other_possible@gmail.com"), ASCIIToUTF16("other_username"));
  PasswordForm saved_form = *saved_match();
  saved_form.other_possible_usernames.push_back(kOtherUsername);

  fake_form_fetcher()->SetNonFederated({&saved_form}, 0u);

  // The user chooses an alternative username.
  PasswordForm login(*observed_form());
  login.preferred = true;
  login.username_value = kOtherUsername.first;
  login.password_value = saved_match()->password_value;

  form_manager()->ProvisionallySave(
      login, PasswordFormManager::ALLOW_OTHER_POSSIBLE_USERNAMES);

  EXPECT_FALSE(form_manager()->IsNewLogin());

  PasswordForm saved_result;
  // Changing the username changes the primary key of the stored credential.
  EXPECT_CALL(MockFormSaver::Get(form_manager()),
              Update(_, _, _, CheckUsernamePtr(saved_form.username_value)))
      .WillOnce(SaveArg<0>(&saved_result));

  form_manager()->Save();

  // |other_possible_usernames| should also be empty, but username_value should
  // be changed to match |new_username|.
  EXPECT_EQ(kOtherUsername.first, saved_result.username_value);
  EXPECT_TRUE(saved_result.other_possible_usernames.empty());
}

TEST_F(PasswordFormManagerTest, TestSendNotBlacklistedMessage_NoCredentials) {
  // First time sign-up attempt. Password store does not contain matching
  // credentials. AllowPasswordGenerationForForm should be called to send the
  // "not blacklisted" message.
  EXPECT_CALL(*(client()->mock_driver()), AllowPasswordGenerationForForm(_));
  fake_form_fetcher()->SetNonFederated(std::vector<const PasswordForm*>(), 0u);
}

TEST_F(PasswordFormManagerTest, TestSendNotBlacklistedMessage_Credentials) {
  // Signing up on a previously visited site. Credentials are found in the
  // password store, and are not blacklisted. AllowPasswordGenerationForForm
  // should be called to send the "not blacklisted" message.
  EXPECT_CALL(*(client()->mock_driver()), AllowPasswordGenerationForForm(_));
  PasswordForm simulated_result = CreateSavedMatch(false);
  fake_form_fetcher()->SetNonFederated({&simulated_result}, 0u);
}

TEST_F(PasswordFormManagerTest,
       TestSendNotBlacklistedMessage_DroppedCredentials) {
  // There are cases, such as when a form is made explicitly for creating a new
  // password, where we may ignore saved credentials. Make sure that we still
  // allow generation in that case.
  PasswordForm signup_form(*observed_form());
  signup_form.new_password_element = base::ASCIIToUTF16("new_password_field");

  FakeFormFetcher fetcher;
  fetcher.Fetch();
  PasswordFormManager form_manager(password_manager(), client(),
                                   client()->driver(), signup_form,
                                   base::MakeUnique<MockFormSaver>(), &fetcher);
  EXPECT_CALL(*(client()->mock_driver()), AllowPasswordGenerationForForm(_));
  PasswordForm simulated_result = CreateSavedMatch(false);
  fetcher.SetNonFederated({&simulated_result}, 0u);
}

TEST_F(PasswordFormManagerTest,
       TestSendNotBlacklistedMessage_BlacklistedCredentials) {
  // Signing up on a previously visited site. Credentials are found in the
  // password store, but they are blacklisted. AllowPasswordGenerationForForm
  // is still called.
  EXPECT_CALL(*(client()->mock_driver()), AllowPasswordGenerationForForm(_));
  PasswordForm simulated_result = CreateSavedMatch(true);
  fake_form_fetcher()->SetNonFederated({&simulated_result}, 0u);
}

// Test that exactly one match for each username is chosen as a best match, even
// though it is a PSL match.
TEST_F(PasswordFormManagerTest, TestBestCredentialsForEachUsernameAreIncluded) {
  // Add a best scoring match. It should be in |best_matches| and chosen as a
  // prefferred match.
  PasswordForm best_scoring = *saved_match();

  // Add a match saved on another form, it has lower score. It should not be in
  // |best_matches|.
  PasswordForm other_form = *saved_match();
  other_form.password_element = ASCIIToUTF16("signup_password");
  other_form.username_element = ASCIIToUTF16("signup_username");

  // Add a match saved on another form with a different username. It should be
  // in |best_matches|.
  PasswordForm other_username = other_form;
  const base::string16 kUsername1 =
      other_username.username_value + ASCIIToUTF16("1");
  other_username.username_value = kUsername1;

  // Add a PSL match, it should not be in |best_matches|.
  PasswordForm psl_match = *psl_saved_match();

  // Add a PSL match with a different username. It should be in |best_matches|.
  PasswordForm psl_match_other = psl_match;
  const base::string16 kUsername2 =
      psl_match_other.username_value + ASCIIToUTF16("2");
  psl_match_other.username_value = kUsername2;

  autofill::PasswordFormFillData fill_data;
  EXPECT_CALL(*client()->mock_driver(), FillPasswordForm(_))
      .WillOnce(SaveArg<0>(&fill_data));

  fake_form_fetcher()->SetNonFederated(
      {&best_scoring, &other_form, &other_username, &psl_match,
       &psl_match_other},
      0u);

  const std::map<base::string16, const PasswordForm*>& best_matches =
      form_manager()->best_matches();
  EXPECT_EQ(3u, best_matches.size());
  EXPECT_NE(best_matches.end(),
            best_matches.find(saved_match()->username_value));
  EXPECT_EQ(*saved_match(),
            *best_matches.find(saved_match()->username_value)->second);
  EXPECT_NE(best_matches.end(), best_matches.find(kUsername1));
  EXPECT_NE(best_matches.end(), best_matches.find(kUsername2));

  EXPECT_EQ(*saved_match(), *form_manager()->preferred_match());
  EXPECT_EQ(2u, fill_data.additional_logins.size());
}

TEST_F(PasswordFormManagerTest, TestSanitizePossibleUsernames) {
  const PossibleUsernamePair kUsernameOther(ASCIIToUTF16("other username"),
                                            ASCIIToUTF16("other_username_id"));

  fake_form_fetcher()->SetNonFederated(std::vector<const PasswordForm*>(), 0u);

  PasswordForm credentials(*observed_form());
  credentials.other_possible_usernames.push_back(
      PossibleUsernamePair(ASCIIToUTF16("543-43-1234"), ASCIIToUTF16("id1")));
  credentials.other_possible_usernames.push_back(PossibleUsernamePair(
      ASCIIToUTF16("378282246310005"), ASCIIToUTF16("id2")));
  credentials.other_possible_usernames.push_back(kUsernameOther);
  credentials.username_value = ASCIIToUTF16("test@gmail.com");
  credentials.preferred = true;

  // Pass in ALLOW_OTHER_POSSIBLE_USERNAMES, although it will not make a
  // difference as no matches coming from the password store were autofilled.
  form_manager()->ProvisionallySave(
      credentials, PasswordFormManager::ALLOW_OTHER_POSSIBLE_USERNAMES);

  PasswordForm saved_result;
  EXPECT_CALL(MockFormSaver::Get(form_manager()), Save(_, _, nullptr))
      .WillOnce(SaveArg<0>(&saved_result));
  form_manager()->Save();

  // Possible credit card number and SSN are stripped.
  EXPECT_THAT(saved_result.other_possible_usernames,
              UnorderedElementsAre(kUsernameOther));
}

TEST_F(PasswordFormManagerTest, TestSanitizePossibleUsernamesDuplicates) {
  const PossibleUsernamePair kUsernameSsn(ASCIIToUTF16("511-32-9830"),
                                          ASCIIToUTF16("ssn_id"));
  const PossibleUsernamePair kUsernameEmail(ASCIIToUTF16("test@gmail.com"),
                                            ASCIIToUTF16("email_id"));
  const PossibleUsernamePair kUsernameDuplicate(ASCIIToUTF16("duplicate"),
                                                ASCIIToUTF16("duplicate_id"));
  const PossibleUsernamePair kUsernameRandom(ASCIIToUTF16("random"),
                                             ASCIIToUTF16("random_id"));

  fake_form_fetcher()->SetNonFederated(std::vector<const PasswordForm*>(), 0u);

  PasswordForm credentials(*observed_form());
  credentials.other_possible_usernames.push_back(kUsernameSsn);
  credentials.other_possible_usernames.push_back(kUsernameDuplicate);
  credentials.other_possible_usernames.push_back(kUsernameDuplicate);
  credentials.other_possible_usernames.push_back(kUsernameRandom);
  credentials.other_possible_usernames.push_back(kUsernameEmail);
  credentials.username_value = kUsernameEmail.first;
  credentials.preferred = true;

  // Pass in ALLOW_OTHER_POSSIBLE_USERNAMES, although it will not make a
  // difference as no matches coming from the password store were autofilled.
  form_manager()->ProvisionallySave(
      credentials, PasswordFormManager::ALLOW_OTHER_POSSIBLE_USERNAMES);

  PasswordForm saved_result;
  EXPECT_CALL(MockFormSaver::Get(form_manager()), Save(_, _, nullptr))
      .WillOnce(SaveArg<0>(&saved_result));
  form_manager()->Save();

  // SSN, duplicate in |other_possible_usernames| and duplicate of
  // |username_value| all removed.
  EXPECT_THAT(saved_result.other_possible_usernames,
              UnorderedElementsAre(kUsernameDuplicate, kUsernameRandom));
}

// Test that if metadata stored with a form in PasswordStore are incomplete,
// they are updated upon the next encounter.
TEST_F(PasswordFormManagerTest, TestUpdateIncompleteCredentials) {
  PasswordForm encountered_form;
  encountered_form.origin = GURL("http://accounts.google.com/LoginAuth");
  encountered_form.signon_realm = "http://accounts.google.com/";
  encountered_form.action = GURL("http://accounts.google.com/Login");
  encountered_form.username_element = ASCIIToUTF16("Email");
  encountered_form.password_element = ASCIIToUTF16("Passwd");
  encountered_form.submit_element = ASCIIToUTF16("signIn");

  EXPECT_CALL(*(client()->mock_driver()), AllowPasswordGenerationForForm(_));

  FakeFormFetcher fetcher;
  fetcher.Fetch();
  PasswordFormManager form_manager(password_manager(), client(),
                                   client()->driver(), encountered_form,
                                   base::MakeUnique<MockFormSaver>(), &fetcher);

  PasswordForm incomplete_form;
  incomplete_form.origin = GURL("http://accounts.google.com/LoginAuth");
  incomplete_form.signon_realm = "http://accounts.google.com/";
  incomplete_form.password_value = ASCIIToUTF16("my_password");
  incomplete_form.username_value = ASCIIToUTF16("my_username");
  incomplete_form.preferred = true;
  incomplete_form.scheme = PasswordForm::SCHEME_HTML;

  // We expect to see this form eventually sent to the Password store. It
  // has password/username values from the store and 'username_element',
  // 'password_element', 'submit_element' and 'action' fields copied from
  // the encountered form.
  PasswordForm complete_form(incomplete_form);
  complete_form.action = encountered_form.action;
  complete_form.password_element = encountered_form.password_element;
  complete_form.username_element = encountered_form.username_element;
  complete_form.submit_element = encountered_form.submit_element;

  PasswordForm obsolete_form(incomplete_form);
  obsolete_form.action = encountered_form.action;

  // Feed the incomplete credentials to the manager.
  fetcher.SetNonFederated({&incomplete_form}, 0u);

  form_manager.ProvisionallySave(
      complete_form, PasswordFormManager::IGNORE_OTHER_POSSIBLE_USERNAMES);
  // By now that form has been used once.
  complete_form.times_used = 1;
  obsolete_form.times_used = 1;

  // Check that PasswordStore receives an update request with the complete form.
  EXPECT_CALL(MockFormSaver::Get(&form_manager),
              Update(complete_form, _, _, Pointee(obsolete_form)));
  form_manager.Save();
}

// Test that public-suffix-matched credentials score lower than same-origin
// ones.
TEST_F(PasswordFormManagerTest, TestScoringPublicSuffixMatch) {
  EXPECT_CALL(*(client()->mock_driver()), AllowPasswordGenerationForForm(_));

  PasswordForm base_match = CreateSavedMatch(false);
  base_match.origin = GURL("http://accounts.google.com/a/ServiceLoginAuth");
  base_match.action = GURL("http://accounts.google.com/a/ServiceLogin");

  PasswordForm psl_match = base_match;
  psl_match.is_public_suffix_match = true;

  // Change origin and action URLs to decrease the score.
  PasswordForm same_origin_match = base_match;
  psl_match.origin = GURL("http://accounts.google.com/a/ServiceLoginAuth2");
  psl_match.action = GURL("http://accounts.google.com/a/ServiceLogin2");

  autofill::PasswordFormFillData fill_data;
  EXPECT_CALL(*client()->mock_driver(), FillPasswordForm(_))
      .WillOnce(SaveArg<0>(&fill_data));

  fake_form_fetcher()->SetNonFederated({&psl_match, &same_origin_match}, 0u);
  EXPECT_TRUE(fill_data.additional_logins.empty());
  EXPECT_EQ(1u, form_manager()->best_matches().size());
  EXPECT_FALSE(
      form_manager()->best_matches().begin()->second->is_public_suffix_match);
}

TEST_F(PasswordFormManagerTest, AndroidCredentialsAreAutofilled) {
  EXPECT_CALL(*(client()->mock_driver()), AllowPasswordGenerationForForm(_));

  // Although Android-based credentials are treated similarly to PSL-matched
  // credentials in some respects, they should be autofilled as opposed to be
  // filled on username-select.
  PasswordForm android_login;
  android_login.signon_realm = "android://hash@com.google.android";
  android_login.origin = GURL("android://hash@com.google.android/");
  android_login.is_affiliation_based_match = true;
  android_login.username_value = saved_match()->username_value;
  android_login.password_value = saved_match()->password_value;
  android_login.preferred = false;
  android_login.times_used = 42;

  autofill::PasswordFormFillData fill_data;
  EXPECT_CALL(*client()->mock_driver(), FillPasswordForm(_))
      .WillOnce(SaveArg<0>(&fill_data));

  fake_form_fetcher()->SetNonFederated({&android_login}, 0u);
  EXPECT_TRUE(fill_data.additional_logins.empty());
  EXPECT_FALSE(fill_data.wait_for_username);
  EXPECT_EQ(1u, form_manager()->best_matches().size());

  // When the user submits the filled form, no copy of the credential should be
  // created, instead the usage counter of the original credential should be
  // incremented in-place, as if it were a regular credential for that website.
  PasswordForm credential(*observed_form());
  credential.username_value = android_login.username_value;
  credential.password_value = android_login.password_value;
  credential.preferred = true;
  form_manager()->ProvisionallySave(
      credential, PasswordFormManager::IGNORE_OTHER_POSSIBLE_USERNAMES);
  EXPECT_FALSE(form_manager()->IsNewLogin());

  PasswordForm updated_credential;
  EXPECT_CALL(MockFormSaver::Get(form_manager()), Update(_, _, _, nullptr))
      .WillOnce(testing::SaveArg<0>(&updated_credential));
  form_manager()->Save();

  EXPECT_EQ(android_login.username_value, updated_credential.username_value);
  EXPECT_EQ(android_login.password_value, updated_credential.password_value);
  EXPECT_EQ(android_login.times_used + 1, updated_credential.times_used);
  EXPECT_TRUE(updated_credential.preferred);
  EXPECT_EQ(GURL(), updated_credential.action);
  EXPECT_EQ(base::string16(), updated_credential.username_element);
  EXPECT_EQ(base::string16(), updated_credential.password_element);
  EXPECT_EQ(base::string16(), updated_credential.submit_element);
}

// Credentials saved through Android apps should always be shown in the drop-
// down menu, unless there is a better-scoring match with the same username.
TEST_F(PasswordFormManagerTest, AndroidCredentialsAreProtected) {
  const char kTestUsername1[] = "test-user@gmail.com";
  const char kTestUsername2[] = "test-other-user@gmail.com";
  const char kTestWebPassword[] = "web-password";
  const char kTestAndroidPassword1[] = "android-password-alpha";
  const char kTestAndroidPassword2[] = "android-password-beta";

  EXPECT_CALL(*(client()->mock_driver()), AllowPasswordGenerationForForm(_));

  // Suppose there is one login saved through the website, and two other coming
  // from Android: the first has the same username as the web-based credential,
  // so it should be suppressed, but the second has a different username, so it
  // should be shown.
  PasswordForm website_login = CreateSavedMatch(false);
  website_login.username_value = ASCIIToUTF16(kTestUsername1);
  website_login.password_value = ASCIIToUTF16(kTestWebPassword);

  PasswordForm android_same;
  android_same.signon_realm = "android://hash@com.google.android";
  android_same.origin = GURL("android://hash@com.google.android/");
  android_same.username_value = ASCIIToUTF16(kTestUsername1);
  android_same.password_value = ASCIIToUTF16(kTestAndroidPassword1);

  PasswordForm android_other = android_same;
  android_other.username_value = ASCIIToUTF16(kTestUsername2);
  android_other.password_value = ASCIIToUTF16(kTestAndroidPassword2);

  std::vector<std::unique_ptr<PasswordForm>> expected_matches;
  expected_matches.push_back(base::MakeUnique<PasswordForm>(website_login));
  expected_matches.push_back(base::MakeUnique<PasswordForm>(android_other));

  autofill::PasswordFormFillData fill_data;
  EXPECT_CALL(*client()->mock_driver(), FillPasswordForm(_))
      .WillOnce(SaveArg<0>(&fill_data));
  fake_form_fetcher()->SetNonFederated(
      {&website_login, &android_same, &android_other}, 0u);

  EXPECT_FALSE(fill_data.wait_for_username);
  EXPECT_EQ(1u, fill_data.additional_logins.size());

  std::vector<std::unique_ptr<PasswordForm>> actual_matches;
  for (const auto& username_match_pair : form_manager()->best_matches())
    actual_matches.push_back(
        base::MakeUnique<PasswordForm>(*username_match_pair.second));
  EXPECT_THAT(actual_matches,
              UnorderedPasswordFormElementsAre(&expected_matches));
}

TEST_F(PasswordFormManagerTest, InvalidActionURLsDoNotMatch) {
  PasswordForm invalid_action_form(*observed_form());
  invalid_action_form.action = GURL("http://");
  ASSERT_FALSE(invalid_action_form.action.is_valid());
  ASSERT_FALSE(invalid_action_form.action.is_empty());
  // Non-empty invalid action URLs should not match other actions.
  // First when the compared form has an invalid URL:
  EXPECT_EQ(0, form_manager()->DoesManage(invalid_action_form, nullptr) &
                   PasswordFormManager::RESULT_ACTION_MATCH);
  // Then when the observed form has an invalid URL:
  PasswordForm valid_action_form(*observed_form());
  PasswordFormManager invalid_manager(
      password_manager(), client(), client()->driver(), invalid_action_form,
      base::MakeUnique<MockFormSaver>(), fake_form_fetcher());
  EXPECT_EQ(0, invalid_manager.DoesManage(valid_action_form, nullptr) &
                   PasswordFormManager::RESULT_ACTION_MATCH);
}

TEST_F(PasswordFormManagerTest, EmptyActionURLsDoNotMatchNonEmpty) {
  PasswordForm empty_action_form(*observed_form());
  empty_action_form.action = GURL();
  ASSERT_FALSE(empty_action_form.action.is_valid());
  ASSERT_TRUE(empty_action_form.action.is_empty());
  // First when the compared form has an empty URL:
  EXPECT_EQ(0, form_manager()->DoesManage(empty_action_form, nullptr) &
                   PasswordFormManager::RESULT_ACTION_MATCH);
  // Then when the observed form has an empty URL:
  PasswordForm valid_action_form(*observed_form());
  PasswordFormManager empty_action_manager(
      password_manager(), client(), client()->driver(), empty_action_form,
      base::MakeUnique<MockFormSaver>(), fake_form_fetcher());
  EXPECT_EQ(0, empty_action_manager.DoesManage(valid_action_form, nullptr) &
                   PasswordFormManager::RESULT_ACTION_MATCH);
}

TEST_F(PasswordFormManagerTest, NonHTMLFormsDoNotMatchHTMLForms) {
  ASSERT_EQ(PasswordForm::SCHEME_HTML, observed_form()->scheme);
  PasswordForm non_html_form(*observed_form());
  non_html_form.scheme = PasswordForm::SCHEME_DIGEST;
  EXPECT_EQ(0, form_manager()->DoesManage(non_html_form, nullptr) &
                   PasswordFormManager::RESULT_HTML_ATTRIBUTES_MATCH);

  // The other way round: observing a non-HTML form, don't match a HTML form.
  PasswordForm html_form(*observed_form());
  PasswordFormManager non_html_manager(
      password_manager(), client(), kNoDriver, non_html_form,
      base::MakeUnique<MockFormSaver>(), fake_form_fetcher());
  EXPECT_EQ(0, non_html_manager.DoesManage(html_form, nullptr) &
                   PasswordFormManager::RESULT_HTML_ATTRIBUTES_MATCH);
}

TEST_F(PasswordFormManagerTest, OriginCheck_HostsMatchExactly) {
  // Host part of origins must match exactly, not just by prefix.
  PasswordForm form_longer_host(*observed_form());
  form_longer_host.origin = GURL("http://accounts.google.com.au/a/LoginAuth");
  // Check that accounts.google.com does not match accounts.google.com.au.
  EXPECT_EQ(0, form_manager()->DoesManage(form_longer_host, nullptr) &
                   PasswordFormManager::RESULT_HTML_ATTRIBUTES_MATCH);
}

TEST_F(PasswordFormManagerTest, OriginCheck_MoreSecureSchemePathsMatchPrefix) {
  // If the URL scheme of the observed form is HTTP, and the compared form is
  // HTTPS, then the compared form can extend the path.
  PasswordForm form_longer_path(*observed_form());
  form_longer_path.origin = GURL("https://accounts.google.com/a/LoginAuth/sec");
  EXPECT_NE(0, form_manager()->DoesManage(form_longer_path, nullptr) &
                   PasswordFormManager::RESULT_HTML_ATTRIBUTES_MATCH);
}

TEST_F(PasswordFormManagerTest,
       OriginCheck_NotMoreSecureSchemePathsMatchExactly) {
  // If the origin URL scheme of the compared form is not more secure than that
  // of the observed form, then the paths must match exactly.
  PasswordForm form_longer_path(*observed_form());
  form_longer_path.origin = GURL("http://accounts.google.com/a/LoginAuth/sec");
  // Check that /a/LoginAuth does not match /a/LoginAuth/more.
  EXPECT_EQ(0, form_manager()->DoesManage(form_longer_path, nullptr) &
                   PasswordFormManager::RESULT_HTML_ATTRIBUTES_MATCH);

  PasswordForm secure_observed_form(*observed_form());
  secure_observed_form.origin = GURL("https://accounts.google.com/a/LoginAuth");
  PasswordFormManager secure_manager(
      password_manager(), client(), client()->driver(), secure_observed_form,
      base::MakeUnique<MockFormSaver>(), fake_form_fetcher());
  // Also for HTTPS in the observed form, and HTTP in the compared form, an
  // exact path match is expected.
  EXPECT_EQ(0, secure_manager.DoesManage(form_longer_path, nullptr) &
                   PasswordFormManager::RESULT_HTML_ATTRIBUTES_MATCH);
  // Not even upgrade to HTTPS in the compared form should help.
  form_longer_path.origin = GURL("https://accounts.google.com/a/LoginAuth/sec");
  EXPECT_EQ(0, secure_manager.DoesManage(form_longer_path, nullptr) &
                   PasswordFormManager::RESULT_HTML_ATTRIBUTES_MATCH);
}

TEST_F(PasswordFormManagerTest, OriginCheck_OnlyOriginsMatch) {
  // Make sure DoesManage() can distinguish when only origins match.

  PasswordForm different_html_attributes(*observed_form());
  different_html_attributes.password_element = ASCIIToUTF16("random_pass");
  different_html_attributes.username_element = ASCIIToUTF16("random_user");

  EXPECT_EQ(0, form_manager()->DoesManage(different_html_attributes, nullptr) &
                   PasswordFormManager::RESULT_HTML_ATTRIBUTES_MATCH);

  EXPECT_EQ(PasswordFormManager::RESULT_ORIGINS_OR_FRAMES_MATCH,
            form_manager()->DoesManage(different_html_attributes, nullptr) &
                PasswordFormManager::RESULT_ORIGINS_OR_FRAMES_MATCH);
}

// Test that if multiple credentials with the same username are stored, and the
// user updates the password, then all of the stored passwords get updated as
// long as they have the same password value.
TEST_F(PasswordFormManagerTest, CorrectlyUpdatePasswordsWithSameUsername) {
  EXPECT_CALL(*client()->mock_driver(), AllowPasswordGenerationForForm(_));

  PasswordForm first(*saved_match());
  first.action = observed_form()->action;
  first.password_value = ASCIIToUTF16("first");
  first.preferred = true;

  // The second credential has the same password value, but it has a different
  // |username_element| to make a different unique key for the database
  // (otherwise the two credentials could not be stored at the same time). The
  // different unique key results in a slightly lower score than for |first|.
  PasswordForm second(first);
  second.username_element.clear();
  second.preferred = false;

  // The third credential has a different password value. It also has a
  // different |password_element| to make a different unique key for the
  // database again.
  PasswordForm third(first);
  third.password_element.clear();
  third.password_value = ASCIIToUTF16("second");
  third.preferred = false;

  fake_form_fetcher()->SetNonFederated({&first, &second, &third}, 0u);

  // |first| scored slightly higher.
  EXPECT_EQ(ASCIIToUTF16("first"),
            form_manager()->preferred_match()->password_value);

  PasswordForm login(*observed_form());
  login.username_value = saved_match()->username_value;
  login.password_value = ASCIIToUTF16("third");
  login.preferred = true;
  form_manager()->ProvisionallySave(
      login, PasswordFormManager::IGNORE_OTHER_POSSIBLE_USERNAMES);

  EXPECT_FALSE(form_manager()->IsNewLogin());

  PasswordForm saved_result;
  std::vector<PasswordForm> credentials_to_update;
  EXPECT_CALL(MockFormSaver::Get(form_manager()), Update(_, _, _, nullptr))
      .WillOnce(testing::DoAll(SaveArg<0>(&saved_result),
                               SaveArgPointee<2>(&credentials_to_update)));
  EXPECT_CALL(*client()->mock_driver()->mock_autofill_download_manager(),
              StartUploadRequest(_, false, _, _, true));

  form_manager()->Save();

  // What was |first| above should be the main credential updated.
  EXPECT_EQ(ASCIIToUTF16("third"), saved_result.password_value);
  EXPECT_FALSE(saved_result.password_element.empty());
  EXPECT_FALSE(saved_result.username_element.empty());

  // What was |second| above should be another credential updated.
  ASSERT_EQ(1u, credentials_to_update.size());
  EXPECT_EQ(ASCIIToUTF16("third"), credentials_to_update[0].password_value);
  EXPECT_FALSE(credentials_to_update[0].password_element.empty());
  EXPECT_TRUE(credentials_to_update[0].username_element.empty());
}

TEST_F(PasswordFormManagerTest, UploadFormData_NewPassword) {
  // For newly saved passwords, upload a password vote for autofill::PASSWORD.
  // Don't vote for the username field yet.
  FakeFormFetcher fetcher;
  fetcher.Fetch();
  PasswordFormManager form_manager(
      password_manager(), client(), client()->driver(), *saved_match(),
      base::MakeUnique<NiceMock<MockFormSaver>>(), &fetcher);
  fetcher.SetNonFederated(std::vector<const PasswordForm*>(), 0u);

  PasswordForm form_to_save(*saved_match());
  form_to_save.preferred = true;
  form_to_save.username_value = ASCIIToUTF16("username");
  form_to_save.password_value = ASCIIToUTF16("1234");

  autofill::ServerFieldTypeSet expected_available_field_types;
  expected_available_field_types.insert(autofill::PASSWORD);
  EXPECT_CALL(
      *client()->mock_driver()->mock_autofill_download_manager(),
      StartUploadRequest(_, false, expected_available_field_types, _, true));
  form_manager.ProvisionallySave(
      form_to_save, PasswordFormManager::IGNORE_OTHER_POSSIBLE_USERNAMES);
  form_manager.Save();
}

TEST_F(PasswordFormManagerTest, UploadFormData_NewPassword_Blacklist) {
  // Do not upload a vote if the user is blacklisting the form.
  FakeFormFetcher fetcher;
  fetcher.Fetch();
  PasswordFormManager blacklist_form_manager(
      password_manager(), client(), client()->driver(), *saved_match(),
      base::MakeUnique<NiceMock<MockFormSaver>>(), &fetcher);
  fetcher.SetNonFederated(std::vector<const PasswordForm*>(), 0u);

  autofill::ServerFieldTypeSet expected_available_field_types;
  expected_available_field_types.insert(autofill::USERNAME);
  expected_available_field_types.insert(autofill::PASSWORD);
  EXPECT_CALL(*client()->mock_driver()->mock_autofill_download_manager(),
              StartUploadRequest(_, _, expected_available_field_types, _, true))
      .Times(0);
  blacklist_form_manager.PermanentlyBlacklist();
}

TEST_F(PasswordFormManagerTest, UploadPasswordForm) {
  autofill::FormData observed_form_data;
  autofill::FormFieldData field;
  field.label = ASCIIToUTF16("Email");
  field.name = ASCIIToUTF16("observed-username-field");
  field.form_control_type = "text";
  observed_form_data.fields.push_back(field);

  field.label = ASCIIToUTF16("password");
  field.name = ASCIIToUTF16("password");
  field.form_control_type = "password";
  observed_form_data.fields.push_back(field);

  // Form data is different than saved form data, account creation signal should
  // be sent.
  autofill::ServerFieldType field_type = autofill::ACCOUNT_CREATION_PASSWORD;
  AccountCreationUploadTest(observed_form_data, 0, PasswordForm::NO_SIGNAL_SENT,
                            &field_type);

  // Non-zero times used will not upload since we only upload a positive signal
  // at most once.
  AccountCreationUploadTest(observed_form_data, 1, PasswordForm::NO_SIGNAL_SENT,
                            nullptr);

  // Same form data as saved match and POSITIVE_SIGNAL_SENT means there should
  // be a negative autofill ping sent.
  field_type = autofill::NOT_ACCOUNT_CREATION_PASSWORD;
  AccountCreationUploadTest(saved_match()->form_data, 2,
                            PasswordForm::POSITIVE_SIGNAL_SENT, &field_type);

  // For any other GenerationUplaodStatus, no autofill upload should occur
  // if the observed form data matches the saved form data.
  AccountCreationUploadTest(saved_match()->form_data, 3,
                            PasswordForm::NO_SIGNAL_SENT, nullptr);
  AccountCreationUploadTest(saved_match()->form_data, 3,
                            PasswordForm::NEGATIVE_SIGNAL_SENT, nullptr);
}

TEST_F(PasswordFormManagerTest, CorrectlySavePasswordWithoutUsernameFields) {
  EXPECT_CALL(*client()->mock_driver(), AllowPasswordGenerationForForm(_));

  fake_form_fetcher()->SetNonFederated(std::vector<const PasswordForm*>(), 0u);

  PasswordForm login(*observed_form());
  login.username_element.clear();
  login.password_value = ASCIIToUTF16("password");
  login.preferred = true;

  form_manager()->ProvisionallySave(
      login, PasswordFormManager::IGNORE_OTHER_POSSIBLE_USERNAMES);

  EXPECT_TRUE(form_manager()->IsNewLogin());

  PasswordForm saved_result;
  EXPECT_CALL(MockFormSaver::Get(form_manager()), Save(_, _, nullptr))
      .WillOnce(SaveArg<0>(&saved_result));

  form_manager()->Save();

  // Make sure that the password is updated appropriately.
  EXPECT_EQ(ASCIIToUTF16("password"), saved_result.password_value);
}

TEST_F(PasswordFormManagerTest, DriverDeletedBeforeStoreDone) {
  // Test graceful handling of the following situation:
  // 1. A form appears in a frame, a PFM is created for that form.
  // 2. The PFM asks the store for credentials for this form.
  // 3. The frame (and associated driver) gets deleted.
  // 4. The PFM returns the callback with credentials.
  // This test checks implicitly that after step 4 the PFM does not attempt
  // use-after-free of the deleted driver.
  std::string example_url("http://example.com");
  PasswordForm form;
  form.origin = GURL(example_url);
  form.signon_realm = example_url;
  form.action = GURL(example_url);
  form.username_element = ASCIIToUTF16("u");
  form.password_element = ASCIIToUTF16("p");
  form.submit_element = ASCIIToUTF16("s");

  FakeFormFetcher fetcher;
  fetcher.Fetch();
  PasswordFormManager form_manager(password_manager(), client(),
                                   client()->driver(), form,
                                   base::MakeUnique<MockFormSaver>(), &fetcher);

  // Suddenly, the frame and its driver disappear.
  client()->KillDriver();

  fetcher.SetNonFederated({&form}, 0u);
}

TEST_F(PasswordFormManagerTest, PreferredMatchIsUpToDate) {
  // Check that preferred_match() is always a member of best_matches().
  PasswordForm form = *observed_form();
  form.username_value = ASCIIToUTF16("username");
  form.password_value = ASCIIToUTF16("password1");
  form.preferred = false;

  PasswordForm generated_form = form;
  generated_form.type = PasswordForm::TYPE_GENERATED;
  generated_form.password_value = ASCIIToUTF16("password2");
  generated_form.preferred = true;

  fake_form_fetcher()->SetNonFederated({&form, &generated_form}, 0u);

  EXPECT_EQ(1u, form_manager()->best_matches().size());
  EXPECT_EQ(form_manager()->preferred_match(),
            form_manager()->best_matches().begin()->second);
  // Make sure to access all fields of preferred_match; this way if it was
  // deleted, ASAN might notice it.
  PasswordForm dummy(*form_manager()->preferred_match());
}

TEST_F(PasswordFormManagerTest, PasswordToSave_NoElements) {
  PasswordForm form;
  EXPECT_TRUE(PasswordFormManager::PasswordToSave(form).empty());
}

TEST_F(PasswordFormManagerTest, PasswordToSave_NoNewElement) {
  PasswordForm form;
  form.password_element = base::ASCIIToUTF16("pwd");
  base::string16 kValue = base::ASCIIToUTF16("val");
  form.password_value = kValue;
  EXPECT_EQ(kValue, PasswordFormManager::PasswordToSave(form));
}

TEST_F(PasswordFormManagerTest, PasswordToSave_NoOldElement) {
  PasswordForm form;
  form.new_password_element = base::ASCIIToUTF16("new_pwd");
  base::string16 kNewValue = base::ASCIIToUTF16("new_val");
  form.new_password_value = kNewValue;
  EXPECT_EQ(kNewValue, PasswordFormManager::PasswordToSave(form));
}

TEST_F(PasswordFormManagerTest, PasswordToSave_BothButNoNewValue) {
  PasswordForm form;
  form.password_element = base::ASCIIToUTF16("pwd");
  form.new_password_element = base::ASCIIToUTF16("new_pwd");
  base::string16 kValue = base::ASCIIToUTF16("val");
  form.password_value = kValue;
  EXPECT_EQ(kValue, PasswordFormManager::PasswordToSave(form));
}

TEST_F(PasswordFormManagerTest, PasswordToSave_NewValue) {
  PasswordForm form;
  form.password_element = base::ASCIIToUTF16("pwd");
  form.new_password_element = base::ASCIIToUTF16("new_pwd");
  form.password_value = base::ASCIIToUTF16("val");
  base::string16 kNewValue = base::ASCIIToUTF16("new_val");
  form.new_password_value = kNewValue;
  EXPECT_EQ(kNewValue, PasswordFormManager::PasswordToSave(form));
}

TEST_F(PasswordFormManagerTest, TestSuggestingPasswordChangeForms) {
  // Suggesting password on the password change form on the previously visited
  // site. Credentials are found in the password store, and are not blacklisted.
  PasswordForm observed_change_password_form = *observed_form();
  observed_change_password_form.new_password_element =
      base::ASCIIToUTF16("new_pwd");
  FakeFormFetcher fetcher;
  fetcher.Fetch();
  PasswordFormManager manager_creds(
      password_manager(), client(), client()->driver(),
      observed_change_password_form, base::MakeUnique<MockFormSaver>(),
      &fetcher);

  autofill::PasswordFormFillData fill_data;
  EXPECT_CALL(*client()->mock_driver(), FillPasswordForm(_))
      .WillOnce(SaveArg<0>(&fill_data));

  PasswordForm result = CreateSavedMatch(false);
  fetcher.SetNonFederated({&result}, 0u);
  EXPECT_EQ(1u, manager_creds.best_matches().size());
  EXPECT_EQ(0u, fill_data.additional_logins.size());
  EXPECT_TRUE(fill_data.wait_for_username);
}

TEST_F(PasswordFormManagerTest, TestUpdateMethod) {
  // Add a new password field to the test form. The PasswordFormManager should
  // save the password from this field, instead of the current password field.
  observed_form()->new_password_element = ASCIIToUTF16("NewPasswd");
  autofill::FormFieldData field;
  field.label = ASCIIToUTF16("NewPasswd");
  field.name = ASCIIToUTF16("NewPasswd");
  field.form_control_type = "password";
  observed_form()->form_data.fields.push_back(field);

  // Given that |observed_form| was most likely a change password form, it
  // should not serve as a source for updating meta-information stored with the
  // old credentials, such as element names, as they are likely going to be
  // different between change password and login forms. To test this in depth,
  // forcibly wipe |submit_element|, which should normally trigger updating
  // this field from |observed_form| during updating as a special case. We will
  // verify in the end that this did not happen.
  saved_match()->submit_element.clear();

  FakeFormFetcher fetcher;
  fetcher.Fetch();
  PasswordFormManager form_manager(password_manager(), client(),
                                   client()->driver(), *observed_form(),
                                   base::MakeUnique<MockFormSaver>(), &fetcher);

  fetcher.SetNonFederated({saved_match()}, 0u);

  // User submits current and new credentials to the observed form.
  PasswordForm credentials(*observed_form());
  credentials.username_element.clear();
  credentials.password_value = saved_match()->password_value;
  credentials.new_password_value = ASCIIToUTF16("test2");
  credentials.preferred = true;
  form_manager.ProvisionallySave(
      credentials, PasswordFormManager::IGNORE_OTHER_POSSIBLE_USERNAMES);

  // Successful login. The PasswordManager would instruct PasswordFormManager
  // to save, and since this is an update, it should know not to save as a new
  // login.
  EXPECT_FALSE(form_manager.IsNewLogin());
  EXPECT_FALSE(
      form_manager.is_possible_change_password_form_without_username());

  // By now, the PasswordFormManager should have promoted the new password value
  // already to be the current password, and should no longer maintain any info
  // about the new password value.
  EXPECT_EQ(credentials.new_password_value,
            form_manager.pending_credentials().password_value);
  EXPECT_TRUE(form_manager.pending_credentials().new_password_value.empty());

  // Trigger saving to exercise some special case handling during updating.
  PasswordForm new_credentials;
  EXPECT_CALL(MockFormSaver::Get(&form_manager), Update(_, _, _, nullptr))
      .WillOnce(SaveArg<0>(&new_credentials));

  form_manager.Update(*saved_match());

  // No meta-information should be updated, only the password.
  EXPECT_EQ(credentials.new_password_value, new_credentials.password_value);
  EXPECT_EQ(saved_match()->username_element, new_credentials.username_element);
  EXPECT_EQ(saved_match()->password_element, new_credentials.password_element);
  EXPECT_EQ(saved_match()->submit_element, new_credentials.submit_element);
}

TEST_F(PasswordFormManagerTest, TestUpdateNoUsernameTextfieldPresent) {
  // Add a new password field to the test form and insert a |username_value|
  // unlikely to be a real username. The PasswordFormManager should still save
  // the password from this field, instead of the current password field.
  observed_form()->new_password_element = ASCIIToUTF16("NewPasswd");
  autofill::FormFieldData field;
  field.label = ASCIIToUTF16("NewPasswd");
  field.name = ASCIIToUTF16("NewPasswd");
  field.form_control_type = "password";
  observed_form()->form_data.fields.push_back(field);

  // Given that |observed_form| was most likely a change password form, it
  // should not serve as a source for updating meta-information stored with the
  // old credentials, such as element names, as they are likely going to be
  // different between change password and login forms. To test this in depth,
  // forcibly wipe |submit_element|, which should normally trigger updating this
  // field from |observed_form| during updating as a special case. We
  // will verify in the end that this did not happen.
  saved_match()->submit_element.clear();

  FakeFormFetcher fetcher;
  fetcher.Fetch();
  PasswordFormManager form_manager(password_manager(), client(),
                                   client()->driver(), *observed_form(),
                                   base::MakeUnique<MockFormSaver>(), &fetcher);

  fetcher.SetNonFederated({saved_match()}, 0u);

  // User submits current and new credentials to the observed form.
  PasswordForm credentials(*observed_form());
  // The |username_value| contains a text that's unlikely to be real username.
  credentials.username_value = ASCIIToUTF16("3");
  credentials.password_value = saved_match()->password_value;
  credentials.new_password_value = ASCIIToUTF16("test2");
  credentials.preferred = true;
  form_manager.ProvisionallySave(
      credentials, PasswordFormManager::IGNORE_OTHER_POSSIBLE_USERNAMES);

  // Successful login. The PasswordManager would instruct PasswordFormManager
  // to save, and since this is an update, it should know not to save as a new
  // login.
  EXPECT_FALSE(form_manager.IsNewLogin());
  EXPECT_TRUE(form_manager.is_possible_change_password_form_without_username());

  // By now, the PasswordFormManager should have promoted the new password value
  // already to be the current password, and should no longer maintain any info
  // about the new password value.
  EXPECT_EQ(saved_match()->username_value,
            form_manager.pending_credentials().username_value);
  EXPECT_EQ(credentials.new_password_value,
            form_manager.pending_credentials().password_value);
  EXPECT_TRUE(form_manager.pending_credentials().new_password_value.empty());

  // Trigger saving to exercise some special case handling during updating.
  PasswordForm new_credentials;
  EXPECT_CALL(MockFormSaver::Get(&form_manager), Update(_, _, _, nullptr))
      .WillOnce(SaveArg<0>(&new_credentials));

  form_manager.Update(form_manager.pending_credentials());

  // No other information than password value should be updated. In particular
  // not the username.
  EXPECT_EQ(saved_match()->username_value, new_credentials.username_value);
  EXPECT_EQ(credentials.new_password_value, new_credentials.password_value);
  EXPECT_EQ(saved_match()->username_element, new_credentials.username_element);
  EXPECT_EQ(saved_match()->password_element, new_credentials.password_element);
  EXPECT_EQ(saved_match()->submit_element, new_credentials.submit_element);
}

// Test that if WipeStoreCopyIfOutdated is called before password store
// callback, the UMA is signalled accordingly.
TEST_F(PasswordFormManagerTest, WipeStoreCopyIfOutdated_BeforeStoreCallback) {
  PasswordForm form(*saved_match());
  form.password_value = ASCIIToUTF16("nonempty-password");

  FakeFormFetcher fetcher;
  fetcher.Fetch();
  PasswordFormManager form_manager(password_manager(), client(),
                                   client()->driver(), form,
                                   base::MakeUnique<MockFormSaver>(), &fetcher);
  // The creation of |fetcher| keeps it waiting for store results. This test
  // keeps the fetcher waiting on purpose.

  PasswordForm submitted_form(form);
  submitted_form.password_value += ASCIIToUTF16("add stuff, make it different");
  form_manager.ProvisionallySave(
      submitted_form, PasswordFormManager::IGNORE_OTHER_POSSIBLE_USERNAMES);

  base::HistogramTester histogram_tester;
  EXPECT_CALL(MockFormSaver::Get(&form_manager),
              WipeOutdatedCopies(form_manager.pending_credentials(), _, _));
  form_manager.WipeStoreCopyIfOutdated();
  histogram_tester.ExpectUniqueSample("PasswordManager.StoreReadyWhenWiping", 0,
                                      1);
}

TEST_F(PasswordFormManagerTest, GenerationStatusChangedWithPassword) {
  PasswordForm generated_form = *observed_form();
  generated_form.type = PasswordForm::TYPE_GENERATED;
  generated_form.username_value = ASCIIToUTF16("username");
  generated_form.password_value = ASCIIToUTF16("password2");
  generated_form.preferred = true;

  PasswordForm submitted_form(generated_form);
  submitted_form.password_value = ASCIIToUTF16("password3");

  fake_form_fetcher()->SetNonFederated({&generated_form}, 0u);

  form_manager()->ProvisionallySave(
      submitted_form, PasswordFormManager::IGNORE_OTHER_POSSIBLE_USERNAMES);

  PasswordForm new_credentials;
  EXPECT_CALL(MockFormSaver::Get(form_manager()), Update(_, _, _, nullptr))
      .WillOnce(testing::SaveArg<0>(&new_credentials));
  form_manager()->Save();

  EXPECT_EQ(PasswordForm::TYPE_MANUAL, new_credentials.type);
}

TEST_F(PasswordFormManagerTest, GenerationStatusNotUpdatedIfPasswordUnchanged) {
  base::HistogramTester histogram_tester;

  PasswordForm generated_form = *observed_form();
  generated_form.type = PasswordForm::TYPE_GENERATED;
  generated_form.username_value = ASCIIToUTF16("username");
  generated_form.password_value = ASCIIToUTF16("password2");
  generated_form.preferred = true;

  PasswordForm submitted_form(generated_form);

  fake_form_fetcher()->SetNonFederated({&generated_form}, 0u);

  form_manager()->ProvisionallySave(
      submitted_form, PasswordFormManager::IGNORE_OTHER_POSSIBLE_USERNAMES);

  PasswordForm new_credentials;
  EXPECT_CALL(MockFormSaver::Get(form_manager()), Update(_, _, _, nullptr))
      .WillOnce(testing::SaveArg<0>(&new_credentials));
  form_manager()->Save();

  EXPECT_EQ(PasswordForm::TYPE_GENERATED, new_credentials.type);
  histogram_tester.ExpectBucketCount("PasswordGeneration.SubmissionEvent",
                                     metrics_util::PASSWORD_USED, 1);
}

// Test that ProcessFrame is called on receiving matches from the fetcher,
// resulting in a FillPasswordForm call.
TEST_F(PasswordFormManagerTest, ProcessFrame) {
  EXPECT_CALL(*client()->mock_driver(), FillPasswordForm(_));
  fake_form_fetcher()->SetNonFederated({saved_match()}, 0u);
}

// Test that ProcessFrame can also be called directly, resulting in an
// additional FillPasswordForm call.
TEST_F(PasswordFormManagerTest, ProcessFrame_MoreProcessFrameMoreFill) {
  EXPECT_CALL(*client()->mock_driver(), FillPasswordForm(_)).Times(2);
  fake_form_fetcher()->SetNonFederated({saved_match()}, 0u);
  form_manager()->ProcessFrame(client()->mock_driver()->AsWeakPtr());
}

// Test that when ProcessFrame is called on a driver added after receiving
// matches, such driver is still told to call FillPasswordForm.
TEST_F(PasswordFormManagerTest, ProcessFrame_TwoDrivers) {
  NiceMock<MockPasswordManagerDriver> second_driver;

  EXPECT_CALL(*client()->mock_driver(), FillPasswordForm(_));
  EXPECT_CALL(second_driver, FillPasswordForm(_));
  fake_form_fetcher()->SetNonFederated({saved_match()}, 0u);
  form_manager()->ProcessFrame(second_driver.AsWeakPtr());
}

// Test that a second driver is added before receiving matches, such driver is
// also told to call FillPasswordForm once the matches arrive.
TEST_F(PasswordFormManagerTest, ProcessFrame_DriverBeforeMatching) {
  NiceMock<MockPasswordManagerDriver> second_driver;
  form_manager()->ProcessFrame(second_driver.AsWeakPtr());

  EXPECT_CALL(*client()->mock_driver(), FillPasswordForm(_));
  EXPECT_CALL(second_driver, FillPasswordForm(_));
  fake_form_fetcher()->SetNonFederated({saved_match()}, 0u);
}

// Test that if the fetcher updates the information about stored matches,
// autofill is re-triggered.
TEST_F(PasswordFormManagerTest, ProcessFrame_StoreUpdatesCausesAutofill) {
  EXPECT_CALL(*client()->mock_driver(), FillPasswordForm(_)).Times(2);
  std::vector<const PasswordForm*> matches = {saved_match()};
  fake_form_fetcher()->SetNonFederated(matches, 0u);
  fake_form_fetcher()->Fetch();
  fake_form_fetcher()->SetNonFederated(matches, 0u);
}

// TODO(crbug.com/639786): Restore the following test:
// when PasswordFormManager::Save is called, then PasswordFormManager also
// calls PasswordManager::UpdateFormManagers.

TEST_F(PasswordFormManagerTest, UploadChangePasswordForm) {
  autofill::ServerFieldType kChangePasswordVotes[] = {
      autofill::NEW_PASSWORD, autofill::PROBABLY_NEW_PASSWORD,
      autofill::NOT_NEW_PASSWORD};
  bool kFalseTrue[] = {false, true};
  for (autofill::ServerFieldType vote : kChangePasswordVotes) {
    for (bool has_confirmation_field : kFalseTrue)
      ChangePasswordUploadTest(vote, has_confirmation_field);
  }
}

TEST_F(PasswordFormManagerTest, TestUpdatePSLMatchedCredentials) {
  FakeFormFetcher fetcher;
  fetcher.Fetch();
  PasswordFormManager form_manager(password_manager(), client(),
                                   client()->driver(), *observed_form(),
                                   base::MakeUnique<MockFormSaver>(), &fetcher);
  fetcher.SetNonFederated({saved_match(), psl_saved_match()}, 0u);

  // User submits a credentials with an old username and a new password.
  PasswordForm credentials(*observed_form());
  credentials.username_value = saved_match()->username_value;
  credentials.password_value = ASCIIToUTF16("new_password");
  credentials.preferred = true;
  form_manager.ProvisionallySave(
      credentials, PasswordFormManager::IGNORE_OTHER_POSSIBLE_USERNAMES);

  // Successful login. The PasswordManager would instruct PasswordFormManager
  // to save, and since this is an update, it should know not to save as a new
  // login.
  EXPECT_FALSE(form_manager.IsNewLogin());

  // Trigger saving to exercise some special case handling during updating.
  PasswordForm new_credentials;
  std::vector<autofill::PasswordForm> credentials_to_update;
  EXPECT_CALL(MockFormSaver::Get(&form_manager), Update(_, _, _, nullptr))
      .WillOnce(testing::DoAll(SaveArg<0>(&new_credentials),
                               SaveArgPointee<2>(&credentials_to_update)));
  EXPECT_CALL(*client()->mock_driver()->mock_autofill_download_manager(),
              StartUploadRequest(_, false, _, _, true));

  form_manager.Save();

  // No meta-information should be updated, only the password.
  EXPECT_EQ(credentials.password_value, new_credentials.password_value);
  EXPECT_EQ(saved_match()->username_value, new_credentials.username_value);
  EXPECT_EQ(saved_match()->username_element, new_credentials.username_element);
  EXPECT_EQ(saved_match()->password_element, new_credentials.password_element);
  EXPECT_EQ(saved_match()->origin, new_credentials.origin);

  ASSERT_EQ(1u, credentials_to_update.size());
  EXPECT_EQ(credentials.password_value,
            credentials_to_update[0].password_value);
  EXPECT_EQ(psl_saved_match()->username_element,
            credentials_to_update[0].username_element);
  EXPECT_EQ(psl_saved_match()->username_element,
            credentials_to_update[0].username_element);
  EXPECT_EQ(psl_saved_match()->password_element,
            credentials_to_update[0].password_element);
  EXPECT_EQ(psl_saved_match()->origin, credentials_to_update[0].origin);
}

TEST_F(PasswordFormManagerTest,
       TestNotUpdatePSLMatchedCredentialsWithAnotherUsername) {
  FakeFormFetcher fetcher;
  fetcher.Fetch();
  PasswordFormManager form_manager(password_manager(), client(),
                                   client()->driver(), *observed_form(),
                                   base::MakeUnique<MockFormSaver>(), &fetcher);
  psl_saved_match()->username_value += ASCIIToUTF16("1");
  fetcher.SetNonFederated({saved_match(), psl_saved_match()}, 0u);

  // User submits a credentials with an old username and a new password.
  PasswordForm credentials(*observed_form());
  credentials.username_value = saved_match()->username_value;
  credentials.password_value = ASCIIToUTF16("new_password");
  credentials.preferred = true;
  form_manager.ProvisionallySave(
      credentials, PasswordFormManager::IGNORE_OTHER_POSSIBLE_USERNAMES);

  // Successful login. The PasswordManager would instruct PasswordFormManager
  // to save, and since this is an update, it should know not to save as a new
  // login.
  EXPECT_FALSE(form_manager.IsNewLogin());

  // Trigger saving to exercise some special case handling during updating.
  PasswordForm new_credentials;
  EXPECT_CALL(MockFormSaver::Get(&form_manager),
              Update(_, _, Pointee(IsEmpty()), nullptr))
      .WillOnce(testing::SaveArg<0>(&new_credentials));
  EXPECT_CALL(*client()->mock_driver()->mock_autofill_download_manager(),
              StartUploadRequest(_, false, _, _, true));

  form_manager.Save();

  // No meta-information should be updated, only the password.
  EXPECT_EQ(credentials.password_value, new_credentials.password_value);
  EXPECT_EQ(saved_match()->username_value, new_credentials.username_value);
  EXPECT_EQ(saved_match()->password_element, new_credentials.password_element);
  EXPECT_EQ(saved_match()->username_element, new_credentials.username_element);
  EXPECT_EQ(saved_match()->origin, new_credentials.origin);
}

TEST_F(PasswordFormManagerTest,
       TestNotUpdatePSLMatchedCredentialsWithAnotherPassword) {
  FakeFormFetcher fetcher;
  fetcher.Fetch();
  PasswordFormManager form_manager(password_manager(), client(),
                                   client()->driver(), *observed_form(),
                                   base::MakeUnique<MockFormSaver>(), &fetcher);
  psl_saved_match()->password_value += ASCIIToUTF16("1");
  fetcher.SetNonFederated({saved_match(), psl_saved_match()}, 0u);

  // User submits a credentials with an old username and a new password.
  PasswordForm credentials(*observed_form());
  credentials.username_value = saved_match()->username_value;
  credentials.password_value = ASCIIToUTF16("new_password");
  credentials.preferred = true;
  form_manager.ProvisionallySave(
      credentials, PasswordFormManager::IGNORE_OTHER_POSSIBLE_USERNAMES);

  // Successful login. The PasswordManager would instruct PasswordFormManager
  // to save, and since this is an update, it should know not to save as a new
  // login.
  EXPECT_FALSE(form_manager.IsNewLogin());

  // Trigger saving to exercise some special case handling during updating.
  PasswordForm new_credentials;
  EXPECT_CALL(MockFormSaver::Get(&form_manager),
              Update(_, _, Pointee(IsEmpty()), nullptr))
      .WillOnce(testing::SaveArg<0>(&new_credentials));
  EXPECT_CALL(*client()->mock_driver()->mock_autofill_download_manager(),
              StartUploadRequest(_, false, _, _, true));

  form_manager.Save();

  // No meta-information should be updated, only the password.
  EXPECT_EQ(credentials.password_value, new_credentials.password_value);
  EXPECT_EQ(saved_match()->username_value, new_credentials.username_value);
  EXPECT_EQ(saved_match()->password_element, new_credentials.password_element);
  EXPECT_EQ(saved_match()->username_element, new_credentials.username_element);
  EXPECT_EQ(saved_match()->origin, new_credentials.origin);
}

TEST_F(PasswordFormManagerTest, TestNotUpdateWhenOnlyPSLMatched) {
  FakeFormFetcher fetcher;
  fetcher.Fetch();
  PasswordFormManager form_manager(password_manager(), client(),
                                   client()->driver(), *observed_form(),
                                   base::MakeUnique<MockFormSaver>(), &fetcher);
  fetcher.SetNonFederated({psl_saved_match()}, 0u);

  // User submits a credentials with an old username and a new password.
  PasswordForm credentials(*observed_form());
  credentials.username_value = saved_match()->username_value;
  credentials.password_value = ASCIIToUTF16("new_password");
  credentials.preferred = true;
  form_manager.ProvisionallySave(
      credentials, PasswordFormManager::IGNORE_OTHER_POSSIBLE_USERNAMES);

  EXPECT_TRUE(form_manager.IsNewLogin());

  // PSL matched credential should not be updated, since we are not sure that
  // this is the same credential as submitted one.
  PasswordForm new_credentials;
  EXPECT_CALL(MockFormSaver::Get(&form_manager), Save(_, _, nullptr))
      .WillOnce(testing::SaveArg<0>(&new_credentials));

  form_manager.Save();

  EXPECT_EQ(credentials.password_value, new_credentials.password_value);
  EXPECT_EQ(credentials.username_value, new_credentials.username_value);
  EXPECT_EQ(credentials.password_element, new_credentials.password_element);
  EXPECT_EQ(credentials.username_element, new_credentials.username_element);
  EXPECT_EQ(credentials.origin, new_credentials.origin);
}

TEST_F(PasswordFormManagerTest,
       TestSavingOnChangePasswordFormGenerationNoStoredForms) {
  fake_form_fetcher()->SetNonFederated(std::vector<const PasswordForm*>(), 0u);
  form_manager()->set_has_generated_password(true);

  // User submits change password form and there is no stored credentials.
  PasswordForm credentials = *observed_form();
  credentials.username_element.clear();
  credentials.password_value = saved_match()->password_value;
  credentials.new_password_element = ASCIIToUTF16("NewPasswd");
  credentials.new_password_value = ASCIIToUTF16("new_password");
  credentials.preferred = true;
  form_manager()->ProvisionallySave(
      credentials, PasswordFormManager::IGNORE_OTHER_POSSIBLE_USERNAMES);

  // Successful login. The PasswordManager would instruct PasswordFormManager
  // to save, which should know this is a new login.
  EXPECT_TRUE(form_manager()->IsNewLogin());
  // Make sure the credentials that would be submitted on successful login
  // are going to match submitted form.
  EXPECT_EQ(observed_form()->origin.spec(),
            form_manager()->pending_credentials().origin.spec());
  EXPECT_EQ(observed_form()->signon_realm,
            form_manager()->pending_credentials().signon_realm);
  EXPECT_EQ(observed_form()->action,
            form_manager()->pending_credentials().action);
  EXPECT_TRUE(form_manager()->pending_credentials().preferred);
  EXPECT_EQ(ASCIIToUTF16("new_password"),
            form_manager()->pending_credentials().password_value);
  EXPECT_EQ(base::string16(),
            form_manager()->pending_credentials().username_value);
  EXPECT_TRUE(
      form_manager()->pending_credentials().new_password_element.empty());
  EXPECT_TRUE(form_manager()->pending_credentials().new_password_value.empty());
}

TEST_F(PasswordFormManagerTest, TestUpdatingOnChangePasswordFormGeneration) {
  fake_form_fetcher()->SetNonFederated({saved_match()}, 0u);
  form_manager()->set_has_generated_password(true);

  // User submits credentials for the change password form, and old password is
  // coincide with password from an existing credentials, so stored credentials
  // should be updated.
  PasswordForm credentials = *observed_form();
  credentials.username_element.clear();
  credentials.password_value = saved_match()->password_value;
  credentials.new_password_element = ASCIIToUTF16("NewPasswd");
  credentials.new_password_value = ASCIIToUTF16("new_password");
  credentials.preferred = true;
  form_manager()->ProvisionallySave(
      credentials, PasswordFormManager::IGNORE_OTHER_POSSIBLE_USERNAMES);

  EXPECT_FALSE(form_manager()->IsNewLogin());
  // Make sure the credentials that would be submitted on successful login
  // are going to match the stored entry in the db.
  EXPECT_EQ(saved_match()->origin.spec(),
            form_manager()->pending_credentials().origin.spec());
  EXPECT_EQ(saved_match()->signon_realm,
            form_manager()->pending_credentials().signon_realm);
  EXPECT_EQ(observed_form()->action,
            form_manager()->pending_credentials().action);
  EXPECT_TRUE(form_manager()->pending_credentials().preferred);
  EXPECT_EQ(ASCIIToUTF16("new_password"),
            form_manager()->pending_credentials().password_value);
  EXPECT_EQ(saved_match()->username_value,
            form_manager()->pending_credentials().username_value);
  EXPECT_TRUE(
      form_manager()->pending_credentials().new_password_element.empty());
  EXPECT_TRUE(form_manager()->pending_credentials().new_password_value.empty());
}

TEST_F(PasswordFormManagerTest,
       TestSavingOnChangePasswordFormGenerationNoMatchedForms) {
  fake_form_fetcher()->SetNonFederated({saved_match()}, 0u);
  form_manager()->set_has_generated_password(true);

  // User submits credentials for the change password form, and old password is
  // not coincide with password from existing credentials, so new credentials
  // should be saved.
  PasswordForm credentials = *observed_form();
  credentials.username_element.clear();
  credentials.password_value =
      saved_match()->password_value + ASCIIToUTF16("1");
  credentials.new_password_element = ASCIIToUTF16("NewPasswd");
  credentials.new_password_value = ASCIIToUTF16("new_password");
  credentials.preferred = true;
  form_manager()->ProvisionallySave(
      credentials, PasswordFormManager::IGNORE_OTHER_POSSIBLE_USERNAMES);

  EXPECT_TRUE(form_manager()->IsNewLogin());
  // Make sure the credentials that would be submitted on successful login
  // are going to match submitted form.
  EXPECT_EQ(observed_form()->origin.spec(),
            form_manager()->pending_credentials().origin.spec());
  EXPECT_EQ(observed_form()->signon_realm,
            form_manager()->pending_credentials().signon_realm);
  EXPECT_EQ(observed_form()->action,
            form_manager()->pending_credentials().action);
  EXPECT_TRUE(form_manager()->pending_credentials().preferred);
  EXPECT_EQ(ASCIIToUTF16("new_password"),
            form_manager()->pending_credentials().password_value);
  EXPECT_EQ(base::string16(),
            form_manager()->pending_credentials().username_value);
  EXPECT_TRUE(
      form_manager()->pending_credentials().new_password_element.empty());
  EXPECT_TRUE(form_manager()->pending_credentials().new_password_value.empty());
}

TEST_F(PasswordFormManagerTest,
       TestUploadVotesForPasswordChangeFormsWithTwoFields) {
  // Turn |observed_form_| into change password form with only 2 fields: an old
  // password and a new password.
  observed_form()->username_element.clear();
  observed_form()->new_password_element = ASCIIToUTF16("NewPasswd");
  autofill::FormFieldData field;
  field.label = ASCIIToUTF16("password");
  field.name = ASCIIToUTF16("Passwd");
  field.form_control_type = "password";
  observed_form()->form_data.fields.push_back(field);

  field.label = ASCIIToUTF16("new password");
  field.name = ASCIIToUTF16("NewPasswd");
  observed_form()->form_data.fields.push_back(field);

  FakeFormFetcher fetcher;
  fetcher.Fetch();
  PasswordFormManager form_manager(password_manager(), client(),
                                   client()->driver(), *observed_form(),
                                   base::MakeUnique<MockFormSaver>(), &fetcher);
  fetcher.SetNonFederated({saved_match()}, 0u);

  // User submits current and new credentials to the observed form.
  PasswordForm submitted_form(*observed_form());
  submitted_form.password_value = saved_match()->password_value;
  submitted_form.new_password_value = ASCIIToUTF16("test2");
  submitted_form.preferred = true;
  form_manager.ProvisionallySave(
      submitted_form, PasswordFormManager::IGNORE_OTHER_POSSIBLE_USERNAMES);

  // Successful login. The PasswordManager would instruct PasswordFormManager
  // to update.
  EXPECT_FALSE(form_manager.IsNewLogin());
  EXPECT_TRUE(form_manager.is_possible_change_password_form_without_username());

  // By now, the PasswordFormManager should have promoted the new password
  // value already to be the current password, and should no longer maintain
  // any info about the new password value.
  EXPECT_EQ(submitted_form.new_password_value,
            form_manager.pending_credentials().password_value);
  EXPECT_TRUE(form_manager.pending_credentials().new_password_value.empty());

  std::map<base::string16, autofill::ServerFieldType> expected_types;
  expected_types[observed_form()->password_element] = autofill::PASSWORD;
  expected_types[observed_form()->new_password_element] =
      autofill::NEW_PASSWORD;

  autofill::ServerFieldTypeSet expected_available_field_types;
  expected_available_field_types.insert(autofill::PASSWORD);
  expected_available_field_types.insert(autofill::NEW_PASSWORD);

  std::string observed_form_signature =
      autofill::FormStructure(observed_form()->form_data).FormSignatureAsStr();

  std::string expected_login_signature =
      autofill::FormStructure(saved_match()->form_data).FormSignatureAsStr();

  EXPECT_CALL(*client()->mock_driver()->mock_autofill_download_manager(),
              StartUploadRequest(CheckUploadedAutofillTypesAndSignature(
                                     observed_form_signature, expected_types,
                                     false /* expect_generation_vote */),
                                 false, expected_available_field_types,
                                 expected_login_signature, true));

  form_manager.Update(*saved_match());
}

// Checks uploading a vote about the usage of the password generation popup.
TEST_F(PasswordFormManagerTest, GeneratedVoteUpload) {
  bool kFalseTrue[] = {false, true};
  SavePromptInteraction kSavePromptInterations[] = {SAVE, NEVER,
                                                    NO_INTERACTION};
  for (bool is_manual_generation : kFalseTrue) {
    for (bool is_change_password_form : kFalseTrue) {
      for (bool has_generated_password : kFalseTrue) {
        for (SavePromptInteraction interaction : kSavePromptInterations) {
          GeneratedVoteUploadTest(is_manual_generation, is_change_password_form,
                                  has_generated_password, interaction);
        }
      }
    }
  }
}

TEST_F(PasswordFormManagerTest, FormClassifierVoteUpload) {
  const bool kFalseTrue[] = {false, true};
  for (bool found_generation_element : kFalseTrue) {
    SCOPED_TRACE(testing::Message() << "found_generation_element="
                                    << found_generation_element);
    PasswordForm form(*observed_form());
    form.form_data = saved_match()->form_data;

    // Create submitted form.
    PasswordForm submitted_form(form);
    submitted_form.preferred = true;
    submitted_form.username_value = saved_match()->username_value;
    submitted_form.password_value = saved_match()->password_value;

    FakeFormFetcher fetcher;
    fetcher.Fetch();
    PasswordFormManager form_manager(
        password_manager(), client(), client()->driver(), form,
        base::MakeUnique<NiceMock<MockFormSaver>>(), &fetcher);
    base::string16 generation_element = form.password_element;
    if (found_generation_element)
      form_manager.SaveGenerationFieldDetectedByClassifier(generation_element);
    else
      form_manager.SaveGenerationFieldDetectedByClassifier(base::string16());

    fetcher.SetNonFederated(std::vector<const PasswordForm*>(), 0u);

    autofill::FormStructure form_structure(submitted_form.form_data);

    EXPECT_CALL(
        *client()->mock_driver()->mock_autofill_download_manager(),
        StartUploadRequest(CheckUploadedFormClassifierVote(
                               found_generation_element, generation_element),
                           false, _, _, true));

    form_manager.ProvisionallySave(
        submitted_form, PasswordFormManager::IGNORE_OTHER_POSSIBLE_USERNAMES);
    form_manager.Save();
  }
}

TEST_F(PasswordFormManagerTest, FieldPropertiesMasksUpload) {
  PasswordForm form(*observed_form());
  form.form_data = saved_match()->form_data;

  // Create submitted form.
  PasswordForm submitted_form(form);
  submitted_form.preferred = true;
  submitted_form.username_value = saved_match()->username_value;
  submitted_form.password_value = saved_match()->password_value;

  FakeFormFetcher fetcher;
  fetcher.Fetch();
  PasswordFormManager form_manager(
      password_manager(), client(), client()->driver(), form,
      base::MakeUnique<NiceMock<MockFormSaver>>(), &fetcher);
  fetcher.SetNonFederated(std::vector<const PasswordForm*>(), 0u);

  DCHECK_EQ(3U, form.form_data.fields.size());
  submitted_form.form_data.fields[1].properties_mask =
      FieldPropertiesFlags::USER_TYPED;
  submitted_form.form_data.fields[2].properties_mask =
      FieldPropertiesFlags::USER_TYPED;

  std::map<base::string16, autofill::FieldPropertiesMask>
      expected_field_properties;
  for (const autofill::FormFieldData& field : submitted_form.form_data.fields)
    if (field.properties_mask)
      expected_field_properties[field.name] = field.properties_mask;

  EXPECT_CALL(*client()->mock_driver()->mock_autofill_download_manager(),
              StartUploadRequest(
                  CheckFieldPropertiesMasksUpload(expected_field_properties),
                  false, _, _, true));
  form_manager.ProvisionallySave(
      submitted_form, PasswordFormManager::IGNORE_OTHER_POSSIBLE_USERNAMES);
  form_manager.Save();
}

TEST_F(PasswordFormManagerTest, TestSavingAPIFormsWithSamePassword) {
  // Turn |observed_form| and |saved_match| to API created forms.
  observed_form()->username_element.clear();
  observed_form()->type = autofill::PasswordForm::TYPE_API;
  saved_match()->username_element.clear();
  saved_match()->type = autofill::PasswordForm::TYPE_API;

  FakeFormFetcher fetcher;
  fetcher.Fetch();
  PasswordFormManager form_manager(password_manager(), client(),
                                   client()->driver(), *observed_form(),
                                   base::MakeUnique<MockFormSaver>(), &fetcher);
  fetcher.SetNonFederated({saved_match()}, 0u);

  // User submits new credentials with the same password as in already saved
  // one.
  PasswordForm credentials(*observed_form());
  credentials.username_value =
      saved_match()->username_value + ASCIIToUTF16("1");
  credentials.password_value = saved_match()->password_value;
  credentials.preferred = true;

  form_manager.ProvisionallySave(
      credentials, PasswordFormManager::IGNORE_OTHER_POSSIBLE_USERNAMES);

  EXPECT_TRUE(form_manager.IsNewLogin());

  PasswordForm new_credentials;
  EXPECT_CALL(MockFormSaver::Get(&form_manager), Save(_, _, nullptr))
      .WillOnce(SaveArg<0>(&new_credentials));

  form_manager.Save();

  EXPECT_EQ(saved_match()->username_value + ASCIIToUTF16("1"),
            new_credentials.username_value);
  EXPECT_EQ(saved_match()->password_value, new_credentials.password_value);
  EXPECT_EQ(base::string16(), new_credentials.username_element);
  EXPECT_EQ(autofill::PasswordForm::TYPE_API, new_credentials.type);
}

TEST_F(PasswordFormManagerTest, SkipZeroClickIntact) {
  saved_match()->skip_zero_click = true;
  psl_saved_match()->skip_zero_click = true;
  fake_form_fetcher()->SetNonFederated({saved_match(), psl_saved_match()}, 0u);
  EXPECT_EQ(1u, form_manager()->best_matches().size());

  // User submits a credentials with an old username and a new password.
  PasswordForm credentials(*observed_form());
  credentials.username_value = saved_match()->username_value;
  credentials.password_value = ASCIIToUTF16("new_password");
  credentials.preferred = true;
  form_manager()->ProvisionallySave(
      credentials, PasswordFormManager::IGNORE_OTHER_POSSIBLE_USERNAMES);

  // Trigger saving to exercise some special case handling during updating.
  PasswordForm new_credentials;
  std::vector<autofill::PasswordForm> credentials_to_update;
  EXPECT_CALL(MockFormSaver::Get(form_manager()), Update(_, _, _, nullptr))
      .WillOnce(testing::DoAll(SaveArg<0>(&new_credentials),
                               SaveArgPointee<2>(&credentials_to_update)));
  EXPECT_CALL(*client()->mock_driver()->mock_autofill_download_manager(),
              StartUploadRequest(_, false, _, _, true));
  form_manager()->Save();

  EXPECT_TRUE(new_credentials.skip_zero_click);
  ASSERT_EQ(1u, credentials_to_update.size());
  EXPECT_TRUE(credentials_to_update[0].skip_zero_click);
}

TEST_F(PasswordFormManagerTest, ProbablyAccountCreationUpload) {
  PasswordForm form(*observed_form());
  form.form_data = saved_match()->form_data;

  FakeFormFetcher fetcher;
  fetcher.Fetch();
  PasswordFormManager form_manager(
      password_manager(), client(), client()->driver(), form,
      base::MakeUnique<NiceMock<MockFormSaver>>(), &fetcher);

  PasswordForm form_to_save(form);
  form_to_save.preferred = true;
  form_to_save.username_element = ASCIIToUTF16("observed-username-field");
  form_to_save.username_value = saved_match()->username_value;
  form_to_save.password_value = saved_match()->password_value;
  form_to_save.does_look_like_signup_form = true;

  fetcher.SetNonFederated(std::vector<const PasswordForm*>(), 0u);

  autofill::FormStructure pending_structure(form_to_save.form_data);
  autofill::ServerFieldTypeSet expected_available_field_types;
  std::map<base::string16, autofill::ServerFieldType> expected_types;
  expected_types[ASCIIToUTF16("full_name")] = autofill::UNKNOWN_TYPE;
  expected_types[saved_match()->username_element] = autofill::UNKNOWN_TYPE;
  expected_available_field_types.insert(
      autofill::PROBABLY_ACCOUNT_CREATION_PASSWORD);
  expected_types[saved_match()->password_element] =
      autofill::PROBABLY_ACCOUNT_CREATION_PASSWORD;

  EXPECT_CALL(*client()->mock_driver()->mock_autofill_download_manager(),
              StartUploadRequest(
                  CheckUploadedAutofillTypesAndSignature(
                      pending_structure.FormSignatureAsStr(), expected_types,
                      false /* expect_generation_vote */),
                  false, expected_available_field_types, std::string(), true));

  form_manager.ProvisionallySave(
      form_to_save, PasswordFormManager::IGNORE_OTHER_POSSIBLE_USERNAMES);
  form_manager.Save();
}

TEST_F(PasswordFormManagerFillOnAccountSelectTest, ProcessFrame) {
  EXPECT_CALL(*client()->mock_driver(),
              ShowInitialPasswordAccountSuggestions(_));
  fake_form_fetcher()->SetNonFederated({saved_match()}, 0u);
}

// Check that PasswordFormManager records
// PasswordManager_LoginFollowingAutofill as part of processing a credential
// update.
TEST_F(PasswordFormManagerTest, ReportProcessingUpdate) {
  fake_form_fetcher()->SetNonFederated({saved_match()}, 0u);
  PasswordForm pending = *observed_form();
  pending.username_value = saved_match()->username_value;
  pending.password_value = saved_match()->password_value;
  form_manager()->ProvisionallySave(
      pending, PasswordFormManager::IGNORE_OTHER_POSSIBLE_USERNAMES);

  EXPECT_FALSE(form_manager()->IsNewLogin());
  EXPECT_CALL(*client()->mock_driver()->mock_autofill_download_manager(),
              StartUploadRequest(_, false, _, _, true));

  base::UserActionTester tester;
  EXPECT_EQ(0, tester.GetActionCount("PasswordManager_LoginFollowingAutofill"));
  form_manager()->Update(*saved_match());
  EXPECT_EQ(1, tester.GetActionCount("PasswordManager_LoginFollowingAutofill"));
}

// Sanity check for calling ProcessMatches with empty vector. Should not crash
// or make sanitizers scream.
TEST_F(PasswordFormManagerTest, ProcessMatches_Empty) {
  static_cast<FormFetcher::Consumer*>(form_manager())
      ->ProcessMatches(std::vector<const PasswordForm*>(), 0u);
}

// For all combinations of PasswordForm schemes, test that ProcessMatches
// filters out forms with schemes not matching the observed form.
TEST_F(PasswordFormManagerTest, RemoveResultsWithWrongScheme_ObservingHTML) {
  for (int correct = 0; correct <= PasswordForm::SCHEME_LAST; ++correct) {
    for (int wrong = 0; wrong <= PasswordForm::SCHEME_LAST; ++wrong) {
      if (correct == wrong)
        continue;

      const PasswordForm::Scheme kCorrectScheme =
          static_cast<PasswordForm::Scheme>(correct);
      const PasswordForm::Scheme kWrongScheme =
          static_cast<PasswordForm::Scheme>(wrong);
      SCOPED_TRACE(testing::Message() << "Correct scheme = " << kCorrectScheme
                                      << ", wrong scheme = " << kWrongScheme);

      PasswordForm observed = *observed_form();
      observed.scheme = kCorrectScheme;
      FakeFormFetcher fetcher;
      fetcher.Fetch();
      PasswordFormManager form_manager(
          password_manager(), client(),
          (kCorrectScheme == PasswordForm::SCHEME_HTML ? client()->driver()
                                                       : nullptr),
          observed, base::MakeUnique<NiceMock<MockFormSaver>>(), &fetcher);

      PasswordForm match = *saved_match();
      match.scheme = kCorrectScheme;

      PasswordForm non_match = match;
      non_match.scheme = kWrongScheme;

      // First try putting the correct scheme first in returned matches.
      static_cast<FormFetcher::Consumer*>(&form_manager)
          ->ProcessMatches({&match, &non_match}, 0u);

      EXPECT_EQ(1u, form_manager.best_matches().size());
      EXPECT_EQ(kCorrectScheme,
                form_manager.best_matches().begin()->second->scheme);

      // Now try putting the correct scheme last in returned matches.
      static_cast<FormFetcher::Consumer*>(&form_manager)
          ->ProcessMatches({&non_match, &match}, 0u);

      EXPECT_EQ(1u, form_manager.best_matches().size());
      EXPECT_EQ(kCorrectScheme,
                form_manager.best_matches().begin()->second->scheme);
    }
  }
}

// Ensure that DoesManage takes into consideration drivers when origins are
// different.
TEST_F(PasswordFormManagerTest, DoesManageDifferentOrigins) {
  for (bool same_drivers : {false, true}) {
    PasswordForm submitted_form(*observed_form());
    observed_form()->origin = GURL("http://accounts.google.com/a/Login");
    submitted_form.origin = GURL("http://accounts.google.com/signin");

    EXPECT_NE(observed_form()->origin, submitted_form.origin);

    NiceMock<MockPasswordManagerDriver> driver;
    EXPECT_EQ(
        same_drivers ? PasswordFormManager::RESULT_COMPLETE_MATCH
                     : PasswordFormManager::RESULT_NO_MATCH,
        form_manager()->DoesManage(
            submitted_form, same_drivers ? client()->driver().get() : &driver));
  }
}

// Ensure that DoesManage returns No match when signon realms are different.
TEST_F(PasswordFormManagerTest, DoesManageDifferentSignonRealmSameDrivers) {
  PasswordForm submitted_form(*observed_form());
  observed_form()->signon_realm = "http://accounts.google.com";
  submitted_form.signon_realm = "http://facebook.com";

  EXPECT_EQ(
      PasswordFormManager::RESULT_NO_MATCH,
      form_manager()->DoesManage(submitted_form, client()->driver().get()));
}

TEST_F(PasswordFormManagerTest, UploadUsernameCorrectionVote) {
  // Observed and saved forms have the same password, but different usernames.
  PasswordForm new_login(*observed_form());
  new_login.username_value = saved_match()->other_possible_usernames[0].first;
  new_login.password_value = saved_match()->password_value;

  fake_form_fetcher()->SetNonFederated({saved_match()}, 0u);
  form_manager()->ProvisionallySave(
      new_login, PasswordFormManager::IGNORE_OTHER_POSSIBLE_USERNAMES);
  // No match found (because usernames are different).
  EXPECT_TRUE(form_manager()->IsNewLogin());

  // Checks the username correction vote is saved.
  PasswordForm expected_username_vote(*saved_match());
  expected_username_vote.username_element =
      saved_match()->other_possible_usernames[0].second;

  // Checks the upload.
  autofill::ServerFieldTypeSet expected_available_field_types;
  expected_available_field_types.insert(autofill::USERNAME);
  expected_available_field_types.insert(autofill::ACCOUNT_CREATION_PASSWORD);

  FormStructure expected_upload(expected_username_vote.form_data);

  std::string expected_login_signature =
      FormStructure(form_manager()->observed_form().form_data)
          .FormSignatureAsStr();

  std::map<base::string16, autofill::ServerFieldType> expected_types;
  expected_types[expected_username_vote.username_element] = autofill::USERNAME;
  expected_types[expected_username_vote.password_element] =
      autofill::ACCOUNT_CREATION_PASSWORD;
  expected_types[ASCIIToUTF16("Email")] = autofill::UNKNOWN_TYPE;

  EXPECT_CALL(*client()->mock_driver()->mock_autofill_download_manager(),
              StartUploadRequest(CheckUploadedAutofillTypesAndSignature(
                                     expected_upload.FormSignatureAsStr(),
                                     expected_types, false),
                                 false, expected_available_field_types,
                                 expected_login_signature, true));
  form_manager()->Save();
}

// Test that ResetStoredMatches removes references to previously fetched store
// results.
TEST_F(PasswordFormManagerTest, ResetStoredMatches) {
  PasswordForm best_match1 = *observed_form();
  best_match1.username_value = ASCIIToUTF16("user1");
  best_match1.password_value = ASCIIToUTF16("pass");
  best_match1.preferred = true;

  PasswordForm best_match2 = best_match1;
  best_match2.username_value = ASCIIToUTF16("user2");
  best_match2.preferred = false;

  PasswordForm non_best_match = best_match1;
  non_best_match.action = GURL();

  PasswordForm blacklisted = *observed_form();
  blacklisted.blacklisted_by_user = true;
  blacklisted.username_value.clear();

  fake_form_fetcher()->SetNonFederated(
      {&best_match1, &best_match2, &non_best_match, &blacklisted}, 0u);

  EXPECT_EQ(2u, form_manager()->best_matches().size());
  EXPECT_TRUE(form_manager()->preferred_match());
  EXPECT_EQ(1u, form_manager()->blacklisted_matches().size());

  // Trigger Update to verify that there is a non-best match.
  PasswordForm updated(best_match1);
  updated.password_value = ASCIIToUTF16("updated password");
  form_manager()->ProvisionallySave(
      updated, PasswordFormManager::IGNORE_OTHER_POSSIBLE_USERNAMES);
  std::vector<PasswordForm> credentials_to_update;
  EXPECT_CALL(MockFormSaver::Get(form_manager()), Update(_, _, _, nullptr))
      .WillOnce(SaveArgPointee<2>(&credentials_to_update));
  form_manager()->Save();

  PasswordForm updated_non_best = non_best_match;
  updated_non_best.password_value = updated.password_value;
  EXPECT_THAT(credentials_to_update, UnorderedElementsAre(updated_non_best));

  form_manager()->ResetStoredMatches();

  EXPECT_THAT(form_manager()->best_matches(), IsEmpty());
  EXPECT_FALSE(form_manager()->preferred_match());
  EXPECT_THAT(form_manager()->blacklisted_matches(), IsEmpty());

  // Simulate updating a saved credential again, but this time without non-best
  // matches. Verify that the old non-best matches are no longer present.
  fake_form_fetcher()->Fetch();
  fake_form_fetcher()->SetNonFederated({&best_match1}, 0u);

  form_manager()->ProvisionallySave(
      updated, PasswordFormManager::IGNORE_OTHER_POSSIBLE_USERNAMES);
  credentials_to_update.clear();
  EXPECT_CALL(MockFormSaver::Get(form_manager()), Update(_, _, _, nullptr))
      .WillOnce(SaveArgPointee<2>(&credentials_to_update));
  form_manager()->Save();

  EXPECT_THAT(credentials_to_update, IsEmpty());
}

// Check that on changing FormFetcher, the PasswordFormManager removes itself
// from consuming the old one.
TEST_F(PasswordFormManagerTest, DropFetcherOnDestruction) {
  MockFormFetcher fetcher;
  FormFetcher::Consumer* added_consumer = nullptr;
  EXPECT_CALL(fetcher, AddConsumer(_)).WillOnce(SaveArg<0>(&added_consumer));
  auto form_manager = base::MakeUnique<PasswordFormManager>(
      password_manager(), client(), client()->driver(), *observed_form(),
      base::MakeUnique<MockFormSaver>(), &fetcher);
  EXPECT_EQ(form_manager.get(), added_consumer);

  EXPECT_CALL(fetcher, RemoveConsumer(form_manager.get()));
  form_manager.reset();
}

// Check that if asked to take ownership of the same FormFetcher which it had
// consumed before, the PasswordFormManager does not add itself as a consumer
// again.
TEST_F(PasswordFormManagerTest, GrabFetcher_Same) {
  auto fetcher = base::MakeUnique<MockFormFetcher>();
  fetcher->Fetch();
  PasswordFormManager form_manager(
      password_manager(), client(), client()->driver(), *observed_form(),
      base::MakeUnique<MockFormSaver>(), fetcher.get());

  EXPECT_CALL(*fetcher, AddConsumer(_)).Times(0);
  EXPECT_CALL(*fetcher, RemoveConsumer(_)).Times(0);
  form_manager.GrabFetcher(std::move(fetcher));
  // There will be a RemoveConsumer call as soon as form_manager goes out of
  // scope, but the test needs to ensure that there is none as a result of
  // GrabFetcher.
  Mock::VerifyAndClearExpectations(form_manager.form_fetcher());
}

// Check that if asked to take ownership of a different FormFetcher than which
// it had consumed before, the PasswordFormManager adds itself as a consumer
// and replaces the references to the old results.
TEST_F(PasswordFormManagerTest, GrabFetcher_Different) {
  PasswordForm old_match = *observed_form();
  old_match.username_value = ASCIIToUTF16("user1");
  old_match.password_value = ASCIIToUTF16("pass");
  fake_form_fetcher()->SetNonFederated({&old_match}, 0u);
  EXPECT_EQ(1u, form_manager()->best_matches().size());
  EXPECT_EQ(&old_match, form_manager()->best_matches().begin()->second);

  // |form_manager()| uses |fake_form_fetcher()|, which is an instance different
  // from |fetcher| below.
  auto fetcher = base::MakeUnique<MockFormFetcher>();
  fetcher->Fetch();
  fetcher->SetNonFederated(std::vector<const PasswordForm*>(), 0u);
  EXPECT_CALL(*fetcher, AddConsumer(form_manager()));
  form_manager()->GrabFetcher(std::move(fetcher));

  EXPECT_EQ(0u, form_manager()->best_matches().size());
}

// Check that on changing FormFetcher, the PasswordFormManager removes itself
// from consuming the old one.
TEST_F(PasswordFormManagerTest, GrabFetcher_Remove) {
  MockFormFetcher old_fetcher;
  FormFetcher::Consumer* added_consumer = nullptr;
  EXPECT_CALL(old_fetcher, AddConsumer(_))
      .WillOnce(SaveArg<0>(&added_consumer));
  PasswordFormManager form_manager(
      password_manager(), client(), client()->driver(), *observed_form(),
      base::MakeUnique<MockFormSaver>(), &old_fetcher);
  EXPECT_EQ(&form_manager, added_consumer);

  auto new_fetcher = base::MakeUnique<MockFormFetcher>();
  EXPECT_CALL(*new_fetcher, AddConsumer(&form_manager));
  EXPECT_CALL(old_fetcher, RemoveConsumer(&form_manager));
  form_manager.GrabFetcher(std::move(new_fetcher));
}

TEST_F(PasswordFormManagerTest, UploadSignInForm_WithAutofillTypes) {
  // For newly saved passwords on a sign-in form, upload an autofill vote for a
  // username field and a autofill::PASSWORD vote for a password field.
  autofill::FormFieldData field;
  field.name = ASCIIToUTF16("Email");
  field.form_control_type = "text";
  observed_form()->form_data.fields.push_back(field);

  field.name = ASCIIToUTF16("Passwd");
  field.form_control_type = "password";
  observed_form()->form_data.fields.push_back(field);

  FakeFormFetcher fetcher;
  fetcher.Fetch();
  PasswordFormManager form_manager(
      password_manager(), client(), client()->driver(), *observed_form(),
      base::MakeUnique<NiceMock<MockFormSaver>>(), &fetcher);
  fetcher.SetNonFederated(std::vector<const PasswordForm*>(), 0u);

  PasswordForm form_to_save(*observed_form());
  form_to_save.preferred = true;
  form_to_save.username_value = ASCIIToUTF16("test@gmail.com");
  form_to_save.password_value = ASCIIToUTF16("password");

  std::unique_ptr<FormStructure> uploaded_form_structure;
  auto* mock_autofill_manager =
      client()->mock_driver()->mock_autofill_manager();
  EXPECT_CALL(*mock_autofill_manager, StartUploadProcessPtr(_, _, true))
      .WillOnce(WithArg<0>(SaveToUniquePtr(&uploaded_form_structure)));
  form_manager.ProvisionallySave(
      form_to_save, PasswordFormManager::IGNORE_OTHER_POSSIBLE_USERNAMES);
  form_manager.Save();

  ASSERT_EQ(2u, uploaded_form_structure->field_count());
  autofill::ServerFieldTypeSet expected_types = {autofill::PASSWORD};
  EXPECT_EQ(form_to_save.username_value,
            uploaded_form_structure->field(0)->value);
  EXPECT_EQ(expected_types,
            uploaded_form_structure->field(1)->possible_types());
}

// Checks that there is no upload on saving a password on a password form only
// with 1 field.
TEST_F(PasswordFormManagerTest, NoUploadsForSubmittedFormWithOnlyOneField) {
  autofill::FormFieldData field;
  field.name = ASCIIToUTF16("Passwd");
  field.form_control_type = "password";
  observed_form()->form_data.fields.push_back(field);

  FakeFormFetcher fetcher;
  fetcher.Fetch();
  PasswordFormManager form_manager(
      password_manager(), client(), client()->driver(), *observed_form(),
      base::MakeUnique<NiceMock<MockFormSaver>>(), &fetcher);
  fetcher.SetNonFederated(std::vector<const PasswordForm*>(), 0u);

  PasswordForm form_to_save(*observed_form());
  form_to_save.preferred = true;
  form_to_save.password_value = ASCIIToUTF16("password");

  auto* mock_autofill_manager =
      client()->mock_driver()->mock_autofill_manager();
  EXPECT_CALL(*mock_autofill_manager, StartUploadProcessPtr(_, _, _)).Times(0);
  form_manager.ProvisionallySave(
      form_to_save, PasswordFormManager::IGNORE_OTHER_POSSIBLE_USERNAMES);
  form_manager.Save();
}

}  // namespace password_manager
