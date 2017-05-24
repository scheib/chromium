// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "components/safe_browsing/password_protection/password_protection_request.h"

#include "base/memory/ptr_util.h"
#include "base/memory/weak_ptr.h"
#include "base/metrics/histogram_macros.h"
#include "components/data_use_measurement/core/data_use_user_data.h"
#include "components/safe_browsing_db/database_manager.h"
#include "net/base/escape.h"
#include "net/base/load_flags.h"
#include "net/base/url_util.h"
#include "net/http/http_status_code.h"
#include "net/traffic_annotation/network_traffic_annotation.h"

using content::BrowserThread;

namespace safe_browsing {

PasswordProtectionRequest::PasswordProtectionRequest(
    const GURL& main_frame_url,
    const GURL& password_form_action,
    const GURL& password_form_frame_url,
    const std::string& saved_domain,
    LoginReputationClientRequest::TriggerType type,
    PasswordProtectionService* pps,
    int request_timeout_in_ms)
    : main_frame_url_(main_frame_url),
      password_form_action_(password_form_action),
      password_form_frame_url_(password_form_frame_url),
      saved_domain_(saved_domain),
      request_type_(type),
      password_protection_service_(pps),
      database_manager_(password_protection_service_->database_manager()),
      request_timeout_in_ms_(request_timeout_in_ms),
      weakptr_factory_(this) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
}

PasswordProtectionRequest::~PasswordProtectionRequest() {
  weakptr_factory_.InvalidateWeakPtrs();
}

void PasswordProtectionRequest::Start() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  CheckWhitelistOnUIThread();
}

void PasswordProtectionRequest::CheckWhitelistOnUIThread() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  bool* match_whitelist = new bool(false);
  // TODO(jialiul): Move CheckCsdWhitelistOnIOThread to
  // PasswordProtectionRequest class, since PasswordProtectionService no longer
  // need it.
  tracker_.PostTaskAndReply(
      BrowserThread::GetTaskRunnerForThread(BrowserThread::IO).get(), FROM_HERE,
      base::Bind(&PasswordProtectionService::CheckCsdWhitelistOnIOThread,
                 base::Unretained(password_protection_service_),
                 main_frame_url_, match_whitelist),
      base::Bind(&PasswordProtectionRequest::OnWhitelistCheckDone, this,
                 base::Owned(match_whitelist)));
}

void PasswordProtectionRequest::OnWhitelistCheckDone(
    const bool* match_whitelist) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  if (*match_whitelist)
    Finish(PasswordProtectionService::MATCHED_WHITELIST, nullptr);
  else
    CheckCachedVerdicts();
}

void PasswordProtectionRequest::CheckCachedVerdicts() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  if (!password_protection_service_) {
    Finish(PasswordProtectionService::SERVICE_DESTROYED, nullptr);
    return;
  }

  std::unique_ptr<LoginReputationClientResponse> cached_response =
      base::MakeUnique<LoginReputationClientResponse>();
  auto verdict = password_protection_service_->GetCachedVerdict(
      main_frame_url_, cached_response.get());
  if (verdict != LoginReputationClientResponse::VERDICT_TYPE_UNSPECIFIED)
    Finish(PasswordProtectionService::RESPONSE_ALREADY_CACHED,
           std::move(cached_response));
  else
    SendRequest();
}

void PasswordProtectionRequest::FillRequestProto() {
  request_proto_ = base::MakeUnique<LoginReputationClientRequest>();
  request_proto_->set_page_url(main_frame_url_.spec());
  request_proto_->set_trigger_type(request_type_);
  password_protection_service_->FillUserPopulation(request_type_,
                                                   request_proto_.get());
  request_proto_->set_stored_verdict_cnt(
      password_protection_service_->GetStoredVerdictCount());
  LoginReputationClientRequest::Frame* main_frame =
      request_proto_->add_frames();
  main_frame->set_url(main_frame_url_.spec());
  main_frame->set_frame_index(0 /* main frame */);
  password_protection_service_->FillReferrerChain(
      main_frame_url_, -1 /* tab id not available */, main_frame);

  switch (request_type_) {
    case LoginReputationClientRequest::UNFAMILIAR_LOGIN_PAGE: {
      LoginReputationClientRequest::Frame::Form* password_form;
      if (password_form_frame_url_ == main_frame_url_) {
        main_frame->set_has_password_field(true);
        password_form = main_frame->add_forms();
      } else {
        LoginReputationClientRequest::Frame* password_frame =
            request_proto_->add_frames();
        password_frame->set_url(password_form_frame_url_.spec());
        password_frame->set_has_password_field(true);
        // TODO(jialiul): Add referrer chain for subframes later.
        password_form = password_frame->add_forms();
      }
      password_form->set_action_url(password_form_action_.spec());
      // TODO(jialiul): Fill more frame specific info when Safe Browsing backend
      // is ready to handle these pieces of information.
      break;
    }
    case LoginReputationClientRequest::PASSWORD_REUSE_EVENT: {
      // TODO(jialiul): Fill more password reuse related information when ready.
      break;
    }
    default:
      NOTREACHED();
  }
}

void PasswordProtectionRequest::SendRequest() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  FillRequestProto();

  std::string serialized_request;
  if (!request_proto_->SerializeToString(&serialized_request)) {
    Finish(PasswordProtectionService::REQUEST_MALFORMED, nullptr);
    return;
  }

  // In case the request take too long, we set a timer to cancel this request.
  StartTimeout();
  net::NetworkTrafficAnnotationTag traffic_annotation =
      net::DefineNetworkTrafficAnnotation("password_protection_request", R"(
        semantics {
          sender: "Safe Browsing"
          description:
            "When the user is about to log in to a new, uncommon site, Chrome "
            "will send a request to Safe Browsing to determine if the page is "
            "phishing. It'll then show a warning if the page poses a risk of "
            "phishing."
          trigger:
            "When a user focuses on a password field on a page that they "
            "haven't visited before and that isn't popular or known to be safe."
          data:
            "URL and referrer of the current page, password form action, and "
            "iframe structure."
          destination: GOOGLE_OWNED_SERVICE
        }
        policy {
          cookies_allowed: true
          cookies_store: "Safe Browsing Cookie Store"
          setting:
            "Users can control this feature via 'Protect you and your device "
            "from dangerous sites' or 'Automatically report details of "
            "possible security incidents to Google' setting under 'Privacy'. "
            "By default, the first setting is enabled and the second is not."
          chrome_policy {
            SafeBrowsingExtendedReportingOptInAllowed {
              policy_options {mode: MANDATORY}
              SafeBrowsingExtendedReportingOptInAllowed: false
            }
          }
        })");
  fetcher_ = net::URLFetcher::Create(
      0, PasswordProtectionService::GetPasswordProtectionRequestUrl(),
      net::URLFetcher::POST, this, traffic_annotation);
  data_use_measurement::DataUseUserData::AttachToFetcher(
      fetcher_.get(), data_use_measurement::DataUseUserData::SAFE_BROWSING);
  fetcher_->SetLoadFlags(net::LOAD_DISABLE_CACHE);
  fetcher_->SetAutomaticallyRetryOn5xx(false);
  fetcher_->SetRequestContext(
      password_protection_service_->request_context_getter().get());
  fetcher_->SetUploadData("application/octet-stream", serialized_request);
  request_start_time_ = base::TimeTicks::Now();
  fetcher_->Start();
}

void PasswordProtectionRequest::StartTimeout() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  // If request is not done withing 10 seconds, we cancel this request.
  // The weak pointer used for the timeout will be invalidated (and
  // hence would prevent the timeout) if the check completes on time and
  // execution reaches Finish().
  BrowserThread::PostDelayedTask(
      BrowserThread::UI, FROM_HERE,
      base::Bind(&PasswordProtectionRequest::Cancel, GetWeakPtr(), true),
      base::TimeDelta::FromMilliseconds(request_timeout_in_ms_));
}

void PasswordProtectionRequest::OnURLFetchComplete(
    const net::URLFetcher* source) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  net::URLRequestStatus status = source->GetStatus();
  const bool is_success = status.is_success();
  const int response_code = source->GetResponseCode();

  UMA_HISTOGRAM_SPARSE_SLOWLY(
      "PasswordProtection.PasswordProtectionResponseOrErrorCode",
      is_success ? response_code : status.error());

  if (!is_success || net::HTTP_OK != response_code) {
    Finish(PasswordProtectionService::FETCH_FAILED, nullptr);
    return;
  }

  std::unique_ptr<LoginReputationClientResponse> response =
      base::MakeUnique<LoginReputationClientResponse>();
  std::string response_body;
  bool received_data = source->GetResponseAsString(&response_body);
  DCHECK(received_data);
  fetcher_.reset();  // We don't need it anymore.
  UMA_HISTOGRAM_TIMES("PasswordProtection.RequestNetworkDuration",
                      base::TimeTicks::Now() - request_start_time_);
  if (response->ParseFromString(response_body))
    Finish(PasswordProtectionService::SUCCEEDED, std::move(response));
  else
    Finish(PasswordProtectionService::RESPONSE_MALFORMED, nullptr);
}

void PasswordProtectionRequest::Finish(
    PasswordProtectionService::RequestOutcome outcome,
    std::unique_ptr<LoginReputationClientResponse> response) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  tracker_.TryCancelAll();

  if (request_type_ == LoginReputationClientRequest::UNFAMILIAR_LOGIN_PAGE) {
    UMA_HISTOGRAM_ENUMERATION(kPasswordOnFocusRequestOutcomeHistogramName,
                              outcome, PasswordProtectionService::MAX_OUTCOME);
  } else {
    UMA_HISTOGRAM_ENUMERATION(kPasswordEntryRequestOutcomeHistogramName,
                              outcome, PasswordProtectionService::MAX_OUTCOME);
  }

  if (response) {
    switch (request_type_) {
      case LoginReputationClientRequest::UNFAMILIAR_LOGIN_PAGE:
        UMA_HISTOGRAM_ENUMERATION(
            "PasswordProtection.Verdict.PasswordFieldOnFocus",
            response->verdict_type(),
            LoginReputationClientResponse_VerdictType_VerdictType_MAX + 1);
        break;
      case LoginReputationClientRequest::PASSWORD_REUSE_EVENT:
        UMA_HISTOGRAM_ENUMERATION(
            "PasswordProtection.Verdict.ProtectedPasswordEntry",
            response->verdict_type(),
            LoginReputationClientResponse_VerdictType_VerdictType_MAX + 1);
        break;
      default:
        NOTREACHED();
    }
  }

  DCHECK(password_protection_service_);
  password_protection_service_->RequestFinished(this, std::move(response));
}

void PasswordProtectionRequest::Cancel(bool timed_out) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  fetcher_.reset();

  Finish(timed_out ? PasswordProtectionService::TIMEDOUT
                   : PasswordProtectionService::CANCELED,
         nullptr);
}

}  // namespace safe_browsing
