// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_PASSWORD_MANAGER_CORE_BROWSER_SUPPRESSED_HTTPS_FORM_FETCHER_H_
#define COMPONENTS_PASSWORD_MANAGER_CORE_BROWSER_SUPPRESSED_HTTPS_FORM_FETCHER_H_

#include <memory>
#include <vector>

#include "base/gtest_prod_util.h"
#include "base/macros.h"
#include "components/autofill/core/common/password_form.h"
#include "components/password_manager/core/browser/password_store_consumer.h"

class GURL;

namespace password_manager {

class PasswordManagerClient;

// Fetches credentials saved for the HTTPS counterpart of the given HTTP origin.
//
// Filling these HTTPS credentials into forms served over HTTP is obviously
// suppressed, the purpose of doing such a query is to collect metrics on how
// often this happens and inconveniences the user.
//
// This logic is implemented by this class, a separate PasswordStore consumer,
// to make it very sure that these credentials will not get mistakenly filled.
class SuppressedHTTPSFormFetcher : public PasswordStoreConsumer {
 public:
  // Interface to be implemented by the consumer of this class.
  class Consumer {
   public:
    virtual void ProcessSuppressedHTTPSForms(
        std::vector<std::unique_ptr<autofill::PasswordForm>> forms) = 0;
  };

  SuppressedHTTPSFormFetcher(const GURL& http_origin,
                             const PasswordManagerClient* client,
                             Consumer* consumer);
  ~SuppressedHTTPSFormFetcher() override;

 protected:
  // PasswordStoreConsumer:
  void OnGetPasswordStoreResults(
      std::vector<std::unique_ptr<autofill::PasswordForm>> results) override;

 private:
  FRIEND_TEST_ALL_PREFIXES(SuppressedHTTPSFormFetcherTest, EmptyStore);
  FRIEND_TEST_ALL_PREFIXES(SuppressedHTTPSFormFetcherTest, FullStore);

  // The client and the consumer should outlive |this|.
  const PasswordManagerClient* client_;
  Consumer* consumer_;

  DISALLOW_COPY_AND_ASSIGN(SuppressedHTTPSFormFetcher);
};

}  // namespace password_manager

#endif  // COMPONENTS_PASSWORD_MANAGER_CORE_BROWSER_SUPPRESSED_HTTPS_FORM_FETCHER_H_
