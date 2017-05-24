// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/update_client/ping_manager.h"

#include <stddef.h>

#include <memory>
#include <string>
#include <vector>

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/macros.h"
#include "base/memory/ptr_util.h"
#include "base/sequenced_task_runner.h"
#include "components/update_client/configurator.h"
#include "components/update_client/protocol_builder.h"
#include "components/update_client/request_sender.h"
#include "components/update_client/utils.h"
#include "net/url_request/url_fetcher.h"
#include "url/gurl.h"

namespace update_client {

namespace {

// Sends a fire and forget ping. The instances of this class have no
// ownership and they self-delete upon completion. One instance of this class
// can send only one ping.
class PingSender {
 public:
  explicit PingSender(const scoped_refptr<Configurator>& config);
  ~PingSender();

  bool SendPing(const Component& component);

 private:
  void OnRequestSenderComplete(int error,
                               const std::string& response,
                               int retry_after_sec);

  const scoped_refptr<Configurator> config_;
  std::unique_ptr<RequestSender> request_sender_;
  base::ThreadChecker thread_checker_;

  DISALLOW_COPY_AND_ASSIGN(PingSender);
};

PingSender::PingSender(const scoped_refptr<Configurator>& config)
    : config_(config) {}

PingSender::~PingSender() {
  DCHECK(thread_checker_.CalledOnValidThread());
}

void PingSender::OnRequestSenderComplete(int error,
                                         const std::string& response,
                                         int retry_after_sec) {
  DCHECK(thread_checker_.CalledOnValidThread());
  delete this;
}

bool PingSender::SendPing(const Component& component) {
  DCHECK(thread_checker_.CalledOnValidThread());

  if (component.events().empty())
    return false;

  auto urls(config_->PingUrl());
  if (component.crx_component().requires_network_encryption)
    RemoveUnsecureUrls(&urls);

  if (urls.empty())
    return false;

  request_sender_ = base::MakeUnique<RequestSender>(config_);
  request_sender_->Send(
      false, BuildEventPingRequest(*config_, component), urls,
      base::Bind(&PingSender::OnRequestSenderComplete, base::Unretained(this)));
  return true;
}

}  // namespace

PingManager::PingManager(const scoped_refptr<Configurator>& config)
    : config_(config) {}

PingManager::~PingManager() {
  DCHECK(thread_checker_.CalledOnValidThread());
}

bool PingManager::SendPing(const Component& component) {
  DCHECK(thread_checker_.CalledOnValidThread());

  auto ping_sender = base::MakeUnique<PingSender>(config_);
  if (!ping_sender->SendPing(component))
    return false;

  // The ping sender object self-deletes after sending the ping asynchrously.
  ping_sender.release();
  return true;
}

}  // namespace update_client
