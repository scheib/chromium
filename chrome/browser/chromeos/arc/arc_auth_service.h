// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_ARC_ARC_AUTH_SERVICE_H_
#define CHROME_BROWSER_CHROMEOS_ARC_ARC_AUTH_SERVICE_H_

#include <memory>
#include <string>

#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/chromeos/arc/auth/arc_active_directory_enrollment_token_fetcher.h"
#include "components/arc/arc_service.h"
#include "components/arc/common/auth.mojom.h"
#include "components/arc/instance_holder.h"
#include "mojo/public/cpp/bindings/binding.h"

namespace arc {

class ArcFetcherBase;

// Implementation of ARC authorization.
// TODO(hidehiko): Move to c/b/c/arc/auth with adding tests.
class ArcAuthService : public ArcService,
                       public mojom::AuthHost,
                       public InstanceHolder<mojom::AuthInstance>::Observer {
 public:
  explicit ArcAuthService(ArcBridgeService* bridge_service);
  ~ArcAuthService() override;

  // For supporting ArcServiceManager::GetService<T>().
  static const char kArcServiceName[];

  // InstanceHolder<mojom::AuthInstance>::Observer:
  void OnInstanceReady() override;
  void OnInstanceClosed() override;

  // mojom::AuthHost:
  void OnSignInComplete() override;
  void OnSignInFailed(mojom::ArcSignInFailureReason reason) override;
  void RequestAccountInfo() override;
  void ReportMetrics(mojom::MetricsType metrics_type, int32_t value) override;
  void ReportAccountCheckStatus(mojom::AccountCheckStatus status) override;

  // Deprecated methods:
  // For security reason this code can be used only once and exists for specific
  // period of time.
  void GetAuthCodeDeprecated0(
      const GetAuthCodeDeprecated0Callback& callback) override;
  void GetAuthCodeDeprecated(
      const GetAuthCodeDeprecatedCallback& callback) override;
  void GetAuthCodeAndAccountTypeDeprecated(
      const GetAuthCodeAndAccountTypeDeprecatedCallback& callback) override;
  void GetIsAccountManagedDeprecated(
      const GetIsAccountManagedDeprecatedCallback& callback) override;

 private:
  using AccountInfoCallback = base::Callback<void(mojom::AccountInfoPtr)>;
  class AccountInfoNotifier;

  // Starts to request account info.
  void RequestAccountInfoInternal(
      std::unique_ptr<AccountInfoNotifier> account_info_notifier);

  // Callbacks when auth info is fetched.
  void OnEnrollmentTokenFetched(
      ArcActiveDirectoryEnrollmentTokenFetcher::Status status,
      const std::string& enrollment_token,
      const std::string& user_id);
  void OnAuthCodeFetched(bool success, const std::string& auth_code);

  // Called to let ARC container know the account info.
  void OnAccountInfoReady(mojom::AccountInfoPtr account_info);

  mojo::Binding<mojom::AuthHost> binding_;

  std::unique_ptr<AccountInfoNotifier> notifier_;
  std::unique_ptr<ArcFetcherBase> fetcher_;

  base::WeakPtrFactory<ArcAuthService> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(ArcAuthService);
};

}  // namespace arc

#endif  // CHROME_BROWSER_CHROMEOS_ARC_ARC_AUTH_SERVICE_H_
