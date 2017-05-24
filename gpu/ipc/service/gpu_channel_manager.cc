// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gpu/ipc/service/gpu_channel_manager.h"

#include <algorithm>
#include <utility>

#include "base/bind.h"
#include "base/command_line.h"
#include "base/location.h"
#include "base/memory/ptr_util.h"
#include "base/message_loop/message_loop.h"
#include "base/single_thread_task_runner.h"
#include "base/threading/thread_task_runner_handle.h"
#include "build/build_config.h"
#include "gpu/command_buffer/common/sync_token.h"
#include "gpu/command_buffer/service/feature_info.h"
#include "gpu/command_buffer/service/mailbox_manager.h"
#include "gpu/command_buffer/service/memory_program_cache.h"
#include "gpu/command_buffer/service/preemption_flag.h"
#include "gpu/command_buffer/service/scheduler.h"
#include "gpu/command_buffer/service/shader_translator_cache.h"
#include "gpu/command_buffer/service/sync_point_manager.h"
#include "gpu/ipc/common/gpu_messages.h"
#include "gpu/ipc/service/gpu_channel.h"
#include "gpu/ipc/service/gpu_channel_manager_delegate.h"
#include "gpu/ipc/service/gpu_memory_buffer_factory.h"
#include "gpu/ipc/service/gpu_memory_manager.h"
#include "ui/gl/gl_bindings.h"
#include "ui/gl/gl_share_group.h"
#include "ui/gl/init/gl_factory.h"

namespace gpu {

namespace {
#if defined(OS_ANDROID)
// Amount of time we expect the GPU to stay powered up without being used.
const int kMaxGpuIdleTimeMs = 40;
// Maximum amount of time we keep pinging the GPU waiting for the client to
// draw.
const int kMaxKeepAliveTimeMs = 200;
#endif
}

GpuChannelManager::GpuChannelManager(
    const GpuPreferences& gpu_preferences,
    const GpuDriverBugWorkarounds& workarounds,
    GpuChannelManagerDelegate* delegate,
    GpuWatchdogThread* watchdog,
    scoped_refptr<base::SingleThreadTaskRunner> task_runner,
    scoped_refptr<base::SingleThreadTaskRunner> io_task_runner,
    Scheduler* scheduler,
    SyncPointManager* sync_point_manager,
    GpuMemoryBufferFactory* gpu_memory_buffer_factory,
    const GpuFeatureInfo& gpu_feature_info,
    GpuProcessActivityFlags activity_flags)
    : task_runner_(task_runner),
      io_task_runner_(io_task_runner),
      gpu_preferences_(gpu_preferences),
      gpu_driver_bug_workarounds_(workarounds),
      delegate_(delegate),
      watchdog_(watchdog),
      share_group_(new gl::GLShareGroup()),
      mailbox_manager_(gles2::MailboxManager::Create(gpu_preferences)),
      gpu_memory_manager_(this),
      scheduler_(scheduler),
      sync_point_manager_(sync_point_manager),
      gpu_memory_buffer_factory_(gpu_memory_buffer_factory),
      gpu_feature_info_(gpu_feature_info),
      exiting_for_lost_context_(false),
      activity_flags_(std::move(activity_flags)),
      weak_factory_(this) {
  DCHECK(task_runner);
  DCHECK(io_task_runner);
  if (gpu_preferences.ui_prioritize_in_gpu_process)
    preemption_flag_ = new PreemptionFlag;
}

GpuChannelManager::~GpuChannelManager() {
  // Destroy channels before anything else because of dependencies.
  gpu_channels_.clear();
  if (default_offscreen_surface_.get()) {
    default_offscreen_surface_->Destroy();
    default_offscreen_surface_ = NULL;
  }
}

gles2::ProgramCache* GpuChannelManager::program_cache() {
  if (!program_cache_.get() &&
      !gpu_preferences_.disable_gpu_program_cache) {
    const GpuDriverBugWorkarounds& workarounds = gpu_driver_bug_workarounds_;
    bool disable_disk_cache =
        gpu_preferences_.disable_gpu_shader_disk_cache ||
        workarounds.disable_program_disk_cache;
    program_cache_.reset(new gles2::MemoryProgramCache(
        gpu_preferences_.gpu_program_cache_size, disable_disk_cache,
        workarounds.disable_program_caching_for_transform_feedback,
        &activity_flags_));
  }
  return program_cache_.get();
}

gles2::ShaderTranslatorCache* GpuChannelManager::shader_translator_cache() {
  if (!shader_translator_cache_.get()) {
    shader_translator_cache_ =
        new gles2::ShaderTranslatorCache(gpu_preferences_);
  }
  return shader_translator_cache_.get();
}

gles2::FramebufferCompletenessCache*
GpuChannelManager::framebuffer_completeness_cache() {
  if (!framebuffer_completeness_cache_.get())
    framebuffer_completeness_cache_ = new gles2::FramebufferCompletenessCache;
  return framebuffer_completeness_cache_.get();
}

void GpuChannelManager::RemoveChannel(int client_id) {
  delegate_->DidDestroyChannel(client_id);
  gpu_channels_.erase(client_id);
}

GpuChannel* GpuChannelManager::LookupChannel(int32_t client_id) const {
  const auto& it = gpu_channels_.find(client_id);
  return it != gpu_channels_.end() ? it->second.get() : nullptr;
}

GpuChannel* GpuChannelManager::EstablishChannel(int client_id,
                                                uint64_t client_tracing_id,
                                                bool is_gpu_host) {
  std::unique_ptr<GpuChannel> gpu_channel = base::MakeUnique<GpuChannel>(
      this, scheduler_, sync_point_manager_, watchdog_, share_group_,
      mailbox_manager_, &discardable_manager_,
      is_gpu_host ? preemption_flag_ : nullptr,
      is_gpu_host ? nullptr : preemption_flag_, task_runner_, io_task_runner_,
      client_id, client_tracing_id, is_gpu_host);

  GpuChannel* gpu_channel_ptr = gpu_channel.get();
  gpu_channels_[client_id] = std::move(gpu_channel);
  return gpu_channel_ptr;
}

void GpuChannelManager::InternalDestroyGpuMemoryBuffer(
    gfx::GpuMemoryBufferId id,
    int client_id) {
  io_task_runner_->PostTask(
      FROM_HERE,
      base::Bind(&GpuChannelManager::InternalDestroyGpuMemoryBufferOnIO,
                 base::Unretained(this), id, client_id));
}

void GpuChannelManager::InternalDestroyGpuMemoryBufferOnIO(
    gfx::GpuMemoryBufferId id,
    int client_id) {
  gpu_memory_buffer_factory_->DestroyGpuMemoryBuffer(id, client_id);
}

void GpuChannelManager::DestroyGpuMemoryBuffer(gfx::GpuMemoryBufferId id,
                                               int client_id,
                                               const SyncToken& sync_token) {
  if (!sync_point_manager_->WaitOutOfOrder(
          sync_token,
          base::Bind(&GpuChannelManager::InternalDestroyGpuMemoryBuffer,
                     base::Unretained(this), id, client_id))) {
    // No sync token or invalid sync token, destroy immediately.
    InternalDestroyGpuMemoryBuffer(id, client_id);
  }
}

void GpuChannelManager::PopulateShaderCache(const std::string& program_proto) {
  if (program_cache())
    program_cache()->LoadProgram(program_proto);
}

void GpuChannelManager::LoseAllContexts() {
  for (auto& kv : gpu_channels_) {
    kv.second->MarkAllContextsLost();
  }
  task_runner_->PostTask(FROM_HERE,
                         base::Bind(&GpuChannelManager::DestroyAllChannels,
                                    weak_factory_.GetWeakPtr()));
}

void GpuChannelManager::MaybeExitOnContextLost() {
  if (!gpu_preferences().single_process && !gpu_preferences().in_process_gpu) {
    LOG(ERROR) << "Exiting GPU process because some drivers cannot recover"
               << " from problems.";
    // Signal the message loop to quit to shut down other threads
    // gracefully.
    base::MessageLoop::current()->QuitNow();
    exiting_for_lost_context_ = true;
  }
}

void GpuChannelManager::DestroyAllChannels() {
  gpu_channels_.clear();
}

gl::GLSurface* GpuChannelManager::GetDefaultOffscreenSurface() {
  if (!default_offscreen_surface_.get()) {
    default_offscreen_surface_ =
        gl::init::CreateOffscreenGLSurface(gfx::Size());
  }
  return default_offscreen_surface_.get();
}

#if defined(OS_ANDROID)
void GpuChannelManager::DidAccessGpu() {
  last_gpu_access_time_ = base::TimeTicks::Now();
}

void GpuChannelManager::WakeUpGpu() {
  begin_wake_up_time_ = base::TimeTicks::Now();
  ScheduleWakeUpGpu();
}

void GpuChannelManager::ScheduleWakeUpGpu() {
  base::TimeTicks now = base::TimeTicks::Now();
  TRACE_EVENT2("gpu", "GpuChannelManager::ScheduleWakeUp", "idle_time",
               (now - last_gpu_access_time_).InMilliseconds(),
               "keep_awake_time", (now - begin_wake_up_time_).InMilliseconds());
  if (now - last_gpu_access_time_ <
      base::TimeDelta::FromMilliseconds(kMaxGpuIdleTimeMs))
    return;
  if (now - begin_wake_up_time_ >
      base::TimeDelta::FromMilliseconds(kMaxKeepAliveTimeMs))
    return;

  DoWakeUpGpu();

  base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(
      FROM_HERE, base::Bind(&GpuChannelManager::ScheduleWakeUpGpu,
                            weak_factory_.GetWeakPtr()),
      base::TimeDelta::FromMilliseconds(kMaxGpuIdleTimeMs));
}

void GpuChannelManager::DoWakeUpGpu() {
  const GpuCommandBufferStub* stub = nullptr;
  for (const auto& kv : gpu_channels_) {
    const GpuChannel* channel = kv.second.get();
    stub = channel->GetOneStub();
    if (stub) {
      DCHECK(stub->decoder());
      break;
    }
  }
  if (!stub || !stub->decoder()->MakeCurrent())
    return;
  glFinish();
  DidAccessGpu();
}
#endif

}  // namespace gpu
