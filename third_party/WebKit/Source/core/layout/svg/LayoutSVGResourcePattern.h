/*
 * Copyright (C) 2006 Nikolas Zimmermann <zimmermann@kde.org>
 * Copyright (C) Research In Motion Limited 2010. All rights reserved.
 * Copyright 2014 The Chromium Authors. All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef LayoutSVGResourcePattern_h
#define LayoutSVGResourcePattern_h

#include <memory>
#include "core/layout/svg/LayoutSVGResourcePaintServer.h"
#include "core/svg/PatternAttributes.h"
#include "platform/heap/Handle.h"
#include "platform/wtf/HashMap.h"
#include "third_party/skia/include/core/SkRefCnt.h"

namespace blink {

class AffineTransform;
class FloatRect;
class SVGPatternElement;
struct PatternData;

class LayoutSVGResourcePattern final : public LayoutSVGResourcePaintServer {
 public:
  explicit LayoutSVGResourcePattern(SVGPatternElement*);

  const char* GetName() const override { return "LayoutSVGResourcePattern"; }

  void RemoveAllClientsFromCache(bool mark_for_invalidation = true) override;
  void RemoveClientFromCache(LayoutObject*,
                             bool mark_for_invalidation = true) override;

  SVGPaintServer PreparePaintServer(const LayoutObject&) override;

  static const LayoutSVGResourceType kResourceType = kPatternResourceType;
  LayoutSVGResourceType ResourceType() const override { return kResourceType; }

 private:
  std::unique_ptr<PatternData> BuildPatternData(const LayoutObject&);
  sk_sp<PaintRecord> AsPaintRecord(const FloatRect& tile,
                                   const AffineTransform&) const;
  PatternData* PatternForLayoutObject(const LayoutObject&);

  const LayoutSVGResourceContainer* ResolveContentElement() const;

  bool should_collect_pattern_attributes_ : 1;
  Persistent<PatternAttributesWrapper> attributes_wrapper_;

  PatternAttributes& MutableAttributes() {
    return attributes_wrapper_->Attributes();
  }
  const PatternAttributes& Attributes() const {
    return attributes_wrapper_->Attributes();
  }

  // FIXME: we can almost do away with this per-object map, but not quite: the
  // tile size can be relative to the client bounding box, and it gets captured
  // in the cached Pattern shader.
  // Hence, we need one Pattern shader per client. The display list OTOH is the
  // same => we should be able to cache a single display list per
  // LayoutSVGResourcePattern + one Pattern(shader) for each client -- this
  // would avoid re-recording when multiple clients share the same pattern.
  HashMap<const LayoutObject*, std::unique_ptr<PatternData>> pattern_map_;
};

}  // namespace blink

#endif
