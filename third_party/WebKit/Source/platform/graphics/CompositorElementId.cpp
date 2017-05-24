// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "platform/graphics/CompositorElementId.h"

#include "platform/RuntimeEnabledFeatures.h"

namespace blink {

static CompositorElementId CreateCompositorElementId(
    uint64_t blink_id,
    CompositorElementIdNamespace namespace_id) {
  DCHECK(
      blink_id > 0 &&
      blink_id <
          std::numeric_limits<uint64_t>::max() /
              static_cast<unsigned>(
                  CompositorElementIdNamespace::kMaxRepresentableNamespaceId));
  // Shift to make room for namespace_id enum bits.
  cc::ElementIdType id = blink_id << kCompositorNamespaceBitCount;
  id += static_cast<uint64_t>(namespace_id);
  return CompositorElementId(id);
}

CompositorElementId PLATFORM_EXPORT
CompositorElementIdFromPaintLayerId(PaintLayerId id,
                                    CompositorElementIdNamespace namespace_id) {
  DCHECK(namespace_id == CompositorElementIdNamespace::kPrimary ||
         namespace_id == CompositorElementIdNamespace::kScroll);
  return CreateCompositorElementId(id, namespace_id);
}

CompositorElementId PLATFORM_EXPORT
CompositorElementIdFromDOMNodeId(DOMNodeId id,
                                 CompositorElementIdNamespace namespace_id) {
  DCHECK(namespace_id == CompositorElementIdNamespace::kViewport ||
         namespace_id == CompositorElementIdNamespace::kLinkHighlight ||
         namespace_id == CompositorElementIdNamespace::kRootScroll ||
         namespace_id == CompositorElementIdNamespace::kScrollState ||
         namespace_id ==
             CompositorElementIdNamespace::kPrimaryCompositorProxy ||
         namespace_id == CompositorElementIdNamespace::kScrollCompositorProxy);
  return CreateCompositorElementId(id, namespace_id);
}

CompositorElementId PLATFORM_EXPORT
CompositorElementIdFromScrollbarId(ScrollbarId id,
                                   CompositorElementIdNamespace namespace_id) {
  DCHECK(namespace_id == CompositorElementIdNamespace::kScrollbar);
  return CreateCompositorElementId(id, namespace_id);
}

CompositorElementIdNamespace NamespaceFromCompositorElementId(
    CompositorElementId element_id) {
  return static_cast<CompositorElementIdNamespace>(
      element_id.id_ %
      static_cast<uint64_t>(
          CompositorElementIdNamespace::kMaxRepresentableNamespaceId));
}

}  // namespace blink
