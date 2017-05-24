// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef WebDisplayItemList_h
#define WebDisplayItemList_h

#include "WebBlendMode.h"
#include "WebFloatPoint.h"
#include "WebFloatRect.h"
#include "WebRect.h"
#include "WebSize.h"
#include "WebVector.h"

#include "cc/paint/paint_record.h"
#include "third_party/skia/include/core/SkBlendMode.h"
#include "third_party/skia/include/core/SkRefCnt.h"

class SkColorFilter;
class SkMatrix44;
class SkPath;
struct SkRect;
class SkRRect;

namespace cc {
class FilterOperations;
}

namespace blink {

// An ordered list of items representing content to be rendered (stored in
// 'drawing' items) and operations to be performed when rendering this content
// (stored in 'clip', 'transform', 'filter', etc...). For more details see:
// http://dev.chromium.org/blink/slimming-paint.
class WebDisplayItemList {
 public:
  virtual ~WebDisplayItemList() {}

  virtual void AppendDrawingItem(const WebRect& visual_rect,
                                 sk_sp<const cc::PaintRecord>) {}

  virtual void AppendClipItem(const WebRect& clip_rect,
                              const WebVector<SkRRect>& rounded_clip_rects) {}
  virtual void AppendEndClipItem() {}
  virtual void AppendClipPathItem(const SkPath&, bool antialias) {}
  virtual void AppendEndClipPathItem() {}
  virtual void AppendFloatClipItem(const WebFloatRect& clip_rect) {}
  virtual void AppendEndFloatClipItem() {}
  virtual void AppendTransformItem(const SkMatrix44&) {}
  virtual void AppendEndTransformItem() {}
  virtual void AppendCompositingItem(float opacity,
                                     SkBlendMode,
                                     SkRect* bounds,
                                     SkColorFilter*) {}
  virtual void AppendEndCompositingItem() {}

  // TODO(loyso): This should use CompositorFilterOperation. crbug.com/584551
  virtual void AppendFilterItem(const cc::FilterOperations&,
                                const WebFloatRect& filter_bounds,
                                const WebFloatPoint& origin) {}
  virtual void AppendEndFilterItem() {}

  // Scroll containers are identified by an opaque pointer.
  using ScrollContainerId = const void*;
  virtual void AppendScrollItem(const WebSize& scroll_offset,
                                ScrollContainerId) {}
  virtual void AppendEndScrollItem() {}

  virtual void SetIsSuitableForGpuRasterization(bool is_suitable) {}
};

}  // namespace blink

#endif  // WebDisplayItemList_h
