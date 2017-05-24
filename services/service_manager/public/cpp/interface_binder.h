// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_SERVICE_MANAGER_PUBLIC_CPP_INTERFACE_BINDER_H_
#define SERVICES_SERVICE_MANAGER_PUBLIC_CPP_INTERFACE_BINDER_H_

#include <string>
#include <utility>

#include "base/bind.h"
#include "mojo/public/cpp/bindings/interface_request.h"
#include "mojo/public/cpp/system/message_pipe.h"

namespace service_manager {

struct BindSourceInfo;

class InterfaceBinder {
 public:
  virtual ~InterfaceBinder() {}

  // Asks the InterfaceBinder to bind an implementation of the specified
  // interface to the request passed via |handle|. If the InterfaceBinder binds
  // an implementation it must take ownership of the request handle.
  virtual void BindInterface(const BindSourceInfo& source_info,
                             const std::string& interface_name,
                             mojo::ScopedMessagePipeHandle handle) = 0;
};

template <typename Interface>
class CallbackBinder : public InterfaceBinder {
 public:
  using BindCallback = base::Callback<void(const BindSourceInfo&,
                                           mojo::InterfaceRequest<Interface>)>;

  CallbackBinder(const BindCallback& callback,
                 const scoped_refptr<base::SingleThreadTaskRunner>& task_runner)
      : callback_(callback), task_runner_(task_runner) {}
  ~CallbackBinder() override {}

 private:
  // InterfaceBinder:
  void BindInterface(const BindSourceInfo& source_info,
                     const std::string& interface_name,
                     mojo::ScopedMessagePipeHandle handle) override {
    mojo::InterfaceRequest<Interface> request(std::move(handle));
    if (task_runner_) {
      task_runner_->PostTask(
          FROM_HERE, base::BindOnce(&CallbackBinder::RunCallback, callback_,
                                    source_info, std::move(request)));
    } else {
      RunCallback(callback_, source_info, std::move(request));
    }
  }

  static void RunCallback(const BindCallback& callback,
                          const BindSourceInfo& source_info,
                          mojo::InterfaceRequest<Interface> request) {
    callback.Run(source_info, std::move(request));
  }

  const BindCallback callback_;
  scoped_refptr<base::SingleThreadTaskRunner> task_runner_;
  DISALLOW_COPY_AND_ASSIGN(CallbackBinder);
};

class GenericCallbackBinder : public InterfaceBinder {
 public:
  using BindCallback = base::Callback<void(const BindSourceInfo&,
                                           const std::string&,
                                           mojo::ScopedMessagePipeHandle)>;

  GenericCallbackBinder(
      const BindCallback& callback,
      const scoped_refptr<base::SingleThreadTaskRunner>& task_runner);
  GenericCallbackBinder(
      const base::Callback<void(mojo::ScopedMessagePipeHandle)>& callback,
      const scoped_refptr<base::SingleThreadTaskRunner>& task_runner);
  ~GenericCallbackBinder() override;

 private:
  // InterfaceBinder:
  void BindInterface(const BindSourceInfo& source_info,
                     const std::string& interface_name,
                     mojo::ScopedMessagePipeHandle handle) override;

  static void RunCallback(const BindCallback& callback,
                          const BindSourceInfo& source_info,
                          const std::string& interface_name,
                          mojo::ScopedMessagePipeHandle client_handle);

  const BindCallback callback_;
  scoped_refptr<base::SingleThreadTaskRunner> task_runner_;
  DISALLOW_COPY_AND_ASSIGN(GenericCallbackBinder);
};

}  // namespace service_manager

#endif  // SERVICES_SERVICE_MANAGER_PUBLIC_CPP_INTERFACE_BINDER_H_
