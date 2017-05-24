// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mojo/edk/embedder/embedder.h"

#include <stdint.h>
#include <utility>

#include "base/bind.h"
#include "base/logging.h"
#include "base/memory/ref_counted.h"
#include "base/rand_util.h"
#include "base/strings/string_number_conversions.h"
#include "base/task_runner.h"
#include "mojo/edk/embedder/embedder_internal.h"
#include "mojo/edk/embedder/entrypoints.h"
#include "mojo/edk/system/configuration.h"
#include "mojo/edk/system/core.h"
#include "mojo/edk/system/node_controller.h"

#if !defined(OS_NACL)
#include "crypto/random.h"
#endif

namespace mojo {
namespace edk {

class Core;
class PlatformSupport;

namespace internal {

Core* g_core;

Core* GetCore() { return g_core; }

}  // namespace internal

void Init(const Configuration& configuration) {
  MojoSystemThunks thunks = MakeSystemThunks();
  size_t expected_size = MojoEmbedderSetSystemThunks(&thunks);
  DCHECK_EQ(expected_size, sizeof(thunks));

  internal::g_configuration = configuration;
  internal::g_core = new Core;
}

void Init() {
  Init(Configuration());
}

void SetDefaultProcessErrorCallback(const ProcessErrorCallback& callback) {
  internal::g_core->SetDefaultProcessErrorCallback(callback);
}

std::string GenerateRandomToken() {
  char random_bytes[16];
#if defined(OS_NACL)
  // Not secure. For NaCl only!
  base::RandBytes(random_bytes, 16);
#else
  crypto::RandBytes(random_bytes, 16);
#endif
  return base::HexEncode(random_bytes, 16);
}

MojoResult CreatePlatformHandleWrapper(
    ScopedPlatformHandle platform_handle,
    MojoHandle* platform_handle_wrapper_handle) {
  return internal::g_core->CreatePlatformHandleWrapper(
      std::move(platform_handle), platform_handle_wrapper_handle);
}

MojoResult PassWrappedPlatformHandle(MojoHandle platform_handle_wrapper_handle,
                                     ScopedPlatformHandle* platform_handle) {
  return internal::g_core->PassWrappedPlatformHandle(
      platform_handle_wrapper_handle, platform_handle);
}

MojoResult CreateSharedBufferWrapper(
    base::SharedMemoryHandle shared_memory_handle,
    size_t num_bytes,
    bool read_only,
    MojoHandle* mojo_wrapper_handle) {
  return internal::g_core->CreateSharedBufferWrapper(
      shared_memory_handle, num_bytes, read_only, mojo_wrapper_handle);
}

MojoResult PassSharedMemoryHandle(
    MojoHandle mojo_handle,
    base::SharedMemoryHandle* shared_memory_handle,
    size_t* num_bytes,
    bool* read_only) {
  return internal::g_core->PassSharedMemoryHandle(
      mojo_handle, shared_memory_handle, num_bytes, read_only);
}

MojoResult SetProperty(MojoPropertyType type, const void* value) {
  CHECK(internal::g_core);
  return internal::g_core->SetProperty(type, value);
}

scoped_refptr<base::TaskRunner> GetIOTaskRunner() {
  return internal::g_core->GetNodeController()->io_task_runner();
}

#if defined(OS_MACOSX) && !defined(OS_IOS)
void SetMachPortProvider(base::PortProvider* port_provider) {
  DCHECK(port_provider);
  internal::g_core->SetMachPortProvider(port_provider);
}
#endif

}  // namespace edk
}  // namespace mojo
