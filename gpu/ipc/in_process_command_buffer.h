// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GPU_IPC_IN_PROCESS_COMMAND_BUFFER_H_
#define GPU_IPC_IN_PROCESS_COMMAND_BUFFER_H_

#include <stddef.h>
#include <stdint.h>

#include <map>
#include <memory>
#include <vector>

#include "base/atomic_sequence_num.h"
#include "base/callback.h"
#include "base/compiler_specific.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/single_thread_task_runner.h"
#include "base/synchronization/lock.h"
#include "base/synchronization/waitable_event.h"
#include "base/threading/thread.h"
#include "gpu/command_buffer/client/gpu_control.h"
#include "gpu/command_buffer/common/activity_flags.h"
#include "gpu/command_buffer/common/command_buffer.h"
#include "gpu/command_buffer/service/command_executor.h"
#include "gpu/command_buffer/service/context_group.h"
#include "gpu/command_buffer/service/gpu_preferences.h"
#include "gpu/config/gpu_driver_bug_workarounds.h"
#include "gpu/gpu_export.h"
#include "gpu/ipc/service/image_transport_surface_delegate.h"
#include "ui/gfx/gpu_memory_buffer.h"
#include "ui/gfx/native_widget_types.h"
#include "ui/gl/gl_surface.h"
#include "ui/gl/gpu_preference.h"

namespace base {
class SequenceChecker;
}

namespace gl {
class GLContext;
class GLShareGroup;
class GLSurface;
}

namespace gfx {
class Size;
}

namespace gpu {

class ServiceDiscardableManager;
class SyncPointClientState;
class SyncPointOrderData;
class SyncPointManager;
struct GpuProcessHostedCALayerTreeParamsMac;

namespace gles2 {
struct ContextCreationAttribHelper;
class FramebufferCompletenessCache;
class GLES2Decoder;
class MailboxManager;
class ProgramCache;
class ShaderTranslatorCache;
}

class CommandBufferServiceBase;
class GpuMemoryBufferManager;
class CommandExecutor;
class ImageFactory;
class TransferBufferManager;

// This class provides a thread-safe interface to the global GPU service (for
// example GPU thread) when being run in single process mode.
// However, the behavior for accessing one context (i.e. one instance of this
// class) from different client threads is undefined.
class GPU_EXPORT InProcessCommandBuffer : public CommandBuffer,
                                          public GpuControl,
                                          public ImageTransportSurfaceDelegate {
 public:
  class Service;
  typedef base::Callback<void(const std::vector<ui::LatencyInfo>&)>
      LatencyInfoCallback;

  explicit InProcessCommandBuffer(const scoped_refptr<Service>& service);
  ~InProcessCommandBuffer() override;

  // If |surface| is not null, use it directly; in this case, the command
  // buffer gpu thread must be the same as the client thread. Otherwise create
  // a new GLSurface.
  bool Initialize(scoped_refptr<gl::GLSurface> surface,
                  bool is_offscreen,
                  SurfaceHandle window,
                  const gles2::ContextCreationAttribHelper& attribs,
                  InProcessCommandBuffer* share_group,
                  GpuMemoryBufferManager* gpu_memory_buffer_manager,
                  ImageFactory* image_factory,
                  scoped_refptr<base::SingleThreadTaskRunner> task_runner);

  // CommandBuffer implementation:
  State GetLastState() override;
  void Flush(int32_t put_offset) override;
  void OrderingBarrier(int32_t put_offset) override;
  State WaitForTokenInRange(int32_t start, int32_t end) override;
  State WaitForGetOffsetInRange(uint32_t set_get_buffer_count,
                                int32_t start,
                                int32_t end) override;
  void SetGetBuffer(int32_t shm_id) override;
  scoped_refptr<gpu::Buffer> CreateTransferBuffer(size_t size,
                                                  int32_t* id) override;
  void DestroyTransferBuffer(int32_t id) override;

  // GpuControl implementation:
  // NOTE: The GpuControlClient will be called on the client thread.
  void SetGpuControlClient(GpuControlClient*) override;
  gpu::Capabilities GetCapabilities() override;
  int32_t CreateImage(ClientBuffer buffer,
                      size_t width,
                      size_t height,
                      unsigned internalformat) override;
  void DestroyImage(int32_t id) override;
  void SignalQuery(uint32_t query_id, const base::Closure& callback) override;
  void SetLock(base::Lock*) override;
  void EnsureWorkVisible() override;
  CommandBufferNamespace GetNamespaceID() const override;
  CommandBufferId GetCommandBufferID() const override;
  int32_t GetStreamId() const override;
  void FlushOrderingBarrierOnStream(int32_t stream_id) override;
  uint64_t GenerateFenceSyncRelease() override;
  bool IsFenceSyncRelease(uint64_t release) override;
  bool IsFenceSyncFlushed(uint64_t release) override;
  bool IsFenceSyncFlushReceived(uint64_t release) override;
  bool IsFenceSyncReleased(uint64_t release) override;
  void SignalSyncToken(const SyncToken& sync_token,
                       const base::Closure& callback) override;
  void WaitSyncTokenHint(const SyncToken& sync_token) override;
  bool CanWaitUnverifiedSyncToken(const SyncToken& sync_token) override;
  void AddLatencyInfo(
      const std::vector<ui::LatencyInfo>& latency_info) override;

// ImageTransportSurfaceDelegate implementation:
#if defined(OS_WIN)
  void DidCreateAcceleratedSurfaceChildWindow(
      SurfaceHandle parent_window,
      SurfaceHandle child_window) override;
#endif
  void DidSwapBuffersComplete(SwapBuffersCompleteParams params) override;
  const gles2::FeatureInfo* GetFeatureInfo() const override;
  void SetLatencyInfoCallback(const LatencyInfoCallback& callback) override;
  void UpdateVSyncParameters(base::TimeTicks timebase,
                             base::TimeDelta interval) override;

  void AddFilter(IPC::MessageFilter* message_filter) override;
  int32_t GetRouteID() const override;

  using SwapBuffersCompletionCallback = base::Callback<void(
      const std::vector<ui::LatencyInfo>& latency_info,
      gfx::SwapResult result,
      const gpu::GpuProcessHostedCALayerTreeParamsMac* params_mac)>;
  void SetSwapBuffersCompletionCallback(
      const SwapBuffersCompletionCallback& callback);

  using UpdateVSyncParametersCallback =
      base::Callback<void(base::TimeTicks timebase, base::TimeDelta interval)>;
  void SetUpdateVSyncParametersCallback(
      const UpdateVSyncParametersCallback& callback);

  void DidSwapBuffersCompleteOnOriginThread(SwapBuffersCompleteParams params);
  void UpdateVSyncParametersOnOriginThread(base::TimeTicks timebase,
                                           base::TimeDelta interval);

  // The serializer interface to the GPU service (i.e. thread).
  class Service {
   public:
    Service(const gpu::GpuPreferences& gpu_preferences);
    Service(gpu::gles2::MailboxManager* mailbox_manager,
            scoped_refptr<gl::GLShareGroup> share_group);

    virtual ~Service();

    virtual void AddRef() const = 0;
    virtual void Release() const = 0;

    // Queues a task to run as soon as possible.
    virtual void ScheduleTask(const base::Closure& task) = 0;

    // Schedules |callback| to run at an appropriate time for performing delayed
    // work.
    virtual void ScheduleDelayedWork(const base::Closure& task) = 0;

    virtual bool UseVirtualizedGLContexts() = 0;
    virtual scoped_refptr<gles2::ShaderTranslatorCache>
    shader_translator_cache() = 0;
    virtual scoped_refptr<gles2::FramebufferCompletenessCache>
    framebuffer_completeness_cache() = 0;
    virtual SyncPointManager* sync_point_manager() = 0;
    const GpuPreferences& gpu_preferences();
    const GpuDriverBugWorkarounds& gpu_driver_bug_workarounds();
    scoped_refptr<gl::GLShareGroup> share_group();
    scoped_refptr<gles2::MailboxManager> mailbox_manager();
    gpu::gles2::ProgramCache* program_cache();
    ServiceDiscardableManager* discardable_manager();
    virtual bool BlockThreadOnWaitSyncToken() const = 0;

   protected:
    const GpuPreferences gpu_preferences_;
    const GpuDriverBugWorkarounds gpu_driver_bug_workarounds_;
    scoped_refptr<gles2::MailboxManager> mailbox_manager_;
    scoped_refptr<gl::GLShareGroup> share_group_;
    std::unique_ptr<gpu::gles2::ProgramCache> program_cache_;
    // No-op default initialization is used in in-process mode.
    GpuProcessActivityFlags activity_flags_;
    std::unique_ptr<ServiceDiscardableManager> discardable_manager_;
  };

 private:
  struct InitializeOnGpuThreadParams {
    bool is_offscreen;
    SurfaceHandle window;
    const gles2::ContextCreationAttribHelper& attribs;
    gpu::Capabilities* capabilities;  // Ouptut.
    InProcessCommandBuffer* context_group;
    ImageFactory* image_factory;

    InitializeOnGpuThreadParams(
        bool is_offscreen,
        SurfaceHandle window,
        const gles2::ContextCreationAttribHelper& attribs,
        gpu::Capabilities* capabilities,
        InProcessCommandBuffer* share_group,
        ImageFactory* image_factory)
        : is_offscreen(is_offscreen),
          window(window),
          attribs(attribs),
          capabilities(capabilities),
          context_group(share_group),
          image_factory(image_factory) {}
  };

  bool InitializeOnGpuThread(const InitializeOnGpuThreadParams& params);
  void Destroy();
  bool DestroyOnGpuThread();
  void FlushOnGpuThread(int32_t put_offset,
                        std::vector<ui::LatencyInfo>* latency_info);
  void UpdateLastStateOnGpuThread();
  void ScheduleDelayedWorkOnGpuThread();
  bool MakeCurrent();
  base::Closure WrapCallback(const base::Closure& callback);
  void QueueTask(bool out_of_order, const base::Closure& task);
  void ProcessTasksOnGpuThread();
  void CheckSequencedThread();
  void FenceSyncReleaseOnGpuThread(uint64_t release);
  bool WaitSyncTokenOnGpuThread(const SyncToken& sync_token);
  void OnWaitSyncTokenCompleted(const SyncToken& sync_token);
  void DescheduleUntilFinishedOnGpuThread();
  void RescheduleAfterFinishedOnGpuThread();
  void SignalSyncTokenOnGpuThread(const SyncToken& sync_token,
                                  const base::Closure& callback);
  void SignalQueryOnGpuThread(unsigned query_id, const base::Closure& callback);
  void DestroyTransferBufferOnGpuThread(int32_t id);
  void CreateImageOnGpuThread(int32_t id,
                              const gfx::GpuMemoryBufferHandle& handle,
                              const gfx::Size& size,
                              gfx::BufferFormat format,
                              uint32_t internalformat,
                              // uint32_t order_num,
                              uint64_t fence_sync);
  void DestroyImageOnGpuThread(int32_t id);
  void SetGetBufferOnGpuThread(int32_t shm_id, base::WaitableEvent* completion);

  // Callbacks on the gpu thread.
  void OnContextLostOnGpuThread();
  void PumpCommandsOnGpuThread();
  void PerformDelayedWorkOnGpuThread();
  // Callback implementations on the client thread.
  void OnContextLost();

  const CommandBufferId command_buffer_id_;

  // Members accessed on the gpu thread (possibly with the exception of
  // creation):
  bool waiting_for_sync_point_ = false;

  scoped_refptr<base::SingleThreadTaskRunner> origin_task_runner_;
  std::unique_ptr<TransferBufferManager> transfer_buffer_manager_;
  std::unique_ptr<CommandExecutor> executor_;
  std::unique_ptr<gles2::GLES2Decoder> decoder_;
  scoped_refptr<gl::GLContext> context_;
  scoped_refptr<gl::GLSurface> surface_;
  scoped_refptr<SyncPointOrderData> sync_point_order_data_;
  scoped_refptr<SyncPointClientState> sync_point_client_state_;
  base::Closure context_lost_callback_;
  // Used to throttle PerformDelayedWorkOnGpuThread.
  bool delayed_work_pending_;
  ImageFactory* image_factory_;

  LatencyInfoCallback latency_info_callback_;

  // Should only be accessed on the client thread.
  std::unique_ptr<std::vector<ui::LatencyInfo>> latency_info_;

  // Members accessed on the client thread:
  GpuControlClient* gpu_control_client_;
#if DCHECK_IS_ON()
  bool context_lost_;
#endif
  State last_state_;
  base::Lock last_state_lock_;
  int32_t last_put_offset_;
  gpu::Capabilities capabilities_;
  GpuMemoryBufferManager* gpu_memory_buffer_manager_;
  base::AtomicSequenceNumber next_image_id_;
  uint64_t next_fence_sync_release_;
  uint64_t flushed_fence_sync_release_;

  // Accessed on both threads:
  std::unique_ptr<CommandBufferServiceBase> command_buffer_;
  base::Lock command_buffer_lock_;
  base::WaitableEvent flush_event_;
  scoped_refptr<Service> service_;

  // The group of contexts that share namespaces with this context.
  scoped_refptr<gles2::ContextGroup> context_group_;

  scoped_refptr<gl::GLShareGroup> gl_share_group_;
  base::WaitableEvent fence_sync_wait_event_;

  // Only used with explicit scheduling and the gpu thread is the same as
  // the client thread.
  std::unique_ptr<base::SequenceChecker> sequence_checker_;

  base::Lock task_queue_lock_;
  struct GpuTask {
    GpuTask(const base::Closure& callback, uint32_t order_number);
    ~GpuTask();
    base::Closure callback;
    uint32_t order_number;
  };
  std::queue<std::unique_ptr<GpuTask>> task_queue_;

  SwapBuffersCompletionCallback swap_buffers_completion_callback_;
  UpdateVSyncParametersCallback update_vsync_parameters_completion_callback_;

  base::WeakPtr<InProcessCommandBuffer> client_thread_weak_ptr_;
  base::WeakPtr<InProcessCommandBuffer> gpu_thread_weak_ptr_;
  base::WeakPtrFactory<InProcessCommandBuffer> client_thread_weak_ptr_factory_;
  base::WeakPtrFactory<InProcessCommandBuffer> gpu_thread_weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(InProcessCommandBuffer);
};

}  // namespace gpu

#endif  // GPU_IPC_IN_PROCESS_COMMAND_BUFFER_H_
