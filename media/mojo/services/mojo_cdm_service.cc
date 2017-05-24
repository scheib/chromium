// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/mojo/services/mojo_cdm_service.h"

#include <map>
#include <utility>

#include "base/bind.h"
#include "base/macros.h"
#include "base/memory/ptr_util.h"
#include "base/synchronization/lock.h"
#include "media/base/cdm_config.h"
#include "media/base/cdm_context.h"
#include "media/base/cdm_factory.h"
#include "media/base/cdm_key_information.h"
#include "media/base/key_systems.h"
#include "media/cdm/cdm_manager.h"
#include "media/mojo/common/media_type_converters.h"
#include "media/mojo/services/mojo_cdm_service_context.h"
#include "url/gurl.h"

namespace media {

using SimpleMojoCdmPromise = MojoCdmPromise<>;
using NewSessionMojoCdmPromise = MojoCdmPromise<std::string>;

int MojoCdmService::next_cdm_id_ = CdmContext::kInvalidCdmId + 1;

MojoCdmService::MojoCdmService(base::WeakPtr<MojoCdmServiceContext> context,
                               CdmFactory* cdm_factory)
    : context_(context),
      cdm_factory_(cdm_factory),
      cdm_id_(CdmContext::kInvalidCdmId),
      weak_factory_(this) {
  DCHECK(context_);
  DCHECK(cdm_factory_);
}

MojoCdmService::~MojoCdmService() {
  if (cdm_id_ == CdmContext::kInvalidCdmId)
    return;

  CdmManager::GetInstance()->UnregisterCdm(cdm_id_);

  if (context_)
    context_->UnregisterCdm(cdm_id_);
}

void MojoCdmService::SetClient(mojom::ContentDecryptionModuleClientPtr client) {
  client_ = std::move(client);
}

void MojoCdmService::Initialize(const std::string& key_system,
                                const std::string& security_origin,
                                mojom::CdmConfigPtr cdm_config,
                                const InitializeCallback& callback) {
  DVLOG(1) << __func__ << ": " << key_system;
  DCHECK(!cdm_);

  auto weak_this = weak_factory_.GetWeakPtr();
  cdm_factory_->Create(
      key_system, GURL(security_origin), cdm_config.To<CdmConfig>(),
      base::Bind(&MojoCdmService::OnSessionMessage, weak_this),
      base::Bind(&MojoCdmService::OnSessionClosed, weak_this),
      base::Bind(&MojoCdmService::OnSessionKeysChange, weak_this),
      base::Bind(&MojoCdmService::OnSessionExpirationUpdate, weak_this),
      base::Bind(&MojoCdmService::OnCdmCreated, weak_this, callback));
}

void MojoCdmService::SetServerCertificate(
    const std::vector<uint8_t>& certificate_data,
    const SetServerCertificateCallback& callback) {
  DVLOG(2) << __func__;
  cdm_->SetServerCertificate(certificate_data,
                             base::MakeUnique<SimpleMojoCdmPromise>(callback));
}

void MojoCdmService::CreateSessionAndGenerateRequest(
    CdmSessionType session_type,
    EmeInitDataType init_data_type,
    const std::vector<uint8_t>& init_data,
    const CreateSessionAndGenerateRequestCallback& callback) {
  DVLOG(2) << __func__;
  cdm_->CreateSessionAndGenerateRequest(
      session_type, init_data_type, init_data,
      base::MakeUnique<NewSessionMojoCdmPromise>(callback));
}

void MojoCdmService::LoadSession(CdmSessionType session_type,
                                 const std::string& session_id,
                                 const LoadSessionCallback& callback) {
  DVLOG(2) << __func__;
  cdm_->LoadSession(session_type, session_id,
                    base::MakeUnique<NewSessionMojoCdmPromise>(callback));
}

void MojoCdmService::UpdateSession(const std::string& session_id,
                                   const std::vector<uint8_t>& response,
                                   const UpdateSessionCallback& callback) {
  DVLOG(2) << __func__;
  cdm_->UpdateSession(
      session_id, response,
      std::unique_ptr<SimpleCdmPromise>(new SimpleMojoCdmPromise(callback)));
}

void MojoCdmService::CloseSession(const std::string& session_id,
                                  const CloseSessionCallback& callback) {
  DVLOG(2) << __func__;
  cdm_->CloseSession(session_id,
                     base::MakeUnique<SimpleMojoCdmPromise>(callback));
}

void MojoCdmService::RemoveSession(const std::string& session_id,
                                   const RemoveSessionCallback& callback) {
  DVLOG(2) << __func__;
  cdm_->RemoveSession(session_id,
                      base::MakeUnique<SimpleMojoCdmPromise>(callback));
}

scoped_refptr<ContentDecryptionModule> MojoCdmService::GetCdm() {
  return cdm_;
}

void MojoCdmService::OnCdmCreated(
    const InitializeCallback& callback,
    const scoped_refptr<::media::ContentDecryptionModule>& cdm,
    const std::string& error_message) {
  mojom::CdmPromiseResultPtr cdm_promise_result(mojom::CdmPromiseResult::New());

  // TODO(xhwang): This should not happen when KeySystemInfo is properly
  // populated. See http://crbug.com/469366
  if (!cdm || !context_) {
    cdm_promise_result->success = false;
    cdm_promise_result->exception = CdmPromise::Exception::NOT_SUPPORTED_ERROR;
    cdm_promise_result->system_code = 0;
    cdm_promise_result->error_message = error_message;
    callback.Run(std::move(cdm_promise_result), 0, nullptr);
    return;
  }

  cdm_ = cdm;
  cdm_id_ = next_cdm_id_++;

  context_->RegisterCdm(cdm_id_, this);
  CdmManager::GetInstance()->RegisterCdm(cdm_id_, cdm);

  // If |cdm| has a decryptor, create the MojoDecryptorService
  // and pass the connection back to the client.
  mojom::DecryptorPtr decryptor_service;
  CdmContext* const cdm_context = cdm_->GetCdmContext();
  if (cdm_context && cdm_context->GetDecryptor()) {
    decryptor_.reset(new MojoDecryptorService(
        cdm_context->GetDecryptor(), MakeRequest(&decryptor_service),
        base::Bind(&MojoCdmService::OnDecryptorConnectionError, weak_this_)));
  }

  DVLOG(1) << __func__ << ": CDM successfully created with ID " << cdm_id_;
  cdm_promise_result->success = true;
  callback.Run(std::move(cdm_promise_result), cdm_id_,
               std::move(decryptor_service));
}

void MojoCdmService::OnSessionMessage(const std::string& session_id,
                                      ::media::CdmMessageType message_type,
                                      const std::vector<uint8_t>& message) {
  DVLOG(2) << __func__;
  client_->OnSessionMessage(session_id, message_type, message);
}

void MojoCdmService::OnSessionKeysChange(const std::string& session_id,
                                         bool has_additional_usable_key,
                                         CdmKeysInfo keys_info) {
  DVLOG(2) << __func__
           << " has_additional_usable_key=" << has_additional_usable_key;

  std::vector<mojom::CdmKeyInformationPtr> keys_data;
  for (auto& key : keys_info)
    keys_data.push_back(mojom::CdmKeyInformation::From(*(key.get())));
  client_->OnSessionKeysChange(session_id, has_additional_usable_key,
                               std::move(keys_data));
}

void MojoCdmService::OnSessionExpirationUpdate(const std::string& session_id,
                                               base::Time new_expiry_time_sec) {
  DVLOG(2) << __func__ << " expiry=" << new_expiry_time_sec;
  client_->OnSessionExpirationUpdate(session_id,
                                     new_expiry_time_sec.ToDoubleT());
}

void MojoCdmService::OnSessionClosed(const std::string& session_id) {
  DVLOG(2) << __func__;
  client_->OnSessionClosed(session_id);
}

void MojoCdmService::OnDecryptorConnectionError() {
  DVLOG(2) << __func__;

  // MojoDecryptorService has lost connectivity to it's client, so it can be
  // freed.
  decryptor_.reset();
}

}  // namespace media
