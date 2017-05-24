// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/password_manager/core/browser/suppressed_https_form_fetcher.h"

#include "base/logging.h"
#include "base/stl_util.h"
#include "components/password_manager/core/browser/password_manager_client.h"
#include "components/password_manager/core/browser/password_store.h"
#include "url/gurl.h"

namespace password_manager {

SuppressedHTTPSFormFetcher::SuppressedHTTPSFormFetcher(
    const GURL& http_origin,
    const PasswordManagerClient* client,
    Consumer* consumer)
    : client_(client), consumer_(consumer) {
  DCHECK(client_);
  DCHECK(consumer_);
  DCHECK(http_origin.is_valid());
  DCHECK(http_origin.SchemeIs(url::kHttpScheme));

  GURL::Replacements scheme_to_https;
  scheme_to_https.SetSchemeStr(url::kHttpsScheme);
  GURL https_origin = http_origin.ReplaceComponents(scheme_to_https);
  PasswordStore::FormDigest synthetic_form_digest(
      autofill::PasswordForm::SCHEME_HTML, https_origin.GetOrigin().spec(),
      https_origin);

  client_->GetPasswordStore()->GetLogins(synthetic_form_digest, this);
}

SuppressedHTTPSFormFetcher::~SuppressedHTTPSFormFetcher() = default;

void SuppressedHTTPSFormFetcher::OnGetPasswordStoreResults(
    std::vector<std::unique_ptr<autofill::PasswordForm>> results) {
  base::EraseIf(
      results, [](const std::unique_ptr<autofill::PasswordForm>& form) {
        return form->is_public_suffix_match || form->is_affiliation_based_match;
      });

  consumer_->ProcessSuppressedHTTPSForms(std::move(results));
}

}  // namespace password_manager
