// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "platform/graphics/OffscreenCanvasFrameDispatcherImpl.h"

#include "cc/output/compositor_frame.h"
#include "cc/quads/texture_draw_quad.h"
#include "gpu/command_buffer/client/gles2_interface.h"
#include "platform/CrossThreadFunctional.h"
#include "platform/Histogram.h"
#include "platform/WebTaskRunner.h"
#include "platform/graphics/OffscreenCanvasPlaceholder.h"
#include "platform/graphics/gpu/SharedGpuContext.h"
#include "platform/wtf/typed_arrays/ArrayBuffer.h"
#include "platform/wtf/typed_arrays/Uint8Array.h"
#include "public/platform/InterfaceProvider.h"
#include "public/platform/Platform.h"
#include "public/platform/WebGraphicsContext3DProvider.h"
#include "public/platform/modules/offscreencanvas/offscreen_canvas_surface.mojom-blink.h"
#include "third_party/khronos/GLES2/gl2.h"
#include "third_party/khronos/GLES2/gl2ext.h"
#include "third_party/skia/include/core/SkColor.h"
#include "third_party/skia/include/core/SkImage.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/transform.h"

namespace blink {

enum {
  kMaxPendingCompositorFrames = 2,
};

OffscreenCanvasFrameDispatcherImpl::OffscreenCanvasFrameDispatcherImpl(
    OffscreenCanvasFrameDispatcherClient* client,
    uint32_t client_id,
    uint32_t sink_id,
    int canvas_id,
    int width,
    int height)
    : OffscreenCanvasFrameDispatcher(client),
      frame_sink_id_(cc::FrameSinkId(client_id, sink_id)),
      width_(width),
      height_(height),
      change_size_for_next_commit_(false),
      needs_begin_frame_(false),
      next_resource_id_(1u),
      binding_(this),
      placeholder_canvas_id_(canvas_id) {
  if (frame_sink_id_.is_valid()) {
    // Only frameless canvas pass an invalid frame sink id; we don't create
    // mojo channel for this special case.
    current_local_surface_id_ = local_surface_id_allocator_.GenerateId();
    DCHECK(!sink_.is_bound());
    mojom::blink::OffscreenCanvasProviderPtr provider;
    Platform::Current()->GetInterfaceProvider()->GetInterface(
        mojo::MakeRequest(&provider));
    provider->CreateCompositorFrameSink(frame_sink_id_,
                                        binding_.CreateInterfacePtrAndBind(),
                                        mojo::MakeRequest(&sink_));
  }
}

OffscreenCanvasFrameDispatcherImpl::~OffscreenCanvasFrameDispatcherImpl() {
}

void OffscreenCanvasFrameDispatcherImpl::SetTransferableResourceToSharedBitmap(
    cc::TransferableResource& resource,
    RefPtr<StaticBitmapImage> image) {
  std::unique_ptr<cc::SharedBitmap> bitmap =
      Platform::Current()->AllocateSharedBitmap(IntSize(width_, height_));
  if (!bitmap)
    return;
  unsigned char* pixels = bitmap->pixels();
  DCHECK(pixels);
  SkImageInfo image_info = SkImageInfo::Make(
      width_, height_, kN32_SkColorType,
      image->IsPremultiplied() ? kPremul_SkAlphaType : kUnpremul_SkAlphaType);
  // TODO(xlai): Optimize to avoid copying pixels. See crbug.com/651456.
  // However, in the case when |image| is texture backed, this function call
  // does a GPU readback which is required.
  image->ImageForCurrentFrame()->readPixels(image_info, pixels,
                                            image_info.minRowBytes(), 0, 0);
  resource.mailbox_holder.mailbox = bitmap->id();
  resource.mailbox_holder.texture_target = 0;
  resource.is_software = true;

  // Hold ref to |bitmap|, to keep it alive until the browser ReclaimResources.
  // It guarantees that the shared bitmap is not re-used or deleted.
  shared_bitmaps_.insert(next_resource_id_, std::move(bitmap));
}

void OffscreenCanvasFrameDispatcherImpl::
    SetTransferableResourceToSharedGPUContext(
        cc::TransferableResource& resource,
        RefPtr<StaticBitmapImage> image) {
  // TODO(crbug.com/652707): When committing the first frame, there is no
  // instance of SharedGpuContext yet, calling SharedGpuContext::gl() will
  // trigger a creation of an instace, which requires to create a
  // WebGraphicsContext3DProvider. This process is quite expensive, because
  // WebGraphicsContext3DProvider can only be constructed on the main thread,
  // and bind to the worker thread if commit() is called on worker. In the
  // subsequent frame, we should already have a SharedGpuContext, then getting
  // the gl interface should not be expensive.
  gpu::gles2::GLES2Interface* gl = SharedGpuContext::Gl();

  SkImageInfo info = SkImageInfo::Make(
      width_, height_, kN32_SkColorType,
      image->IsPremultiplied() ? kPremul_SkAlphaType : kUnpremul_SkAlphaType);
  RefPtr<ArrayBuffer> dst_buffer =
      ArrayBuffer::CreateOrNull(width_ * height_, info.bytesPerPixel());
  // If it fails to create a buffer for copying the pixel data, then exit early.
  if (!dst_buffer)
    return;
  RefPtr<Uint8Array> dst_pixels =
      Uint8Array::Create(dst_buffer, 0, dst_buffer->ByteLength());
  image->ImageForCurrentFrame()->readPixels(info, dst_pixels->Data(),
                                            info.minRowBytes(), 0, 0);

  GLuint texture_id = 0u;
  gl->GenTextures(1, &texture_id);
  gl->BindTexture(GL_TEXTURE_2D, texture_id);
  GLenum format =
      (kN32_SkColorType == kRGBA_8888_SkColorType) ? GL_RGBA : GL_BGRA_EXT;
  gl->TexImage2D(GL_TEXTURE_2D, 0, format, width_, height_, 0, format,
                 GL_UNSIGNED_BYTE, 0);
  gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  gl->TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width_, height_, format,
                    GL_UNSIGNED_BYTE, dst_pixels->Data());

  gpu::Mailbox mailbox;
  gl->GenMailboxCHROMIUM(mailbox.name);
  gl->ProduceTextureCHROMIUM(GL_TEXTURE_2D, mailbox.name);

  const GLuint64 fence_sync = gl->InsertFenceSyncCHROMIUM();
  gl->ShallowFlushCHROMIUM();
  gpu::SyncToken sync_token;
  gl->GenSyncTokenCHROMIUM(fence_sync, sync_token.GetData());

  resource.mailbox_holder =
      gpu::MailboxHolder(mailbox, sync_token, GL_TEXTURE_2D);
  resource.read_lock_fences_enabled = false;
  resource.is_software = false;

  // Hold ref to |textureId| for the piece of GPU memory where the pixel data
  // is uploaded to, to keep it alive until the browser ReclaimResources.
  cached_texture_ids_.insert(next_resource_id_, texture_id);
}

void OffscreenCanvasFrameDispatcherImpl::
    SetTransferableResourceToStaticBitmapImage(
        cc::TransferableResource& resource,
        RefPtr<StaticBitmapImage> image) {
  image->EnsureMailbox();
  resource.mailbox_holder = gpu::MailboxHolder(
      image->GetMailbox(), image->GetSyncToken(), GL_TEXTURE_2D);
  resource.read_lock_fences_enabled = false;
  resource.is_software = false;

  // Hold ref to |image|, to keep it alive until the browser ReclaimResources.
  // It guarantees that the resource is not re-used or deleted.
  cached_images_.insert(next_resource_id_, std::move(image));
}

namespace {

void UpdatePlaceholderImage(WeakPtr<OffscreenCanvasFrameDispatcher> dispatcher,
                            RefPtr<WebTaskRunner> task_runner,
                            int placeholder_canvas_id,
                            RefPtr<blink::StaticBitmapImage> image,
                            unsigned resource_id) {
  DCHECK(IsMainThread());
  OffscreenCanvasPlaceholder* placeholder_canvas =
      OffscreenCanvasPlaceholder::GetPlaceholderById(placeholder_canvas_id);
  if (placeholder_canvas) {
    placeholder_canvas->SetPlaceholderFrame(
        std::move(image), std::move(dispatcher), std::move(task_runner),
        resource_id);
  }
}

}  // namespace

void OffscreenCanvasFrameDispatcherImpl::PostImageToPlaceholder(
    RefPtr<StaticBitmapImage> image) {
  // After this point, |image| can only be used on the main thread, until
  // it is returned.
  image->Transfer();
  RefPtr<WebTaskRunner> dispatcher_task_runner =
      Platform::Current()->CurrentThread()->GetWebTaskRunner();

  Platform::Current()->MainThread()->GetWebTaskRunner()->PostTask(
      BLINK_FROM_HERE,
      CrossThreadBind(UpdatePlaceholderImage, this->CreateWeakPtr(),
                      WTF::Passed(std::move(dispatcher_task_runner)),
                      placeholder_canvas_id_, std::move(image),
                      next_resource_id_));
  spare_resource_locks_.insert(next_resource_id_);
}

void OffscreenCanvasFrameDispatcherImpl::DispatchFrame(
    RefPtr<StaticBitmapImage> image,
    double commit_start_time,
    bool
        is_web_gl_software_rendering /* This flag is true when WebGL's commit is
    called on SwiftShader. */) {
  if (!image || !VerifyImageSize(image->Size()))
    return;
  if (!frame_sink_id_.is_valid()) {
    PostImageToPlaceholder(std::move(image));
    return;
  }
  cc::CompositorFrame frame;
  // TODO(crbug.com/652931): update the device_scale_factor
  frame.metadata.device_scale_factor = 1.0f;
  if (current_begin_frame_ack_.sequence_number ==
      cc::BeginFrameArgs::kInvalidFrameNumber) {
    // TODO(eseckler): This shouldn't be necessary when OffscreenCanvas no
    // longer submits CompositorFrames without prior BeginFrame.
    current_begin_frame_ack_ = cc::BeginFrameAck::CreateManualAckWithDamage();
  } else {
    current_begin_frame_ack_.has_damage = true;
  }
  frame.metadata.begin_frame_ack = current_begin_frame_ack_;

  const gfx::Rect bounds(width_, height_);
  const int kRenderPassId = 1;
  std::unique_ptr<cc::RenderPass> pass = cc::RenderPass::Create();
  pass->SetNew(kRenderPassId, bounds, bounds, gfx::Transform());

  cc::SharedQuadState* sqs = pass->CreateAndAppendSharedQuadState();
  sqs->SetAll(gfx::Transform(), bounds, bounds, bounds, false, 1.f,
              SkBlendMode::kSrcOver, 0);

  cc::TransferableResource resource;
  resource.id = next_resource_id_;
  resource.format = cc::ResourceFormat::RGBA_8888;
  resource.size = gfx::Size(width_, height_);
  // This indicates the filtering on the resource inherently, not the desired
  // filtering effect on the quad.
  resource.filter = GL_NEAREST;
  // TODO(crbug.com/646022): making this overlay-able.
  resource.is_overlay_candidate = false;

  bool yflipped = false;
  OffscreenCanvasCommitType commit_type;
  DEFINE_THREAD_SAFE_STATIC_LOCAL(
      EnumerationHistogram, commit_type_histogram,
      new EnumerationHistogram("OffscreenCanvas.CommitType",
                               kOffscreenCanvasCommitTypeCount));
  if (image->IsTextureBacked()) {
    if (Platform::Current()->IsGPUCompositingEnabled() &&
        !is_web_gl_software_rendering) {
      // Case 1: both canvas and compositor are gpu accelerated.
      commit_type = kCommitGPUCanvasGPUCompositing;
      SetTransferableResourceToStaticBitmapImage(resource, image);
      yflipped = true;
    } else {
      // Case 2: canvas is accelerated but --disable-gpu-compositing is
      // specified, or WebGL's commit is called with SwiftShader. The latter
      // case is indicated by
      // WebGraphicsContext3DProvider::isSoftwareRendering.
      commit_type = kCommitGPUCanvasSoftwareCompositing;
      SetTransferableResourceToSharedBitmap(resource, image);
    }
  } else {
    if (Platform::Current()->IsGPUCompositingEnabled() &&
        !is_web_gl_software_rendering) {
      // Case 3: canvas is not gpu-accelerated, but compositor is
      commit_type = kCommitSoftwareCanvasGPUCompositing;
      SetTransferableResourceToSharedGPUContext(resource, image);
    } else {
      // Case 4: both canvas and compositor are not gpu accelerated.
      commit_type = kCommitSoftwareCanvasSoftwareCompositing;
      SetTransferableResourceToSharedBitmap(resource, image);
    }
  }

  PostImageToPlaceholder(std::move(image));
  commit_type_histogram.Count(commit_type);

  next_resource_id_++;
  frame.resource_list.push_back(std::move(resource));

  cc::TextureDrawQuad* quad =
      pass->CreateAndAppendDrawQuad<cc::TextureDrawQuad>();
  gfx::Size rect_size(width_, height_);

  // TODO(crbug.com/705019): optimize for contexts that have {alpha: false}
  const bool kNeedsBlending = true;
  gfx::Rect opaque_rect(0, 0);

  // TOOD(crbug.com/645993): this should be inherited from WebGL context's
  // creation settings.
  const bool kPremultipliedAlpha = true;
  const gfx::PointF uv_top_left(0.f, 0.f);
  const gfx::PointF uv_bottom_right(1.f, 1.f);
  float vertex_opacity[4] = {1.f, 1.f, 1.f, 1.f};
  // TODO(crbug.com/645994): this should be true when using style
  // "image-rendering: pixelated".
  // TODO(crbug.com/645590): filter should respect the image-rendering CSS
  // property of associated canvas element.
  const bool kNearestNeighbor = false;
  quad->SetAll(sqs, bounds, opaque_rect, bounds, kNeedsBlending, resource.id,
               gfx::Size(), kPremultipliedAlpha, uv_top_left, uv_bottom_right,
               SK_ColorTRANSPARENT, vertex_opacity, yflipped, kNearestNeighbor,
               false);

  frame.render_pass_list.push_back(std::move(pass));

  double elapsed_time = WTF::MonotonicallyIncreasingTime() - commit_start_time;

  switch (commit_type) {
    case kCommitGPUCanvasGPUCompositing:
      if (IsMainThread()) {
        DEFINE_STATIC_LOCAL(
            CustomCountHistogram, commit_gpu_canvas_gpu_compositing_main_timer,
            ("Blink.Canvas.OffscreenCommit.GPUCanvasGPUCompositingMain", 0,
             10000000, 50));
        commit_gpu_canvas_gpu_compositing_main_timer.Count(elapsed_time *
                                                           1000000.0);
      } else {
        DEFINE_THREAD_SAFE_STATIC_LOCAL(
            CustomCountHistogram,
            commit_gpu_canvas_gpu_compositing_worker_timer,
            new CustomCountHistogram(
                "Blink.Canvas.OffscreenCommit.GPUCanvasGPUCompositingWorker", 0,
                10000000, 50));
        commit_gpu_canvas_gpu_compositing_worker_timer.Count(elapsed_time *
                                                             1000000.0);
      }
      break;
    case kCommitGPUCanvasSoftwareCompositing:
      if (IsMainThread()) {
        DEFINE_STATIC_LOCAL(
            CustomCountHistogram,
            commit_gpu_canvas_software_compositing_main_timer,
            ("Blink.Canvas.OffscreenCommit.GPUCanvasSoftwareCompositingMain", 0,
             10000000, 50));
        commit_gpu_canvas_software_compositing_main_timer.Count(elapsed_time *
                                                                1000000.0);
      } else {
        DEFINE_THREAD_SAFE_STATIC_LOCAL(
            CustomCountHistogram,
            commit_gpu_canvas_software_compositing_worker_timer,
            new CustomCountHistogram("Blink.Canvas.OffscreenCommit."
                                     "GPUCanvasSoftwareCompositingWorker",
                                     0, 10000000, 50));
        commit_gpu_canvas_software_compositing_worker_timer.Count(elapsed_time *
                                                                  1000000.0);
      }
      break;
    case kCommitSoftwareCanvasGPUCompositing:
      if (IsMainThread()) {
        DEFINE_STATIC_LOCAL(
            CustomCountHistogram,
            commit_software_canvas_gpu_compositing_main_timer,
            ("Blink.Canvas.OffscreenCommit.SoftwareCanvasGPUCompositingMain", 0,
             10000000, 50));
        commit_software_canvas_gpu_compositing_main_timer.Count(elapsed_time *
                                                                1000000.0);
      } else {
        DEFINE_THREAD_SAFE_STATIC_LOCAL(
            CustomCountHistogram,
            commit_software_canvas_gpu_compositing_worker_timer,
            new CustomCountHistogram("Blink.Canvas.OffscreenCommit."
                                     "SoftwareCanvasGPUCompositingWorker",
                                     0, 10000000, 50));
        commit_software_canvas_gpu_compositing_worker_timer.Count(elapsed_time *
                                                                  1000000.0);
      }
      break;
    case kCommitSoftwareCanvasSoftwareCompositing:
      if (IsMainThread()) {
        DEFINE_STATIC_LOCAL(
            CustomCountHistogram,
            commit_software_canvas_software_compositing_main_timer,
            ("Blink.Canvas.OffscreenCommit."
             "SoftwareCanvasSoftwareCompositingMain",
             0, 10000000, 50));
        commit_software_canvas_software_compositing_main_timer.Count(
            elapsed_time * 1000000.0);
      } else {
        DEFINE_THREAD_SAFE_STATIC_LOCAL(
            CustomCountHistogram,
            commit_software_canvas_software_compositing_worker_timer,
            new CustomCountHistogram("Blink.Canvas.OffscreenCommit."
                                     "SoftwareCanvasSoftwareCompositingWorker",
                                     0, 10000000, 50));
        commit_software_canvas_software_compositing_worker_timer.Count(
            elapsed_time * 1000000.0);
      }
      break;
    case kOffscreenCanvasCommitTypeCount:
      NOTREACHED();
  }

  if (change_size_for_next_commit_) {
    current_local_surface_id_ = local_surface_id_allocator_.GenerateId();
    change_size_for_next_commit_ = false;
  }

  pending_compositor_frames_++;
  sink_->SubmitCompositorFrame(current_local_surface_id_, std::move(frame));
}

void OffscreenCanvasFrameDispatcherImpl::DidReceiveCompositorFrameAck(
    const cc::ReturnedResourceArray& resources) {
  ReclaimResources(resources);
  pending_compositor_frames_--;
  DCHECK_GE(pending_compositor_frames_, 0);
}

void OffscreenCanvasFrameDispatcherImpl::SetNeedsBeginFrame(
    bool needs_begin_frame) {
  if (sink_ && needs_begin_frame != needs_begin_frame_) {
    needs_begin_frame_ = needs_begin_frame;
    sink_->SetNeedsBeginFrame(needs_begin_frame);
  }
}

void OffscreenCanvasFrameDispatcherImpl::OnBeginFrame(
    const cc::BeginFrameArgs& begin_frame_args) {
  DCHECK(Client());

  // TODO(eseckler): Set correct |latest_confirmed_sequence_number|.
  current_begin_frame_ack_ = cc::BeginFrameAck(
      begin_frame_args.source_id, begin_frame_args.sequence_number,
      begin_frame_args.sequence_number, false);

  if (pending_compositor_frames_ >= kMaxPendingCompositorFrames ||
      (begin_frame_args.type == cc::BeginFrameArgs::MISSED &&
       base::TimeTicks::Now() > begin_frame_args.deadline)) {
    sink_->DidNotProduceFrame(current_begin_frame_ack_);
    return;
  }

  Client()->BeginFrame();
  // TODO(eseckler): Tell |m_sink| if we did not draw during the BeginFrame.
  current_begin_frame_ack_.sequence_number =
      cc::BeginFrameArgs::kInvalidFrameNumber;
}

void OffscreenCanvasFrameDispatcherImpl::ReclaimResources(
    const cc::ReturnedResourceArray& resources) {
  for (const auto& resource : resources) {
    RefPtr<StaticBitmapImage> image = cached_images_.at(resource.id);
    if (image) {
      if (image->HasMailbox()) {
        image->UpdateSyncToken(resource.sync_token);
      } else if (SharedGpuContext::IsValid() && resource.sync_token.HasData()) {
        // Although image has MailboxTextureHolder at the time when it is
        // inserted to m_cachedImages, the
        // OffscreenCanvasPlaceHolder::placeholderFrame() exposes this image to
        // everyone accessing the placeholder canvas as an image source, some of
        // which may want to consume the image as a SkImage, thereby converting
        // the MailTextureHolder to a SkiaTextureHolder. In this case, we
        // need to wait for the new sync token passed by CompositorFrameSink.
        SharedGpuContext::Gl()->WaitSyncTokenCHROMIUM(
            resource.sync_token.GetConstData());
      }
    }
    ReclaimResource(resource.id);
  }
}

void OffscreenCanvasFrameDispatcherImpl::ReclaimResource(unsigned resource_id) {
  // An image resource needs to be returned by both the
  // CompositorFrameSink and the HTMLCanvasElement. These
  // events can happen in any order.  The first of the two
  // to return a given resource will result in the spare
  // resource lock being lifted, and the second will delete
  // the resource for real.
  if (spare_resource_locks_.Contains(resource_id)) {
    spare_resource_locks_.erase(resource_id);
    return;
  }
  cached_images_.erase(resource_id);
  shared_bitmaps_.erase(resource_id);
  cached_texture_ids_.erase(resource_id);
}

bool OffscreenCanvasFrameDispatcherImpl::VerifyImageSize(
    const IntSize image_size) {
  if (image_size.Width() == width_ && image_size.Height() == height_)
    return true;
  return false;
}

void OffscreenCanvasFrameDispatcherImpl::Reshape(int width, int height) {
  if (width_ != width || height_ != height) {
    width_ = width;
    height_ = height;
    change_size_for_next_commit_ = true;
  }
}

}  // namespace blink
