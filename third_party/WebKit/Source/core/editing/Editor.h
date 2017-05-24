/*
 * Copyright (C) 2006, 2007, 2008 Apple Inc. All rights reserved.
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

#ifndef Editor_h
#define Editor_h

#include "core/CoreExport.h"
#include "core/clipboard/DataTransferAccessPolicy.h"
#include "core/editing/EditingBehavior.h"
#include "core/editing/EditingStyle.h"
#include "core/editing/EphemeralRange.h"
#include "core/editing/FindOptions.h"
#include "core/editing/FrameSelection.h"
#include "core/editing/VisibleSelection.h"
#include "core/editing/WritingDirection.h"
#include "core/editing/iterators/TextIterator.h"
#include "core/editing/markers/DocumentMarker.h"
#include "core/events/InputEvent.h"
#include "platform/PasteMode.h"
#include "platform/heap/Handle.h"
#include <memory>

namespace blink {

class CompositeEditCommand;
class DragData;
class EditorClient;
class EditorInternalCommand;
class LocalFrame;
class HitTestResult;
class KillRing;
class Pasteboard;
class SpellChecker;
class StylePropertySet;
class TextEvent;
class TypingCommand;
class UndoStack;
class UndoStep;

enum class DeleteDirection;
enum class DeleteMode { kSimple, kSmart };
enum class InsertMode { kSimple, kSmart };
enum class DragSourceType { kHTMLSource, kPlainTextSource };

enum EditorCommandSource { kCommandFromMenuOrKeyBinding, kCommandFromDOM };
enum EditorParagraphSeparator {
  kEditorParagraphSeparatorIsDiv,
  kEditorParagraphSeparatorIsP
};

class CORE_EXPORT Editor final : public GarbageCollectedFinalized<Editor> {
  WTF_MAKE_NONCOPYABLE(Editor);

 public:
  static Editor* Create(LocalFrame&);
  ~Editor();

  EditorClient& Client() const;

  CompositeEditCommand* LastEditCommand() { return last_edit_command_.Get(); }
  TypingCommand* LastTypingCommandIfStillOpenForTyping() const;

  void HandleKeyboardEvent(KeyboardEvent*);
  bool HandleTextEvent(TextEvent*);

  bool CanEdit() const;
  bool CanEditRichly() const;

  bool CanDHTMLCut();
  bool CanDHTMLCopy();

  bool CanCut() const;
  bool CanCopy() const;
  bool CanPaste() const;
  bool CanDelete() const;
  bool CanSmartCopyOrDelete() const;

  void Cut(EditorCommandSource);
  void Copy(EditorCommandSource);
  void Paste(EditorCommandSource);
  void PasteAsPlainText(EditorCommandSource);
  void PerformDelete();

  static void CountEvent(ExecutionContext*, const Event*);
  void CopyImage(const HitTestResult&);

  void Transpose();

  void RespondToChangedContents(const Position&);

  bool SelectionStartHasStyle(CSSPropertyID, const String& value) const;
  TriState SelectionHasStyle(CSSPropertyID, const String& value) const;
  String SelectionStartCSSPropertyValue(CSSPropertyID);

  void RemoveFormattingAndStyle();

  void RegisterCommandGroup(CompositeEditCommand* command_group_wrapper);

  bool DeleteWithDirection(DeleteDirection,
                           TextGranularity,
                           bool kill_ring,
                           bool is_typing_action);
  void DeleteSelectionWithSmartDelete(
      DeleteMode,
      InputEvent::InputType,
      const Position& reference_move_position = Position());

  void ApplyStyle(StylePropertySet*, InputEvent::InputType);
  void ApplyParagraphStyle(StylePropertySet*, InputEvent::InputType);
  void ApplyStyleToSelection(StylePropertySet*, InputEvent::InputType);
  void ApplyParagraphStyleToSelection(StylePropertySet*, InputEvent::InputType);

  void AppliedEditing(CompositeEditCommand*);
  void UnappliedEditing(UndoStep*);
  void ReappliedEditing(UndoStep*);

  void SetShouldStyleWithCSS(bool flag) { should_style_with_css_ = flag; }
  bool ShouldStyleWithCSS() const { return should_style_with_css_; }

  class CORE_EXPORT Command {
    STACK_ALLOCATED();

   public:
    Command();
    Command(const EditorInternalCommand*, EditorCommandSource, LocalFrame*);

    bool Execute(const String& parameter = String(),
                 Event* triggering_event = nullptr) const;
    bool Execute(Event* triggering_event) const;

    bool IsSupported() const;
    bool IsEnabled(Event* triggering_event = nullptr) const;

    TriState GetState(Event* triggering_event = nullptr) const;
    String Value(Event* triggering_event = nullptr) const;

    bool IsTextInsertion() const;

    // Returns 0 if this Command is not supported.
    int IdForHistogram() const;

   private:
    LocalFrame& GetFrame() const {
      DCHECK(frame_);
      return *frame_;
    }

    // Returns target ranges for the command, currently only supports delete
    // related commands. Used by InputEvent.
    const StaticRangeVector* GetTargetRanges() const;

    const EditorInternalCommand* command_;
    EditorCommandSource source_;
    Member<LocalFrame> frame_;
  };
  Command CreateCommand(
      const String&
          command_name);  // Command source is CommandFromMenuOrKeyBinding.
  Command CreateCommand(const String& command_name, EditorCommandSource);

  // |Editor::executeCommand| is implementation of |WebFrame::executeCommand|
  // rather than |Document::execCommand|.
  bool ExecuteCommand(const String&);
  bool ExecuteCommand(const String& command_name, const String& value);

  bool InsertText(const String&, KeyboardEvent* triggering_event);
  bool InsertTextWithoutSendingTextEvent(
      const String&,
      bool select_inserted_text,
      TextEvent* triggering_event,
      InputEvent::InputType = InputEvent::InputType::kInsertText);
  bool InsertLineBreak();
  bool InsertParagraphSeparator();

  bool IsOverwriteModeEnabled() const { return overwrite_mode_enabled_; }
  void ToggleOverwriteModeEnabled();

  bool CanUndo();
  void Undo();
  bool CanRedo();
  void Redo();

  // Exposed for IdleSpellCheckCallback only.
  // Supposed to be used as |const UndoStack&|.
  UndoStack& GetUndoStack() const { return *undo_stack_; }

  void SetBaseWritingDirection(WritingDirection);

  // smartInsertDeleteEnabled and selectTrailingWhitespaceEnabled are
  // mutually exclusive, meaning that enabling one will disable the other.
  bool SmartInsertDeleteEnabled() const;
  bool IsSelectTrailingWhitespaceEnabled() const;

  bool PreventRevealSelection() const { return prevent_reveal_selection_; }

  void SetStartNewKillRingSequence(bool);

  void Clear();

  VisibleSelection SelectionForCommand(Event*);

  KillRing& GetKillRing() const { return *kill_ring_; }

  EditingBehavior Behavior() const;

  EphemeralRange SelectedRange();

  void AddToKillRing(const EphemeralRange&);

  void PasteAsFragment(DocumentFragment*, bool smart_replace, bool match_style);
  void PasteAsPlainText(const String&, bool smart_replace);

  Element* FindEventTargetFrom(const VisibleSelection&) const;
  Element* FindEventTargetFromSelection() const;

  bool FindString(const String&, FindOptions);

  Range* FindStringAndScrollToVisible(const String&, Range*, FindOptions);
  Range* FindRangeOfString(const String& target,
                           const EphemeralRange& reference_range,
                           FindOptions);
  Range* FindRangeOfString(const String& target,
                           const EphemeralRangeInFlatTree& reference_range,
                           FindOptions);

  const VisibleSelection& Mark() const;  // Mark, to be used as emacs uses it.
  void SetMark(const VisibleSelection&);

  void ComputeAndSetTypingStyle(StylePropertySet*, InputEvent::InputType);

  // |firstRectForRange| requires up-to-date layout.
  IntRect FirstRectForRange(const EphemeralRange&) const;

  void RespondToChangedSelection(const Position& old_selection_start,
                                 FrameSelection::SetSelectionOptions);

  bool MarkedTextMatchesAreHighlighted() const;
  void SetMarkedTextMatchesAreHighlighted(bool);

  void ReplaceSelectionWithFragment(DocumentFragment*,
                                    bool select_replacement,
                                    bool smart_replace,
                                    bool match_style,
                                    InputEvent::InputType);
  void ReplaceSelectionWithText(const String&,
                                bool select_replacement,
                                bool smart_replace,
                                InputEvent::InputType);

  // Implementation of WebLocalFrameImpl::replaceSelection.
  void ReplaceSelection(const String&);

  void ReplaceSelectionAfterDragging(DocumentFragment*,
                                     InsertMode,
                                     DragSourceType);

  // Return false if frame was destroyed by event handler, should stop executing
  // remaining actions.
  bool DeleteSelectionAfterDraggingWithEvents(
      Element* drag_source,
      DeleteMode,
      const Position& reference_move_position);
  bool ReplaceSelectionAfterDraggingWithEvents(Element* drop_target,
                                               DragData*,
                                               DocumentFragment*,
                                               Range* drop_caret_range,
                                               InsertMode,
                                               DragSourceType);

  EditorParagraphSeparator DefaultParagraphSeparator() const {
    return default_paragraph_separator_;
  }
  void SetDefaultParagraphSeparator(EditorParagraphSeparator separator) {
    default_paragraph_separator_ = separator;
  }

  static void TidyUpHTMLStructure(Document&);

  class RevealSelectionScope {
    WTF_MAKE_NONCOPYABLE(RevealSelectionScope);
    DISALLOW_NEW_EXCEPT_PLACEMENT_NEW();

   public:
    explicit RevealSelectionScope(Editor*);
    ~RevealSelectionScope();

    DECLARE_TRACE();

   private:
    Member<Editor> editor_;
  };
  friend class RevealSelectionScope;

  EditingStyle* TypingStyle() const;
  void SetTypingStyle(EditingStyle*);
  void ClearTypingStyle();

  DECLARE_TRACE();

 private:
  Member<LocalFrame> frame_;
  Member<CompositeEditCommand> last_edit_command_;
  const Member<UndoStack> undo_stack_;
  int prevent_reveal_selection_;
  bool should_start_new_kill_ring_sequence_;
  bool should_style_with_css_;
  const std::unique_ptr<KillRing> kill_ring_;
  VisibleSelection mark_;
  bool are_marked_text_matches_highlighted_;
  EditorParagraphSeparator default_paragraph_separator_;
  bool overwrite_mode_enabled_;
  Member<EditingStyle> typing_style_;

  explicit Editor(LocalFrame&);

  LocalFrame& GetFrame() const {
    DCHECK(frame_);
    return *frame_;
  }

  bool CanDeleteRange(const EphemeralRange&) const;

  bool TryDHTMLCopy();
  bool TryDHTMLCut();
  bool TryDHTMLPaste(PasteMode);

  bool CanSmartReplaceWithPasteboard(Pasteboard*);
  void PasteAsPlainTextWithPasteboard(Pasteboard*);
  void PasteWithPasteboard(Pasteboard*);
  void WriteSelectionToPasteboard();
  bool DispatchCPPEvent(const AtomicString&,
                        DataTransferAccessPolicy,
                        PasteMode = kAllMimeTypes);

  void RevealSelectionAfterEditingOperation(
      const ScrollAlignment& = ScrollAlignment::kAlignCenterIfNeeded,
      RevealExtentOption = kDoNotRevealExtent);
  void ChangeSelectionAfterCommand(const SelectionInDOMTree&,
                                   FrameSelection::SetSelectionOptions);

  SpellChecker& GetSpellChecker() const;

  bool HandleEditingKeyboardEvent(KeyboardEvent*);
};

inline void Editor::SetStartNewKillRingSequence(bool flag) {
  should_start_new_kill_ring_sequence_ = flag;
}

inline const VisibleSelection& Editor::Mark() const {
  return mark_;
}

inline void Editor::SetMark(const VisibleSelection& selection) {
  mark_ = selection;
}

inline bool Editor::MarkedTextMatchesAreHighlighted() const {
  return are_marked_text_matches_highlighted_;
}

inline EditingStyle* Editor::TypingStyle() const {
  return typing_style_.Get();
}

inline void Editor::ClearTypingStyle() {
  typing_style_.Clear();
}

inline void Editor::SetTypingStyle(EditingStyle* style) {
  typing_style_ = style;
}

}  // namespace blink

#endif  // Editor_h
