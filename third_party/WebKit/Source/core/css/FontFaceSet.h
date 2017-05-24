/*
 * Copyright (C) 2013 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 */

#ifndef FontFaceSet_h
#define FontFaceSet_h

#include "bindings/core/v8/Iterable.h"
#include "bindings/core/v8/ScriptPromise.h"
#include "core/css/FontFace.h"
#include "core/dom/Document.h"
#include "core/dom/SuspendableObject.h"
#include "core/events/EventListener.h"
#include "core/events/EventTarget.h"
#include "platform/AsyncMethodRunner.h"
#include "platform/heap/Handle.h"
#include "platform/wtf/Allocator.h"
#include "platform/wtf/Vector.h"

// Mac OS X 10.6 SDK defines check() macro that interferes with our check()
// method
#ifdef check
#undef check
#endif

namespace blink {

class CSSFontSelector;
class ExceptionState;
class Font;
class FontFaceCache;
class ExecutionContext;

using FontFaceSetIterable = SetlikeIterable<Member<FontFace>>;

class CORE_EXPORT FontFaceSet final : public EventTargetWithInlineData,
                                      public Supplement<Document>,
                                      public SuspendableObject,
                                      public FontFaceSetIterable,
                                      public FontFace::LoadFontCallback {
  USING_GARBAGE_COLLECTED_MIXIN(FontFaceSet);
  DEFINE_WRAPPERTYPEINFO();
  WTF_MAKE_NONCOPYABLE(FontFaceSet);

 public:
  ~FontFaceSet() override;

  DEFINE_ATTRIBUTE_EVENT_LISTENER(loading);
  DEFINE_ATTRIBUTE_EVENT_LISTENER(loadingdone);
  DEFINE_ATTRIBUTE_EVENT_LISTENER(loadingerror);

  bool check(const String& font, const String& text, ExceptionState&);
  ScriptPromise load(ScriptState*, const String& font, const String& text);
  ScriptPromise ready(ScriptState*);

  FontFaceSet* addForBinding(ScriptState*, FontFace*, ExceptionState&);
  void clearForBinding(ScriptState*, ExceptionState&);
  bool deleteForBinding(ScriptState*, FontFace*, ExceptionState&);
  bool hasForBinding(ScriptState*, FontFace*, ExceptionState&) const;

  size_t size() const;
  AtomicString status() const;

  ExecutionContext* GetExecutionContext() const override;
  const AtomicString& InterfaceName() const override;

  Document* GetDocument() const;

  void DidLayout();
  void BeginFontLoading(FontFace*);

  // FontFace::LoadFontCallback
  void NotifyLoaded(FontFace*) override;
  void NotifyError(FontFace*) override;

  size_t ApproximateBlankCharacterCount() const;

  // SuspendableObject
  void Suspend() override;
  void Resume() override;
  void ContextDestroyed(ExecutionContext*) override;

  static FontFaceSet* From(Document&);
  static void DidLayout(Document&);
  static size_t ApproximateBlankCharacterCount(Document&);

  static const char* SupplementName() { return "FontFaceSet"; }

  void AddFontFacesToFontFaceCache(FontFaceCache*, CSSFontSelector*);

  DECLARE_VIRTUAL_TRACE();

 private:
  static FontFaceSet* Create(Document& document) {
    return new FontFaceSet(document);
  }

  // Iterable overrides.
  FontFaceSetIterable::IterationSource* StartIteration(
      ScriptState*,
      ExceptionState&) override;

  class IterationSource final : public FontFaceSetIterable::IterationSource {
   public:
    explicit IterationSource(const HeapVector<Member<FontFace>>& font_faces)
        : index_(0), font_faces_(font_faces) {}
    bool Next(ScriptState*,
              Member<FontFace>&,
              Member<FontFace>&,
              ExceptionState&) override;

    DEFINE_INLINE_VIRTUAL_TRACE() {
      visitor->Trace(font_faces_);
      FontFaceSetIterable::IterationSource::Trace(visitor);
    }

   private:
    size_t index_;
    HeapVector<Member<FontFace>> font_faces_;
  };

  class FontLoadHistogram {
    DISALLOW_NEW();

   public:
    enum Status { kNoWebFonts, kHadBlankText, kDidNotHaveBlankText, kReported };
    FontLoadHistogram() : status_(kNoWebFonts), count_(0), recorded_(false) {}
    void IncrementCount() { count_++; }
    void UpdateStatus(FontFace*);
    void Record();

   private:
    Status status_;
    int count_;
    bool recorded_;
  };

  explicit FontFaceSet(Document&);

  bool InActiveDocumentContext() const;
  void AddToLoadingFonts(FontFace*);
  void RemoveFromLoadingFonts(FontFace*);
  void FireLoadingEvent();
  void FireDoneEventIfPossible();
  bool ResolveFontStyle(const String&, Font&);
  void HandlePendingEventsAndPromisesSoon();
  void HandlePendingEventsAndPromises();
  const HeapListHashSet<Member<FontFace>>& CssConnectedFontFaceList() const;
  bool IsCSSConnectedFontFace(FontFace*) const;
  bool ShouldSignalReady() const;

  using ReadyProperty = ScriptPromiseProperty<Member<FontFaceSet>,
                                              Member<FontFaceSet>,
                                              Member<DOMException>>;

  HeapHashSet<Member<FontFace>> loading_fonts_;
  bool should_fire_loading_event_;
  bool is_loading_;
  Member<ReadyProperty> ready_;
  FontFaceArray loaded_fonts_;
  FontFaceArray failed_fonts_;
  HeapListHashSet<Member<FontFace>> non_css_connected_faces_;

  Member<AsyncMethodRunner<FontFaceSet>> async_runner_;

  FontLoadHistogram histogram_;
};

}  // namespace blink

#endif  // FontFaceSet_h
