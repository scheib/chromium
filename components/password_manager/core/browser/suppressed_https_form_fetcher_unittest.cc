// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/password_manager/core/browser/suppressed_https_form_fetcher.h"

#include "base/macros.h"
#include "base/memory/ptr_util.h"
#include "base/message_loop/message_loop.h"
#include "components/password_manager/core/browser/mock_password_store.h"
#include "components/password_manager/core/browser/password_manager_test_utils.h"
#include "components/password_manager/core/browser/stub_password_manager_client.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace password_manager {
namespace {

using autofill::PasswordForm;
using testing::_;

const char kTestHttpsURL[] = "https://one.example.com/";
const char kTestPSLMatchingHttpsURL[] = "https://psl.example.com/";
const char kTestHttpURL[] = "http://one.example.com/";
const char kTestAndroidRealmURI[] = "android://hash@com.example.one.android/";

class MockConsumer : public SuppressedHTTPSFormFetcher::Consumer {
 public:
  MockConsumer() = default;
  ~MockConsumer() = default;

  // GMock still cannot mock methods with move-only args.
  MOCK_METHOD1(ProcessSuppressedHTTPSFormsConstRef,
               void(const std::vector<std::unique_ptr<PasswordForm>>&));

 protected:
  // SuppressedHTTPSFormFetcher::Consumer:
  void ProcessSuppressedHTTPSForms(
      std::vector<std::unique_ptr<PasswordForm>> forms) override {
    ProcessSuppressedHTTPSFormsConstRef(forms);
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(MockConsumer);
};

class PasswordManagerClientWithMockStore : public StubPasswordManagerClient {
 public:
  PasswordManagerClientWithMockStore()
      : mock_store_(new ::testing::StrictMock<MockPasswordStore>()) {}
  ~PasswordManagerClientWithMockStore() override {
    mock_store_->ShutdownOnUIThread();
  }

  MockPasswordStore& mock_password_store() const { return *mock_store_.get(); }

 protected:
  // StubPasswordManagerClient:
  PasswordStore* GetPasswordStore() const override { return mock_store_.get(); }

 private:
  scoped_refptr<MockPasswordStore> mock_store_;

  DISALLOW_COPY_AND_ASSIGN(PasswordManagerClientWithMockStore);
};

}  // namespace

class SuppressedHTTPSFormFetcherTest : public testing::Test {
 public:
  SuppressedHTTPSFormFetcherTest() = default;
  ~SuppressedHTTPSFormFetcherTest() override = default;

  MockConsumer* mock_consumer() { return &consumer_; }
  MockPasswordStore* mock_store() { return &client_.mock_password_store(); }
  PasswordManagerClientWithMockStore* mock_client() { return &client_; }

 private:
  base::MessageLoop message_loop_;  // Needed by the MockPasswordStore.

  MockConsumer consumer_;
  PasswordManagerClientWithMockStore client_;

  DISALLOW_COPY_AND_ASSIGN(SuppressedHTTPSFormFetcherTest);
};

TEST_F(SuppressedHTTPSFormFetcherTest, EmptyStore) {
  PasswordStore::FormDigest observed_form_digest(
      autofill::PasswordForm::SCHEME_HTML, kTestHttpURL, GURL(kTestHttpURL));
  PasswordStore::FormDigest https_form_digest(
      autofill::PasswordForm::SCHEME_HTML, kTestHttpsURL, GURL(kTestHttpsURL));

  EXPECT_CALL(*mock_store(), GetLogins(https_form_digest, _));
  EXPECT_CALL(*mock_consumer(),
              ProcessSuppressedHTTPSFormsConstRef(::testing::IsEmpty()));
  SuppressedHTTPSFormFetcher suppressed_form_fetcher(
      observed_form_digest.origin, mock_client(), mock_consumer());
  suppressed_form_fetcher.OnGetPasswordStoreResults(
      std::vector<std::unique_ptr<PasswordForm>>());
}

TEST_F(SuppressedHTTPSFormFetcherTest, FullStore) {
  static const PasswordFormData kTestCredentials[] = {
      // Credential that is for the HTTPS counterpart of the observed form.
      {PasswordForm::SCHEME_HTML, kTestHttpsURL, kTestHttpsURL, "", L"", L"",
       L"", L"username_value_1", L"password_value_1", true, 1},
      // Another credential for the HTTPS counterpart of the observed form.
      {PasswordForm::SCHEME_HTML, kTestHttpsURL, kTestHttpsURL, "", L"", L"",
       L"", L"username_value_2", L"password_value_2", true, 1},
      // A PSL match of the HTTPS counterpart of the observed form.
      {PasswordForm::SCHEME_HTML, kTestPSLMatchingHttpsURL,
       kTestPSLMatchingHttpsURL, "", L"", L"", L"", L"username_value_3",
       L"password_value_3", true, 1},
      // Credential for an affiliated Android application.
      {PasswordForm::SCHEME_HTML, kTestAndroidRealmURI, kTestAndroidRealmURI,
       "", L"", L"", L"", L"username_value_4", L"password_value_4", true, 1}};

  std::vector<std::unique_ptr<PasswordForm>> simulated_store_results;
  for (const auto& form_data : kTestCredentials)
    simulated_store_results.push_back(
        CreatePasswordFormFromDataForTesting(form_data));
  ASSERT_EQ(4u, simulated_store_results.size());
  simulated_store_results[2]->is_public_suffix_match = true;
  simulated_store_results[3]->is_affiliation_based_match = true;

  // The PSL and affiliated matches should be filtered out.
  std::vector<std::unique_ptr<PasswordForm>> expected_results;
  expected_results.push_back(
      base::MakeUnique<PasswordForm>(*simulated_store_results[0]));
  expected_results.push_back(
      base::MakeUnique<PasswordForm>(*simulated_store_results[1]));

  PasswordStore::FormDigest observed_form_digest(
      autofill::PasswordForm::SCHEME_HTML, kTestHttpURL, GURL(kTestHttpURL));
  PasswordStore::FormDigest https_form_digest(
      autofill::PasswordForm::SCHEME_HTML, kTestHttpsURL, GURL(kTestHttpsURL));

  EXPECT_CALL(*mock_store(), GetLogins(https_form_digest, _));
  EXPECT_CALL(*mock_consumer(),
              ProcessSuppressedHTTPSFormsConstRef(
                  UnorderedPasswordFormElementsAre(&expected_results)));
  SuppressedHTTPSFormFetcher suppressed_form_fetcher(
      observed_form_digest.origin, mock_client(), mock_consumer());
  suppressed_form_fetcher.OnGetPasswordStoreResults(
      std::move(simulated_store_results));
}

}  // namespace password_manager
