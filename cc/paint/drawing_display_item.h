// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CC_PAINT_DRAWING_DISPLAY_ITEM_H_
#define CC_PAINT_DRAWING_DISPLAY_ITEM_H_

#include <stddef.h>

#include "cc/paint/display_item.h"
#include "cc/paint/paint_export.h"
#include "cc/paint/paint_record.h"
#include "third_party/skia/include/core/SkRefCnt.h"

namespace cc {

class CC_PAINT_EXPORT DrawingDisplayItem : public DisplayItem {
 public:
  DrawingDisplayItem();
  explicit DrawingDisplayItem(sk_sp<const PaintRecord> record);
  explicit DrawingDisplayItem(const DrawingDisplayItem& item);
  ~DrawingDisplayItem() override;

  size_t ExternalMemoryUsage() const;
  size_t OpCount() const;

  const sk_sp<const PaintRecord> picture;
};

}  // namespace cc

#endif  // CC_PAINT_DRAWING_DISPLAY_ITEM_H_
