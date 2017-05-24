// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/page/scrolling/ScrollState.h"

#include <memory>
#include "core/dom/DOMNodeIds.h"
#include "core/dom/Element.h"
#include "core/dom/ExceptionCode.h"
#include "core/layout/LayoutObject.h"
#include "platform/graphics/CompositorElementId.h"
#include "platform/wtf/PtrUtil.h"

namespace blink {

namespace {
Element* ElementForId(int element_id) {
  Node* node = DOMNodeIds::NodeForId(element_id);
  DCHECK(node);
  if (!node)
    return nullptr;
  DCHECK(node->IsElementNode());
  if (!node->IsElementNode())
    return nullptr;
  return static_cast<Element*>(node);
}
}  // namespace

ScrollState* ScrollState::Create(ScrollStateInit init) {
  std::unique_ptr<ScrollStateData> scroll_state_data =
      WTF::MakeUnique<ScrollStateData>();
  scroll_state_data->delta_x = init.deltaX();
  scroll_state_data->delta_y = init.deltaY();
  scroll_state_data->position_x = init.positionX();
  scroll_state_data->position_y = init.positionY();
  scroll_state_data->velocity_x = init.velocityX();
  scroll_state_data->velocity_y = init.velocityY();
  scroll_state_data->is_beginning = init.isBeginning();
  scroll_state_data->is_in_inertial_phase = init.isInInertialPhase();
  scroll_state_data->is_ending = init.isEnding();
  scroll_state_data->should_propagate = init.shouldPropagate();
  scroll_state_data->from_user_input = init.fromUserInput();
  scroll_state_data->is_direct_manipulation = init.isDirectManipulation();
  scroll_state_data->delta_granularity = init.deltaGranularity();
  ScrollState* scroll_state = new ScrollState(std::move(scroll_state_data));
  return scroll_state;
}

ScrollState* ScrollState::Create(std::unique_ptr<ScrollStateData> data) {
  ScrollState* scroll_state = new ScrollState(std::move(data));
  return scroll_state;
}

ScrollState::ScrollState(std::unique_ptr<ScrollStateData> data)
    : data_(std::move(data)) {}

void ScrollState::consumeDelta(double x,
                               double y,
                               ExceptionState& exception_state) {
  if ((data_->delta_x > 0 && 0 > x) || (data_->delta_x < 0 && 0 < x) ||
      (data_->delta_y > 0 && 0 > y) || (data_->delta_y < 0 && 0 < y)) {
    exception_state.ThrowDOMException(
        kInvalidModificationError, "Can't increase delta using consumeDelta");
    return;
  }
  if (fabs(x) > fabs(data_->delta_x) || fabs(y) > fabs(data_->delta_y)) {
    exception_state.ThrowDOMException(
        kInvalidModificationError,
        "Can't change direction of delta using consumeDelta");
    return;
  }
  ConsumeDeltaNative(x, y);
}

void ScrollState::distributeToScrollChainDescendant() {
  if (!scroll_chain_.empty()) {
    int descendant_id = scroll_chain_.front();
    scroll_chain_.pop_front();
    ElementForId(descendant_id)->CallDistributeScroll(*this);
  }
}

void ScrollState::ConsumeDeltaNative(double x, double y) {
  data_->delta_x -= x;
  data_->delta_y -= y;

  if (x)
    data_->caused_scroll_x = true;
  if (y)
    data_->caused_scroll_y = true;
  if (x || y)
    data_->delta_consumed_for_scroll_sequence = true;
}

Element* ScrollState::CurrentNativeScrollingElement() {
  if (data_->current_native_scrolling_element() == CompositorElementId()) {
    element_.Clear();
    return nullptr;
  }
  return element_;
}

void ScrollState::SetCurrentNativeScrollingElement(Element* element) {
  element_ = element;
  data_->set_current_native_scrolling_element(CompositorElementIdFromDOMNodeId(
      DOMNodeIds::IdForNode(element),
      CompositorElementIdNamespace::kScrollState));
}

}  // namespace blink
