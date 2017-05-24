/*
 * Copyright (C) 2004, 2008, 2009, 2010 Apple Inc. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY APPLE COMPUTER, INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE COMPUTER, INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "core/editing/FrameSelection.h"

#include <stdio.h>
#include "bindings/core/v8/ExceptionState.h"
#include "core/HTMLNames.h"
#include "core/css/StylePropertySet.h"
#include "core/dom/AXObjectCache.h"
#include "core/dom/CharacterData.h"
#include "core/dom/Document.h"
#include "core/dom/Element.h"
#include "core/dom/ElementTraversal.h"
#include "core/dom/NodeTraversal.h"
#include "core/dom/NodeWithIndex.h"
#include "core/dom/Text.h"
#include "core/editing/CaretDisplayItemClient.h"
#include "core/editing/EditingUtilities.h"
#include "core/editing/Editor.h"
#include "core/editing/FrameCaret.h"
#include "core/editing/GranularityStrategy.h"
#include "core/editing/InputMethodController.h"
#include "core/editing/LayoutSelection.h"
#include "core/editing/SelectionController.h"
#include "core/editing/SelectionEditor.h"
#include "core/editing/SelectionModifier.h"
#include "core/editing/TextAffinity.h"
#include "core/editing/VisibleUnits.h"
#include "core/editing/commands/TypingCommand.h"
#include "core/editing/iterators/TextIterator.h"
#include "core/editing/serializers/Serialization.h"
#include "core/editing/spellcheck/SpellChecker.h"
#include "core/events/Event.h"
#include "core/frame/FrameView.h"
#include "core/frame/LocalDOMWindow.h"
#include "core/frame/LocalFrame.h"
#include "core/frame/Settings.h"
#include "core/html/HTMLBodyElement.h"
#include "core/html/HTMLFrameElementBase.h"
#include "core/html/HTMLInputElement.h"
#include "core/html/HTMLSelectElement.h"
#include "core/input/EventHandler.h"
#include "core/layout/HitTestRequest.h"
#include "core/layout/HitTestResult.h"
#include "core/layout/LayoutPart.h"
#include "core/layout/api/LayoutViewItem.h"
#include "core/loader/DocumentLoader.h"
#include "core/page/EditorClient.h"
#include "core/page/FocusController.h"
#include "core/page/FrameTree.h"
#include "core/page/Page.h"
#include "core/page/SpatialNavigation.h"
#include "core/paint/PaintLayer.h"
#include "platform/SecureTextInput.h"
#include "platform/geometry/FloatQuad.h"
#include "platform/graphics/GraphicsContext.h"
#include "platform/text/UnicodeUtilities.h"
#include "platform/wtf/PtrUtil.h"
#include "platform/wtf/text/CString.h"

#define EDIT_DEBUG 0

namespace blink {

using namespace HTMLNames;

static inline bool ShouldAlwaysUseDirectionalSelection(LocalFrame* frame) {
  return frame->GetEditor().Behavior().ShouldConsiderSelectionAsDirectional();
}

FrameSelection::FrameSelection(LocalFrame& frame)
    : frame_(frame),
      layout_selection_(LayoutSelection::Create(*this)),
      selection_editor_(SelectionEditor::Create(frame)),
      granularity_(kCharacterGranularity),
      x_pos_for_vertical_arrow_navigation_(NoXPosForVerticalArrowNavigation()),
      focused_(frame.GetPage() &&
               frame.GetPage()->GetFocusController().FocusedFrame() == frame),
      frame_caret_(new FrameCaret(frame, *selection_editor_)) {}

FrameSelection::~FrameSelection() {}

const DisplayItemClient& FrameSelection::CaretDisplayItemClientForTesting()
    const {
  return frame_caret_->GetDisplayItemClient();
}

Document& FrameSelection::GetDocument() const {
  DCHECK(LifecycleContext());
  return *LifecycleContext();
}

bool FrameSelection::IsHandleVisible() const {
  return GetSelectionInDOMTree().IsHandleVisible();
}

const VisibleSelection& FrameSelection::ComputeVisibleSelectionInDOMTree()
    const {
  return selection_editor_->ComputeVisibleSelectionInDOMTree();
}

const VisibleSelectionInFlatTree&
FrameSelection::ComputeVisibleSelectionInFlatTree() const {
  return selection_editor_->ComputeVisibleSelectionInFlatTree();
}

const SelectionInDOMTree& FrameSelection::GetSelectionInDOMTree() const {
  return selection_editor_->GetSelectionInDOMTree();
}

Element* FrameSelection::RootEditableElementOrDocumentElement() const {
  Element* selection_root =
      ComputeVisibleSelectionInDOMTreeDeprecated().RootEditableElement();
  return selection_root ? selection_root : GetDocument().documentElement();
}

// TODO(yosin): We should move |rootEditableElementOrTreeScopeRootNodeOf()| to
// "EditingUtilities.cpp"
ContainerNode* RootEditableElementOrTreeScopeRootNodeOf(
    const Position& position) {
  Element* selection_root = RootEditableElementOf(position);
  if (selection_root)
    return selection_root;

  Node* const node = position.ComputeContainerNode();
  return node ? &node->GetTreeScope().RootNode() : 0;
}

const VisibleSelection&
FrameSelection::ComputeVisibleSelectionInDOMTreeDeprecated() const {
  // TODO(editing-dev): Hoist updateStyleAndLayoutIgnorePendingStylesheets
  // to caller. See http://crbug.com/590369 for more details.
  GetDocument().UpdateStyleAndLayoutIgnorePendingStylesheets();
  return ComputeVisibleSelectionInDOMTree();
}

const VisibleSelectionInFlatTree& FrameSelection::GetSelectionInFlatTree()
    const {
  return ComputeVisibleSelectionInFlatTree();
}

void FrameSelection::MoveCaretSelection(const IntPoint& point) {
  DCHECK(!GetDocument().NeedsLayoutTreeUpdate());

  Element* const editable =
      ComputeVisibleSelectionInDOMTree().RootEditableElement();
  if (!editable)
    return;

  const VisiblePosition position =
      VisiblePositionForContentsPoint(point, GetFrame());
  SelectionInDOMTree::Builder builder;
  builder.SetIsDirectional(GetSelectionInDOMTree().IsDirectional());
  builder.SetIsHandleVisible(true);
  if (position.IsNotNull())
    builder.Collapse(position.ToPositionWithAffinity());
  SetSelection(builder.Build(),
               kCloseTyping | kClearTypingStyle | kUserTriggered);
}

void FrameSelection::SetSelection(const SelectionInDOMTree& passed_selection,
                                  SetSelectionOptions options,
                                  CursorAlignOnScroll align,
                                  TextGranularity granularity) {
  if (SetSelectionDeprecated(passed_selection, options, granularity))
    DidSetSelectionDeprecated(options, align);
}

bool FrameSelection::SetSelectionDeprecated(
    const SelectionInDOMTree& passed_selection,
    SetSelectionOptions options,
    TextGranularity granularity) {
  DCHECK(IsAvailable());
  passed_selection.AssertValidFor(GetDocument());

  SelectionInDOMTree::Builder builder(passed_selection);
  if (ShouldAlwaysUseDirectionalSelection(frame_))
    builder.SetIsDirectional(true);
  SelectionInDOMTree new_selection = builder.Build();
  if (granularity_strategy_ &&
      (options & FrameSelection::kDoNotClearStrategy) == 0)
    granularity_strategy_->Clear();
  bool close_typing = options & kCloseTyping;
  bool should_clear_typing_style = options & kClearTypingStyle;
  granularity_ = granularity;

  // TODO(yosin): We should move to call |TypingCommand::closeTyping()| to
  // |Editor| class.
  if (close_typing)
    TypingCommand::CloseTyping(frame_);

  if (should_clear_typing_style)
    frame_->GetEditor().ClearTypingStyle();

  const SelectionInDOMTree old_selection_in_dom_tree =
      selection_editor_->GetSelectionInDOMTree();
  if (old_selection_in_dom_tree == new_selection)
    return false;
  selection_editor_->SetSelection(new_selection);
  ScheduleVisualUpdateForPaintInvalidationIfNeeded();

  const Document& current_document = GetDocument();
  // TODO(yosin): We should get rid of unsued |options| for
  // |Editor::respondToChangedSelection()|.
  // Note: Since, setting focus can modify DOM tree, we should use
  // |oldSelection| before setting focus
  frame_->GetEditor().RespondToChangedSelection(
      old_selection_in_dom_tree.ComputeStartPosition(), options);
  DCHECK_EQ(current_document, GetDocument());
  return true;
}

void FrameSelection::DidSetSelectionDeprecated(SetSelectionOptions options,
                                               CursorAlignOnScroll align) {
  const Document& current_document = GetDocument();
  if (!GetSelectionInDOMTree().IsNone() && !(options & kDoNotSetFocus)) {
    SetFocusedNodeIfNeeded();
    // |setFocusedNodeIfNeeded()| dispatches sync events "FocusOut" and
    // "FocusIn", |m_frame| may associate to another document.
    if (!IsAvailable() || GetDocument() != current_document) {
      // Once we get test case to reach here, we should change this
      // if-statement to |DCHECK()|.
      NOTREACHED();
      return;
    }
  }

  frame_caret_->StopCaretBlinkTimer();
  UpdateAppearance();

  // Always clear the x position used for vertical arrow navigation.
  // It will be restored by the vertical arrow navigation code if necessary.
  x_pos_for_vertical_arrow_navigation_ = NoXPosForVerticalArrowNavigation();

  // TODO(yosin): Can we move this to at end of this function?
  // This may dispatch a synchronous focus-related events.
  if (!(options & kDoNotSetFocus)) {
    SelectFrameElementInParentIfFullySelected();
    if (!IsAvailable() || GetDocument() != current_document) {
      // editing/selection/selectallchildren-crash.html and
      // editing/selection/longpress-selection-in-iframe-removed-crash.html
      // reach here.
      return;
    }
  }

  EUserTriggered user_triggered = SelectionOptionsToUserTriggered(options);
  NotifyTextControlOfSelectionChange(user_triggered);
  if (user_triggered == kUserTriggered) {
    ScrollAlignment alignment;

    if (frame_->GetEditor()
            .Behavior()
            .ShouldCenterAlignWhenSelectionIsRevealed())
      alignment = (align == CursorAlignOnScroll::kAlways)
                      ? ScrollAlignment::kAlignCenterAlways
                      : ScrollAlignment::kAlignCenterIfNeeded;
    else
      alignment = (align == CursorAlignOnScroll::kAlways)
                      ? ScrollAlignment::kAlignTopAlways
                      : ScrollAlignment::kAlignToEdgeIfNeeded;

    RevealSelection(alignment, kRevealExtent);
  }

  NotifyAccessibilityForSelectionChange();
  NotifyCompositorForSelectionChange();
  NotifyEventHandlerForSelectionChange();
  frame_->DomWindow()->EnqueueDocumentEvent(
      Event::Create(EventTypeNames::selectionchange));
}

void FrameSelection::SetSelection(const SelectionInFlatTree& new_selection,
                                  SetSelectionOptions options,
                                  CursorAlignOnScroll align,
                                  TextGranularity granularity) {
  new_selection.AssertValidFor(GetDocument());
  SelectionInDOMTree::Builder builder;
  builder.SetAffinity(new_selection.Affinity())
      .SetBaseAndExtent(ToPositionInDOMTree(new_selection.Base()),
                        ToPositionInDOMTree(new_selection.Extent()))
      .SetGranularity(new_selection.Granularity())
      .SetIsDirectional(new_selection.IsDirectional())
      .SetIsHandleVisible(new_selection.IsHandleVisible())
      .SetHasTrailingWhitespace(new_selection.HasTrailingWhitespace());
  return SetSelection(builder.Build(), options, align, granularity);
}

void FrameSelection::NodeChildrenWillBeRemoved(ContainerNode& container) {
  if (!container.InActiveDocument())
    return;
  // TODO(yosin): We should move to call |TypingCommand::closeTyping()| to
  // |Editor| class.
  if (!GetDocument().IsRunningExecCommand())
    TypingCommand::CloseTyping(frame_);
}

void FrameSelection::NodeWillBeRemoved(Node& node) {
  // There can't be a selection inside a fragment, so if a fragment's node is
  // being removed, the selection in the document that created the fragment
  // needs no adjustment.
  if (!node.InActiveDocument())
    return;
  // TODO(yosin): We should move to call |TypingCommand::closeTyping()| to
  // |Editor| class.
  if (!GetDocument().IsRunningExecCommand())
    TypingCommand::CloseTyping(frame_);
}

void FrameSelection::DidChangeFocus() {
  // Hits in
  // virtual/gpu/compositedscrolling/scrollbars/scrollbar-miss-mousemove-disabled.html
  DisableCompositingQueryAsserts disabler;
  UpdateAppearance();
}

static DispatchEventResult DispatchSelectStart(
    const VisibleSelection& selection) {
  Node* select_start_target = selection.Extent().ComputeContainerNode();
  if (!select_start_target)
    return DispatchEventResult::kNotCanceled;

  return select_start_target->DispatchEvent(
      Event::CreateCancelableBubble(EventTypeNames::selectstart));
}

// The return value of |FrameSelection::modify()| is different based on
// value of |userTriggered| parameter.
// When |userTriggered| is |userTriggered|, |modify()| returns false if
// "selectstart" event is dispatched and canceled, otherwise returns true.
// When |userTriggered| is |NotUserTrigged|, return value specifies whether
// selection is modified or not.
bool FrameSelection::Modify(EAlteration alter,
                            SelectionDirection direction,
                            TextGranularity granularity,
                            EUserTriggered user_triggered) {
  SelectionModifier selection_modifier(*GetFrame(),
                                       ComputeVisibleSelectionInDOMTree(),
                                       x_pos_for_vertical_arrow_navigation_);
  const bool modified =
      selection_modifier.Modify(alter, direction, granularity);
  if (user_triggered == kUserTriggered &&
      selection_modifier.Selection().IsRange() &&
      ComputeVisibleSelectionInDOMTree().IsCaret() &&
      DispatchSelectStart(ComputeVisibleSelectionInDOMTree()) !=
          DispatchEventResult::kNotCanceled) {
    return false;
  }
  if (!modified) {
    if (user_triggered == kNotUserTriggered)
      return false;
    // If spatial navigation enabled, focus navigator will move focus to
    // another element. See snav-input.html and snav-textarea.html
    if (IsSpatialNavigationEnabled(frame_))
      return false;
    // Even if selection isn't changed, we prevent to default action, e.g.
    // scroll window when caret is at end of content editable.
    return true;
  }

  const SetSelectionOptions options =
      kCloseTyping | kClearTypingStyle | user_triggered;
  SetSelection(selection_modifier.Selection().AsSelection(), options);

  if (granularity == kLineGranularity || granularity == kParagraphGranularity)
    x_pos_for_vertical_arrow_navigation_ =
        selection_modifier.XPosForVerticalArrowNavigation();

  if (user_triggered == kUserTriggered)
    granularity_ = kCharacterGranularity;

  ScheduleVisualUpdateForPaintInvalidationIfNeeded();

  return true;
}

bool FrameSelection::Modify(EAlteration alter,
                            unsigned vertical_distance,
                            VerticalDirection direction) {
  SelectionModifier selection_modifier(*GetFrame(),
                                       ComputeVisibleSelectionInDOMTree());
  if (!selection_modifier.ModifyWithPageGranularity(alter, vertical_distance,
                                                    direction)) {
    return false;
  }

  SetSelection(selection_modifier.Selection().AsSelection(),
               kCloseTyping | kClearTypingStyle | kUserTriggered,
               alter == kAlterationMove ? CursorAlignOnScroll::kAlways
                                        : CursorAlignOnScroll::kIfNeeded);

  granularity_ = kCharacterGranularity;

  return true;
}

void FrameSelection::Clear() {
  granularity_ = kCharacterGranularity;
  if (granularity_strategy_)
    granularity_strategy_->Clear();
  SetSelection(SelectionInDOMTree());
}

bool FrameSelection::SelectionHasFocus() const {
  // TODO(editing-dev): Hoist UpdateStyleAndLayoutIgnorePendingStylesheets
  // to caller. See http://crbug.com/590369 for more details.
  GetDocument().UpdateStyleAndLayoutIgnorePendingStylesheets();
  const Node* current =
      ComputeVisibleSelectionInDOMTree().Start().ComputeContainerNode();
  if (!current)
    return false;

  // No focused element means document root has focus.
  const Element* const focused_element = GetDocument().FocusedElement()
                                             ? GetDocument().FocusedElement()
                                             : GetDocument().documentElement();
  if (!focused_element)
    return false;

  if (focused_element->IsTextControl())
    return focused_element->ContainsIncludingHostElements(*current);

  bool has_editable_style = HasEditableStyle(*current);
  do {
    // If the selection is within an editable sub tree and that sub tree
    // doesn't have focus, the selection doesn't have focus either.
    if (has_editable_style && !HasEditableStyle(*current))
      return false;

    // Selection has focus if its sub tree has focus.
    if (current == focused_element)
      return true;
    current = current->ParentOrShadowHostNode();
  } while (current);

  return false;
}

bool FrameSelection::IsHidden() const {
  if (SelectionHasFocus())
    return false;

  const Node* start =
      ComputeVisibleSelectionInDOMTree().Start().ComputeContainerNode();
  if (!start)
    return true;

  // The selection doesn't have focus, so hide everything but range selections.
  if (!GetSelectionInDOMTree().IsRange())
    return true;

  // Here we know we have an unfocused range selection. Let's say that
  // selection resides inside a text control. Since the selection doesn't have
  // focus neither does the text control. Meaning, if the selection indeed
  // resides inside a text control, it should be hidden.
  return EnclosingTextControl(start);
}

void FrameSelection::DocumentAttached(Document* document) {
  DCHECK(document);
  use_secure_keyboard_entry_when_active_ = false;
  selection_editor_->DocumentAttached(document);
  SetContext(document);
}

void FrameSelection::ContextDestroyed(Document* document) {
  granularity_ = kCharacterGranularity;

  layout_selection_->OnDocumentShutdown();

  frame_->GetEditor().ClearTypingStyle();
}

void FrameSelection::ClearPreviousCaretVisualRect(const LayoutBlock& block) {
  frame_caret_->ClearPreviousVisualRect(block);
}

void FrameSelection::LayoutBlockWillBeDestroyed(const LayoutBlock& block) {
  frame_caret_->LayoutBlockWillBeDestroyed(block);
}

void FrameSelection::UpdateStyleAndLayoutIfNeeded() {
  frame_caret_->UpdateStyleAndLayoutIfNeeded();
}

void FrameSelection::InvalidatePaint(const LayoutBlock& block,
                                     const PaintInvalidatorContext& context) {
  frame_caret_->InvalidatePaint(block, context);
}

bool FrameSelection::ShouldPaintCaret(const LayoutBlock& block) const {
  DCHECK_GE(GetDocument().Lifecycle().GetState(),
            DocumentLifecycle::kLayoutClean);
  bool result = frame_caret_->ShouldPaintCaret(block);
  DCHECK(!result || (ComputeVisibleSelectionInDOMTree().IsCaret() &&
                     ComputeVisibleSelectionInDOMTree().HasEditableStyle()));
  return result;
}

IntRect FrameSelection::AbsoluteCaretBounds() {
  DCHECK(ComputeVisibleSelectionInDOMTree().IsValidFor(*frame_->GetDocument()));
  return frame_caret_->AbsoluteCaretBounds();
}

void FrameSelection::PaintCaret(GraphicsContext& context,
                                const LayoutPoint& paint_offset) {
  frame_caret_->PaintCaret(context, paint_offset);
}

bool FrameSelection::Contains(const LayoutPoint& point) {
  if (GetDocument().GetLayoutViewItem().IsNull())
    return false;

  // Treat a collapsed selection like no selection.
  const VisibleSelectionInFlatTree& visible_selection =
      ComputeVisibleSelectionInFlatTree();
  if (!visible_selection.IsRange())
    return false;

  HitTestRequest request(HitTestRequest::kReadOnly | HitTestRequest::kActive);
  HitTestResult result(request, point);
  GetDocument().GetLayoutViewItem().HitTest(result);
  Node* inner_node = result.InnerNode();
  if (!inner_node || !inner_node->GetLayoutObject())
    return false;

  const VisiblePositionInFlatTree& visible_pos =
      CreateVisiblePosition(FromPositionInDOMTree<EditingInFlatTreeStrategy>(
          inner_node->GetLayoutObject()->PositionForPoint(
              result.LocalPoint())));
  if (visible_pos.IsNull())
    return false;

  const VisiblePositionInFlatTree& visible_start =
      visible_selection.VisibleStart();
  const VisiblePositionInFlatTree& visible_end = visible_selection.VisibleEnd();
  if (visible_start.IsNull() || visible_end.IsNull())
    return false;

  const PositionInFlatTree& start = visible_start.DeepEquivalent();
  const PositionInFlatTree& end = visible_end.DeepEquivalent();
  const PositionInFlatTree& pos = visible_pos.DeepEquivalent();
  return start.CompareTo(pos) <= 0 && pos.CompareTo(end) <= 0;
}

// Workaround for the fact that it's hard to delete a frame.
// Call this after doing user-triggered selections to make it easy to delete the
// frame you entirely selected. Can't do this implicitly as part of every
// setSelection call because in some contexts it might not be good for the focus
// to move to another frame. So instead we call it from places where we are
// selecting with the mouse or the keyboard after setting the selection.
void FrameSelection::SelectFrameElementInParentIfFullySelected() {
  // Find the parent frame; if there is none, then we have nothing to do.
  Frame* parent = frame_->Tree().Parent();
  if (!parent)
    return;
  Page* page = frame_->GetPage();
  if (!page)
    return;

  // Check if the selection contains the entire frame contents; if not, then
  // there is nothing to do.
  if (GetSelectionInDOMTree().SelectionTypeWithLegacyGranularity() !=
      kRangeSelection) {
    return;
  }

  // TODO(editing-dev): The use of updateStyleAndLayoutIgnorePendingStylesheets
  // needs to be audited.  See http://crbug.com/590369 for more details.
  GetDocument().UpdateStyleAndLayoutIgnorePendingStylesheets();

  if (!IsStartOfDocument(ComputeVisibleSelectionInDOMTree().VisibleStart()))
    return;
  if (!IsEndOfDocument(ComputeVisibleSelectionInDOMTree().VisibleEnd()))
    return;

  // FIXME: This is not yet implemented for cross-process frame relationships.
  if (!parent->IsLocalFrame())
    return;

  // Get to the <iframe> or <frame> (or even <object>) element in the parent
  // frame.
  // FIXME: Doesn't work for OOPI.
  HTMLFrameOwnerElement* owner_element = frame_->DeprecatedLocalOwner();
  if (!owner_element)
    return;
  ContainerNode* owner_element_parent = owner_element->parentNode();
  if (!owner_element_parent)
    return;

  // TODO(editing-dev): The use of updateStyleAndLayoutIgnorePendingStylesheets
  // needs to be audited.  See http://crbug.com/590369 for more details.
  owner_element_parent->GetDocument()
      .UpdateStyleAndLayoutIgnorePendingStylesheets();

  // This method's purpose is it to make it easier to select iframes (in order
  // to delete them).  Don't do anything if the iframe isn't deletable.
  if (!blink::HasEditableStyle(*owner_element_parent))
    return;

  // Create compute positions before and after the element.
  unsigned owner_element_node_index = owner_element->NodeIndex();
  VisiblePosition before_owner_element = CreateVisiblePosition(
      Position(owner_element_parent, owner_element_node_index));
  VisiblePosition after_owner_element = CreateVisiblePosition(
      Position(owner_element_parent, owner_element_node_index + 1),
      VP_UPSTREAM_IF_POSSIBLE);

  SelectionInDOMTree::Builder builder;
  builder
      .SetBaseAndExtentDeprecated(before_owner_element.DeepEquivalent(),
                                  after_owner_element.DeepEquivalent())
      .SetAffinity(before_owner_element.Affinity());

  // Focus on the parent frame, and then select from before this element to
  // after.
  VisibleSelection new_selection = CreateVisibleSelection(builder.Build());
  // TODO(yosin): We should call |FocusController::setFocusedFrame()| before
  // |createVisibleSelection()|.
  page->GetFocusController().SetFocusedFrame(parent);
  // setFocusedFrame can dispatch synchronous focus/blur events.  The document
  // tree might be modified.
  if (new_selection.IsNonOrphanedCaretOrRange())
    ToLocalFrame(parent)->Selection().SetSelection(new_selection.AsSelection());
}

// Returns a shadow tree node for legacy shadow trees, a child of the
// ShadowRoot node for new shadow trees, or 0 for non-shadow trees.
static Node* NonBoundaryShadowTreeRootNode(const Position& position) {
  return position.AnchorNode() && !position.AnchorNode()->IsShadowRoot()
             ? position.AnchorNode()->NonBoundaryShadowTreeRootNode()
             : nullptr;
}

void FrameSelection::SelectAll(EUserTriggered user_triggered) {
  if (isHTMLSelectElement(GetDocument().FocusedElement())) {
    HTMLSelectElement* select_element =
        toHTMLSelectElement(GetDocument().FocusedElement());
    if (select_element->CanSelectAll()) {
      select_element->SelectAll();
      return;
    }
  }

  Node* root = nullptr;
  Node* select_start_target = nullptr;
  if (user_triggered == kUserTriggered && IsHidden()) {
    // Hidden selection appears as no selection to user, in which case user-
    // triggered SelectAll should act as if there is no selection.
    root = GetDocument().documentElement();
    select_start_target = GetDocument().body();
  } else if (ComputeVisibleSelectionInDOMTree().IsContentEditable()) {
    root = HighestEditableRoot(ComputeVisibleSelectionInDOMTree().Start());
    if (Node* shadow_root = NonBoundaryShadowTreeRootNode(
            ComputeVisibleSelectionInDOMTree().Start()))
      select_start_target = shadow_root->OwnerShadowHost();
    else
      select_start_target = root;
  } else {
    root = NonBoundaryShadowTreeRootNode(
        ComputeVisibleSelectionInDOMTree().Start());
    if (root) {
      select_start_target = root->OwnerShadowHost();
    } else {
      root = GetDocument().documentElement();
      select_start_target = GetDocument().body();
    }
  }
  if (!root || EditingIgnoresContent(*root))
    return;

  if (select_start_target) {
    const Document& expected_document = GetDocument();
    if (select_start_target->DispatchEvent(Event::CreateCancelableBubble(
            EventTypeNames::selectstart)) != DispatchEventResult::kNotCanceled)
      return;
    // The frame may be detached due to selectstart event.
    if (!IsAvailable()) {
      // Reached by editing/selection/selectstart_detach_frame.html
      return;
    }
    // |root| may be detached due to selectstart event.
    if (!root->isConnected() || expected_document != root->GetDocument())
      return;
  }

  // TODO(editing-dev): Should we pass in user_triggered?
  SetSelection(SelectionInDOMTree::Builder()
                   .SelectAllChildren(*root)
                   .SetIsHandleVisible(IsHandleVisible())
                   .Build());
  SelectFrameElementInParentIfFullySelected();
  // TODO(editing-dev): Should we pass in user_triggered?
  NotifyTextControlOfSelectionChange(kUserTriggered);
}

bool FrameSelection::SetSelectedRange(const EphemeralRange& range,
                                      TextAffinity affinity,
                                      SelectionDirectionalMode directional,
                                      SetSelectionOptions options) {
  if (range.IsNull())
    return false;
  SetSelection(SelectionInDOMTree::Builder()
                   .SetBaseAndExtent(range)
                   .SetAffinity(affinity)
                   .SetIsDirectional(directional ==
                                     SelectionDirectionalMode::kDirectional)
                   .Build(),
               options);
  return true;
}

void FrameSelection::NotifyAccessibilityForSelectionChange() {
  if (GetSelectionInDOMTree().IsNone())
    return;
  AXObjectCache* cache = GetDocument().ExistingAXObjectCache();
  if (!cache)
    return;
  const Position& start = GetSelectionInDOMTree().ComputeStartPosition();
  cache->SelectionChanged(start.ComputeContainerNode());
}

void FrameSelection::NotifyCompositorForSelectionChange() {
  if (!RuntimeEnabledFeatures::compositedSelectionUpdateEnabled())
    return;

  ScheduleVisualUpdate();
}

void FrameSelection::NotifyEventHandlerForSelectionChange() {
  frame_->GetEventHandler().GetSelectionController().NotifySelectionChanged();
}

void FrameSelection::FocusedOrActiveStateChanged() {
  bool active_and_focused = FrameIsFocusedAndActive();

  // Trigger style invalidation from the focused element. Even though
  // the focused element hasn't changed, the evaluation of focus pseudo
  // selectors are dependent on whether the frame is focused and active.
  if (Element* element = GetDocument().FocusedElement())
    element->FocusStateChanged();

  GetDocument().UpdateStyleAndLayoutTree();

  // Because LayoutObject::selectionBackgroundColor() and
  // LayoutObject::selectionForegroundColor() check if the frame is active,
  // we have to update places those colors were painted.
  LayoutViewItem view = GetDocument().GetLayoutViewItem();
  if (!view.IsNull())
    layout_selection_->InvalidatePaintForSelection();

  // Caret appears in the active frame.
  if (active_and_focused)
    SetSelectionFromNone();
  else
    frame_->GetSpellChecker().SpellCheckAfterBlur();
  frame_caret_->SetCaretVisibility(active_and_focused
                                       ? CaretVisibility::kVisible
                                       : CaretVisibility::kHidden);

  // Update for caps lock state
  frame_->GetEventHandler().CapsLockStateMayHaveChanged();

  // Secure keyboard entry is set by the active frame.
  if (use_secure_keyboard_entry_when_active_)
    SetUseSecureKeyboardEntry(active_and_focused);
}

void FrameSelection::PageActivationChanged() {
  FocusedOrActiveStateChanged();
}

void FrameSelection::UpdateSecureKeyboardEntryIfActive() {
  if (!FrameIsFocusedAndActive())
    return;
  SetUseSecureKeyboardEntry(use_secure_keyboard_entry_when_active_);
}

void FrameSelection::SetUseSecureKeyboardEntryWhenActive(
    bool uses_secure_keyboard) {
  if (use_secure_keyboard_entry_when_active_ == uses_secure_keyboard)
    return;
  use_secure_keyboard_entry_when_active_ = uses_secure_keyboard;
  UpdateSecureKeyboardEntryIfActive();
}

void FrameSelection::SetUseSecureKeyboardEntry(bool enable) {
  if (enable)
    EnableSecureTextInput();
  else
    DisableSecureTextInput();
}

void FrameSelection::SetFrameIsFocused(bool flag) {
  if (focused_ == flag)
    return;
  focused_ = flag;

  FocusedOrActiveStateChanged();
}

bool FrameSelection::FrameIsFocusedAndActive() const {
  return focused_ && frame_->GetPage() &&
         frame_->GetPage()->GetFocusController().IsActive();
}

bool FrameSelection::NeedsLayoutSelectionUpdate() const {
  return layout_selection_->HasPendingSelection();
}

void FrameSelection::CommitAppearanceIfNeeded() {
  return layout_selection_->Commit();
}

void FrameSelection::DidLayout() {
  UpdateAppearance();
}

void FrameSelection::UpdateAppearance() {
  DCHECK(!frame_->ContentLayoutItem().IsNull());
  frame_caret_->ScheduleVisualUpdateForPaintInvalidationIfNeeded();
  layout_selection_->SetHasPendingSelection();
}

void FrameSelection::NotifyTextControlOfSelectionChange(
    EUserTriggered user_triggered) {
  TextControlElement* text_control =
      EnclosingTextControl(GetSelectionInDOMTree().Base());
  if (!text_control)
    return;
  text_control->SelectionChanged(user_triggered == kUserTriggered);
}

// Helper function that tells whether a particular node is an element that has
// an entire LocalFrame and FrameView, a <frame>, <iframe>, or <object>.
static bool IsFrameElement(const Node* n) {
  if (!n)
    return false;
  LayoutObject* layout_object = n->GetLayoutObject();
  if (!layout_object || !layout_object->IsLayoutPart())
    return false;
  return ToLayoutPart(layout_object)->ChildFrameView();
}

void FrameSelection::SetFocusedNodeIfNeeded() {
  if (ComputeVisibleSelectionInDOMTreeDeprecated().IsNone() ||
      !FrameIsFocused())
    return;

  if (Element* target =
          ComputeVisibleSelectionInDOMTreeDeprecated().RootEditableElement()) {
    // Walk up the DOM tree to search for a node to focus.
    GetDocument().UpdateStyleAndLayoutTreeIgnorePendingStylesheets();
    while (target) {
      // We don't want to set focus on a subframe when selecting in a parent
      // frame, so add the !isFrameElement check here. There's probably a better
      // way to make this work in the long term, but this is the safest fix at
      // this time.
      if (target->IsMouseFocusable() && !IsFrameElement(target)) {
        frame_->GetPage()->GetFocusController().SetFocusedElement(target,
                                                                  frame_);
        return;
      }
      target = target->ParentOrShadowHostElement();
    }
    GetDocument().ClearFocusedElement();
  }
}

static String ExtractSelectedText(const FrameSelection& selection,
                                  TextIteratorBehavior behavior) {
  const VisibleSelectionInFlatTree& visible_selection =
      selection.ComputeVisibleSelectionInFlatTree();
  const EphemeralRangeInFlatTree& range =
      visible_selection.ToNormalizedEphemeralRange();
  // We remove '\0' characters because they are not visibly rendered to the
  // user.
  return PlainText(range, behavior).Replace(0, "");
}

String FrameSelection::SelectedHTMLForClipboard() const {
  const VisibleSelectionInFlatTree& visible_selection =
      ComputeVisibleSelectionInFlatTree();
  const EphemeralRangeInFlatTree& range =
      visible_selection.ToNormalizedEphemeralRange();
  return CreateMarkup(
      range.StartPosition(), range.EndPosition(), kAnnotateForInterchange,
      ConvertBlocksToInlines::kNotConvert, kResolveNonLocalURLs);
}

String FrameSelection::SelectedText(
    const TextIteratorBehavior& behavior) const {
  return ExtractSelectedText(*this, behavior);
}

String FrameSelection::SelectedText() const {
  return SelectedText(TextIteratorBehavior());
}

String FrameSelection::SelectedTextForClipboard() const {
  return ExtractSelectedText(
      *this, TextIteratorBehavior::Builder()
                 .SetEmitsImageAltText(
                     frame_->GetSettings() &&
                     frame_->GetSettings()->GetSelectionIncludesAltImageText())
                 .Build());
}

LayoutRect FrameSelection::Bounds() const {
  FrameView* view = frame_->View();
  if (!view)
    return LayoutRect();

  return Intersection(UnclippedBounds(),
                      LayoutRect(view->VisibleContentRect()));
}

LayoutRect FrameSelection::UnclippedBounds() const {
  FrameView* view = frame_->View();
  LayoutViewItem layout_view = frame_->ContentLayoutItem();

  if (!view || layout_view.IsNull())
    return LayoutRect();

  view->UpdateLifecycleToLayoutClean();
  return LayoutRect(layout_selection_->SelectionBounds());
}

static IntRect AbsoluteSelectionBoundsOf(
    const VisibleSelectionInFlatTree& selection) {
  return ComputeTextRect(
      EphemeralRangeInFlatTree(selection.Start(), selection.end()));
}

// TODO(editing-dev): This should be done in FlatTree world.
void FrameSelection::RevealSelection(const ScrollAlignment& alignment,
                                     RevealExtentOption reveal_extent_option) {
  DCHECK(IsAvailable());

  // TODO(editing-dev): The use of updateStyleAndLayoutIgnorePendingStylesheets
  // needs to be audited.  See http://crbug.com/590369 for more details.
  // Calculation of absolute caret bounds requires clean layout.
  GetDocument().UpdateStyleAndLayoutIgnorePendingStylesheets();

  LayoutRect rect;

  switch (ComputeVisibleSelectionInDOMTree().GetSelectionType()) {
    case kNoSelection:
      return;
    case kCaretSelection:
      rect = LayoutRect(AbsoluteCaretBounds());
      break;
    case kRangeSelection:
      rect = LayoutRect(
          reveal_extent_option == kRevealExtent
              ? AbsoluteCaretBoundsOf(CreateVisiblePosition(
                    ComputeVisibleSelectionInDOMTree().Extent()))
              : AbsoluteSelectionBoundsOf(ComputeVisibleSelectionInFlatTree()));
      break;
  }

  Position start = ComputeVisibleSelectionInDOMTree().Start();
  DCHECK(start.AnchorNode());
  if (start.AnchorNode() && start.AnchorNode()->GetLayoutObject()) {
    // FIXME: This code only handles scrolling the startContainer's layer, but
    // the selection rect could intersect more than just that.
    if (DocumentLoader* document_loader = frame_->Loader().GetDocumentLoader())
      document_loader->GetInitialScrollState().was_scrolled_by_user = true;
    if (start.AnchorNode()->GetLayoutObject()->ScrollRectToVisible(
            rect, alignment, alignment))
      UpdateAppearance();
  }
}

void FrameSelection::SetSelectionFromNone() {
  // Put a caret inside the body if the entire frame is editable (either the
  // entire WebView is editable or designMode is on for this document).

  Document* document = frame_->GetDocument();
  if (!ComputeVisibleSelectionInDOMTreeDeprecated().IsNone() ||
      !(blink::HasEditableStyle(*document)))
    return;

  Element* document_element = document->documentElement();
  if (!document_element)
    return;
  if (HTMLBodyElement* body =
          Traversal<HTMLBodyElement>::FirstChild(*document_element)) {
    SetSelection(SelectionInDOMTree::Builder()
                     .Collapse(FirstPositionInOrBeforeNode(body))
                     .Build());
  }
}

// TODO(yoichio): We should have LocalFrame having FrameCaret,
// Editor and PendingSelection using FrameCaret directly
// and get rid of this.
bool FrameSelection::ShouldShowBlockCursor() const {
  return frame_caret_->ShouldShowBlockCursor();
}

// TODO(yoichio): We should have LocalFrame having FrameCaret,
// Editor and PendingSelection using FrameCaret directly
// and get rid of this.
// TODO(yoichio): We should use "caret-shape" in "CSS Basic User Interface
// Module Level 4" https://drafts.csswg.org/css-ui-4/
// To use "caret-shape", we need to expose inserting mode information to CSS;
// https://github.com/w3c/csswg-drafts/issues/133
void FrameSelection::SetShouldShowBlockCursor(bool should_show_block_cursor) {
  frame_caret_->SetShouldShowBlockCursor(should_show_block_cursor);
}

#ifndef NDEBUG

void FrameSelection::ShowTreeForThis() const {
  ComputeVisibleSelectionInDOMTreeDeprecated().ShowTreeForThis();
}

#endif

DEFINE_TRACE(FrameSelection) {
  visitor->Trace(frame_);
  visitor->Trace(layout_selection_);
  visitor->Trace(selection_editor_);
  visitor->Trace(frame_caret_);
  SynchronousMutationObserver::Trace(visitor);
}

void FrameSelection::ScheduleVisualUpdate() const {
  if (Page* page = frame_->GetPage())
    page->Animator().ScheduleVisualUpdate(&frame_->LocalFrameRoot());
}

void FrameSelection::ScheduleVisualUpdateForPaintInvalidationIfNeeded() const {
  if (FrameView* frame_view = frame_->View())
    frame_view->ScheduleVisualUpdateForPaintInvalidationIfNeeded();
}

bool FrameSelection::SelectWordAroundPosition(const VisiblePosition& position) {
  static const EWordSide kWordSideList[2] = {kRightWordIfOnBoundary,
                                             kLeftWordIfOnBoundary};
  for (EWordSide word_side : kWordSideList) {
    // TODO(yoichio): We should have Position version of |start/endOfWord|
    // for avoiding unnecessary canonicalization.
    VisiblePosition start = StartOfWord(position, word_side);
    VisiblePosition end = EndOfWord(position, word_side);
    String text =
        PlainText(EphemeralRange(start.DeepEquivalent(), end.DeepEquivalent()));
    if (!text.IsEmpty() && !IsSeparator(text.CharacterStartingAt(0))) {
      SetSelection(SelectionInDOMTree::Builder()
                       .Collapse(start.ToPositionWithAffinity())
                       .Extend(end.DeepEquivalent())
                       .Build(),
                   kCloseTyping | kClearTypingStyle,
                   CursorAlignOnScroll::kIfNeeded, kWordGranularity);
      return true;
    }
  }

  return false;
}

GranularityStrategy* FrameSelection::GetGranularityStrategy() {
  // We do lazy initalization for m_granularityStrategy, because if we
  // initialize it right in the constructor - the correct settings may not be
  // set yet.
  SelectionStrategy strategy_type = SelectionStrategy::kCharacter;
  Settings* settings = frame_ ? frame_->GetSettings() : 0;
  if (settings &&
      settings->GetSelectionStrategy() == SelectionStrategy::kDirection)
    strategy_type = SelectionStrategy::kDirection;

  if (granularity_strategy_ &&
      granularity_strategy_->GetType() == strategy_type)
    return granularity_strategy_.get();

  if (strategy_type == SelectionStrategy::kDirection)
    granularity_strategy_ = WTF::MakeUnique<DirectionGranularityStrategy>();
  else
    granularity_strategy_ = WTF::MakeUnique<CharacterGranularityStrategy>();
  return granularity_strategy_.get();
}

void FrameSelection::MoveRangeSelectionExtent(const IntPoint& contents_point) {
  if (ComputeVisibleSelectionInDOMTree().IsNone())
    return;

  const SetSelectionOptions kOptions =
      FrameSelection::kCloseTyping | FrameSelection::kClearTypingStyle |
      FrameSelection::kDoNotClearStrategy | kUserTriggered;
  SetSelection(
      SelectionInDOMTree::Builder(
          GetGranularityStrategy()->UpdateExtent(contents_point, frame_))
          .SetIsHandleVisible(true)
          .Build(),
      kOptions);
}

// TODO(yosin): We should make |FrameSelection::moveRangeSelection()| to take
// two |IntPoint| instead of two |VisiblePosition| like
// |moveRangeSelectionExtent()|.
void FrameSelection::MoveRangeSelection(const VisiblePosition& base_position,
                                        const VisiblePosition& extent_position,
                                        TextGranularity granularity) {
  SelectionInDOMTree new_selection =
      SelectionInDOMTree::Builder()
          .SetBaseAndExtentDeprecated(base_position.DeepEquivalent(),
                                      extent_position.DeepEquivalent())
          .SetAffinity(base_position.Affinity())
          .SetGranularity(granularity)
          .SetIsHandleVisible(IsHandleVisible())
          .Build();

  if (new_selection.IsNone())
    return;

  SetSelection(new_selection, kCloseTyping | kClearTypingStyle,
               CursorAlignOnScroll::kIfNeeded, granularity);
}

void FrameSelection::SetCaretVisible(bool caret_is_visible) {
  frame_caret_->SetCaretVisibility(caret_is_visible ? CaretVisibility::kVisible
                                                    : CaretVisibility::kHidden);
}

void FrameSelection::SetCaretBlinkingSuspended(bool suspended) {
  frame_caret_->SetCaretBlinkingSuspended(suspended);
}

bool FrameSelection::IsCaretBlinkingSuspended() const {
  return frame_caret_->IsCaretBlinkingSuspended();
}

void FrameSelection::CacheRangeOfDocument(Range* range) {
  selection_editor_->CacheRangeOfDocument(range);
}

Range* FrameSelection::DocumentCachedRange() const {
  return selection_editor_->DocumentCachedRange();
}

void FrameSelection::ClearDocumentCachedRange() {
  selection_editor_->ClearDocumentCachedRange();
}

std::pair<int, int> FrameSelection::LayoutSelectionStartEnd() {
  return layout_selection_->SelectionStartEnd();
}

void FrameSelection::ClearLayoutSelection() {
  layout_selection_->ClearSelection();
}

}  // namespace blink

#ifndef NDEBUG

void showTree(const blink::FrameSelection& sel) {
  sel.ShowTreeForThis();
}

void showTree(const blink::FrameSelection* sel) {
  if (sel)
    sel->ShowTreeForThis();
  else
    LOG(INFO) << "Cannot showTree for <null> FrameSelection.";
}

#endif
