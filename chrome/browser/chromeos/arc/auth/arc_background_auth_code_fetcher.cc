// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/arc/auth/arc_background_auth_code_fetcher.h"

#include <utility>

#include "base/bind.h"
#include "base/callback_helpers.h"
#include "base/json/json_string_value_serializer.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/values.h"
#include "chrome/browser/ui/ash/multi_user/multi_user_util.h"
#include "components/signin/core/account_id/account_id.h"
#include "components/signin/core/browser/profile_oauth2_token_service.h"
#include "components/user_manager/known_user.h"
#include "content/public/browser/browser_context.h"
#include "content/public/common/url_constants.h"
#include "google_apis/gaia/gaia_auth_fetcher.h"
#include "google_apis/gaia/gaia_constants.h"
#include "net/base/load_flags.h"
#include "net/http/http_status_code.h"
#include "net/url_request/url_fetcher.h"

namespace arc {

namespace {

constexpr int kGetAuthCodeNetworkRetry = 3;

constexpr char kConsumerName[] = "ArcAuthContext";
constexpr char kToken[] = "token";
constexpr char kDeviceId[] = "device_id";
constexpr char kDeviceType[] = "device_type";
constexpr char kDeviceTypeArc[] = "arc_plus_plus";
constexpr char kLoginScopedToken[] = "login_scoped_token";
constexpr char kGetAuthCodeHeaders[] =
    "Content-Type: application/json; charset=utf-8";
constexpr char kContentTypeJSON[] = "application/json";

}  // namespace

const char kAuthTokenExchangeEndPoint[] =
    "https://www.googleapis.com/oauth2/v4/ExchangeToken";

ArcBackgroundAuthCodeFetcher::ArcBackgroundAuthCodeFetcher(
    Profile* profile,
    ArcAuthContext* context)
    : OAuth2TokenService::Consumer(kConsumerName),
      profile_(profile),
      context_(context),
      weak_ptr_factory_(this) {}

ArcBackgroundAuthCodeFetcher::~ArcBackgroundAuthCodeFetcher() = default;

void ArcBackgroundAuthCodeFetcher::Fetch(const FetchCallback& callback) {
  DCHECK(callback_.is_null());
  callback_ = callback;

  context_->Prepare(base::Bind(&ArcBackgroundAuthCodeFetcher::OnPrepared,
                               weak_ptr_factory_.GetWeakPtr()));
}

void ArcBackgroundAuthCodeFetcher::OnPrepared(
    net::URLRequestContextGetter* request_context_getter) {
  if (!request_context_getter) {
    ReportResult(std::string(), OptInSilentAuthCode::CONTEXT_NOT_READY);
    return;
  }

  DCHECK(!request_context_getter_);
  request_context_getter_ = request_context_getter;

  // Get token service and account ID to fetch auth tokens.
  ProfileOAuth2TokenService* const token_service = context_->token_service();
  const std::string& account_id = context_->account_id();
  DCHECK(token_service->RefreshTokenIsAvailable(account_id));

  OAuth2TokenService::ScopeSet scopes;
  scopes.insert(GaiaConstants::kOAuth1LoginScope);
  login_token_request_ = token_service->StartRequest(account_id, scopes, this);
}

void ArcBackgroundAuthCodeFetcher::OnGetTokenSuccess(
    const OAuth2TokenService::Request* request,
    const std::string& access_token,
    const base::Time& expiration_time) {
  ResetFetchers();

  const std::string device_id = user_manager::known_user::GetDeviceId(
      multi_user_util::GetAccountIdFromProfile(profile_));
  DCHECK(!device_id.empty());

  base::DictionaryValue request_data;
  request_data.SetString(kLoginScopedToken, access_token);
  request_data.SetString(kDeviceType, kDeviceTypeArc);
  request_data.SetString(kDeviceId, device_id);
  std::string request_string;
  base::JSONWriter::Write(request_data, &request_string);

  auth_code_fetcher_ = net::URLFetcher::Create(
      0, GURL(kAuthTokenExchangeEndPoint), net::URLFetcher::POST, this);
  auth_code_fetcher_->SetRequestContext(request_context_getter_);
  auth_code_fetcher_->SetUploadData(kContentTypeJSON, request_string);
  auth_code_fetcher_->SetLoadFlags(net::LOAD_DISABLE_CACHE |
                                   net::LOAD_BYPASS_CACHE);
  auth_code_fetcher_->SetAutomaticallyRetryOnNetworkChanges(
      kGetAuthCodeNetworkRetry);
  auth_code_fetcher_->SetExtraRequestHeaders(kGetAuthCodeHeaders);
  auth_code_fetcher_->Start();
}

void ArcBackgroundAuthCodeFetcher::OnGetTokenFailure(
    const OAuth2TokenService::Request* request,
    const GoogleServiceAuthError& error) {
  LOG(WARNING) << "Failed to get LST " << error.ToString() << ".";
  ResetFetchers();
  ReportResult(std::string(), OptInSilentAuthCode::NO_LST_TOKEN);
}

void ArcBackgroundAuthCodeFetcher::OnURLFetchComplete(
    const net::URLFetcher* source) {
  const int response_code = source->GetResponseCode();
  std::string json_string;
  source->GetResponseAsString(&json_string);

  ResetFetchers();

  if (response_code != net::HTTP_OK) {
    LOG(WARNING) << "Server returned wrong response code: " << response_code
                 << ".";
    OptInSilentAuthCode uma_status;
    if (response_code >= 400 && response_code < 500)
      uma_status = OptInSilentAuthCode::HTTP_CLIENT_FAILURE;
    if (response_code >= 500 && response_code < 600)
      uma_status = OptInSilentAuthCode::HTTP_SERVER_FAILURE;
    else
      uma_status = OptInSilentAuthCode::HTTP_UNKNOWN_FAILURE;
    ReportResult(std::string(), uma_status);
    return;
  }

  JSONStringValueDeserializer deserializer(json_string);
  std::string error_msg;
  std::unique_ptr<base::Value> auth_code_info =
      deserializer.Deserialize(nullptr, &error_msg);
  if (!auth_code_info) {
    LOG(WARNING) << "Unable to deserialize auth code json data: " << error_msg
                 << ".";
    ReportResult(std::string(), OptInSilentAuthCode::RESPONSE_PARSE_FAILURE);
    return;
  }

  std::unique_ptr<base::DictionaryValue> auth_code_dictionary =
      base::DictionaryValue::From(std::move(auth_code_info));
  if (!auth_code_dictionary) {
    NOTREACHED();
    ReportResult(std::string(), OptInSilentAuthCode::RESPONSE_PARSE_FAILURE);
    return;
  }

  std::string auth_code;
  if (!auth_code_dictionary->GetString(kToken, &auth_code) ||
      auth_code.empty()) {
    LOG(WARNING) << "Response does not contain auth code.";
    ReportResult(std::string(), OptInSilentAuthCode::NO_AUTH_CODE_IN_RESPONSE);
    return;
  }

  ReportResult(auth_code, OptInSilentAuthCode::SUCCESS);
}

void ArcBackgroundAuthCodeFetcher::ResetFetchers() {
  login_token_request_.reset();
  auth_code_fetcher_.reset();
}

void ArcBackgroundAuthCodeFetcher::ReportResult(
    const std::string& auth_code,
    OptInSilentAuthCode uma_status) {
  UpdateSilentAuthCodeUMA(uma_status);
  base::ResetAndReturn(&callback_).Run(!auth_code.empty(), auth_code);
}

}  // namespace arc
