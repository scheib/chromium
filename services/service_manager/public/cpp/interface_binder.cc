// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/service_manager/public/cpp/interface_binder.h"

#include "services/service_manager/public/cpp/bind_source_info.h"

namespace service_manager {

namespace {

void BindCallbackAdapter(
    const base::Callback<void(mojo::ScopedMessagePipeHandle)>& callback,
    const BindSourceInfo& source_info,
    const std::string& interface_name,
    mojo::ScopedMessagePipeHandle handle) {
  callback.Run(std::move(handle));
}

}  // namespace

GenericCallbackBinder::GenericCallbackBinder(
    const BindCallback& callback,
    const scoped_refptr<base::SingleThreadTaskRunner>& task_runner)
    : callback_(callback), task_runner_(task_runner) {}

GenericCallbackBinder::GenericCallbackBinder(
    const base::Callback<void(mojo::ScopedMessagePipeHandle)>& callback,
    const scoped_refptr<base::SingleThreadTaskRunner>& task_runner)
    : callback_(base::Bind(&BindCallbackAdapter, callback)),
      task_runner_(task_runner) {}

GenericCallbackBinder::~GenericCallbackBinder() {}

void GenericCallbackBinder::BindInterface(
    const BindSourceInfo& source_info,
    const std::string& interface_name,
    mojo::ScopedMessagePipeHandle handle) {
  if (task_runner_) {
    task_runner_->PostTask(
        FROM_HERE,
        base::Bind(&GenericCallbackBinder::RunCallback, callback_, source_info,
                   interface_name, base::Passed(&handle)));
    return;
  }
  RunCallback(callback_, source_info, interface_name, std::move(handle));
}

// static
void GenericCallbackBinder::RunCallback(const BindCallback& callback,
                                        const BindSourceInfo& source_info,
                                        const std::string& interface_name,
                                        mojo::ScopedMessagePipeHandle handle) {
  callback.Run(source_info, interface_name, std::move(handle));
}

}  // namespace service_manager
