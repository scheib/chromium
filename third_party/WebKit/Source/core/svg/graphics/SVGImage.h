/*
 * Copyright (C) 2006 Eric Seidel <eric@webkit.org>
 * Copyright (C) 2009 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef SVGImage_h
#define SVGImage_h

#include "core/CoreExport.h"
#include "platform/graphics/Image.h"
#include "platform/graphics/paint/PaintRecord.h"
#include "platform/heap/Handle.h"
#include "platform/weborigin/KURL.h"
#include "platform/wtf/Allocator.h"
#include "third_party/skia/include/core/SkRefCnt.h"

namespace blink {

class Document;
class Page;
class PaintController;
class LayoutReplaced;
class SVGImageChromeClient;
class SVGImageForContainer;

// SVGImage does not use Skia to draw images (as BitmapImage does) but instead
// handles drawing itself. Internally, SVGImage creates a detached & sandboxed
// Page containing an SVGDocument and reuses the existing paint code in Blink to
// draw the image. Because a single SVGImage can be referenced by multiple
// containers (see: SVGImageForContainer.h), each call to SVGImage::draw() may
// require (re-)laying out the inner SVGDocument.
//
// Using Page was an architectural hack and has surprising side-effects. Ideally
// SVGImage would use a lighter container around an SVGDocument that does not
// have the full Page machinery but still has the sandboxing security guarantees
// needed by SVGImage.
class CORE_EXPORT SVGImage final : public Image {
 public:
  static PassRefPtr<SVGImage> Create(ImageObserver* observer) {
    return AdoptRef(new SVGImage(observer));
  }

  static bool IsInSVGImage(const Node*);

  LayoutReplaced* EmbeddedReplacedContent() const;

  bool IsSVGImage() const override { return true; }
  IntSize Size() const override { return intrinsic_size_; }

  void CheckLoaded() const;
  bool CurrentFrameHasSingleSecurityOrigin() const override;

  void StartAnimation(CatchUpAnimation = kCatchUp) override;
  void ResetAnimation() override;

  // Does the SVG image/document contain any animations?
  bool MaybeAnimated() override;

  // Advances an animated image. This will trigger an animation update for CSS
  // and advance the SMIL timeline by one frame.
  void AdvanceAnimationForTesting() override;
  SVGImageChromeClient& ChromeClientForTesting();

  sk_sp<SkImage> ImageForCurrentFrame() override;
  static FloatPoint OffsetForCurrentFrame(const FloatRect& dst_rect,
                                          const FloatRect& src_rect);

  // Service CSS and SMIL animations.
  void ServiceAnimations(double monotonic_animation_start_time);

  void UpdateUseCounters(const Document&) const;

  // The defaultObjectSize is assumed to be unzoomed, i.e. it should
  // not have the effective zoom level applied. The returned size is
  // thus also independent of current zoom level.
  FloatSize ConcreteObjectSize(const FloatSize& default_object_size) const;

  bool HasIntrinsicDimensions() const;

 private:
  // Accesses m_page.
  friend class SVGImageChromeClient;
  // Forwards calls to the various *ForContainer methods and other parts of
  // the the Image interface.
  friend class SVGImageForContainer;

  SVGImage(ImageObserver*);
  ~SVGImage() override;

  String FilenameExtension() const override;

  IntSize ContainerSize() const;
  bool UsesContainerSize() const override { return true; }

  SizeAvailability DataChanged(bool all_data_received) override;

  // FIXME: SVGImages are underreporting decoded sizes and will be unable
  // to prune because these functions are not implemented yet.
  void DestroyDecodedData() override {}

  // FIXME: Implement this to be less conservative.
  bool CurrentFrameKnownToBeOpaque(
      MetadataMode = kUseCurrentMetadata) override {
    return false;
  }

  void Draw(PaintCanvas*,
            const PaintFlags&,
            const FloatRect& from_rect,
            const FloatRect& to_rect,
            RespectImageOrientationEnum,
            ImageClampingMode) override;
  void DrawForContainer(PaintCanvas*,
                        const PaintFlags&,
                        const FloatSize&,
                        float,
                        const FloatRect&,
                        const FloatRect&,
                        const KURL&);
  void DrawPatternForContainer(GraphicsContext&,
                               const FloatSize,
                               float,
                               const FloatRect&,
                               const FloatSize&,
                               const FloatPoint&,
                               SkBlendMode,
                               const FloatRect&,
                               const FloatSize& repeat_spacing,
                               const KURL&);
  sk_sp<SkImage> ImageForCurrentFrameForContainer(
      const KURL&,
      const IntSize& container_size);

  // Paints the current frame. If a PaintCanvas is passed, paints into that
  // canvas and returns nullptr.
  // Otherwise returns a pointer to the new PaintRecord.
  sk_sp<PaintRecord> PaintRecordForCurrentFrame(const FloatRect& bounds,
                                                const KURL&,
                                                PaintCanvas* = nullptr);

  void DrawInternal(PaintCanvas*,
                    const PaintFlags&,
                    const FloatRect& from_rect,
                    const FloatRect& to_rect,
                    RespectImageOrientationEnum,
                    ImageClampingMode,
                    const KURL&);

  template <typename Func>
  void ForContainer(const FloatSize&, Func&&);

  bool ApplyShader(PaintFlags&, const SkMatrix& local_matrix) override;
  bool ApplyShaderForContainer(const FloatSize&,
                               float zoom,
                               const KURL&,
                               PaintFlags&,
                               const SkMatrix& local_matrix);
  bool ApplyShaderInternal(PaintFlags&,
                           const SkMatrix& local_matrix,
                           const KURL&);

  void StopAnimation();
  void ScheduleTimelineRewind();
  void FlushPendingTimelineRewind();

  Page* GetPageForTesting() { return page_; }
  void LoadCompleted();
  void NotifyAsyncLoadCompleted();

  class SVGImageLocalFrameClient;

  Persistent<SVGImageChromeClient> chrome_client_;
  Persistent<Page> page_;
  std::unique_ptr<PaintController> paint_controller_;

  // When an SVG image has no intrinsic size, the size depends on the default
  // object size, which in turn depends on the container. One SVGImage may
  // belong to multiple containers so the final image size can't be known in
  // SVGImage. SVGImageForContainer carries the final image size, also called
  // the "concrete object size". For more, see: SVGImageForContainer.h
  IntSize intrinsic_size_;
  bool has_pending_timeline_rewind_;

  enum LoadState {
    kDataChangedNotStarted,
    kInDataChanged,
    kWaitingForAsyncLoadCompletion,
    kLoadCompleted,
  };

  LoadState load_state_ = kDataChangedNotStarted;

  Persistent<SVGImageLocalFrameClient> frame_client_;
  FRIEND_TEST_ALL_PREFIXES(SVGImageTest, SupportsSubsequenceCaching);
};

DEFINE_IMAGE_TYPE_CASTS(SVGImage);

class ImageObserverDisabler {
  STACK_ALLOCATED();
  WTF_MAKE_NONCOPYABLE(ImageObserverDisabler);

 public:
  ImageObserverDisabler(Image* image) : image_(image) {
    image_->SetImageObserverDisabled(true);
  }

  ~ImageObserverDisabler() { image_->SetImageObserverDisabled(false); }

 private:
  Image* image_;
};

}  // namespace blink

#endif  // SVGImage_h
