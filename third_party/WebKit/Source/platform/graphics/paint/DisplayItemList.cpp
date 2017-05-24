// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "platform/graphics/paint/DisplayItemList.h"

#include "platform/graphics/LoggingCanvas.h"
#include "platform/graphics/paint/DrawingDisplayItem.h"
#include "platform/graphics/paint/PaintChunk.h"

#ifndef NDEBUG
#include "platform/wtf/text/WTFString.h"
#endif

namespace blink {

DisplayItem& DisplayItemList::AppendByMoving(DisplayItem& item) {
#ifndef NDEBUG
  String original_debug_string = item.AsDebugString();
#endif
  DCHECK(item.HasValidClient());
  DisplayItem& result =
      ContiguousContainer::AppendByMoving(item, item.DerivedSize());
  // ContiguousContainer::appendByMoving() calls an in-place constructor
  // on item which replaces it with a tombstone/"dead display item" that
  // can be safely destructed but should never be used.
  DCHECK(!item.HasValidClient());
#ifndef NDEBUG
  // Save original debug string in the old item to help debugging.
  item.SetClientDebugString(original_debug_string);
#endif
  return result;
}

void DisplayItemList::AppendVisualRect(const IntRect& visual_rect) {
  visual_rects_.push_back(visual_rect);
}

DisplayItemList::Range<DisplayItemList::iterator>
DisplayItemList::ItemsInPaintChunk(const PaintChunk& paint_chunk) {
  return Range<iterator>(begin() + paint_chunk.begin_index,
                         begin() + paint_chunk.end_index);
}

DisplayItemList::Range<DisplayItemList::const_iterator>
DisplayItemList::ItemsInPaintChunk(const PaintChunk& paint_chunk) const {
  return Range<const_iterator>(begin() + paint_chunk.begin_index,
                               begin() + paint_chunk.end_index);
}

std::unique_ptr<JSONArray> DisplayItemList::SubsequenceAsJSON(
    size_t begin_index,
    size_t end_index,
    JsonFlags options) const {
  std::unique_ptr<JSONArray> json_array = JSONArray::Create();
  size_t i = 0;
  for (auto it = begin() + begin_index; it != begin() + end_index; ++it, ++i) {
    std::unique_ptr<JSONObject> json = JSONObject::Create();

    const DisplayItem& display_item = *it;
    if ((options & kSkipNonDrawings) && !display_item.IsDrawing())
      continue;

    json->SetInteger("index", i);
#ifndef NDEBUG
    StringBuilder string_builder;
    display_item.DumpPropertiesAsDebugString(string_builder);

    if (options & kShownOnlyDisplayItemTypes) {
      json->SetString("type",
                      DisplayItem::TypeAsDebugString(display_item.GetType()));
    } else {
      json->SetString("properties", string_builder.ToString());
    }

#endif
    if (display_item.HasValidClient()) {
#if CHECK_DISPLAY_ITEM_CLIENT_ALIVENESS
      if (!display_item.Client().IsAlive()) {
        json->SetBoolean("clientIsAlive", true);
      } else {
#else

      if (options & kShowClientDebugName) {
#endif
        json->SetString(
            "clientDebugName",
            String::Format("clientDebugName: \"%s\"",
                           display_item.Client().DebugName().Ascii().data()));
      }
#ifndef NDEBUG
      if ((options & kShowPaintRecords) && display_item.IsDrawing()) {
        const DrawingDisplayItem& item =
            static_cast<const DrawingDisplayItem&>(display_item);
        if (const PaintRecord* record = item.GetPaintRecord().get()) {
          json->SetString("record", RecordAsDebugString(record));
        }
      }
#endif
    }
    if (HasVisualRect(i)) {
      IntRect local_visual_rect = VisualRect(i);
      json->SetString(
          "visualRect",
          String::Format("[%d,%d %dx%d]", local_visual_rect.X(),
                         local_visual_rect.Y(), local_visual_rect.Width(),
                         local_visual_rect.Height()));
    }
    json_array->PushObject(std::move(json));
  }
  return json_array;
}

}  // namespace blink
