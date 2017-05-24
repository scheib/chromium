// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cc/paint/drawing_display_item.h"

#include "third_party/skia/include/core/SkPicture.h"

namespace cc {

DrawingDisplayItem::DrawingDisplayItem() : DisplayItem(DRAWING) {}

DrawingDisplayItem::DrawingDisplayItem(sk_sp<const PaintRecord> record)
    : DisplayItem(DRAWING), picture(std::move(record)) {}

DrawingDisplayItem::DrawingDisplayItem(const DrawingDisplayItem& item)
    : DisplayItem(DRAWING), picture(item.picture) {}

DrawingDisplayItem::~DrawingDisplayItem() = default;

size_t DrawingDisplayItem::ExternalMemoryUsage() const {
  return picture->bytes_used();
}

DISABLE_CFI_PERF
size_t DrawingDisplayItem::OpCount() const {
  return picture->size();
}

}  // namespace cc
