/*
 * Copyright (c) 2013, Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "core/imagebitmap/ImageBitmapFactories.h"

#include <memory>

#include "bindings/core/v8/ExceptionState.h"
#include "core/dom/DOMException.h"
#include "core/dom/ExecutionContext.h"
#include "core/fileapi/Blob.h"
#include "core/frame/ImageBitmap.h"
#include "core/frame/LocalDOMWindow.h"
#include "core/frame/UseCounter.h"
#include "core/html/HTMLCanvasElement.h"
#include "core/html/HTMLImageElement.h"
#include "core/html/HTMLVideoElement.h"
#include "core/html/ImageData.h"
#include "core/imagebitmap/ImageBitmapOptions.h"
#include "core/offscreencanvas/OffscreenCanvas.h"
#include "core/svg/SVGImageElement.h"
#include "core/svg/graphics/SVGImage.h"
#include "core/workers/WorkerGlobalScope.h"
#include "platform/CrossThreadFunctional.h"
#include "platform/SharedBuffer.h"
#include "platform/image-decoders/ImageDecoder.h"
#include "platform/threading/BackgroundTaskRunner.h"
#include "public/platform/Platform.h"
#include "public/platform/WebThread.h"
#include "public/platform/WebTraceLocation.h"
#include "v8/include/v8.h"

namespace blink {

static inline ImageBitmapSource* ToImageBitmapSourceInternal(
    const ImageBitmapSourceUnion& value,
    ExceptionState& exception_state,
    const ImageBitmapOptions& options,
    bool has_crop_rect) {
  ImageElementBase* image_element = nullptr;
  if (value.isHTMLImageElement()) {
    if (!(image_element = value.getAsHTMLImageElement()))
      return nullptr;
  } else if (value.isSVGImageElement()) {
    if (!(image_element = value.getAsSVGImageElement()))
      return nullptr;
  }
  if (image_element) {
    if (!image_element->CachedImage()) {
      exception_state.ThrowDOMException(
          kInvalidStateError,
          "No image can be retrieved from the provided element.");
      return nullptr;
    }
    if (image_element->CachedImage()->GetImage()->IsSVGImage()) {
      SVGImage* image = ToSVGImage(image_element->CachedImage()->GetImage());
      if (!image->HasIntrinsicDimensions() &&
          (!has_crop_rect &&
           (!options.hasResizeWidth() || !options.hasResizeHeight()))) {
        exception_state.ThrowDOMException(
            kInvalidStateError,
            "The image element contains an SVG image without intrinsic "
            "dimensions, and no resize options or crop region are specified.");
        return nullptr;
      }
    }
    return image_element;
  }
  if (value.isHTMLVideoElement())
    return value.getAsHTMLVideoElement();
  if (value.isHTMLCanvasElement())
    return value.getAsHTMLCanvasElement();
  if (value.isBlob())
    return value.getAsBlob();
  if (value.isImageData())
    return value.getAsImageData();
  if (value.isImageBitmap())
    return value.getAsImageBitmap();
  if (value.isOffscreenCanvas())
    return value.getAsOffscreenCanvas();
  NOTREACHED();
  return nullptr;
}

ScriptPromise ImageBitmapFactories::CreateImageBitmapFromBlob(
    ScriptState* script_state,
    EventTarget& event_target,
    ImageBitmapSource* bitmap_source,
    Optional<IntRect> crop_rect,
    const ImageBitmapOptions& options,
    ExceptionState& exception_state) {
  if (crop_rect &&
      !ImageBitmap::IsSourceSizeValid(crop_rect->Width(), crop_rect->Height(),
                                      exception_state))
    return ScriptPromise();
  if (!ImageBitmap::IsResizeOptionValid(options, exception_state))
    return ScriptPromise();
  Blob* blob = static_cast<Blob*>(bitmap_source);
  ImageBitmapLoader* loader = ImageBitmapFactories::ImageBitmapLoader::Create(
      From(event_target), crop_rect, options, script_state);
  ScriptPromise promise = loader->Promise();
  From(event_target).AddLoader(loader);
  loader->LoadBlobAsync(event_target.GetExecutionContext(), blob);
  return promise;
}

ScriptPromise ImageBitmapFactories::createImageBitmap(
    ScriptState* script_state,
    EventTarget& event_target,
    const ImageBitmapSourceUnion& bitmap_source,
    const ImageBitmapOptions& options,
    ExceptionState& exception_state) {
  UseCounter::Feature feature = UseCounter::kCreateImageBitmap;
  UseCounter::Count(ExecutionContext::From(script_state), feature);
  ImageBitmapSource* bitmap_source_internal = ToImageBitmapSourceInternal(
      bitmap_source, exception_state, options, false);
  if (!bitmap_source_internal)
    return ScriptPromise();
  return createImageBitmap(script_state, event_target, bitmap_source_internal,
                           Optional<IntRect>(), options, exception_state);
}

ScriptPromise ImageBitmapFactories::createImageBitmap(
    ScriptState* script_state,
    EventTarget& event_target,
    const ImageBitmapSourceUnion& bitmap_source,
    int sx,
    int sy,
    int sw,
    int sh,
    const ImageBitmapOptions& options,
    ExceptionState& exception_state) {
  UseCounter::Feature feature = UseCounter::kCreateImageBitmap;
  UseCounter::Count(ExecutionContext::From(script_state), feature);
  ImageBitmapSource* bitmap_source_internal = ToImageBitmapSourceInternal(
      bitmap_source, exception_state, options, true);
  if (!bitmap_source_internal)
    return ScriptPromise();
  Optional<IntRect> crop_rect = IntRect(sx, sy, sw, sh);
  return createImageBitmap(script_state, event_target, bitmap_source_internal,
                           crop_rect, options, exception_state);
}

ScriptPromise ImageBitmapFactories::createImageBitmap(
    ScriptState* script_state,
    EventTarget& event_target,
    ImageBitmapSource* bitmap_source,
    Optional<IntRect> crop_rect,
    const ImageBitmapOptions& options,
    ExceptionState& exception_state) {
  if (bitmap_source->IsBlob())
    return CreateImageBitmapFromBlob(script_state, event_target, bitmap_source,
                                     crop_rect, options, exception_state);

  return bitmap_source->CreateImageBitmap(script_state, event_target, crop_rect,
                                          options, exception_state);
}

const char* ImageBitmapFactories::SupplementName() {
  return "ImageBitmapFactories";
}

ImageBitmapFactories& ImageBitmapFactories::From(EventTarget& event_target) {
  if (LocalDOMWindow* window = event_target.ToLocalDOMWindow())
    return FromInternal(*window);

  DCHECK(event_target.GetExecutionContext()->IsWorkerGlobalScope());
  return ImageBitmapFactories::FromInternal(
      *ToWorkerGlobalScope(event_target.GetExecutionContext()));
}

template <class GlobalObject>
ImageBitmapFactories& ImageBitmapFactories::FromInternal(GlobalObject& object) {
  ImageBitmapFactories* supplement = static_cast<ImageBitmapFactories*>(
      Supplement<GlobalObject>::From(object, SupplementName()));
  if (!supplement) {
    supplement = new ImageBitmapFactories;
    Supplement<GlobalObject>::ProvideTo(object, SupplementName(), supplement);
  }
  return *supplement;
}

void ImageBitmapFactories::AddLoader(ImageBitmapLoader* loader) {
  pending_loaders_.insert(loader);
}

void ImageBitmapFactories::DidFinishLoading(ImageBitmapLoader* loader) {
  DCHECK(pending_loaders_.Contains(loader));
  pending_loaders_.erase(loader);
}

ImageBitmapFactories::ImageBitmapLoader::ImageBitmapLoader(
    ImageBitmapFactories& factory,
    Optional<IntRect> crop_rect,
    ScriptState* script_state,
    const ImageBitmapOptions& options)
    : loader_(
          FileReaderLoader::Create(FileReaderLoader::kReadAsArrayBuffer, this)),
      factory_(&factory),
      resolver_(ScriptPromiseResolver::Create(script_state)),
      crop_rect_(crop_rect),
      options_(options) {}

void ImageBitmapFactories::ImageBitmapLoader::LoadBlobAsync(
    ExecutionContext* context,
    Blob* blob) {
  loader_->Start(context, blob->GetBlobDataHandle());
}

DEFINE_TRACE(ImageBitmapFactories) {
  visitor->Trace(pending_loaders_);
  Supplement<LocalDOMWindow>::Trace(visitor);
  Supplement<WorkerGlobalScope>::Trace(visitor);
}

void ImageBitmapFactories::ImageBitmapLoader::RejectPromise() {
  resolver_->Reject(DOMException::Create(
      kInvalidStateError, "The source image cannot be decoded."));
  factory_->DidFinishLoading(this);
}

void ImageBitmapFactories::ImageBitmapLoader::DidFinishLoading() {
  DOMArrayBuffer* array_buffer = loader_->ArrayBufferResult();
  if (!array_buffer) {
    RejectPromise();
    return;
  }
  ScheduleAsyncImageBitmapDecoding(array_buffer);
}

void ImageBitmapFactories::ImageBitmapLoader::DidFail(FileError::ErrorCode) {
  RejectPromise();
}

void ImageBitmapFactories::ImageBitmapLoader::ScheduleAsyncImageBitmapDecoding(
    DOMArrayBuffer* array_buffer) {
  RefPtr<WebTaskRunner> task_runner =
      Platform::Current()->CurrentThread()->GetWebTaskRunner();
  BackgroundTaskRunner::PostOnBackgroundThread(
      BLINK_FROM_HERE,
      CrossThreadBind(
          &ImageBitmapFactories::ImageBitmapLoader::DecodeImageOnDecoderThread,
          WrapCrossThreadPersistent(this), std::move(task_runner),
          WrapCrossThreadPersistent(array_buffer), options_.premultiplyAlpha(),
          options_.colorSpaceConversion()));
}

void ImageBitmapFactories::ImageBitmapLoader::DecodeImageOnDecoderThread(
    RefPtr<WebTaskRunner> task_runner,
    DOMArrayBuffer* array_buffer,
    const String& premultiply_alpha_option,
    const String& color_space_conversion_option) {
  DCHECK(!IsMainThread());

  ImageDecoder::AlphaOption alpha_op = ImageDecoder::kAlphaPremultiplied;
  if (premultiply_alpha_option == "none")
    alpha_op = ImageDecoder::kAlphaNotPremultiplied;
  bool ignore_color_space = false;
  if (color_space_conversion_option == "none")
    ignore_color_space = true;
  std::unique_ptr<ImageDecoder> decoder(ImageDecoder::Create(
      SegmentReader::CreateFromSkData(SkData::MakeWithoutCopy(
          array_buffer->Data(), array_buffer->ByteLength())),
      true, alpha_op,
      ignore_color_space ? ColorBehavior::Ignore()
                         : ColorBehavior::TransformToGlobalTarget()));
  sk_sp<SkImage> frame;
  if (decoder) {
    frame = ImageBitmap::GetSkImageFromDecoder(std::move(decoder));
  }
  task_runner->PostTask(
      BLINK_FROM_HERE,
      CrossThreadBind(&ImageBitmapFactories::ImageBitmapLoader::
                          ResolvePromiseOnOriginalThread,
                      WrapCrossThreadPersistent(this), std::move(frame)));
}

void ImageBitmapFactories::ImageBitmapLoader::ResolvePromiseOnOriginalThread(
    sk_sp<SkImage> frame) {
  if (!frame) {
    RejectPromise();
    return;
  }
  DCHECK(frame->width());
  DCHECK(frame->height());

  RefPtr<StaticBitmapImage> image = StaticBitmapImage::Create(std::move(frame));
  image->SetOriginClean(true);
  ImageBitmap* image_bitmap = ImageBitmap::Create(image, crop_rect_, options_);
  if (image_bitmap && image_bitmap->BitmapImage()) {
    resolver_->Resolve(image_bitmap);
  } else {
    RejectPromise();
    return;
  }
  factory_->DidFinishLoading(this);
}

DEFINE_TRACE(ImageBitmapFactories::ImageBitmapLoader) {
  visitor->Trace(factory_);
  visitor->Trace(resolver_);
}

}  // namespace blink
