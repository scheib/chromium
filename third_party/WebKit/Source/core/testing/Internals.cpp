/*
 * Copyright (C) 2012 Google Inc. All rights reserved.
 * Copyright (C) 2013 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "core/testing/Internals.h"

#include <deque>
#include <memory>

#include "bindings/core/v8/ExceptionMessages.h"
#include "bindings/core/v8/ExceptionState.h"
#include "bindings/core/v8/ScriptFunction.h"
#include "bindings/core/v8/ScriptPromise.h"
#include "bindings/core/v8/ScriptPromiseResolver.h"
#include "bindings/core/v8/V8IteratorResultValue.h"
#include "core/HTMLNames.h"
#include "core/SVGNames.h"
#include "core/animation/DocumentTimeline.h"
#include "core/dom/ClientRect.h"
#include "core/dom/ClientRectList.h"
#include "core/dom/DOMArrayBuffer.h"
#include "core/dom/DOMNodeIds.h"
#include "core/dom/DOMStringList.h"
#include "core/dom/Document.h"
#include "core/dom/Element.h"
#include "core/dom/ExceptionCode.h"
#include "core/dom/Iterator.h"
#include "core/dom/NodeComputedStyle.h"
#include "core/dom/PseudoElement.h"
#include "core/dom/Range.h"
#include "core/dom/StaticNodeList.h"
#include "core/dom/StyleEngine.h"
#include "core/dom/TreeScope.h"
#include "core/dom/ViewportDescription.h"
#include "core/dom/shadow/ElementShadow.h"
#include "core/dom/shadow/ElementShadowV0.h"
#include "core/dom/shadow/FlatTreeTraversal.h"
#include "core/dom/shadow/SelectRuleFeatureSet.h"
#include "core/dom/shadow/ShadowRoot.h"
#include "core/editing/Editor.h"
#include "core/editing/PlainTextRange.h"
#include "core/editing/SurroundingText.h"
#include "core/editing/iterators/TextIterator.h"
#include "core/editing/markers/DocumentMarker.h"
#include "core/editing/markers/DocumentMarkerController.h"
#include "core/editing/serializers/Serialization.h"
#include "core/editing/spellcheck/IdleSpellCheckCallback.h"
#include "core/editing/spellcheck/SpellCheckRequester.h"
#include "core/editing/spellcheck/SpellChecker.h"
#include "core/frame/EventHandlerRegistry.h"
#include "core/frame/FrameConsole.h"
#include "core/frame/FrameView.h"
#include "core/frame/LocalDOMWindow.h"
#include "core/frame/LocalFrame.h"
#include "core/frame/Settings.h"
#include "core/frame/VisualViewport.h"
#include "core/geometry/DOMPoint.h"
#include "core/html/HTMLContentElement.h"
#include "core/html/HTMLIFrameElement.h"
#include "core/html/HTMLImageElement.h"
#include "core/html/HTMLInputElement.h"
#include "core/html/HTMLMediaElement.h"
#include "core/html/HTMLSelectElement.h"
#include "core/html/HTMLTextAreaElement.h"
#include "core/html/HTMLVideoElement.h"
#include "core/html/canvas/CanvasFontCache.h"
#include "core/html/canvas/CanvasRenderingContext.h"
#include "core/html/forms/FormController.h"
#include "core/html/forms/TextControlInnerElements.h"
#include "core/html/shadow/ShadowElementNames.h"
#include "core/input/EventHandler.h"
#include "core/input/KeyboardEventManager.h"
#include "core/inspector/MainThreadDebugger.h"
#include "core/layout/LayoutMenuList.h"
#include "core/layout/LayoutObject.h"
#include "core/layout/LayoutTreeAsText.h"
#include "core/layout/api/LayoutMenuListItem.h"
#include "core/layout/api/LayoutViewItem.h"
#include "core/layout/compositing/CompositedLayerMapping.h"
#include "core/layout/compositing/PaintLayerCompositor.h"
#include "core/loader/DocumentLoader.h"
#include "core/loader/FrameLoader.h"
#include "core/loader/HistoryItem.h"
#include "core/page/ChromeClient.h"
#include "core/page/FocusController.h"
#include "core/page/Page.h"
#include "core/page/PrintContext.h"
#include "core/page/scrolling/ScrollState.h"
#include "core/paint/PaintLayer.h"
#include "core/probe/CoreProbes.h"
#include "core/svg/SVGImageElement.h"
#include "core/testing/CallbackFunctionTest.h"
#include "core/testing/DictionaryTest.h"
#include "core/testing/GCObservation.h"
#include "core/testing/InternalRuntimeFlags.h"
#include "core/testing/InternalSettings.h"
#include "core/testing/LayerRect.h"
#include "core/testing/LayerRectList.h"
#include "core/testing/MockHyphenation.h"
#include "core/testing/OriginTrialsTest.h"
#include "core/testing/RecordTest.h"
#include "core/testing/SequenceTest.h"
#include "core/testing/TypeConversions.h"
#include "core/testing/UnionTypesTest.h"
#include "core/workers/WorkerThread.h"
#include "gpu/command_buffer/client/gles2_interface.h"
#include "platform/Cursor.h"
#include "platform/InstanceCounters.h"
#include "platform/Language.h"
#include "platform/LayoutLocale.h"
#include "platform/RuntimeEnabledFeatures.h"
#include "platform/bindings/V8ThrowException.h"
#include "platform/geometry/IntRect.h"
#include "platform/geometry/LayoutRect.h"
#include "platform/graphics/GraphicsLayer.h"
#include "platform/heap/Handle.h"
#include "platform/instrumentation/tracing/TraceEvent.h"
#include "platform/loader/fetch/MemoryCache.h"
#include "platform/loader/fetch/ResourceFetcher.h"
#include "platform/loader/fetch/ResourceLoadPriority.h"
#include "platform/network/NetworkStateNotifier.h"
#include "platform/scroll/ProgrammaticScrollAnimator.h"
#include "platform/scroll/ScrollbarTheme.h"
#include "platform/testing/URLTestHelpers.h"
#include "platform/weborigin/SchemeRegistry.h"
#include "platform/wtf/Optional.h"
#include "platform/wtf/PtrUtil.h"
#include "platform/wtf/dtoa.h"
#include "platform/wtf/text/StringBuffer.h"
#include "public/platform/Platform.h"
#include "public/platform/WebConnectionType.h"
#include "public/platform/WebGraphicsContext3DProvider.h"
#include "public/platform/WebLayer.h"
#include "public/platform/modules/remoteplayback/WebRemotePlaybackAvailability.h"
#include "v8/include/v8.h"

namespace blink {

namespace {

class UseCounterObserverImpl final : public UseCounter::Observer {
  WTF_MAKE_NONCOPYABLE(UseCounterObserverImpl);

 public:
  UseCounterObserverImpl(ScriptPromiseResolver* resolver,
                         UseCounter::Feature feature)
      : resolver_(resolver), feature_(feature) {}

  bool OnCountFeature(UseCounter::Feature feature) final {
    if (feature_ != feature)
      return false;
    resolver_->Resolve(feature);
    return true;
  }

  DEFINE_INLINE_VIRTUAL_TRACE() {
    UseCounter::Observer::Trace(visitor);
    visitor->Trace(resolver_);
  }

 private:
  Member<ScriptPromiseResolver> resolver_;
  UseCounter::Feature feature_;
};

}  // namespace

static WTF::Optional<DocumentMarker::MarkerType> MarkerTypeFrom(
    const String& marker_type) {
  if (DeprecatedEqualIgnoringCase(marker_type, "Spelling"))
    return DocumentMarker::kSpelling;
  if (DeprecatedEqualIgnoringCase(marker_type, "Grammar"))
    return DocumentMarker::kGrammar;
  if (DeprecatedEqualIgnoringCase(marker_type, "TextMatch"))
    return DocumentMarker::kTextMatch;
  return WTF::nullopt;
}

static WTF::Optional<DocumentMarker::MarkerTypes> MarkerTypesFrom(
    const String& marker_type) {
  if (marker_type.IsEmpty() || DeprecatedEqualIgnoringCase(marker_type, "all"))
    return DocumentMarker::AllMarkers();
  WTF::Optional<DocumentMarker::MarkerType> type = MarkerTypeFrom(marker_type);
  if (!type)
    return WTF::nullopt;
  return DocumentMarker::MarkerTypes(type.value());
}

static SpellCheckRequester* GetSpellCheckRequester(Document* document) {
  if (!document || !document->GetFrame())
    return 0;
  return &document->GetFrame()->GetSpellChecker().GetSpellCheckRequester();
}

static ScrollableArea* ScrollableAreaForNode(Node* node) {
  if (!node)
    return nullptr;

  if (node->IsDocumentNode()) {
    // This can be removed after root layer scrolling is enabled.
    if (FrameView* frame_view = ToDocument(node)->View())
      return frame_view->LayoutViewportScrollableArea();
  }

  LayoutObject* layout_object = node->GetLayoutObject();
  if (!layout_object || !layout_object->IsBox())
    return nullptr;

  return ToLayoutBox(layout_object)->GetScrollableArea();
}

static RuntimeEnabledFeatures::Backup* g_s_features_backup = nullptr;

void Internals::ResetToConsistentState(Page* page) {
  DCHECK(page);

  if (!g_s_features_backup)
    g_s_features_backup = new RuntimeEnabledFeatures::Backup;
  g_s_features_backup->restore();
  page->SetIsCursorVisible(true);
  page->SetPageScaleFactor(1);
  page->DeprecatedLocalMainFrame()
      ->View()
      ->LayoutViewportScrollableArea()
      ->SetScrollOffset(ScrollOffset(), kProgrammaticScroll);
  OverrideUserPreferredLanguages(Vector<AtomicString>());
  if (!page->DeprecatedLocalMainFrame()
           ->GetSpellChecker()
           .IsSpellCheckingEnabled())
    page->DeprecatedLocalMainFrame()
        ->GetSpellChecker()
        .ToggleSpellCheckingEnabled();
  if (page->DeprecatedLocalMainFrame()->GetEditor().IsOverwriteModeEnabled())
    page->DeprecatedLocalMainFrame()->GetEditor().ToggleOverwriteModeEnabled();

  if (ScrollingCoordinator* scrolling_coordinator =
          page->GetScrollingCoordinator())
    scrolling_coordinator->Reset();

  page->DeprecatedLocalMainFrame()->View()->Clear();
  KeyboardEventManager::SetCurrentCapsLockState(
      OverrideCapsLockState::kDefault);
}

Internals::Internals(ExecutionContext* context)
    : runtime_flags_(InternalRuntimeFlags::create()),
      document_(ToDocument(context)) {
  document_->Fetcher()->EnableIsPreloadedForTest();
}

LocalFrame* Internals::GetFrame() const {
  if (!document_)
    return nullptr;
  return document_->GetFrame();
}

InternalSettings* Internals::settings() const {
  if (!document_)
    return 0;
  Page* page = document_->GetPage();
  if (!page)
    return 0;
  return InternalSettings::From(*page);
}

InternalRuntimeFlags* Internals::runtimeFlags() const {
  return runtime_flags_.Get();
}

unsigned Internals::workerThreadCount() const {
  return WorkerThread::WorkerThreadCount();
}

GCObservation* Internals::observeGC(ScriptValue script_value) {
  v8::Local<v8::Value> observed_value = script_value.V8Value();
  DCHECK(!observed_value.IsEmpty());
  if (observed_value->IsNull() || observed_value->IsUndefined()) {
    V8ThrowException::ThrowTypeError(v8::Isolate::GetCurrent(),
                                     "value to observe is null or undefined");
    return nullptr;
  }

  return GCObservation::Create(observed_value);
}

unsigned Internals::updateStyleAndReturnAffectedElementCount(
    ExceptionState& exception_state) const {
  if (!document_) {
    exception_state.ThrowDOMException(kInvalidAccessError,
                                      "No context document is available.");
    return 0;
  }

  unsigned before_count = document_->GetStyleEngine().StyleForElementCount();
  document_->UpdateStyleAndLayoutTree();
  return document_->GetStyleEngine().StyleForElementCount() - before_count;
}

unsigned Internals::needsLayoutCount(ExceptionState& exception_state) const {
  LocalFrame* context_frame = GetFrame();
  if (!context_frame) {
    exception_state.ThrowDOMException(kInvalidAccessError,
                                      "No context frame is available.");
    return 0;
  }

  bool is_partial;
  unsigned needs_layout_objects;
  unsigned total_objects;
  context_frame->View()->CountObjectsNeedingLayout(needs_layout_objects,
                                                   total_objects, is_partial);
  return needs_layout_objects;
}

unsigned Internals::hitTestCount(Document* doc,
                                 ExceptionState& exception_state) const {
  if (!doc) {
    exception_state.ThrowDOMException(kInvalidAccessError,
                                      "Must supply document to check");
    return 0;
  }

  return doc->GetLayoutViewItem().HitTestCount();
}

unsigned Internals::hitTestCacheHits(Document* doc,
                                     ExceptionState& exception_state) const {
  if (!doc) {
    exception_state.ThrowDOMException(kInvalidAccessError,
                                      "Must supply document to check");
    return 0;
  }

  return doc->GetLayoutViewItem().HitTestCacheHits();
}

Element* Internals::elementFromPoint(Document* doc,
                                     double x,
                                     double y,
                                     bool ignore_clipping,
                                     bool allow_child_frame_content,
                                     ExceptionState& exception_state) const {
  if (!doc) {
    exception_state.ThrowDOMException(kInvalidAccessError,
                                      "Must supply document to check");
    return 0;
  }

  if (doc->GetLayoutViewItem().IsNull())
    return 0;

  HitTestRequest::HitTestRequestType hit_type =
      HitTestRequest::kReadOnly | HitTestRequest::kActive;
  if (ignore_clipping)
    hit_type |= HitTestRequest::kIgnoreClipping;
  if (allow_child_frame_content)
    hit_type |= HitTestRequest::kAllowChildFrameContent;

  HitTestRequest request(hit_type);

  return doc->HitTestPoint(x, y, request);
}

void Internals::clearHitTestCache(Document* doc,
                                  ExceptionState& exception_state) const {
  if (!doc) {
    exception_state.ThrowDOMException(kInvalidAccessError,
                                      "Must supply document to check");
    return;
  }

  if (doc->GetLayoutViewItem().IsNull())
    return;

  doc->GetLayoutViewItem().ClearHitTestCache();
}

bool Internals::isPreloaded(const String& url) {
  return isPreloadedBy(url, document_);
}

bool Internals::isPreloadedBy(const String& url, Document* document) {
  if (!document)
    return false;
  return document->Fetcher()->IsPreloadedForTest(document->CompleteURL(url));
}

bool Internals::isLoading(const String& url) {
  if (!document_)
    return false;
  const String cache_identifier = document_->Fetcher()->GetCacheIdentifier();
  Resource* resource = GetMemoryCache()->ResourceForURL(
      document_->CompleteURL(url), cache_identifier);
  // We check loader() here instead of isLoading(), because a multipart
  // ImageResource lies isLoading() == false after the first part is loaded.
  return resource && resource->Loader();
}

bool Internals::isLoadingFromMemoryCache(const String& url) {
  if (!document_)
    return false;
  const String cache_identifier = document_->Fetcher()->GetCacheIdentifier();
  Resource* resource = GetMemoryCache()->ResourceForURL(
      document_->CompleteURL(url), cache_identifier);
  return resource && resource->GetStatus() == ResourceStatus::kCached;
}

int Internals::getResourcePriority(const String& url, Document* document) {
  if (!document)
    return ResourceLoadPriority::kResourceLoadPriorityUnresolved;

  Resource* resource = document->Fetcher()->AllResources().at(
      URLTestHelpers::ToKURL(url.Utf8().data()));

  if (!resource)
    return ResourceLoadPriority::kResourceLoadPriorityUnresolved;

  return resource->GetResourceRequest().Priority();
}

String Internals::getResourceHeader(const String& url,
                                    const String& header,
                                    Document* document) {
  if (!document)
    return String();
  Resource* resource = document->Fetcher()->AllResources().at(
      URLTestHelpers::ToKURL(url.Utf8().data()));
  if (!resource)
    return String();
  return resource->GetResourceRequest().HttpHeaderField(header.Utf8().data());
}

bool Internals::isSharingStyle(Element* element1, Element* element2) const {
  DCHECK(element1 && element2);
  return element1->GetComputedStyle() == element2->GetComputedStyle();
}

bool Internals::isValidContentSelect(Element* insertion_point,
                                     ExceptionState& exception_state) {
  DCHECK(insertion_point);
  if (!insertion_point->IsInsertionPoint()) {
    exception_state.ThrowDOMException(kInvalidAccessError,
                                      "The element is not an insertion point.");
    return false;
  }

  return isHTMLContentElement(*insertion_point) &&
         toHTMLContentElement(*insertion_point).IsSelectValid();
}

Node* Internals::treeScopeRootNode(Node* node) {
  DCHECK(node);
  return &node->GetTreeScope().RootNode();
}

Node* Internals::parentTreeScope(Node* node) {
  DCHECK(node);
  const TreeScope* parent_tree_scope = node->GetTreeScope().ParentTreeScope();
  return parent_tree_scope ? &parent_tree_scope->RootNode() : 0;
}

unsigned short Internals::compareTreeScopePosition(
    const Node* node1,
    const Node* node2,
    ExceptionState& exception_state) const {
  DCHECK(node1 && node2);
  const TreeScope* tree_scope1 =
      node1->IsDocumentNode()
          ? static_cast<const TreeScope*>(ToDocument(node1))
          : node1->IsShadowRoot()
                ? static_cast<const TreeScope*>(ToShadowRoot(node1))
                : 0;
  const TreeScope* tree_scope2 =
      node2->IsDocumentNode()
          ? static_cast<const TreeScope*>(ToDocument(node2))
          : node2->IsShadowRoot()
                ? static_cast<const TreeScope*>(ToShadowRoot(node2))
                : 0;
  if (!tree_scope1 || !tree_scope2) {
    exception_state.ThrowDOMException(
        kInvalidAccessError,
        String::Format(
            "The %s node is neither a document node, nor a shadow root.",
            tree_scope1 ? "second" : "first"));
    return 0;
  }
  return tree_scope1->ComparePosition(*tree_scope2);
}

void Internals::pauseAnimations(double pause_time,
                                ExceptionState& exception_state) {
  if (pause_time < 0) {
    exception_state.ThrowDOMException(
        kInvalidAccessError, ExceptionMessages::IndexExceedsMinimumBound(
                                 "pauseTime", pause_time, 0.0));
    return;
  }

  if (!GetFrame())
    return;

  GetFrame()->View()->UpdateAllLifecyclePhases();
  GetFrame()->GetDocument()->Timeline().PauseAnimationsForTesting(pause_time);
}

bool Internals::isCompositedAnimation(Animation* animation) {
  return animation->HasActiveAnimationsOnCompositor();
}

void Internals::disableCompositedAnimation(Animation* animation) {
  animation->DisableCompositedAnimationForTesting();
}

void Internals::disableCSSAdditiveAnimations() {
  RuntimeEnabledFeatures::setCSSAdditiveAnimationsEnabled(false);
}

void Internals::advanceTimeForImage(Element* image,
                                    double delta_time_in_seconds,
                                    ExceptionState& exception_state) {
  DCHECK(image);
  if (delta_time_in_seconds < 0) {
    exception_state.ThrowDOMException(
        kInvalidAccessError,
        ExceptionMessages::IndexExceedsMinimumBound(
            "deltaTimeInSeconds", delta_time_in_seconds, 0.0));
    return;
  }

  ImageResourceContent* resource = nullptr;
  if (isHTMLImageElement(*image)) {
    resource = toHTMLImageElement(*image).CachedImage();
  } else if (isSVGImageElement(*image)) {
    resource = toSVGImageElement(*image).CachedImage();
  } else {
    exception_state.ThrowDOMException(
        kInvalidAccessError, "The element provided is not a image element.");
    return;
  }

  if (!resource || !resource->HasImage()) {
    exception_state.ThrowDOMException(kInvalidAccessError,
                                      "The image resource is not available.");
    return;
  }

  Image* image_data = resource->GetImage();
  if (!image_data->IsBitmapImage()) {
    exception_state.ThrowDOMException(
        kInvalidAccessError, "The image resource is not a BitmapImage type.");
    return;
  }

  image_data->AdvanceTime(delta_time_in_seconds);
}

void Internals::advanceImageAnimation(Element* image,
                                      ExceptionState& exception_state) {
  DCHECK(image);

  ImageResourceContent* resource = nullptr;
  if (isHTMLImageElement(*image)) {
    resource = toHTMLImageElement(*image).CachedImage();
  } else if (isSVGImageElement(*image)) {
    resource = toSVGImageElement(*image).CachedImage();
  } else {
    exception_state.ThrowDOMException(
        kInvalidAccessError, "The element provided is not a image element.");
    return;
  }

  if (!resource || !resource->HasImage()) {
    exception_state.ThrowDOMException(kInvalidAccessError,
                                      "The image resource is not available.");
    return;
  }

  Image* image_data = resource->GetImage();
  image_data->AdvanceAnimationForTesting();
}

bool Internals::hasShadowInsertionPoint(const Node* root,
                                        ExceptionState& exception_state) const {
  DCHECK(root);
  if (!root->IsShadowRoot()) {
    exception_state.ThrowDOMException(
        kInvalidAccessError, "The node argument is not a shadow root.");
    return false;
  }
  return ToShadowRoot(root)->ContainsShadowElements();
}

bool Internals::hasContentElement(const Node* root,
                                  ExceptionState& exception_state) const {
  DCHECK(root);
  if (!root->IsShadowRoot()) {
    exception_state.ThrowDOMException(
        kInvalidAccessError, "The node argument is not a shadow root.");
    return false;
  }
  return ToShadowRoot(root)->ContainsContentElements();
}

size_t Internals::countElementShadow(const Node* root,
                                     ExceptionState& exception_state) const {
  DCHECK(root);
  if (!root->IsShadowRoot()) {
    exception_state.ThrowDOMException(
        kInvalidAccessError, "The node argument is not a shadow root.");
    return 0;
  }
  return ToShadowRoot(root)->ChildShadowRootCount();
}

Node* Internals::nextSiblingInFlatTree(Node* node,
                                       ExceptionState& exception_state) {
  DCHECK(node);
  if (!node->CanParticipateInFlatTree()) {
    exception_state.ThrowDOMException(
        kInvalidAccessError,
        "The node argument doesn't particite in the flat tree.");
    return 0;
  }
  return FlatTreeTraversal::NextSibling(*node);
}

Node* Internals::firstChildInFlatTree(Node* node,
                                      ExceptionState& exception_state) {
  DCHECK(node);
  if (!node->CanParticipateInFlatTree()) {
    exception_state.ThrowDOMException(
        kInvalidAccessError,
        "The node argument doesn't particite in the flat tree");
    return 0;
  }
  return FlatTreeTraversal::FirstChild(*node);
}

Node* Internals::lastChildInFlatTree(Node* node,
                                     ExceptionState& exception_state) {
  DCHECK(node);
  if (!node->CanParticipateInFlatTree()) {
    exception_state.ThrowDOMException(
        kInvalidAccessError,
        "The node argument doesn't particite in the flat tree.");
    return 0;
  }
  return FlatTreeTraversal::LastChild(*node);
}

Node* Internals::nextInFlatTree(Node* node, ExceptionState& exception_state) {
  DCHECK(node);
  if (!node->CanParticipateInFlatTree()) {
    exception_state.ThrowDOMException(
        kInvalidAccessError,
        "The node argument doesn't particite in the flat tree.");
    return 0;
  }
  return FlatTreeTraversal::Next(*node);
}

Node* Internals::previousInFlatTree(Node* node,
                                    ExceptionState& exception_state) {
  DCHECK(node);
  if (!node->CanParticipateInFlatTree()) {
    exception_state.ThrowDOMException(
        kInvalidAccessError,
        "The node argument doesn't particite in the flat tree.");
    return 0;
  }
  return FlatTreeTraversal::Previous(*node);
}

String Internals::elementLayoutTreeAsText(Element* element,
                                          ExceptionState& exception_state) {
  DCHECK(element);
  element->GetDocument().View()->UpdateAllLifecyclePhases();

  String representation = ExternalRepresentation(element);
  if (representation.IsEmpty()) {
    exception_state.ThrowDOMException(
        kInvalidAccessError,
        "The element provided has no external representation.");
    return String();
  }

  return representation;
}

CSSStyleDeclaration* Internals::computedStyleIncludingVisitedInfo(
    Node* node) const {
  DCHECK(node);
  bool allow_visited_style = true;
  return CSSComputedStyleDeclaration::Create(node, allow_visited_style);
}

ShadowRoot* Internals::createUserAgentShadowRoot(Element* host) {
  DCHECK(host);
  return &host->EnsureUserAgentShadowRoot();
}

ShadowRoot* Internals::shadowRoot(Element* host) {
  // FIXME: Internals::shadowRoot() in tests should be converted to
  // youngestShadowRoot() or oldestShadowRoot().
  // https://bugs.webkit.org/show_bug.cgi?id=78465
  return youngestShadowRoot(host);
}

ShadowRoot* Internals::youngestShadowRoot(Element* host) {
  DCHECK(host);
  if (ElementShadow* shadow = host->Shadow())
    return &shadow->YoungestShadowRoot();
  return 0;
}

ShadowRoot* Internals::oldestShadowRoot(Element* host) {
  DCHECK(host);
  if (ElementShadow* shadow = host->Shadow())
    return &shadow->OldestShadowRoot();
  return 0;
}

ShadowRoot* Internals::youngerShadowRoot(Node* shadow,
                                         ExceptionState& exception_state) {
  DCHECK(shadow);
  if (!shadow->IsShadowRoot()) {
    exception_state.ThrowDOMException(
        kInvalidAccessError, "The node provided is not a shadow root.");
    return 0;
  }

  return ToShadowRoot(shadow)->YoungerShadowRoot();
}

String Internals::shadowRootType(const Node* root,
                                 ExceptionState& exception_state) const {
  DCHECK(root);
  if (!root->IsShadowRoot()) {
    exception_state.ThrowDOMException(
        kInvalidAccessError, "The node provided is not a shadow root.");
    return String();
  }

  switch (ToShadowRoot(root)->GetType()) {
    case ShadowRootType::kUserAgent:
      return String("UserAgentShadowRoot");
    case ShadowRootType::V0:
      return String("V0ShadowRoot");
    case ShadowRootType::kOpen:
      return String("OpenShadowRoot");
    case ShadowRootType::kClosed:
      return String("ClosedShadowRoot");
    default:
      NOTREACHED();
      return String("Unknown");
  }
}

const AtomicString& Internals::shadowPseudoId(Element* element) {
  DCHECK(element);
  return element->ShadowPseudoId();
}

String Internals::visiblePlaceholder(Element* element) {
  if (element && IsTextControlElement(*element)) {
    const TextControlElement& text_control_element =
        ToTextControlElement(*element);
    if (!text_control_element.IsPlaceholderVisible())
      return String();
    if (HTMLElement* placeholder_element =
            text_control_element.PlaceholderElement())
      return placeholder_element->textContent();
  }

  return String();
}

void Internals::selectColorInColorChooser(Element* element,
                                          const String& color_value) {
  DCHECK(element);
  if (!isHTMLInputElement(*element))
    return;
  Color color;
  if (!color.SetFromString(color_value))
    return;
  toHTMLInputElement(*element).SelectColorInColorChooser(color);
}

void Internals::endColorChooser(Element* element) {
  DCHECK(element);
  if (!isHTMLInputElement(*element))
    return;
  toHTMLInputElement(*element).EndColorChooser();
}

bool Internals::hasAutofocusRequest(Document* document) {
  if (!document)
    document = document_;
  return document->AutofocusElement();
}

bool Internals::hasAutofocusRequest() {
  return hasAutofocusRequest(0);
}

Vector<String> Internals::formControlStateOfHistoryItem(
    ExceptionState& exception_state) {
  HistoryItem* main_item = nullptr;
  if (GetFrame())
    main_item = GetFrame()->Loader().GetDocumentLoader()->GetHistoryItem();
  if (!main_item) {
    exception_state.ThrowDOMException(kInvalidAccessError,
                                      "No history item is available.");
    return Vector<String>();
  }
  return main_item->GetDocumentState();
}

void Internals::setFormControlStateOfHistoryItem(
    const Vector<String>& state,
    ExceptionState& exception_state) {
  HistoryItem* main_item = nullptr;
  if (GetFrame())
    main_item = GetFrame()->Loader().GetDocumentLoader()->GetHistoryItem();
  if (!main_item) {
    exception_state.ThrowDOMException(kInvalidAccessError,
                                      "No history item is available.");
    return;
  }
  main_item->ClearDocumentState();
  main_item->SetDocumentState(state);
}

DOMWindow* Internals::pagePopupWindow() const {
  if (!document_)
    return nullptr;
  if (Page* page = document_->GetPage()) {
    LocalDOMWindow* popup =
        ToLocalDOMWindow(page->GetChromeClient().PagePopupWindowForTesting());
    if (popup) {
      // We need to make the popup same origin so layout tests can access it.
      popup->document()->UpdateSecurityOrigin(document_->GetSecurityOrigin());
    }
    return popup;
  }
  return nullptr;
}

ClientRect* Internals::absoluteCaretBounds(ExceptionState& exception_state) {
  if (!GetFrame()) {
    exception_state.ThrowDOMException(
        kInvalidAccessError, "The document's frame cannot be retrieved.");
    return ClientRect::Create();
  }

  document_->UpdateStyleAndLayoutIgnorePendingStylesheets();
  return ClientRect::Create(GetFrame()->Selection().AbsoluteCaretBounds());
}

String Internals::textAffinity() {
  if (GetFrame()
          ->GetPage()
          ->GetFocusController()
          .FocusedFrame()
          ->Selection()
          .GetSelectionInDOMTree()
          .Affinity() == TextAffinity::kUpstream) {
    return "Upstream";
  }
  return "Downstream";
}

ClientRect* Internals::boundingBox(Element* element) {
  DCHECK(element);

  element->GetDocument().UpdateStyleAndLayoutIgnorePendingStylesheets();
  LayoutObject* layout_object = element->GetLayoutObject();
  if (!layout_object)
    return ClientRect::Create();
  return ClientRect::Create(
      layout_object->AbsoluteBoundingBoxRectIgnoringTransforms());
}

void Internals::setMarker(Document* document,
                          const Range* range,
                          const String& marker_type,
                          ExceptionState& exception_state) {
  if (!document) {
    exception_state.ThrowDOMException(kInvalidAccessError,
                                      "No context document is available.");
    return;
  }

  WTF::Optional<DocumentMarker::MarkerType> type = MarkerTypeFrom(marker_type);
  if (!type) {
    exception_state.ThrowDOMException(
        kSyntaxError,
        "The marker type provided ('" + marker_type + "') is invalid.");
    return;
  }

  document->UpdateStyleAndLayoutIgnorePendingStylesheets();
  document->Markers().AddMarker(range->StartPosition(), range->EndPosition(),
                                type.value());
}

unsigned Internals::markerCountForNode(Node* node,
                                       const String& marker_type,
                                       ExceptionState& exception_state) {
  DCHECK(node);
  WTF::Optional<DocumentMarker::MarkerTypes> marker_types =
      MarkerTypesFrom(marker_type);
  if (!marker_types) {
    exception_state.ThrowDOMException(
        kSyntaxError,
        "The marker type provided ('" + marker_type + "') is invalid.");
    return 0;
  }

  return node->GetDocument()
      .Markers()
      .MarkersFor(node, marker_types.value())
      .size();
}

unsigned Internals::activeMarkerCountForNode(Node* node) {
  DCHECK(node);

  // Only TextMatch markers can be active.
  DocumentMarker::MarkerType marker_type = DocumentMarker::kTextMatch;
  DocumentMarkerVector markers =
      node->GetDocument().Markers().MarkersFor(node, marker_type);

  unsigned active_marker_count = 0;
  for (const auto& marker : markers) {
    if (marker->IsActiveMatch())
      active_marker_count++;
  }

  return active_marker_count;
}

DocumentMarker* Internals::MarkerAt(Node* node,
                                    const String& marker_type,
                                    unsigned index,
                                    ExceptionState& exception_state) {
  DCHECK(node);
  WTF::Optional<DocumentMarker::MarkerTypes> marker_types =
      MarkerTypesFrom(marker_type);
  if (!marker_types) {
    exception_state.ThrowDOMException(
        kSyntaxError,
        "The marker type provided ('" + marker_type + "') is invalid.");
    return 0;
  }

  DocumentMarkerVector markers =
      node->GetDocument().Markers().MarkersFor(node, marker_types.value());
  if (markers.size() <= index)
    return 0;
  return markers[index];
}

Range* Internals::markerRangeForNode(Node* node,
                                     const String& marker_type,
                                     unsigned index,
                                     ExceptionState& exception_state) {
  DCHECK(node);
  DocumentMarker* marker = MarkerAt(node, marker_type, index, exception_state);
  if (!marker)
    return nullptr;
  return Range::Create(node->GetDocument(), node, marker->StartOffset(), node,
                       marker->EndOffset());
}

String Internals::markerDescriptionForNode(Node* node,
                                           const String& marker_type,
                                           unsigned index,
                                           ExceptionState& exception_state) {
  DocumentMarker* marker = MarkerAt(node, marker_type, index, exception_state);
  if (!marker)
    return String();
  return marker->Description();
}

static WTF::Optional<DocumentMarker::MatchStatus> MatchStatusFrom(
    const String& match_status) {
  if (EqualIgnoringASCIICase(match_status, "kActive"))
    return DocumentMarker::MatchStatus::kActive;
  if (EqualIgnoringASCIICase(match_status, "kInactive"))
    return DocumentMarker::MatchStatus::kInactive;
  return WTF::nullopt;
}

void Internals::addTextMatchMarker(const Range* range,
                                   const String& match_status,
                                   ExceptionState& exception_state) {
  DCHECK(range);
  if (!range->OwnerDocument().View())
    return;

  WTF::Optional<DocumentMarker::MatchStatus> match_status_enum =
      MatchStatusFrom(match_status);
  if (!match_status_enum) {
    exception_state.ThrowDOMException(
        kSyntaxError,
        "The match status provided ('" + match_status + "') is invalid.");
    return;
  }

  range->OwnerDocument().UpdateStyleAndLayoutIgnorePendingStylesheets();
  range->OwnerDocument().Markers().AddTextMatchMarker(
      EphemeralRange(range), match_status_enum.value());

  // This simulates what the production code does after
  // DocumentMarkerController::addTextMatchMarker().
  range->OwnerDocument().View()->InvalidatePaintForTickmarks();
}

static bool ParseColor(const String& value,
                       Color& color,
                       ExceptionState& exception_state,
                       String error_message) {
  if (!color.SetFromString(value)) {
    exception_state.ThrowDOMException(kInvalidAccessError, error_message);
    return false;
  }
  return true;
}

void Internals::addCompositionMarker(const Range* range,
                                     const String& underline_color_value,
                                     bool thick,
                                     const String& background_color_value,
                                     ExceptionState& exception_state) {
  DCHECK(range);
  range->OwnerDocument().UpdateStyleAndLayoutIgnorePendingStylesheets();

  Color underline_color;
  Color background_color;
  if (ParseColor(underline_color_value, underline_color, exception_state,
                 "Invalid underline color.") &&
      ParseColor(background_color_value, background_color, exception_state,
                 "Invalid background color.")) {
    range->OwnerDocument().Markers().AddCompositionMarker(
        range->StartPosition(), range->EndPosition(), underline_color, thick,
        background_color);
  }
}

void Internals::setTextMatchMarkersActive(Node* node,
                                          unsigned start_offset,
                                          unsigned end_offset,
                                          bool active) {
  DCHECK(node);
  node->GetDocument().Markers().SetTextMatchMarkersActive(node, start_offset,
                                                          end_offset, active);
}

void Internals::setMarkedTextMatchesAreHighlighted(Document* document,
                                                   bool highlight) {
  if (!document || !document->GetFrame())
    return;

  document->GetFrame()->GetEditor().SetMarkedTextMatchesAreHighlighted(
      highlight);
}

void Internals::setFrameViewPosition(Document* document,
                                     long x,
                                     long y,
                                     ExceptionState& exception_state) {
  DCHECK(document);
  if (!document->View()) {
    exception_state.ThrowDOMException(kInvalidAccessError,
                                      "The document provided is invalid.");
    return;
  }

  FrameView* frame_view = document->View();
  bool scrollbars_suppressed_old_value = frame_view->ScrollbarsSuppressed();

  frame_view->SetScrollbarsSuppressed(false);
  frame_view->UpdateScrollOffsetFromInternals(IntSize(x, y));
  frame_view->SetScrollbarsSuppressed(scrollbars_suppressed_old_value);
}

String Internals::viewportAsText(Document* document,
                                 float,
                                 int available_width,
                                 int available_height,
                                 ExceptionState& exception_state) {
  DCHECK(document);
  if (!document->GetPage()) {
    exception_state.ThrowDOMException(kInvalidAccessError,
                                      "The document provided is invalid.");
    return String();
  }

  document->UpdateStyleAndLayoutIgnorePendingStylesheets();

  Page* page = document->GetPage();

  // Update initial viewport size.
  IntSize initial_viewport_size(available_width, available_height);
  document->GetPage()->DeprecatedLocalMainFrame()->View()->SetFrameRect(
      IntRect(IntPoint::Zero(), initial_viewport_size));

  ViewportDescription description = page->GetViewportDescription();
  PageScaleConstraints constraints =
      description.Resolve(FloatSize(initial_viewport_size), Length());

  constraints.FitToContentsWidth(constraints.layout_size.Width(),
                                 available_width);
  constraints.ResolveAutoInitialScale();

  StringBuilder builder;

  builder.Append("viewport size ");
  builder.Append(String::Number(constraints.layout_size.Width()));
  builder.Append('x');
  builder.Append(String::Number(constraints.layout_size.Height()));

  builder.Append(" scale ");
  builder.Append(String::Number(constraints.initial_scale));
  builder.Append(" with limits [");
  builder.Append(String::Number(constraints.minimum_scale));
  builder.Append(", ");
  builder.Append(String::Number(constraints.maximum_scale));

  builder.Append("] and userScalable ");
  builder.Append(description.user_zoom ? "true" : "false");

  return builder.ToString();
}

bool Internals::elementShouldAutoComplete(Element* element,
                                          ExceptionState& exception_state) {
  DCHECK(element);
  if (isHTMLInputElement(*element))
    return toHTMLInputElement(*element).ShouldAutocomplete();

  exception_state.ThrowDOMException(kInvalidNodeTypeError,
                                    "The element provided is not an INPUT.");
  return false;
}

String Internals::suggestedValue(Element* element,
                                 ExceptionState& exception_state) {
  DCHECK(element);
  if (!element->IsFormControlElement()) {
    exception_state.ThrowDOMException(
        kInvalidNodeTypeError,
        "The element provided is not a form control element.");
    return String();
  }

  String suggested_value;
  if (isHTMLInputElement(*element))
    suggested_value = toHTMLInputElement(*element).SuggestedValue();

  if (isHTMLTextAreaElement(*element))
    suggested_value = toHTMLTextAreaElement(*element).SuggestedValue();

  if (isHTMLSelectElement(*element))
    suggested_value = toHTMLSelectElement(*element).SuggestedValue();

  return suggested_value;
}

void Internals::setSuggestedValue(Element* element,
                                  const String& value,
                                  ExceptionState& exception_state) {
  DCHECK(element);
  if (!element->IsFormControlElement()) {
    exception_state.ThrowDOMException(
        kInvalidNodeTypeError,
        "The element provided is not a form control element.");
    return;
  }

  if (isHTMLInputElement(*element))
    toHTMLInputElement(*element).SetSuggestedValue(value);

  if (isHTMLTextAreaElement(*element))
    toHTMLTextAreaElement(*element).SetSuggestedValue(value);

  if (isHTMLSelectElement(*element))
    toHTMLSelectElement(*element).SetSuggestedValue(value);
}

void Internals::setEditingValue(Element* element,
                                const String& value,
                                ExceptionState& exception_state) {
  DCHECK(element);
  if (!isHTMLInputElement(*element)) {
    exception_state.ThrowDOMException(kInvalidNodeTypeError,
                                      "The element provided is not an INPUT.");
    return;
  }

  toHTMLInputElement(*element).SetEditingValue(value);
}

void Internals::setAutofilled(Element* element,
                              bool enabled,
                              ExceptionState& exception_state) {
  DCHECK(element);
  if (!element->IsFormControlElement()) {
    exception_state.ThrowDOMException(
        kInvalidNodeTypeError,
        "The element provided is not a form control element.");
    return;
  }
  ToHTMLFormControlElement(element)->SetAutofilled(enabled);
}

Range* Internals::rangeFromLocationAndLength(Element* scope,
                                             int range_location,
                                             int range_length) {
  DCHECK(scope);

  // TextIterator depends on Layout information, make sure layout it up to date.
  scope->GetDocument().UpdateStyleAndLayoutIgnorePendingStylesheets();

  return CreateRange(
      PlainTextRange(range_location, range_location + range_length)
          .CreateRange(*scope));
}

unsigned Internals::locationFromRange(Element* scope, const Range* range) {
  DCHECK(scope && range);
  // PlainTextRange depends on Layout information, make sure layout it up to
  // date.
  scope->GetDocument().UpdateStyleAndLayoutIgnorePendingStylesheets();

  return PlainTextRange::Create(*scope, *range).Start();
}

unsigned Internals::lengthFromRange(Element* scope, const Range* range) {
  DCHECK(scope && range);
  // PlainTextRange depends on Layout information, make sure layout it up to
  // date.
  scope->GetDocument().UpdateStyleAndLayoutIgnorePendingStylesheets();

  return PlainTextRange::Create(*scope, *range).length();
}

String Internals::rangeAsText(const Range* range) {
  DCHECK(range);
  // Clean layout is required by plain text extraction.
  range->OwnerDocument().UpdateStyleAndLayoutIgnorePendingStylesheets();

  return range->GetText();
}

// FIXME: The next four functions are very similar - combine them once
// bestClickableNode/bestContextMenuNode have been combined..

DOMPoint* Internals::touchPositionAdjustedToBestClickableNode(
    long x,
    long y,
    long width,
    long height,
    Document* document,
    ExceptionState& exception_state) {
  DCHECK(document);
  if (!document->GetFrame()) {
    exception_state.ThrowDOMException(kInvalidAccessError,
                                      "The document provided is invalid.");
    return 0;
  }

  document->UpdateStyleAndLayout();

  IntSize radius(width / 2, height / 2);
  IntPoint point(x + radius.Width(), y + radius.Height());

  EventHandler& event_handler = document->GetFrame()->GetEventHandler();
  IntPoint hit_test_point =
      document->GetFrame()->View()->RootFrameToContents(point);
  HitTestResult result = event_handler.HitTestResultAtPoint(
      hit_test_point,
      HitTestRequest::kReadOnly | HitTestRequest::kActive |
          HitTestRequest::kListBased,
      LayoutSize(radius));

  Node* target_node = 0;
  IntPoint adjusted_point;

  bool found_node = event_handler.BestClickableNodeForHitTestResult(
      result, adjusted_point, target_node);
  if (found_node)
    return DOMPoint::Create(adjusted_point.X(), adjusted_point.Y());

  return 0;
}

Node* Internals::touchNodeAdjustedToBestClickableNode(
    long x,
    long y,
    long width,
    long height,
    Document* document,
    ExceptionState& exception_state) {
  DCHECK(document);
  if (!document->GetFrame()) {
    exception_state.ThrowDOMException(kInvalidAccessError,
                                      "The document provided is invalid.");
    return 0;
  }

  document->UpdateStyleAndLayout();

  IntSize radius(width / 2, height / 2);
  IntPoint point(x + radius.Width(), y + radius.Height());

  EventHandler& event_handler = document->GetFrame()->GetEventHandler();
  IntPoint hit_test_point =
      document->GetFrame()->View()->RootFrameToContents(point);
  HitTestResult result = event_handler.HitTestResultAtPoint(
      hit_test_point,
      HitTestRequest::kReadOnly | HitTestRequest::kActive |
          HitTestRequest::kListBased,
      LayoutSize(radius));

  Node* target_node = 0;
  IntPoint adjusted_point;
  document->GetFrame()->GetEventHandler().BestClickableNodeForHitTestResult(
      result, adjusted_point, target_node);
  return target_node;
}

DOMPoint* Internals::touchPositionAdjustedToBestContextMenuNode(
    long x,
    long y,
    long width,
    long height,
    Document* document,
    ExceptionState& exception_state) {
  DCHECK(document);
  if (!document->GetFrame()) {
    exception_state.ThrowDOMException(kInvalidAccessError,
                                      "The document provided is invalid.");
    return 0;
  }

  document->UpdateStyleAndLayout();

  IntSize radius(width / 2, height / 2);
  IntPoint point(x + radius.Width(), y + radius.Height());

  EventHandler& event_handler = document->GetFrame()->GetEventHandler();
  IntPoint hit_test_point =
      document->GetFrame()->View()->RootFrameToContents(point);
  HitTestResult result = event_handler.HitTestResultAtPoint(
      hit_test_point,
      HitTestRequest::kReadOnly | HitTestRequest::kActive |
          HitTestRequest::kListBased,
      LayoutSize(radius));

  Node* target_node = 0;
  IntPoint adjusted_point;

  bool found_node = event_handler.BestContextMenuNodeForHitTestResult(
      result, adjusted_point, target_node);
  if (found_node)
    return DOMPoint::Create(adjusted_point.X(), adjusted_point.Y());

  return DOMPoint::Create(x, y);
}

Node* Internals::touchNodeAdjustedToBestContextMenuNode(
    long x,
    long y,
    long width,
    long height,
    Document* document,
    ExceptionState& exception_state) {
  DCHECK(document);
  if (!document->GetFrame()) {
    exception_state.ThrowDOMException(kInvalidAccessError,
                                      "The document provided is invalid.");
    return 0;
  }

  document->UpdateStyleAndLayout();

  IntSize radius(width / 2, height / 2);
  IntPoint point(x + radius.Width(), y + radius.Height());

  EventHandler& event_handler = document->GetFrame()->GetEventHandler();
  IntPoint hit_test_point =
      document->GetFrame()->View()->RootFrameToContents(point);
  HitTestResult result = event_handler.HitTestResultAtPoint(
      hit_test_point,
      HitTestRequest::kReadOnly | HitTestRequest::kActive |
          HitTestRequest::kListBased,
      LayoutSize(radius));

  Node* target_node = 0;
  IntPoint adjusted_point;
  event_handler.BestContextMenuNodeForHitTestResult(result, adjusted_point,
                                                    target_node);
  return target_node;
}

ClientRect* Internals::bestZoomableAreaForTouchPoint(
    long x,
    long y,
    long width,
    long height,
    Document* document,
    ExceptionState& exception_state) {
  DCHECK(document);
  if (!document->GetFrame()) {
    exception_state.ThrowDOMException(kInvalidAccessError,
                                      "The document provided is invalid.");
    return nullptr;
  }

  document->UpdateStyleAndLayout();

  IntSize radius(width / 2, height / 2);
  IntPoint point(x + radius.Width(), y + radius.Height());

  Node* target_node = 0;
  IntRect zoomable_area;
  bool found_node =
      document->GetFrame()->GetEventHandler().BestZoomableAreaForTouchPoint(
          point, radius, zoomable_area, target_node);
  if (found_node)
    return ClientRect::Create(zoomable_area);

  return nullptr;
}

int Internals::lastSpellCheckRequestSequence(Document* document,
                                             ExceptionState& exception_state) {
  SpellCheckRequester* requester = GetSpellCheckRequester(document);

  if (!requester) {
    exception_state.ThrowDOMException(
        kInvalidAccessError,
        "No spell check requestor can be obtained for the provided document.");
    return -1;
  }

  return requester->LastRequestSequence();
}

int Internals::lastSpellCheckProcessedSequence(
    Document* document,
    ExceptionState& exception_state) {
  SpellCheckRequester* requester = GetSpellCheckRequester(document);

  if (!requester) {
    exception_state.ThrowDOMException(
        kInvalidAccessError,
        "No spell check requestor can be obtained for the provided document.");
    return -1;
  }

  return requester->LastProcessedSequence();
}

String Internals::idleTimeSpellCheckerState(Document* document,
                                            ExceptionState& exception_state) {
  static const char* const kTexts[] = {
#define V(state) #state,
      FOR_EACH_IDLE_SPELL_CHECK_CALLBACK_STATE(V)
#undef V
  };

  if (!document || !document->GetFrame()) {
    exception_state.ThrowDOMException(
        kInvalidAccessError,
        "No frame can be obtained from the provided document.");
    return String();
  }

  IdleSpellCheckCallback::State state = document->GetFrame()
                                            ->GetSpellChecker()
                                            .GetIdleSpellCheckCallback()
                                            .GetState();
  const auto& it = std::begin(kTexts) + static_cast<size_t>(state);
  DCHECK_GE(it, std::begin(kTexts)) << "Unknown state value";
  DCHECK_LT(it, std::end(kTexts)) << "Unknown state value";
  return *it;
}

void Internals::runIdleTimeSpellChecker(Document* document,
                                        ExceptionState& exception_state) {
  if (!document || !document->GetFrame()) {
    exception_state.ThrowDOMException(
        kInvalidAccessError,
        "No frame can be obtained from the provided document.");
    return;
  }

  document->GetFrame()
      ->GetSpellChecker()
      .GetIdleSpellCheckCallback()
      .ForceInvocationForTesting();
}

Vector<AtomicString> Internals::userPreferredLanguages() const {
  return blink::UserPreferredLanguages();
}

// Optimally, the bindings generator would pass a Vector<AtomicString> here but
// this is not supported yet.
void Internals::setUserPreferredLanguages(const Vector<String>& languages) {
  Vector<AtomicString> atomic_languages;
  for (size_t i = 0; i < languages.size(); ++i)
    atomic_languages.push_back(AtomicString(languages[i]));
  OverrideUserPreferredLanguages(atomic_languages);
}

unsigned Internals::mediaKeysCount() {
  return InstanceCounters::CounterValue(InstanceCounters::kMediaKeysCounter);
}

unsigned Internals::mediaKeySessionCount() {
  return InstanceCounters::CounterValue(
      InstanceCounters::kMediaKeySessionCounter);
}

unsigned Internals::suspendableObjectCount(Document* document) {
  DCHECK(document);
  return document->SuspendableObjectCount();
}

static unsigned EventHandlerCount(
    Document& document,
    EventHandlerRegistry::EventHandlerClass handler_class) {
  if (!document.GetPage())
    return 0;
  EventHandlerRegistry* registry =
      &document.GetPage()->GetEventHandlerRegistry();
  unsigned count = 0;
  const EventTargetSet* targets = registry->EventHandlerTargets(handler_class);
  if (targets) {
    for (const auto& target : *targets)
      count += target.value;
  }
  return count;
}

unsigned Internals::wheelEventHandlerCount(Document* document) {
  DCHECK(document);
  return EventHandlerCount(*document,
                           EventHandlerRegistry::kWheelEventBlocking);
}

unsigned Internals::scrollEventHandlerCount(Document* document) {
  DCHECK(document);
  return EventHandlerCount(*document, EventHandlerRegistry::kScrollEvent);
}

unsigned Internals::touchStartOrMoveEventHandlerCount(Document* document) {
  DCHECK(document);
  return EventHandlerCount(
             *document, EventHandlerRegistry::kTouchStartOrMoveEventBlocking) +
         EventHandlerCount(*document,
                           EventHandlerRegistry::kTouchStartOrMoveEventPassive);
}

unsigned Internals::touchEndOrCancelEventHandlerCount(Document* document) {
  DCHECK(document);
  return EventHandlerCount(
             *document, EventHandlerRegistry::kTouchEndOrCancelEventBlocking) +
         EventHandlerCount(*document,
                           EventHandlerRegistry::kTouchEndOrCancelEventPassive);
}

static PaintLayer* FindLayerForGraphicsLayer(PaintLayer* search_root,
                                             GraphicsLayer* graphics_layer,
                                             IntSize* layer_offset,
                                             String* layer_type) {
  *layer_offset = IntSize();
  if (search_root->HasCompositedLayerMapping() &&
      graphics_layer ==
          search_root->GetCompositedLayerMapping()->MainGraphicsLayer()) {
    // If the |graphicsLayer| sets the scrollingContent layer as its
    // scroll parent, consider it belongs to the scrolling layer and
    // mark the layer type as "scrolling".
    if (!search_root->GetLayoutObject().HasTransformRelatedProperty() &&
        search_root->ScrollParent() &&
        search_root->Parent() == search_root->ScrollParent()) {
      *layer_type = "scrolling";
      // For hit-test rect visualization to work, the hit-test rect should
      // be relative to the scrolling layer and in this case the hit-test
      // rect is relative to the element's own GraphicsLayer. So we will have
      // to adjust the rect to be relative to the scrolling layer here.
      // Only when the element's offsetParent == scroller's offsetParent we
      // can compute the element's relative position to the scrolling content
      // in this way.
      if (search_root->GetLayoutObject().OffsetParent() ==
          search_root->Parent()->GetLayoutObject().OffsetParent()) {
        LayoutBoxModelObject& current = search_root->GetLayoutObject();
        LayoutBoxModelObject& parent = search_root->Parent()->GetLayoutObject();
        layer_offset->SetWidth((parent.OffsetLeft(parent.OffsetParent()) -
                                current.OffsetLeft(parent.OffsetParent()))
                                   .ToInt());
        layer_offset->SetHeight((parent.OffsetTop(parent.OffsetParent()) -
                                 current.OffsetTop(parent.OffsetParent()))
                                    .ToInt());
        return search_root->Parent();
      }
    }

    LayoutRect rect;
    PaintLayer::MapRectInPaintInvalidationContainerToBacking(
        search_root->GetLayoutObject(), rect);
    rect.Move(search_root->GetCompositedLayerMapping()
                  ->ContentOffsetInCompositingLayer());

    *layer_offset = IntSize(rect.X().ToInt(), rect.Y().ToInt());
    return search_root;
  }

  // If the |graphicsLayer| is a scroller's scrollingContent layer,
  // consider this is a scrolling layer.
  GraphicsLayer* layer_for_scrolling =
      search_root->GetScrollableArea()
          ? search_root->GetScrollableArea()->LayerForScrolling()
          : 0;
  if (graphics_layer == layer_for_scrolling) {
    *layer_type = "scrolling";
    return search_root;
  }

  if (search_root->GetCompositingState() == kPaintsIntoGroupedBacking) {
    GraphicsLayer* squashing_layer =
        search_root->GroupedMapping()->SquashingLayer();
    if (graphics_layer == squashing_layer) {
      *layer_type = "squashing";
      LayoutRect rect;
      PaintLayer::MapRectInPaintInvalidationContainerToBacking(
          search_root->GetLayoutObject(), rect);
      *layer_offset = IntSize(rect.X().ToInt(), rect.Y().ToInt());
      return search_root;
    }
  }

  GraphicsLayer* layer_for_horizontal_scrollbar =
      search_root->GetScrollableArea()
          ? search_root->GetScrollableArea()->LayerForHorizontalScrollbar()
          : 0;
  if (graphics_layer == layer_for_horizontal_scrollbar) {
    *layer_type = "horizontalScrollbar";
    return search_root;
  }

  GraphicsLayer* layer_for_vertical_scrollbar =
      search_root->GetScrollableArea()
          ? search_root->GetScrollableArea()->LayerForVerticalScrollbar()
          : 0;
  if (graphics_layer == layer_for_vertical_scrollbar) {
    *layer_type = "verticalScrollbar";
    return search_root;
  }

  GraphicsLayer* layer_for_scroll_corner =
      search_root->GetScrollableArea()
          ? search_root->GetScrollableArea()->LayerForScrollCorner()
          : 0;
  if (graphics_layer == layer_for_scroll_corner) {
    *layer_type = "scrollCorner";
    return search_root;
  }

  // Search right to left to increase the chances that we'll choose the top-most
  // layers in a grouped mapping for squashing.
  for (PaintLayer* child = search_root->LastChild(); child;
       child = child->PreviousSibling()) {
    PaintLayer* found_layer = FindLayerForGraphicsLayer(
        child, graphics_layer, layer_offset, layer_type);
    if (found_layer)
      return found_layer;
  }

  return 0;
}

// Given a vector of rects, merge those that are adjacent, leaving empty rects
// in the place of no longer used slots. This is intended to simplify the list
// of rects returned by an SkRegion (which have been split apart for sorting
// purposes). No attempt is made to do this efficiently (eg. by relying on the
// sort criteria of SkRegion).
static void MergeRects(WebVector<blink::WebRect>& rects) {
  for (size_t i = 0; i < rects.size(); ++i) {
    if (rects[i].IsEmpty())
      continue;
    bool updated;
    do {
      updated = false;
      for (size_t j = i + 1; j < rects.size(); ++j) {
        if (rects[j].IsEmpty())
          continue;
        // Try to merge rects[j] into rects[i] along the 4 possible edges.
        if (rects[i].y == rects[j].y && rects[i].height == rects[j].height) {
          if (rects[i].x + rects[i].width == rects[j].x) {
            rects[i].width += rects[j].width;
            rects[j] = blink::WebRect();
            updated = true;
          } else if (rects[i].x == rects[j].x + rects[j].width) {
            rects[i].x = rects[j].x;
            rects[i].width += rects[j].width;
            rects[j] = blink::WebRect();
            updated = true;
          }
        } else if (rects[i].x == rects[j].x &&
                   rects[i].width == rects[j].width) {
          if (rects[i].y + rects[i].height == rects[j].y) {
            rects[i].height += rects[j].height;
            rects[j] = blink::WebRect();
            updated = true;
          } else if (rects[i].y == rects[j].y + rects[j].height) {
            rects[i].y = rects[j].y;
            rects[i].height += rects[j].height;
            rects[j] = blink::WebRect();
            updated = true;
          }
        }
      }
    } while (updated);
  }
}

static void AccumulateLayerRectList(PaintLayerCompositor* compositor,
                                    GraphicsLayer* graphics_layer,
                                    LayerRectList* rects) {
  WebVector<blink::WebRect> layer_rects =
      graphics_layer->PlatformLayer()->TouchEventHandlerRegion();
  if (!layer_rects.empty()) {
    MergeRects(layer_rects);
    String layer_type;
    IntSize layer_offset;
    PaintLayer* paint_layer = FindLayerForGraphicsLayer(
        compositor->RootLayer(), graphics_layer, &layer_offset, &layer_type);
    Node* node = paint_layer ? paint_layer->GetLayoutObject().GetNode() : 0;
    for (size_t i = 0; i < layer_rects.size(); ++i) {
      if (!layer_rects[i].IsEmpty()) {
        rects->Append(node, layer_type, layer_offset.Width(),
                      layer_offset.Height(),
                      ClientRect::Create(layer_rects[i]));
      }
    }
  }

  size_t num_children = graphics_layer->Children().size();
  for (size_t i = 0; i < num_children; ++i)
    AccumulateLayerRectList(compositor, graphics_layer->Children()[i], rects);
}

LayerRectList* Internals::touchEventTargetLayerRects(
    Document* document,
    ExceptionState& exception_state) {
  DCHECK(document);
  if (!document->View() || !document->GetPage() || document != document_) {
    exception_state.ThrowDOMException(kInvalidAccessError,
                                      "The document provided is invalid.");
    return nullptr;
  }

  if (ScrollingCoordinator* scrolling_coordinator =
          document->GetPage()->GetScrollingCoordinator())
    scrolling_coordinator->UpdateAfterCompositingChangeIfNeeded();

  LayoutViewItem view = document->GetLayoutViewItem();
  if (!view.IsNull()) {
    if (PaintLayerCompositor* compositor = view.Compositor()) {
      if (GraphicsLayer* root_layer = compositor->RootGraphicsLayer()) {
        LayerRectList* rects = LayerRectList::Create();
        AccumulateLayerRectList(compositor, root_layer, rects);
        return rects;
      }
    }
  }

  return nullptr;
}

bool Internals::executeCommand(Document* document,
                               const String& name,
                               const String& value,
                               ExceptionState& exception_state) {
  DCHECK(document);
  if (!document->GetFrame()) {
    exception_state.ThrowDOMException(kInvalidAccessError,
                                      "The document provided is invalid.");
    return false;
  }

  LocalFrame* frame = document->GetFrame();
  return frame->GetEditor().ExecuteCommand(name, value);
}

AtomicString Internals::htmlNamespace() {
  return HTMLNames::xhtmlNamespaceURI;
}

Vector<AtomicString> Internals::htmlTags() {
  Vector<AtomicString> tags(HTMLNames::HTMLTagsCount);
  std::unique_ptr<const HTMLQualifiedName* []> qualified_names =
      HTMLNames::getHTMLTags();
  for (size_t i = 0; i < HTMLNames::HTMLTagsCount; ++i)
    tags[i] = qualified_names[i]->LocalName();
  return tags;
}

AtomicString Internals::svgNamespace() {
  return SVGNames::svgNamespaceURI;
}

Vector<AtomicString> Internals::svgTags() {
  Vector<AtomicString> tags(SVGNames::SVGTagsCount);
  std::unique_ptr<const SVGQualifiedName* []> qualified_names =
      SVGNames::getSVGTags();
  for (size_t i = 0; i < SVGNames::SVGTagsCount; ++i)
    tags[i] = qualified_names[i]->LocalName();
  return tags;
}

StaticNodeList* Internals::nodesFromRect(
    Document* document,
    int center_x,
    int center_y,
    unsigned top_padding,
    unsigned right_padding,
    unsigned bottom_padding,
    unsigned left_padding,
    bool ignore_clipping,
    bool allow_child_frame_content,
    ExceptionState& exception_state) const {
  DCHECK(document);
  if (!document->GetFrame() || !document->GetFrame()->View()) {
    exception_state.ThrowDOMException(
        kInvalidAccessError,
        "No view can be obtained from the provided document.");
    return nullptr;
  }

  LocalFrame* frame = document->GetFrame();
  FrameView* frame_view = document->View();
  LayoutViewItem layout_view_item = document->GetLayoutViewItem();

  if (layout_view_item.IsNull())
    return nullptr;

  float zoom_factor = frame->PageZoomFactor();
  LayoutPoint point =
      LayoutPoint(FloatPoint(center_x * zoom_factor + frame_view->ScrollX(),
                             center_y * zoom_factor + frame_view->ScrollY()));

  HitTestRequest::HitTestRequestType hit_type = HitTestRequest::kReadOnly |
                                                HitTestRequest::kActive |
                                                HitTestRequest::kListBased;
  if (ignore_clipping)
    hit_type |= HitTestRequest::kIgnoreClipping;
  if (allow_child_frame_content)
    hit_type |= HitTestRequest::kAllowChildFrameContent;

  HitTestRequest request(hit_type);

  // When ignoreClipping is false, this method returns null for coordinates
  // outside of the viewport.
  if (!request.IgnoreClipping() &&
      !frame_view->VisibleContentRect().Intersects(
          HitTestLocation::RectForPoint(point, top_padding, right_padding,
                                        bottom_padding, left_padding)))
    return nullptr;

  HeapVector<Member<Node>> matches;
  HitTestResult result(request, point, top_padding, right_padding,
                       bottom_padding, left_padding);
  layout_view_item.HitTest(result);
  CopyToVector(result.ListBasedTestResult(), matches);

  return StaticNodeList::Adopt(matches);
}

bool Internals::hasSpellingMarker(Document* document,
                                  int from,
                                  int length,
                                  ExceptionState& exception_state) {
  if (!document || !document->GetFrame()) {
    exception_state.ThrowDOMException(
        kInvalidAccessError,
        "No frame can be obtained from the provided document.");
    return false;
  }

  document->UpdateStyleAndLayoutIgnorePendingStylesheets();
  return document->GetFrame()->GetSpellChecker().SelectionStartHasMarkerFor(
      DocumentMarker::kSpelling, from, length);
}

void Internals::setSpellCheckingEnabled(bool enabled,
                                        ExceptionState& exception_state) {
  if (!GetFrame()) {
    exception_state.ThrowDOMException(
        kInvalidAccessError,
        "No frame can be obtained from the provided document.");
    return;
  }

  if (enabled != GetFrame()->GetSpellChecker().IsSpellCheckingEnabled())
    GetFrame()->GetSpellChecker().ToggleSpellCheckingEnabled();
}

void Internals::replaceMisspelled(Document* document,
                                  const String& replacement,
                                  ExceptionState& exception_state) {
  if (!document || !document->GetFrame()) {
    exception_state.ThrowDOMException(
        kInvalidAccessError,
        "No frame can be obtained from the provided document.");
    return;
  }

  document->UpdateStyleAndLayoutIgnorePendingStylesheets();
  document->GetFrame()->GetSpellChecker().ReplaceMisspelledRange(replacement);
}

bool Internals::canHyphenate(const AtomicString& locale) {
  return LayoutLocale::ValueOrDefault(LayoutLocale::Get(locale))
      .GetHyphenation();
}

void Internals::setMockHyphenation(const AtomicString& locale) {
  LayoutLocale::SetHyphenationForTesting(locale, AdoptRef(new MockHyphenation));
}

bool Internals::isOverwriteModeEnabled(Document* document) {
  DCHECK(document);
  if (!document->GetFrame())
    return false;

  return document->GetFrame()->GetEditor().IsOverwriteModeEnabled();
}

void Internals::toggleOverwriteModeEnabled(Document* document) {
  DCHECK(document);
  if (!document->GetFrame())
    return;

  document->GetFrame()->GetEditor().ToggleOverwriteModeEnabled();
}

unsigned Internals::numberOfLiveNodes() const {
  return InstanceCounters::CounterValue(InstanceCounters::kNodeCounter);
}

unsigned Internals::numberOfLiveDocuments() const {
  return InstanceCounters::CounterValue(InstanceCounters::kDocumentCounter);
}

bool Internals::hasGrammarMarker(Document* document,
                                 int from,
                                 int length,
                                 ExceptionState& exception_state) {
  if (!document || !document->GetFrame()) {
    exception_state.ThrowDOMException(
        kInvalidAccessError,
        "No frame can be obtained from the provided document.");
    return false;
  }

  document->UpdateStyleAndLayoutIgnorePendingStylesheets();
  return document->GetFrame()->GetSpellChecker().SelectionStartHasMarkerFor(
      DocumentMarker::kGrammar, from, length);
}

unsigned Internals::numberOfScrollableAreas(Document* document) {
  DCHECK(document);
  if (!document->GetFrame())
    return 0;

  unsigned count = 0;
  LocalFrame* frame = document->GetFrame();
  if (frame->View()->ScrollableAreas())
    count += frame->View()->ScrollableAreas()->size();

  for (Frame* child = frame->Tree().FirstChild(); child;
       child = child->Tree().NextSibling()) {
    if (child->IsLocalFrame() && ToLocalFrame(child)->View() &&
        ToLocalFrame(child)->View()->ScrollableAreas())
      count += ToLocalFrame(child)->View()->ScrollableAreas()->size();
  }

  return count;
}

bool Internals::isPageBoxVisible(Document* document, int page_number) {
  DCHECK(document);
  return document->IsPageBoxVisible(page_number);
}

String Internals::layerTreeAsText(Document* document,
                                  ExceptionState& exception_state) const {
  return layerTreeAsText(document, 0, exception_state);
}

String Internals::elementLayerTreeAsText(
    Element* element,
    ExceptionState& exception_state) const {
  DCHECK(element);
  FrameView* frame_view = element->GetDocument().View();
  frame_view->UpdateAllLifecyclePhases();

  return elementLayerTreeAsText(element, 0, exception_state);
}

bool Internals::scrollsWithRespectTo(Element* element1,
                                     Element* element2,
                                     ExceptionState& exception_state) {
  DCHECK(element1 && element2);
  element1->GetDocument().View()->UpdateAllLifecyclePhases();

  LayoutObject* layout_object1 = element1->GetLayoutObject();
  LayoutObject* layout_object2 = element2->GetLayoutObject();
  if (!layout_object1 || !layout_object1->IsBox()) {
    exception_state.ThrowDOMException(
        kInvalidAccessError,
        layout_object1
            ? "The first provided element's layoutObject is not a box."
            : "The first provided element has no layoutObject.");
    return false;
  }
  if (!layout_object2 || !layout_object2->IsBox()) {
    exception_state.ThrowDOMException(
        kInvalidAccessError,
        layout_object2
            ? "The second provided element's layoutObject is not a box."
            : "The second provided element has no layoutObject.");
    return false;
  }

  PaintLayer* layer1 = ToLayoutBox(layout_object1)->Layer();
  PaintLayer* layer2 = ToLayoutBox(layout_object2)->Layer();
  if (!layer1 || !layer2) {
    exception_state.ThrowDOMException(
        kInvalidAccessError,
        String::Format(
            "No PaintLayer can be obtained from the %s provided element.",
            layer1 ? "second" : "first"));
    return false;
  }

  return layer1->ScrollsWithRespectTo(layer2);
}

String Internals::layerTreeAsText(Document* document,
                                  unsigned flags,
                                  ExceptionState& exception_state) const {
  DCHECK(document);
  if (!document->GetFrame()) {
    exception_state.ThrowDOMException(kInvalidAccessError,
                                      "The document provided is invalid.");
    return String();
  }

  document->View()->UpdateAllLifecyclePhases();

  return document->GetFrame()->LayerTreeAsText(flags);
}

String Internals::elementLayerTreeAsText(
    Element* element,
    unsigned flags,
    ExceptionState& exception_state) const {
  DCHECK(element);
  element->GetDocument().UpdateStyleAndLayout();

  LayoutObject* layout_object = element->GetLayoutObject();
  if (!layout_object || !layout_object->IsBox()) {
    exception_state.ThrowDOMException(
        kInvalidAccessError,
        layout_object ? "The provided element's layoutObject is not a box."
                      : "The provided element has no layoutObject.");
    return String();
  }

  PaintLayer* layer = ToLayoutBox(layout_object)->Layer();
  if (!layer || !layer->HasCompositedLayerMapping() ||
      !layer->GetCompositedLayerMapping()->MainGraphicsLayer()) {
    // Don't raise exception in these cases which may be normally used in tests.
    return String();
  }

  return layer->GetCompositedLayerMapping()
      ->MainGraphicsLayer()
      ->LayerTreeAsText(flags);
}

String Internals::scrollingStateTreeAsText(Document*) const {
  return String();
}

String Internals::mainThreadScrollingReasons(
    Document* document,
    ExceptionState& exception_state) const {
  DCHECK(document);
  if (!document->GetFrame()) {
    exception_state.ThrowDOMException(kInvalidAccessError,
                                      "The document provided is invalid.");
    return String();
  }

  document->GetFrame()->View()->UpdateAllLifecyclePhases();

  return document->GetFrame()->View()->MainThreadScrollingReasonsAsText();
}

ClientRectList* Internals::nonFastScrollableRects(
    Document* document,
    ExceptionState& exception_state) const {
  DCHECK(document);
  if (!document->GetFrame()) {
    exception_state.ThrowDOMException(kInvalidAccessError,
                                      "The document provided is invalid.");
    return nullptr;
  }

  Page* page = document->GetPage();
  if (!page)
    return nullptr;

  return page->NonFastScrollableRects(document->GetFrame());
}

void Internals::evictAllResources() const {
  GetMemoryCache()->EvictResources();
}

String Internals::counterValue(Element* element) {
  if (!element)
    return String();

  return CounterValueForElement(element);
}

int Internals::pageNumber(Element* element,
                          float page_width,
                          float page_height,
                          ExceptionState& exception_state) {
  if (!element)
    return 0;

  if (page_width <= 0 || page_height <= 0) {
    exception_state.ThrowDOMException(
        kV8TypeError, "Page width and height must be larger than 0.");
    return 0;
  }

  return PrintContext::PageNumberForElement(element,
                                            FloatSize(page_width, page_height));
}

Vector<String> Internals::IconURLs(Document* document,
                                   int icon_types_mask) const {
  Vector<IconURL> icon_urls = document->IconURLs(icon_types_mask);
  Vector<String> array;

  for (auto& icon_url : icon_urls)
    array.push_back(icon_url.icon_url_.GetString());

  return array;
}

Vector<String> Internals::shortcutIconURLs(Document* document) const {
  return IconURLs(document, kFavicon);
}

Vector<String> Internals::allIconURLs(Document* document) const {
  return IconURLs(document, kFavicon | kTouchIcon | kTouchPrecomposedIcon);
}

int Internals::numberOfPages(float page_width,
                             float page_height,
                             ExceptionState& exception_state) {
  if (!GetFrame())
    return -1;

  if (page_width <= 0 || page_height <= 0) {
    exception_state.ThrowDOMException(
        kV8TypeError, "Page width and height must be larger than 0.");
    return -1;
  }

  return PrintContext::NumberOfPages(GetFrame(),
                                     FloatSize(page_width, page_height));
}

String Internals::pageProperty(String property_name,
                               int page_number,
                               ExceptionState& exception_state) const {
  if (!GetFrame()) {
    exception_state.ThrowDOMException(kInvalidAccessError,
                                      "No frame is available.");
    return String();
  }

  return PrintContext::PageProperty(GetFrame(), property_name.Utf8().data(),
                                    page_number);
}

String Internals::pageSizeAndMarginsInPixels(
    int page_number,
    int width,
    int height,
    int margin_top,
    int margin_right,
    int margin_bottom,
    int margin_left,
    ExceptionState& exception_state) const {
  if (!GetFrame()) {
    exception_state.ThrowDOMException(kInvalidAccessError,
                                      "No frame is available.");
    return String();
  }

  return PrintContext::PageSizeAndMarginsInPixels(
      GetFrame(), page_number, width, height, margin_top, margin_right,
      margin_bottom, margin_left);
}

float Internals::pageScaleFactor(ExceptionState& exception_state) {
  if (!document_->GetPage()) {
    exception_state.ThrowDOMException(
        kInvalidAccessError, "The document's page cannot be retrieved.");
    return 0;
  }
  Page* page = document_->GetPage();
  return page->GetVisualViewport().PageScale();
}

void Internals::setPageScaleFactor(float scale_factor,
                                   ExceptionState& exception_state) {
  if (scale_factor <= 0)
    return;
  if (!document_->GetPage()) {
    exception_state.ThrowDOMException(
        kInvalidAccessError, "The document's page cannot be retrieved.");
    return;
  }
  Page* page = document_->GetPage();
  page->GetVisualViewport().SetScale(scale_factor);
}

void Internals::setPageScaleFactorLimits(float min_scale_factor,
                                         float max_scale_factor,
                                         ExceptionState& exception_state) {
  if (!document_->GetPage()) {
    exception_state.ThrowDOMException(
        kInvalidAccessError, "The document's page cannot be retrieved.");
    return;
  }

  Page* page = document_->GetPage();
  page->SetDefaultPageScaleLimits(min_scale_factor, max_scale_factor);
}

bool Internals::magnifyScaleAroundAnchor(float scale_factor, float x, float y) {
  if (!GetFrame())
    return false;

  return GetFrame()->GetPage()->GetVisualViewport().MagnifyScaleAroundAnchor(
      scale_factor, FloatPoint(x, y));
}

void Internals::setIsCursorVisible(Document* document,
                                   bool is_visible,
                                   ExceptionState& exception_state) {
  DCHECK(document);
  if (!document->GetPage()) {
    exception_state.ThrowDOMException(kInvalidAccessError,
                                      "No context document can be obtained.");
    return;
  }
  document->GetPage()->SetIsCursorVisible(is_visible);
}

String Internals::effectivePreload(HTMLMediaElement* media_element) {
  DCHECK(media_element);
  return media_element->EffectivePreload();
}

void Internals::mediaPlayerRemoteRouteAvailabilityChanged(
    HTMLMediaElement* media_element,
    bool available) {
  DCHECK(media_element);
  media_element->RemoteRouteAvailabilityChanged(
      available ? WebRemotePlaybackAvailability::kDeviceAvailable
                : WebRemotePlaybackAvailability::kSourceNotSupported);
}

void Internals::mediaPlayerPlayingRemotelyChanged(
    HTMLMediaElement* media_element,
    bool remote) {
  DCHECK(media_element);
  if (remote)
    media_element->ConnectedToRemoteDevice();
  else
    media_element->DisconnectedFromRemoteDevice();
}

void Internals::setMediaElementNetworkState(HTMLMediaElement* media_element,
                                            int state) {
  DCHECK(media_element);
  DCHECK(state >= WebMediaPlayer::NetworkState::kNetworkStateEmpty);
  DCHECK(state <= WebMediaPlayer::NetworkState::kNetworkStateDecodeError);
  media_element->SetNetworkState(
      static_cast<WebMediaPlayer::NetworkState>(state));
}

void Internals::setPersistent(HTMLVideoElement* video_element,
                              bool persistent) {
  DCHECK(video_element);
  video_element->OnBecamePersistentVideo(persistent);
}

void Internals::registerURLSchemeAsBypassingContentSecurityPolicy(
    const String& scheme) {
  SchemeRegistry::RegisterURLSchemeAsBypassingContentSecurityPolicy(scheme);
}

void Internals::registerURLSchemeAsBypassingContentSecurityPolicy(
    const String& scheme,
    const Vector<String>& policy_areas) {
  uint32_t policy_areas_enum = SchemeRegistry::kPolicyAreaNone;
  for (const auto& policy_area : policy_areas) {
    if (policy_area == "img")
      policy_areas_enum |= SchemeRegistry::kPolicyAreaImage;
    else if (policy_area == "style")
      policy_areas_enum |= SchemeRegistry::kPolicyAreaStyle;
  }
  SchemeRegistry::RegisterURLSchemeAsBypassingContentSecurityPolicy(
      scheme, static_cast<SchemeRegistry::PolicyAreas>(policy_areas_enum));
}

void Internals::removeURLSchemeRegisteredAsBypassingContentSecurityPolicy(
    const String& scheme) {
  SchemeRegistry::RemoveURLSchemeRegisteredAsBypassingContentSecurityPolicy(
      scheme);
}

TypeConversions* Internals::typeConversions() const {
  return TypeConversions::Create();
}

DictionaryTest* Internals::dictionaryTest() const {
  return DictionaryTest::Create();
}

RecordTest* Internals::recordTest() const {
  return RecordTest::Create();
}

SequenceTest* Internals::sequenceTest() const {
  return SequenceTest::create();
}

UnionTypesTest* Internals::unionTypesTest() const {
  return UnionTypesTest::Create();
}

OriginTrialsTest* Internals::originTrialsTest() const {
  return OriginTrialsTest::Create();
}

CallbackFunctionTest* Internals::callbackFunctionTest() const {
  return CallbackFunctionTest::Create();
}

Vector<String> Internals::getReferencedFilePaths() const {
  if (!GetFrame())
    return Vector<String>();

  return GetFrame()
      ->Loader()
      .GetDocumentLoader()
      ->GetHistoryItem()
      ->GetReferencedFilePaths();
}

void Internals::startStoringCompositedLayerDebugInfo(
    Document* document,
    ExceptionState& exception_state) {
  DCHECK(document);
  if (!document->View()) {
    exception_state.ThrowDOMException(kInvalidAccessError,
                                      "The document provided is invalid.");
    return;
  }

  FrameView* frame_view = document->View();
  frame_view->SetIsStoringCompositedLayerDebugInfo(true);
  frame_view->UpdateAllLifecyclePhases();
}

void Internals::stopStoringCompositedLayerDebugInfo(
    Document* document,
    ExceptionState& exception_state) {
  DCHECK(document);
  if (!document->View()) {
    exception_state.ThrowDOMException(kInvalidAccessError,
                                      "The document provided is invalid.");
    return;
  }

  FrameView* frame_view = document->View();
  frame_view->SetIsStoringCompositedLayerDebugInfo(false);
  frame_view->UpdateAllLifecyclePhases();
}

void Internals::startTrackingRepaints(Document* document,
                                      ExceptionState& exception_state) {
  DCHECK(document);
  if (!document->View()) {
    exception_state.ThrowDOMException(kInvalidAccessError,
                                      "The document provided is invalid.");
    return;
  }

  FrameView* frame_view = document->View();
  frame_view->UpdateAllLifecyclePhases();
  frame_view->SetTracksPaintInvalidations(true);
}

void Internals::stopTrackingRepaints(Document* document,
                                     ExceptionState& exception_state) {
  DCHECK(document);
  if (!document->View()) {
    exception_state.ThrowDOMException(kInvalidAccessError,
                                      "The document provided is invalid.");
    return;
  }

  FrameView* frame_view = document->View();
  frame_view->UpdateAllLifecyclePhases();
  frame_view->SetTracksPaintInvalidations(false);
}

void Internals::updateLayoutIgnorePendingStylesheetsAndRunPostLayoutTasks(
    Node* node,
    ExceptionState& exception_state) {
  Document* document = nullptr;
  if (!node) {
    document = document_;
  } else if (node->IsDocumentNode()) {
    document = ToDocument(node);
  } else if (isHTMLIFrameElement(*node)) {
    document = toHTMLIFrameElement(*node).contentDocument();
  }

  if (!document) {
    exception_state.ThrowTypeError(
        "The node provided is neither a document nor an IFrame.");
    return;
  }
  document->UpdateStyleAndLayoutIgnorePendingStylesheets(
      Document::kRunPostLayoutTasksSynchronously);
}

void Internals::forceFullRepaint(Document* document,
                                 ExceptionState& exception_state) {
  DCHECK(document);
  if (!document->View()) {
    exception_state.ThrowDOMException(kInvalidAccessError,
                                      "The document provided is invalid.");
    return;
  }

  LayoutViewItem layout_view_item = document->GetLayoutViewItem();
  if (!layout_view_item.IsNull())
    layout_view_item.InvalidatePaintForViewAndCompositedLayers();
}

ClientRectList* Internals::draggableRegions(Document* document,
                                            ExceptionState& exception_state) {
  return AnnotatedRegions(document, true, exception_state);
}

ClientRectList* Internals::nonDraggableRegions(
    Document* document,
    ExceptionState& exception_state) {
  return AnnotatedRegions(document, false, exception_state);
}

ClientRectList* Internals::AnnotatedRegions(Document* document,
                                            bool draggable,
                                            ExceptionState& exception_state) {
  DCHECK(document);
  if (!document->View()) {
    exception_state.ThrowDOMException(kInvalidAccessError,
                                      "The document provided is invalid.");
    return ClientRectList::Create();
  }

  document->UpdateStyleAndLayout();
  document->View()->UpdateDocumentAnnotatedRegions();
  Vector<AnnotatedRegionValue> regions = document->AnnotatedRegions();

  Vector<FloatQuad> quads;
  for (size_t i = 0; i < regions.size(); ++i) {
    if (regions[i].draggable == draggable)
      quads.push_back(FloatQuad(FloatRect(regions[i].bounds)));
  }
  return ClientRectList::Create(quads);
}

static const char* CursorTypeToString(Cursor::Type cursor_type) {
  switch (cursor_type) {
    case Cursor::kPointer:
      return "Pointer";
    case Cursor::kCross:
      return "Cross";
    case Cursor::kHand:
      return "Hand";
    case Cursor::kIBeam:
      return "IBeam";
    case Cursor::kWait:
      return "Wait";
    case Cursor::kHelp:
      return "Help";
    case Cursor::kEastResize:
      return "EastResize";
    case Cursor::kNorthResize:
      return "NorthResize";
    case Cursor::kNorthEastResize:
      return "NorthEastResize";
    case Cursor::kNorthWestResize:
      return "NorthWestResize";
    case Cursor::kSouthResize:
      return "SouthResize";
    case Cursor::kSouthEastResize:
      return "SouthEastResize";
    case Cursor::kSouthWestResize:
      return "SouthWestResize";
    case Cursor::kWestResize:
      return "WestResize";
    case Cursor::kNorthSouthResize:
      return "NorthSouthResize";
    case Cursor::kEastWestResize:
      return "EastWestResize";
    case Cursor::kNorthEastSouthWestResize:
      return "NorthEastSouthWestResize";
    case Cursor::kNorthWestSouthEastResize:
      return "NorthWestSouthEastResize";
    case Cursor::kColumnResize:
      return "ColumnResize";
    case Cursor::kRowResize:
      return "RowResize";
    case Cursor::kMiddlePanning:
      return "MiddlePanning";
    case Cursor::kEastPanning:
      return "EastPanning";
    case Cursor::kNorthPanning:
      return "NorthPanning";
    case Cursor::kNorthEastPanning:
      return "NorthEastPanning";
    case Cursor::kNorthWestPanning:
      return "NorthWestPanning";
    case Cursor::kSouthPanning:
      return "SouthPanning";
    case Cursor::kSouthEastPanning:
      return "SouthEastPanning";
    case Cursor::kSouthWestPanning:
      return "SouthWestPanning";
    case Cursor::kWestPanning:
      return "WestPanning";
    case Cursor::kMove:
      return "Move";
    case Cursor::kVerticalText:
      return "VerticalText";
    case Cursor::kCell:
      return "Cell";
    case Cursor::kContextMenu:
      return "ContextMenu";
    case Cursor::kAlias:
      return "Alias";
    case Cursor::kProgress:
      return "Progress";
    case Cursor::kNoDrop:
      return "NoDrop";
    case Cursor::kCopy:
      return "Copy";
    case Cursor::kNone:
      return "None";
    case Cursor::kNotAllowed:
      return "NotAllowed";
    case Cursor::kZoomIn:
      return "ZoomIn";
    case Cursor::kZoomOut:
      return "ZoomOut";
    case Cursor::kGrab:
      return "Grab";
    case Cursor::kGrabbing:
      return "Grabbing";
    case Cursor::kCustom:
      return "Custom";
  }

  NOTREACHED();
  return "UNKNOWN";
}

String Internals::getCurrentCursorInfo() {
  if (!GetFrame())
    return String();

  Cursor cursor =
      GetFrame()->GetPage()->GetChromeClient().LastSetCursorForTesting();

  StringBuilder result;
  result.Append("type=");
  result.Append(CursorTypeToString(cursor.GetType()));
  result.Append(" hotSpot=");
  result.AppendNumber(cursor.HotSpot().X());
  result.Append(',');
  result.AppendNumber(cursor.HotSpot().Y());
  if (cursor.GetImage()) {
    IntSize size = cursor.GetImage()->Size();
    result.Append(" image=");
    result.AppendNumber(size.Width());
    result.Append('x');
    result.AppendNumber(size.Height());
  }
  if (cursor.ImageScaleFactor() != 1) {
    result.Append(" scale=");
    result.AppendNumber(cursor.ImageScaleFactor(), 8);
  }

  return result.ToString();
}

bool Internals::cursorUpdatePending() const {
  if (!GetFrame())
    return false;

  return GetFrame()->GetEventHandler().CursorUpdatePending();
}

DOMArrayBuffer* Internals::serializeObject(
    PassRefPtr<SerializedScriptValue> value) const {
  StringView view = value->GetWireData();
  DCHECK(view.Is8Bit());
  DOMArrayBuffer* buffer =
      DOMArrayBuffer::CreateUninitializedOrNull(view.length(), sizeof(LChar));
  if (buffer)
    memcpy(buffer->Data(), view.Characters8(), view.length());
  return buffer;
}

PassRefPtr<SerializedScriptValue> Internals::deserializeBuffer(
    DOMArrayBuffer* buffer) const {
  String value(static_cast<const UChar*>(buffer->Data()),
               buffer->ByteLength() / sizeof(UChar));
  return SerializedScriptValue::Create(value);
}

DOMArrayBuffer* Internals::serializeWithInlineWasm(ScriptValue value) const {
  v8::Isolate* isolate = value.GetIsolate();
  ExceptionState exception_state(isolate, ExceptionState::kExecutionContext,
                                 "Internals", "serializeWithInlineWasm");
  v8::Local<v8::Value> v8_value = value.V8Value();
  SerializedScriptValue::SerializeOptions options;
  options.wasm_policy = SerializedScriptValue::SerializeOptions::kSerialize;
  RefPtr<SerializedScriptValue> obj = SerializedScriptValue::Serialize(
      isolate, v8_value, options, exception_state);
  if (exception_state.HadException())
    return nullptr;
  return serializeObject(obj);
}

ScriptValue Internals::deserializeBufferContainingWasm(
    ScriptState* state,
    DOMArrayBuffer* buffer) const {
  String value(static_cast<const UChar*>(buffer->Data()),
               buffer->ByteLength() / sizeof(UChar));
  DummyExceptionStateForTesting exception_state;
  SerializedScriptValue::DeserializeOptions options;
  options.read_wasm_from_stream = true;
  return ScriptValue::From(state,
                           SerializedScriptValue::Create(value)->Deserialize(
                               state->GetIsolate(), options));
}

void Internals::forceReload(bool bypass_cache) {
  if (!GetFrame())
    return;

  GetFrame()->Reload(
      bypass_cache ? kFrameLoadTypeReloadBypassingCache : kFrameLoadTypeReload,
      ClientRedirectPolicy::kNotClientRedirect);
}

Node* Internals::visibleSelectionAnchorNode() {
  if (!GetFrame())
    return nullptr;
  Position position = GetFrame()
                          ->Selection()
                          .ComputeVisibleSelectionInDOMTreeDeprecated()
                          .Base();
  return position.IsNull() ? nullptr : position.ComputeContainerNode();
}

unsigned Internals::visibleSelectionAnchorOffset() {
  if (!GetFrame())
    return 0;
  Position position = GetFrame()
                          ->Selection()
                          .ComputeVisibleSelectionInDOMTreeDeprecated()
                          .Base();
  return position.IsNull() ? 0 : position.ComputeOffsetInContainerNode();
}

Node* Internals::visibleSelectionFocusNode() {
  if (!GetFrame())
    return nullptr;
  Position position = GetFrame()
                          ->Selection()
                          .ComputeVisibleSelectionInDOMTreeDeprecated()
                          .Extent();
  return position.IsNull() ? nullptr : position.ComputeContainerNode();
}

unsigned Internals::visibleSelectionFocusOffset() {
  if (!GetFrame())
    return 0;
  Position position = GetFrame()
                          ->Selection()
                          .ComputeVisibleSelectionInDOMTreeDeprecated()
                          .Extent();
  return position.IsNull() ? 0 : position.ComputeOffsetInContainerNode();
}

ClientRect* Internals::selectionBounds(ExceptionState& exception_state) {
  if (!GetFrame()) {
    exception_state.ThrowDOMException(
        kInvalidAccessError, "The document's frame cannot be retrieved.");
    return nullptr;
  }

  return ClientRect::Create(FloatRect(GetFrame()->Selection().Bounds()));
}

String Internals::markerTextForListItem(Element* element) {
  DCHECK(element);
  return blink::MarkerTextForListItem(element);
}

String Internals::getImageSourceURL(Element* element) {
  DCHECK(element);
  return element->ImageSourceURL();
}

void Internals::forceImageReload(Element* element,
                                 ExceptionState& exception_state) {
  if (!element || !isHTMLImageElement(*element)) {
    exception_state.ThrowDOMException(
        kInvalidAccessError, "The element should be HTMLImageElement.");
  }

  toHTMLImageElement(*element).ForceReload();
}

String Internals::selectMenuListText(HTMLSelectElement* select) {
  DCHECK(select);
  LayoutObject* layout_object = select->GetLayoutObject();
  if (!layout_object || !layout_object->IsMenuList())
    return String();

  LayoutMenuListItem menu_list_item =
      LayoutMenuListItem(ToLayoutMenuList(layout_object));
  return menu_list_item.GetText();
}

bool Internals::isSelectPopupVisible(Node* node) {
  DCHECK(node);
  if (!isHTMLSelectElement(*node))
    return false;
  return toHTMLSelectElement(*node).PopupIsVisible();
}

bool Internals::selectPopupItemStyleIsRtl(Node* node, int item_index) {
  if (!node || !isHTMLSelectElement(*node))
    return false;

  HTMLSelectElement& select = toHTMLSelectElement(*node);
  if (item_index < 0 ||
      static_cast<size_t>(item_index) >= select.GetListItems().size())
    return false;
  const ComputedStyle* item_style =
      select.ItemComputedStyle(*select.GetListItems()[item_index]);
  return item_style && item_style->Direction() == TextDirection::kRtl;
}

int Internals::selectPopupItemStyleFontHeight(Node* node, int item_index) {
  if (!node || !isHTMLSelectElement(*node))
    return false;

  HTMLSelectElement& select = toHTMLSelectElement(*node);
  if (item_index < 0 ||
      static_cast<size_t>(item_index) >= select.GetListItems().size())
    return false;
  const ComputedStyle* item_style =
      select.ItemComputedStyle(*select.GetListItems()[item_index]);

  if (item_style) {
    const SimpleFontData* font_data = item_style->GetFont().PrimaryFont();
    DCHECK(font_data);
    return font_data ? font_data->GetFontMetrics().Height() : 0;
  }
  return 0;
}

void Internals::resetTypeAheadSession(HTMLSelectElement* select) {
  DCHECK(select);
  select->ResetTypeAheadSessionForTesting();
}

bool Internals::loseSharedGraphicsContext3D() {
  std::unique_ptr<WebGraphicsContext3DProvider> shared_provider =
      Platform::Current()->CreateSharedOffscreenGraphicsContext3DProvider();
  if (!shared_provider)
    return false;
  gpu::gles2::GLES2Interface* shared_gl = shared_provider->ContextGL();
  shared_gl->LoseContextCHROMIUM(GL_GUILTY_CONTEXT_RESET_EXT,
                                 GL_INNOCENT_CONTEXT_RESET_EXT);
  // To prevent tests that call loseSharedGraphicsContext3D from being
  // flaky, we call finish so that the context is guaranteed to be lost
  // synchronously (i.e. before returning).
  shared_gl->Finish();
  return true;
}

void Internals::forceCompositingUpdate(Document* document,
                                       ExceptionState& exception_state) {
  DCHECK(document);
  if (document->GetLayoutViewItem().IsNull()) {
    exception_state.ThrowDOMException(kInvalidAccessError,
                                      "The document provided is invalid.");
    return;
  }

  document->GetFrame()->View()->UpdateAllLifecyclePhases();
}

void Internals::setZoomFactor(float factor) {
  if (!GetFrame())
    return;

  GetFrame()->SetPageZoomFactor(factor);
}

void Internals::setShouldRevealPassword(Element* element,
                                        bool reveal,
                                        ExceptionState& exception_state) {
  DCHECK(element);
  if (!isHTMLInputElement(element)) {
    exception_state.ThrowDOMException(kInvalidNodeTypeError,
                                      "The element provided is not an INPUT.");
    return;
  }

  return toHTMLInputElement(*element).SetShouldRevealPassword(reveal);
}

namespace {

class AddOneFunction : public ScriptFunction {
 public:
  static v8::Local<v8::Function> CreateFunction(ScriptState* script_state) {
    AddOneFunction* self = new AddOneFunction(script_state);
    return self->BindToV8Function();
  }

 private:
  explicit AddOneFunction(ScriptState* script_state)
      : ScriptFunction(script_state) {}

  ScriptValue Call(ScriptValue value) override {
    v8::Local<v8::Value> v8_value = value.V8Value();
    DCHECK(v8_value->IsNumber());
    int int_value = v8_value.As<v8::Integer>()->Value();
    return ScriptValue(
        GetScriptState(),
        v8::Integer::New(GetScriptState()->GetIsolate(), int_value + 1));
  }
};

}  // namespace

ScriptPromise Internals::createResolvedPromise(ScriptState* script_state,
                                               ScriptValue value) {
  ScriptPromiseResolver* resolver = ScriptPromiseResolver::Create(script_state);
  ScriptPromise promise = resolver->Promise();
  resolver->Resolve(value);
  return promise;
}

ScriptPromise Internals::createRejectedPromise(ScriptState* script_state,
                                               ScriptValue value) {
  ScriptPromiseResolver* resolver = ScriptPromiseResolver::Create(script_state);
  ScriptPromise promise = resolver->Promise();
  resolver->Reject(value);
  return promise;
}

ScriptPromise Internals::addOneToPromise(ScriptState* script_state,
                                         ScriptPromise promise) {
  return promise.Then(AddOneFunction::CreateFunction(script_state));
}

ScriptPromise Internals::promiseCheck(ScriptState* script_state,
                                      long arg1,
                                      bool arg2,
                                      const Dictionary& arg3,
                                      const String& arg4,
                                      const Vector<String>& arg5,
                                      ExceptionState& exception_state) {
  if (arg2)
    return ScriptPromise::Cast(script_state,
                               V8String(script_state->GetIsolate(), "done"));
  exception_state.ThrowDOMException(kInvalidStateError,
                                    "Thrown from the native implementation.");
  return ScriptPromise();
}

ScriptPromise Internals::promiseCheckWithoutExceptionState(
    ScriptState* script_state,
    const Dictionary& arg1,
    const String& arg2,
    const Vector<String>& arg3) {
  return ScriptPromise::Cast(script_state,
                             V8String(script_state->GetIsolate(), "done"));
}

ScriptPromise Internals::promiseCheckRange(ScriptState* script_state,
                                           long arg1) {
  return ScriptPromise::Cast(script_state,
                             V8String(script_state->GetIsolate(), "done"));
}

ScriptPromise Internals::promiseCheckOverload(ScriptState* script_state,
                                              Location*) {
  return ScriptPromise::Cast(script_state,
                             V8String(script_state->GetIsolate(), "done"));
}

ScriptPromise Internals::promiseCheckOverload(ScriptState* script_state,
                                              Document*) {
  return ScriptPromise::Cast(script_state,
                             V8String(script_state->GetIsolate(), "done"));
}

ScriptPromise Internals::promiseCheckOverload(ScriptState* script_state,
                                              Location*,
                                              long,
                                              long) {
  return ScriptPromise::Cast(script_state,
                             V8String(script_state->GetIsolate(), "done"));
}

DEFINE_TRACE(Internals) {
  visitor->Trace(runtime_flags_);
  visitor->Trace(document_);
}

void Internals::setValueForUser(HTMLInputElement* element,
                                const String& value) {
  element->SetValueForUser(value);
}

String Internals::textSurroundingNode(Node* node,
                                      int x,
                                      int y,
                                      unsigned long max_length) {
  if (!node)
    return String();

  // VisiblePosition and SurroundingText must be created with clean layout.
  node->GetDocument().UpdateStyleAndLayoutIgnorePendingStylesheets();
  DocumentLifecycle::DisallowTransitionScope disallow_transition(
      node->GetDocument().Lifecycle());

  if (!node->GetLayoutObject())
    return String();
  blink::WebPoint point(x, y);
  SurroundingText surrounding_text(
      CreateVisiblePosition(node->GetLayoutObject()->PositionForPoint(
                                static_cast<IntPoint>(point)))
          .DeepEquivalent()
          .ParentAnchoredEquivalent(),
      max_length);
  return surrounding_text.Content();
}

void Internals::setFocused(bool focused) {
  if (!GetFrame())
    return;

  GetFrame()->GetPage()->GetFocusController().SetFocused(focused);
}

void Internals::setInitialFocus(bool reverse) {
  if (!GetFrame())
    return;

  GetFrame()->GetDocument()->ClearFocusedElement();
  GetFrame()->GetPage()->GetFocusController().SetInitialFocus(
      reverse ? kWebFocusTypeBackward : kWebFocusTypeForward);
}

bool Internals::ignoreLayoutWithPendingStylesheets(Document* document) {
  DCHECK(document);
  return document->IgnoreLayoutWithPendingStylesheets();
}

void Internals::setNetworkConnectionInfoOverride(
    bool on_line,
    const String& type,
    double downlink_max_mbps,
    ExceptionState& exception_state) {
  WebConnectionType webtype;
  if (type == "cellular2g") {
    webtype = kWebConnectionTypeCellular2G;
  } else if (type == "cellular3g") {
    webtype = kWebConnectionTypeCellular3G;
  } else if (type == "cellular4g") {
    webtype = kWebConnectionTypeCellular4G;
  } else if (type == "bluetooth") {
    webtype = kWebConnectionTypeBluetooth;
  } else if (type == "ethernet") {
    webtype = kWebConnectionTypeEthernet;
  } else if (type == "wifi") {
    webtype = kWebConnectionTypeWifi;
  } else if (type == "wimax") {
    webtype = kWebConnectionTypeWimax;
  } else if (type == "other") {
    webtype = kWebConnectionTypeOther;
  } else if (type == "none") {
    webtype = kWebConnectionTypeNone;
  } else if (type == "unknown") {
    webtype = kWebConnectionTypeUnknown;
  } else {
    exception_state.ThrowDOMException(
        kNotFoundError,
        ExceptionMessages::FailedToEnumerate("connection type", type));
    return;
  }
  GetNetworkStateNotifier().SetOverride(on_line, webtype, downlink_max_mbps);
}

void Internals::clearNetworkConnectionInfoOverride() {
  GetNetworkStateNotifier().ClearOverride();
}

unsigned Internals::countHitRegions(CanvasRenderingContext* context) {
  return context->HitRegionsCount();
}

bool Internals::isInCanvasFontCache(Document* document,
                                    const String& font_string) {
  return document->GetCanvasFontCache()->IsInCache(font_string);
}

unsigned Internals::canvasFontCacheMaxFonts() {
  return CanvasFontCache::MaxFonts();
}

void Internals::setScrollChain(ScrollState* scroll_state,
                               const HeapVector<Member<Element>>& elements,
                               ExceptionState&) {
  std::deque<int> scroll_chain;
  for (size_t i = 0; i < elements.size(); ++i)
    scroll_chain.push_back(DOMNodeIds::IdForNode(elements[i].Get()));
  scroll_state->SetScrollChain(scroll_chain);
}

void Internals::forceBlinkGCWithoutV8GC() {
  ThreadState::Current()->SetGCState(ThreadState::kFullGCScheduled);
}

String Internals::selectedHTMLForClipboard() {
  if (!GetFrame())
    return String();

  // Selection normalization and markup generation require clean layout.
  GetFrame()->GetDocument()->UpdateStyleAndLayoutIgnorePendingStylesheets();

  return GetFrame()->Selection().SelectedHTMLForClipboard();
}

String Internals::selectedTextForClipboard() {
  if (!GetFrame() || !GetFrame()->GetDocument())
    return String();

  // Clean layout is required for extracting plain text from selection.
  GetFrame()->GetDocument()->UpdateStyleAndLayoutIgnorePendingStylesheets();

  return GetFrame()->Selection().SelectedTextForClipboard();
}

void Internals::setVisualViewportOffset(int x, int y) {
  if (!GetFrame())
    return;

  GetFrame()->GetPage()->GetVisualViewport().SetLocation(FloatPoint(x, y));
}

int Internals::visualViewportHeight() {
  if (!GetFrame())
    return 0;

  return ExpandedIntSize(
             GetFrame()->GetPage()->GetVisualViewport().VisibleRect().Size())
      .Height();
}

int Internals::visualViewportWidth() {
  if (!GetFrame())
    return 0;

  return ExpandedIntSize(
             GetFrame()->GetPage()->GetVisualViewport().VisibleRect().Size())
      .Width();
}

float Internals::visualViewportScrollX() {
  if (!GetFrame())
    return 0;

  return GetFrame()->View()->GetScrollableArea()->GetScrollOffset().Width();
}

float Internals::visualViewportScrollY() {
  if (!GetFrame())
    return 0;

  return GetFrame()->View()->GetScrollableArea()->GetScrollOffset().Height();
}

bool Internals::isUseCounted(Document* document, uint32_t feature) {
  if (feature >= UseCounter::kNumberOfFeatures)
    return false;
  return UseCounter::IsCounted(*document,
                               static_cast<UseCounter::Feature>(feature));
}

bool Internals::isCSSPropertyUseCounted(Document* document,
                                        const String& property_name) {
  return UseCounter::IsCounted(*document, property_name);
}

bool Internals::isAnimatedCSSPropertyUseCounted(Document* document,
                                                const String& property_name) {
  return UseCounter::IsCountedAnimatedCSS(*document, property_name);
}

ScriptPromise Internals::observeUseCounter(ScriptState* script_state,
                                           Document* document,
                                           uint32_t feature) {
  ScriptPromiseResolver* resolver = ScriptPromiseResolver::Create(script_state);
  ScriptPromise promise = resolver->Promise();
  if (feature >= UseCounter::kNumberOfFeatures) {
    resolver->Reject();
    return promise;
  }

  UseCounter::Feature use_counter_feature =
      static_cast<UseCounter::Feature>(feature);
  if (UseCounter::IsCounted(*document, use_counter_feature)) {
    resolver->Resolve();
    return promise;
  }

  Page* page = document->GetPage();
  if (!page) {
    resolver->Reject();
    return promise;
  }

  page->GetUseCounter().AddObserver(
      new UseCounterObserverImpl(resolver, use_counter_feature));
  return promise;
}

String Internals::unscopableAttribute() {
  return "unscopableAttribute";
}

String Internals::unscopableMethod() {
  return "unscopableMethod";
}

ClientRectList* Internals::focusRingRects(Element* element) {
  Vector<LayoutRect> rects;
  if (element && element->GetLayoutObject())
    element->GetLayoutObject()->AddOutlineRects(
        rects, LayoutPoint(), LayoutObject::kIncludeBlockVisualOverflow);
  return ClientRectList::Create(rects);
}

ClientRectList* Internals::outlineRects(Element* element) {
  Vector<LayoutRect> rects;
  if (element && element->GetLayoutObject())
    element->GetLayoutObject()->AddOutlineRects(
        rects, LayoutPoint(), LayoutObject::kDontIncludeBlockVisualOverflow);
  return ClientRectList::Create(rects);
}

void Internals::setCapsLockState(bool enabled) {
  KeyboardEventManager::SetCurrentCapsLockState(
      enabled ? OverrideCapsLockState::kOn : OverrideCapsLockState::kOff);
}

bool Internals::setScrollbarVisibilityInScrollableArea(Node* node,
                                                       bool visible) {
  if (ScrollableArea* scrollable_area = ScrollableAreaForNode(node)) {
    scrollable_area->SetScrollbarsHidden(!visible);
    scrollable_area->GetScrollAnimator().SetScrollbarsVisibleForTesting(
        visible);
    return ScrollbarTheme::GetTheme().UsesOverlayScrollbars();
  }
  return false;
}

double Internals::monotonicTimeToZeroBasedDocumentTime(
    double platform_time,
    ExceptionState& exception_state) {
  return document_->Loader()->GetTiming().MonotonicTimeToZeroBasedDocumentTime(
      platform_time);
}

String Internals::getScrollAnimationState(Node* node) const {
  if (ScrollableArea* scrollable_area = ScrollableAreaForNode(node))
    return scrollable_area->GetScrollAnimator().RunStateAsText();
  return String();
}

String Internals::getProgrammaticScrollAnimationState(Node* node) const {
  if (ScrollableArea* scrollable_area = ScrollableAreaForNode(node))
    return scrollable_area->GetProgrammaticScrollAnimator().RunStateAsText();
  return String();
}

ClientRect* Internals::visualRect(Node* node) {
  if (!node || !node->GetLayoutObject())
    return ClientRect::Create();

  return ClientRect::Create(FloatRect(node->GetLayoutObject()->VisualRect()));
}

void Internals::crash() {
  CHECK(false) << "Intentional crash";
}

void Internals::setIsLowEndDevice(bool is_low_end_device) {
  MemoryCoordinator::SetIsLowEndDeviceForTesting(is_low_end_device);
}

}  // namespace blink
