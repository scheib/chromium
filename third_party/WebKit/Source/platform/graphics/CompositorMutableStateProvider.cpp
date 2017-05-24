// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "platform/graphics/CompositorMutableStateProvider.h"

#include <memory>
#include "cc/layers/layer_impl.h"
#include "cc/trees/layer_tree_impl.h"
#include "platform/graphics/CompositorMutableProperties.h"
#include "platform/graphics/CompositorMutableState.h"
#include "platform/graphics/CompositorMutation.h"
#include "platform/wtf/PtrUtil.h"

namespace blink {

CompositorMutableStateProvider::CompositorMutableStateProvider(
    cc::LayerTreeImpl* tree_impl,
    CompositorMutations* mutations)
    : tree_(tree_impl), mutations_(mutations) {}

CompositorMutableStateProvider::~CompositorMutableStateProvider() {}

std::unique_ptr<CompositorMutableState>
CompositorMutableStateProvider::GetMutableStateFor(DOMNodeId dom_node_id) {
  cc::LayerImpl* main_layer =
      tree_->LayerByElementId(CompositorElementIdFromDOMNodeId(
          dom_node_id, CompositorElementIdNamespace::kPrimaryCompositorProxy));
  cc::LayerImpl* scroll_layer =
      tree_->LayerByElementId(CompositorElementIdFromDOMNodeId(
          dom_node_id, CompositorElementIdNamespace::kScrollCompositorProxy));

  if (!main_layer && !scroll_layer)
    return nullptr;

  // Ensure that we have an entry in the map for |elementId| but do as few
  // allocations and queries as possible. This will update the map only if we
  // have not added a value for |elementId|.
  auto result = mutations_->map.insert(dom_node_id, nullptr);

  // Only if this is a new entry do we want to allocate a new mutation.
  if (result.is_new_entry)
    result.stored_value->value = WTF::WrapUnique(new CompositorMutation);

  return WTF::WrapUnique(new CompositorMutableState(
      result.stored_value->value.get(), main_layer, scroll_layer));
}

}  // namespace blink
