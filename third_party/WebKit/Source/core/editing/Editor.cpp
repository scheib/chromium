/*
 * Copyright (C) 2006, 2007, 2008, 2011 Apple Inc. All rights reserved.
 * Copyright (C) 2008 Nokia Corporation and/or its subsidiary(-ies)
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

#include "core/editing/Editor.h"

#include "bindings/core/v8/ExceptionState.h"
#include "core/CSSPropertyNames.h"
#include "core/EventNames.h"
#include "core/HTMLNames.h"
#include "core/InputTypeNames.h"
#include "core/clipboard/DataObject.h"
#include "core/clipboard/DataTransfer.h"
#include "core/clipboard/Pasteboard.h"
#include "core/css/CSSComputedStyleDeclaration.h"
#include "core/css/StylePropertySet.h"
#include "core/dom/AXObjectCache.h"
#include "core/dom/DocumentFragment.h"
#include "core/dom/ElementTraversal.h"
#include "core/dom/NodeTraversal.h"
#include "core/dom/ParserContentPolicy.h"
#include "core/dom/Text.h"
#include "core/editing/EditingStyleUtilities.h"
#include "core/editing/EditingUtilities.h"
#include "core/editing/InputMethodController.h"
#include "core/editing/RenderedPosition.h"
#include "core/editing/VisibleUnits.h"
#include "core/editing/commands/ApplyStyleCommand.h"
#include "core/editing/commands/DeleteSelectionCommand.h"
#include "core/editing/commands/IndentOutdentCommand.h"
#include "core/editing/commands/InsertListCommand.h"
#include "core/editing/commands/RemoveFormatCommand.h"
#include "core/editing/commands/ReplaceSelectionCommand.h"
#include "core/editing/commands/SimplifyMarkupCommand.h"
#include "core/editing/commands/TypingCommand.h"
#include "core/editing/commands/UndoStack.h"
#include "core/editing/iterators/SearchBuffer.h"
#include "core/editing/markers/DocumentMarkerController.h"
#include "core/editing/serializers/Serialization.h"
#include "core/editing/spellcheck/SpellChecker.h"
#include "core/events/ClipboardEvent.h"
#include "core/events/KeyboardEvent.h"
#include "core/events/ScopedEventQueue.h"
#include "core/events/TextEvent.h"
#include "core/frame/FrameView.h"
#include "core/frame/LocalFrame.h"
#include "core/frame/Settings.h"
#include "core/frame/UseCounter.h"
#include "core/html/HTMLBodyElement.h"
#include "core/html/HTMLCanvasElement.h"
#include "core/html/HTMLHtmlElement.h"
#include "core/html/HTMLImageElement.h"
#include "core/html/HTMLInputElement.h"
#include "core/html/HTMLTextAreaElement.h"
#include "core/html/parser/HTMLParserIdioms.h"
#include "core/input/EventHandler.h"
#include "core/inspector/ConsoleMessage.h"
#include "core/layout/HitTestResult.h"
#include "core/layout/LayoutImage.h"
#include "core/loader/EmptyClients.h"
#include "core/loader/resource/ImageResourceContent.h"
#include "core/page/DragData.h"
#include "core/page/EditorClient.h"
#include "core/page/FocusController.h"
#include "core/page/Page.h"
#include "core/svg/SVGImageElement.h"
#include "platform/KillRing.h"
#include "platform/loader/fetch/ResourceFetcher.h"
#include "platform/weborigin/KURL.h"
#include "platform/wtf/PtrUtil.h"
#include "platform/wtf/text/CharacterNames.h"

namespace blink {

using namespace HTMLNames;
using namespace WTF;
using namespace Unicode;

namespace {

void DispatchInputEvent(Element* target,
                        InputEvent::InputType input_type,
                        const String& data,
                        InputEvent::EventIsComposing is_composing) {
  if (!RuntimeEnabledFeatures::inputEventEnabled())
    return;
  if (!target)
    return;
  // TODO(chongz): Pass appreciate |ranges| after it's defined on spec.
  // http://w3c.github.io/editing/input-events.html#dom-inputevent-inputtype
  InputEvent* input_event =
      InputEvent::CreateInput(input_type, data, is_composing, nullptr);
  target->DispatchScopedEvent(input_event);
}

void DispatchInputEventEditableContentChanged(
    Element* start_root,
    Element* end_root,
    InputEvent::InputType input_type,
    const String& data,
    InputEvent::EventIsComposing is_composing) {
  if (start_root)
    DispatchInputEvent(start_root, input_type, data, is_composing);
  if (end_root && end_root != start_root)
    DispatchInputEvent(end_root, input_type, data, is_composing);
}

InputEvent::EventIsComposing IsComposingFromCommand(
    const CompositeEditCommand* command) {
  if (command->IsTypingCommand() &&
      ToTypingCommand(command)->CompositionType() !=
          TypingCommand::kTextCompositionNone)
    return InputEvent::EventIsComposing::kIsComposing;
  return InputEvent::EventIsComposing::kNotComposing;
}

bool IsInPasswordFieldWithUnrevealedPassword(const Position& position) {
  TextControlElement* text_control = EnclosingTextControl(position);
  if (!isHTMLInputElement(text_control))
    return false;
  HTMLInputElement* input = toHTMLInputElement(text_control);
  return (input->type() == InputTypeNames::password) &&
         !input->ShouldRevealPassword();
}

EphemeralRange ComputeRangeForTranspose(LocalFrame& frame) {
  const VisibleSelection& selection =
      frame.Selection().ComputeVisibleSelectionInDOMTree();
  if (!selection.IsCaret())
    return EphemeralRange();

  // Make a selection that goes back one character and forward two characters.
  const VisiblePosition& caret = selection.VisibleStart();
  const VisiblePosition& next =
      IsEndOfParagraph(caret) ? caret : NextPositionOf(caret);
  const VisiblePosition& previous = PreviousPositionOf(next);
  if (next.DeepEquivalent() == previous.DeepEquivalent())
    return EphemeralRange();
  const VisiblePosition& previous_of_previous = PreviousPositionOf(previous);
  if (!InSameParagraph(next, previous_of_previous))
    return EphemeralRange();
  return MakeRange(previous_of_previous, next);
}

}  // anonymous namespace

Editor::RevealSelectionScope::RevealSelectionScope(Editor* editor)
    : editor_(editor) {
  ++editor_->prevent_reveal_selection_;
}

Editor::RevealSelectionScope::~RevealSelectionScope() {
  DCHECK(editor_->prevent_reveal_selection_);
  --editor_->prevent_reveal_selection_;
  if (!editor_->prevent_reveal_selection_) {
    editor_->GetFrame().Selection().RevealSelection(
        ScrollAlignment::kAlignToEdgeIfNeeded, kRevealExtent);
  }
}

// When an event handler has moved the selection outside of a text control
// we should use the target control's selection for this editing operation.
// TODO(yosin): We should make |Editor::selectionForCommand()| to return
// |SelectionInDOMTree| instead of |VisibleSelection|.
VisibleSelection Editor::SelectionForCommand(Event* event) {
  VisibleSelection selection =
      GetFrame().Selection().ComputeVisibleSelectionInDOMTree();
  if (!event)
    return selection;
  // If the target is a text control, and the current selection is outside of
  // its shadow tree, then use the saved selection for that text control.
  TextControlElement* text_control_of_selection_start =
      EnclosingTextControl(selection.Start());
  TextControlElement* text_control_of_target =
      IsTextControlElement(*event->target()->ToNode())
          ? ToTextControlElement(event->target()->ToNode())
          : nullptr;
  if (text_control_of_target &&
      (selection.Start().IsNull() ||
       text_control_of_target != text_control_of_selection_start)) {
    const SelectionInDOMTree& select = text_control_of_target->Selection();
    if (!select.IsNone())
      return CreateVisibleSelection(select);
  }
  return selection;
}

// Function considers Mac editing behavior a fallback when Page or Settings is
// not available.
EditingBehavior Editor::Behavior() const {
  if (!GetFrame().GetSettings())
    return EditingBehavior(kEditingMacBehavior);

  return EditingBehavior(GetFrame().GetSettings()->GetEditingBehaviorType());
}

static EditorClient& GetEmptyEditorClient() {
  DEFINE_STATIC_LOCAL(EmptyEditorClient, client, ());
  return client;
}

EditorClient& Editor::Client() const {
  if (Page* page = GetFrame().GetPage())
    return page->GetEditorClient();
  return GetEmptyEditorClient();
}

static bool IsCaretAtStartOfWrappedLine(const FrameSelection& selection) {
  if (!selection.ComputeVisibleSelectionInDOMTree().IsCaret())
    return false;
  if (selection.GetSelectionInDOMTree().Affinity() != TextAffinity::kDownstream)
    return false;
  const Position& position =
      selection.ComputeVisibleSelectionInDOMTree().Start();
  return !InSameLine(PositionWithAffinity(position, TextAffinity::kUpstream),
                     PositionWithAffinity(position, TextAffinity::kDownstream));
}

bool Editor::HandleTextEvent(TextEvent* event) {
  // Default event handling for Drag and Drop will be handled by DragController
  // so we leave the event for it.
  if (event->IsDrop())
    return false;

  // Default event handling for IncrementalInsertion will be handled by
  // TypingCommand::insertText(), so we leave the event for it.
  if (event->IsIncrementalInsertion())
    return false;

  // TODO(editing-dev): The use of updateStyleAndLayoutIgnorePendingStylesheets
  // needs to be audited.  See http://crbug.com/590369 for more details.
  frame_->GetDocument()->UpdateStyleAndLayoutIgnorePendingStylesheets();

  if (event->IsPaste()) {
    if (event->PastingFragment()) {
      ReplaceSelectionWithFragment(
          event->PastingFragment(), false, event->ShouldSmartReplace(),
          event->ShouldMatchStyle(), InputEvent::InputType::kInsertFromPaste);
    } else {
      ReplaceSelectionWithText(event->data(), false,
                               event->ShouldSmartReplace(),
                               InputEvent::InputType::kInsertFromPaste);
    }
    return true;
  }

  String data = event->data();
  if (data == "\n") {
    if (event->IsLineBreak())
      return InsertLineBreak();
    return InsertParagraphSeparator();
  }

  // Typing spaces at the beginning of wrapped line is confusing, because
  // inserted spaces would appear in the previous line.
  // Insert a line break automatically so that the spaces appear at the caret.
  // TODO(kojii): rich editing has the same issue, but has more options and
  // needs coordination with JS. Enable for plaintext only for now and collect
  // feedback.
  if (data == " " && !CanEditRichly() &&
      IsCaretAtStartOfWrappedLine(GetFrame().Selection())) {
    InsertLineBreak();
  }

  return InsertTextWithoutSendingTextEvent(data, false, event);
}

bool Editor::CanEdit() const {
  return GetFrame()
      .Selection()
      .ComputeVisibleSelectionInDOMTreeDeprecated()
      .RootEditableElement();
}

bool Editor::CanEditRichly() const {
  return GetFrame()
      .Selection()
      .ComputeVisibleSelectionInDOMTreeDeprecated()
      .IsContentRichlyEditable();
}

// WinIE uses onbeforecut and onbeforepaste to enables the cut and paste menu
// items. They also send onbeforecopy, apparently for symmetry, but it doesn't
// affect the menu items. We need to use onbeforecopy as a real menu enabler
// because we allow elements that are not normally selectable to implement
// copy/paste (like divs, or a document body).

bool Editor::CanDHTMLCut() {
  // TODO(editing-dev): The use of updateStyleAndLayoutIgnorePendingStylesheets
  // needs to be audited.  See http://crbug.com/590369 for more details.
  GetFrame().GetDocument()->UpdateStyleAndLayoutIgnorePendingStylesheets();
  return !IsInPasswordField(GetFrame()
                                .Selection()
                                .ComputeVisibleSelectionInDOMTree()
                                .Start()) &&
         !DispatchCPPEvent(EventTypeNames::beforecut, kDataTransferNumb);
}

bool Editor::CanDHTMLCopy() {
  // TODO(editing-dev): The use of updateStyleAndLayoutIgnorePendingStylesheets
  // needs to be audited.  See http://crbug.com/590369 for more details.
  GetFrame().GetDocument()->UpdateStyleAndLayoutIgnorePendingStylesheets();
  return !IsInPasswordField(GetFrame()
                                .Selection()
                                .ComputeVisibleSelectionInDOMTree()
                                .Start()) &&
         !DispatchCPPEvent(EventTypeNames::beforecopy, kDataTransferNumb);
}

bool Editor::CanCut() const {
  return CanCopy() && CanDelete();
}

static HTMLImageElement* ImageElementFromImageDocument(Document* document) {
  if (!document)
    return 0;
  if (!document->IsImageDocument())
    return 0;

  HTMLElement* body = document->body();
  if (!body)
    return 0;

  Node* node = body->firstChild();
  if (!isHTMLImageElement(node))
    return 0;
  return toHTMLImageElement(node);
}

bool Editor::CanCopy() const {
  if (ImageElementFromImageDocument(GetFrame().GetDocument()))
    return true;
  FrameSelection& selection = GetFrame().Selection();
  return selection.ComputeVisibleSelectionInDOMTreeDeprecated().IsRange() &&
         !IsInPasswordFieldWithUnrevealedPassword(
             GetFrame().Selection().ComputeVisibleSelectionInDOMTree().Start());
}

bool Editor::CanPaste() const {
  return CanEdit();
}

bool Editor::CanDelete() const {
  FrameSelection& selection = GetFrame().Selection();
  return selection.ComputeVisibleSelectionInDOMTreeDeprecated().IsRange() &&
         selection.ComputeVisibleSelectionInDOMTree().RootEditableElement();
}

bool Editor::SmartInsertDeleteEnabled() const {
  if (Settings* settings = GetFrame().GetSettings())
    return settings->GetSmartInsertDeleteEnabled();
  return false;
}

bool Editor::CanSmartCopyOrDelete() const {
  return SmartInsertDeleteEnabled() &&
         GetFrame().Selection().Granularity() == kWordGranularity;
}

bool Editor::IsSelectTrailingWhitespaceEnabled() const {
  if (Settings* settings = GetFrame().GetSettings())
    return settings->GetSelectTrailingWhitespaceEnabled();
  return false;
}

bool Editor::DeleteWithDirection(DeleteDirection direction,
                                 TextGranularity granularity,
                                 bool kill_ring,
                                 bool is_typing_action) {
  if (!CanEdit())
    return false;

  EditingState editing_state;
  if (GetFrame()
          .Selection()
          .ComputeVisibleSelectionInDOMTreeDeprecated()
          .IsRange()) {
    if (is_typing_action) {
      DCHECK(GetFrame().GetDocument());
      TypingCommand::DeleteKeyPressed(
          *GetFrame().GetDocument(),
          CanSmartCopyOrDelete() ? TypingCommand::kSmartDelete : 0,
          granularity);
      RevealSelectionAfterEditingOperation();
    } else {
      if (kill_ring)
        AddToKillRing(SelectedRange());
      DeleteSelectionWithSmartDelete(
          CanSmartCopyOrDelete() ? DeleteMode::kSmart : DeleteMode::kSimple,
          DeletionInputTypeFromTextGranularity(direction, granularity));
      // Implicitly calls revealSelectionAfterEditingOperation().
    }
  } else {
    TypingCommand::Options options = 0;
    if (CanSmartCopyOrDelete())
      options |= TypingCommand::kSmartDelete;
    if (kill_ring)
      options |= TypingCommand::kKillRing;
    switch (direction) {
      case DeleteDirection::kForward:
        DCHECK(GetFrame().GetDocument());
        TypingCommand::ForwardDeleteKeyPressed(
            *GetFrame().GetDocument(), &editing_state, options, granularity);
        if (editing_state.IsAborted())
          return false;
        break;
      case DeleteDirection::kBackward:
        DCHECK(GetFrame().GetDocument());
        TypingCommand::DeleteKeyPressed(*GetFrame().GetDocument(), options,
                                        granularity);
        break;
    }
    RevealSelectionAfterEditingOperation();
  }

  // FIXME: We should to move this down into deleteKeyPressed.
  // clear the "start new kill ring sequence" setting, because it was set to
  // true when the selection was updated by deleting the range
  if (kill_ring)
    SetStartNewKillRingSequence(false);

  return true;
}

void Editor::DeleteSelectionWithSmartDelete(
    DeleteMode delete_mode,
    InputEvent::InputType input_type,
    const Position& reference_move_position) {
  if (GetFrame()
          .Selection()
          .ComputeVisibleSelectionInDOMTreeDeprecated()
          .IsNone())
    return;

  const bool kMergeBlocksAfterDelete = true;
  const bool kExpandForSpecialElements = false;
  const bool kSanitizeMarkup = true;
  DCHECK(GetFrame().GetDocument());
  DeleteSelectionCommand::Create(
      *GetFrame().GetDocument(), delete_mode == DeleteMode::kSmart,
      kMergeBlocksAfterDelete, kExpandForSpecialElements, kSanitizeMarkup,
      input_type, reference_move_position)
      ->Apply();
}

void Editor::PasteAsPlainText(const String& pasting_text, bool smart_replace) {
  Element* target = FindEventTargetFromSelection();
  if (!target)
    return;
  target->DispatchEvent(TextEvent::CreateForPlainTextPaste(
      GetFrame().DomWindow(), pasting_text, smart_replace));
}

void Editor::PasteAsFragment(DocumentFragment* pasting_fragment,
                             bool smart_replace,
                             bool match_style) {
  Element* target = FindEventTargetFromSelection();
  if (!target)
    return;
  target->DispatchEvent(TextEvent::CreateForFragmentPaste(
      GetFrame().DomWindow(), pasting_fragment, smart_replace, match_style));
}

bool Editor::TryDHTMLCopy() {
  // TODO(editing-dev): The use of updateStyleAndLayoutIgnorePendingStylesheets
  // needs to be audited.  See http://crbug.com/590369 for more details.
  GetFrame().GetDocument()->UpdateStyleAndLayoutIgnorePendingStylesheets();
  if (IsInPasswordField(
          GetFrame().Selection().ComputeVisibleSelectionInDOMTree().Start()))
    return false;

  return !DispatchCPPEvent(EventTypeNames::copy, kDataTransferWritable);
}

bool Editor::TryDHTMLCut() {
  // TODO(editing-dev): The use of updateStyleAndLayoutIgnorePendingStylesheets
  // needs to be audited.  See http://crbug.com/590369 for more details.
  GetFrame().GetDocument()->UpdateStyleAndLayoutIgnorePendingStylesheets();
  if (IsInPasswordField(
          GetFrame().Selection().ComputeVisibleSelectionInDOMTree().Start()))
    return false;

  return !DispatchCPPEvent(EventTypeNames::cut, kDataTransferWritable);
}

bool Editor::TryDHTMLPaste(PasteMode paste_mode) {
  return !DispatchCPPEvent(EventTypeNames::paste, kDataTransferReadable,
                           paste_mode);
}

void Editor::PasteAsPlainTextWithPasteboard(Pasteboard* pasteboard) {
  String text = pasteboard->PlainText();
  PasteAsPlainText(text, CanSmartReplaceWithPasteboard(pasteboard));
}

void Editor::PasteWithPasteboard(Pasteboard* pasteboard) {
  DocumentFragment* fragment = nullptr;
  bool chose_plain_text = false;

  if (pasteboard->IsHTMLAvailable()) {
    unsigned fragment_start = 0;
    unsigned fragment_end = 0;
    KURL url;
    String markup = pasteboard->ReadHTML(url, fragment_start, fragment_end);
    if (!markup.IsEmpty()) {
      DCHECK(GetFrame().GetDocument());
      fragment = CreateFragmentFromMarkupWithContext(
          *GetFrame().GetDocument(), markup, fragment_start, fragment_end, url,
          kDisallowScriptingAndPluginContent);
    }
  }

  if (!fragment) {
    String text = pasteboard->PlainText();
    if (!text.IsEmpty()) {
      chose_plain_text = true;

      // TODO(editing-dev): Use of updateStyleAndLayoutIgnorePendingStylesheets
      // needs to be audited.  See http://crbug.com/590369 for more details.
      // |selectedRange| requires clean layout for visible selection
      // normalization.
      GetFrame().GetDocument()->UpdateStyleAndLayoutIgnorePendingStylesheets();

      fragment = CreateFragmentFromText(SelectedRange(), text);
    }
  }

  if (fragment)
    PasteAsFragment(fragment, CanSmartReplaceWithPasteboard(pasteboard),
                    chose_plain_text);
}

void Editor::WriteSelectionToPasteboard() {
  KURL url = GetFrame().GetDocument()->Url();
  String html = GetFrame().Selection().SelectedHTMLForClipboard();
  String plain_text = GetFrame().SelectedTextForClipboard();
  Pasteboard::GeneralPasteboard()->WriteHTML(html, url, plain_text,
                                             CanSmartCopyOrDelete());
}

static PassRefPtr<Image> ImageFromNode(const Node& node) {
  DCHECK(!node.GetDocument().NeedsLayoutTreeUpdate());
  DocumentLifecycle::DisallowTransitionScope disallow_transition(
      node.GetDocument().Lifecycle());

  LayoutObject* layout_object = node.GetLayoutObject();
  if (!layout_object)
    return nullptr;

  if (layout_object->IsCanvas()) {
    return toHTMLCanvasElement(const_cast<Node&>(node))
        .CopiedImage(kFrontBuffer, kPreferNoAcceleration,
                     kSnapshotReasonCopyToClipboard);
  }

  if (layout_object->IsImage()) {
    LayoutImage* layout_image = ToLayoutImage(layout_object);
    if (!layout_image)
      return nullptr;

    ImageResourceContent* cached_image = layout_image->CachedImage();
    if (!cached_image || cached_image->ErrorOccurred())
      return nullptr;
    return cached_image->GetImage();
  }

  return nullptr;
}

static void WriteImageNodeToPasteboard(Pasteboard* pasteboard,
                                       Node* node,
                                       const String& title) {
  DCHECK(pasteboard);
  DCHECK(node);

  RefPtr<Image> image = ImageFromNode(*node);
  if (!image.Get())
    return;

  // FIXME: This should probably be reconciled with
  // HitTestResult::absoluteImageURL.
  AtomicString url_string;
  if (isHTMLImageElement(*node) || isHTMLInputElement(*node))
    url_string = ToHTMLElement(node)->getAttribute(srcAttr);
  else if (isSVGImageElement(*node))
    url_string = ToSVGElement(node)->ImageSourceURL();
  else if (isHTMLEmbedElement(*node) || isHTMLObjectElement(*node) ||
           isHTMLCanvasElement(*node))
    url_string = ToHTMLElement(node)->ImageSourceURL();
  KURL url = url_string.IsEmpty()
                 ? KURL()
                 : node->GetDocument().CompleteURL(
                       StripLeadingAndTrailingHTMLSpaces(url_string));

  pasteboard->WriteImage(image.Get(), url, title);
}

// Returns whether caller should continue with "the default processing", which
// is the same as the event handler NOT setting the return value to false
bool Editor::DispatchCPPEvent(const AtomicString& event_type,
                              DataTransferAccessPolicy policy,
                              PasteMode paste_mode) {
  Element* target = FindEventTargetFromSelection();
  if (!target)
    return true;

  DataTransfer* data_transfer =
      DataTransfer::Create(DataTransfer::kCopyAndPaste, policy,
                           policy == kDataTransferWritable
                               ? DataObject::Create()
                               : DataObject::CreateFromPasteboard(paste_mode));

  Event* evt = ClipboardEvent::Create(event_type, true, true, data_transfer);
  target->DispatchEvent(evt);
  bool no_default_processing = evt->defaultPrevented();
  if (no_default_processing && policy == kDataTransferWritable)
    Pasteboard::GeneralPasteboard()->WriteDataObject(
        data_transfer->GetDataObject());

  // invalidate clipboard here for security
  data_transfer->SetAccessPolicy(kDataTransferNumb);

  return !no_default_processing;
}

bool Editor::CanSmartReplaceWithPasteboard(Pasteboard* pasteboard) {
  return SmartInsertDeleteEnabled() && pasteboard->CanSmartReplace();
}

void Editor::ReplaceSelectionWithFragment(DocumentFragment* fragment,
                                          bool select_replacement,
                                          bool smart_replace,
                                          bool match_style,
                                          InputEvent::InputType input_type) {
  DCHECK(!GetFrame().GetDocument()->NeedsLayoutTreeUpdate());
  if (GetFrame()
          .Selection()
          .ComputeVisibleSelectionInDOMTreeDeprecated()
          .IsNone() ||
      !GetFrame()
           .Selection()
           .ComputeVisibleSelectionInDOMTreeDeprecated()
           .IsContentEditable() ||
      !fragment)
    return;

  ReplaceSelectionCommand::CommandOptions options =
      ReplaceSelectionCommand::kPreventNesting |
      ReplaceSelectionCommand::kSanitizeFragment;
  if (select_replacement)
    options |= ReplaceSelectionCommand::kSelectReplacement;
  if (smart_replace)
    options |= ReplaceSelectionCommand::kSmartReplace;
  if (match_style)
    options |= ReplaceSelectionCommand::kMatchStyle;
  DCHECK(GetFrame().GetDocument());
  ReplaceSelectionCommand::Create(*GetFrame().GetDocument(), fragment, options,
                                  input_type)
      ->Apply();
  RevealSelectionAfterEditingOperation();
}

void Editor::ReplaceSelectionWithText(const String& text,
                                      bool select_replacement,
                                      bool smart_replace,
                                      InputEvent::InputType input_type) {
  ReplaceSelectionWithFragment(CreateFragmentFromText(SelectedRange(), text),
                               select_replacement, smart_replace, true,
                               input_type);
}

// TODO(xiaochengh): Merge it with |replaceSelectionWithFragment()|.
void Editor::ReplaceSelectionAfterDragging(DocumentFragment* fragment,
                                           InsertMode insert_mode,
                                           DragSourceType drag_source_type) {
  ReplaceSelectionCommand::CommandOptions options =
      ReplaceSelectionCommand::kSelectReplacement |
      ReplaceSelectionCommand::kPreventNesting;
  if (insert_mode == InsertMode::kSmart)
    options |= ReplaceSelectionCommand::kSmartReplace;
  if (drag_source_type == DragSourceType::kPlainTextSource)
    options |= ReplaceSelectionCommand::kMatchStyle;
  DCHECK(GetFrame().GetDocument());
  ReplaceSelectionCommand::Create(*GetFrame().GetDocument(), fragment, options,
                                  InputEvent::InputType::kInsertFromDrop)
      ->Apply();
}

bool Editor::DeleteSelectionAfterDraggingWithEvents(
    Element* drag_source,
    DeleteMode delete_mode,
    const Position& reference_move_position) {
  if (!drag_source || !drag_source->isConnected())
    return true;

  // Dispatch 'beforeinput'.
  const bool should_delete =
      DispatchBeforeInputEditorCommand(
          drag_source, InputEvent::InputType::kDeleteByDrag,
          TargetRangesForInputEvent(*drag_source)) ==
      DispatchEventResult::kNotCanceled;

  // 'beforeinput' event handler may destroy frame, return false to cancel
  // remaining actions;
  if (frame_->GetDocument()->GetFrame() != frame_)
    return false;

  if (should_delete && drag_source->isConnected()) {
    DeleteSelectionWithSmartDelete(delete_mode,
                                   InputEvent::InputType::kDeleteByDrag,
                                   reference_move_position);
  }

  return true;
}

bool Editor::ReplaceSelectionAfterDraggingWithEvents(
    Element* drop_target,
    DragData* drag_data,
    DocumentFragment* fragment,
    Range* drop_caret_range,
    InsertMode insert_mode,
    DragSourceType drag_source_type) {
  if (!drop_target || !drop_target->isConnected())
    return true;

  // Dispatch 'beforeinput'.
  DataTransfer* data_transfer =
      DataTransfer::Create(DataTransfer::kDragAndDrop, kDataTransferReadable,
                           drag_data->PlatformData());
  data_transfer->SetSourceOperation(drag_data->DraggingSourceOperationMask());
  const bool should_insert =
      DispatchBeforeInputDataTransfer(
          drop_target, InputEvent::InputType::kInsertFromDrop, data_transfer) ==
      DispatchEventResult::kNotCanceled;

  // 'beforeinput' event handler may destroy frame, return false to cancel
  // remaining actions;
  if (frame_->GetDocument()->GetFrame() != frame_)
    return false;

  if (should_insert && drop_target->isConnected())
    ReplaceSelectionAfterDragging(fragment, insert_mode, drag_source_type);

  return true;
}

EphemeralRange Editor::SelectedRange() {
  return GetFrame()
      .Selection()
      .ComputeVisibleSelectionInDOMTreeDeprecated()
      .ToNormalizedEphemeralRange();
}

bool Editor::CanDeleteRange(const EphemeralRange& range) const {
  if (range.IsCollapsed())
    return false;

  Node* start_container = range.StartPosition().ComputeContainerNode();
  Node* end_container = range.EndPosition().ComputeContainerNode();
  if (!start_container || !end_container)
    return false;

  return HasEditableStyle(*start_container) && HasEditableStyle(*end_container);
}

void Editor::RespondToChangedContents(const Position& position) {
  if (GetFrame().GetSettings() &&
      GetFrame().GetSettings()->GetAccessibilityEnabled()) {
    Node* node = position.AnchorNode();
    if (AXObjectCache* cache =
            GetFrame().GetDocument()->ExistingAXObjectCache())
      cache->HandleEditableTextContentChanged(node);
  }

  GetSpellChecker().RespondToChangedContents();
  Client().RespondToChangedContents();
}

void Editor::RemoveFormattingAndStyle() {
  DCHECK(GetFrame().GetDocument());
  RemoveFormatCommand::Create(*GetFrame().GetDocument())->Apply();
}

void Editor::RegisterCommandGroup(CompositeEditCommand* command_group_wrapper) {
  DCHECK(command_group_wrapper->IsCommandGroupWrapper());
  last_edit_command_ = command_group_wrapper;
}

Element* Editor::FindEventTargetFrom(const VisibleSelection& selection) const {
  Element* target = AssociatedElementOf(selection.Start());
  if (!target)
    target = GetFrame().GetDocument()->body();

  return target;
}

Element* Editor::FindEventTargetFromSelection() const {
  return FindEventTargetFrom(
      GetFrame().Selection().ComputeVisibleSelectionInDOMTreeDeprecated());
}

void Editor::ApplyStyle(StylePropertySet* style,
                        InputEvent::InputType input_type) {
  switch (GetFrame()
              .Selection()
              .ComputeVisibleSelectionInDOMTreeDeprecated()
              .GetSelectionType()) {
    case kNoSelection:
      // do nothing
      break;
    case kCaretSelection:
      ComputeAndSetTypingStyle(style, input_type);
      break;
    case kRangeSelection:
      if (style) {
        DCHECK(GetFrame().GetDocument());
        ApplyStyleCommand::Create(*GetFrame().GetDocument(),
                                  EditingStyle::Create(style), input_type)
            ->Apply();
      }
      break;
  }
}

void Editor::ApplyParagraphStyle(StylePropertySet* style,
                                 InputEvent::InputType input_type) {
  if (GetFrame()
          .Selection()
          .ComputeVisibleSelectionInDOMTreeDeprecated()
          .IsNone() ||
      !style)
    return;
  DCHECK(GetFrame().GetDocument());
  ApplyStyleCommand::Create(*GetFrame().GetDocument(),
                            EditingStyle::Create(style), input_type,
                            ApplyStyleCommand::kForceBlockProperties)
      ->Apply();
}

void Editor::ApplyStyleToSelection(StylePropertySet* style,
                                   InputEvent::InputType input_type) {
  if (!style || style->IsEmpty() || !CanEditRichly())
    return;

  ApplyStyle(style, input_type);
}

void Editor::ApplyParagraphStyleToSelection(StylePropertySet* style,
                                            InputEvent::InputType input_type) {
  if (!style || style->IsEmpty() || !CanEditRichly())
    return;

  ApplyParagraphStyle(style, input_type);
}

bool Editor::SelectionStartHasStyle(CSSPropertyID property_id,
                                    const String& value) const {
  EditingStyle* style_to_check = EditingStyle::Create(property_id, value);
  EditingStyle* style_at_start =
      EditingStyleUtilities::CreateStyleAtSelectionStart(
          GetFrame().Selection().ComputeVisibleSelectionInDOMTreeDeprecated(),
          property_id == CSSPropertyBackgroundColor, style_to_check->Style());
  return style_to_check->TriStateOfStyle(style_at_start);
}

TriState Editor::SelectionHasStyle(CSSPropertyID property_id,
                                   const String& value) const {
  return EditingStyle::Create(property_id, value)
      ->TriStateOfStyle(
          GetFrame().Selection().ComputeVisibleSelectionInDOMTreeDeprecated());
}

String Editor::SelectionStartCSSPropertyValue(CSSPropertyID property_id) {
  EditingStyle* selection_style =
      EditingStyleUtilities::CreateStyleAtSelectionStart(
          GetFrame().Selection().ComputeVisibleSelectionInDOMTreeDeprecated(),
          property_id == CSSPropertyBackgroundColor);
  if (!selection_style || !selection_style->Style())
    return String();

  if (property_id == CSSPropertyFontSize)
    return String::Number(
        selection_style->LegacyFontSize(GetFrame().GetDocument()));
  return selection_style->Style()->GetPropertyValue(property_id);
}

static void DispatchEditableContentChangedEvents(Element* start_root,
                                                 Element* end_root) {
  if (start_root)
    start_root->DispatchEvent(
        Event::Create(EventTypeNames::webkitEditableContentChanged));
  if (end_root && end_root != start_root)
    end_root->DispatchEvent(
        Event::Create(EventTypeNames::webkitEditableContentChanged));
}

static SelectionInDOMTree CorrectedSelectionAfterCommand(
    const VisibleSelection& passed_selection,
    Document* document) {
  if (!passed_selection.Base().IsConnected() ||
      !passed_selection.Extent().IsConnected() ||
      passed_selection.Base().GetDocument() != document ||
      passed_selection.Base().GetDocument() !=
          passed_selection.Extent().GetDocument())
    return SelectionInDOMTree();
  return passed_selection.AsSelection();
}

void Editor::AppliedEditing(CompositeEditCommand* cmd) {
  DCHECK(!cmd->IsCommandGroupWrapper());
  EventQueueScope scope;

  // Request spell checking before any further DOM change.
  GetSpellChecker().MarkMisspellingsAfterApplyingCommand(*cmd);

  UndoStep* undo_step = cmd->GetUndoStep();
  DCHECK(undo_step);
  DispatchEditableContentChangedEvents(undo_step->StartingRootEditableElement(),
                                       undo_step->EndingRootEditableElement());
  // TODO(chongz): Filter empty InputType after spec is finalized.
  DispatchInputEventEditableContentChanged(
      undo_step->StartingRootEditableElement(),
      undo_step->EndingRootEditableElement(), cmd->GetInputType(),
      cmd->TextDataForInputEvent(), IsComposingFromCommand(cmd));

  const SelectionInDOMTree& new_selection = CorrectedSelectionAfterCommand(
      cmd->EndingSelection(), GetFrame().GetDocument());

  // Don't clear the typing style with this selection change. We do those things
  // elsewhere if necessary.
  ChangeSelectionAfterCommand(new_selection, 0);

  if (!cmd->PreservesTypingStyle())
    ClearTypingStyle();

  // Command will be equal to last edit command only in the case of typing
  if (last_edit_command_.Get() == cmd) {
    DCHECK(cmd->IsTypingCommand());
  } else if (last_edit_command_ && last_edit_command_->IsDragAndDropCommand() &&
             (cmd->GetInputType() == InputEvent::InputType::kDeleteByDrag ||
              cmd->GetInputType() == InputEvent::InputType::kInsertFromDrop)) {
    // Only register undo entry when combined with other commands.
    if (!last_edit_command_->GetUndoStep())
      undo_stack_->RegisterUndoStep(last_edit_command_->EnsureUndoStep());
    last_edit_command_->EnsureUndoStep()->SetEndingSelection(
        cmd->EnsureUndoStep()->EndingSelection());
    last_edit_command_->AppendCommandToUndoStep(cmd);
  } else {
    // Only register a new undo command if the command passed in is
    // different from the last command
    last_edit_command_ = cmd;
    undo_stack_->RegisterUndoStep(last_edit_command_->EnsureUndoStep());
  }

  RespondToChangedContents(new_selection.Base());
}

void Editor::UnappliedEditing(UndoStep* cmd) {
  EventQueueScope scope;

  DispatchEditableContentChangedEvents(cmd->StartingRootEditableElement(),
                                       cmd->EndingRootEditableElement());
  DispatchInputEventEditableContentChanged(
      cmd->StartingRootEditableElement(), cmd->EndingRootEditableElement(),
      InputEvent::InputType::kHistoryUndo, g_null_atom,
      InputEvent::EventIsComposing::kNotComposing);

  const SelectionInDOMTree& new_selection = CorrectedSelectionAfterCommand(
      cmd->StartingSelection(), GetFrame().GetDocument());
  ChangeSelectionAfterCommand(
      new_selection,
      FrameSelection::kCloseTyping | FrameSelection::kClearTypingStyle);

  last_edit_command_ = nullptr;
  undo_stack_->RegisterRedoStep(cmd);
  RespondToChangedContents(new_selection.Base());
}

void Editor::ReappliedEditing(UndoStep* cmd) {
  EventQueueScope scope;

  DispatchEditableContentChangedEvents(cmd->StartingRootEditableElement(),
                                       cmd->EndingRootEditableElement());
  DispatchInputEventEditableContentChanged(
      cmd->StartingRootEditableElement(), cmd->EndingRootEditableElement(),
      InputEvent::InputType::kHistoryRedo, g_null_atom,
      InputEvent::EventIsComposing::kNotComposing);

  const SelectionInDOMTree& new_selection = CorrectedSelectionAfterCommand(
      cmd->EndingSelection(), GetFrame().GetDocument());
  ChangeSelectionAfterCommand(
      new_selection,
      FrameSelection::kCloseTyping | FrameSelection::kClearTypingStyle);

  last_edit_command_ = nullptr;
  undo_stack_->RegisterUndoStep(cmd);
  RespondToChangedContents(new_selection.Base());
}

Editor* Editor::Create(LocalFrame& frame) {
  return new Editor(frame);
}

Editor::Editor(LocalFrame& frame)
    : frame_(&frame),
      undo_stack_(UndoStack::Create()),
      prevent_reveal_selection_(0),
      should_start_new_kill_ring_sequence_(false),
      // This is off by default, since most editors want this behavior (this
      // matches IE but not FF).
      should_style_with_css_(false),
      kill_ring_(WTF::WrapUnique(new KillRing)),
      are_marked_text_matches_highlighted_(false),
      default_paragraph_separator_(kEditorParagraphSeparatorIsDiv),
      overwrite_mode_enabled_(false) {}

Editor::~Editor() {}

void Editor::Clear() {
  GetFrame().GetInputMethodController().Clear();
  should_style_with_css_ = false;
  default_paragraph_separator_ = kEditorParagraphSeparatorIsDiv;
  last_edit_command_ = nullptr;
  undo_stack_->Clear();
}

bool Editor::InsertText(const String& text, KeyboardEvent* triggering_event) {
  return GetFrame().GetEventHandler().HandleTextInputEvent(text,
                                                           triggering_event);
}

bool Editor::InsertTextWithoutSendingTextEvent(
    const String& text,
    bool select_inserted_text,
    TextEvent* triggering_event,
    InputEvent::InputType input_type) {
  const VisibleSelection& selection = SelectionForCommand(triggering_event);
  if (!selection.IsContentEditable())
    return false;

  GetSpellChecker().UpdateMarkersForWordsAffectedByEditing(
      !text.IsEmpty() && IsSpaceOrNewline(text[0]));

  // Insert the text
  TypingCommand::InsertText(
      *selection.Start().GetDocument(), text, selection.AsSelection(),
      select_inserted_text ? TypingCommand::kSelectInsertedText : 0,
      triggering_event && triggering_event->IsComposition()
          ? TypingCommand::kTextCompositionConfirm
          : TypingCommand::kTextCompositionNone,
      false, input_type);

  // Reveal the current selection
  if (LocalFrame* edited_frame = selection.Start().GetDocument()->GetFrame()) {
    if (Page* page = edited_frame->GetPage()) {
      LocalFrame* focused_or_main_frame =
          ToLocalFrame(page->GetFocusController().FocusedOrMainFrame());
      focused_or_main_frame->Selection().RevealSelection(
          ScrollAlignment::kAlignCenterIfNeeded);
    }
  }

  return true;
}

bool Editor::InsertLineBreak() {
  if (!CanEdit())
    return false;

  VisiblePosition caret = GetFrame()
                              .Selection()
                              .ComputeVisibleSelectionInDOMTreeDeprecated()
                              .VisibleStart();
  bool align_to_edge = IsEndOfEditableOrNonEditableContent(caret);
  DCHECK(GetFrame().GetDocument());
  if (!TypingCommand::InsertLineBreak(*GetFrame().GetDocument()))
    return false;
  RevealSelectionAfterEditingOperation(
      align_to_edge ? ScrollAlignment::kAlignToEdgeIfNeeded
                    : ScrollAlignment::kAlignCenterIfNeeded);

  return true;
}

bool Editor::InsertParagraphSeparator() {
  if (!CanEdit())
    return false;

  if (!CanEditRichly())
    return InsertLineBreak();

  VisiblePosition caret = GetFrame()
                              .Selection()
                              .ComputeVisibleSelectionInDOMTreeDeprecated()
                              .VisibleStart();
  bool align_to_edge = IsEndOfEditableOrNonEditableContent(caret);
  DCHECK(GetFrame().GetDocument());
  EditingState editing_state;
  if (!TypingCommand::InsertParagraphSeparator(*GetFrame().GetDocument()))
    return false;
  RevealSelectionAfterEditingOperation(
      align_to_edge ? ScrollAlignment::kAlignToEdgeIfNeeded
                    : ScrollAlignment::kAlignCenterIfNeeded);

  return true;
}

void Editor::Cut(EditorCommandSource source) {
  if (TryDHTMLCut())
    return;  // DHTML did the whole operation
  if (!CanCut())
    return;

  // TODO(editing-dev): The use of updateStyleAndLayoutIgnorePendingStylesheets
  // needs to be audited.  See http://crbug.com/590369 for more details.
  // |tryDHTMLCut| dispatches cut event, which may make layout dirty, but we
  // need clean layout to obtain the selected content.
  GetFrame().GetDocument()->UpdateStyleAndLayoutIgnorePendingStylesheets();

  if (source == kCommandFromMenuOrKeyBinding &&
      !GetFrame().Selection().SelectionHasFocus())
    return;

  // TODO(yosin) We should use early return style here.
  if (CanDeleteRange(SelectedRange())) {
    GetSpellChecker().UpdateMarkersForWordsAffectedByEditing(true);
    if (EnclosingTextControl(GetFrame()
                                 .Selection()
                                 .ComputeVisibleSelectionInDOMTree()
                                 .Start())) {
      String plain_text = GetFrame().SelectedTextForClipboard();
      Pasteboard::GeneralPasteboard()->WritePlainText(
          plain_text, CanSmartCopyOrDelete() ? Pasteboard::kCanSmartReplace
                                             : Pasteboard::kCannotSmartReplace);
    } else {
      WriteSelectionToPasteboard();
    }

    if (source == kCommandFromMenuOrKeyBinding) {
      if (DispatchBeforeInputDataTransfer(FindEventTargetFromSelection(),
                                          InputEvent::InputType::kDeleteByCut,
                                          nullptr) !=
          DispatchEventResult::kNotCanceled)
        return;
      // 'beforeinput' event handler may destroy target frame.
      if (frame_->GetDocument()->GetFrame() != frame_)
        return;
    }
    DeleteSelectionWithSmartDelete(
        CanSmartCopyOrDelete() ? DeleteMode::kSmart : DeleteMode::kSimple,
        InputEvent::InputType::kDeleteByCut);
  }
}

void Editor::Copy(EditorCommandSource source) {
  if (TryDHTMLCopy())
    return;  // DHTML did the whole operation
  if (!CanCopy())
    return;

  // TODO(editing-dev): The use of updateStyleAndLayoutIgnorePendingStylesheets
  // needs to be audited.  See http://crbug.com/590369 for more details.
  // |tryDHTMLCopy| dispatches copy event, which may make layout dirty, but
  // we need clean layout to obtain the selected content.
  GetFrame().GetDocument()->UpdateStyleAndLayoutIgnorePendingStylesheets();

  if (source == kCommandFromMenuOrKeyBinding &&
      !GetFrame().Selection().SelectionHasFocus())
    return;

  if (EnclosingTextControl(
          GetFrame().Selection().ComputeVisibleSelectionInDOMTree().Start())) {
    Pasteboard::GeneralPasteboard()->WritePlainText(
        GetFrame().SelectedTextForClipboard(),
        CanSmartCopyOrDelete() ? Pasteboard::kCanSmartReplace
                               : Pasteboard::kCannotSmartReplace);
  } else {
    Document* document = GetFrame().GetDocument();
    if (HTMLImageElement* image_element =
            ImageElementFromImageDocument(document))
      WriteImageNodeToPasteboard(Pasteboard::GeneralPasteboard(), image_element,
                                 document->title());
    else
      WriteSelectionToPasteboard();
  }
}

void Editor::Paste(EditorCommandSource source) {
  DCHECK(GetFrame().GetDocument());
  if (TryDHTMLPaste(kAllMimeTypes))
    return;  // DHTML did the whole operation
  if (!CanPaste())
    return;

  // TODO(editing-dev): The use of updateStyleAndLayoutIgnorePendingStylesheets
  // needs to be audited.  See http://crbug.com/590369 for more details.
  // |tryDHTMLPaste| dispatches copy event, which may make layout dirty, but
  // we need clean layout to obtain the selected content.
  GetFrame().GetDocument()->UpdateStyleAndLayoutIgnorePendingStylesheets();

  if (source == kCommandFromMenuOrKeyBinding &&
      !GetFrame().Selection().SelectionHasFocus())
    return;

  GetSpellChecker().UpdateMarkersForWordsAffectedByEditing(false);
  ResourceFetcher* loader = GetFrame().GetDocument()->Fetcher();
  ResourceCacheValidationSuppressor validation_suppressor(loader);

  PasteMode paste_mode = GetFrame()
                                 .Selection()
                                 .ComputeVisibleSelectionInDOMTree()
                                 .IsContentRichlyEditable()
                             ? kAllMimeTypes
                             : kPlainTextOnly;

  if (source == kCommandFromMenuOrKeyBinding) {
    DataTransfer* data_transfer =
        DataTransfer::Create(DataTransfer::kCopyAndPaste, kDataTransferReadable,
                             DataObject::CreateFromPasteboard(paste_mode));

    if (DispatchBeforeInputDataTransfer(FindEventTargetFromSelection(),
                                        InputEvent::InputType::kInsertFromPaste,
                                        data_transfer) !=
        DispatchEventResult::kNotCanceled)
      return;
    // 'beforeinput' event handler may destroy target frame.
    if (frame_->GetDocument()->GetFrame() != frame_)
      return;
  }

  if (paste_mode == kAllMimeTypes)
    PasteWithPasteboard(Pasteboard::GeneralPasteboard());
  else
    PasteAsPlainTextWithPasteboard(Pasteboard::GeneralPasteboard());
}

void Editor::PasteAsPlainText(EditorCommandSource source) {
  if (TryDHTMLPaste(kPlainTextOnly))
    return;
  if (!CanPaste())
    return;

  // TODO(editing-dev): The use of updateStyleAndLayoutIgnorePendingStylesheets
  // needs to be audited.  See http://crbug.com/590369 for more details.
  // |tryDHTMLPaste| dispatches copy event, which may make layout dirty, but
  // we need clean layout to obtain the selected content.
  GetFrame().GetDocument()->UpdateStyleAndLayoutIgnorePendingStylesheets();

  if (source == kCommandFromMenuOrKeyBinding &&
      !GetFrame().Selection().SelectionHasFocus())
    return;

  GetSpellChecker().UpdateMarkersForWordsAffectedByEditing(false);
  PasteAsPlainTextWithPasteboard(Pasteboard::GeneralPasteboard());
}

void Editor::PerformDelete() {
  if (!CanDelete())
    return;

  // TODO(editing-dev): The use of updateStyleAndLayoutIgnorePendingStylesheets
  // needs to be audited.  See http://crbug.com/590369 for more details.
  // |selectedRange| requires clean layout for visible selection normalization.
  GetFrame().GetDocument()->UpdateStyleAndLayoutIgnorePendingStylesheets();

  AddToKillRing(SelectedRange());
  // TODO(chongz): |Editor::performDelete()| has no direction.
  // https://github.com/w3c/editing/issues/130
  DeleteSelectionWithSmartDelete(
      CanSmartCopyOrDelete() ? DeleteMode::kSmart : DeleteMode::kSimple,
      InputEvent::InputType::kDeleteContentBackward);

  // clear the "start new kill ring sequence" setting, because it was set to
  // true when the selection was updated by deleting the range
  SetStartNewKillRingSequence(false);
}

static void CountEditingEvent(ExecutionContext* execution_context,
                              const Event* event,
                              UseCounter::Feature feature_on_input,
                              UseCounter::Feature feature_on_text_area,
                              UseCounter::Feature feature_on_content_editable,
                              UseCounter::Feature feature_on_non_node) {
  EventTarget* event_target = event->target();
  Node* node = event_target->ToNode();
  if (!node) {
    UseCounter::Count(execution_context, feature_on_non_node);
    return;
  }

  if (isHTMLInputElement(node)) {
    UseCounter::Count(execution_context, feature_on_input);
    return;
  }

  if (isHTMLTextAreaElement(node)) {
    UseCounter::Count(execution_context, feature_on_text_area);
    return;
  }

  TextControlElement* control = EnclosingTextControl(node);
  if (isHTMLInputElement(control)) {
    UseCounter::Count(execution_context, feature_on_input);
    return;
  }

  if (isHTMLTextAreaElement(control)) {
    UseCounter::Count(execution_context, feature_on_text_area);
    return;
  }

  UseCounter::Count(execution_context, feature_on_content_editable);
}

void Editor::CountEvent(ExecutionContext* execution_context,
                        const Event* event) {
  if (!execution_context)
    return;

  if (event->type() == EventTypeNames::textInput) {
    CountEditingEvent(execution_context, event,
                      UseCounter::kTextInputEventOnInput,
                      UseCounter::kTextInputEventOnTextArea,
                      UseCounter::kTextInputEventOnContentEditable,
                      UseCounter::kTextInputEventOnNotNode);
    return;
  }

  if (event->type() == EventTypeNames::webkitBeforeTextInserted) {
    CountEditingEvent(execution_context, event,
                      UseCounter::kWebkitBeforeTextInsertedOnInput,
                      UseCounter::kWebkitBeforeTextInsertedOnTextArea,
                      UseCounter::kWebkitBeforeTextInsertedOnContentEditable,
                      UseCounter::kWebkitBeforeTextInsertedOnNotNode);
    return;
  }

  if (event->type() == EventTypeNames::webkitEditableContentChanged) {
    CountEditingEvent(
        execution_context, event,
        UseCounter::kWebkitEditableContentChangedOnInput,
        UseCounter::kWebkitEditableContentChangedOnTextArea,
        UseCounter::kWebkitEditableContentChangedOnContentEditable,
        UseCounter::kWebkitEditableContentChangedOnNotNode);
  }
}

void Editor::CopyImage(const HitTestResult& result) {
  WriteImageNodeToPasteboard(Pasteboard::GeneralPasteboard(),
                             result.InnerNodeOrImageMapImage(),
                             result.AltDisplayString());
}

bool Editor::CanUndo() {
  return undo_stack_->CanUndo();
}

void Editor::Undo() {
  undo_stack_->Undo();
}

bool Editor::CanRedo() {
  return undo_stack_->CanRedo();
}

void Editor::Redo() {
  undo_stack_->Redo();
}

void Editor::SetBaseWritingDirection(WritingDirection direction) {
  Element* focused_element = GetFrame().GetDocument()->FocusedElement();
  if (IsTextControlElement(focused_element)) {
    if (direction == NaturalWritingDirection)
      return;
    focused_element->setAttribute(
        dirAttr, direction == LeftToRightWritingDirection ? "ltr" : "rtl");
    focused_element->DispatchInputEvent();
    return;
  }

  MutableStylePropertySet* style =
      MutableStylePropertySet::Create(kHTMLQuirksMode);
  style->SetProperty(
      CSSPropertyDirection,
      direction == LeftToRightWritingDirection
          ? "ltr"
          : direction == RightToLeftWritingDirection ? "rtl" : "inherit",
      false);
  ApplyParagraphStyleToSelection(
      style, InputEvent::InputType::kFormatSetBlockTextDirection);
}

void Editor::RevealSelectionAfterEditingOperation(
    const ScrollAlignment& alignment,
    RevealExtentOption reveal_extent_option) {
  if (prevent_reveal_selection_)
    return;
  if (!GetFrame().Selection().IsAvailable())
    return;
  GetFrame().Selection().RevealSelection(alignment, reveal_extent_option);
}

void Editor::Transpose() {
  if (!CanEdit())
    return;

  // TODO(editing-dev): The use of updateStyleAndLayoutIgnorePendingStylesheets
  // needs to be audited. see http://crbug.com/590369 for more details.
  GetFrame().GetDocument()->UpdateStyleAndLayoutIgnorePendingStylesheets();

  const EphemeralRange& range = ComputeRangeForTranspose(GetFrame());
  if (range.IsNull())
    return;

  // Transpose the two characters.
  const String& text = PlainText(range);
  if (text.length() != 2)
    return;
  const String& transposed = text.Right(1) + text.Left(1);

  if (DispatchBeforeInputInsertText(
          EventTargetNodeForDocument(GetFrame().GetDocument()), transposed,
          InputEvent::InputType::kInsertTranspose,
          new StaticRangeVector(1, StaticRange::Create(range))) !=
      DispatchEventResult::kNotCanceled)
    return;

  // 'beforeinput' event handler may destroy document.
  if (frame_->GetDocument()->GetFrame() != frame_)
    return;

  // TODO(editing-dev): The use of updateStyleAndLayoutIgnorePendingStylesheets
  // needs to be audited. see http://crbug.com/590369 for more details.
  GetFrame().GetDocument()->UpdateStyleAndLayoutIgnorePendingStylesheets();

  // 'beforeinput' event handler may change selection, we need to re-calculate
  // range.
  const EphemeralRange& new_range = ComputeRangeForTranspose(GetFrame());
  if (new_range.IsNull())
    return;

  const String& new_text = PlainText(new_range);
  if (new_text.length() != 2)
    return;
  const String& new_transposed = new_text.Right(1) + new_text.Left(1);

  const SelectionInDOMTree& new_selection =
      SelectionInDOMTree::Builder().SetBaseAndExtent(new_range).Build();

  // Select the two characters.
  if (CreateVisibleSelection(new_selection) !=
      GetFrame().Selection().ComputeVisibleSelectionInDOMTree())
    GetFrame().Selection().SetSelection(new_selection);

  // Insert the transposed characters.
  ReplaceSelectionWithText(new_transposed, false, false,
                           InputEvent::InputType::kInsertTranspose);
}

void Editor::AddToKillRing(const EphemeralRange& range) {
  if (should_start_new_kill_ring_sequence_)
    GetKillRing().StartNewSequence();

  DCHECK(!GetFrame().GetDocument()->NeedsLayoutTreeUpdate());
  String text = PlainText(range);
  GetKillRing().Append(text);
  should_start_new_kill_ring_sequence_ = false;
}

void Editor::ChangeSelectionAfterCommand(
    const SelectionInDOMTree& new_selection,
    FrameSelection::SetSelectionOptions options) {
  if (new_selection.IsNone())
    return;

  // See <rdar://problem/5729315> Some shouldChangeSelectedDOMRange contain
  // Ranges for selections that are no longer valid
  bool selection_did_not_change_dom_position =
      new_selection == GetFrame().Selection().GetSelectionInDOMTree();
  GetFrame().Selection().SetSelection(new_selection, options);

  // Some editing operations change the selection visually without affecting its
  // position within the DOM. For example when you press return in the following
  // (the caret is marked by ^):
  // <div contentEditable="true"><div>^Hello</div></div>
  // WebCore inserts <div><br></div> *before* the current block, which correctly
  // moves the paragraph down but which doesn't change the caret's DOM position
  // (["hello", 0]). In these situations the above FrameSelection::setSelection
  // call does not call EditorClient::respondToChangedSelection(), which, on the
  // Mac, sends selection change notifications and starts a new kill ring
  // sequence, but we want to do these things (matches AppKit).
  if (selection_did_not_change_dom_position) {
    Client().RespondToChangedSelection(
        frame_, GetFrame()
                    .Selection()
                    .GetSelectionInDOMTree()
                    .SelectionTypeWithLegacyGranularity());
  }
}

IntRect Editor::FirstRectForRange(const EphemeralRange& range) const {
  DCHECK(!GetFrame().GetDocument()->NeedsLayoutTreeUpdate());
  DocumentLifecycle::DisallowTransitionScope disallow_transition(
      GetFrame().GetDocument()->Lifecycle());

  LayoutUnit extra_width_to_end_of_line;
  DCHECK(range.IsNotNull());

  IntRect start_caret_rect =
      RenderedPosition(
          CreateVisiblePosition(range.StartPosition()).DeepEquivalent(),
          TextAffinity::kDownstream)
          .AbsoluteRect(&extra_width_to_end_of_line);
  if (start_caret_rect.IsEmpty())
    return IntRect();

  IntRect end_caret_rect =
      RenderedPosition(
          CreateVisiblePosition(range.EndPosition()).DeepEquivalent(),
          TextAffinity::kUpstream)
          .AbsoluteRect();
  if (end_caret_rect.IsEmpty())
    return IntRect();

  if (start_caret_rect.Y() == end_caret_rect.Y()) {
    // start and end are on the same line
    return IntRect(
        std::min(start_caret_rect.X(), end_caret_rect.X()),
        start_caret_rect.Y(), abs(end_caret_rect.X() - start_caret_rect.X()),
        std::max(start_caret_rect.Height(), end_caret_rect.Height()));
  }

  // start and end aren't on the same line, so go from start to the end of its
  // line
  return IntRect(
      start_caret_rect.X(), start_caret_rect.Y(),
      (start_caret_rect.Width() + extra_width_to_end_of_line).ToInt(),
      start_caret_rect.Height());
}

void Editor::ComputeAndSetTypingStyle(StylePropertySet* style,
                                      InputEvent::InputType input_type) {
  if (!style || style->IsEmpty()) {
    ClearTypingStyle();
    return;
  }

  // Calculate the current typing style.
  if (typing_style_)
    typing_style_->OverrideWithStyle(style);
  else
    typing_style_ = EditingStyle::Create(style);

  typing_style_->PrepareToApplyAt(
      GetFrame()
          .Selection()
          .ComputeVisibleSelectionInDOMTreeDeprecated()
          .VisibleStart()
          .DeepEquivalent(),
      EditingStyle::kPreserveWritingDirection);

  // Handle block styles, substracting these from the typing style.
  EditingStyle* block_style = typing_style_->ExtractAndRemoveBlockProperties();
  if (!block_style->IsEmpty()) {
    DCHECK(GetFrame().GetDocument());
    ApplyStyleCommand::Create(*GetFrame().GetDocument(), block_style,
                              input_type)
        ->Apply();
  }
}

bool Editor::FindString(const String& target, FindOptions options) {
  VisibleSelection selection =
      GetFrame().Selection().ComputeVisibleSelectionInDOMTreeDeprecated();

  // TODO(yosin) We should make |findRangeOfString()| to return
  // |EphemeralRange| rather than|Range| object.
  Range* result_range = FindRangeOfString(
      target, EphemeralRange(selection.Start(), selection.end()),
      static_cast<FindOptions>(options | kFindAPICall));

  if (!result_range)
    return false;

  GetFrame().Selection().SetSelection(
      SelectionInDOMTree::Builder()
          .SetBaseAndExtent(EphemeralRange(result_range))
          .Build());
  GetFrame().Selection().RevealSelection();
  return true;
}

Range* Editor::FindStringAndScrollToVisible(const String& target,
                                            Range* previous_match,
                                            FindOptions options) {
  Range* next_match = FindRangeOfString(
      target, EphemeralRangeInFlatTree(previous_match), options);
  if (!next_match)
    return nullptr;

  Node* first_node = next_match->FirstNode();
  first_node->GetLayoutObject()->ScrollRectToVisible(
      LayoutRect(next_match->BoundingBox()),
      ScrollAlignment::kAlignCenterIfNeeded,
      ScrollAlignment::kAlignCenterIfNeeded, kUserScroll);
  first_node->GetDocument().SetSequentialFocusNavigationStartingPoint(
      first_node);

  return next_match;
}

// TODO(yosin) We should return |EphemeralRange| rather than |Range|. We use
// |Range| object for checking whether start and end position crossing shadow
// boundaries, however we can do it without |Range| object.
template <typename Strategy>
static Range* FindStringBetweenPositions(
    const String& target,
    const EphemeralRangeTemplate<Strategy>& reference_range,
    FindOptions options) {
  EphemeralRangeTemplate<Strategy> search_range(reference_range);

  bool forward = !(options & kBackwards);

  while (true) {
    EphemeralRangeTemplate<Strategy> result_range =
        FindPlainText(search_range, target, options);
    if (result_range.IsCollapsed())
      return nullptr;

    Range* range_object =
        Range::Create(result_range.GetDocument(),
                      ToPositionInDOMTree(result_range.StartPosition()),
                      ToPositionInDOMTree(result_range.EndPosition()));
    if (!range_object->collapsed())
      return range_object;

    // Found text spans over multiple TreeScopes. Since it's impossible to
    // return such section as a Range, we skip this match and seek for the
    // next occurrence.
    // TODO(yosin) Handle this case.
    if (forward) {
      search_range = EphemeralRangeTemplate<Strategy>(
          NextPositionOf(result_range.StartPosition(),
                         PositionMoveType::kGraphemeCluster),
          search_range.EndPosition());
    } else {
      search_range = EphemeralRangeTemplate<Strategy>(
          search_range.StartPosition(),
          PreviousPositionOf(result_range.EndPosition(),
                             PositionMoveType::kGraphemeCluster));
    }
  }

  NOTREACHED();
  return nullptr;
}

template <typename Strategy>
static Range* FindRangeOfStringAlgorithm(
    Document& document,
    const String& target,
    const EphemeralRangeTemplate<Strategy>& reference_range,
    FindOptions options) {
  if (target.IsEmpty())
    return nullptr;

  // Start from an edge of the reference range. Which edge is used depends on
  // whether we're searching forward or backward, and whether startInSelection
  // is set.
  EphemeralRangeTemplate<Strategy> document_range =
      EphemeralRangeTemplate<Strategy>::RangeOfContents(document);
  EphemeralRangeTemplate<Strategy> search_range(document_range);

  bool forward = !(options & kBackwards);
  bool start_in_reference_range = false;
  if (reference_range.IsNotNull()) {
    start_in_reference_range = options & kStartInSelection;
    if (forward && start_in_reference_range)
      search_range = EphemeralRangeTemplate<Strategy>(
          reference_range.StartPosition(), document_range.EndPosition());
    else if (forward)
      search_range = EphemeralRangeTemplate<Strategy>(
          reference_range.EndPosition(), document_range.EndPosition());
    else if (start_in_reference_range)
      search_range = EphemeralRangeTemplate<Strategy>(
          document_range.StartPosition(), reference_range.EndPosition());
    else
      search_range = EphemeralRangeTemplate<Strategy>(
          document_range.StartPosition(), reference_range.StartPosition());
  }

  Range* result_range =
      FindStringBetweenPositions(target, search_range, options);

  // If we started in the reference range and the found range exactly matches
  // the reference range, find again. Build a selection with the found range
  // to remove collapsed whitespace. Compare ranges instead of selection
  // objects to ignore the way that the current selection was made.
  if (result_range && start_in_reference_range &&
      NormalizeRange(EphemeralRangeTemplate<Strategy>(result_range)) ==
          reference_range) {
    if (forward)
      search_range = EphemeralRangeTemplate<Strategy>(
          FromPositionInDOMTree<Strategy>(result_range->EndPosition()),
          search_range.EndPosition());
    else
      search_range = EphemeralRangeTemplate<Strategy>(
          search_range.StartPosition(),
          FromPositionInDOMTree<Strategy>(result_range->StartPosition()));
    result_range = FindStringBetweenPositions(target, search_range, options);
  }

  if (!result_range && options & kWrapAround)
    return FindStringBetweenPositions(target, document_range, options);

  return result_range;
}

Range* Editor::FindRangeOfString(const String& target,
                                 const EphemeralRange& reference,
                                 FindOptions options) {
  return FindRangeOfStringAlgorithm<EditingStrategy>(
      *GetFrame().GetDocument(), target, reference, options);
}

Range* Editor::FindRangeOfString(const String& target,
                                 const EphemeralRangeInFlatTree& reference,
                                 FindOptions options) {
  return FindRangeOfStringAlgorithm<EditingInFlatTreeStrategy>(
      *GetFrame().GetDocument(), target, reference, options);
}

void Editor::SetMarkedTextMatchesAreHighlighted(bool flag) {
  if (flag == are_marked_text_matches_highlighted_)
    return;

  are_marked_text_matches_highlighted_ = flag;
  GetFrame().GetDocument()->Markers().RepaintMarkers(
      DocumentMarker::kTextMatch);
}

void Editor::RespondToChangedSelection(
    const Position& old_selection_start,
    FrameSelection::SetSelectionOptions options) {
  GetSpellChecker().RespondToChangedSelection(old_selection_start, options);
  Client().RespondToChangedSelection(&GetFrame(),
                                     GetFrame()
                                         .Selection()
                                         .GetSelectionInDOMTree()
                                         .SelectionTypeWithLegacyGranularity());
  SetStartNewKillRingSequence(true);
}

SpellChecker& Editor::GetSpellChecker() const {
  return GetFrame().GetSpellChecker();
}

void Editor::ToggleOverwriteModeEnabled() {
  overwrite_mode_enabled_ = !overwrite_mode_enabled_;
  GetFrame().Selection().SetShouldShowBlockCursor(overwrite_mode_enabled_);
}

// TODO(tkent): This is a workaround of some crash bugs in the editing code,
// which assumes a document has a valid HTML structure. We should make the
// editing code more robust, and should remove this hack. crbug.com/580941.
void Editor::TidyUpHTMLStructure(Document& document) {
  // hasEditableStyle() needs up-to-date ComputedStyle.
  document.UpdateStyleAndLayoutTree();
  bool needs_valid_structure = HasEditableStyle(document) ||
                               (document.documentElement() &&
                                HasEditableStyle(*document.documentElement()));
  if (!needs_valid_structure)
    return;
  Element* existing_head = nullptr;
  Element* existing_body = nullptr;
  Element* current_root = document.documentElement();
  if (current_root) {
    if (isHTMLHtmlElement(current_root))
      return;
    if (isHTMLHeadElement(current_root))
      existing_head = current_root;
    else if (isHTMLBodyElement(current_root))
      existing_body = current_root;
    else if (isHTMLFrameSetElement(current_root))
      existing_body = current_root;
  }
  // We ensure only "the root is <html>."
  // documentElement as rootEditableElement is problematic.  So we move
  // non-<html> root elements under <body>, and the <body> works as
  // rootEditableElement.
  document.AddConsoleMessage(ConsoleMessage::Create(
      kJSMessageSource, kWarningMessageLevel,
      "document.execCommand() doesn't work with an invalid HTML structure. It "
      "is corrected automatically."));
  UseCounter::Count(document, UseCounter::kExecCommandAltersHTMLStructure);

  Element* root = HTMLHtmlElement::Create(document);
  if (existing_head)
    root->AppendChild(existing_head);
  Element* body = nullptr;
  if (existing_body)
    body = existing_body;
  else
    body = HTMLBodyElement::Create(document);
  if (document.documentElement() && body != document.documentElement())
    body->AppendChild(document.documentElement());
  root->AppendChild(body);
  DCHECK(!document.documentElement());
  document.AppendChild(root);

  // TODO(tkent): Should we check and move Text node children of <html>?
}

void Editor::ReplaceSelection(const String& text) {
  DCHECK(!GetFrame().GetDocument()->NeedsLayoutTreeUpdate());
  bool select_replacement = Behavior().ShouldSelectReplacement();
  bool smart_replace = true;
  ReplaceSelectionWithText(text, select_replacement, smart_replace,
                           InputEvent::InputType::kInsertReplacementText);
}

TypingCommand* Editor::LastTypingCommandIfStillOpenForTyping() const {
  return TypingCommand::LastTypingCommandIfStillOpenForTyping(&GetFrame());
}

DEFINE_TRACE(Editor) {
  visitor->Trace(frame_);
  visitor->Trace(last_edit_command_);
  visitor->Trace(undo_stack_);
  visitor->Trace(mark_);
  visitor->Trace(typing_style_);
}

}  // namespace blink
