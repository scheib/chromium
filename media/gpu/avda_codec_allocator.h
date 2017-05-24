// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEDIA_GPU_AVDA_CODEC_ALLOCATOR_H_
#define MEDIA_GPU_AVDA_CODEC_ALLOCATOR_H_

#include <stddef.h>

#include <memory>

#include "base/android/build_info.h"
#include "base/bind.h"
#include "base/logging.h"
#include "base/message_loop/message_loop.h"
#include "base/optional.h"
#include "base/synchronization/waitable_event.h"
#include "base/sys_info.h"
#include "base/threading/thread.h"
#include "base/threading/thread_checker.h"
#include "base/time/tick_clock.h"
#include "base/trace_event/trace_event.h"
#include "media/base/android/android_overlay.h"
#include "media/base/android/media_codec_bridge_impl.h"
#include "media/base/android/media_drm_bridge_cdm_context.h"
#include "media/base/media.h"
#include "media/base/surface_manager.h"
#include "media/base/video_codecs.h"
#include "media/gpu/avda_surface_bundle.h"
#include "media/gpu/media_gpu_export.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gl/android/scoped_java_surface.h"

namespace media {

// For TaskRunnerFor. These are used as vector indices, so please update
// AVDACodecAllocator's constructor if you add / change them.
enum TaskType {
  // Task for an autodetected MediaCodec instance.
  AUTO_CODEC = 0,

  // Task for a software-codec-required MediaCodec.
  SW_CODEC = 1,
};

// Configuration info for MediaCodec.
// This is used to shuttle configuration info between threads without needing
// to worry about the lifetime of the AVDA instance.
class CodecConfig : public base::RefCountedThreadSafe<CodecConfig> {
 public:
  CodecConfig();

  VideoCodec codec = kUnknownVideoCodec;

  // The surface that MediaCodec is configured to output to.
  scoped_refptr<AVDASurfaceBundle> surface_bundle;

  // The MediaCrypto that MediaCodec is configured with for an encrypted stream.
  MediaDrmBridgeCdmContext::JavaObjectPtr media_crypto;

  // Whether the encryption scheme requires us to use a protected surface.
  bool needs_protected_surface = false;

  // The initial coded size. The actual size might change at any time, so this
  // is only a hint.
  gfx::Size initial_expected_coded_size;

  // Whether creating a software decoder backed MediaCodec is forbidden.
  bool software_codec_forbidden = false;

  // Codec specific data (SPS and PPS for H264).
  std::vector<uint8_t> csd0;
  std::vector<uint8_t> csd1;

 protected:
  friend class base::RefCountedThreadSafe<CodecConfig>;
  virtual ~CodecConfig();

 private:
  DISALLOW_COPY_AND_ASSIGN(CodecConfig);
};

class AVDASurfaceAllocatorClient {
 public:
  // Called when the requested SurfaceView becomes available after a call to
  // AllocateSurface()
  virtual void OnSurfaceAvailable(bool success) = 0;

  // Called when the allocated surface is being destroyed. This must either
  // replace the surface with MediaCodec#setSurface, or release the MediaCodec
  // it's attached to. The client no longer owns the surface and doesn't
  // need to call DeallocateSurface();
  virtual void OnSurfaceDestroyed() = 0;

 protected:
  ~AVDASurfaceAllocatorClient() {}
};

class AVDACodecAllocatorClient {
 public:
  // Called on the main thread when a new MediaCodec is configured.
  // |media_codec| will be null if configuration failed.
  virtual void OnCodecConfigured(
      std::unique_ptr<MediaCodecBridge> media_codec) = 0;

 protected:
  ~AVDACodecAllocatorClient() {}
};

// AVDACodecAllocator manages threads for allocating and releasing MediaCodec
// instances.  These activities can hang, depending on android version, due
// to mediaserver bugs.  AVDACodecAllocator detects these cases, and reports
// on them to allow software fallback if the HW path is hung up.
class MEDIA_GPU_EXPORT AVDACodecAllocator {
 public:
  static AVDACodecAllocator* GetInstance();

  // Make sure the construction threads are started for |client|. Returns true
  // on success.
  virtual bool StartThread(AVDACodecAllocatorClient* client);
  virtual void StopThread(AVDACodecAllocatorClient* client);

  // Create and configure a MediaCodec synchronously.
  virtual std::unique_ptr<MediaCodecBridge> CreateMediaCodecSync(
      scoped_refptr<CodecConfig> codec_config);

  // Create and configure a MediaCodec asynchronously. The result is delivered
  // via OnCodecConfigured().
  virtual void CreateMediaCodecAsync(
      base::WeakPtr<AVDACodecAllocatorClient> client,
      scoped_refptr<CodecConfig> codec_config);

  // Asynchronously release |media_codec| with the attached surface.  We will
  // drop our reference to |surface_bundle| on the main thread after the codec
  // is deallocated, since the codec isn't using it anymore.  We will not take
  // other action on it (e.g., calling ReleaseSurfaceTexture if it has one),
  // since some other codec might be going to use it.  We just want to be sure
  // that it outlives |media_codec|.
  // TODO(watk): Bundle the MediaCodec and surface together so you can't get
  // this pairing wrong.
  virtual void ReleaseMediaCodec(
      std::unique_ptr<MediaCodecBridge> media_codec,
      scoped_refptr<AVDASurfaceBundle> surface_bundle);

  // Return true if and only if there is any AVDA registered.
  bool IsAnyRegisteredAVDA();

  // Return a reference to the thread for unit tests.
  base::Thread& GetThreadForTesting(TaskType task_type);

  // Wait for a bounded amount of time for |overlay| to be freed, if it's
  // in use pending release of a codec.  Returns true on success, or false if
  // the wait times out.
  bool WaitForPendingRelease(AndroidOverlay* overlay);

 protected:
  // |tick_clock| and |stop_event| are for tests only.
  AVDACodecAllocator(base::TickClock* tick_clock = nullptr,
                     base::WaitableEvent* stop_event = nullptr);
  virtual ~AVDACodecAllocator();

  // Forward |media_codec|, which is configured to output to |surface_bundle|,
  // to |client| if |client| is still around.  Otherwise, release the codec and
  // then drop our ref to |surface_bundle|.
  void ForwardOrDropCodec(base::WeakPtr<AVDACodecAllocatorClient> client,
                          TaskType task_type,
                          scoped_refptr<AVDASurfaceBundle> surface_bundle,
                          std::unique_ptr<MediaCodecBridge> media_codec);

 private:
  friend class AVDACodecAllocatorTest;

  struct OwnerRecord {
    AVDASurfaceAllocatorClient* owner = nullptr;
    AVDASurfaceAllocatorClient* waiter = nullptr;
  };

  class HangDetector : public base::MessageLoop::TaskObserver {
   public:
    HangDetector(base::TickClock* tick_clock);
    void WillProcessTask(const base::PendingTask& pending_task) override;
    void DidProcessTask(const base::PendingTask& pending_task) override;
    bool IsThreadLikelyHung();

   private:
    base::Lock lock_;

    // Non-null when a task is currently running.
    base::TimeTicks task_start_time_;

    base::TickClock* tick_clock_;

    DISALLOW_COPY_AND_ASSIGN(HangDetector);
  };

  // Handy combination of a thread and hang detector for it.
  struct ThreadAndHangDetector {
    ThreadAndHangDetector(const std::string& name, base::TickClock* tick_clock)
        : thread(name), hang_detector(tick_clock) {}
    base::Thread thread;
    HangDetector hang_detector;
  };

  // Return the task type to use for a new codec allocation, or nullopt if
  // both threads are hung.
  base::Optional<TaskType> TaskTypeForAllocation(bool software_codec_forbidden);

  // Return the task runner for tasks of type |type|.
  scoped_refptr<base::SingleThreadTaskRunner> TaskRunnerFor(TaskType task_type);

  // Called on the gpu main thread when a codec is freed on a codec thread.
  // |surface_bundle| is the surface bundle that the codec was using.
  void OnMediaCodecReleased(scoped_refptr<AVDASurfaceBundle> surface_bundle);

  // Stop the thread indicated by |index|. This signals stop_event_for_testing_
  // after both threads are stopped.
  void StopThreadTask(size_t index);

  // All registered AVDAs.
  std::set<AVDACodecAllocatorClient*> clients_;

  // Waitable events for ongoing release tasks indexed by overlay so we can
  // wait on the codec release if the surface attached to it is being destroyed.
  // This really is needed only for ContentVideoViewOverlay, since it requires
  // synchronous releases with respect to the main thread.
  std::map<AndroidOverlay*, base::WaitableEvent> pending_codec_releases_;

  // Threads for each of TaskType.  They are started / stopped as avda instances
  // show and and request them.  The vector indicies must match TaskType.
  std::vector<ThreadAndHangDetector*> threads_;

  base::ThreadChecker thread_checker_;

  base::WaitableEvent* stop_event_for_testing_;

  // Saves the TaskType used to create a given codec so it can later be released
  // on the same thread.
  std::map<MediaCodecBridge*, TaskType> codec_task_types_;

  // For canceling pending StopThreadTask()s.
  base::WeakPtrFactory<AVDACodecAllocator> weak_this_factory_;

  DISALLOW_COPY_AND_ASSIGN(AVDACodecAllocator);
};

}  // namespace media

#endif  // MEDIA_GPU_AVDA_CODEC_ALLOCATOR_H_
