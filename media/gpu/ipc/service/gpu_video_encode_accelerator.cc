// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/gpu/ipc/service/gpu_video_encode_accelerator.h"

#include <memory>

#include "base/callback.h"
#include "base/command_line.h"
#include "base/logging.h"
#include "base/memory/ptr_util.h"
#include "base/memory/shared_memory.h"
#include "base/numerics/safe_math.h"
#include "base/sys_info.h"
#include "base/threading/thread_task_runner_handle.h"
#include "build/build_config.h"
#include "gpu/ipc/service/gpu_channel.h"
#include "gpu/ipc/service/gpu_channel_manager.h"
#include "ipc/ipc_message_macros.h"
#include "media/base/bind_to_current_loop.h"
#include "media/base/limits.h"
#include "media/base/video_frame.h"
#include "media/gpu/gpu_video_accelerator_util.h"
#include "media/gpu/gpu_video_encode_accelerator_factory.h"
#include "media/gpu/ipc/common/media_messages.h"
#include "media/media_features.h"


namespace media {

namespace {

bool MakeDecoderContextCurrent(
    const base::WeakPtr<gpu::GpuCommandBufferStub> stub) {
  if (!stub) {
    DLOG(ERROR) << "Stub is gone; won't MakeCurrent().";
    return false;
  }

  if (!stub->decoder()->MakeCurrent()) {
    DLOG(ERROR) << "Failed to MakeCurrent()";
    return false;
  }

  return true;
}

void DropSharedMemory(std::unique_ptr<base::SharedMemory> shm) {
  // Just let |shm| fall out of scope.
}

}  // anonymous namespace

class GpuVideoEncodeAccelerator::MessageFilter : public IPC::MessageFilter {
 public:
  MessageFilter(GpuVideoEncodeAccelerator* owner, int32_t host_route_id)
      : owner_(owner), host_route_id_(host_route_id) {}

  void OnChannelError() override { sender_ = nullptr; }

  void OnChannelClosing() override { sender_ = nullptr; }

  void OnFilterAdded(IPC::Channel* channel) override { sender_ = channel; }

  void OnFilterRemoved() override { owner_->OnFilterRemoved(); }

  bool OnMessageReceived(const IPC::Message& msg) override {
    if (msg.routing_id() != host_route_id_)
      return false;

    IPC_BEGIN_MESSAGE_MAP(MessageFilter, msg)
      IPC_MESSAGE_FORWARD(AcceleratedVideoEncoderMsg_Encode, owner_,
                          GpuVideoEncodeAccelerator::OnEncode)
      IPC_MESSAGE_FORWARD(AcceleratedVideoEncoderMsg_UseOutputBitstreamBuffer,
                          owner_,
                          GpuVideoEncodeAccelerator::OnUseOutputBitstreamBuffer)
      IPC_MESSAGE_FORWARD(
          AcceleratedVideoEncoderMsg_RequestEncodingParametersChange, owner_,
          GpuVideoEncodeAccelerator::OnRequestEncodingParametersChange)
      IPC_MESSAGE_UNHANDLED(return false)
    IPC_END_MESSAGE_MAP()
    return true;
  }

  bool SendOnIOThread(IPC::Message* message) {
    if (!sender_ || message->is_sync()) {
      DCHECK(!message->is_sync());
      delete message;
      return false;
    }
    return sender_->Send(message);
  }

 protected:
  ~MessageFilter() override {}

 private:
  GpuVideoEncodeAccelerator* const owner_;
  const int32_t host_route_id_;
  // The sender to which this filter was added.
  IPC::Sender* sender_ = nullptr;

  DISALLOW_COPY_AND_ASSIGN(MessageFilter);
};

GpuVideoEncodeAccelerator::GpuVideoEncodeAccelerator(
    int32_t host_route_id,
    gpu::GpuCommandBufferStub* stub,
    const scoped_refptr<base::SingleThreadTaskRunner>& io_task_runner)
    : host_route_id_(host_route_id),
      stub_(stub),
      input_format_(PIXEL_FORMAT_UNKNOWN),
      output_buffer_size_(0),
      filter_removed_(base::WaitableEvent::ResetPolicy::MANUAL,
                      base::WaitableEvent::InitialState::NOT_SIGNALED),
      encoder_worker_thread_("EncoderWorkerThread"),
      main_task_runner_(base::ThreadTaskRunnerHandle::Get()),
      io_task_runner_(io_task_runner),
      encode_task_runner_(main_task_runner_),
      weak_this_factory_for_encoder_worker_(this),
      weak_this_factory_(this) {
  weak_this_for_encoder_worker_ =
      weak_this_factory_for_encoder_worker_.GetWeakPtr();
  weak_this_ = weak_this_factory_.GetWeakPtr();
  stub_->AddDestructionObserver(this);
  make_context_current_ =
      base::Bind(&MakeDecoderContextCurrent, stub_->AsWeakPtr());
}

GpuVideoEncodeAccelerator::~GpuVideoEncodeAccelerator() {
  // This class can only be self-deleted from OnWillDestroyStub(), which means
  // the VEA has already been destroyed in there.
  DCHECK(!encoder_);
  DCHECK(!encoder_worker_thread_.IsRunning());
}

bool GpuVideoEncodeAccelerator::Initialize(VideoPixelFormat input_format,
                                           const gfx::Size& input_visible_size,
                                           VideoCodecProfile output_profile,
                                           uint32_t initial_bitrate) {
  DCHECK(main_task_runner_->BelongsToCurrentThread());
  DVLOG(1) << __func__
           << " input_format=" << VideoPixelFormatToString(input_format)
           << ", input_visible_size=" << input_visible_size.ToString()
           << ", output_profile=" << GetProfileName(output_profile)
           << ", initial_bitrate=" << initial_bitrate;
  DCHECK(!encoder_);

  if (!stub_->channel()->AddRoute(host_route_id_, stub_->sequence_id(), this)) {
    DLOG(ERROR) << __func__ << " failed to add route";
    return false;
  }

  if (input_visible_size.width() > limits::kMaxDimension ||
      input_visible_size.height() > limits::kMaxDimension ||
      input_visible_size.GetArea() > limits::kMaxCanvas) {
    DLOG(ERROR) << __func__ << "too large input_visible_size "
                << input_visible_size.ToString();
    return false;
  }

  const gpu::GpuPreferences& gpu_preferences =
      stub_->channel()->gpu_channel_manager()->gpu_preferences();

  encoder_ = GpuVideoEncodeAcceleratorFactory::CreateVEA(
      input_format, input_visible_size, output_profile, initial_bitrate, this,
      gpu_preferences);
  if (!encoder_) {
    DLOG(ERROR) << __func__ << " Could not create VEA";
    return false;
  }

  if (!encoder_worker_thread_.Start()) {
    encoder_.reset();
    DLOG(ERROR) << "Failed spawning encoder worker thread.";
    return false;
  }

  input_format_ = input_format;
  input_visible_size_ = input_visible_size;
  // Attempt to set up performing encoding tasks on IO thread, if supported
  // by the VEA.
  if (encoder_->TryToSetupEncodeOnSeparateThread(weak_this_, io_task_runner_)) {
    filter_ = new MessageFilter(this, host_route_id_);
    stub_->channel()->AddFilter(filter_.get());
    encode_task_runner_ = io_task_runner_;
  }
  encoder_worker_task_runner_ = encoder_worker_thread_.task_runner();
  return true;
}

bool GpuVideoEncodeAccelerator::OnMessageReceived(const IPC::Message& message) {
  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP(GpuVideoEncodeAccelerator, message)
    IPC_MESSAGE_HANDLER(AcceleratedVideoEncoderMsg_Encode, OnEncode)
    IPC_MESSAGE_HANDLER(AcceleratedVideoEncoderMsg_UseOutputBitstreamBuffer,
                        OnUseOutputBitstreamBuffer)
    IPC_MESSAGE_HANDLER(
        AcceleratedVideoEncoderMsg_RequestEncodingParametersChange,
        OnRequestEncodingParametersChange)
    IPC_MESSAGE_HANDLER(AcceleratedVideoEncoderMsg_Destroy, OnDestroy)
    IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP()
  return handled;
}

bool GpuVideoEncodeAccelerator::Send(IPC::Message* message) {
  if (filter_ && io_task_runner_->BelongsToCurrentThread()) {
    return filter_->SendOnIOThread(message);
  }
  DCHECK(main_task_runner_->BelongsToCurrentThread());
  return stub_->channel()->Send(message);
}

void GpuVideoEncodeAccelerator::RequireBitstreamBuffers(
    unsigned int input_count,
    const gfx::Size& input_coded_size,
    size_t output_buffer_size) {
  DCHECK(main_task_runner_->BelongsToCurrentThread());
  if (!Send(new AcceleratedVideoEncoderHostMsg_RequireBitstreamBuffers(
          host_route_id_, input_count, input_coded_size, output_buffer_size))) {
    DLOG(ERROR) << __func__ << " failed.";
    return;
  }
  input_coded_size_ = input_coded_size;
  output_buffer_size_ = output_buffer_size;
}

void GpuVideoEncodeAccelerator::BitstreamBufferReady(
    int32_t bitstream_buffer_id,
    size_t payload_size,
    bool key_frame,
    base::TimeDelta timestamp) {
  DCHECK(CheckIfCalledOnCorrectThread());
  if (!Send(new AcceleratedVideoEncoderHostMsg_BitstreamBufferReady(
          host_route_id_, bitstream_buffer_id, payload_size, key_frame,
          timestamp))) {
    DLOG(ERROR) << __func__ << " failed.";
  }
}

void GpuVideoEncodeAccelerator::NotifyError(
    VideoEncodeAccelerator::Error error) {
  if (!Send(new AcceleratedVideoEncoderHostMsg_NotifyError(host_route_id_,
                                                           error))) {
    DLOG(ERROR) << __func__ << " failed.";
  }
}

void GpuVideoEncodeAccelerator::OnWillDestroyStub() {
  DVLOG(2) << __func__;
  DCHECK(main_task_runner_->BelongsToCurrentThread());
  DCHECK(stub_);

  // The stub is going away, so we have to stop and destroy VEA here before
  // returning. We cannot destroy the VEA before the IO thread message filter is
  // removed however, since we cannot service incoming messages with VEA gone.
  // We cannot simply check for existence of VEA on IO thread though, because
  // we don't want to synchronize the IO thread with the ChildThread.
  // So we have to wait for the RemoveFilter callback here instead and remove
  // the VEA after it arrives and before returning.
  if (filter_) {
    stub_->channel()->RemoveFilter(filter_.get());
    filter_removed_.Wait();
  }

  // We should stop |encoder_worker_thread_| before releasing |encoder_|, see
  // crbug.com/715759.
  if (encoder_worker_thread_.IsRunning()) {
    encoder_worker_task_runner_->PostTask(
        FROM_HERE,
        base::Bind(&GpuVideoEncodeAccelerator::DestroyOnEncoderWorker,
                   weak_this_for_encoder_worker_));
    encoder_worker_thread_.Stop();
  }

  stub_->channel()->RemoveRoute(host_route_id_);
  stub_->RemoveDestructionObserver(this);
  encoder_.reset();
  delete this;
}

// static
gpu::VideoEncodeAcceleratorSupportedProfiles
GpuVideoEncodeAccelerator::GetSupportedProfiles(
    const gpu::GpuPreferences& gpu_preferences) {
  return GpuVideoAcceleratorUtil::ConvertMediaToGpuEncodeProfiles(
      GpuVideoEncodeAcceleratorFactory::GetSupportedProfiles(gpu_preferences));
}

void GpuVideoEncodeAccelerator::OnFilterRemoved() {
  DVLOG(2) << __func__;
  DCHECK(io_task_runner_->BelongsToCurrentThread());

  // We're destroying; cancel all callbacks.
  weak_this_factory_.InvalidateWeakPtrs();
  filter_removed_.Signal();
}

void GpuVideoEncodeAccelerator::OnEncode(
    const AcceleratedVideoEncoderMsg_Encode_Params& params) {
  DVLOG(3) << __func__ << " frame_id = " << params.frame_id
           << ", buffer_size=" << params.buffer_size
           << ", force_keyframe=" << params.force_keyframe;
  DCHECK(CheckIfCalledOnCorrectThread());
  DCHECK_EQ(PIXEL_FORMAT_I420, input_format_);

  if (!encoder_)
    return;

  if (params.frame_id < 0) {
    DLOG(ERROR) << __func__ << " invalid frame_id=" << params.frame_id;
    NotifyError(VideoEncodeAccelerator::kPlatformFailureError);
    return;
  }

  encoder_worker_task_runner_->PostTask(
      FROM_HERE,
      base::Bind(&GpuVideoEncodeAccelerator::CreateEncodeFrameOnEncoderWorker,
                 weak_this_for_encoder_worker_, params));
}

void GpuVideoEncodeAccelerator::OnUseOutputBitstreamBuffer(
    int32_t buffer_id,
    base::SharedMemoryHandle buffer_handle,
    uint32_t buffer_size) {
  DVLOG(3) << __func__ << " buffer_id=" << buffer_id
           << ", buffer_size=" << buffer_size;
  DCHECK(CheckIfCalledOnCorrectThread());
  if (!encoder_)
    return;
  if (buffer_id < 0) {
    DLOG(ERROR) << __func__ << " invalid buffer_id=" << buffer_id;
    NotifyError(VideoEncodeAccelerator::kPlatformFailureError);
    return;
  }
  if (buffer_size < output_buffer_size_) {
    DLOG(ERROR) << __func__ << " buffer too small for buffer_id=" << buffer_id;
    NotifyError(VideoEncodeAccelerator::kPlatformFailureError);
    return;
  }
  encoder_->UseOutputBitstreamBuffer(
      BitstreamBuffer(buffer_id, buffer_handle, buffer_size));
}

void GpuVideoEncodeAccelerator::OnDestroy() {
  DVLOG(2) << __func__;
  DCHECK(main_task_runner_->BelongsToCurrentThread());
  OnWillDestroyStub();
}

void GpuVideoEncodeAccelerator::OnRequestEncodingParametersChange(
    uint32_t bitrate,
    uint32_t framerate) {
  DVLOG(2) << __func__ << " bitrate=" << bitrate << ", framerate=" << framerate;
  DCHECK(CheckIfCalledOnCorrectThread());
  if (!encoder_)
    return;
  encoder_->RequestEncodingParametersChange(bitrate, framerate);
}

void GpuVideoEncodeAccelerator::CreateEncodeFrameOnEncoderWorker(
    const AcceleratedVideoEncoderMsg_Encode_Params& params) {
  DVLOG(3) << __func__;
  DCHECK(encoder_worker_task_runner_->BelongsToCurrentThread());

  // Wrap into a SharedMemory in the beginning, so that |params.buffer_handle|
  // is cleaned properly in case of an early return.
  std::unique_ptr<base::SharedMemory> shm(
      new base::SharedMemory(params.buffer_handle, true));
  const uint32_t aligned_offset =
      params.buffer_offset % base::SysInfo::VMAllocationGranularity();
  base::CheckedNumeric<off_t> map_offset = params.buffer_offset;
  map_offset -= aligned_offset;
  base::CheckedNumeric<size_t> map_size = params.buffer_size;
  map_size += aligned_offset;

  if (!map_offset.IsValid() || !map_size.IsValid()) {
    DLOG(ERROR) << __func__ << "  invalid map_offset or map_size";
    encode_task_runner_->PostTask(
        FROM_HERE,
        base::Bind(&GpuVideoEncodeAccelerator::NotifyError, weak_this_,
                   VideoEncodeAccelerator::kPlatformFailureError));
    return;
  }

  if (!shm->MapAt(map_offset.ValueOrDie(), map_size.ValueOrDie())) {
    DLOG(ERROR) << __func__ << " could not map frame_id=" << params.frame_id;
    encode_task_runner_->PostTask(
        FROM_HERE,
        base::Bind(&GpuVideoEncodeAccelerator::NotifyError, weak_this_,
                   VideoEncodeAccelerator::kPlatformFailureError));
    return;
  }

  uint8_t* shm_memory =
      reinterpret_cast<uint8_t*>(shm->memory()) + aligned_offset;
  scoped_refptr<VideoFrame> frame = VideoFrame::WrapExternalSharedMemory(
      input_format_, input_coded_size_, gfx::Rect(input_visible_size_),
      input_visible_size_, shm_memory, params.buffer_size, params.buffer_handle,
      params.buffer_offset, params.timestamp);
  if (!frame) {
    DLOG(ERROR) << __func__ << " could not create a frame";
    encode_task_runner_->PostTask(
        FROM_HERE,
        base::Bind(&GpuVideoEncodeAccelerator::NotifyError, weak_this_,
                   VideoEncodeAccelerator::kPlatformFailureError));
    return;
  }

  // We wrap |shm| in a callback and add it as a destruction observer, so it
  // stays alive and mapped until |frame| goes out of scope.
  frame->AddDestructionObserver(
      base::Bind(&DropSharedMemory, base::Passed(&shm)));
  encode_task_runner_->PostTask(
      FROM_HERE,
      base::Bind(&GpuVideoEncodeAccelerator::OnEncodeFrameCreated, weak_this_,
                 params.frame_id, params.force_keyframe, frame));
}

void GpuVideoEncodeAccelerator::DestroyOnEncoderWorker() {
  DCHECK(encoder_worker_task_runner_->BelongsToCurrentThread());
  weak_this_factory_for_encoder_worker_.InvalidateWeakPtrs();
}

void GpuVideoEncodeAccelerator::OnEncodeFrameCreated(
    int32_t frame_id,
    bool force_keyframe,
    const scoped_refptr<media::VideoFrame>& frame) {
  DVLOG(3) << __func__;
  DCHECK(CheckIfCalledOnCorrectThread());

  if (filter_removed_.IsSignaled())
    return;

  if (!frame) {
    DLOG(ERROR) << __func__ << " could not create a frame";
    NotifyError(VideoEncodeAccelerator::kPlatformFailureError);
    return;
  }

  frame->AddDestructionObserver(BindToCurrentLoop(
      base::Bind(&GpuVideoEncodeAccelerator::EncodeFrameFinished,
                 weak_this_factory_.GetWeakPtr(), frame_id)));
  encoder_->Encode(frame, force_keyframe);
}

void GpuVideoEncodeAccelerator::EncodeFrameFinished(int32_t frame_id) {
  DCHECK(CheckIfCalledOnCorrectThread());
  if (!Send(new AcceleratedVideoEncoderHostMsg_NotifyInputDone(host_route_id_,
                                                               frame_id))) {
    DLOG(ERROR) << __func__ << " failed.";
  }
}

bool GpuVideoEncodeAccelerator::CheckIfCalledOnCorrectThread() {
  return (filter_ && io_task_runner_->BelongsToCurrentThread()) ||
         (!filter_ && main_task_runner_->BelongsToCurrentThread());
}

}  // namespace media
