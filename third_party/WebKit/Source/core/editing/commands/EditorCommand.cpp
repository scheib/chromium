/*
 * Copyright (C) 2006, 2007, 2008 Apple Inc. All rights reserved.
 * Copyright (C) 2008 Nokia Corporation and/or its subsidiary(-ies)
 * Copyright (C) 2009 Igalia S.L.
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
#include "core/CSSValueKeywords.h"
#include "core/HTMLNames.h"
#include "core/clipboard/Pasteboard.h"
#include "core/css/CSSComputedStyleDeclaration.h"
#include "core/css/CSSIdentifierValue.h"
#include "core/css/CSSValueList.h"
#include "core/css/StylePropertySet.h"
#include "core/dom/DocumentFragment.h"
#include "core/dom/TagCollection.h"
#include "core/editing/EditingStyleUtilities.h"
#include "core/editing/EditingUtilities.h"
#include "core/editing/FrameSelection.h"
#include "core/editing/SelectionModifier.h"
#include "core/editing/commands/CreateLinkCommand.h"
#include "core/editing/commands/EditorCommandNames.h"
#include "core/editing/commands/FormatBlockCommand.h"
#include "core/editing/commands/IndentOutdentCommand.h"
#include "core/editing/commands/InsertListCommand.h"
#include "core/editing/commands/ReplaceSelectionCommand.h"
#include "core/editing/commands/TypingCommand.h"
#include "core/editing/commands/UnlinkCommand.h"
#include "core/editing/serializers/Serialization.h"
#include "core/editing/spellcheck/SpellChecker.h"
#include "core/events/Event.h"
#include "core/frame/FrameView.h"
#include "core/frame/LocalFrame.h"
#include "core/frame/Settings.h"
#include "core/html/HTMLFontElement.h"
#include "core/html/HTMLHRElement.h"
#include "core/html/HTMLImageElement.h"
#include "core/html/TextControlElement.h"
#include "core/input/EventHandler.h"
#include "core/layout/LayoutBox.h"
#include "core/page/ChromeClient.h"
#include "core/page/EditorClient.h"
#include "core/page/Page.h"
#include "platform/Histogram.h"
#include "platform/KillRing.h"
#include "platform/UserGestureIndicator.h"
#include "platform/scroll/Scrollbar.h"
#include "platform/wtf/StringExtras.h"
#include "platform/wtf/text/AtomicString.h"
#include "public/platform/Platform.h"
#include "public/platform/WebEditingCommandType.h"

#include <iterator>

namespace blink {

using namespace HTMLNames;

namespace {

struct CommandNameEntry {
  const char* name;
  WebEditingCommandType type;
};

const CommandNameEntry kCommandNameEntries[] = {
#define V(name) {#name, WebEditingCommandType::k##name},
    FOR_EACH_BLINK_EDITING_COMMAND_NAME(V)
#undef V
};
// Handles all commands except WebEditingCommandType::Invalid.
static_assert(
    arraysize(kCommandNameEntries) + 1 ==
        static_cast<size_t>(WebEditingCommandType::kNumberOfCommandTypes),
    "must handle all valid WebEditingCommandType");

WebEditingCommandType WebEditingCommandTypeFromCommandName(
    const String& command_name) {
  const CommandNameEntry* result = std::lower_bound(
      std::begin(kCommandNameEntries), std::end(kCommandNameEntries),
      command_name, [](const CommandNameEntry& entry, const String& needle) {
        return CodePointCompareIgnoringASCIICase(needle, entry.name) > 0;
      });
  if (result != std::end(kCommandNameEntries) &&
      CodePointCompareIgnoringASCIICase(command_name, result->name) == 0)
    return result->type;
  return WebEditingCommandType::kInvalid;
}

// |frame| is only used for |InsertNewline| due to how |executeInsertNewline()|
// works.
InputEvent::InputType InputTypeFromCommandType(
    WebEditingCommandType command_type,
    LocalFrame& frame) {
  // We only handle InputType on spec for 'beforeinput'.
  // http://w3c.github.io/editing/input-events.html
  using CommandType = WebEditingCommandType;
  using InputType = InputEvent::InputType;

  // |executeInsertNewline()| could do two things but we have no other ways to
  // predict.
  if (command_type == CommandType::kInsertNewline)
    return frame.GetEditor().CanEditRichly() ? InputType::kInsertParagraph
                                             : InputType::kInsertLineBreak;

  switch (command_type) {
    // Insertion.
    case CommandType::kInsertBacktab:
    case CommandType::kInsertText:
      return InputType::kInsertText;
    case CommandType::kInsertLineBreak:
      return InputType::kInsertLineBreak;
    case CommandType::kInsertParagraph:
    case CommandType::kInsertNewlineInQuotedContent:
      return InputType::kInsertParagraph;
    case CommandType::kInsertHorizontalRule:
      return InputType::kInsertHorizontalRule;
    case CommandType::kInsertOrderedList:
      return InputType::kInsertOrderedList;
    case CommandType::kInsertUnorderedList:
      return InputType::kInsertUnorderedList;

    // Deletion.
    case CommandType::kDelete:
    case CommandType::kDeleteBackward:
    case CommandType::kDeleteBackwardByDecomposingPreviousCharacter:
      return InputType::kDeleteContentBackward;
    case CommandType::kDeleteForward:
      return InputType::kDeleteContentForward;
    case CommandType::kDeleteToBeginningOfLine:
      return InputType::kDeleteSoftLineBackward;
    case CommandType::kDeleteToEndOfLine:
      return InputType::kDeleteSoftLineForward;
    case CommandType::kDeleteWordBackward:
      return InputType::kDeleteWordBackward;
    case CommandType::kDeleteWordForward:
      return InputType::kDeleteWordForward;
    case CommandType::kDeleteToBeginningOfParagraph:
      return InputType::kDeleteHardLineBackward;
    case CommandType::kDeleteToEndOfParagraph:
      return InputType::kDeleteHardLineForward;
    // TODO(chongz): Find appreciate InputType for following commands.
    case CommandType::kDeleteToMark:
      return InputType::kNone;

    // Command.
    case CommandType::kUndo:
      return InputType::kHistoryUndo;
    case CommandType::kRedo:
      return InputType::kHistoryRedo;
    // Cut and Paste will be handled in |Editor::dispatchCPPEvent()|.

    // Styling.
    case CommandType::kBold:
    case CommandType::kToggleBold:
      return InputType::kFormatBold;
    case CommandType::kItalic:
    case CommandType::kToggleItalic:
      return InputType::kFormatItalic;
    case CommandType::kUnderline:
    case CommandType::kToggleUnderline:
      return InputType::kFormatUnderline;
    case CommandType::kStrikethrough:
      return InputType::kFormatStrikeThrough;
    case CommandType::kSuperscript:
      return InputType::kFormatSuperscript;
    case CommandType::kSubscript:
      return InputType::kFormatSubscript;
    default:
      return InputType::kNone;
  }
}

StaticRangeVector* RangesFromCurrentSelectionOrExtendCaret(
    const LocalFrame& frame,
    SelectionDirection direction,
    TextGranularity granularity) {
  frame.GetDocument()->UpdateStyleAndLayoutIgnorePendingStylesheets();
  SelectionModifier selection_modifier(
      frame, frame.Selection().ComputeVisibleSelectionInDOMTreeDeprecated());
  if (selection_modifier.Selection().IsCaret())
    selection_modifier.Modify(FrameSelection::kAlterationExtend, direction,
                              granularity);
  StaticRangeVector* ranges = new StaticRangeVector;
  // We only supports single selections.
  if (selection_modifier.Selection().IsNone())
    return ranges;
  ranges->push_back(StaticRange::Create(
      FirstEphemeralRangeOf(selection_modifier.Selection())));
  return ranges;
}

}  // anonymous namespace

class EditorInternalCommand {
 public:
  WebEditingCommandType command_type;
  bool (*execute)(LocalFrame&, Event*, EditorCommandSource, const String&);
  bool (*is_supported_from_dom)(LocalFrame*);
  bool (*is_enabled)(LocalFrame&, Event*, EditorCommandSource);
  TriState (*state)(LocalFrame&, Event*);
  String (*value)(const EditorInternalCommand&, LocalFrame&, Event*);
  bool is_text_insertion;
  // TODO(yosin) We should have |canExecute()|, which checks clipboard
  // accessibility to simplify |Editor::Command::execute()|.
  bool allow_execution_when_disabled;
};

static const bool kNotTextInsertion = false;
static const bool kIsTextInsertion = true;

static const bool kAllowExecutionWhenDisabled = true;
static const bool kDoNotAllowExecutionWhenDisabled = false;

// Related to Editor::selectionForCommand.
// Certain operations continue to use the target control's selection even if the
// event handler already moved the selection outside of the text control.
static LocalFrame* TargetFrame(LocalFrame& frame, Event* event) {
  if (!event)
    return &frame;
  Node* node = event->target()->ToNode();
  if (!node)
    return &frame;
  return node->GetDocument().GetFrame();
}

static bool ApplyCommandToFrame(LocalFrame& frame,
                                EditorCommandSource source,
                                InputEvent::InputType input_type,
                                StylePropertySet* style) {
  // FIXME: We don't call shouldApplyStyle when the source is DOM; is there a
  // good reason for that?
  switch (source) {
    case kCommandFromMenuOrKeyBinding:
      frame.GetEditor().ApplyStyleToSelection(style, input_type);
      return true;
    case kCommandFromDOM:
      frame.GetEditor().ApplyStyle(style, input_type);
      return true;
  }
  NOTREACHED();
  return false;
}

static bool ExecuteApplyStyle(LocalFrame& frame,
                              EditorCommandSource source,
                              InputEvent::InputType input_type,
                              CSSPropertyID property_id,
                              const String& property_value) {
  MutableStylePropertySet* style =
      MutableStylePropertySet::Create(kHTMLQuirksMode);
  style->SetProperty(property_id, property_value);
  return ApplyCommandToFrame(frame, source, input_type, style);
}

static bool ExecuteApplyStyle(LocalFrame& frame,
                              EditorCommandSource source,
                              InputEvent::InputType input_type,
                              CSSPropertyID property_id,
                              CSSValueID property_value) {
  MutableStylePropertySet* style =
      MutableStylePropertySet::Create(kHTMLQuirksMode);
  style->SetProperty(property_id, property_value);
  return ApplyCommandToFrame(frame, source, input_type, style);
}

// FIXME: executeToggleStyleInList does not handle complicated cases such as
// <b><u>hello</u>world</b> properly. This function must use
// Editor::selectionHasStyle to determine the current style but we cannot fix
// this until https://bugs.webkit.org/show_bug.cgi?id=27818 is resolved.
static bool ExecuteToggleStyleInList(LocalFrame& frame,
                                     EditorCommandSource source,
                                     InputEvent::InputType input_type,
                                     CSSPropertyID property_id,
                                     CSSValue* value) {
  EditingStyle* selection_style =
      EditingStyleUtilities::CreateStyleAtSelectionStart(
          frame.Selection().ComputeVisibleSelectionInDOMTreeDeprecated());
  if (!selection_style || !selection_style->Style())
    return false;

  const CSSValue* selected_css_value =
      selection_style->Style()->GetPropertyCSSValue(property_id);
  String new_style("none");
  if (selected_css_value->IsValueList()) {
    CSSValueList* selected_css_value_list =
        ToCSSValueList(selected_css_value)->Copy();
    if (!selected_css_value_list->RemoveAll(*value))
      selected_css_value_list->Append(*value);
    if (selected_css_value_list->length())
      new_style = selected_css_value_list->CssText();

  } else if (selected_css_value->CssText() == "none") {
    new_style = value->CssText();
  }

  // FIXME: We shouldn't be having to convert new style into text.  We should
  // have setPropertyCSSValue.
  MutableStylePropertySet* new_mutable_style =
      MutableStylePropertySet::Create(kHTMLQuirksMode);
  new_mutable_style->SetProperty(property_id, new_style);
  return ApplyCommandToFrame(frame, source, input_type, new_mutable_style);
}

static bool ExecuteToggleStyle(LocalFrame& frame,
                               EditorCommandSource source,
                               InputEvent::InputType input_type,
                               CSSPropertyID property_id,
                               const char* off_value,
                               const char* on_value) {
  // Style is considered present when
  // Mac: present at the beginning of selection
  // other: present throughout the selection

  bool style_is_present;
  if (frame.GetEditor().Behavior().ShouldToggleStyleBasedOnStartOfSelection())
    style_is_present =
        frame.GetEditor().SelectionStartHasStyle(property_id, on_value);
  else
    style_is_present = frame.GetEditor().SelectionHasStyle(
                           property_id, on_value) == kTrueTriState;

  EditingStyle* style = EditingStyle::Create(
      property_id, style_is_present ? off_value : on_value);
  return ApplyCommandToFrame(frame, source, input_type, style->Style());
}

static bool ExecuteApplyParagraphStyle(LocalFrame& frame,
                                       EditorCommandSource source,
                                       InputEvent::InputType input_type,
                                       CSSPropertyID property_id,
                                       const String& property_value) {
  MutableStylePropertySet* style =
      MutableStylePropertySet::Create(kHTMLQuirksMode);
  style->SetProperty(property_id, property_value);
  // FIXME: We don't call shouldApplyStyle when the source is DOM; is there a
  // good reason for that?
  switch (source) {
    case kCommandFromMenuOrKeyBinding:
      frame.GetEditor().ApplyParagraphStyleToSelection(style, input_type);
      return true;
    case kCommandFromDOM:
      frame.GetEditor().ApplyParagraphStyle(style, input_type);
      return true;
  }
  NOTREACHED();
  return false;
}

static bool ExecuteInsertFragment(LocalFrame& frame,
                                  DocumentFragment* fragment) {
  DCHECK(frame.GetDocument());
  return ReplaceSelectionCommand::Create(
             *frame.GetDocument(), fragment,
             ReplaceSelectionCommand::kPreventNesting,
             InputEvent::InputType::kNone)
      ->Apply();
}

static bool ExecuteInsertElement(LocalFrame& frame, HTMLElement* content) {
  DCHECK(frame.GetDocument());
  DocumentFragment* fragment = DocumentFragment::Create(*frame.GetDocument());
  DummyExceptionStateForTesting exception_state;
  fragment->AppendChild(content, exception_state);
  if (exception_state.HadException())
    return false;
  return ExecuteInsertFragment(frame, fragment);
}

static bool ExpandSelectionToGranularity(LocalFrame& frame,
                                         TextGranularity granularity) {
  const VisibleSelection& selection = CreateVisibleSelection(
      SelectionInDOMTree::Builder()
          .SetBaseAndExtent(frame.Selection()
                                .ComputeVisibleSelectionInDOMTreeDeprecated()
                                .Base(),
                            frame.Selection()
                                .ComputeVisibleSelectionInDOMTreeDeprecated()
                                .Extent())
          .SetGranularity(granularity)
          .Build());
  const EphemeralRange new_range = selection.ToNormalizedEphemeralRange();
  if (new_range.IsNull())
    return false;
  if (new_range.IsCollapsed())
    return false;
  TextAffinity affinity = frame.Selection().GetSelectionInDOMTree().Affinity();
  frame.Selection().SetSelectedRange(new_range, affinity,
                                     SelectionDirectionalMode::kNonDirectional,
                                     FrameSelection::kCloseTyping);
  return true;
}

static bool HasChildTags(Element& element, const QualifiedName& tag_name) {
  return !element.getElementsByTagName(tag_name.LocalName())->IsEmpty();
}

static TriState SelectionListState(const FrameSelection& selection,
                                   const QualifiedName& tag_name) {
  if (selection.ComputeVisibleSelectionInDOMTreeDeprecated().IsCaret()) {
    if (EnclosingElementWithTag(
            selection.ComputeVisibleSelectionInDOMTreeDeprecated().Start(),
            tag_name))
      return kTrueTriState;
  } else if (selection.ComputeVisibleSelectionInDOMTreeDeprecated().IsRange()) {
    Element* start_element = EnclosingElementWithTag(
        selection.ComputeVisibleSelectionInDOMTreeDeprecated().Start(),
        tag_name);
    Element* end_element = EnclosingElementWithTag(
        selection.ComputeVisibleSelectionInDOMTreeDeprecated().end(), tag_name);

    if (start_element && end_element && start_element == end_element) {
      // If the selected list has the different type of list as child, return
      // |FalseTriState|.
      // See http://crbug.com/385374
      if (HasChildTags(*start_element, tag_name.Matches(ulTag) ? olTag : ulTag))
        return kFalseTriState;
      return kTrueTriState;
    }
  }

  return kFalseTriState;
}

static TriState StateStyle(LocalFrame& frame,
                           CSSPropertyID property_id,
                           const char* desired_value) {
  frame.GetDocument()->UpdateStyleAndLayoutIgnorePendingStylesheets();
  if (frame.GetEditor().Behavior().ShouldToggleStyleBasedOnStartOfSelection())
    return frame.GetEditor().SelectionStartHasStyle(property_id, desired_value)
               ? kTrueTriState
               : kFalseTriState;
  return frame.GetEditor().SelectionHasStyle(property_id, desired_value);
}

static String ValueStyle(LocalFrame& frame, CSSPropertyID property_id) {
  frame.GetDocument()->UpdateStyleAndLayoutIgnorePendingStylesheets();

  // FIXME: Rather than retrieving the style at the start of the current
  // selection, we should retrieve the style present throughout the selection
  // for non-Mac platforms.
  return frame.GetEditor().SelectionStartCSSPropertyValue(property_id);
}

static bool IsUnicodeBidiNestedOrMultipleEmbeddings(CSSValueID value_id) {
  return value_id == CSSValueEmbed || value_id == CSSValueBidiOverride ||
         value_id == CSSValueWebkitIsolate ||
         value_id == CSSValueWebkitIsolateOverride ||
         value_id == CSSValueWebkitPlaintext || value_id == CSSValueIsolate ||
         value_id == CSSValueIsolateOverride || value_id == CSSValuePlaintext;
}

// TODO(editing-dev): We should make |textDirectionForSelection()| to take
// |selectionInDOMTree|.
static WritingDirection TextDirectionForSelection(
    const VisibleSelection& selection,
    EditingStyle* typing_style,
    bool& has_nested_or_multiple_embeddings) {
  has_nested_or_multiple_embeddings = true;

  if (selection.IsNone())
    return NaturalWritingDirection;

  Position position = MostForwardCaretPosition(selection.Start());

  Node* node = position.AnchorNode();
  if (!node)
    return NaturalWritingDirection;

  Position end;
  if (selection.IsRange()) {
    end = MostBackwardCaretPosition(selection.end());

    DCHECK(end.GetDocument());
    const EphemeralRange caret_range(position.ParentAnchoredEquivalent(),
                                     end.ParentAnchoredEquivalent());
    for (Node& n : caret_range.Nodes()) {
      if (!n.IsStyledElement())
        continue;

      CSSComputedStyleDeclaration* style =
          CSSComputedStyleDeclaration::Create(&n);
      const CSSValue* unicode_bidi =
          style->GetPropertyCSSValue(CSSPropertyUnicodeBidi);
      if (!unicode_bidi || !unicode_bidi->IsIdentifierValue())
        continue;

      CSSValueID unicode_bidi_value =
          ToCSSIdentifierValue(unicode_bidi)->GetValueID();
      if (IsUnicodeBidiNestedOrMultipleEmbeddings(unicode_bidi_value))
        return NaturalWritingDirection;
    }
  }

  if (selection.IsCaret()) {
    WritingDirection direction;
    if (typing_style && typing_style->GetTextDirection(direction)) {
      has_nested_or_multiple_embeddings = false;
      return direction;
    }
    node = selection.VisibleStart().DeepEquivalent().AnchorNode();
  }
  DCHECK(node);

  // The selection is either a caret with no typing attributes or a range in
  // which no embedding is added, so just use the start position to decide.
  Node* block = EnclosingBlock(node);
  WritingDirection found_direction = NaturalWritingDirection;

  for (Node& runner : NodeTraversal::InclusiveAncestorsOf(*node)) {
    if (runner == block)
      break;
    if (!runner.IsStyledElement())
      continue;

    Element* element = &ToElement(runner);
    CSSComputedStyleDeclaration* style =
        CSSComputedStyleDeclaration::Create(element);
    const CSSValue* unicode_bidi =
        style->GetPropertyCSSValue(CSSPropertyUnicodeBidi);
    if (!unicode_bidi || !unicode_bidi->IsIdentifierValue())
      continue;

    CSSValueID unicode_bidi_value =
        ToCSSIdentifierValue(unicode_bidi)->GetValueID();
    if (unicode_bidi_value == CSSValueNormal)
      continue;

    if (unicode_bidi_value == CSSValueBidiOverride)
      return NaturalWritingDirection;

    DCHECK(EditingStyleUtilities::IsEmbedOrIsolate(unicode_bidi_value))
        << unicode_bidi_value;
    const CSSValue* direction =
        style->GetPropertyCSSValue(CSSPropertyDirection);
    if (!direction || !direction->IsIdentifierValue())
      continue;

    int direction_value = ToCSSIdentifierValue(direction)->GetValueID();
    if (direction_value != CSSValueLtr && direction_value != CSSValueRtl)
      continue;

    if (found_direction != NaturalWritingDirection)
      return NaturalWritingDirection;

    // In the range case, make sure that the embedding element persists until
    // the end of the range.
    if (selection.IsRange() && !end.AnchorNode()->IsDescendantOf(element))
      return NaturalWritingDirection;

    found_direction = direction_value == CSSValueLtr
                          ? LeftToRightWritingDirection
                          : RightToLeftWritingDirection;
  }
  has_nested_or_multiple_embeddings = false;
  return found_direction;
}

static TriState StateTextWritingDirection(LocalFrame& frame,
                                          WritingDirection direction) {
  frame.GetDocument()->UpdateStyleAndLayoutIgnorePendingStylesheets();

  bool has_nested_or_multiple_embeddings;
  WritingDirection selection_direction = TextDirectionForSelection(
      frame.Selection().ComputeVisibleSelectionInDOMTreeDeprecated(),
      frame.GetEditor().TypingStyle(), has_nested_or_multiple_embeddings);
  // FXIME: We should be returning MixedTriState when selectionDirection ==
  // direction && hasNestedOrMultipleEmbeddings
  return (selection_direction == direction &&
          !has_nested_or_multiple_embeddings)
             ? kTrueTriState
             : kFalseTriState;
}

static unsigned VerticalScrollDistance(LocalFrame& frame) {
  Element* focused_element = frame.GetDocument()->FocusedElement();
  if (!focused_element)
    return 0;
  LayoutObject* layout_object = focused_element->GetLayoutObject();
  if (!layout_object || !layout_object->IsBox())
    return 0;
  LayoutBox& layout_box = ToLayoutBox(*layout_object);
  const ComputedStyle* style = layout_box.Style();
  if (!style)
    return 0;
  if (!(style->OverflowY() == EOverflow::kScroll ||
        style->OverflowY() == EOverflow::kAuto ||
        HasEditableStyle(*focused_element)))
    return 0;
  int height = std::min<int>(layout_box.ClientHeight().ToInt(),
                             frame.View()->VisibleHeight());
  return static_cast<unsigned>(
      max(max<int>(height * ScrollableArea::MinFractionToStepWhenPaging(),
                   height - ScrollableArea::MaxOverlapBetweenPages()),
          1));
}

static EphemeralRange UnionEphemeralRanges(const EphemeralRange& range1,
                                           const EphemeralRange& range2) {
  const Position start_position =
      range1.StartPosition().CompareTo(range2.StartPosition()) <= 0
          ? range1.StartPosition()
          : range2.StartPosition();
  const Position end_position =
      range1.EndPosition().CompareTo(range2.EndPosition()) <= 0
          ? range1.EndPosition()
          : range2.EndPosition();
  return EphemeralRange(start_position, end_position);
}

// Execute command functions

static bool ExecuteBackColor(LocalFrame& frame,
                             Event*,
                             EditorCommandSource source,
                             const String& value) {
  return ExecuteApplyStyle(frame, source, InputEvent::InputType::kNone,
                           CSSPropertyBackgroundColor, value);
}

static bool CanWriteClipboard(LocalFrame& frame, EditorCommandSource source) {
  if (source == kCommandFromMenuOrKeyBinding)
    return true;
  Settings* settings = frame.GetSettings();
  bool default_value =
      (settings && settings->GetJavaScriptCanAccessClipboard()) ||
      UserGestureIndicator::ProcessingUserGesture();
  return frame.GetEditor().Client().CanCopyCut(&frame, default_value);
}

static bool ExecuteCopy(LocalFrame& frame,
                        Event*,
                        EditorCommandSource source,
                        const String&) {
  // To support |allowExecutionWhenDisabled|, we need to check clipboard
  // accessibility here rather than |Editor::Command::execute()|.
  // TODO(yosin) We should move checking |canWriteClipboard()| to
  // |Editor::Command::execute()| with introducing appropriate predicate, e.g.
  // |canExecute()|. See also "Cut", and "Paste" command.
  if (!CanWriteClipboard(frame, source))
    return false;
  frame.GetEditor().Copy(source);
  return true;
}

static bool ExecuteCreateLink(LocalFrame& frame,
                              Event*,
                              EditorCommandSource,
                              const String& value) {
  if (value.IsEmpty())
    return false;
  DCHECK(frame.GetDocument());
  return CreateLinkCommand::Create(*frame.GetDocument(), value)->Apply();
}

static bool ExecuteCut(LocalFrame& frame,
                       Event*,
                       EditorCommandSource source,
                       const String&) {
  // To support |allowExecutionWhenDisabled|, we need to check clipboard
  // accessibility here rather than |Editor::Command::execute()|.
  // TODO(yosin) We should move checking |canWriteClipboard()| to
  // |Editor::Command::execute()| with introducing appropriate predicate, e.g.
  // |canExecute()|. See also "Copy", and "Paste" command.
  if (!CanWriteClipboard(frame, source))
    return false;
  frame.GetEditor().Cut(source);
  return true;
}

static bool ExecuteDefaultParagraphSeparator(LocalFrame& frame,
                                             Event*,
                                             EditorCommandSource,
                                             const String& value) {
  if (DeprecatedEqualIgnoringCase(value, "div"))
    frame.GetEditor().SetDefaultParagraphSeparator(
        kEditorParagraphSeparatorIsDiv);
  else if (DeprecatedEqualIgnoringCase(value, "p"))
    frame.GetEditor().SetDefaultParagraphSeparator(
        kEditorParagraphSeparatorIsP);

  return true;
}

static bool ExecuteDelete(LocalFrame& frame,
                          Event*,
                          EditorCommandSource source,
                          const String&) {
  switch (source) {
    case kCommandFromMenuOrKeyBinding: {
      // Doesn't modify the text if the current selection isn't a range.
      frame.GetEditor().PerformDelete();
      return true;
    }
    case kCommandFromDOM:
      // If the current selection is a caret, delete the preceding character. IE
      // performs forwardDelete, but we currently side with Firefox. Doesn't
      // scroll to make the selection visible, or modify the kill ring (this
      // time, siding with IE, not Firefox).
      DCHECK(frame.GetDocument());
      TypingCommand::DeleteKeyPressed(
          *frame.GetDocument(),
          frame.Selection().Granularity() == kWordGranularity
              ? TypingCommand::kSmartDelete
              : 0);
      return true;
  }
  NOTREACHED();
  return false;
}

static bool ExecuteDeleteBackward(LocalFrame& frame,
                                  Event*,
                                  EditorCommandSource,
                                  const String&) {
  frame.GetEditor().DeleteWithDirection(DeleteDirection::kBackward,
                                        kCharacterGranularity, false, true);
  return true;
}

static bool ExecuteDeleteBackwardByDecomposingPreviousCharacter(
    LocalFrame& frame,
    Event*,
    EditorCommandSource,
    const String&) {
  DLOG(ERROR) << "DeleteBackwardByDecomposingPreviousCharacter is not "
                 "implemented, doing DeleteBackward instead";
  frame.GetEditor().DeleteWithDirection(DeleteDirection::kBackward,
                                        kCharacterGranularity, false, true);
  return true;
}

static bool ExecuteDeleteForward(LocalFrame& frame,
                                 Event*,
                                 EditorCommandSource,
                                 const String&) {
  frame.GetEditor().DeleteWithDirection(DeleteDirection::kForward,
                                        kCharacterGranularity, false, true);
  return true;
}

static bool ExecuteDeleteToBeginningOfLine(LocalFrame& frame,
                                           Event*,
                                           EditorCommandSource,
                                           const String&) {
  frame.GetEditor().DeleteWithDirection(DeleteDirection::kBackward,
                                        kLineBoundary, true, false);
  return true;
}

static bool ExecuteDeleteToBeginningOfParagraph(LocalFrame& frame,
                                                Event*,
                                                EditorCommandSource,
                                                const String&) {
  frame.GetEditor().DeleteWithDirection(DeleteDirection::kBackward,
                                        kParagraphBoundary, true, false);
  return true;
}

static bool ExecuteDeleteToEndOfLine(LocalFrame& frame,
                                     Event*,
                                     EditorCommandSource,
                                     const String&) {
  // Despite its name, this command should delete the newline at the end of a
  // paragraph if you are at the end of a paragraph (like
  // DeleteToEndOfParagraph).
  frame.GetEditor().DeleteWithDirection(DeleteDirection::kForward,
                                        kLineBoundary, true, false);
  return true;
}

static bool ExecuteDeleteToEndOfParagraph(LocalFrame& frame,
                                          Event*,
                                          EditorCommandSource,
                                          const String&) {
  // Despite its name, this command should delete the newline at the end of
  // a paragraph if you are at the end of a paragraph.
  frame.GetEditor().DeleteWithDirection(DeleteDirection::kForward,
                                        kParagraphBoundary, true, false);
  return true;
}

static bool ExecuteDeleteToMark(LocalFrame& frame,
                                Event*,
                                EditorCommandSource,
                                const String&) {
  const EphemeralRange mark =
      frame.GetEditor().Mark().ToNormalizedEphemeralRange();
  if (mark.IsNotNull()) {
    bool selected = frame.Selection().SetSelectedRange(
        UnionEphemeralRanges(mark, frame.GetEditor().SelectedRange()),
        TextAffinity::kDownstream, SelectionDirectionalMode::kNonDirectional,
        FrameSelection::kCloseTyping);
    DCHECK(selected);
    if (!selected)
      return false;
  }
  frame.GetEditor().PerformDelete();
  frame.GetEditor().SetMark(
      frame.Selection().ComputeVisibleSelectionInDOMTreeDeprecated());
  return true;
}

static bool ExecuteDeleteWordBackward(LocalFrame& frame,
                                      Event*,
                                      EditorCommandSource,
                                      const String&) {
  frame.GetEditor().DeleteWithDirection(DeleteDirection::kBackward,
                                        kWordGranularity, true, false);
  return true;
}

static bool ExecuteDeleteWordForward(LocalFrame& frame,
                                     Event*,
                                     EditorCommandSource,
                                     const String&) {
  frame.GetEditor().DeleteWithDirection(DeleteDirection::kForward,
                                        kWordGranularity, true, false);
  return true;
}

static bool ExecuteFindString(LocalFrame& frame,
                              Event*,
                              EditorCommandSource,
                              const String& value) {
  return frame.GetEditor().FindString(value, kCaseInsensitive | kWrapAround);
}

static bool ExecuteFontName(LocalFrame& frame,
                            Event*,
                            EditorCommandSource source,
                            const String& value) {
  return ExecuteApplyStyle(frame, source, InputEvent::InputType::kNone,
                           CSSPropertyFontFamily, value);
}

static bool ExecuteFontSize(LocalFrame& frame,
                            Event*,
                            EditorCommandSource source,
                            const String& value) {
  CSSValueID size;
  if (!HTMLFontElement::CssValueFromFontSizeNumber(value, size))
    return false;
  return ExecuteApplyStyle(frame, source, InputEvent::InputType::kNone,
                           CSSPropertyFontSize, size);
}

static bool ExecuteFontSizeDelta(LocalFrame& frame,
                                 Event*,
                                 EditorCommandSource source,
                                 const String& value) {
  return ExecuteApplyStyle(frame, source, InputEvent::InputType::kNone,
                           CSSPropertyWebkitFontSizeDelta, value);
}

static bool ExecuteForeColor(LocalFrame& frame,
                             Event*,
                             EditorCommandSource source,
                             const String& value) {
  return ExecuteApplyStyle(frame, source, InputEvent::InputType::kNone,
                           CSSPropertyColor, value);
}

static bool ExecuteFormatBlock(LocalFrame& frame,
                               Event*,
                               EditorCommandSource,
                               const String& value) {
  String tag_name = value.DeprecatedLower();
  if (tag_name[0] == '<' && tag_name[tag_name.length() - 1] == '>')
    tag_name = tag_name.Substring(1, tag_name.length() - 2);

  AtomicString local_name, prefix;
  if (!Document::ParseQualifiedName(AtomicString(tag_name), prefix, local_name,
                                    IGNORE_EXCEPTION_FOR_TESTING))
    return false;
  QualifiedName qualified_tag_name(prefix, local_name, xhtmlNamespaceURI);

  DCHECK(frame.GetDocument());
  FormatBlockCommand* command =
      FormatBlockCommand::Create(*frame.GetDocument(), qualified_tag_name);
  command->Apply();
  return command->DidApply();
}

static bool ExecuteForwardDelete(LocalFrame& frame,
                                 Event*,
                                 EditorCommandSource source,
                                 const String&) {
  EditingState editing_state;
  switch (source) {
    case kCommandFromMenuOrKeyBinding:
      frame.GetEditor().DeleteWithDirection(DeleteDirection::kForward,
                                            kCharacterGranularity, false, true);
      return true;
    case kCommandFromDOM:
      // Doesn't scroll to make the selection visible, or modify the kill ring.
      // ForwardDelete is not implemented in IE or Firefox, so this behavior is
      // only needed for backward compatibility with ourselves, and for
      // consistency with Delete.
      DCHECK(frame.GetDocument());
      TypingCommand::ForwardDeleteKeyPressed(*frame.GetDocument(),
                                             &editing_state);
      if (editing_state.IsAborted())
        return false;
      return true;
  }
  NOTREACHED();
  return false;
}

static bool ExecuteIgnoreSpelling(LocalFrame& frame,
                                  Event*,
                                  EditorCommandSource,
                                  const String&) {
  frame.GetSpellChecker().IgnoreSpelling();
  return true;
}

static bool ExecuteIndent(LocalFrame& frame,
                          Event*,
                          EditorCommandSource,
                          const String&) {
  DCHECK(frame.GetDocument());
  return IndentOutdentCommand::Create(*frame.GetDocument(),
                                      IndentOutdentCommand::kIndent)
      ->Apply();
}

static bool ExecuteInsertBacktab(LocalFrame& frame,
                                 Event* event,
                                 EditorCommandSource,
                                 const String&) {
  return TargetFrame(frame, event)
      ->GetEventHandler()
      .HandleTextInputEvent("\t", event);
}

static bool ExecuteInsertHorizontalRule(LocalFrame& frame,
                                        Event*,
                                        EditorCommandSource,
                                        const String& value) {
  DCHECK(frame.GetDocument());
  HTMLHRElement* rule = HTMLHRElement::Create(*frame.GetDocument());
  if (!value.IsEmpty())
    rule->SetIdAttribute(AtomicString(value));
  return ExecuteInsertElement(frame, rule);
}

static bool ExecuteInsertHTML(LocalFrame& frame,
                              Event*,
                              EditorCommandSource,
                              const String& value) {
  DCHECK(frame.GetDocument());
  return ExecuteInsertFragment(
      frame, CreateFragmentFromMarkup(*frame.GetDocument(), value, ""));
}

static bool ExecuteInsertImage(LocalFrame& frame,
                               Event*,
                               EditorCommandSource,
                               const String& value) {
  DCHECK(frame.GetDocument());
  HTMLImageElement* image = HTMLImageElement::Create(*frame.GetDocument());
  if (!value.IsEmpty())
    image->SetSrc(value);
  return ExecuteInsertElement(frame, image);
}

static bool ExecuteInsertLineBreak(LocalFrame& frame,
                                   Event* event,
                                   EditorCommandSource source,
                                   const String&) {
  switch (source) {
    case kCommandFromMenuOrKeyBinding:
      return TargetFrame(frame, event)
          ->GetEventHandler()
          .HandleTextInputEvent("\n", event, kTextEventInputLineBreak);
    case kCommandFromDOM:
      // Doesn't scroll to make the selection visible, or modify the kill ring.
      // InsertLineBreak is not implemented in IE or Firefox, so this behavior
      // is only needed for backward compatibility with ourselves, and for
      // consistency with other commands.
      DCHECK(frame.GetDocument());
      return TypingCommand::InsertLineBreak(*frame.GetDocument());
  }
  NOTREACHED();
  return false;
}

static bool ExecuteInsertNewline(LocalFrame& frame,
                                 Event* event,
                                 EditorCommandSource,
                                 const String&) {
  LocalFrame* target_frame = blink::TargetFrame(frame, event);
  return target_frame->GetEventHandler().HandleTextInputEvent(
      "\n", event,
      target_frame->GetEditor().CanEditRichly() ? kTextEventInputKeyboard
                                                : kTextEventInputLineBreak);
}

static bool ExecuteInsertNewlineInQuotedContent(LocalFrame& frame,
                                                Event*,
                                                EditorCommandSource,
                                                const String&) {
  DCHECK(frame.GetDocument());
  return TypingCommand::InsertParagraphSeparatorInQuotedContent(
      *frame.GetDocument());
}

static bool ExecuteInsertOrderedList(LocalFrame& frame,
                                     Event*,
                                     EditorCommandSource,
                                     const String&) {
  DCHECK(frame.GetDocument());
  return InsertListCommand::Create(*frame.GetDocument(),
                                   InsertListCommand::kOrderedList)
      ->Apply();
}

static bool ExecuteInsertParagraph(LocalFrame& frame,
                                   Event*,
                                   EditorCommandSource,
                                   const String&) {
  DCHECK(frame.GetDocument());
  return TypingCommand::InsertParagraphSeparator(*frame.GetDocument());
}

static bool ExecuteInsertTab(LocalFrame& frame,
                             Event* event,
                             EditorCommandSource,
                             const String&) {
  return TargetFrame(frame, event)
      ->GetEventHandler()
      .HandleTextInputEvent("\t", event);
}

static bool ExecuteInsertText(LocalFrame& frame,
                              Event*,
                              EditorCommandSource,
                              const String& value) {
  DCHECK(frame.GetDocument());
  TypingCommand::InsertText(*frame.GetDocument(), value, 0);
  return true;
}

static bool ExecuteInsertUnorderedList(LocalFrame& frame,
                                       Event*,
                                       EditorCommandSource,
                                       const String&) {
  DCHECK(frame.GetDocument());
  return InsertListCommand::Create(*frame.GetDocument(),
                                   InsertListCommand::kUnorderedList)
      ->Apply();
}

static bool ExecuteJustifyCenter(LocalFrame& frame,
                                 Event*,
                                 EditorCommandSource source,
                                 const String&) {
  return ExecuteApplyParagraphStyle(frame, source,
                                    InputEvent::InputType::kFormatJustifyCenter,
                                    CSSPropertyTextAlign, "center");
}

static bool ExecuteJustifyFull(LocalFrame& frame,
                               Event*,
                               EditorCommandSource source,
                               const String&) {
  return ExecuteApplyParagraphStyle(frame, source,
                                    InputEvent::InputType::kFormatJustifyFull,
                                    CSSPropertyTextAlign, "justify");
}

static bool ExecuteJustifyLeft(LocalFrame& frame,
                               Event*,
                               EditorCommandSource source,
                               const String&) {
  return ExecuteApplyParagraphStyle(frame, source,
                                    InputEvent::InputType::kFormatJustifyLeft,
                                    CSSPropertyTextAlign, "left");
}

static bool ExecuteJustifyRight(LocalFrame& frame,
                                Event*,
                                EditorCommandSource source,
                                const String&) {
  return ExecuteApplyParagraphStyle(frame, source,
                                    InputEvent::InputType::kFormatJustifyRight,
                                    CSSPropertyTextAlign, "right");
}

static bool ExecuteMakeTextWritingDirectionLeftToRight(LocalFrame& frame,
                                                       Event*,
                                                       EditorCommandSource,
                                                       const String&) {
  MutableStylePropertySet* style =
      MutableStylePropertySet::Create(kHTMLQuirksMode);
  style->SetProperty(CSSPropertyUnicodeBidi, CSSValueIsolate);
  style->SetProperty(CSSPropertyDirection, CSSValueLtr);
  frame.GetEditor().ApplyStyle(
      style, InputEvent::InputType::kFormatSetBlockTextDirection);
  return true;
}

static bool ExecuteMakeTextWritingDirectionNatural(LocalFrame& frame,
                                                   Event*,
                                                   EditorCommandSource,
                                                   const String&) {
  MutableStylePropertySet* style =
      MutableStylePropertySet::Create(kHTMLQuirksMode);
  style->SetProperty(CSSPropertyUnicodeBidi, CSSValueNormal);
  frame.GetEditor().ApplyStyle(
      style, InputEvent::InputType::kFormatSetBlockTextDirection);
  return true;
}

static bool ExecuteMakeTextWritingDirectionRightToLeft(LocalFrame& frame,
                                                       Event*,
                                                       EditorCommandSource,
                                                       const String&) {
  MutableStylePropertySet* style =
      MutableStylePropertySet::Create(kHTMLQuirksMode);
  style->SetProperty(CSSPropertyUnicodeBidi, CSSValueIsolate);
  style->SetProperty(CSSPropertyDirection, CSSValueRtl);
  frame.GetEditor().ApplyStyle(
      style, InputEvent::InputType::kFormatSetBlockTextDirection);
  return true;
}

static bool ExecuteMoveBackward(LocalFrame& frame,
                                Event*,
                                EditorCommandSource,
                                const String&) {
  frame.Selection().Modify(FrameSelection::kAlterationMove, kDirectionBackward,
                           kCharacterGranularity, kUserTriggered);
  return true;
}

static bool ExecuteMoveBackwardAndModifySelection(LocalFrame& frame,
                                                  Event*,
                                                  EditorCommandSource,
                                                  const String&) {
  frame.Selection().Modify(FrameSelection::kAlterationExtend,
                           kDirectionBackward, kCharacterGranularity,
                           kUserTriggered);
  return true;
}

static bool ExecuteMoveDown(LocalFrame& frame,
                            Event*,
                            EditorCommandSource,
                            const String&) {
  return frame.Selection().Modify(FrameSelection::kAlterationMove,
                                  kDirectionForward, kLineGranularity,
                                  kUserTriggered);
}

static bool ExecuteMoveDownAndModifySelection(LocalFrame& frame,
                                              Event*,
                                              EditorCommandSource,
                                              const String&) {
  frame.Selection().Modify(FrameSelection::kAlterationExtend, kDirectionForward,
                           kLineGranularity, kUserTriggered);
  return true;
}

static bool ExecuteMoveForward(LocalFrame& frame,
                               Event*,
                               EditorCommandSource,
                               const String&) {
  frame.Selection().Modify(FrameSelection::kAlterationMove, kDirectionForward,
                           kCharacterGranularity, kUserTriggered);
  return true;
}

static bool ExecuteMoveForwardAndModifySelection(LocalFrame& frame,
                                                 Event*,
                                                 EditorCommandSource,
                                                 const String&) {
  frame.Selection().Modify(FrameSelection::kAlterationExtend, kDirectionForward,
                           kCharacterGranularity, kUserTriggered);
  return true;
}

static bool ExecuteMoveLeft(LocalFrame& frame,
                            Event*,
                            EditorCommandSource,
                            const String&) {
  return frame.Selection().Modify(FrameSelection::kAlterationMove,
                                  kDirectionLeft, kCharacterGranularity,
                                  kUserTriggered);
}

static bool ExecuteMoveLeftAndModifySelection(LocalFrame& frame,
                                              Event*,
                                              EditorCommandSource,
                                              const String&) {
  frame.Selection().Modify(FrameSelection::kAlterationExtend, kDirectionLeft,
                           kCharacterGranularity, kUserTriggered);
  return true;
}

static bool ExecuteMovePageDown(LocalFrame& frame,
                                Event*,
                                EditorCommandSource,
                                const String&) {
  unsigned distance = VerticalScrollDistance(frame);
  if (!distance)
    return false;
  return frame.Selection().Modify(FrameSelection::kAlterationMove, distance,
                                  FrameSelection::kDirectionDown);
}

static bool ExecuteMovePageDownAndModifySelection(LocalFrame& frame,
                                                  Event*,
                                                  EditorCommandSource,
                                                  const String&) {
  unsigned distance = VerticalScrollDistance(frame);
  if (!distance)
    return false;
  return frame.Selection().Modify(FrameSelection::kAlterationExtend, distance,
                                  FrameSelection::kDirectionDown);
}

static bool ExecuteMovePageUp(LocalFrame& frame,
                              Event*,
                              EditorCommandSource,
                              const String&) {
  unsigned distance = VerticalScrollDistance(frame);
  if (!distance)
    return false;
  return frame.Selection().Modify(FrameSelection::kAlterationMove, distance,
                                  FrameSelection::kDirectionUp);
}

static bool ExecuteMovePageUpAndModifySelection(LocalFrame& frame,
                                                Event*,
                                                EditorCommandSource,
                                                const String&) {
  unsigned distance = VerticalScrollDistance(frame);
  if (!distance)
    return false;
  return frame.Selection().Modify(FrameSelection::kAlterationExtend, distance,
                                  FrameSelection::kDirectionUp);
}

static bool ExecuteMoveRight(LocalFrame& frame,
                             Event*,
                             EditorCommandSource,
                             const String&) {
  return frame.Selection().Modify(FrameSelection::kAlterationMove,
                                  kDirectionRight, kCharacterGranularity,
                                  kUserTriggered);
}

static bool ExecuteMoveRightAndModifySelection(LocalFrame& frame,
                                               Event*,
                                               EditorCommandSource,
                                               const String&) {
  frame.Selection().Modify(FrameSelection::kAlterationExtend, kDirectionRight,
                           kCharacterGranularity, kUserTriggered);
  return true;
}

static bool ExecuteMoveToBeginningOfDocument(LocalFrame& frame,
                                             Event*,
                                             EditorCommandSource,
                                             const String&) {
  frame.Selection().Modify(FrameSelection::kAlterationMove, kDirectionBackward,
                           kDocumentBoundary, kUserTriggered);
  return true;
}

static bool ExecuteMoveToBeginningOfDocumentAndModifySelection(
    LocalFrame& frame,
    Event*,
    EditorCommandSource,
    const String&) {
  frame.Selection().Modify(FrameSelection::kAlterationExtend,
                           kDirectionBackward, kDocumentBoundary,
                           kUserTriggered);
  return true;
}

static bool ExecuteMoveToBeginningOfLine(LocalFrame& frame,
                                         Event*,
                                         EditorCommandSource,
                                         const String&) {
  frame.Selection().Modify(FrameSelection::kAlterationMove, kDirectionBackward,
                           kLineBoundary, kUserTriggered);
  return true;
}

static bool ExecuteMoveToBeginningOfLineAndModifySelection(LocalFrame& frame,
                                                           Event*,
                                                           EditorCommandSource,
                                                           const String&) {
  frame.Selection().Modify(FrameSelection::kAlterationExtend,
                           kDirectionBackward, kLineBoundary, kUserTriggered);
  return true;
}

static bool ExecuteMoveToBeginningOfParagraph(LocalFrame& frame,
                                              Event*,
                                              EditorCommandSource,
                                              const String&) {
  frame.Selection().Modify(FrameSelection::kAlterationMove, kDirectionBackward,
                           kParagraphBoundary, kUserTriggered);
  return true;
}

static bool ExecuteMoveToBeginningOfParagraphAndModifySelection(
    LocalFrame& frame,
    Event*,
    EditorCommandSource,
    const String&) {
  frame.Selection().Modify(FrameSelection::kAlterationExtend,
                           kDirectionBackward, kParagraphBoundary,
                           kUserTriggered);
  return true;
}

static bool ExecuteMoveToBeginningOfSentence(LocalFrame& frame,
                                             Event*,
                                             EditorCommandSource,
                                             const String&) {
  frame.Selection().Modify(FrameSelection::kAlterationMove, kDirectionBackward,
                           kSentenceBoundary, kUserTriggered);
  return true;
}

static bool ExecuteMoveToBeginningOfSentenceAndModifySelection(
    LocalFrame& frame,
    Event*,
    EditorCommandSource,
    const String&) {
  frame.Selection().Modify(FrameSelection::kAlterationExtend,
                           kDirectionBackward, kSentenceBoundary,
                           kUserTriggered);
  return true;
}

static bool ExecuteMoveToEndOfDocument(LocalFrame& frame,
                                       Event*,
                                       EditorCommandSource,
                                       const String&) {
  frame.Selection().Modify(FrameSelection::kAlterationMove, kDirectionForward,
                           kDocumentBoundary, kUserTriggered);
  return true;
}

static bool ExecuteMoveToEndOfDocumentAndModifySelection(LocalFrame& frame,
                                                         Event*,
                                                         EditorCommandSource,
                                                         const String&) {
  frame.Selection().Modify(FrameSelection::kAlterationExtend, kDirectionForward,
                           kDocumentBoundary, kUserTriggered);
  return true;
}

static bool ExecuteMoveToEndOfSentence(LocalFrame& frame,
                                       Event*,
                                       EditorCommandSource,
                                       const String&) {
  frame.Selection().Modify(FrameSelection::kAlterationMove, kDirectionForward,
                           kSentenceBoundary, kUserTriggered);
  return true;
}

static bool ExecuteMoveToEndOfSentenceAndModifySelection(LocalFrame& frame,
                                                         Event*,
                                                         EditorCommandSource,
                                                         const String&) {
  frame.Selection().Modify(FrameSelection::kAlterationExtend, kDirectionForward,
                           kSentenceBoundary, kUserTriggered);
  return true;
}

static bool ExecuteMoveToEndOfLine(LocalFrame& frame,
                                   Event*,
                                   EditorCommandSource,
                                   const String&) {
  frame.Selection().Modify(FrameSelection::kAlterationMove, kDirectionForward,
                           kLineBoundary, kUserTriggered);
  return true;
}

static bool ExecuteMoveToEndOfLineAndModifySelection(LocalFrame& frame,
                                                     Event*,
                                                     EditorCommandSource,
                                                     const String&) {
  frame.Selection().Modify(FrameSelection::kAlterationExtend, kDirectionForward,
                           kLineBoundary, kUserTriggered);
  return true;
}

static bool ExecuteMoveToEndOfParagraph(LocalFrame& frame,
                                        Event*,
                                        EditorCommandSource,
                                        const String&) {
  frame.Selection().Modify(FrameSelection::kAlterationMove, kDirectionForward,
                           kParagraphBoundary, kUserTriggered);
  return true;
}

static bool ExecuteMoveToEndOfParagraphAndModifySelection(LocalFrame& frame,
                                                          Event*,
                                                          EditorCommandSource,
                                                          const String&) {
  frame.Selection().Modify(FrameSelection::kAlterationExtend, kDirectionForward,
                           kParagraphBoundary, kUserTriggered);
  return true;
}

static bool ExecuteMoveParagraphBackward(LocalFrame& frame,
                                         Event*,
                                         EditorCommandSource,
                                         const String&) {
  frame.Selection().Modify(FrameSelection::kAlterationMove, kDirectionBackward,
                           kParagraphGranularity, kUserTriggered);
  return true;
}

static bool ExecuteMoveParagraphBackwardAndModifySelection(LocalFrame& frame,
                                                           Event*,
                                                           EditorCommandSource,
                                                           const String&) {
  frame.Selection().Modify(FrameSelection::kAlterationExtend,
                           kDirectionBackward, kParagraphGranularity,
                           kUserTriggered);
  return true;
}

static bool ExecuteMoveParagraphForward(LocalFrame& frame,
                                        Event*,
                                        EditorCommandSource,
                                        const String&) {
  frame.Selection().Modify(FrameSelection::kAlterationMove, kDirectionForward,
                           kParagraphGranularity, kUserTriggered);
  return true;
}

static bool ExecuteMoveParagraphForwardAndModifySelection(LocalFrame& frame,
                                                          Event*,
                                                          EditorCommandSource,
                                                          const String&) {
  frame.Selection().Modify(FrameSelection::kAlterationExtend, kDirectionForward,
                           kParagraphGranularity, kUserTriggered);
  return true;
}

static bool ExecuteMoveUp(LocalFrame& frame,
                          Event*,
                          EditorCommandSource,
                          const String&) {
  return frame.Selection().Modify(FrameSelection::kAlterationMove,
                                  kDirectionBackward, kLineGranularity,
                                  kUserTriggered);
}

static bool ExecuteMoveUpAndModifySelection(LocalFrame& frame,
                                            Event*,
                                            EditorCommandSource,
                                            const String&) {
  frame.Selection().Modify(FrameSelection::kAlterationExtend,
                           kDirectionBackward, kLineGranularity,
                           kUserTriggered);
  return true;
}

static bool ExecuteMoveWordBackward(LocalFrame& frame,
                                    Event*,
                                    EditorCommandSource,
                                    const String&) {
  frame.Selection().Modify(FrameSelection::kAlterationMove, kDirectionBackward,
                           kWordGranularity, kUserTriggered);
  return true;
}

static bool ExecuteMoveWordBackwardAndModifySelection(LocalFrame& frame,
                                                      Event*,
                                                      EditorCommandSource,
                                                      const String&) {
  frame.Selection().Modify(FrameSelection::kAlterationExtend,
                           kDirectionBackward, kWordGranularity,
                           kUserTriggered);
  return true;
}

static bool ExecuteMoveWordForward(LocalFrame& frame,
                                   Event*,
                                   EditorCommandSource,
                                   const String&) {
  frame.Selection().Modify(FrameSelection::kAlterationMove, kDirectionForward,
                           kWordGranularity, kUserTriggered);
  return true;
}

static bool ExecuteMoveWordForwardAndModifySelection(LocalFrame& frame,
                                                     Event*,
                                                     EditorCommandSource,
                                                     const String&) {
  frame.Selection().Modify(FrameSelection::kAlterationExtend, kDirectionForward,
                           kWordGranularity, kUserTriggered);
  return true;
}

static bool ExecuteMoveWordLeft(LocalFrame& frame,
                                Event*,
                                EditorCommandSource,
                                const String&) {
  frame.Selection().Modify(FrameSelection::kAlterationMove, kDirectionLeft,
                           kWordGranularity, kUserTriggered);
  return true;
}

static bool ExecuteMoveWordLeftAndModifySelection(LocalFrame& frame,
                                                  Event*,
                                                  EditorCommandSource,
                                                  const String&) {
  frame.Selection().Modify(FrameSelection::kAlterationExtend, kDirectionLeft,
                           kWordGranularity, kUserTriggered);
  return true;
}

static bool ExecuteMoveWordRight(LocalFrame& frame,
                                 Event*,
                                 EditorCommandSource,
                                 const String&) {
  frame.Selection().Modify(FrameSelection::kAlterationMove, kDirectionRight,
                           kWordGranularity, kUserTriggered);
  return true;
}

static bool ExecuteMoveWordRightAndModifySelection(LocalFrame& frame,
                                                   Event*,
                                                   EditorCommandSource,
                                                   const String&) {
  frame.Selection().Modify(FrameSelection::kAlterationExtend, kDirectionRight,
                           kWordGranularity, kUserTriggered);
  return true;
}

static bool ExecuteMoveToLeftEndOfLine(LocalFrame& frame,
                                       Event*,
                                       EditorCommandSource,
                                       const String&) {
  frame.Selection().Modify(FrameSelection::kAlterationMove, kDirectionLeft,
                           kLineBoundary, kUserTriggered);
  return true;
}

static bool ExecuteMoveToLeftEndOfLineAndModifySelection(LocalFrame& frame,
                                                         Event*,
                                                         EditorCommandSource,
                                                         const String&) {
  frame.Selection().Modify(FrameSelection::kAlterationExtend, kDirectionLeft,
                           kLineBoundary, kUserTriggered);
  return true;
}

static bool ExecuteMoveToRightEndOfLine(LocalFrame& frame,
                                        Event*,
                                        EditorCommandSource,
                                        const String&) {
  frame.Selection().Modify(FrameSelection::kAlterationMove, kDirectionRight,
                           kLineBoundary, kUserTriggered);
  return true;
}

static bool ExecuteMoveToRightEndOfLineAndModifySelection(LocalFrame& frame,
                                                          Event*,
                                                          EditorCommandSource,
                                                          const String&) {
  frame.Selection().Modify(FrameSelection::kAlterationExtend, kDirectionRight,
                           kLineBoundary, kUserTriggered);
  return true;
}

static bool ExecuteOutdent(LocalFrame& frame,
                           Event*,
                           EditorCommandSource,
                           const String&) {
  DCHECK(frame.GetDocument());
  return IndentOutdentCommand::Create(*frame.GetDocument(),
                                      IndentOutdentCommand::kOutdent)
      ->Apply();
}

static bool ExecuteToggleOverwrite(LocalFrame& frame,
                                   Event*,
                                   EditorCommandSource,
                                   const String&) {
  frame.GetEditor().ToggleOverwriteModeEnabled();
  return true;
}

static bool CanReadClipboard(LocalFrame& frame, EditorCommandSource source) {
  if (source == kCommandFromMenuOrKeyBinding)
    return true;
  Settings* settings = frame.GetSettings();
  bool default_value = settings &&
                       settings->GetJavaScriptCanAccessClipboard() &&
                       settings->GetDOMPasteAllowed();
  return frame.GetEditor().Client().CanPaste(&frame, default_value);
}

static bool ExecutePaste(LocalFrame& frame,
                         Event*,
                         EditorCommandSource source,
                         const String&) {
  // To support |allowExecutionWhenDisabled|, we need to check clipboard
  // accessibility here rather than |Editor::Command::execute()|.
  // TODO(yosin) We should move checking |canReadClipboard()| to
  // |Editor::Command::execute()| with introducing appropriate predicate, e.g.
  // |canExecute()|. See also "Copy", and "Cut" command.
  if (!CanReadClipboard(frame, source))
    return false;
  frame.GetEditor().Paste(source);
  return true;
}

static bool ExecutePasteGlobalSelection(LocalFrame& frame,
                                        Event*,
                                        EditorCommandSource source,
                                        const String&) {
  // To support |allowExecutionWhenDisabled|, we need to check clipboard
  // accessibility here rather than |Editor::Command::execute()|.
  // TODO(yosin) We should move checking |canReadClipboard()| to
  // |Editor::Command::execute()| with introducing appropriate predicate, e.g.
  // |canExecute()|. See also "Copy", and "Cut" command.
  if (!CanReadClipboard(frame, source))
    return false;
  if (!frame.GetEditor().Behavior().SupportsGlobalSelection())
    return false;
  DCHECK_EQ(source, kCommandFromMenuOrKeyBinding);

  bool old_selection_mode = Pasteboard::GeneralPasteboard()->IsSelectionMode();
  Pasteboard::GeneralPasteboard()->SetSelectionMode(true);
  frame.GetEditor().Paste(source);
  Pasteboard::GeneralPasteboard()->SetSelectionMode(old_selection_mode);
  return true;
}

static bool ExecutePasteAndMatchStyle(LocalFrame& frame,
                                      Event*,
                                      EditorCommandSource source,
                                      const String&) {
  frame.GetEditor().PasteAsPlainText(source);
  return true;
}

static bool ExecutePrint(LocalFrame& frame,
                         Event*,
                         EditorCommandSource,
                         const String&) {
  Page* page = frame.GetPage();
  if (!page)
    return false;
  return page->GetChromeClient().Print(&frame);
}

static bool ExecuteRedo(LocalFrame& frame,
                        Event*,
                        EditorCommandSource,
                        const String&) {
  frame.GetEditor().Redo();
  return true;
}

static bool ExecuteRemoveFormat(LocalFrame& frame,
                                Event*,
                                EditorCommandSource,
                                const String&) {
  frame.GetEditor().RemoveFormattingAndStyle();
  return true;
}

static bool ExecuteScrollPageBackward(LocalFrame& frame,
                                      Event*,
                                      EditorCommandSource,
                                      const String&) {
  return frame.GetEventHandler().BubblingScroll(kScrollBlockDirectionBackward,
                                                kScrollByPage);
}

static bool ExecuteScrollPageForward(LocalFrame& frame,
                                     Event*,
                                     EditorCommandSource,
                                     const String&) {
  return frame.GetEventHandler().BubblingScroll(kScrollBlockDirectionForward,
                                                kScrollByPage);
}

static bool ExecuteScrollLineUp(LocalFrame& frame,
                                Event*,
                                EditorCommandSource,
                                const String&) {
  return frame.GetEventHandler().BubblingScroll(kScrollUpIgnoringWritingMode,
                                                kScrollByLine);
}

static bool ExecuteScrollLineDown(LocalFrame& frame,
                                  Event*,
                                  EditorCommandSource,
                                  const String&) {
  return frame.GetEventHandler().BubblingScroll(kScrollDownIgnoringWritingMode,
                                                kScrollByLine);
}

static bool ExecuteScrollToBeginningOfDocument(LocalFrame& frame,
                                               Event*,
                                               EditorCommandSource,
                                               const String&) {
  return frame.GetEventHandler().BubblingScroll(kScrollBlockDirectionBackward,
                                                kScrollByDocument);
}

static bool ExecuteScrollToEndOfDocument(LocalFrame& frame,
                                         Event*,
                                         EditorCommandSource,
                                         const String&) {
  return frame.GetEventHandler().BubblingScroll(kScrollBlockDirectionForward,
                                                kScrollByDocument);
}

static bool ExecuteSelectAll(LocalFrame& frame,
                             Event*,
                             EditorCommandSource source,
                             const String&) {
  const EUserTriggered user_triggered = source == kCommandFromMenuOrKeyBinding
                                            ? kUserTriggered
                                            : kNotUserTriggered;
  frame.Selection().SelectAll(user_triggered);
  return true;
}

static bool ExecuteSelectLine(LocalFrame& frame,
                              Event*,
                              EditorCommandSource,
                              const String&) {
  return ExpandSelectionToGranularity(frame, kLineGranularity);
}

static bool ExecuteSelectParagraph(LocalFrame& frame,
                                   Event*,
                                   EditorCommandSource,
                                   const String&) {
  return ExpandSelectionToGranularity(frame, kParagraphGranularity);
}

static bool ExecuteSelectSentence(LocalFrame& frame,
                                  Event*,
                                  EditorCommandSource,
                                  const String&) {
  return ExpandSelectionToGranularity(frame, kSentenceGranularity);
}

static bool ExecuteSelectToMark(LocalFrame& frame,
                                Event*,
                                EditorCommandSource,
                                const String&) {
  const EphemeralRange mark =
      frame.GetEditor().Mark().ToNormalizedEphemeralRange();
  EphemeralRange selection = frame.GetEditor().SelectedRange();
  if (mark.IsNull() || selection.IsNull())
    return false;
  frame.Selection().SetSelectedRange(
      UnionEphemeralRanges(mark, selection), TextAffinity::kDownstream,
      SelectionDirectionalMode::kNonDirectional, FrameSelection::kCloseTyping);
  return true;
}

static bool ExecuteSelectWord(LocalFrame& frame,
                              Event*,
                              EditorCommandSource,
                              const String&) {
  return ExpandSelectionToGranularity(frame, kWordGranularity);
}

static bool ExecuteSetMark(LocalFrame& frame,
                           Event*,
                           EditorCommandSource,
                           const String&) {
  frame.GetEditor().SetMark(
      frame.Selection().ComputeVisibleSelectionInDOMTreeDeprecated());
  return true;
}

static bool ExecuteStrikethrough(LocalFrame& frame,
                                 Event*,
                                 EditorCommandSource source,
                                 const String&) {
  CSSIdentifierValue* line_through =
      CSSIdentifierValue::Create(CSSValueLineThrough);
  return ExecuteToggleStyleInList(
      frame, source, InputEvent::InputType::kFormatStrikeThrough,
      CSSPropertyWebkitTextDecorationsInEffect, line_through);
}

static bool ExecuteStyleWithCSS(LocalFrame& frame,
                                Event*,
                                EditorCommandSource,
                                const String& value) {
  frame.GetEditor().SetShouldStyleWithCSS(
      !DeprecatedEqualIgnoringCase(value, "false"));
  return true;
}

static bool ExecuteUseCSS(LocalFrame& frame,
                          Event*,
                          EditorCommandSource,
                          const String& value) {
  frame.GetEditor().SetShouldStyleWithCSS(
      DeprecatedEqualIgnoringCase(value, "false"));
  return true;
}

static bool ExecuteSubscript(LocalFrame& frame,
                             Event*,
                             EditorCommandSource source,
                             const String&) {
  return ExecuteToggleStyle(frame, source,
                            InputEvent::InputType::kFormatSubscript,
                            CSSPropertyVerticalAlign, "baseline", "sub");
}

static bool ExecuteSuperscript(LocalFrame& frame,
                               Event*,
                               EditorCommandSource source,
                               const String&) {
  return ExecuteToggleStyle(frame, source,
                            InputEvent::InputType::kFormatSuperscript,
                            CSSPropertyVerticalAlign, "baseline", "super");
}

static bool ExecuteSwapWithMark(LocalFrame& frame,
                                Event*,
                                EditorCommandSource,
                                const String&) {
  const VisibleSelection& mark = frame.GetEditor().Mark();
  const VisibleSelection& selection =
      frame.Selection().ComputeVisibleSelectionInDOMTreeDeprecated();
  if (mark.IsNone() || selection.IsNone())
    return false;
  frame.Selection().SetSelection(mark.AsSelection());
  frame.GetEditor().SetMark(selection);
  return true;
}

static bool ExecuteToggleBold(LocalFrame& frame,
                              Event*,
                              EditorCommandSource source,
                              const String&) {
  return ExecuteToggleStyle(frame, source, InputEvent::InputType::kFormatBold,
                            CSSPropertyFontWeight, "normal", "bold");
}

static bool ExecuteToggleItalic(LocalFrame& frame,
                                Event*,
                                EditorCommandSource source,
                                const String&) {
  return ExecuteToggleStyle(frame, source, InputEvent::InputType::kFormatItalic,
                            CSSPropertyFontStyle, "normal", "italic");
}

static bool ExecuteTranspose(LocalFrame& frame,
                             Event*,
                             EditorCommandSource,
                             const String&) {
  frame.GetEditor().Transpose();
  return true;
}

static bool ExecuteUnderline(LocalFrame& frame,
                             Event*,
                             EditorCommandSource source,
                             const String&) {
  CSSIdentifierValue* underline = CSSIdentifierValue::Create(CSSValueUnderline);
  return ExecuteToggleStyleInList(
      frame, source, InputEvent::InputType::kFormatUnderline,
      CSSPropertyWebkitTextDecorationsInEffect, underline);
}

static bool ExecuteUndo(LocalFrame& frame,
                        Event*,
                        EditorCommandSource,
                        const String&) {
  frame.GetEditor().Undo();
  return true;
}

static bool ExecuteUnlink(LocalFrame& frame,
                          Event*,
                          EditorCommandSource,
                          const String&) {
  DCHECK(frame.GetDocument());
  return UnlinkCommand::Create(*frame.GetDocument())->Apply();
}

static bool ExecuteUnscript(LocalFrame& frame,
                            Event*,
                            EditorCommandSource source,
                            const String&) {
  return ExecuteApplyStyle(frame, source, InputEvent::InputType::kNone,
                           CSSPropertyVerticalAlign, "baseline");
}

static bool ExecuteUnselect(LocalFrame& frame,
                            Event*,
                            EditorCommandSource,
                            const String&) {
  frame.Selection().Clear();
  return true;
}

static bool ExecuteYank(LocalFrame& frame,
                        Event*,
                        EditorCommandSource,
                        const String&) {
  const String& yank_string = frame.GetEditor().GetKillRing().Yank();
  if (DispatchBeforeInputInsertText(
          EventTargetNodeForDocument(frame.GetDocument()), yank_string,
          InputEvent::InputType::kInsertFromYank) !=
      DispatchEventResult::kNotCanceled)
    return true;

  // 'beforeinput' event handler may destroy document.
  if (frame.GetDocument()->GetFrame() != &frame)
    return false;

  // TODO(editing-dev): The use of updateStyleAndLayoutIgnorePendingStylesheets
  // needs to be audited. see http://crbug.com/590369 for more details.
  frame.GetDocument()->UpdateStyleAndLayoutIgnorePendingStylesheets();

  frame.GetEditor().InsertTextWithoutSendingTextEvent(
      yank_string, false, 0, InputEvent::InputType::kInsertFromYank);
  frame.GetEditor().GetKillRing().SetToYankedState();
  return true;
}

static bool ExecuteYankAndSelect(LocalFrame& frame,
                                 Event*,
                                 EditorCommandSource,
                                 const String&) {
  const String& yank_string = frame.GetEditor().GetKillRing().Yank();
  if (DispatchBeforeInputInsertText(
          EventTargetNodeForDocument(frame.GetDocument()), yank_string,
          InputEvent::InputType::kInsertFromYank) !=
      DispatchEventResult::kNotCanceled)
    return true;

  // 'beforeinput' event handler may destroy document.
  if (frame.GetDocument()->GetFrame() != &frame)
    return false;

  // TODO(editing-dev): The use of updateStyleAndLayoutIgnorePendingStylesheets
  // needs to be audited. see http://crbug.com/590369 for more details.
  frame.GetDocument()->UpdateStyleAndLayoutIgnorePendingStylesheets();

  frame.GetEditor().InsertTextWithoutSendingTextEvent(
      frame.GetEditor().GetKillRing().Yank(), true, 0,
      InputEvent::InputType::kInsertFromYank);
  frame.GetEditor().GetKillRing().SetToYankedState();
  return true;
}

// Supported functions

static bool Supported(LocalFrame*) {
  return true;
}

static bool SupportedFromMenuOrKeyBinding(LocalFrame*) {
  return false;
}

// Enabled functions

static bool Enabled(LocalFrame&, Event*, EditorCommandSource) {
  return true;
}

static bool EnabledVisibleSelection(LocalFrame& frame,
                                    Event* event,
                                    EditorCommandSource source) {
  frame.GetDocument()->UpdateStyleAndLayoutIgnorePendingStylesheets();

  if (source == kCommandFromMenuOrKeyBinding &&
      !frame.Selection().SelectionHasFocus())
    return false;

  // The term "visible" here includes a caret in editable text or a range in any
  // text.
  const VisibleSelection& selection =
      frame.GetEditor().SelectionForCommand(event);
  return (selection.IsCaret() && selection.IsContentEditable()) ||
         selection.IsRange();
}

static bool EnabledVisibleSelectionAndMark(LocalFrame& frame,
                                           Event* event,
                                           EditorCommandSource) {
  frame.GetDocument()->UpdateStyleAndLayoutIgnorePendingStylesheets();

  const VisibleSelection& selection =
      frame.GetEditor().SelectionForCommand(event);
  return ((selection.IsCaret() && selection.IsContentEditable()) ||
          selection.IsRange()) &&
         !frame.GetEditor().Mark().IsNone();
}

static bool EnableCaretInEditableText(LocalFrame& frame,
                                      Event* event,
                                      EditorCommandSource source) {
  frame.GetDocument()->UpdateStyleAndLayoutIgnorePendingStylesheets();

  if (source == kCommandFromMenuOrKeyBinding &&
      !frame.Selection().SelectionHasFocus())
    return false;
  const VisibleSelection& selection =
      frame.GetEditor().SelectionForCommand(event);
  return selection.IsCaret() && selection.IsContentEditable();
}

static bool EnabledCopy(LocalFrame& frame, Event*, EditorCommandSource source) {
  if (!CanWriteClipboard(frame, source))
    return false;
  if (source == kCommandFromMenuOrKeyBinding &&
      !frame.Selection().SelectionHasFocus())
    return false;
  return frame.GetEditor().CanDHTMLCopy() || frame.GetEditor().CanCopy();
}

static bool EnabledCut(LocalFrame& frame, Event*, EditorCommandSource source) {
  if (!CanWriteClipboard(frame, source))
    return false;
  if (source == kCommandFromMenuOrKeyBinding &&
      !frame.Selection().SelectionHasFocus())
    return false;
  return frame.GetEditor().CanDHTMLCut() || frame.GetEditor().CanCut();
}

static bool EnabledInEditableText(LocalFrame& frame,
                                  Event* event,
                                  EditorCommandSource source) {
  frame.GetDocument()->UpdateStyleAndLayoutIgnorePendingStylesheets();
  if (source == kCommandFromMenuOrKeyBinding &&
      !frame.Selection().SelectionHasFocus())
    return false;
  return frame.GetEditor().SelectionForCommand(event).RootEditableElement();
}

static bool EnabledDelete(LocalFrame& frame,
                          Event* event,
                          EditorCommandSource source) {
  switch (source) {
    case kCommandFromMenuOrKeyBinding:
      return frame.Selection().SelectionHasFocus() &&
             frame.GetEditor().CanDelete();
    case kCommandFromDOM:
      // "Delete" from DOM is like delete/backspace keypress, affects selected
      // range if non-empty, otherwise removes a character
      return EnabledInEditableText(frame, event, source);
  }
  NOTREACHED();
  return false;
}

static bool EnabledInRichlyEditableText(LocalFrame& frame,
                                        Event*,
                                        EditorCommandSource source) {
  frame.GetDocument()->UpdateStyleAndLayoutIgnorePendingStylesheets();
  if (source == kCommandFromMenuOrKeyBinding &&
      !frame.Selection().SelectionHasFocus())
    return false;
  return !frame.Selection()
              .ComputeVisibleSelectionInDOMTreeDeprecated()
              .IsNone() &&
         frame.Selection()
             .ComputeVisibleSelectionInDOMTreeDeprecated()
             .IsContentRichlyEditable() &&
         frame.Selection()
             .ComputeVisibleSelectionInDOMTreeDeprecated()
             .RootEditableElement();
}

static bool EnabledPaste(LocalFrame& frame,
                         Event*,
                         EditorCommandSource source) {
  if (!CanReadClipboard(frame, source))
    return false;
  if (source == kCommandFromMenuOrKeyBinding &&
      !frame.Selection().SelectionHasFocus())
    return false;
  return frame.GetEditor().CanPaste();
}

static bool EnabledRangeInEditableText(LocalFrame& frame,
                                       Event*,
                                       EditorCommandSource source) {
  frame.GetDocument()->UpdateStyleAndLayoutIgnorePendingStylesheets();
  if (source == kCommandFromMenuOrKeyBinding &&
      !frame.Selection().SelectionHasFocus())
    return false;
  return frame.Selection()
             .ComputeVisibleSelectionInDOMTreeDeprecated()
             .IsRange() &&
         frame.Selection()
             .ComputeVisibleSelectionInDOMTreeDeprecated()
             .IsContentEditable();
}

static bool EnabledRangeInRichlyEditableText(LocalFrame& frame,
                                             Event*,
                                             EditorCommandSource source) {
  frame.GetDocument()->UpdateStyleAndLayoutIgnorePendingStylesheets();
  if (source == kCommandFromMenuOrKeyBinding &&
      !frame.Selection().SelectionHasFocus())
    return false;
  return frame.Selection()
             .ComputeVisibleSelectionInDOMTreeDeprecated()
             .IsRange() &&
         frame.Selection()
             .ComputeVisibleSelectionInDOMTreeDeprecated()
             .IsContentRichlyEditable();
}

static bool EnabledRedo(LocalFrame& frame, Event*, EditorCommandSource) {
  return frame.GetEditor().CanRedo();
}

static bool EnabledUndo(LocalFrame& frame, Event*, EditorCommandSource) {
  return frame.GetEditor().CanUndo();
}

static bool EnabledUnselect(LocalFrame& frame,
                            Event* event,
                            EditorCommandSource) {
  frame.GetDocument()->UpdateStyleAndLayoutIgnorePendingStylesheets();

  // The term "visible" here includes a caret in editable text or a range in any
  // text.
  const VisibleSelection& selection =
      frame.GetEditor().SelectionForCommand(event);
  return (selection.IsCaret() && selection.IsContentEditable()) ||
         selection.IsRange();
}

static bool EnabledSelectAll(LocalFrame& frame,
                             Event*,
                             EditorCommandSource source) {
  // TODO(editing-dev): The use of updateStyleAndLayoutIgnorePendingStylesheets
  // needs to be audited.  See http://crbug.com/590369 for more details.
  frame.GetDocument()->UpdateStyleAndLayoutIgnorePendingStylesheets();
  const VisibleSelection& selection =
      frame.Selection().ComputeVisibleSelectionInDOMTree();
  if (selection.IsNone())
    return true;
  // Hidden selection appears as no selection to users, in which case user-
  // triggered SelectAll should be enabled and act as if there is no selection.
  if (source == kCommandFromMenuOrKeyBinding && frame.Selection().IsHidden())
    return true;
  if (Node* root = HighestEditableRoot(selection.Start())) {
    if (!root->hasChildren())
      return false;
    // TODO(amaralp): Return false if already fully selected.
  }
  // TODO(amaralp): Address user-select handling.
  return true;
}

// State functions

static TriState StateNone(LocalFrame&, Event*) {
  return kFalseTriState;
}

static TriState StateBold(LocalFrame& frame, Event*) {
  return StateStyle(frame, CSSPropertyFontWeight, "bold");
}

static TriState StateItalic(LocalFrame& frame, Event*) {
  return StateStyle(frame, CSSPropertyFontStyle, "italic");
}

static TriState StateOrderedList(LocalFrame& frame, Event*) {
  return SelectionListState(frame.Selection(), olTag);
}

static TriState StateStrikethrough(LocalFrame& frame, Event*) {
  return StateStyle(frame, CSSPropertyWebkitTextDecorationsInEffect,
                    "line-through");
}

static TriState StateStyleWithCSS(LocalFrame& frame, Event*) {
  return frame.GetEditor().ShouldStyleWithCSS() ? kTrueTriState
                                                : kFalseTriState;
}

static TriState StateSubscript(LocalFrame& frame, Event*) {
  return StateStyle(frame, CSSPropertyVerticalAlign, "sub");
}

static TriState StateSuperscript(LocalFrame& frame, Event*) {
  return StateStyle(frame, CSSPropertyVerticalAlign, "super");
}

static TriState StateTextWritingDirectionLeftToRight(LocalFrame& frame,
                                                     Event*) {
  return StateTextWritingDirection(frame, LeftToRightWritingDirection);
}

static TriState StateTextWritingDirectionNatural(LocalFrame& frame, Event*) {
  return StateTextWritingDirection(frame, NaturalWritingDirection);
}

static TriState StateTextWritingDirectionRightToLeft(LocalFrame& frame,
                                                     Event*) {
  return StateTextWritingDirection(frame, RightToLeftWritingDirection);
}

static TriState StateUnderline(LocalFrame& frame, Event*) {
  return StateStyle(frame, CSSPropertyWebkitTextDecorationsInEffect,
                    "underline");
}

static TriState StateUnorderedList(LocalFrame& frame, Event*) {
  return SelectionListState(frame.Selection(), ulTag);
}

static TriState StateJustifyCenter(LocalFrame& frame, Event*) {
  return StateStyle(frame, CSSPropertyTextAlign, "center");
}

static TriState StateJustifyFull(LocalFrame& frame, Event*) {
  return StateStyle(frame, CSSPropertyTextAlign, "justify");
}

static TriState StateJustifyLeft(LocalFrame& frame, Event*) {
  return StateStyle(frame, CSSPropertyTextAlign, "left");
}

static TriState StateJustifyRight(LocalFrame& frame, Event*) {
  return StateStyle(frame, CSSPropertyTextAlign, "right");
}

// Value functions

static String ValueStateOrNull(const EditorInternalCommand& self,
                               LocalFrame& frame,
                               Event* triggering_event) {
  if (self.state == StateNone)
    return String();
  return self.state(frame, triggering_event) == kTrueTriState ? "true"
                                                              : "false";
}

// The command has no value.
// https://w3c.github.io/editing/execCommand.html#querycommandvalue()
// > ... or has no value, return the empty string.
static String ValueEmpty(const EditorInternalCommand&, LocalFrame&, Event*) {
  return g_empty_string;
}

static String ValueBackColor(const EditorInternalCommand&,
                             LocalFrame& frame,
                             Event*) {
  return ValueStyle(frame, CSSPropertyBackgroundColor);
}

static String ValueDefaultParagraphSeparator(const EditorInternalCommand&,
                                             LocalFrame& frame,
                                             Event*) {
  switch (frame.GetEditor().DefaultParagraphSeparator()) {
    case kEditorParagraphSeparatorIsDiv:
      return divTag.LocalName();
    case kEditorParagraphSeparatorIsP:
      return pTag.LocalName();
  }

  NOTREACHED();
  return String();
}

static String ValueFontName(const EditorInternalCommand&,
                            LocalFrame& frame,
                            Event*) {
  return ValueStyle(frame, CSSPropertyFontFamily);
}

static String ValueFontSize(const EditorInternalCommand&,
                            LocalFrame& frame,
                            Event*) {
  return ValueStyle(frame, CSSPropertyFontSize);
}

static String ValueFontSizeDelta(const EditorInternalCommand&,
                                 LocalFrame& frame,
                                 Event*) {
  return ValueStyle(frame, CSSPropertyWebkitFontSizeDelta);
}

static String ValueForeColor(const EditorInternalCommand&,
                             LocalFrame& frame,
                             Event*) {
  return ValueStyle(frame, CSSPropertyColor);
}

static String ValueFormatBlock(const EditorInternalCommand&,
                               LocalFrame& frame,
                               Event*) {
  const VisibleSelection& selection =
      frame.Selection().ComputeVisibleSelectionInDOMTreeDeprecated();
  if (!selection.IsNonOrphanedCaretOrRange() || !selection.IsContentEditable())
    return "";
  Element* format_block_element =
      FormatBlockCommand::ElementForFormatBlockCommand(
          FirstEphemeralRangeOf(selection));
  if (!format_block_element)
    return "";
  return format_block_element->localName();
}

// Map of functions

static const EditorInternalCommand* InternalCommand(
    const String& command_name) {
  static const EditorInternalCommand kEditorCommands[] = {
      // Lists all commands in blink::WebEditingCommandType.
      // Must be ordered by |commandType| for index lookup.
      // Covered by unit tests in EditingCommandTest.cpp
      {WebEditingCommandType::kAlignJustified, ExecuteJustifyFull,
       SupportedFromMenuOrKeyBinding, EnabledInRichlyEditableText, StateNone,
       ValueStateOrNull, kNotTextInsertion, kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kAlignLeft, ExecuteJustifyLeft,
       SupportedFromMenuOrKeyBinding, EnabledInRichlyEditableText, StateNone,
       ValueStateOrNull, kNotTextInsertion, kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kAlignRight, ExecuteJustifyRight,
       SupportedFromMenuOrKeyBinding, EnabledInRichlyEditableText, StateNone,
       ValueStateOrNull, kNotTextInsertion, kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kBackColor, ExecuteBackColor, Supported,
       EnabledInRichlyEditableText, StateNone, ValueBackColor,
       kNotTextInsertion, kDoNotAllowExecutionWhenDisabled},
      // FIXME: remove BackwardDelete when Safari for Windows stops using it.
      {WebEditingCommandType::kBackwardDelete, ExecuteDeleteBackward,
       SupportedFromMenuOrKeyBinding, EnabledInEditableText, StateNone,
       ValueStateOrNull, kNotTextInsertion, kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kBold, ExecuteToggleBold, Supported,
       EnabledInRichlyEditableText, StateBold, ValueStateOrNull,
       kNotTextInsertion, kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kCopy, ExecuteCopy, Supported, EnabledCopy,
       StateNone, ValueStateOrNull, kNotTextInsertion,
       kAllowExecutionWhenDisabled},
      {WebEditingCommandType::kCreateLink, ExecuteCreateLink, Supported,
       EnabledInRichlyEditableText, StateNone, ValueStateOrNull,
       kNotTextInsertion, kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kCut, ExecuteCut, Supported, EnabledCut,
       StateNone, ValueStateOrNull, kNotTextInsertion,
       kAllowExecutionWhenDisabled},
      {WebEditingCommandType::kDefaultParagraphSeparator,
       ExecuteDefaultParagraphSeparator, Supported, Enabled, StateNone,
       ValueDefaultParagraphSeparator, kNotTextInsertion,
       kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kDelete, ExecuteDelete, Supported, EnabledDelete,
       StateNone, ValueStateOrNull, kNotTextInsertion,
       kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kDeleteBackward, ExecuteDeleteBackward,
       SupportedFromMenuOrKeyBinding, EnabledInEditableText, StateNone,
       ValueStateOrNull, kNotTextInsertion, kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kDeleteBackwardByDecomposingPreviousCharacter,
       ExecuteDeleteBackwardByDecomposingPreviousCharacter,
       SupportedFromMenuOrKeyBinding, EnabledInEditableText, StateNone,
       ValueStateOrNull, kNotTextInsertion, kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kDeleteForward, ExecuteDeleteForward,
       SupportedFromMenuOrKeyBinding, EnabledInEditableText, StateNone,
       ValueStateOrNull, kNotTextInsertion, kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kDeleteToBeginningOfLine,
       ExecuteDeleteToBeginningOfLine, SupportedFromMenuOrKeyBinding,
       EnabledInEditableText, StateNone, ValueStateOrNull, kNotTextInsertion,
       kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kDeleteToBeginningOfParagraph,
       ExecuteDeleteToBeginningOfParagraph, SupportedFromMenuOrKeyBinding,
       EnabledInEditableText, StateNone, ValueStateOrNull, kNotTextInsertion,
       kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kDeleteToEndOfLine, ExecuteDeleteToEndOfLine,
       SupportedFromMenuOrKeyBinding, EnabledInEditableText, StateNone,
       ValueStateOrNull, kNotTextInsertion, kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kDeleteToEndOfParagraph,
       ExecuteDeleteToEndOfParagraph, SupportedFromMenuOrKeyBinding,
       EnabledInEditableText, StateNone, ValueStateOrNull, kNotTextInsertion,
       kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kDeleteToMark, ExecuteDeleteToMark,
       SupportedFromMenuOrKeyBinding, EnabledInEditableText, StateNone,
       ValueStateOrNull, kNotTextInsertion, kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kDeleteWordBackward, ExecuteDeleteWordBackward,
       SupportedFromMenuOrKeyBinding, EnabledInEditableText, StateNone,
       ValueStateOrNull, kNotTextInsertion, kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kDeleteWordForward, ExecuteDeleteWordForward,
       SupportedFromMenuOrKeyBinding, EnabledInEditableText, StateNone,
       ValueStateOrNull, kNotTextInsertion, kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kFindString, ExecuteFindString, Supported,
       Enabled, StateNone, ValueStateOrNull, kNotTextInsertion,
       kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kFontName, ExecuteFontName, Supported,
       EnabledInRichlyEditableText, StateNone, ValueFontName, kNotTextInsertion,
       kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kFontSize, ExecuteFontSize, Supported,
       EnabledInRichlyEditableText, StateNone, ValueFontSize, kNotTextInsertion,
       kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kFontSizeDelta, ExecuteFontSizeDelta, Supported,
       EnabledInRichlyEditableText, StateNone, ValueFontSizeDelta,
       kNotTextInsertion, kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kForeColor, ExecuteForeColor, Supported,
       EnabledInRichlyEditableText, StateNone, ValueForeColor,
       kNotTextInsertion, kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kFormatBlock, ExecuteFormatBlock, Supported,
       EnabledInRichlyEditableText, StateNone, ValueFormatBlock,
       kNotTextInsertion, kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kForwardDelete, ExecuteForwardDelete, Supported,
       EnabledInEditableText, StateNone, ValueStateOrNull, kNotTextInsertion,
       kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kHiliteColor, ExecuteBackColor, Supported,
       EnabledInRichlyEditableText, StateNone, ValueStateOrNull,
       kNotTextInsertion, kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kIgnoreSpelling, ExecuteIgnoreSpelling,
       SupportedFromMenuOrKeyBinding, EnabledInEditableText, StateNone,
       ValueStateOrNull, kNotTextInsertion, kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kIndent, ExecuteIndent, Supported,
       EnabledInRichlyEditableText, StateNone, ValueStateOrNull,
       kNotTextInsertion, kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kInsertBacktab, ExecuteInsertBacktab,
       SupportedFromMenuOrKeyBinding, EnabledInEditableText, StateNone,
       ValueStateOrNull, kIsTextInsertion, kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kInsertHTML, ExecuteInsertHTML, Supported,
       EnabledInEditableText, StateNone, ValueStateOrNull, kNotTextInsertion,
       kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kInsertHorizontalRule,
       ExecuteInsertHorizontalRule, Supported, EnabledInRichlyEditableText,
       StateNone, ValueStateOrNull, kNotTextInsertion,
       kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kInsertImage, ExecuteInsertImage, Supported,
       EnabledInRichlyEditableText, StateNone, ValueStateOrNull,
       kNotTextInsertion, kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kInsertLineBreak, ExecuteInsertLineBreak,
       Supported, EnabledInEditableText, StateNone, ValueStateOrNull,
       kIsTextInsertion, kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kInsertNewline, ExecuteInsertNewline,
       SupportedFromMenuOrKeyBinding, EnabledInEditableText, StateNone,
       ValueStateOrNull, kIsTextInsertion, kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kInsertNewlineInQuotedContent,
       ExecuteInsertNewlineInQuotedContent, Supported,
       EnabledInRichlyEditableText, StateNone, ValueStateOrNull,
       kNotTextInsertion, kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kInsertOrderedList, ExecuteInsertOrderedList,
       Supported, EnabledInRichlyEditableText, StateOrderedList,
       ValueStateOrNull, kNotTextInsertion, kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kInsertParagraph, ExecuteInsertParagraph,
       Supported, EnabledInEditableText, StateNone, ValueStateOrNull,
       kNotTextInsertion, kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kInsertTab, ExecuteInsertTab,
       SupportedFromMenuOrKeyBinding, EnabledInEditableText, StateNone,
       ValueStateOrNull, kIsTextInsertion, kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kInsertText, ExecuteInsertText, Supported,
       EnabledInEditableText, StateNone, ValueStateOrNull, kIsTextInsertion,
       kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kInsertUnorderedList, ExecuteInsertUnorderedList,
       Supported, EnabledInRichlyEditableText, StateUnorderedList,
       ValueStateOrNull, kNotTextInsertion, kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kItalic, ExecuteToggleItalic, Supported,
       EnabledInRichlyEditableText, StateItalic, ValueStateOrNull,
       kNotTextInsertion, kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kJustifyCenter, ExecuteJustifyCenter, Supported,
       EnabledInRichlyEditableText, StateJustifyCenter, ValueStateOrNull,
       kNotTextInsertion, kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kJustifyFull, ExecuteJustifyFull, Supported,
       EnabledInRichlyEditableText, StateJustifyFull, ValueStateOrNull,
       kNotTextInsertion, kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kJustifyLeft, ExecuteJustifyLeft, Supported,
       EnabledInRichlyEditableText, StateJustifyLeft, ValueStateOrNull,
       kNotTextInsertion, kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kJustifyNone, ExecuteJustifyLeft, Supported,
       EnabledInRichlyEditableText, StateNone, ValueStateOrNull,
       kNotTextInsertion, kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kJustifyRight, ExecuteJustifyRight, Supported,
       EnabledInRichlyEditableText, StateJustifyRight, ValueStateOrNull,
       kNotTextInsertion, kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kMakeTextWritingDirectionLeftToRight,
       ExecuteMakeTextWritingDirectionLeftToRight,
       SupportedFromMenuOrKeyBinding, EnabledInRichlyEditableText,
       StateTextWritingDirectionLeftToRight, ValueStateOrNull,
       kNotTextInsertion, kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kMakeTextWritingDirectionNatural,
       ExecuteMakeTextWritingDirectionNatural, SupportedFromMenuOrKeyBinding,
       EnabledInRichlyEditableText, StateTextWritingDirectionNatural,
       ValueStateOrNull, kNotTextInsertion, kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kMakeTextWritingDirectionRightToLeft,
       ExecuteMakeTextWritingDirectionRightToLeft,
       SupportedFromMenuOrKeyBinding, EnabledInRichlyEditableText,
       StateTextWritingDirectionRightToLeft, ValueStateOrNull,
       kNotTextInsertion, kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kMoveBackward, ExecuteMoveBackward,
       SupportedFromMenuOrKeyBinding, EnabledInEditableText, StateNone,
       ValueStateOrNull, kNotTextInsertion, kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kMoveBackwardAndModifySelection,
       ExecuteMoveBackwardAndModifySelection, SupportedFromMenuOrKeyBinding,
       EnabledVisibleSelection, StateNone, ValueStateOrNull, kNotTextInsertion,
       kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kMoveDown, ExecuteMoveDown,
       SupportedFromMenuOrKeyBinding, EnabledInEditableText, StateNone,
       ValueStateOrNull, kNotTextInsertion, kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kMoveDownAndModifySelection,
       ExecuteMoveDownAndModifySelection, SupportedFromMenuOrKeyBinding,
       EnabledVisibleSelection, StateNone, ValueStateOrNull, kNotTextInsertion,
       kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kMoveForward, ExecuteMoveForward,
       SupportedFromMenuOrKeyBinding, EnabledInEditableText, StateNone,
       ValueStateOrNull, kNotTextInsertion, kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kMoveForwardAndModifySelection,
       ExecuteMoveForwardAndModifySelection, SupportedFromMenuOrKeyBinding,
       EnabledVisibleSelection, StateNone, ValueStateOrNull, kNotTextInsertion,
       kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kMoveLeft, ExecuteMoveLeft,
       SupportedFromMenuOrKeyBinding, EnabledInEditableText, StateNone,
       ValueStateOrNull, kNotTextInsertion, kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kMoveLeftAndModifySelection,
       ExecuteMoveLeftAndModifySelection, SupportedFromMenuOrKeyBinding,
       EnabledVisibleSelection, StateNone, ValueStateOrNull, kNotTextInsertion,
       kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kMovePageDown, ExecuteMovePageDown,
       SupportedFromMenuOrKeyBinding, EnabledInEditableText, StateNone,
       ValueStateOrNull, kNotTextInsertion, kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kMovePageDownAndModifySelection,
       ExecuteMovePageDownAndModifySelection, SupportedFromMenuOrKeyBinding,
       EnabledVisibleSelection, StateNone, ValueStateOrNull, kNotTextInsertion,
       kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kMovePageUp, ExecuteMovePageUp,
       SupportedFromMenuOrKeyBinding, EnabledInEditableText, StateNone,
       ValueStateOrNull, kNotTextInsertion, kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kMovePageUpAndModifySelection,
       ExecuteMovePageUpAndModifySelection, SupportedFromMenuOrKeyBinding,
       EnabledVisibleSelection, StateNone, ValueStateOrNull, kNotTextInsertion,
       kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kMoveParagraphBackward,
       ExecuteMoveParagraphBackward, SupportedFromMenuOrKeyBinding,
       EnabledInEditableText, StateNone, ValueStateOrNull, kNotTextInsertion,
       kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kMoveParagraphBackwardAndModifySelection,
       ExecuteMoveParagraphBackwardAndModifySelection,
       SupportedFromMenuOrKeyBinding, EnabledVisibleSelection, StateNone,
       ValueStateOrNull, kNotTextInsertion, kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kMoveParagraphForward,
       ExecuteMoveParagraphForward, SupportedFromMenuOrKeyBinding,
       EnabledInEditableText, StateNone, ValueStateOrNull, kNotTextInsertion,
       kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kMoveParagraphForwardAndModifySelection,
       ExecuteMoveParagraphForwardAndModifySelection,
       SupportedFromMenuOrKeyBinding, EnabledVisibleSelection, StateNone,
       ValueStateOrNull, kNotTextInsertion, kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kMoveRight, ExecuteMoveRight,
       SupportedFromMenuOrKeyBinding, EnabledInEditableText, StateNone,
       ValueStateOrNull, kNotTextInsertion, kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kMoveRightAndModifySelection,
       ExecuteMoveRightAndModifySelection, SupportedFromMenuOrKeyBinding,
       EnabledVisibleSelection, StateNone, ValueStateOrNull, kNotTextInsertion,
       kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kMoveToBeginningOfDocument,
       ExecuteMoveToBeginningOfDocument, SupportedFromMenuOrKeyBinding,
       EnabledInEditableText, StateNone, ValueStateOrNull, kNotTextInsertion,
       kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kMoveToBeginningOfDocumentAndModifySelection,
       ExecuteMoveToBeginningOfDocumentAndModifySelection,
       SupportedFromMenuOrKeyBinding, EnabledVisibleSelection, StateNone,
       ValueStateOrNull, kNotTextInsertion, kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kMoveToBeginningOfLine,
       ExecuteMoveToBeginningOfLine, SupportedFromMenuOrKeyBinding,
       EnabledInEditableText, StateNone, ValueStateOrNull, kNotTextInsertion,
       kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kMoveToBeginningOfLineAndModifySelection,
       ExecuteMoveToBeginningOfLineAndModifySelection,
       SupportedFromMenuOrKeyBinding, EnabledVisibleSelection, StateNone,
       ValueStateOrNull, kNotTextInsertion, kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kMoveToBeginningOfParagraph,
       ExecuteMoveToBeginningOfParagraph, SupportedFromMenuOrKeyBinding,
       EnabledInEditableText, StateNone, ValueStateOrNull, kNotTextInsertion,
       kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kMoveToBeginningOfParagraphAndModifySelection,
       ExecuteMoveToBeginningOfParagraphAndModifySelection,
       SupportedFromMenuOrKeyBinding, EnabledVisibleSelection, StateNone,
       ValueStateOrNull, kNotTextInsertion, kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kMoveToBeginningOfSentence,
       ExecuteMoveToBeginningOfSentence, SupportedFromMenuOrKeyBinding,
       EnabledInEditableText, StateNone, ValueStateOrNull, kNotTextInsertion,
       kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kMoveToBeginningOfSentenceAndModifySelection,
       ExecuteMoveToBeginningOfSentenceAndModifySelection,
       SupportedFromMenuOrKeyBinding, EnabledVisibleSelection, StateNone,
       ValueStateOrNull, kNotTextInsertion, kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kMoveToEndOfDocument, ExecuteMoveToEndOfDocument,
       SupportedFromMenuOrKeyBinding, EnabledInEditableText, StateNone,
       ValueStateOrNull, kNotTextInsertion, kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kMoveToEndOfDocumentAndModifySelection,
       ExecuteMoveToEndOfDocumentAndModifySelection,
       SupportedFromMenuOrKeyBinding, EnabledVisibleSelection, StateNone,
       ValueStateOrNull, kNotTextInsertion, kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kMoveToEndOfLine, ExecuteMoveToEndOfLine,
       SupportedFromMenuOrKeyBinding, EnabledInEditableText, StateNone,
       ValueStateOrNull, kNotTextInsertion, kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kMoveToEndOfLineAndModifySelection,
       ExecuteMoveToEndOfLineAndModifySelection, SupportedFromMenuOrKeyBinding,
       EnabledVisibleSelection, StateNone, ValueStateOrNull, kNotTextInsertion,
       kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kMoveToEndOfParagraph,
       ExecuteMoveToEndOfParagraph, SupportedFromMenuOrKeyBinding,
       EnabledInEditableText, StateNone, ValueStateOrNull, kNotTextInsertion,
       kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kMoveToEndOfParagraphAndModifySelection,
       ExecuteMoveToEndOfParagraphAndModifySelection,
       SupportedFromMenuOrKeyBinding, EnabledVisibleSelection, StateNone,
       ValueStateOrNull, kNotTextInsertion, kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kMoveToEndOfSentence, ExecuteMoveToEndOfSentence,
       SupportedFromMenuOrKeyBinding, EnabledInEditableText, StateNone,
       ValueStateOrNull, kNotTextInsertion, kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kMoveToEndOfSentenceAndModifySelection,
       ExecuteMoveToEndOfSentenceAndModifySelection,
       SupportedFromMenuOrKeyBinding, EnabledVisibleSelection, StateNone,
       ValueStateOrNull, kNotTextInsertion, kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kMoveToLeftEndOfLine, ExecuteMoveToLeftEndOfLine,
       SupportedFromMenuOrKeyBinding, EnabledInEditableText, StateNone,
       ValueStateOrNull, kNotTextInsertion, kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kMoveToLeftEndOfLineAndModifySelection,
       ExecuteMoveToLeftEndOfLineAndModifySelection,
       SupportedFromMenuOrKeyBinding, EnabledInEditableText, StateNone,
       ValueStateOrNull, kNotTextInsertion, kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kMoveToRightEndOfLine,
       ExecuteMoveToRightEndOfLine, SupportedFromMenuOrKeyBinding,
       EnabledInEditableText, StateNone, ValueStateOrNull, kNotTextInsertion,
       kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kMoveToRightEndOfLineAndModifySelection,
       ExecuteMoveToRightEndOfLineAndModifySelection,
       SupportedFromMenuOrKeyBinding, EnabledInEditableText, StateNone,
       ValueStateOrNull, kNotTextInsertion, kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kMoveUp, ExecuteMoveUp,
       SupportedFromMenuOrKeyBinding, EnabledInEditableText, StateNone,
       ValueStateOrNull, kNotTextInsertion, kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kMoveUpAndModifySelection,
       ExecuteMoveUpAndModifySelection, SupportedFromMenuOrKeyBinding,
       EnabledVisibleSelection, StateNone, ValueStateOrNull, kNotTextInsertion,
       kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kMoveWordBackward, ExecuteMoveWordBackward,
       SupportedFromMenuOrKeyBinding, EnabledInEditableText, StateNone,
       ValueStateOrNull, kNotTextInsertion, kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kMoveWordBackwardAndModifySelection,
       ExecuteMoveWordBackwardAndModifySelection, SupportedFromMenuOrKeyBinding,
       EnabledVisibleSelection, StateNone, ValueStateOrNull, kNotTextInsertion,
       kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kMoveWordForward, ExecuteMoveWordForward,
       SupportedFromMenuOrKeyBinding, EnabledInEditableText, StateNone,
       ValueStateOrNull, kNotTextInsertion, kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kMoveWordForwardAndModifySelection,
       ExecuteMoveWordForwardAndModifySelection, SupportedFromMenuOrKeyBinding,
       EnabledVisibleSelection, StateNone, ValueStateOrNull, kNotTextInsertion,
       kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kMoveWordLeft, ExecuteMoveWordLeft,
       SupportedFromMenuOrKeyBinding, EnabledInEditableText, StateNone,
       ValueStateOrNull, kNotTextInsertion, kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kMoveWordLeftAndModifySelection,
       ExecuteMoveWordLeftAndModifySelection, SupportedFromMenuOrKeyBinding,
       EnabledVisibleSelection, StateNone, ValueStateOrNull, kNotTextInsertion,
       kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kMoveWordRight, ExecuteMoveWordRight,
       SupportedFromMenuOrKeyBinding, EnabledInEditableText, StateNone,
       ValueStateOrNull, kNotTextInsertion, kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kMoveWordRightAndModifySelection,
       ExecuteMoveWordRightAndModifySelection, SupportedFromMenuOrKeyBinding,
       EnabledVisibleSelection, StateNone, ValueStateOrNull, kNotTextInsertion,
       kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kOutdent, ExecuteOutdent, Supported,
       EnabledInRichlyEditableText, StateNone, ValueStateOrNull,
       kNotTextInsertion, kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kOverWrite, ExecuteToggleOverwrite,
       SupportedFromMenuOrKeyBinding, EnabledInRichlyEditableText, StateNone,
       ValueStateOrNull, kNotTextInsertion, kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kPaste, ExecutePaste, Supported, EnabledPaste,
       StateNone, ValueStateOrNull, kNotTextInsertion,
       kAllowExecutionWhenDisabled},
      {WebEditingCommandType::kPasteAndMatchStyle, ExecutePasteAndMatchStyle,
       Supported, EnabledPaste, StateNone, ValueStateOrNull, kNotTextInsertion,
       kAllowExecutionWhenDisabled},
      {WebEditingCommandType::kPasteGlobalSelection,
       ExecutePasteGlobalSelection, SupportedFromMenuOrKeyBinding, EnabledPaste,
       StateNone, ValueStateOrNull, kNotTextInsertion,
       kAllowExecutionWhenDisabled},
      {WebEditingCommandType::kPrint, ExecutePrint, Supported, Enabled,
       StateNone, ValueStateOrNull, kNotTextInsertion,
       kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kRedo, ExecuteRedo, Supported, EnabledRedo,
       StateNone, ValueStateOrNull, kNotTextInsertion,
       kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kRemoveFormat, ExecuteRemoveFormat, Supported,
       EnabledRangeInEditableText, StateNone, ValueStateOrNull,
       kNotTextInsertion, kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kScrollPageBackward, ExecuteScrollPageBackward,
       SupportedFromMenuOrKeyBinding, Enabled, StateNone, ValueStateOrNull,
       kNotTextInsertion, kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kScrollPageForward, ExecuteScrollPageForward,
       SupportedFromMenuOrKeyBinding, Enabled, StateNone, ValueStateOrNull,
       kNotTextInsertion, kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kScrollLineUp, ExecuteScrollLineUp,
       SupportedFromMenuOrKeyBinding, Enabled, StateNone, ValueStateOrNull,
       kNotTextInsertion, kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kScrollLineDown, ExecuteScrollLineDown,
       SupportedFromMenuOrKeyBinding, Enabled, StateNone, ValueStateOrNull,
       kNotTextInsertion, kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kScrollToBeginningOfDocument,
       ExecuteScrollToBeginningOfDocument, SupportedFromMenuOrKeyBinding,
       Enabled, StateNone, ValueStateOrNull, kNotTextInsertion,
       kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kScrollToEndOfDocument,
       ExecuteScrollToEndOfDocument, SupportedFromMenuOrKeyBinding, Enabled,
       StateNone, ValueStateOrNull, kNotTextInsertion,
       kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kSelectAll, ExecuteSelectAll, Supported,
       EnabledSelectAll, StateNone, ValueStateOrNull, kNotTextInsertion,
       kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kSelectLine, ExecuteSelectLine,
       SupportedFromMenuOrKeyBinding, EnabledVisibleSelection, StateNone,
       ValueStateOrNull, kNotTextInsertion, kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kSelectParagraph, ExecuteSelectParagraph,
       SupportedFromMenuOrKeyBinding, EnabledVisibleSelection, StateNone,
       ValueStateOrNull, kNotTextInsertion, kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kSelectSentence, ExecuteSelectSentence,
       SupportedFromMenuOrKeyBinding, EnabledVisibleSelection, StateNone,
       ValueStateOrNull, kNotTextInsertion, kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kSelectToMark, ExecuteSelectToMark,
       SupportedFromMenuOrKeyBinding, EnabledVisibleSelectionAndMark, StateNone,
       ValueStateOrNull, kNotTextInsertion, kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kSelectWord, ExecuteSelectWord,
       SupportedFromMenuOrKeyBinding, EnabledVisibleSelection, StateNone,
       ValueStateOrNull, kNotTextInsertion, kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kSetMark, ExecuteSetMark,
       SupportedFromMenuOrKeyBinding, EnabledVisibleSelection, StateNone,
       ValueStateOrNull, kNotTextInsertion, kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kStrikethrough, ExecuteStrikethrough, Supported,
       EnabledInRichlyEditableText, StateStrikethrough, ValueStateOrNull,
       kNotTextInsertion, kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kStyleWithCSS, ExecuteStyleWithCSS, Supported,
       Enabled, StateStyleWithCSS, ValueEmpty, kNotTextInsertion,
       kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kSubscript, ExecuteSubscript, Supported,
       EnabledInRichlyEditableText, StateSubscript, ValueStateOrNull,
       kNotTextInsertion, kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kSuperscript, ExecuteSuperscript, Supported,
       EnabledInRichlyEditableText, StateSuperscript, ValueStateOrNull,
       kNotTextInsertion, kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kSwapWithMark, ExecuteSwapWithMark,
       SupportedFromMenuOrKeyBinding, EnabledVisibleSelectionAndMark, StateNone,
       ValueStateOrNull, kNotTextInsertion, kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kToggleBold, ExecuteToggleBold,
       SupportedFromMenuOrKeyBinding, EnabledInRichlyEditableText, StateBold,
       ValueStateOrNull, kNotTextInsertion, kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kToggleItalic, ExecuteToggleItalic,
       SupportedFromMenuOrKeyBinding, EnabledInRichlyEditableText, StateItalic,
       ValueStateOrNull, kNotTextInsertion, kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kToggleUnderline, ExecuteUnderline,
       SupportedFromMenuOrKeyBinding, EnabledInRichlyEditableText,
       StateUnderline, ValueStateOrNull, kNotTextInsertion,
       kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kTranspose, ExecuteTranspose, Supported,
       EnableCaretInEditableText, StateNone, ValueStateOrNull,
       kNotTextInsertion, kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kUnderline, ExecuteUnderline, Supported,
       EnabledInRichlyEditableText, StateUnderline, ValueStateOrNull,
       kNotTextInsertion, kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kUndo, ExecuteUndo, Supported, EnabledUndo,
       StateNone, ValueStateOrNull, kNotTextInsertion,
       kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kUnlink, ExecuteUnlink, Supported,
       EnabledRangeInRichlyEditableText, StateNone, ValueStateOrNull,
       kNotTextInsertion, kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kUnscript, ExecuteUnscript,
       SupportedFromMenuOrKeyBinding, EnabledInRichlyEditableText, StateNone,
       ValueStateOrNull, kNotTextInsertion, kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kUnselect, ExecuteUnselect, Supported,
       EnabledUnselect, StateNone, ValueStateOrNull, kNotTextInsertion,
       kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kUseCSS, ExecuteUseCSS, Supported, Enabled,
       StateNone, ValueStateOrNull, kNotTextInsertion,
       kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kYank, ExecuteYank, SupportedFromMenuOrKeyBinding,
       EnabledInEditableText, StateNone, ValueStateOrNull, kNotTextInsertion,
       kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kYankAndSelect, ExecuteYankAndSelect,
       SupportedFromMenuOrKeyBinding, EnabledInEditableText, StateNone,
       ValueStateOrNull, kNotTextInsertion, kDoNotAllowExecutionWhenDisabled},
      {WebEditingCommandType::kAlignCenter, ExecuteJustifyCenter,
       SupportedFromMenuOrKeyBinding, EnabledInRichlyEditableText, StateNone,
       ValueStateOrNull, kNotTextInsertion, kDoNotAllowExecutionWhenDisabled},
  };
  // Handles all commands except WebEditingCommandType::Invalid.
  static_assert(
      arraysize(kEditorCommands) + 1 ==
          static_cast<size_t>(WebEditingCommandType::kNumberOfCommandTypes),
      "must handle all valid WebEditingCommandType");

  WebEditingCommandType command_type =
      WebEditingCommandTypeFromCommandName(command_name);
  if (command_type == WebEditingCommandType::kInvalid)
    return nullptr;

  int command_index = static_cast<int>(command_type) - 1;
  DCHECK(command_index >= 0 &&
         command_index < static_cast<int>(arraysize(kEditorCommands)));
  return &kEditorCommands[command_index];
}

Editor::Command Editor::CreateCommand(const String& command_name) {
  return Command(InternalCommand(command_name), kCommandFromMenuOrKeyBinding,
                 frame_);
}

Editor::Command Editor::CreateCommand(const String& command_name,
                                      EditorCommandSource source) {
  return Command(InternalCommand(command_name), source, frame_);
}

bool Editor::ExecuteCommand(const String& command_name) {
  // Specially handling commands that Editor::execCommand does not directly
  // support.
  if (command_name == "DeleteToEndOfParagraph") {
    if (!DeleteWithDirection(DeleteDirection::kForward, kParagraphBoundary,
                             true, false))
      DeleteWithDirection(DeleteDirection::kForward, kCharacterGranularity,
                          true, false);
    return true;
  }
  if (command_name == "DeleteBackward")
    return CreateCommand(AtomicString("BackwardDelete")).Execute();
  if (command_name == "DeleteForward")
    return CreateCommand(AtomicString("ForwardDelete")).Execute();
  if (command_name == "AdvanceToNextMisspelling") {
    // TODO(editing-dev): Use of updateStyleAndLayoutIgnorePendingStylesheets
    // needs to be audited. see http://crbug.com/590369 for more details.
    GetFrame().GetDocument()->UpdateStyleAndLayoutIgnorePendingStylesheets();

    // We need to pass false here or else the currently selected word will never
    // be skipped.
    GetSpellChecker().AdvanceToNextMisspelling(false);
    return true;
  }
  if (command_name == "ToggleSpellPanel") {
    // TODO(editing-dev): Use of updateStyleAndLayoutIgnorePendingStylesheets
    // needs to be audited.
    // see http://crbug.com/590369 for more details.
    GetFrame().GetDocument()->UpdateStyleAndLayoutIgnorePendingStylesheets();

    GetSpellChecker().ShowSpellingGuessPanel();
    return true;
  }
  return CreateCommand(command_name).Execute();
}

bool Editor::ExecuteCommand(const String& command_name, const String& value) {
  // moveToBeginningOfDocument and moveToEndfDocument are only handled by WebKit
  // for editable nodes.
  if (!CanEdit() && command_name == "moveToBeginningOfDocument")
    return GetFrame().GetEventHandler().BubblingScroll(
        kScrollUpIgnoringWritingMode, kScrollByDocument);

  if (!CanEdit() && command_name == "moveToEndOfDocument")
    return GetFrame().GetEventHandler().BubblingScroll(
        kScrollDownIgnoringWritingMode, kScrollByDocument);

  if (command_name == "showGuessPanel") {
    // TODO(editing-dev): Use of updateStyleAndLayoutIgnorePendingStylesheets
    // needs to be audited. see http://crbug.com/590369 for more details.
    GetFrame().GetDocument()->UpdateStyleAndLayoutIgnorePendingStylesheets();

    GetSpellChecker().ShowSpellingGuessPanel();
    return true;
  }

  return CreateCommand(command_name).Execute(value);
}

Editor::Command::Command() : command_(0) {}

Editor::Command::Command(const EditorInternalCommand* command,
                         EditorCommandSource source,
                         LocalFrame* frame)
    : command_(command), source_(source), frame_(command ? frame : nullptr) {
  // Use separate assertions so we can tell which bad thing happened.
  if (!command)
    DCHECK(!frame_);
  else
    DCHECK(frame_);
}

bool Editor::Command::Execute(const String& parameter,
                              Event* triggering_event) const {
  // TODO(yosin) We should move this logic into |canExecute()| member function
  // in |EditorInternalCommand| to replace |allowExecutionWhenDisabled|.
  // |allowExecutionWhenDisabled| is for "Copy", "Cut" and "Paste" commands
  // only.
  if (!IsEnabled(triggering_event)) {
    // Let certain commands be executed when performed explicitly even if they
    // are disabled.
    if (!IsSupported() || !frame_ || !command_->allow_execution_when_disabled)
      return false;
  }

  if (source_ == kCommandFromMenuOrKeyBinding) {
    InputEvent::InputType input_type =
        InputTypeFromCommandType(command_->command_type, *frame_);
    if (input_type != InputEvent::InputType::kNone) {
      if (DispatchBeforeInputEditorCommand(
              EventTargetNodeForDocument(frame_->GetDocument()), input_type,
              GetTargetRanges()) != DispatchEventResult::kNotCanceled)
        return true;
    }
  }

  // 'beforeinput' event handler may destroy target frame.
  if (frame_->GetDocument()->GetFrame() != frame_)
    return false;

  GetFrame().GetDocument()->UpdateStyleAndLayoutIgnorePendingStylesheets();
  DEFINE_STATIC_LOCAL(SparseHistogram, command_histogram,
                      ("WebCore.Editing.Commands"));
  command_histogram.Sample(static_cast<int>(command_->command_type));
  return command_->execute(*frame_, triggering_event, source_, parameter);
}

bool Editor::Command::Execute(Event* triggering_event) const {
  return Execute(String(), triggering_event);
}

bool Editor::Command::IsSupported() const {
  if (!command_)
    return false;
  switch (source_) {
    case kCommandFromMenuOrKeyBinding:
      return true;
    case kCommandFromDOM:
      return command_->is_supported_from_dom(frame_.Get());
  }
  NOTREACHED();
  return false;
}

bool Editor::Command::IsEnabled(Event* triggering_event) const {
  if (!IsSupported() || !frame_)
    return false;
  return command_->is_enabled(*frame_, triggering_event, source_);
}

TriState Editor::Command::GetState(Event* triggering_event) const {
  if (!IsSupported() || !frame_)
    return kFalseTriState;
  return command_->state(*frame_, triggering_event);
}

String Editor::Command::Value(Event* triggering_event) const {
  if (!IsSupported() || !frame_)
    return String();
  return command_->value(*command_, *frame_, triggering_event);
}

bool Editor::Command::IsTextInsertion() const {
  return command_ && command_->is_text_insertion;
}

int Editor::Command::IdForHistogram() const {
  return IsSupported() ? static_cast<int>(command_->command_type) : 0;
}

const StaticRangeVector* Editor::Command::GetTargetRanges() const {
  const Node* target = EventTargetNodeForDocument(frame_->GetDocument());
  if (!IsSupported() || !frame_ || !target || !HasRichlyEditableStyle(*target))
    return nullptr;

  switch (command_->command_type) {
    case WebEditingCommandType::kDelete:
    case WebEditingCommandType::kDeleteBackward:
      return RangesFromCurrentSelectionOrExtendCaret(
          *frame_, kDirectionBackward, kCharacterGranularity);
    case WebEditingCommandType::kDeleteForward:
      return RangesFromCurrentSelectionOrExtendCaret(*frame_, kDirectionForward,
                                                     kCharacterGranularity);
    case WebEditingCommandType::kDeleteToBeginningOfLine:
      return RangesFromCurrentSelectionOrExtendCaret(
          *frame_, kDirectionBackward, kLineGranularity);
    case WebEditingCommandType::kDeleteToBeginningOfParagraph:
      return RangesFromCurrentSelectionOrExtendCaret(
          *frame_, kDirectionBackward, kParagraphGranularity);
    case WebEditingCommandType::kDeleteToEndOfLine:
      return RangesFromCurrentSelectionOrExtendCaret(*frame_, kDirectionForward,
                                                     kLineGranularity);
    case WebEditingCommandType::kDeleteToEndOfParagraph:
      return RangesFromCurrentSelectionOrExtendCaret(*frame_, kDirectionForward,
                                                     kParagraphGranularity);
    case WebEditingCommandType::kDeleteWordBackward:
      return RangesFromCurrentSelectionOrExtendCaret(
          *frame_, kDirectionBackward, kWordGranularity);
    case WebEditingCommandType::kDeleteWordForward:
      return RangesFromCurrentSelectionOrExtendCaret(*frame_, kDirectionForward,
                                                     kWordGranularity);
    default:
      return TargetRangesForInputEvent(*target);
  }
}

}  // namespace blink
