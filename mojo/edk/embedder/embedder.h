// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOJO_EDK_EMBEDDER_EMBEDDER_H_
#define MOJO_EDK_EMBEDDER_EMBEDDER_H_

#include <stddef.h>

#include <string>

#include "base/callback.h"
#include "base/memory/ref_counted.h"
#include "base/memory/shared_memory_handle.h"
#include "base/process/process_handle.h"
#include "base/task_runner.h"
#include "mojo/edk/embedder/configuration.h"
#include "mojo/edk/embedder/scoped_platform_handle.h"
#include "mojo/edk/system/system_impl_export.h"
#include "mojo/public/c/system/types.h"

namespace base {
class PortProvider;
}

namespace mojo {
namespace edk {

using ProcessErrorCallback = base::Callback<void(const std::string& error)>;

// Basic configuration/initialization ------------------------------------------

// Must be called first, or just after setting configuration parameters, to
// initialize the (global, singleton) system state. There is no corresponding
// shutdown operation: once the EDK is initialized, public Mojo C API calls
// remain available for the remainder of the process's lifetime.
MOJO_SYSTEM_IMPL_EXPORT void Init(const Configuration& configuration);

// Like above but uses a default Configuration.
MOJO_SYSTEM_IMPL_EXPORT void Init();

// Sets a default callback to invoke when an internal error is reported but
// cannot be associated with a specific child process. Calling this is optional.
MOJO_SYSTEM_IMPL_EXPORT void SetDefaultProcessErrorCallback(
    const ProcessErrorCallback& callback);

// Generates a random ASCII token string for use with various APIs that expect
// a globally unique token string. May be called at any time on any thread.
MOJO_SYSTEM_IMPL_EXPORT std::string GenerateRandomToken();

// Basic functions -------------------------------------------------------------
//
// The functions in this section are available once |Init()| has been called and
// provide the embedder with some extra capabilities not exposed by public Mojo
// C APIs.

// Creates a |MojoHandle| that wraps the given |PlatformHandle| (taking
// ownership of it). This |MojoHandle| can then, e.g., be passed through message
// pipes. Note: This takes ownership (and thus closes) |platform_handle| even on
// failure, which is different from what you'd expect from a Mojo API, but it
// makes for a more convenient embedder API.
MOJO_SYSTEM_IMPL_EXPORT MojoResult
CreatePlatformHandleWrapper(ScopedPlatformHandle platform_handle,
                            MojoHandle* platform_handle_wrapper_handle);

// Retrieves the |PlatformHandle| that was wrapped into a |MojoHandle| (using
// |CreatePlatformHandleWrapper()| above). Note that the |MojoHandle| is closed
// on success.
MOJO_SYSTEM_IMPL_EXPORT MojoResult
PassWrappedPlatformHandle(MojoHandle platform_handle_wrapper_handle,
                          ScopedPlatformHandle* platform_handle);

// Creates a |MojoHandle| that wraps the given |SharedMemoryHandle| (taking
// ownership of it). |num_bytes| is the size of the shared memory object, and
// |read_only| is whether the handle is a read-only handle to shared memory.
// This |MojoHandle| is a Mojo shared buffer and can be manipulated using the
// shared buffer functions and transferred over a message pipe.
MOJO_SYSTEM_IMPL_EXPORT MojoResult
CreateSharedBufferWrapper(base::SharedMemoryHandle shared_memory_handle,
                          size_t num_bytes,
                          bool read_only,
                          MojoHandle* mojo_wrapper_handle);

// Retrieves the underlying |SharedMemoryHandle| from a shared buffer
// |MojoHandle| and closes the handle. If successful, |num_bytes| will contain
// the size of the shared memory buffer and |read_only| will contain whether the
// buffer handle is read-only. Both |num_bytes| and |read_only| may be null.
// Note: |shared_memory_handle| may be invalid even if this function returns
// success.
// Callers should perform appropriate checks.
MOJO_SYSTEM_IMPL_EXPORT MojoResult
PassSharedMemoryHandle(MojoHandle mojo_handle,
                       base::SharedMemoryHandle* shared_memory_handle,
                       size_t* num_bytes,
                       bool* read_only);

// Sets system properties that can be read by the MojoGetProperty() API. See the
// documentation for MojoPropertyType for supported property types and their
// corresponding value type.
//
// Default property values:
//   |MOJO_PROPERTY_TYPE_SYNC_CALL_ALLOWED| - true
MOJO_SYSTEM_IMPL_EXPORT MojoResult SetProperty(MojoPropertyType type,
                                               const void* value);

// Initialialization/shutdown for interprocess communication (IPC) -------------

// Retrieves the TaskRunner used for IPC I/O, as set by ScopedIPCSupport.
MOJO_SYSTEM_IMPL_EXPORT scoped_refptr<base::TaskRunner> GetIOTaskRunner();

#if defined(OS_MACOSX) && !defined(OS_IOS)
// Set the |base::PortProvider| for this process. Can be called on any thread,
// but must be set in the root process before any Mach ports can be transferred.
//
// If called at all, this must be called while a ScopedIPCSupport exists.
MOJO_SYSTEM_IMPL_EXPORT void SetMachPortProvider(
    base::PortProvider* port_provider);
#endif

}  // namespace edk
}  // namespace mojo

#endif  // MOJO_EDK_EMBEDDER_EMBEDDER_H_
