// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "web/tests/sim/SimDisplayItemList.h"

#include "core/css/parser/CSSParser.h"
#include "platform/graphics/LoggingCanvas.h"
#include "platform/graphics/paint/PaintRecord.h"
#include "third_party/skia/include/core/SkRect.h"

namespace blink {

SimDisplayItemList::SimDisplayItemList() {}

void SimDisplayItemList::AppendDrawingItem(const WebRect&,
                                           sk_sp<const PaintRecord> record) {
  SkIRect bounds = record->cullRect().roundOut();
  SimCanvas canvas(bounds.width(), bounds.height());
  record->playback(&canvas);
  commands_.Append(canvas.Commands().data(), canvas.Commands().size());
}

bool SimDisplayItemList::Contains(SimCanvas::CommandType type,
                                  const String& color_string) const {
  Color color = 0;
  if (!color_string.IsNull())
    CHECK(CSSParser::ParseColor(color, color_string, true));
  for (auto& command : commands_) {
    if (command.type == type &&
        (color_string.IsNull() || command.color == color.Rgb()))
      return true;
  }
  return false;
}

}  // namespace blink
