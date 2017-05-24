// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOJO_EDK_SYSTEM_CORE_H_
#define MOJO_EDK_SYSTEM_CORE_H_

#include <memory>
#include <string>
#include <vector>

#include "base/callback.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/memory/shared_memory_handle.h"
#include "base/synchronization/lock.h"
#include "base/task_runner.h"
#include "mojo/edk/embedder/scoped_platform_handle.h"
#include "mojo/edk/system/dispatcher.h"
#include "mojo/edk/system/handle_signals_state.h"
#include "mojo/edk/system/handle_table.h"
#include "mojo/edk/system/mapping_table.h"
#include "mojo/edk/system/node_controller.h"
#include "mojo/edk/system/system_impl_export.h"
#include "mojo/public/c/system/buffer.h"
#include "mojo/public/c/system/data_pipe.h"
#include "mojo/public/c/system/message_pipe.h"
#include "mojo/public/c/system/platform_handle.h"
#include "mojo/public/c/system/types.h"
#include "mojo/public/c/system/watcher.h"
#include "mojo/public/cpp/system/message_pipe.h"

namespace base {
class PortProvider;
}

namespace mojo {
namespace edk {

// |Core| is an object that implements the Mojo system calls. All public methods
// are thread-safe.
class MOJO_SYSTEM_IMPL_EXPORT Core {
 public:
  Core();
  virtual ~Core();

  // Called exactly once, shortly after construction, and before any other
  // methods are called on this object.
  void SetIOTaskRunner(scoped_refptr<base::TaskRunner> io_task_runner);

  // Retrieves the NodeController for the current process.
  NodeController* GetNodeController();

  scoped_refptr<Dispatcher> GetDispatcher(MojoHandle handle);

  void SetDefaultProcessErrorCallback(const ProcessErrorCallback& callback);

  // Creates a message pipe endpoint with an unbound peer port returned in
  // |*peer|. Useful for setting up cross-process bootstrap message pipes. The
  // returned message pipe handle is usable immediately by the caller.
  //
  // The value returned in |*peer| may be passed along with a broker client
  // invitation. See SendBrokerClientInvitation() below.
  ScopedMessagePipeHandle CreatePartialMessagePipe(ports::PortRef* peer);

  // Like above but exchanges an existing ports::PortRef for a message pipe
  // handle which wraps it.
  ScopedMessagePipeHandle CreatePartialMessagePipe(const ports::PortRef& port);

  // Sends a broker client invitation to |target_process| over the connection
  // medium in |connection_params|. The other end of the connection medium in
  // |connection_params| can be used within the target process to call
  // AcceptBrokerClientInvitation() and complete the process's admission into
  // this process graph.
  //
  // |attached_ports| is a list of named port references to be attached to the
  // invitation. An attached port can be claimed (as a message pipe handle) by
  // the invitee.
  void SendBrokerClientInvitation(
      base::ProcessHandle target_process,
      ConnectionParams connection_params,
      const std::vector<std::pair<std::string, ports::PortRef>>& attached_ports,
      const ProcessErrorCallback& process_error_callback);

  // Accepts a broker client invitation via |connection_params|. The other end
  // of the connection medium in |connection_params| must have been used by some
  // other process to send an OutgoingBrokerClientInvitation.
  void AcceptBrokerClientInvitation(ConnectionParams connection_params);

  // Extracts a named message pipe endpoint from the broker client invitation
  // accepted by this process. Must only be called after
  // AcceptBrokerClientInvitation.
  ScopedMessagePipeHandle ExtractMessagePipeFromInvitation(
      const std::string& name);

  // Called to connect to a peer process. This should be called only if there
  // is no common ancestor for the processes involved within this mojo system.
  // Both processes must call this function, each passing one end of a platform
  // channel. |port| is a port to be merged with the remote peer's port, which
  // it will provide via the same API.
  //
  // Returns an ID which can be later used to close the connection via
  // ClosePeerConnection().
  uint64_t ConnectToPeer(ConnectionParams connection_params,
                         const ports::PortRef& port);
  void ClosePeerConnection(uint64_t peer_connection_id);

  // Sets the mach port provider for this process.
  void SetMachPortProvider(base::PortProvider* port_provider);

  MojoHandle AddDispatcher(scoped_refptr<Dispatcher> dispatcher);

  // Adds new dispatchers for non-message-pipe handles received in a message.
  // |dispatchers| and |handles| should be the same size.
  bool AddDispatchersFromTransit(
      const std::vector<Dispatcher::DispatcherInTransit>& dispatchers,
      MojoHandle* handles);

  // See "mojo/edk/embedder/embedder.h" for more information on these functions.
  MojoResult CreatePlatformHandleWrapper(ScopedPlatformHandle platform_handle,
                                         MojoHandle* wrapper_handle);

  MojoResult PassWrappedPlatformHandle(MojoHandle wrapper_handle,
                                       ScopedPlatformHandle* platform_handle);

  MojoResult CreateSharedBufferWrapper(
      base::SharedMemoryHandle shared_memory_handle,
      size_t num_bytes,
      bool read_only,
      MojoHandle* mojo_wrapper_handle);

  MojoResult PassSharedMemoryHandle(
      MojoHandle mojo_handle,
      base::SharedMemoryHandle* shared_memory_handle,
      size_t* num_bytes,
      bool* read_only);

  // Requests that the EDK tear itself down. |callback| will be called once
  // the shutdown process is complete. Note that |callback| is always called
  // asynchronously on the calling thread if said thread is running a message
  // loop, and the calling thread must continue running a MessageLoop at least
  // until the callback is called. If there is no running loop, the |callback|
  // may be called from any thread. Beware!
  void RequestShutdown(const base::Closure& callback);

  MojoResult SetProperty(MojoPropertyType type, const void* value);

  // ---------------------------------------------------------------------------

  // The following methods are essentially implementations of the Mojo Core
  // functions of the Mojo API, with the C interface translated to C++ by
  // "mojo/edk/embedder/entrypoints.cc". The best way to understand the contract
  // of these methods is to look at the header files defining the corresponding
  // API functions, referenced below.

  // These methods correspond to the API functions defined in
  // "mojo/public/c/system/functions.h":
  MojoTimeTicks GetTimeTicksNow();
  MojoResult Close(MojoHandle handle);
  MojoResult QueryHandleSignalsState(MojoHandle handle,
                                     MojoHandleSignalsState* signals_state);
  MojoResult CreateWatcher(MojoWatcherCallback callback,
                           MojoHandle* watcher_handle);
  MojoResult Watch(MojoHandle watcher_handle,
                   MojoHandle handle,
                   MojoHandleSignals signals,
                   uintptr_t context);
  MojoResult CancelWatch(MojoHandle watcher_handle, uintptr_t context);
  MojoResult ArmWatcher(MojoHandle watcher_handle,
                        uint32_t* num_ready_contexts,
                        uintptr_t* ready_contexts,
                        MojoResult* ready_results,
                        MojoHandleSignalsState* ready_signals_states);
  MojoResult AllocMessage(uint32_t num_bytes,
                          const MojoHandle* handles,
                          uint32_t num_handles,
                          MojoAllocMessageFlags flags,
                          MojoMessageHandle* message);
  MojoResult FreeMessage(MojoMessageHandle message);
  MojoResult GetMessageBuffer(MojoMessageHandle message, void** buffer);
  MojoResult GetProperty(MojoPropertyType type, void* value);

  // These methods correspond to the API functions defined in
  // "mojo/public/c/system/message_pipe.h":
  MojoResult CreateMessagePipe(
      const MojoCreateMessagePipeOptions* options,
      MojoHandle* message_pipe_handle0,
      MojoHandle* message_pipe_handle1);
  MojoResult WriteMessage(MojoHandle message_pipe_handle,
                          const void* bytes,
                          uint32_t num_bytes,
                          const MojoHandle* handles,
                          uint32_t num_handles,
                          MojoWriteMessageFlags flags);
  MojoResult WriteMessageNew(MojoHandle message_pipe_handle,
                             MojoMessageHandle message,
                             MojoWriteMessageFlags flags);
  MojoResult ReadMessage(MojoHandle message_pipe_handle,
                         void* bytes,
                         uint32_t* num_bytes,
                         MojoHandle* handles,
                         uint32_t* num_handles,
                         MojoReadMessageFlags flags);
  MojoResult ReadMessageNew(MojoHandle message_pipe_handle,
                            MojoMessageHandle* message,
                            uint32_t* num_bytes,
                            MojoHandle* handles,
                            uint32_t* num_handles,
                            MojoReadMessageFlags flags);
  MojoResult FuseMessagePipes(MojoHandle handle0, MojoHandle handle1);
  MojoResult NotifyBadMessage(MojoMessageHandle message,
                              const char* error,
                              size_t error_num_bytes);

  // These methods correspond to the API functions defined in
  // "mojo/public/c/system/data_pipe.h":
  MojoResult CreateDataPipe(
      const MojoCreateDataPipeOptions* options,
      MojoHandle* data_pipe_producer_handle,
      MojoHandle* data_pipe_consumer_handle);
  MojoResult WriteData(MojoHandle data_pipe_producer_handle,
                       const void* elements,
                       uint32_t* num_bytes,
                       MojoWriteDataFlags flags);
  MojoResult BeginWriteData(MojoHandle data_pipe_producer_handle,
                            void** buffer,
                            uint32_t* buffer_num_bytes,
                            MojoWriteDataFlags flags);
  MojoResult EndWriteData(MojoHandle data_pipe_producer_handle,
                          uint32_t num_bytes_written);
  MojoResult ReadData(MojoHandle data_pipe_consumer_handle,
                      void* elements,
                      uint32_t* num_bytes,
                      MojoReadDataFlags flags);
  MojoResult BeginReadData(MojoHandle data_pipe_consumer_handle,
                           const void** buffer,
                           uint32_t* buffer_num_bytes,
                           MojoReadDataFlags flags);
  MojoResult EndReadData(MojoHandle data_pipe_consumer_handle,
                         uint32_t num_bytes_read);

  // These methods correspond to the API functions defined in
  // "mojo/public/c/system/buffer.h":
  MojoResult CreateSharedBuffer(
      const MojoCreateSharedBufferOptions* options,
      uint64_t num_bytes,
      MojoHandle* shared_buffer_handle);
  MojoResult DuplicateBufferHandle(
      MojoHandle buffer_handle,
      const MojoDuplicateBufferHandleOptions* options,
      MojoHandle* new_buffer_handle);
  MojoResult MapBuffer(MojoHandle buffer_handle,
                       uint64_t offset,
                       uint64_t num_bytes,
                       void** buffer,
                       MojoMapBufferFlags flags);
  MojoResult UnmapBuffer(void* buffer);

  // These methods correspond to the API functions defined in
  // "mojo/public/c/system/platform_handle.h".
  MojoResult WrapPlatformHandle(const MojoPlatformHandle* platform_handle,
                                MojoHandle* mojo_handle);
  MojoResult UnwrapPlatformHandle(MojoHandle mojo_handle,
                                  MojoPlatformHandle* platform_handle);
  MojoResult WrapPlatformSharedBufferHandle(
      const MojoPlatformHandle* platform_handle,
      size_t size,
      MojoPlatformSharedBufferHandleFlags flags,
      MojoHandle* mojo_handle);
  MojoResult UnwrapPlatformSharedBufferHandle(
      MojoHandle mojo_handle,
      MojoPlatformHandle* platform_handle,
      size_t* size,
      MojoPlatformSharedBufferHandleFlags* flags);

  void GetActiveHandlesForTest(std::vector<MojoHandle>* handles);

 private:
  // Used to pass ownership of our NodeController over to the IO thread in the
  // event that we're torn down before said thread.
  static void PassNodeControllerToIOThread(
      std::unique_ptr<NodeController> node_controller);

  // Guards node_controller_.
  //
  // TODO(rockot): Consider removing this. It's only needed because we
  // initialize node_controller_ lazily and that may happen on any thread.
  // Otherwise it's effectively const and shouldn't need to be guarded.
  //
  // We can get rid of lazy initialization if we defer Mojo initialization far
  // enough that zygotes don't do it. The zygote can't create a NodeController.
  base::Lock node_controller_lock_;

  // This is lazily initialized on first access. Always use GetNodeController()
  // to access it.
  std::unique_ptr<NodeController> node_controller_;

  // The default callback to invoke, if any, when a process error is reported
  // but cannot be associated with a specific process.
  ProcessErrorCallback default_process_error_callback_;

  base::Lock handles_lock_;
  HandleTable handles_;

  base::Lock mapping_table_lock_;  // Protects |mapping_table_|.
  MappingTable mapping_table_;

  base::Lock property_lock_;
  // Properties that can be read using the MojoGetProperty() API.
  bool property_sync_call_allowed_ = true;

  DISALLOW_COPY_AND_ASSIGN(Core);
};

}  // namespace edk
}  // namespace mojo

#endif  // MOJO_EDK_SYSTEM_CORE_H_
