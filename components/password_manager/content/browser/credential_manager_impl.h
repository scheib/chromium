// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_PASSWORD_MANAGER_CONTENT_BROWSER_CONTENT_CREDENTIAL_MANAGER_IMPL_H_
#define COMPONENTS_PASSWORD_MANAGER_CONTENT_BROWSER_CONTENT_CREDENTIAL_MANAGER_IMPL_H_

#include <memory>

#include "base/callback.h"
#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "components/password_manager/content/common/credential_manager.mojom.h"
#include "components/password_manager/core/browser/credential_manager_password_form_manager.h"
#include "components/password_manager/core/browser/credential_manager_pending_request_task.h"
#include "components/password_manager/core/browser/credential_manager_pending_require_user_mediation_task.h"
#include "components/password_manager/core/browser/password_store_consumer.h"
#include "components/password_manager/core/common/credential_manager_types.h"
#include "components/prefs/pref_member.h"
#include "content/public/browser/web_contents_observer.h"
#include "mojo/public/cpp/bindings/binding_set.h"

class GURL;

namespace autofill {
struct PasswordForm;
}

namespace content {
class WebContents;
}

namespace password_manager {
class CredentialManagerPasswordFormManager;
class PasswordManagerClient;
class PasswordManagerDriver;
class PasswordStore;
struct CredentialInfo;

class CredentialManagerImpl
    : public mojom::CredentialManager,
      public content::WebContentsObserver,
      public CredentialManagerPasswordFormManagerDelegate,
      public CredentialManagerPendingRequestTaskDelegate,
      public CredentialManagerPendingRequireUserMediationTaskDelegate {
 public:
  CredentialManagerImpl(content::WebContents* web_contents,
                        PasswordManagerClient* client);
  ~CredentialManagerImpl() override;

  void BindRequest(mojom::CredentialManagerRequest request);

  // mojom::CredentialManager methods:
  void Store(const CredentialInfo& credential, StoreCallback callback) override;
  void RequireUserMediation(RequireUserMediationCallback callback) override;
  void Get(CredentialMediationRequirement mediation,
           bool include_passwords,
           const std::vector<GURL>& federations,
           GetCallback callback) override;

  // CredentialManagerPendingRequestTaskDelegate:
  bool IsZeroClickAllowed() const override;
  GURL GetOrigin() const override;
  void SendCredential(const SendCredentialCallback& send_callback,
                      const CredentialInfo& info) override;
  void SendPasswordForm(const SendCredentialCallback& send_callback,
                        CredentialMediationRequirement mediation,
                        const autofill::PasswordForm* form) override;
  PasswordManagerClient* client() const override;

  // CredentialManagerPendingSignedOutTaskDelegate:
  PasswordStore* GetPasswordStore() override;
  void DoneRequiringUserMediation() override;

  // CredentialManagerPasswordFormManagerDelegate:
  void OnProvisionalSaveComplete() override;

  // Returns FormDigest for the current URL.
  PasswordStore::FormDigest GetSynthesizedFormForOrigin() const;

 private:
  // Returns the driver for the current main frame.
  // Virtual for testing.
  virtual base::WeakPtr<PasswordManagerDriver> GetDriver();

  PasswordManagerClient* client_;
  std::unique_ptr<CredentialManagerPasswordFormManager> form_manager_;

  // Set to false to disable automatic signing in.
  BooleanPrefMember auto_signin_enabled_;

  // When 'OnRequestCredential' is called, it in turn calls out to the
  // PasswordStore; we push enough data into Pending*Task objects so that
  // they can properly respond to the request once the PasswordStore gives
  // us data.
  std::unique_ptr<CredentialManagerPendingRequestTask> pending_request_;
  std::unique_ptr<CredentialManagerPendingRequireUserMediationTask>
      pending_require_user_mediation_;

  mojo::BindingSet<mojom::CredentialManager> bindings_;

  base::WeakPtrFactory<CredentialManagerImpl> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(CredentialManagerImpl);
};

}  // namespace password_manager

#endif  // COMPONENTS_PASSWORD_MANAGER_CONTENT_BROWSER_CONTENT_CREDENTIAL_MANAGER_IMPL_H_
