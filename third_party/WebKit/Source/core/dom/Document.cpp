/*
 * Copyright (C) 1999 Lars Knoll (knoll@kde.org)
 *           (C) 1999 Antti Koivisto (koivisto@kde.org)
 *           (C) 2001 Dirk Mueller (mueller@kde.org)
 *           (C) 2006 Alexey Proskuryakov (ap@webkit.org)
 * Copyright (C) 2004, 2005, 2006, 2007, 2008, 2009, 2011, 2012 Apple Inc. All
 * rights reserved.
 * Copyright (C) 2008, 2009 Torch Mobile Inc. All rights reserved.
 * (http://www.torchmobile.com/)
 * Copyright (C) 2008, 2009, 2011, 2012 Google Inc. All rights reserved.
 * Copyright (C) 2010 Nokia Corporation and/or its subsidiary(-ies)
 * Copyright (C) Research In Motion Limited 2010-2011. All rights reserved.
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

#include "core/dom/Document.h"

#include "bindings/core/v8/ExceptionMessages.h"
#include "bindings/core/v8/ExceptionState.h"
#include "bindings/core/v8/HTMLScriptElementOrSVGScriptElement.h"
#include "bindings/core/v8/ScriptController.h"
#include "bindings/core/v8/SourceLocation.h"
#include "bindings/core/v8/StringOrDictionary.h"
#include "bindings/core/v8/V0CustomElementConstructorBuilder.h"
#include "bindings/core/v8/V8ElementCreationOptions.h"
#include "bindings/core/v8/WindowProxy.h"
#include "core/HTMLElementFactory.h"
#include "core/HTMLElementTypeHelpers.h"
#include "core/HTMLNames.h"
#include "core/SVGElementFactory.h"
#include "core/SVGNames.h"
#include "core/XMLNSNames.h"
#include "core/XMLNames.h"
#include "core/animation/CompositorPendingAnimations.h"
#include "core/animation/DocumentAnimations.h"
#include "core/animation/DocumentTimeline.h"
#include "core/css/CSSFontSelector.h"
#include "core/css/CSSStyleDeclaration.h"
#include "core/css/CSSStyleSheet.h"
#include "core/css/CSSTiming.h"
#include "core/css/FontFaceSet.h"
#include "core/css/MediaQueryMatcher.h"
#include "core/css/PropertyRegistry.h"
#include "core/css/StylePropertySet.h"
#include "core/css/StyleSheetContents.h"
#include "core/css/StyleSheetList.h"
#include "core/css/invalidation/StyleInvalidator.h"
#include "core/css/parser/CSSParser.h"
#include "core/css/resolver/FontBuilder.h"
#include "core/css/resolver/StyleResolver.h"
#include "core/css/resolver/StyleResolverStats.h"
#include "core/dom/AXObjectCache.h"
#include "core/dom/Attr.h"
#include "core/dom/CDATASection.h"
#include "core/dom/ClientRect.h"
#include "core/dom/Comment.h"
#include "core/dom/ContextFeatures.h"
#include "core/dom/DOMImplementation.h"
#include "core/dom/DocumentFragment.h"
#include "core/dom/DocumentParserTiming.h"
#include "core/dom/DocumentType.h"
#include "core/dom/Element.h"
#include "core/dom/ElementCreationOptions.h"
#include "core/dom/ElementDataCache.h"
#include "core/dom/ElementRegistrationOptions.h"
#include "core/dom/ElementTraversal.h"
#include "core/dom/ExceptionCode.h"
#include "core/dom/ExecutionContextTask.h"
#include "core/dom/FrameRequestCallback.h"
#include "core/dom/IntersectionObserverController.h"
#include "core/dom/LayoutTreeBuilderTraversal.h"
#include "core/dom/LiveNodeList.h"
#include "core/dom/MutationObserver.h"
#include "core/dom/NodeChildRemovalTracker.h"
#include "core/dom/NodeComputedStyle.h"
#include "core/dom/NodeFilter.h"
#include "core/dom/NodeIterator.h"
#include "core/dom/NodeRareData.h"
#include "core/dom/NodeTraversal.h"
#include "core/dom/NodeWithIndex.h"
#include "core/dom/NthIndexCache.h"
#include "core/dom/ProcessingInstruction.h"
#include "core/dom/ResizeObserverController.h"
#include "core/dom/ScriptRunner.h"
#include "core/dom/ScriptedAnimationController.h"
#include "core/dom/ScriptedIdleTaskController.h"
#include "core/dom/SelectorQuery.h"
#include "core/dom/StaticNodeList.h"
#include "core/dom/StyleChangeReason.h"
#include "core/dom/StyleEngine.h"
#include "core/dom/TaskRunnerHelper.h"
#include "core/dom/TouchList.h"
#include "core/dom/TransformSource.h"
#include "core/dom/TreeWalker.h"
#include "core/dom/VisitedLinkState.h"
#include "core/dom/XMLDocument.h"
#include "core/dom/custom/CustomElement.h"
#include "core/dom/custom/CustomElementDefinition.h"
#include "core/dom/custom/CustomElementDescriptor.h"
#include "core/dom/custom/CustomElementRegistry.h"
#include "core/dom/custom/V0CustomElementMicrotaskRunQueue.h"
#include "core/dom/custom/V0CustomElementRegistrationContext.h"
#include "core/dom/shadow/ElementShadow.h"
#include "core/dom/shadow/FlatTreeTraversal.h"
#include "core/dom/shadow/ShadowRoot.h"
#include "core/editing/EditingUtilities.h"
#include "core/editing/FrameSelection.h"
#include "core/editing/markers/DocumentMarkerController.h"
#include "core/editing/serializers/Serialization.h"
#include "core/editing/spellcheck/SpellChecker.h"
#include "core/events/BeforeUnloadEvent.h"
#include "core/events/Event.h"
#include "core/events/EventFactory.h"
#include "core/events/EventListener.h"
#include "core/events/HashChangeEvent.h"
#include "core/events/PageTransitionEvent.h"
#include "core/events/ScopedEventQueue.h"
#include "core/events/VisualViewportResizeEvent.h"
#include "core/events/VisualViewportScrollEvent.h"
#include "core/frame/ContentSettingsClient.h"
#include "core/frame/DOMTimer.h"
#include "core/frame/DOMVisualViewport.h"
#include "core/frame/EventHandlerRegistry.h"
#include "core/frame/FrameConsole.h"
#include "core/frame/FrameView.h"
#include "core/frame/History.h"
#include "core/frame/HostsUsingFeatures.h"
#include "core/frame/LocalDOMWindow.h"
#include "core/frame/LocalFrame.h"
#include "core/frame/LocalFrameClient.h"
#include "core/frame/PerformanceMonitor.h"
#include "core/frame/Settings.h"
#include "core/frame/csp/ContentSecurityPolicy.h"
#include "core/html/DocumentNameCollection.h"
#include "core/html/HTMLAllCollection.h"
#include "core/html/HTMLAnchorElement.h"
#include "core/html/HTMLBaseElement.h"
#include "core/html/HTMLBodyElement.h"
#include "core/html/HTMLCanvasElement.h"
#include "core/html/HTMLCollection.h"
#include "core/html/HTMLDialogElement.h"
#include "core/html/HTMLDocument.h"
#include "core/html/HTMLFrameOwnerElement.h"
#include "core/html/HTMLHeadElement.h"
#include "core/html/HTMLHtmlElement.h"
#include "core/html/HTMLIFrameElement.h"
#include "core/html/HTMLInputElement.h"
#include "core/html/HTMLLinkElement.h"
#include "core/html/HTMLMetaElement.h"
#include "core/html/HTMLPlugInElement.h"
#include "core/html/HTMLScriptElement.h"
#include "core/html/HTMLTemplateElement.h"
#include "core/html/HTMLTitleElement.h"
#include "core/html/PluginDocument.h"
#include "core/html/WindowNameCollection.h"
#include "core/html/canvas/CanvasContextCreationAttributes.h"
#include "core/html/canvas/CanvasFontCache.h"
#include "core/html/canvas/CanvasRenderingContext.h"
#include "core/html/forms/FormController.h"
#include "core/html/imports/HTMLImportLoader.h"
#include "core/html/imports/HTMLImportsController.h"
#include "core/html/parser/HTMLDocumentParser.h"
#include "core/html/parser/HTMLParserIdioms.h"
#include "core/html/parser/NestingLevelIncrementer.h"
#include "core/html/parser/TextResourceDecoder.h"
#include "core/input/EventHandler.h"
#include "core/inspector/ConsoleMessage.h"
#include "core/inspector/InspectorTraceEvents.h"
#include "core/inspector/MainThreadDebugger.h"
#include "core/layout/HitTestCanvasResult.h"
#include "core/layout/HitTestResult.h"
#include "core/layout/LayoutPart.h"
#include "core/layout/LayoutView.h"
#include "core/layout/TextAutosizer.h"
#include "core/layout/api/LayoutPartItem.h"
#include "core/layout/api/LayoutViewItem.h"
#include "core/layout/compositing/PaintLayerCompositor.h"
#include "core/loader/CookieJar.h"
#include "core/loader/DocumentLoader.h"
#include "core/loader/FrameFetchContext.h"
#include "core/loader/FrameLoader.h"
#include "core/loader/ImageLoader.h"
#include "core/loader/NavigationScheduler.h"
#include "core/loader/PrerendererClient.h"
#include "core/loader/appcache/ApplicationCacheHost.h"
#include "core/page/ChromeClient.h"
#include "core/page/EventWithHitTestResults.h"
#include "core/page/FocusController.h"
#include "core/page/FrameTree.h"
#include "core/page/Page.h"
#include "core/page/PointerLockController.h"
#include "core/page/scrolling/RootScrollerController.h"
#include "core/page/scrolling/ScrollStateCallback.h"
#include "core/page/scrolling/ScrollingCoordinator.h"
#include "core/page/scrolling/SnapCoordinator.h"
#include "core/page/scrolling/TopDocumentRootScrollerController.h"
#include "core/probe/CoreProbes.h"
#include "core/svg/SVGDocumentExtensions.h"
#include "core/svg/SVGScriptElement.h"
#include "core/svg/SVGTitleElement.h"
#include "core/svg/SVGUseElement.h"
#include "core/timing/DOMWindowPerformance.h"
#include "core/timing/Performance.h"
#include "core/workers/SharedWorkerRepositoryClient.h"
#include "core/xml/parser/XMLDocumentParser.h"
#include "platform/DateComponents.h"
#include "platform/EventDispatchForbiddenScope.h"
#include "platform/Histogram.h"
#include "platform/InstanceCounters.h"
#include "platform/Language.h"
#include "platform/LengthFunctions.h"
#include "platform/PluginScriptForbiddenScope.h"
#include "platform/RuntimeEnabledFeatures.h"
#include "platform/ScriptForbiddenScope.h"
#include "platform/WebFrameScheduler.h"
#include "platform/bindings/DOMDataStore.h"
#include "platform/bindings/Microtask.h"
#include "platform/bindings/V8DOMWrapper.h"
#include "platform/bindings/V8PerIsolateData.h"
#include "platform/instrumentation/tracing/TraceEvent.h"
#include "platform/loader/fetch/ResourceFetcher.h"
#include "platform/network/ContentSecurityPolicyParsers.h"
#include "platform/network/HTTPParsers.h"
#include "platform/network/NetworkStateNotifier.h"
#include "platform/scheduler/child/web_scheduler.h"
#include "platform/scroll/ScrollbarTheme.h"
#include "platform/text/PlatformLocale.h"
#include "platform/text/SegmentedString.h"
#include "platform/weborigin/OriginAccessEntry.h"
#include "platform/weborigin/SchemeRegistry.h"
#include "platform/weborigin/SecurityOrigin.h"
#include "platform/wtf/AutoReset.h"
#include "platform/wtf/CurrentTime.h"
#include "platform/wtf/DateMath.h"
#include "platform/wtf/Functional.h"
#include "platform/wtf/HashFunctions.h"
#include "platform/wtf/PtrUtil.h"
#include "platform/wtf/StdLibExtras.h"
#include "platform/wtf/text/CharacterNames.h"
#include "platform/wtf/text/StringBuffer.h"
#include "platform/wtf/text/TextEncodingRegistry.h"
#include "public/platform/InterfaceProvider.h"
#include "public/platform/Platform.h"
#include "public/platform/WebAddressSpace.h"
#include "public/platform/WebPrerenderingSupport.h"
#include "public/platform/modules/sensitive_input_visibility/sensitive_input_visibility_service.mojom-blink.h"
#include "public/platform/site_engagement.mojom-blink.h"

#include <memory>

#ifndef NDEBUG
using WeakDocumentSet =
    blink::PersistentHeapHashSet<blink::WeakMember<blink::Document>>;
static WeakDocumentSet& liveDocumentSet();
#endif

namespace blink {

using namespace HTMLNames;

static const unsigned kCMaxWriteRecursionDepth = 21;

// This amount of time must have elapsed before we will even consider scheduling
// a layout without a delay.
// FIXME: For faster machines this value can really be lowered to 200.  250 is
// adequate, but a little high for dual G5s. :)
static const int kCLayoutScheduleThreshold = 250;

// After a document has been committed for this time, it can create a history
// entry even if the user hasn't interacted with the document.
static const int kElapsedTimeForHistoryEntryWithoutUserGestureMS = 5000;

// DOM Level 2 says (letters added):
//
// a) Name start characters must have one of the categories Ll, Lu, Lo, Lt, Nl.
// b) Name characters other than Name-start characters must have one of the
//    categories Mc, Me, Mn, Lm, or Nd.
// c) Characters in the compatibility area (i.e. with character code greater
//    than #xF900 and less than #xFFFE) are not allowed in XML names.
// d) Characters which have a font or compatibility decomposition (i.e. those
//    with a "compatibility formatting tag" in field 5 of the database -- marked
//    by field 5 beginning with a "<") are not allowed.
// e) The following characters are treated as name-start characters rather than
//    name characters, because the property file classifies them as Alphabetic:
//    [#x02BB-#x02C1], #x0559, #x06E5, #x06E6.
// f) Characters #x20DD-#x20E0 are excluded (in accordance with Unicode, section
//    5.14).
// g) Character #x00B7 is classified as an extender, because the property list
//    so identifies it.
// h) Character #x0387 is added as a name character, because #x00B7 is its
//    canonical equivalent.
// i) Characters ':' and '_' are allowed as name-start characters.
// j) Characters '-' and '.' are allowed as name characters.
//
// It also contains complete tables. If we decide it's better, we could include
// those instead of the following code.

static inline bool IsValidNameStart(UChar32 c) {
  // rule (e) above
  if ((c >= 0x02BB && c <= 0x02C1) || c == 0x559 || c == 0x6E5 || c == 0x6E6)
    return true;

  // rule (i) above
  if (c == ':' || c == '_')
    return true;

  // rules (a) and (f) above
  const uint32_t kNameStartMask =
      WTF::Unicode::kLetter_Lowercase | WTF::Unicode::kLetter_Uppercase |
      WTF::Unicode::kLetter_Other | WTF::Unicode::kLetter_Titlecase |
      WTF::Unicode::kNumber_Letter;
  if (!(WTF::Unicode::Category(c) & kNameStartMask))
    return false;

  // rule (c) above
  if (c >= 0xF900 && c < 0xFFFE)
    return false;

  // rule (d) above
  WTF::Unicode::CharDecompositionType decomp_type =
      WTF::Unicode::DecompositionType(c);
  if (decomp_type == WTF::Unicode::kDecompositionFont ||
      decomp_type == WTF::Unicode::kDecompositionCompat)
    return false;

  return true;
}

static inline bool IsValidNamePart(UChar32 c) {
  // rules (a), (e), and (i) above
  if (IsValidNameStart(c))
    return true;

  // rules (g) and (h) above
  if (c == 0x00B7 || c == 0x0387)
    return true;

  // rule (j) above
  if (c == '-' || c == '.')
    return true;

  // rules (b) and (f) above
  const uint32_t kOtherNamePartMask =
      WTF::Unicode::kMark_NonSpacing | WTF::Unicode::kMark_Enclosing |
      WTF::Unicode::kMark_SpacingCombining | WTF::Unicode::kLetter_Modifier |
      WTF::Unicode::kNumber_DecimalDigit;
  if (!(WTF::Unicode::Category(c) & kOtherNamePartMask))
    return false;

  // rule (c) above
  if (c >= 0xF900 && c < 0xFFFE)
    return false;

  // rule (d) above
  WTF::Unicode::CharDecompositionType decomp_type =
      WTF::Unicode::DecompositionType(c);
  if (decomp_type == WTF::Unicode::kDecompositionFont ||
      decomp_type == WTF::Unicode::kDecompositionCompat)
    return false;

  return true;
}

// Tests whether |name| is something the HTML parser would accept as a
// tag name.
template <typename CharType>
static inline bool IsValidElementNamePerHTMLParser(const CharType* characters,
                                                   unsigned length) {
  CharType c = characters[0] | 0x20;
  if (!('a' <= c && c < 'z'))
    return false;

  for (unsigned i = 1; i < length; ++i) {
    c = characters[i];
    if (c == '\t' || c == '\n' || c == '\f' || c == '\r' || c == ' ' ||
        c == '/' || c == '>')
      return false;
  }

  return true;
}

static bool IsValidElementNamePerHTMLParser(const String& name) {
  unsigned length = name.length();
  if (!length)
    return false;

  if (name.Is8Bit()) {
    const LChar* characters = name.Characters8();
    return IsValidElementNamePerHTMLParser(characters, length);
  }
  const UChar* characters = name.Characters16();
  return IsValidElementNamePerHTMLParser(characters, length);
}

// Tests whether |name| is a valid name per DOM spec. Also checks
// whether the HTML parser would accept this element name and counts
// cases of mismatches.
static bool IsValidElementName(const LocalDOMWindow* window,
                               const String& name) {
  bool is_valid_dom_name = Document::IsValidName(name);
  bool is_valid_html_name = IsValidElementNamePerHTMLParser(name);
  if (UNLIKELY(is_valid_html_name != is_valid_dom_name && window)) {
    UseCounter::Count(window->GetFrame(),
                      is_valid_dom_name
                          ? UseCounter::kElementNameDOMValidHTMLParserInvalid
                          : UseCounter::kElementNameDOMInvalidHTMLParserValid);
  }
  return is_valid_dom_name;
}

static bool AcceptsEditingFocus(const Element& element) {
  DCHECK(HasEditableStyle(element));

  return element.GetDocument().GetFrame() && RootEditableElement(element);
}

uint64_t Document::global_tree_version_ = 0;

static bool g_threaded_parsing_enabled_for_testing = true;

// This doesn't work with non-Document ExecutionContext.
static void RunAutofocusTask(ExecutionContext* context) {
  // Document lifecycle check is done in Element::focus()
  if (!context)
    return;

  Document* document = ToDocument(context);
  if (Element* element = document->AutofocusElement()) {
    document->SetAutofocusElement(0);
    element->focus();
  }
}

static void RecordLoadReasonToHistogram(WouldLoadReason reason) {
  // TODO(dcheng): Make EnumerationHistogram work with scoped enums.
  DEFINE_STATIC_LOCAL(EnumerationHistogram, unseen_frame_histogram,
                      ("Navigation.DeferredDocumentLoading.StatesV4",
                       static_cast<int>(WouldLoadReason::kCount)));
  unseen_frame_histogram.Count(static_cast<int>(reason));
}

class Document::NetworkStateObserver final
    : public GarbageCollectedFinalized<Document::NetworkStateObserver>,
      public NetworkStateNotifier::NetworkStateObserver,
      public ContextLifecycleObserver {
  USING_GARBAGE_COLLECTED_MIXIN(Document::NetworkStateObserver);

 public:
  explicit NetworkStateObserver(Document& document)
      : ContextLifecycleObserver(&document) {
    GetNetworkStateNotifier().AddOnLineObserver(
        this,
        TaskRunnerHelper::Get(TaskType::kNetworking, GetExecutionContext()));
  }

  void OnLineStateChange(bool on_line) override {
    AtomicString event_name =
        on_line ? EventTypeNames::online : EventTypeNames::offline;
    Document* document = ToDocument(GetExecutionContext());
    if (!document->domWindow())
      return;
    document->domWindow()->DispatchEvent(Event::Create(event_name));
    probe::networkStateChanged(document->GetFrame(), on_line);
  }

  void ContextDestroyed(ExecutionContext* context) override {
    UnregisterAsObserver(context);
  }

  void UnregisterAsObserver(ExecutionContext* context) {
    DCHECK(context);
    GetNetworkStateNotifier().RemoveOnLineObserver(
        this, TaskRunnerHelper::Get(TaskType::kNetworking, context));
  }

  DEFINE_INLINE_VIRTUAL_TRACE() { ContextLifecycleObserver::Trace(visitor); }
};

Document::Document(const DocumentInit& initializer,
                   DocumentClassFlags document_classes)
    : ContainerNode(0, kCreateDocument),
      TreeScope(*this),
      has_nodes_with_placeholder_style_(false),
      evaluate_media_queries_on_style_recalc_(false),
      pending_sheet_layout_(kNoLayoutWithPendingSheets),
      frame_(initializer.GetFrame()),
      // TODO(dcheng): Why does this need both a LocalFrame and LocalDOMWindow
      // pointer?
      dom_window_(frame_ ? frame_->DomWindow() : nullptr),
      imports_controller_(this, initializer.ImportsController()),
      context_features_(ContextFeatures::DefaultSwitch()),
      well_formed_(false),
      implementation_(this, nullptr),
      printing_(kNotPrinting),
      paginated_for_screen_(false),
      compatibility_mode_(kNoQuirksMode),
      compatibility_mode_locked_(false),
      has_autofocused_(false),
      clear_focused_element_timer_(
          TaskRunnerHelper::Get(TaskType::kUnspecedTimer, this),
          this,
          &Document::ClearFocusedElementTimerFired),
      dom_tree_version_(++global_tree_version_),
      style_version_(0),
      listener_types_(0),
      mutation_observer_types_(0),
      style_engine_(this, nullptr),
      style_sheet_list_(this, nullptr),
      visited_link_state_(VisitedLinkState::Create(*this)),
      visually_ordered_(false),
      ready_state_(kComplete),
      parsing_state_(kFinishedParsing),
      goto_anchor_needed_after_stylesheets_load_(false),
      contains_validity_style_rules_(false),
      contains_plugins_(false),
      ignore_destructive_write_count_(0),
      throw_on_dynamic_markup_insertion_count_(0),
      markers_(new DocumentMarkerController(*this)),
      update_focus_appearance_timer_(
          TaskRunnerHelper::Get(TaskType::kUnspecedTimer, this),
          this,
          &Document::UpdateFocusAppearanceTimerFired),
      css_target_(nullptr),
      load_event_progress_(kLoadEventCompleted),
      start_time_(CurrentTime()),
      script_runner_(ScriptRunner::Create(this)),
      xml_version_("1.0"),
      xml_standalone_(kStandaloneUnspecified),
      has_xml_declaration_(0),
      design_mode_(false),
      is_running_exec_command_(false),
      has_annotated_regions_(false),
      annotated_regions_dirty_(false),
      document_classes_(document_classes),
      is_view_source_(false),
      saw_elements_in_known_namespaces_(false),
      is_srcdoc_document_(false),
      is_mobile_document_(false),
      layout_view_(0),
      context_document_(initializer.ContextDocument()),
      has_fullscreen_supplement_(false),
      load_event_delay_count_(0),
      load_event_delay_timer_(
          TaskRunnerHelper::Get(TaskType::kNetworking, this),
          this,
          &Document::LoadEventDelayTimerFired),
      plugin_loading_timer_(
          TaskRunnerHelper::Get(TaskType::kUnspecedLoading, this),
          this,
          &Document::PluginLoadingTimerFired),
      document_timing_(*this),
      write_recursion_is_too_deep_(false),
      write_recursion_depth_(0),
      registration_context_(initializer.RegistrationContext(this)),
      element_data_cache_clear_timer_(
          TaskRunnerHelper::Get(TaskType::kUnspecedTimer, this),
          this,
          &Document::ElementDataCacheClearTimerFired),
      timeline_(DocumentTimeline::Create(this)),
      compositor_pending_animations_(new CompositorPendingAnimations(*this)),
      template_document_host_(nullptr),
      did_associate_form_controls_timer_(
          TaskRunnerHelper::Get(TaskType::kUnspecedLoading, this),
          this,
          &Document::DidAssociateFormControlsTimerFired),
      timers_(TaskRunnerHelper::Get(TaskType::kTimer, this)),
      has_viewport_units_(false),
      parser_sync_policy_(kAllowAsynchronousParsing),
      node_count_(0),
      would_load_reason_(WouldLoadReason::kInvalid),
      password_count_(0),
      engagement_level_(mojom::blink::EngagementLevel::NONE) {
  if (frame_) {
    DCHECK(frame_->GetPage());
    ProvideContextFeaturesToDocumentFrom(*this, *frame_->GetPage());

    fetcher_ = frame_->Loader().GetDocumentLoader()->Fetcher();
    FrameFetchContext::ProvideDocumentToContext(fetcher_->Context(), this);

    // TODO(dcheng): Why does this need to check that DOMWindow is non-null?
    CustomElementRegistry* registry =
        frame_->DomWindow() ? frame_->DomWindow()->MaybeCustomElements()
                            : nullptr;
    if (registry && registration_context_)
      registry->Entangle(registration_context_);
  } else if (imports_controller_) {
    fetcher_ = FrameFetchContext::CreateFetcherFromDocument(this);
  } else {
    fetcher_ = ResourceFetcher::Create(nullptr);
  }
  DCHECK(fetcher_);

  root_scroller_controller_ = RootScrollerController::Create(*this);

  // We depend on the url getting immediately set in subframes, but we
  // also depend on the url NOT getting immediately set in opened windows.
  // See fast/dom/early-frame-url.html
  // and fast/dom/location-new-window-no-crash.html, respectively.
  // FIXME: Can/should we unify this behavior?
  if (initializer.ShouldSetURL())
    SetURL(initializer.Url());

  InitSecurityContext(initializer);

  InitDNSPrefetch();

  InstanceCounters::IncrementCounter(InstanceCounters::kDocumentCounter);

  lifecycle_.AdvanceTo(DocumentLifecycle::kInactive);

  // Since CSSFontSelector requires Document::m_fetcher and StyleEngine owns
  // CSSFontSelector, need to initialize m_styleEngine after initializing
  // m_fetcher.
  style_engine_ = StyleEngine::Create(*this);

  // The parent's parser should be suspended together with all the other
  // objects, else this new Document would have a new ExecutionContext which
  // suspended state would not match the one from the parent, and could start
  // loading resources ignoring the defersLoading flag.
  DCHECK(!ParentDocument() || !ParentDocument()->IsContextSuspended());

#ifndef NDEBUG
  liveDocumentSet().insert(this);
#endif
}

Document::~Document() {
  DCHECK(GetLayoutViewItem().IsNull());
  DCHECK(!ParentTreeScope());
  // If a top document with a cache, verify that it was comprehensively
  // cleared during detach.
  DCHECK(!ax_object_cache_);
  InstanceCounters::DecrementCounter(InstanceCounters::kDocumentCounter);
}

SelectorQueryCache& Document::GetSelectorQueryCache() {
  if (!selector_query_cache_)
    selector_query_cache_ = WTF::MakeUnique<SelectorQueryCache>();
  return *selector_query_cache_;
}

MediaQueryMatcher& Document::GetMediaQueryMatcher() {
  if (!media_query_matcher_)
    media_query_matcher_ = MediaQueryMatcher::Create(*this);
  return *media_query_matcher_;
}

void Document::MediaQueryAffectingValueChanged() {
  GetStyleEngine().MediaQueryAffectingValueChanged();
  if (NeedsLayoutTreeUpdate())
    evaluate_media_queries_on_style_recalc_ = true;
  else
    EvaluateMediaQueryList();
  probe::mediaQueryResultChanged(this);
}

void Document::SetCompatibilityMode(CompatibilityMode mode) {
  if (compatibility_mode_locked_ || mode == compatibility_mode_)
    return;
  compatibility_mode_ = mode;
  GetSelectorQueryCache().Invalidate();
}

String Document::compatMode() const {
  return InQuirksMode() ? "BackCompat" : "CSS1Compat";
}

void Document::SetDoctype(DocumentType* doc_type) {
  // This should never be called more than once.
  DCHECK(!doc_type_ || !doc_type);
  doc_type_ = doc_type;
  if (doc_type_) {
    this->AdoptIfNeeded(*doc_type_);
    if (doc_type_->publicId().StartsWith("-//wapforum//dtd xhtml mobile 1.",
                                         kTextCaseASCIIInsensitive)) {
      is_mobile_document_ = true;
      style_engine_->ViewportRulesChanged();
    }
  }
}

DOMImplementation& Document::implementation() {
  if (!implementation_)
    implementation_ = DOMImplementation::Create(*this);
  return *implementation_;
}

bool Document::HasAppCacheManifest() const {
  return isHTMLHtmlElement(documentElement()) &&
         documentElement()->hasAttribute(manifestAttr);
}

Location* Document::location() const {
  if (!GetFrame())
    return 0;

  return domWindow()->location();
}

void Document::ChildrenChanged(const ChildrenChange& change) {
  ContainerNode::ChildrenChanged(change);
  document_element_ = ElementTraversal::FirstWithin(*this);

  // For non-HTML documents the willInsertBody notification won't happen
  // so we resume as soon as we have a document element. Even for XHTML
  // documents there may never be a <body> (since the parser won't always
  // insert one), so we resume here too. That does mean XHTML documents make
  // frames when there's only a <head>, but such documents are pretty rare.
  if (document_element_ && !IsHTMLDocument())
    BeginLifecycleUpdatesIfRenderingReady();
}

void Document::setRootScroller(Element* new_scroller, ExceptionState&) {
  root_scroller_controller_->Set(new_scroller);
}

Element* Document::rootScroller() const {
  return root_scroller_controller_->Get();
}

bool Document::IsInMainFrame() const {
  return GetFrame() && GetFrame()->IsMainFrame();
}

AtomicString Document::ConvertLocalName(const AtomicString& name) {
  return IsHTMLDocument() ? name.LowerASCII() : name;
}

// https://dom.spec.whatwg.org/#dom-document-createelement
Element* Document::createElement(const LocalDOMWindow* window,
                                 const AtomicString& name,
                                 ExceptionState& exception_state) {
  if (!IsValidElementName(window, name)) {
    exception_state.ThrowDOMException(
        kInvalidCharacterError,
        "The tag name provided ('" + name + "') is not a valid name.");
    return nullptr;
  }

  if (IsXHTMLDocument() || IsHTMLDocument()) {
    // 2. If the context object is an HTML document, let localName be
    // converted to ASCII lowercase.
    AtomicString local_name = ConvertLocalName(name);
    if (CustomElement::ShouldCreateCustomElement(local_name)) {
      return CustomElement::CreateCustomElementSync(
          *this,
          QualifiedName(g_null_atom, local_name, HTMLNames::xhtmlNamespaceURI));
    }
    return HTMLElementFactory::createHTMLElement(local_name, *this,
                                                 kCreatedByCreateElement);
  }
  return Element::Create(QualifiedName(g_null_atom, name, g_null_atom), this);
}

String GetTypeExtension(Document* document,
                        const StringOrDictionary& string_or_options,
                        ExceptionState& exception_state) {
  if (string_or_options.isNull())
    return g_empty_string;

  if (string_or_options.isString()) {
    UseCounter::Count(document,
                      UseCounter::kDocumentCreateElement2ndArgStringHandling);
    return string_or_options.getAsString();
  }

  if (string_or_options.isDictionary()) {
    Dictionary dict = string_or_options.getAsDictionary();
    ElementCreationOptions impl;
    V8ElementCreationOptions::toImpl(dict.GetIsolate(), dict.V8Value(), impl,
                                     exception_state);
    if (exception_state.HadException())
      return g_empty_string;

    if (impl.hasIs())
      return impl.is();
  }

  return g_empty_string;
}

// https://dom.spec.whatwg.org/#dom-document-createelement
Element* Document::createElement(const LocalDOMWindow* window,
                                 const AtomicString& local_name,
                                 const StringOrDictionary& string_or_options,
                                 ExceptionState& exception_state) {
  // 1. If localName does not match Name production, throw InvalidCharacterError
  if (!IsValidElementName(window, local_name)) {
    exception_state.ThrowDOMException(
        kInvalidCharacterError,
        "The tag name provided ('" + local_name + "') is not a valid name.");
    return nullptr;
  }

  // 2. localName converted to ASCII lowercase
  const AtomicString& converted_local_name = ConvertLocalName(local_name);

  bool is_v1 = string_or_options.isDictionary() || !RegistrationContext();
  bool create_v1_builtin =
      string_or_options.isDictionary() &&
      RuntimeEnabledFeatures::customElementsBuiltinEnabled();
  bool should_create_builtin =
      create_v1_builtin || string_or_options.isString();

  // 3.
  const AtomicString& is =
      AtomicString(GetTypeExtension(this, string_or_options, exception_state));
  const AtomicString& name = should_create_builtin ? is : converted_local_name;

  // 4. Let definition be result of lookup up custom element definition
  CustomElementDefinition* definition = nullptr;
  if (is_v1) {
    // Is the runtime flag enabled for customized builtin elements?
    const CustomElementDescriptor desc =
        RuntimeEnabledFeatures::customElementsBuiltinEnabled()
            ? CustomElementDescriptor(name, converted_local_name)
            : CustomElementDescriptor(converted_local_name,
                                      converted_local_name);
    if (CustomElementRegistry* registry = CustomElement::Registry(*this))
      definition = registry->DefinitionFor(desc);

    // 5. If 'is' is non-null and definition is null, throw NotFoundError
    // TODO(yurak): update when https://github.com/w3c/webcomponents/issues/608
    //              is resolved
    if (!definition && create_v1_builtin) {
      exception_state.ThrowDOMException(kNotFoundError,
                                        "Custom element definition not found.");
      return nullptr;
    }
  }

  // 7. Let element be the result of creating an element
  Element* element;

  if (definition) {
    element = CustomElement::CreateCustomElementSync(
        *this, converted_local_name, definition);
  } else if (V0CustomElement::IsValidName(local_name) &&
             RegistrationContext()) {
    element = RegistrationContext()->CreateCustomTagElement(
        *this,
        QualifiedName(g_null_atom, converted_local_name, xhtmlNamespaceURI));
  } else {
    element = createElement(window, local_name, exception_state);
    if (exception_state.HadException())
      return nullptr;
  }

  // 8. If 'is' is non-null, set 'is' attribute
  if (!is.IsEmpty()) {
    if (string_or_options.isString()) {
      V0CustomElementRegistrationContext::SetIsAttributeAndTypeExtension(
          element, is);
    } else if (string_or_options.isDictionary()) {
      element->setAttribute(HTMLNames::isAttr, is);
    }
  }

  return element;
}

static inline QualifiedName CreateQualifiedName(
    const AtomicString& namespace_uri,
    const AtomicString& qualified_name,
    ExceptionState& exception_state) {
  AtomicString prefix, local_name;
  if (!Document::ParseQualifiedName(qualified_name, prefix, local_name,
                                    exception_state))
    return QualifiedName::Null();

  QualifiedName q_name(prefix, local_name, namespace_uri);
  if (!Document::HasValidNamespaceForElements(q_name)) {
    exception_state.ThrowDOMException(
        kNamespaceError,
        "The namespace URI provided ('" + namespace_uri +
            "') is not valid for the qualified name provided ('" +
            qualified_name + "').");
    return QualifiedName::Null();
  }

  return q_name;
}

Element* Document::createElementNS(const LocalDOMWindow* window,
                                   const AtomicString& namespace_uri,
                                   const AtomicString& qualified_name,
                                   ExceptionState& exception_state) {
  QualifiedName q_name(
      CreateQualifiedName(namespace_uri, qualified_name, exception_state));
  if (q_name == QualifiedName::Null())
    return nullptr;

  if (CustomElement::ShouldCreateCustomElement(q_name))
    return CustomElement::CreateCustomElementSync(*this, q_name);
  return createElement(q_name, kCreatedByCreateElement);
}

// https://dom.spec.whatwg.org/#internal-createelementns-steps
Element* Document::createElementNS(const LocalDOMWindow* window,
                                   const AtomicString& namespace_uri,
                                   const AtomicString& qualified_name,
                                   const StringOrDictionary& string_or_options,
                                   ExceptionState& exception_state) {
  // 1. Validate and extract
  QualifiedName q_name(
      CreateQualifiedName(namespace_uri, qualified_name, exception_state));
  if (q_name == QualifiedName::Null())
    return nullptr;

  bool is_v1 = string_or_options.isDictionary() || !RegistrationContext();
  bool create_v1_builtin =
      string_or_options.isDictionary() &&
      RuntimeEnabledFeatures::customElementsBuiltinEnabled();
  bool should_create_builtin =
      create_v1_builtin || string_or_options.isString();

  // 2.
  const AtomicString& is =
      AtomicString(GetTypeExtension(this, string_or_options, exception_state));
  const AtomicString& name = should_create_builtin ? is : qualified_name;

  if (!IsValidElementName(window, qualified_name)) {
    exception_state.ThrowDOMException(
        kInvalidCharacterError, "The tag name provided ('" + qualified_name +
                                    "') is not a valid name.");
    return nullptr;
  }

  // 3. Let definition be result of lookup up custom element definition
  CustomElementDefinition* definition = nullptr;
  if (is_v1) {
    const CustomElementDescriptor desc =
        RuntimeEnabledFeatures::customElementsBuiltinEnabled()
            ? CustomElementDescriptor(name, qualified_name)
            : CustomElementDescriptor(qualified_name, qualified_name);
    if (CustomElementRegistry* registry = CustomElement::Registry(*this))
      definition = registry->DefinitionFor(desc);

    // 4. If 'is' is non-null and definition is null, throw NotFoundError
    if (!definition && create_v1_builtin) {
      exception_state.ThrowDOMException(kNotFoundError,
                                        "Custom element definition not found.");
      return nullptr;
    }
  }

  // 5. Let element be the result of creating an element
  Element* element;

  if (CustomElement::ShouldCreateCustomElement(q_name) || create_v1_builtin) {
    element = CustomElement::CreateCustomElementSync(*this, q_name, definition);
  } else if (V0CustomElement::IsValidName(q_name.LocalName()) &&
             RegistrationContext()) {
    element = RegistrationContext()->CreateCustomTagElement(*this, q_name);
  } else {
    element = createElement(q_name, kCreatedByCreateElement);
  }

  // 6. If 'is' is non-null, set 'is' attribute
  if (!is.IsEmpty()) {
    if (element->GetCustomElementState() != CustomElementState::kCustom) {
      V0CustomElementRegistrationContext::SetIsAttributeAndTypeExtension(
          element, is);
    } else if (string_or_options.isDictionary()) {
      element->setAttribute(HTMLNames::isAttr, is);
    }
  }

  return element;
}

ScriptValue Document::registerElement(ScriptState* script_state,
                                      const AtomicString& name,
                                      const ElementRegistrationOptions& options,
                                      ExceptionState& exception_state,
                                      V0CustomElement::NameSet valid_names) {
  HostsUsingFeatures::CountMainWorldOnly(
      script_state, *this,
      HostsUsingFeatures::Feature::kDocumentRegisterElement);

  if (!RegistrationContext()) {
    exception_state.ThrowDOMException(
        kNotSupportedError, "No element registration context is available.");
    return ScriptValue();
  }

  V0CustomElementConstructorBuilder constructor_builder(script_state, options);
  RegistrationContext()->RegisterElement(this, &constructor_builder, name,
                                         valid_names, exception_state);
  return constructor_builder.BindingsReturnValue();
}

V0CustomElementMicrotaskRunQueue* Document::CustomElementMicrotaskRunQueue() {
  if (!custom_element_microtask_run_queue_)
    custom_element_microtask_run_queue_ =
        V0CustomElementMicrotaskRunQueue::Create();
  return custom_element_microtask_run_queue_.Get();
}

void Document::ClearImportsController() {
  imports_controller_ = nullptr;
  if (!Loader())
    fetcher_->ClearContext();
}

void Document::CreateImportsController() {
  DCHECK(!imports_controller_);
  imports_controller_ = HTMLImportsController::Create(*this);
}

HTMLImportLoader* Document::ImportLoader() const {
  if (!imports_controller_)
    return 0;
  return imports_controller_->LoaderFor(*this);
}

bool Document::HaveImportsLoaded() const {
  if (!imports_controller_)
    return true;
  return !imports_controller_->ShouldBlockScriptExecution(*this);
}

LocalDOMWindow* Document::ExecutingWindow() const {
  if (LocalDOMWindow* owning_window = domWindow())
    return owning_window;
  if (HTMLImportsController* import = this->ImportsController())
    return import->Master()->domWindow();
  return 0;
}

LocalFrame* Document::ExecutingFrame() {
  LocalDOMWindow* window = ExecutingWindow();
  if (!window)
    return 0;
  return window->GetFrame();
}

DocumentFragment* Document::createDocumentFragment() {
  return DocumentFragment::Create(*this);
}

Text* Document::createTextNode(const String& data) {
  return Text::Create(*this, data);
}

Comment* Document::createComment(const String& data) {
  return Comment::Create(*this, data);
}

CDATASection* Document::createCDATASection(const String& data,
                                           ExceptionState& exception_state) {
  if (IsHTMLDocument()) {
    exception_state.ThrowDOMException(
        kNotSupportedError,
        "This operation is not supported for HTML documents.");
    return nullptr;
  }
  if (data.Contains("]]>")) {
    exception_state.ThrowDOMException(kInvalidCharacterError,
                                      "String cannot contain ']]>' since that "
                                      "is the end delimiter of a CData "
                                      "section.");
    return nullptr;
  }
  return CDATASection::Create(*this, data);
}

ProcessingInstruction* Document::createProcessingInstruction(
    const String& target,
    const String& data,
    ExceptionState& exception_state) {
  if (!IsValidName(target)) {
    exception_state.ThrowDOMException(
        kInvalidCharacterError,
        "The target provided ('" + target + "') is not a valid name.");
    return nullptr;
  }
  if (data.Contains("?>")) {
    exception_state.ThrowDOMException(
        kInvalidCharacterError,
        "The data provided ('" + data + "') contains '?>'.");
    return nullptr;
  }
  if (IsHTMLDocument()) {
    UseCounter::Count(*this,
                      UseCounter::kHTMLDocumentCreateProcessingInstruction);
  }
  return ProcessingInstruction::Create(*this, target, data);
}

Text* Document::CreateEditingTextNode(const String& text) {
  return Text::CreateEditingText(*this, text);
}

bool Document::ImportContainerNodeChildren(ContainerNode* old_container_node,
                                           ContainerNode* new_container_node,
                                           ExceptionState& exception_state) {
  for (Node& old_child : NodeTraversal::ChildrenOf(*old_container_node)) {
    Node* new_child = importNode(&old_child, true, exception_state);
    if (exception_state.HadException())
      return false;
    new_container_node->AppendChild(new_child, exception_state);
    if (exception_state.HadException())
      return false;
  }

  return true;
}

Node* Document::importNode(Node* imported_node,
                           bool deep,
                           ExceptionState& exception_state) {
  switch (imported_node->getNodeType()) {
    case kTextNode:
      return createTextNode(imported_node->nodeValue());
    case kCdataSectionNode:
      return CDATASection::Create(*this, imported_node->nodeValue());
    case kProcessingInstructionNode:
      return createProcessingInstruction(imported_node->nodeName(),
                                         imported_node->nodeValue(),
                                         exception_state);
    case kCommentNode:
      return createComment(imported_node->nodeValue());
    case kDocumentTypeNode: {
      DocumentType* doctype = ToDocumentType(imported_node);
      return DocumentType::Create(this, doctype->name(), doctype->publicId(),
                                  doctype->systemId());
    }
    case kElementNode: {
      Element* old_element = ToElement(imported_node);
      // FIXME: The following check might be unnecessary. Is it possible that
      // oldElement has mismatched prefix/namespace?
      if (!HasValidNamespaceForElements(old_element->TagQName())) {
        exception_state.ThrowDOMException(
            kNamespaceError, "The imported node has an invalid namespace.");
        return nullptr;
      }
      Element* new_element =
          createElement(old_element->TagQName(), kCreatedByImportNode);

      new_element->CloneDataFromElement(*old_element);

      if (deep) {
        if (!ImportContainerNodeChildren(old_element, new_element,
                                         exception_state))
          return nullptr;
        if (isHTMLTemplateElement(*old_element) &&
            !EnsureTemplateDocument().ImportContainerNodeChildren(
                toHTMLTemplateElement(old_element)->content(),
                toHTMLTemplateElement(new_element)->content(), exception_state))
          return nullptr;
      }

      return new_element;
    }
    case kAttributeNode:
      return Attr::Create(
          *this,
          QualifiedName(g_null_atom,
                        AtomicString(ToAttr(imported_node)->name()),
                        g_null_atom),
          ToAttr(imported_node)->value());
    case kDocumentFragmentNode: {
      if (imported_node->IsShadowRoot()) {
        // ShadowRoot nodes should not be explicitly importable.
        // Either they are imported along with their host node, or created
        // implicitly.
        exception_state.ThrowDOMException(
            kNotSupportedError,
            "The node provided is a shadow root, which may not be imported.");
        return nullptr;
      }
      DocumentFragment* old_fragment = ToDocumentFragment(imported_node);
      DocumentFragment* new_fragment = createDocumentFragment();
      if (deep && !ImportContainerNodeChildren(old_fragment, new_fragment,
                                               exception_state))
        return nullptr;

      return new_fragment;
    }
    case kDocumentNode:
      exception_state.ThrowDOMException(
          kNotSupportedError,
          "The node provided is a document, which may not be imported.");
      return nullptr;
  }

  NOTREACHED();
  return nullptr;
}

Node* Document::adoptNode(Node* source, ExceptionState& exception_state) {
  EventQueueScope scope;

  switch (source->getNodeType()) {
    case kDocumentNode:
      exception_state.ThrowDOMException(kNotSupportedError,
                                        "The node provided is of type '" +
                                            source->nodeName() +
                                            "', which may not be adopted.");
      return nullptr;
    case kAttributeNode: {
      Attr* attr = ToAttr(source);
      if (Element* owner_element = attr->ownerElement())
        owner_element->removeAttributeNode(attr, exception_state);
      break;
    }
    default:
      if (source->IsShadowRoot()) {
        // ShadowRoot cannot disconnect itself from the host node.
        exception_state.ThrowDOMException(
            kHierarchyRequestError,
            "The node provided is a shadow root, which may not be adopted.");
        return nullptr;
      }

      if (source->IsFrameOwnerElement()) {
        HTMLFrameOwnerElement* frame_owner_element =
            ToHTMLFrameOwnerElement(source);
        if (GetFrame() && GetFrame()->Tree().IsDescendantOf(
                              frame_owner_element->ContentFrame())) {
          exception_state.ThrowDOMException(
              kHierarchyRequestError,
              "The node provided is a frame which contains this document.");
          return nullptr;
        }
      }
      if (source->parentNode()) {
        source->parentNode()->RemoveChild(source, exception_state);
        if (exception_state.HadException())
          return nullptr;
        // The above removeChild() can execute arbitrary JavaScript code.
        if (source->parentNode()) {
          AddConsoleMessage(ConsoleMessage::Create(
              kJSMessageSource, kWarningMessageLevel,
              ExceptionMessages::FailedToExecute("adoptNode", "Document",
                                                 "Unable to remove the "
                                                 "specified node from the "
                                                 "original parent.")));
          return nullptr;
        }
      }
  }

  this->AdoptIfNeeded(*source);

  return source;
}

bool Document::HasValidNamespaceForElements(const QualifiedName& q_name) {
  // These checks are from DOM Core Level 2, createElementNS
  // http://www.w3.org/TR/DOM-Level-2-Core/core.html#ID-DocCrElNS
  // createElementNS(null, "html:div")
  if (!q_name.Prefix().IsEmpty() && q_name.NamespaceURI().IsNull())
    return false;
  // createElementNS("http://www.example.com", "xml:lang")
  if (q_name.Prefix() == g_xml_atom &&
      q_name.NamespaceURI() != XMLNames::xmlNamespaceURI)
    return false;

  // Required by DOM Level 3 Core and unspecified by DOM Level 2 Core:
  // http://www.w3.org/TR/2004/REC-DOM-Level-3-Core-20040407/core.html#ID-DocCrElNS
  // createElementNS("http://www.w3.org/2000/xmlns/", "foo:bar"),
  // createElementNS(null, "xmlns:bar"), createElementNS(null, "xmlns")
  if (q_name.Prefix() == g_xmlns_atom ||
      (q_name.Prefix().IsEmpty() && q_name.LocalName() == g_xmlns_atom))
    return q_name.NamespaceURI() == XMLNSNames::xmlnsNamespaceURI;
  return q_name.NamespaceURI() != XMLNSNames::xmlnsNamespaceURI;
}

bool Document::HasValidNamespaceForAttributes(const QualifiedName& q_name) {
  return HasValidNamespaceForElements(q_name);
}

// FIXME: This should really be in a possible ElementFactory class
Element* Document::createElement(const QualifiedName& q_name,
                                 CreateElementFlags flags) {
  Element* e = nullptr;

  // FIXME: Use registered namespaces and look up in a hash to find the right
  // factory.
  if (q_name.NamespaceURI() == xhtmlNamespaceURI)
    e = HTMLElementFactory::createHTMLElement(q_name.LocalName(), *this, flags);
  else if (q_name.NamespaceURI() == SVGNames::svgNamespaceURI)
    e = SVGElementFactory::createSVGElement(q_name.LocalName(), *this, flags);

  if (e)
    saw_elements_in_known_namespaces_ = true;
  else
    e = Element::Create(q_name, this);

  if (e->prefix() != q_name.Prefix())
    e->SetTagNameForCreateElementNS(q_name);

  DCHECK(q_name == e->TagQName());

  return e;
}

String Document::readyState() const {
  DEFINE_STATIC_LOCAL(const String, loading, ("loading"));
  DEFINE_STATIC_LOCAL(const String, interactive, ("interactive"));
  DEFINE_STATIC_LOCAL(const String, complete, ("complete"));

  switch (ready_state_) {
    case kLoading:
      return loading;
    case kInteractive:
      return interactive;
    case kComplete:
      return complete;
  }

  NOTREACHED();
  return String();
}

void Document::SetReadyState(DocumentReadyState ready_state) {
  if (ready_state == ready_state_)
    return;

  switch (ready_state) {
    case kLoading:
      if (!document_timing_.DomLoading()) {
        document_timing_.MarkDomLoading();
      }
      break;
    case kInteractive:
      if (!document_timing_.DomInteractive())
        document_timing_.MarkDomInteractive();
      break;
    case kComplete:
      if (!document_timing_.DomComplete())
        document_timing_.MarkDomComplete();
      break;
  }

  ready_state_ = ready_state;
  DispatchEvent(Event::Create(EventTypeNames::readystatechange));
}

bool Document::IsLoadCompleted() {
  return ready_state_ == kComplete;
}

AtomicString Document::EncodingName() const {
  // TextEncoding::name() returns a char*, no need to allocate a new
  // String for it each time.
  // FIXME: We should fix TextEncoding to speak AtomicString anyway.
  return AtomicString(Encoding().GetName());
}

void Document::SetContentLanguage(const AtomicString& language) {
  if (content_language_ == language)
    return;
  content_language_ = language;

  // Document's style depends on the content language.
  SetNeedsStyleRecalc(kSubtreeStyleChange, StyleChangeReasonForTracing::Create(
                                               StyleChangeReason::kLanguage));
}

void Document::setXMLVersion(const String& version,
                             ExceptionState& exception_state) {
  if (!XMLDocumentParser::SupportsXMLVersion(version)) {
    exception_state.ThrowDOMException(
        kNotSupportedError,
        "This document does not support the XML version '" + version + "'.");
    return;
  }

  xml_version_ = version;
}

void Document::setXMLStandalone(bool standalone,
                                ExceptionState& exception_state) {
  xml_standalone_ = standalone ? kStandalone : kNotStandalone;
}

void Document::SetContent(const String& content) {
  open();
  parser_->Append(content);
  close();
}

String Document::SuggestedMIMEType() const {
  if (IsXMLDocument()) {
    if (IsXHTMLDocument())
      return "application/xhtml+xml";
    if (IsSVGDocument())
      return "image/svg+xml";
    return "application/xml";
  }
  if (xmlStandalone())
    return "text/xml";
  if (IsHTMLDocument())
    return "text/html";

  if (DocumentLoader* document_loader = Loader())
    return document_loader->ResponseMIMEType();
  return String();
}

void Document::SetMimeType(const AtomicString& mime_type) {
  mime_type_ = mime_type;
}

AtomicString Document::contentType() const {
  if (!mime_type_.IsEmpty())
    return mime_type_;

  if (DocumentLoader* document_loader = Loader())
    return document_loader->MimeType();

  String mime_type = SuggestedMIMEType();
  if (!mime_type.IsEmpty())
    return AtomicString(mime_type);

  return AtomicString("application/xml");
}

Element* Document::ElementFromPoint(int x, int y) const {
  if (GetLayoutViewItem().IsNull())
    return 0;

  return TreeScope::ElementFromPoint(x, y);
}

HeapVector<Member<Element>> Document::ElementsFromPoint(int x, int y) const {
  if (GetLayoutViewItem().IsNull())
    return HeapVector<Member<Element>>();
  return TreeScope::ElementsFromPoint(x, y);
}

Range* Document::caretRangeFromPoint(int x, int y) {
  if (GetLayoutViewItem().IsNull())
    return nullptr;

  HitTestResult result = HitTestInDocument(this, x, y);
  PositionWithAffinity position_with_affinity = result.GetPosition();
  if (position_with_affinity.IsNull())
    return nullptr;

  Position range_compliant_position =
      position_with_affinity.GetPosition().ParentAnchoredEquivalent();
  return Range::CreateAdjustedToTreeScope(*this, range_compliant_position);
}

Element* Document::scrollingElement() {
  if (RuntimeEnabledFeatures::scrollTopLeftInteropEnabled() && InQuirksMode())
    UpdateStyleAndLayoutTree();
  return ScrollingElementNoLayout();
}

Element* Document::ScrollingElementNoLayout() {
  if (RuntimeEnabledFeatures::scrollTopLeftInteropEnabled()) {
    if (InQuirksMode()) {
      DCHECK(lifecycle_.GetState() >= DocumentLifecycle::kStyleClean);
      HTMLBodyElement* body = FirstBodyElement();
      if (body && body->GetLayoutObject() &&
          body->GetLayoutObject()->HasOverflowClip())
        return nullptr;

      return body;
    }

    return documentElement();
  }

  return body();
}

/*
 * Performs three operations:
 *  1. Convert control characters to spaces
 *  2. Trim leading and trailing spaces
 *  3. Collapse internal whitespace.
 */
template <typename CharacterType>
static inline String CanonicalizedTitle(Document* document,
                                        const String& title) {
  unsigned length = title.length();
  unsigned builder_index = 0;
  const CharacterType* characters = title.GetCharacters<CharacterType>();

  StringBuffer<CharacterType> buffer(length);

  // Replace control characters with spaces and collapse whitespace.
  bool pending_whitespace = false;
  for (unsigned i = 0; i < length; ++i) {
    UChar32 c = characters[i];
    if ((c <= WTF::Unicode::kSpaceCharacter &&
         c != WTF::Unicode::kLineTabulationCharacter) ||
        c == WTF::Unicode::kDeleteCharacter) {
      if (builder_index != 0)
        pending_whitespace = true;
    } else {
      if (pending_whitespace) {
        buffer[builder_index++] = ' ';
        pending_whitespace = false;
      }
      buffer[builder_index++] = c;
    }
  }
  buffer.Shrink(builder_index);

  return String::Adopt(buffer);
}

void Document::UpdateTitle(const String& title) {
  if (raw_title_ == title)
    return;

  raw_title_ = title;

  String old_title = title_;
  if (raw_title_.IsEmpty())
    title_ = String();
  else if (raw_title_.Is8Bit())
    title_ = CanonicalizedTitle<LChar>(this, raw_title_);
  else
    title_ = CanonicalizedTitle<UChar>(this, raw_title_);

  if (!frame_ || old_title == title_)
    return;
  frame_->Loader().Client()->DispatchDidReceiveTitle(title_);
}

void Document::setTitle(const String& title) {
  // Title set by JavaScript -- overrides any title elements.
  if (!title_element_) {
    if (IsHTMLDocument() || IsXHTMLDocument()) {
      HTMLElement* head_element = head();
      if (!head_element)
        return;
      title_element_ = HTMLTitleElement::Create(*this);
      head_element->AppendChild(title_element_.Get());
    } else if (IsSVGDocument()) {
      Element* element = documentElement();
      if (!isSVGSVGElement(element))
        return;
      title_element_ = SVGTitleElement::Create(*this);
      element->InsertBefore(title_element_.Get(), element->firstChild());
    }
  } else {
    if (!IsHTMLDocument() && !IsXHTMLDocument() && !IsSVGDocument())
      title_element_ = nullptr;
  }

  if (isHTMLTitleElement(title_element_))
    toHTMLTitleElement(title_element_)->setText(title);
  else if (isSVGTitleElement(title_element_))
    toSVGTitleElement(title_element_)->SetText(title);
  else
    UpdateTitle(title);
}

void Document::SetTitleElement(Element* title_element) {
  // If the root element is an svg element in the SVG namespace, then let value
  // be the child text content of the first title element in the SVG namespace
  // that is a child of the root element.
  if (isSVGSVGElement(documentElement())) {
    title_element_ = Traversal<SVGTitleElement>::FirstChild(*documentElement());
  } else {
    if (title_element_ && title_element_ != title_element)
      title_element_ = Traversal<HTMLTitleElement>::FirstWithin(*this);
    else
      title_element_ = title_element;

    // If the root element isn't an svg element in the SVG namespace and the
    // title element is in the SVG namespace, it is ignored.
    if (isSVGTitleElement(title_element_)) {
      title_element_ = nullptr;
      return;
    }
  }

  if (isHTMLTitleElement(title_element_))
    UpdateTitle(toHTMLTitleElement(title_element_)->text());
  else if (isSVGTitleElement(title_element_))
    UpdateTitle(toSVGTitleElement(title_element_)->textContent());
}

void Document::RemoveTitle(Element* title_element) {
  if (title_element_ != title_element)
    return;

  title_element_ = nullptr;

  // Update title based on first title element in the document, if one exists.
  if (IsHTMLDocument() || IsXHTMLDocument()) {
    if (HTMLTitleElement* title =
            Traversal<HTMLTitleElement>::FirstWithin(*this))
      SetTitleElement(title);
  } else if (IsSVGDocument()) {
    if (SVGTitleElement* title = Traversal<SVGTitleElement>::FirstWithin(*this))
      SetTitleElement(title);
  }

  if (!title_element_)
    UpdateTitle(String());
}

const AtomicString& Document::dir() {
  Element* root_element = documentElement();
  if (isHTMLHtmlElement(root_element))
    return toHTMLHtmlElement(root_element)->dir();
  return g_null_atom;
}

void Document::setDir(const AtomicString& value) {
  Element* root_element = documentElement();
  if (isHTMLHtmlElement(root_element))
    toHTMLHtmlElement(root_element)->setDir(value);
}

PageVisibilityState Document::GetPageVisibilityState() const {
  // The visibility of the document is inherited from the visibility of the
  // page. If there is no page associated with the document, we will assume
  // that the page is hidden, as specified by the spec:
  // https://w3c.github.io/page-visibility/#hidden-attribute
  if (!frame_ || !frame_->GetPage())
    return kPageVisibilityStateHidden;
  // While visibilitychange is being dispatched during unloading it is
  // expected that the visibility is hidden regardless of the page's
  // visibility.
  if (load_event_progress_ >= kUnloadVisibilityChangeInProgress)
    return kPageVisibilityStateHidden;
  return frame_->GetPage()->VisibilityState();
}

bool Document::IsPrefetchOnly() const {
  if (!frame_ || !frame_->GetPage())
    return false;

  PrerendererClient* prerenderer_client =
      PrerendererClient::From(frame_->GetPage());
  return prerenderer_client && prerenderer_client->IsPrefetchOnly();
}

String Document::visibilityState() const {
  return PageVisibilityStateString(GetPageVisibilityState());
}

bool Document::hidden() const {
  return GetPageVisibilityState() != kPageVisibilityStateVisible;
}

void Document::DidChangeVisibilityState() {
  DispatchEvent(Event::CreateBubble(EventTypeNames::visibilitychange));
  // Also send out the deprecated version until it can be removed.
  DispatchEvent(Event::CreateBubble(EventTypeNames::webkitvisibilitychange));

  if (GetPageVisibilityState() == kPageVisibilityStateVisible)
    Timeline().SetAllCompositorPending();

  if (hidden() && canvas_font_cache_)
    canvas_font_cache_->PruneAll();
}

String Document::nodeName() const {
  return "#document";
}

Node::NodeType Document::getNodeType() const {
  return kDocumentNode;
}

FormController& Document::GetFormController() {
  if (!form_controller_) {
    form_controller_ = FormController::Create();
    HistoryItem* history_item = Loader() ? Loader()->GetHistoryItem() : nullptr;
    if (history_item)
      history_item->SetDocumentState(form_controller_->FormElementsState());
  }
  return *form_controller_;
}

DocumentState* Document::FormElementsState() const {
  if (!form_controller_)
    return 0;
  return form_controller_->FormElementsState();
}

void Document::SetStateForNewFormElements(const Vector<String>& state_vector) {
  if (!state_vector.size() && !form_controller_)
    return;
  GetFormController().SetStateForNewFormElements(state_vector);
}

FrameView* Document::View() const {
  return frame_ ? frame_->View() : nullptr;
}

Page* Document::GetPage() const {
  return frame_ ? frame_->GetPage() : nullptr;
}

Settings* Document::GetSettings() const {
  return frame_ ? frame_->GetSettings() : nullptr;
}

Range* Document::createRange() {
  return Range::Create(*this);
}

NodeIterator* Document::createNodeIterator(Node* root,
                                           unsigned what_to_show,
                                           V8NodeFilterCondition* filter) {
  DCHECK(root);
  return NodeIterator::Create(root, what_to_show, filter);
}

TreeWalker* Document::createTreeWalker(Node* root,
                                       unsigned what_to_show,
                                       V8NodeFilterCondition* filter) {
  DCHECK(root);
  return TreeWalker::Create(root, what_to_show, filter);
}

bool Document::NeedsLayoutTreeUpdate() const {
  if (!IsActive() || !View())
    return false;
  if (NeedsFullLayoutTreeUpdate())
    return true;
  if (ChildNeedsStyleRecalc())
    return true;
  if (ChildNeedsStyleInvalidation())
    return true;
  if (GetLayoutViewItem().WasNotifiedOfSubtreeChange())
    return true;
  return false;
}

bool Document::NeedsFullLayoutTreeUpdate() const {
  if (!IsActive() || !View())
    return false;
  if (style_engine_->NeedsActiveStyleUpdate())
    return true;
  if (!use_elements_needing_update_.IsEmpty())
    return true;
  if (NeedsStyleRecalc())
    return true;
  if (NeedsStyleInvalidation())
    return true;
  // FIXME: The childNeedsDistributionRecalc bit means either self or children,
  // we should fix that.
  if (ChildNeedsDistributionRecalc())
    return true;
  if (DocumentAnimations::NeedsAnimationTimingUpdate(*this))
    return true;
  return false;
}

bool Document::ShouldScheduleLayoutTreeUpdate() const {
  if (!IsActive())
    return false;
  if (InStyleRecalc())
    return false;
  // InPreLayout will recalc style itself. There's no reason to schedule another
  // recalc.
  if (lifecycle_.GetState() == DocumentLifecycle::kInPreLayout)
    return false;
  if (!ShouldScheduleLayout())
    return false;
  return true;
}

void Document::ScheduleLayoutTreeUpdate() {
  DCHECK(!HasPendingVisualUpdate());
  DCHECK(ShouldScheduleLayoutTreeUpdate());
  DCHECK(NeedsLayoutTreeUpdate());

  if (!View()->CanThrottleRendering())
    GetPage()->Animator().ScheduleVisualUpdate(GetFrame());
  lifecycle_.EnsureStateAtMost(DocumentLifecycle::kVisualUpdatePending);

  TRACE_EVENT_INSTANT1(TRACE_DISABLED_BY_DEFAULT("devtools.timeline"),
                       "ScheduleStyleRecalculation", TRACE_EVENT_SCOPE_THREAD,
                       "data",
                       InspectorRecalculateStylesEvent::Data(GetFrame()));
  ++style_version_;
}

bool Document::HasPendingForcedStyleRecalc() const {
  return HasPendingVisualUpdate() && !InStyleRecalc() &&
         GetStyleChangeType() >= kSubtreeStyleChange;
}

void Document::UpdateStyleInvalidationIfNeeded() {
  DCHECK(IsActive());
  ScriptForbiddenScope forbid_script;

  if (!ChildNeedsStyleInvalidation() && !NeedsStyleInvalidation())
    return;
  TRACE_EVENT0("blink", "Document::updateStyleInvalidationIfNeeded");
  GetStyleEngine().GetStyleInvalidator().Invalidate(*this);
}

void Document::SetupFontBuilder(ComputedStyle& document_style) {
  FontBuilder font_builder(*this);
  CSSFontSelector* selector = GetStyleEngine().FontSelector();
  font_builder.CreateFontForDocument(selector, document_style);
}

void Document::InheritHtmlAndBodyElementStyles(StyleRecalcChange change) {
  DCHECK(InStyleRecalc());
  DCHECK(documentElement());

  bool did_recalc_document_element = false;
  RefPtr<ComputedStyle> document_element_style =
      documentElement()->MutableComputedStyle();
  if (change == kForce)
    documentElement()->ClearAnimationStyleChange();
  if (!document_element_style || documentElement()->NeedsStyleRecalc() ||
      change == kForce) {
    document_element_style =
        EnsureStyleResolver().StyleForElement(documentElement());
    did_recalc_document_element = true;
  }

  WritingMode root_writing_mode = document_element_style->GetWritingMode();
  TextDirection root_direction = document_element_style->Direction();

  HTMLElement* body = this->body();
  RefPtr<ComputedStyle> body_style;

  if (body) {
    body_style = body->MutableComputedStyle();
    if (did_recalc_document_element)
      body->ClearAnimationStyleChange();
    if (!body_style || body->NeedsStyleRecalc() ||
        did_recalc_document_element) {
      body_style = EnsureStyleResolver().StyleForElement(
          body, document_element_style.Get(), document_element_style.Get());
    }
    root_writing_mode = body_style->GetWritingMode();
    root_direction = body_style->Direction();
  }

  const ComputedStyle* background_style = document_element_style.Get();
  // http://www.w3.org/TR/css3-background/#body-background
  // <html> root element with no background steals background from its first
  // <body> child.
  // Also see LayoutBoxModelObject::backgroundStolenForBeingBody()
  if (isHTMLHtmlElement(documentElement()) && isHTMLBodyElement(body) &&
      !background_style->HasBackground())
    background_style = body_style.Get();
  Color background_color =
      background_style->VisitedDependentColor(CSSPropertyBackgroundColor);
  FillLayer background_layers = background_style->BackgroundLayers();
  for (auto current_layer = &background_layers; current_layer;
       current_layer = current_layer->Next()) {
    // http://www.w3.org/TR/css3-background/#root-background
    // The root element background always have painting area of the whole
    // canvas.
    current_layer->SetClip(kBorderFillBox);

    // The root element doesn't scroll. It always propagates its layout overflow
    // to the viewport. Positioning background against either box is equivalent
    // to positioning against the scrolled box of the viewport.
    if (current_layer->Attachment() == kScrollBackgroundAttachment)
      current_layer->SetAttachment(kLocalBackgroundAttachment);
  }
  EImageRendering image_rendering = background_style->ImageRendering();

  const ComputedStyle* overflow_style = nullptr;
  if (Element* element =
          ViewportDefiningElement(document_element_style.Get())) {
    if (element == body) {
      overflow_style = body_style.Get();
    } else {
      DCHECK_EQ(element, documentElement());
      overflow_style = document_element_style.Get();

      // The body element has its own scrolling box, independent from the
      // viewport.  This is a bit of a weird edge case in the CSS spec that we
      // might want to try to eliminate some day (eg. for ScrollTopLeftInterop -
      // see http://crbug.com/157855).
      if (body_style && !body_style->IsOverflowVisible())
        UseCounter::Count(*this, UseCounter::kBodyScrollsInAdditionToViewport);
    }
  }

  // Resolved rem units are stored in the matched properties cache so we need to
  // make sure to invalidate the cache if the documentElement needed to reattach
  // or the font size changed and then trigger a full document recalc. We also
  // need to clear it here since the call to styleForElement on the body above
  // can cache bad values for rem units if the documentElement's style was
  // dirty. We could keep track of which elements depend on rem units like we do
  // for viewport styles, but we assume root font size changes are rare and just
  // invalidate the cache for now.
  if (GetStyleEngine().UsesRemUnits() &&
      (documentElement()->NeedsAttach() ||
       !documentElement()->GetComputedStyle() ||
       documentElement()->GetComputedStyle()->FontSize() !=
           document_element_style->FontSize())) {
    EnsureStyleResolver().InvalidateMatchedPropertiesCache();
    documentElement()->SetNeedsStyleRecalc(
        kSubtreeStyleChange, StyleChangeReasonForTracing::Create(
                                 StyleChangeReason::kFontSizeChange));
  }

  EOverflowAnchor overflow_anchor = EOverflowAnchor::kAuto;
  EOverflow overflow_x = EOverflow::kAuto;
  EOverflow overflow_y = EOverflow::kAuto;
  float column_gap = 0;
  if (overflow_style) {
    overflow_anchor = overflow_style->OverflowAnchor();
    overflow_x = overflow_style->OverflowX();
    overflow_y = overflow_style->OverflowY();
    // Visible overflow on the viewport is meaningless, and the spec says to
    // treat it as 'auto':
    if (overflow_x == EOverflow::kVisible)
      overflow_x = EOverflow::kAuto;
    if (overflow_y == EOverflow::kVisible)
      overflow_y = EOverflow::kAuto;
    if (overflow_anchor == EOverflowAnchor::kVisible)
      overflow_anchor = EOverflowAnchor::kAuto;
    // Column-gap is (ab)used by the current paged overflow implementation (in
    // lack of other ways to specify gaps between pages), so we have to
    // propagate it too.
    column_gap = overflow_style->ColumnGap();
  }

  ScrollSnapType snap_type = overflow_style->GetScrollSnapType();
  const LengthPoint& snap_destination = overflow_style->ScrollSnapDestination();

  RefPtr<ComputedStyle> document_style = GetLayoutViewItem().MutableStyle();
  if (document_style->GetWritingMode() != root_writing_mode ||
      document_style->Direction() != root_direction ||
      document_style->VisitedDependentColor(CSSPropertyBackgroundColor) !=
          background_color ||
      document_style->BackgroundLayers() != background_layers ||
      document_style->ImageRendering() != image_rendering ||
      document_style->OverflowAnchor() != overflow_anchor ||
      document_style->OverflowX() != overflow_x ||
      document_style->OverflowY() != overflow_y ||
      document_style->ColumnGap() != column_gap ||
      document_style->GetScrollSnapType() != snap_type ||
      document_style->ScrollSnapDestination() != snap_destination) {
    RefPtr<ComputedStyle> new_style = ComputedStyle::Clone(*document_style);
    new_style->SetWritingMode(root_writing_mode);
    new_style->SetDirection(root_direction);
    new_style->SetBackgroundColor(background_color);
    new_style->AccessBackgroundLayers() = background_layers;
    new_style->SetImageRendering(image_rendering);
    new_style->SetOverflowAnchor(overflow_anchor);
    new_style->SetOverflowX(overflow_x);
    new_style->SetOverflowY(overflow_y);
    new_style->SetColumnGap(column_gap);
    new_style->SetScrollSnapType(snap_type);
    new_style->SetScrollSnapDestination(snap_destination);
    GetLayoutViewItem().SetStyle(new_style);
    SetupFontBuilder(*new_style);
  }

  if (body) {
    if (const ComputedStyle* style = body->GetComputedStyle()) {
      if (style->Direction() != root_direction ||
          style->GetWritingMode() != root_writing_mode)
        body->SetNeedsStyleRecalc(kSubtreeStyleChange,
                                  StyleChangeReasonForTracing::Create(
                                      StyleChangeReason::kWritingModeChange));
    }
  }

  if (const ComputedStyle* style = documentElement()->GetComputedStyle()) {
    if (style->Direction() != root_direction ||
        style->GetWritingMode() != root_writing_mode)
      documentElement()->SetNeedsStyleRecalc(
          kSubtreeStyleChange, StyleChangeReasonForTracing::Create(
                                   StyleChangeReason::kWritingModeChange));
  }
}

#if DCHECK_IS_ON()
static void AssertLayoutTreeUpdated(Node& root) {
  for (Node& node : NodeTraversal::InclusiveDescendantsOf(root)) {
    // We leave some nodes with dirty bits in the tree because they don't
    // matter like Comment and ProcessingInstruction nodes.
    // TODO(esprehn): Don't even mark those nodes as needing recalcs in the
    // first place.
    if (!node.IsElementNode() && !node.IsTextNode() && !node.IsShadowRoot() &&
        !node.IsDocumentNode())
      continue;
    DCHECK(!node.NeedsStyleRecalc());
    DCHECK(!node.ChildNeedsStyleRecalc());
    DCHECK(!node.NeedsReattachLayoutTree());
    DCHECK(!node.ChildNeedsReattachLayoutTree());
    DCHECK(!node.ChildNeedsDistributionRecalc());
    DCHECK(!node.NeedsStyleInvalidation());
    DCHECK(!node.ChildNeedsStyleInvalidation());
    for (ShadowRoot* shadow_root = node.YoungestShadowRoot(); shadow_root;
         shadow_root = shadow_root->OlderShadowRoot())
      AssertLayoutTreeUpdated(*shadow_root);
  }
}
#endif

void Document::UpdateStyleAndLayoutTree() {
  DCHECK(IsMainThread());

  ScriptForbiddenScope forbid_script;
  // We should forbid script execution for plugins here because update while
  // layout is changing, HTMLPlugin element can be reattached and plugin can be
  // destroyed. Plugin can execute scripts on destroy. It produces crash without
  // PluginScriptForbiddenScope: crbug.com/550427.
  PluginScriptForbiddenScope plugin_forbid_script;

  if (!View() || !IsActive())
    return;

  if (View()->ShouldThrottleRendering())
    return;

  if (!NeedsLayoutTreeUpdate()) {
    if (Lifecycle().GetState() < DocumentLifecycle::kStyleClean) {
      // needsLayoutTreeUpdate may change to false without any actual layout
      // tree update.  For example, needsAnimationTimingUpdate may change to
      // false when time elapses.  Advance lifecycle to StyleClean because style
      // is actually clean now.
      Lifecycle().AdvanceTo(DocumentLifecycle::kInStyleRecalc);
      Lifecycle().AdvanceTo(DocumentLifecycle::kStyleClean);
    }
    return;
  }

  if (InStyleRecalc())
    return;

  // Entering here from inside layout, paint etc. would be catastrophic since
  // recalcStyle can tear down the layout tree or (unfortunately) run
  // script. Kill the whole layoutObject if someone managed to get into here in
  // states not allowing tree mutations.
  CHECK(Lifecycle().StateAllowsTreeMutations());

  TRACE_EVENT_BEGIN1("blink,devtools.timeline", "UpdateLayoutTree", "beginData",
                     InspectorRecalculateStylesEvent::Data(GetFrame()));

  unsigned start_element_count = GetStyleEngine().StyleForElementCount();

  probe::RecalculateStyle recalculate_style_scope(this);

  DocumentAnimations::UpdateAnimationTimingIfNeeded(*this);
  EvaluateMediaQueryListIfNeeded();
  UpdateUseShadowTreesIfNeeded();
  UpdateDistribution();
  UpdateActiveStyle();
  UpdateStyleInvalidationIfNeeded();

  // FIXME: We should update style on our ancestor chain before proceeding
  // however doing so currently causes several tests to crash, as
  // LocalFrame::setDocument calls Document::attach before setting the
  // LocalDOMWindow on the LocalFrame, or the SecurityOrigin on the
  // document. The attach, in turn resolves style (here) and then when we
  // resolve style on the parent chain, we may end up re-attaching our
  // containing iframe, which when asked HTMLFrameElementBase::isURLAllowed hits
  // a null-dereference due to security code always assuming the document has a
  // SecurityOrigin.

  UpdateStyle();

  NotifyLayoutTreeOfSubtreeChanges();

  // As a result of the style recalculation, the currently hovered element might
  // have been detached (for example, by setting display:none in the :hover
  // style), schedule another mouseMove event to check if any other elements
  // ended up under the mouse pointer due to re-layout.
  if (HoverElement() && !HoverElement()->GetLayoutObject() && GetFrame())
    GetFrame()->GetEventHandler().DispatchFakeMouseMoveEventSoon();

  if (focused_element_ && !focused_element_->IsFocusable())
    ClearFocusedElementSoon();
  GetLayoutViewItem().ClearHitTestCache();

  DCHECK(!DocumentAnimations::NeedsAnimationTimingUpdate(*this));

  unsigned element_count =
      GetStyleEngine().StyleForElementCount() - start_element_count;

  TRACE_EVENT_END1("blink,devtools.timeline", "UpdateLayoutTree",
                   "elementCount", element_count);

#if DCHECK_IS_ON()
  AssertLayoutTreeUpdated(*this);
#endif
}

void Document::UpdateActiveStyle() {
  DCHECK(IsActive());
  DCHECK(IsMainThread());
  TRACE_EVENT0("blink", "Document::updateActiveStyle");
  GetStyleEngine().UpdateActiveStyle();
}

void Document::UpdateStyle() {
  DCHECK(!View()->ShouldThrottleRendering());
  TRACE_EVENT_BEGIN0("blink,blink_style", "Document::updateStyle");
  double start_time = MonotonicallyIncreasingTime();

  unsigned initial_element_count = GetStyleEngine().StyleForElementCount();

  HTMLFrameOwnerElement::UpdateSuspendScope
      suspend_frame_view_base_hierarchy_updates;
  lifecycle_.AdvanceTo(DocumentLifecycle::kInStyleRecalc);

  StyleRecalcChange change = kNoChange;
  if (GetStyleChangeType() >= kSubtreeStyleChange)
    change = kForce;

  NthIndexCache nth_index_cache(*this);

  // FIXME: Cannot access the ensureStyleResolver() before calling
  // styleForDocument below because apparently the StyleResolver's constructor
  // has side effects. We should fix it.  See printing/setPrinting.html,
  // printing/width-overflow.html though they only fail on mac when accessing
  // the resolver by what appears to be a viewport size difference.

  if (change == kForce) {
    has_nodes_with_placeholder_style_ = false;
    RefPtr<ComputedStyle> document_style =
        StyleResolver::StyleForDocument(*this);
    StyleRecalcChange local_change = ComputedStyle::StylePropagationDiff(
        document_style.Get(), GetLayoutViewItem().Style());
    if (local_change != kNoChange)
      GetLayoutViewItem().SetStyle(std::move(document_style));
  }

  ClearNeedsStyleRecalc();
  ClearNeedsReattachLayoutTree();

  StyleResolver& resolver = EnsureStyleResolver();

  bool should_record_stats;
  TRACE_EVENT_CATEGORY_GROUP_ENABLED("blink,blink_style", &should_record_stats);
  GetStyleEngine().SetStatsEnabled(should_record_stats);

  if (Element* document_element = this->documentElement()) {
    InheritHtmlAndBodyElementStyles(change);
    if (document_element->ShouldCallRecalcStyle(change)) {
      TRACE_EVENT0("blink,blink_style", "Document::recalcStyle");
      document_element->RecalcStyle(change);
    }
    if (document_element->NeedsReattachLayoutTree() ||
        document_element->ChildNeedsReattachLayoutTree()) {
      TRACE_EVENT0("blink,blink_style", "Document::rebuildLayoutTree");
      document_element->RebuildLayoutTree();
    }
  }

  View()->RecalcOverflowAfterStyleChange();

  ClearChildNeedsStyleRecalc();
  ClearChildNeedsReattachLayoutTree();

  resolver.ClearStyleSharingList();

  DCHECK(!NeedsStyleRecalc());
  DCHECK(!ChildNeedsStyleRecalc());
  DCHECK(!NeedsReattachLayoutTree());
  DCHECK(!ChildNeedsReattachLayoutTree());
  DCHECK(InStyleRecalc());
  DCHECK_EQ(GetStyleResolver(), &resolver);
  lifecycle_.AdvanceTo(DocumentLifecycle::kStyleClean);
  if (should_record_stats) {
    TRACE_EVENT_END2(
        "blink,blink_style", "Document::updateStyle", "resolverAccessCount",
        GetStyleEngine().StyleForElementCount() - initial_element_count,
        "counters", GetStyleEngine().Stats()->ToTracedValue());
  } else {
    TRACE_EVENT_END1(
        "blink,blink_style", "Document::updateStyle", "resolverAccessCount",
        GetStyleEngine().StyleForElementCount() - initial_element_count);
  }

  double update_duration_seconds = MonotonicallyIncreasingTime() - start_time;
  DEFINE_STATIC_LOCAL(CustomCountHistogram, update_histogram,
                      ("Style.UpdateTime", 0, 10000000, 50));
  update_histogram.Count(update_duration_seconds * 1000 * 1000);
  CSSTiming::From(*this).RecordUpdateDuration(update_duration_seconds);
}

void Document::NotifyLayoutTreeOfSubtreeChanges() {
  if (!GetLayoutViewItem().WasNotifiedOfSubtreeChange())
    return;

  lifecycle_.AdvanceTo(DocumentLifecycle::kInLayoutSubtreeChange);

  GetLayoutViewItem().HandleSubtreeModifications();
  DCHECK(!GetLayoutViewItem().WasNotifiedOfSubtreeChange());

  lifecycle_.AdvanceTo(DocumentLifecycle::kLayoutSubtreeChangeClean);
}

bool Document::NeedsLayoutTreeUpdateForNode(const Node& node) const {
  if (!node.CanParticipateInFlatTree())
    return false;
  if (!NeedsLayoutTreeUpdate())
    return false;
  if (!node.isConnected())
    return false;

  if (NeedsFullLayoutTreeUpdate() || node.NeedsStyleRecalc() ||
      node.NeedsStyleInvalidation())
    return true;
  for (const ContainerNode* ancestor = LayoutTreeBuilderTraversal::Parent(node);
       ancestor; ancestor = LayoutTreeBuilderTraversal::Parent(*ancestor)) {
    if (ancestor->NeedsStyleRecalc() || ancestor->NeedsStyleInvalidation() ||
        ancestor->NeedsAdjacentStyleRecalc())
      return true;
  }
  return false;
}

void Document::UpdateStyleAndLayoutTreeForNode(const Node* node) {
  DCHECK(node);
  if (!NeedsLayoutTreeUpdateForNode(*node))
    return;
  UpdateStyleAndLayoutTree();
}

void Document::UpdateStyleAndLayoutIgnorePendingStylesheetsForNode(Node* node) {
  DCHECK(node);
  if (!node->InActiveDocument())
    return;
  UpdateStyleAndLayoutIgnorePendingStylesheets();
}

void Document::UpdateStyleAndLayout() {
  DCHECK(IsMainThread());

  ScriptForbiddenScope forbid_script;

  FrameView* frame_view = View();
  if (frame_view && frame_view->IsInPerformLayout()) {
    // View layout should not be re-entrant.
    NOTREACHED();
    return;
  }

  if (HTMLFrameOwnerElement* owner = LocalOwner())
    owner->GetDocument().UpdateStyleAndLayout();

  UpdateStyleAndLayoutTree();

  if (!IsActive())
    return;

  if (frame_view->NeedsLayout())
    frame_view->UpdateLayout();

  if (Lifecycle().GetState() < DocumentLifecycle::kLayoutClean)
    Lifecycle().AdvanceTo(DocumentLifecycle::kLayoutClean);

  if (FrameView* frame_view = View())
    frame_view->PerformScrollAnchoringAdjustments();
}

void Document::LayoutUpdated() {
  // Plugins can run script inside layout which can detach the page.
  // TODO(esprehn): Can this still happen now that all plugins are out of
  // process?
  if (GetFrame() && GetFrame()->GetPage())
    GetFrame()->GetPage()->GetChromeClient().LayoutUpdated(GetFrame());

  Markers().InvalidateRectsForAllTextMatchMarkers();

  // The layout system may perform layouts with pending stylesheets. When
  // recording first layout time, we ignore these layouts, since painting is
  // suppressed for them. We're interested in tracking the time of the
  // first real or 'paintable' layout.
  // TODO(esprehn): This doesn't really make sense, why not track the first
  // beginFrame? This will catch the first layout in a page that does lots
  // of layout thrashing even though that layout might not be followed by
  // a paint for many seconds.
  if (IsRenderingReady() && body() &&
      !GetStyleEngine().HasPendingScriptBlockingSheets()) {
    if (!document_timing_.FirstLayout())
      document_timing_.MarkFirstLayout();
  }

  root_scroller_controller_->DidUpdateLayout();
}

void Document::ClearFocusedElementSoon() {
  if (!clear_focused_element_timer_.IsActive())
    clear_focused_element_timer_.StartOneShot(0, BLINK_FROM_HERE);
}

void Document::ClearFocusedElementTimerFired(TimerBase*) {
  UpdateStyleAndLayoutTree();

  if (focused_element_ && !focused_element_->IsFocusable())
    focused_element_->blur();
}

// FIXME: This is a bad idea and needs to be removed eventually.
// Other browsers load stylesheets before they continue parsing the web page.
// Since we don't, we can run JavaScript code that needs answers before the
// stylesheets are loaded. Doing a layout ignoring the pending stylesheets
// lets us get reasonable answers. The long term solution to this problem is
// to instead suspend JavaScript execution.
void Document::UpdateStyleAndLayoutTreeIgnorePendingStylesheets() {
  StyleEngine::IgnoringPendingStylesheet ignoring(GetStyleEngine());

  if (GetStyleEngine().HasPendingScriptBlockingSheets()) {
    // FIXME: We are willing to attempt to suppress painting with outdated style
    // info only once.  Our assumption is that it would be dangerous to try to
    // stop it a second time, after page content has already been loaded and
    // displayed with accurate style information. (Our suppression involves
    // blanking the whole page at the moment. If it were more refined, we might
    // be able to do something better.) It's worth noting though that this
    // entire method is a hack, since what we really want to do is suspend JS
    // instead of doing a layout with inaccurate information.
    HTMLElement* body_element = body();
    if (body_element && !body_element->GetLayoutObject() &&
        pending_sheet_layout_ == kNoLayoutWithPendingSheets) {
      pending_sheet_layout_ = kDidLayoutWithPendingSheets;
      GetStyleEngine().MarkAllTreeScopesDirty();
    }
    if (has_nodes_with_placeholder_style_) {
      // If new nodes have been added or style recalc has been done with style
      // sheets still pending, some nodes may not have had their real style
      // calculated yet.  Normally this gets cleaned when style sheets arrive
      // but here we need up-to-date style immediately.
      SetNeedsStyleRecalc(kSubtreeStyleChange,
                          StyleChangeReasonForTracing::Create(
                              StyleChangeReason::kCleanupPlaceholderStyles));
    }
  }
  UpdateStyleAndLayoutTree();
}

void Document::UpdateStyleAndLayoutIgnorePendingStylesheets(
    Document::RunPostLayoutTasks run_post_layout_tasks) {
  UpdateStyleAndLayoutTreeIgnorePendingStylesheets();
  UpdateStyleAndLayout();

  if (run_post_layout_tasks == kRunPostLayoutTasksSynchronously && View())
    View()->FlushAnyPendingPostLayoutTasks();
}

PassRefPtr<ComputedStyle> Document::StyleForElementIgnoringPendingStylesheets(
    Element* element) {
  DCHECK_EQ(element->GetDocument(), this);
  StyleEngine::IgnoringPendingStylesheet ignoring(GetStyleEngine());
  if (!element->CanParticipateInFlatTree())
    return EnsureStyleResolver().StyleForElement(element, nullptr);

  ContainerNode* parent = LayoutTreeBuilderTraversal::Parent(*element);
  const ComputedStyle* parent_style =
      parent ? parent->EnsureComputedStyle() : nullptr;

  ContainerNode* layout_parent =
      parent ? LayoutTreeBuilderTraversal::LayoutParent(*element) : nullptr;
  const ComputedStyle* layout_parent_style =
      layout_parent ? layout_parent->EnsureComputedStyle() : parent_style;

  return EnsureStyleResolver().StyleForElement(element, parent_style,
                                               layout_parent_style);
}

PassRefPtr<ComputedStyle> Document::StyleForPage(int page_index) {
  UpdateDistribution();
  return EnsureStyleResolver().StyleForPage(page_index);
}

void Document::EnsurePaintLocationDataValidForNode(const Node* node) {
  DCHECK(node);
  if (!node->InActiveDocument())
    return;

  // For all nodes we must have up-to-date style and have performed layout to do
  // any location-based calculation.
  UpdateStyleAndLayoutIgnorePendingStylesheets();

  // The location of elements that are position: sticky is not known until
  // compositing inputs are cleaned. Therefore, for any elements that are either
  // sticky or are in a sticky sub-tree (e.g. are affected by a sticky element),
  // we need to also clean compositing inputs.
  if (View() && node->GetLayoutObject() &&
      node->GetLayoutObject()->StyleRef().SubtreeIsSticky()) {
    if (RuntimeEnabledFeatures::slimmingPaintV2Enabled()) {
      // In SPv2, compositing inputs are cleaned as part of PrePaint.
      View()->UpdateAllLifecyclePhasesExceptPaint();
    } else {
      View()->UpdateLifecycleToCompositingInputsClean();
    }
  }
}

bool Document::IsPageBoxVisible(int page_index) {
  return StyleForPage(page_index)->Visibility() !=
         EVisibility::kHidden;  // display property doesn't apply to @page.
}

void Document::PageSizeAndMarginsInPixels(int page_index,
                                          DoubleSize& page_size,
                                          int& margin_top,
                                          int& margin_right,
                                          int& margin_bottom,
                                          int& margin_left) {
  RefPtr<ComputedStyle> style = StyleForPage(page_index);

  double width = page_size.Width();
  double height = page_size.Height();
  switch (style->GetPageSizeType()) {
    case PAGE_SIZE_AUTO:
      break;
    case PAGE_SIZE_AUTO_LANDSCAPE:
      if (width < height)
        std::swap(width, height);
      break;
    case PAGE_SIZE_AUTO_PORTRAIT:
      if (width > height)
        std::swap(width, height);
      break;
    case PAGE_SIZE_RESOLVED: {
      FloatSize size = style->PageSize();
      width = size.Width();
      height = size.Height();
      break;
    }
    default:
      NOTREACHED();
  }
  page_size = DoubleSize(width, height);

  // The percentage is calculated with respect to the width even for margin top
  // and bottom.
  // http://www.w3.org/TR/CSS2/box.html#margin-properties
  margin_top = style->MarginTop().IsAuto()
                   ? margin_top
                   : IntValueForLength(style->MarginTop(), width);
  margin_right = style->MarginRight().IsAuto()
                     ? margin_right
                     : IntValueForLength(style->MarginRight(), width);
  margin_bottom = style->MarginBottom().IsAuto()
                      ? margin_bottom
                      : IntValueForLength(style->MarginBottom(), width);
  margin_left = style->MarginLeft().IsAuto()
                    ? margin_left
                    : IntValueForLength(style->MarginLeft(), width);
}

void Document::SetIsViewSource(bool is_view_source) {
  is_view_source_ = is_view_source;
  if (!is_view_source_)
    return;
}

void Document::ScheduleUseShadowTreeUpdate(SVGUseElement& element) {
  use_elements_needing_update_.insert(&element);
  ScheduleLayoutTreeUpdateIfNeeded();
}

void Document::UnscheduleUseShadowTreeUpdate(SVGUseElement& element) {
  use_elements_needing_update_.erase(&element);
}

void Document::UpdateUseShadowTreesIfNeeded() {
  ScriptForbiddenScope forbid_script;

  if (use_elements_needing_update_.IsEmpty())
    return;

  HeapHashSet<Member<SVGUseElement>> elements;
  use_elements_needing_update_.swap(elements);
  for (SVGUseElement* element : elements)
    element->BuildPendingResource();
}

StyleResolver* Document::GetStyleResolver() const {
  return style_engine_->Resolver();
}

StyleResolver& Document::EnsureStyleResolver() const {
  return style_engine_->EnsureResolver();
}

void Document::Initialize() {
  DCHECK_EQ(lifecycle_.GetState(), DocumentLifecycle::kInactive);
  DCHECK(!ax_object_cache_ || this != &AxObjectCacheOwner());

  layout_view_ = new LayoutView(this);
  SetLayoutObject(layout_view_);

  layout_view_->SetIsInWindow(true);
  layout_view_->SetStyle(StyleResolver::StyleForDocument(*this));
  layout_view_->Compositor()->SetNeedsCompositingUpdate(
      kCompositingUpdateAfterCompositingInputChange);

  ContainerNode::AttachLayoutTree();

  // The TextAutosizer can't update layout view info while the Document is
  // detached, so update now in case anything changed.
  if (TextAutosizer* autosizer = GetTextAutosizer())
    autosizer->UpdatePageInfo();

  frame_->DocumentAttached();
  lifecycle_.AdvanceTo(DocumentLifecycle::kStyleClean);

  if (View())
    View()->DidAttachDocument();

  // Observer(s) should not be initialized until the document is initialized /
  // attached to a frame. Otherwise ContextLifecycleObserver::contextDestroyed
  // wouldn't be fired.
  network_state_observer_ = new NetworkStateObserver(*this);
}

void Document::Shutdown() {
  TRACE_EVENT0("blink", "Document::shutdown");
  CHECK(!frame_ || frame_->Tree().ChildCount() == 0);
  if (!IsActive())
    return;

  // Frame navigation can cause a new Document to be attached. Don't allow that,
  // since that will cause a situation where LocalFrame still has a Document
  // attached after this finishes!  Normally, it shouldn't actually be possible
  // to trigger navigation here.  However, plugins (see below) can cause lots of
  // crazy things to happen, since plugin detach involves nested run loops.
  FrameNavigationDisabler navigation_disabler(*frame_);
  // Defer FrameViewBase updates to avoid plugins trying to run script inside
  // ScriptForbiddenScope, which will crash the renderer after
  // https://crrev.com/200984
  HTMLFrameOwnerElement::UpdateSuspendScope
      suspend_frame_view_base_hierarchy_updates;
  // Don't allow script to run in the middle of detachLayoutTree() because a
  // detaching Document is not in a consistent state.
  ScriptForbiddenScope forbid_script;

  lifecycle_.AdvanceTo(DocumentLifecycle::kStopping);
  View()->Dispose();

  // If the FrameViewBase of the document's frame owner doesn't match view()
  // then FrameView::dispose() didn't clear the owner's FrameViewBase. If we
  // don't clear it here, it may be clobbered later in LocalFrame::createView().
  // See also https://crbug.com/673170 and the comment in FrameView::dispose().
  HTMLFrameOwnerElement* owner_element = frame_->DeprecatedLocalOwner();
  if (owner_element)
    owner_element->SetWidget(nullptr);

  markers_->PrepareForDestruction();

  if (GetPage())
    GetPage()->DocumentDetached(this);
  probe::documentDetached(this);

  if (frame_->Loader().Client()->GetSharedWorkerRepositoryClient())
    frame_->Loader()
        .Client()
        ->GetSharedWorkerRepositoryClient()
        ->DocumentDetached(this);

  // FIXME: consider using SuspendableObject.
  if (scripted_animation_controller_)
    scripted_animation_controller_->ClearDocumentPointer();
  scripted_animation_controller_.Clear();

  scripted_idle_task_controller_.Clear();

  if (SvgExtensions())
    AccessSVGExtensions().PauseAnimations();

  // FIXME: This shouldn't be needed once LocalDOMWindow becomes
  // ExecutionContext.
  if (dom_window_)
    dom_window_->ClearEventQueue();

  if (layout_view_)
    layout_view_->SetIsInWindow(false);

  if (RegistrationContext())
    RegistrationContext()->DocumentWasDetached();

  MutationObserver::CleanSlotChangeList(*this);

  hover_element_ = nullptr;
  active_hover_element_ = nullptr;
  autofocus_element_ = nullptr;

  if (focused_element_.Get()) {
    Element* old_focused_element = focused_element_;
    focused_element_ = nullptr;
    if (GetPage())
      GetPage()->GetChromeClient().FocusedNodeChanged(old_focused_element,
                                                      nullptr);
  }
  sequential_focus_navigation_starting_point_ = nullptr;

  if (this == &AxObjectCacheOwner())
    ClearAXObjectCache();

  layout_view_ = nullptr;
  ContainerNode::DetachLayoutTree();

  if (this != &AxObjectCacheOwner()) {
    if (AXObjectCache* cache = ExistingAXObjectCache()) {
      // Documents that are not a root document use the AXObjectCache in
      // their root document. Node::removedFrom is called after the
      // document has been detached so it can't find the root document.
      // We do the removals here instead.
      for (Node& node : NodeTraversal::DescendantsOf(*this)) {
        cache->Remove(&node);
      }
    }
  }

  GetStyleEngine().DidDetach();

  GetPage()->GetEventHandlerRegistry().DocumentDetached(*this);

  // Signal destruction to mutation observers.
  SynchronousMutationNotifier::NotifyContextDestroyed();

  // If this Document is associated with a live DocumentLoader, the
  // DocumentLoader will take care of clearing the FetchContext. Deferring
  // to the DocumentLoader when possible also prevents prematurely clearing
  // the context in the case where multiple Documents end up associated with
  // a single DocumentLoader (e.g., navigating to a javascript: url).
  if (!Loader())
    fetcher_->ClearContext();
  // If this document is the master for an HTMLImportsController, sever that
  // relationship. This ensures that we don't leave import loads in flight,
  // thinking they should have access to a valid frame when they don't.
  if (imports_controller_) {
    imports_controller_->Dispose();
    ClearImportsController();
  }

  timers_.SetTimerTaskRunner(
      Platform::Current()->CurrentThread()->Scheduler()->TimerTaskRunner());

  if (media_query_matcher_)
    media_query_matcher_->DocumentDetached();

  lifecycle_.AdvanceTo(DocumentLifecycle::kStopped);

  // TODO(haraken): Call contextDestroyed() before we start any disruptive
  // operations.
  // TODO(haraken): Currently we call notifyContextDestroyed() only in
  // Document::detachLayoutTree(), which means that we don't call
  // notifyContextDestroyed() for a document that doesn't get detached.
  // If such a document has any observer, the observer won't get
  // a contextDestroyed() notification. This can happen for a document
  // created by DOMImplementation::createDocument().
  ExecutionContext::NotifyContextDestroyed();

  // This is required, as our LocalFrame might delete itself as soon as it
  // detaches us. However, this violates Node::detachLayoutTree() semantics, as
  // it's never possible to re-attach. Eventually Document::detachLayoutTree()
  // should be renamed, or this setting of the frame to 0 could be made
  // explicit in each of the callers of Document::detachLayoutTree().
  frame_ = nullptr;
}

void Document::RemoveAllEventListeners() {
  ContainerNode::RemoveAllEventListeners();

  if (LocalDOMWindow* dom_window = this->domWindow())
    dom_window->RemoveAllEventListeners();
}

Document& Document::AxObjectCacheOwner() const {
  // Every document has its own axObjectCache if accessibility is enabled,
  // except for page popups, which share the axObjectCache of their owner.
  Document* doc = const_cast<Document*>(this);
  if (doc->GetFrame() && doc->GetFrame()->PagePopupOwner()) {
    DCHECK(!doc->ax_object_cache_);
    return doc->GetFrame()
        ->PagePopupOwner()
        ->GetDocument()
        .AxObjectCacheOwner();
  }
  return *doc;
}

void Document::ClearAXObjectCache() {
  DCHECK_EQ(&AxObjectCacheOwner(), this);
  // Clear the cache member variable before calling delete because attempts
  // are made to access it during destruction.
  if (ax_object_cache_)
    ax_object_cache_->Dispose();
  ax_object_cache_.Clear();
}

AXObjectCache* Document::ExistingAXObjectCache() const {
  // If the layoutObject is gone then we are in the process of destruction.
  // This method will be called before m_frame = nullptr.
  if (!AxObjectCacheOwner().GetLayoutView())
    return 0;

  return AxObjectCacheOwner().ax_object_cache_.Get();
}

AXObjectCache* Document::AxObjectCache() const {
  Settings* settings = this->GetSettings();
  if (!settings || !settings->GetAccessibilityEnabled())
    return 0;

  // Every document has its own AXObjectCache if accessibility is enabled,
  // except for page popups (such as select popups or context menus),
  // which share the AXObjectCache of their owner.
  //
  // See http://crbug.com/532249
  Document& cache_owner = this->AxObjectCacheOwner();

  // If the document has already been detached, do not make a new axObjectCache.
  if (!cache_owner.GetLayoutView())
    return 0;

  DCHECK(&cache_owner == this || !ax_object_cache_);
  if (!cache_owner.ax_object_cache_)
    cache_owner.ax_object_cache_ = AXObjectCache::Create(cache_owner);
  return cache_owner.ax_object_cache_.Get();
}

CanvasFontCache* Document::GetCanvasFontCache() {
  if (!canvas_font_cache_)
    canvas_font_cache_ = CanvasFontCache::Create(*this);

  return canvas_font_cache_.Get();
}

DocumentParser* Document::CreateParser() {
  if (IsHTMLDocument())
    return HTMLDocumentParser::Create(ToHTMLDocument(*this),
                                      parser_sync_policy_);
  // FIXME: this should probably pass the frame instead
  return XMLDocumentParser::Create(*this, View());
}

bool Document::IsFrameSet() const {
  if (!IsHTMLDocument())
    return false;
  return isHTMLFrameSetElement(body());
}

ScriptableDocumentParser* Document::GetScriptableDocumentParser() const {
  return Parser() ? Parser()->AsScriptableDocumentParser() : nullptr;
}

void Document::open(Document* entered_document,
                    ExceptionState& exception_state) {
  if (ImportLoader()) {
    exception_state.ThrowDOMException(
        kInvalidStateError, "Imported document doesn't support open().");
    return;
  }

  if (!IsHTMLDocument()) {
    exception_state.ThrowDOMException(kInvalidStateError,
                                      "Only HTML documents support open().");
    return;
  }

  if (throw_on_dynamic_markup_insertion_count_) {
    exception_state.ThrowDOMException(
        kInvalidStateError,
        "Custom Element constructor should not use open().");
    return;
  }

  if (entered_document) {
    if (!GetSecurityOrigin()->IsSameSchemeHostPortAndSuborigin(
            entered_document->GetSecurityOrigin())) {
      exception_state.ThrowSecurityError(
          "Can only call open() on same-origin documents.");
      return;
    }
    SetSecurityOrigin(entered_document->GetSecurityOrigin());

    if (this != entered_document) {
      // Clear the hash fragment from the inherited URL to prevent a
      // scroll-into-view for any document.open()'d frame.
      KURL new_url = entered_document->Url();
      new_url.SetFragmentIdentifier(String());
      SetURL(new_url);
    }

    cookie_url_ = entered_document->CookieURL();
  }

  open();
}

void Document::open() {
  DCHECK(!ImportLoader());

  if (frame_) {
    if (ScriptableDocumentParser* parser = GetScriptableDocumentParser()) {
      if (parser->IsParsing()) {
        // FIXME: HTML5 doesn't tell us to check this, it might not be correct.
        if (parser->IsExecutingScript())
          return;

        if (!parser->WasCreatedByScript() && parser->HasInsertionPoint())
          return;
      }
    }

    if (frame_->Loader().HasProvisionalNavigation()) {
      frame_->Loader().StopAllLoaders();
      // PlzNavigate: navigations handled by the client should also be
      // cancelled.
      if (frame_->Loader().Client() &&
          frame_->GetSettings()->GetBrowserSideNavigationEnabled()) {
        frame_->Loader().Client()->AbortClientNavigation();
      }
    }
  }

  RemoveAllEventListenersRecursively();
  ResetTreeScope();
  if (frame_)
    frame_->Selection().Clear();
  ImplicitOpen(kForceSynchronousParsing);
  if (ScriptableDocumentParser* parser = GetScriptableDocumentParser())
    parser->SetWasCreatedByScript(true);

  if (frame_)
    frame_->Loader().DidExplicitOpen();
}

void Document::DetachParser() {
  if (!parser_)
    return;
  parser_->Detach();
  parser_.Clear();
  DocumentParserTiming::From(*this).MarkParserDetached();
}

void Document::CancelParsing() {
  DetachParser();
  SetParsingState(kFinishedParsing);
  SetReadyState(kComplete);
  SuppressLoadEvent();
}

DocumentParser* Document::ImplicitOpen(
    ParserSynchronizationPolicy parser_sync_policy) {
  RemoveChildren();
  DCHECK(!focused_element_);

  SetCompatibilityMode(kNoQuirksMode);

  if (!ThreadedParsingEnabledForTesting()) {
    parser_sync_policy = kForceSynchronousParsing;
  } else if (parser_sync_policy == kAllowAsynchronousParsing &&
             IsPrefetchOnly()) {
    // Prefetch must be synchronous.
    parser_sync_policy = kForceSynchronousParsing;
  }

  DetachParser();
  parser_sync_policy_ = parser_sync_policy;
  parser_ = CreateParser();
  DocumentParserTiming::From(*this).MarkParserStart();
  SetParsingState(kParsing);
  SetReadyState(kLoading);
  if (load_event_progress_ != kLoadEventInProgress &&
      PageDismissalEventBeingDispatched() == kNoDismissal) {
    load_event_progress_ = kLoadEventNotRun;
  }

  return parser_;
}

HTMLElement* Document::body() const {
  if (!documentElement() || !isHTMLHtmlElement(documentElement()))
    return 0;

  for (HTMLElement* child =
           Traversal<HTMLElement>::FirstChild(*documentElement());
       child; child = Traversal<HTMLElement>::NextSibling(*child)) {
    if (isHTMLFrameSetElement(*child) || isHTMLBodyElement(*child))
      return child;
  }

  return 0;
}

HTMLBodyElement* Document::FirstBodyElement() const {
  if (!documentElement())
    return 0;

  for (HTMLElement* child =
           Traversal<HTMLElement>::FirstChild(*documentElement());
       child; child = Traversal<HTMLElement>::NextSibling(*child)) {
    if (isHTMLBodyElement(*child))
      return toHTMLBodyElement(child);
  }

  return 0;
}

void Document::setBody(HTMLElement* prp_new_body,
                       ExceptionState& exception_state) {
  HTMLElement* new_body = prp_new_body;

  if (!new_body) {
    exception_state.ThrowDOMException(
        kHierarchyRequestError,
        ExceptionMessages::ArgumentNullOrIncorrectType(1, "HTMLElement"));
    return;
  }
  if (!documentElement()) {
    exception_state.ThrowDOMException(kHierarchyRequestError,
                                      "No document element exists.");
    return;
  }

  if (!isHTMLBodyElement(*new_body) && !isHTMLFrameSetElement(*new_body)) {
    exception_state.ThrowDOMException(
        kHierarchyRequestError,
        "The new body element is of type '" + new_body->tagName() +
            "'. It must be either a 'BODY' or 'FRAMESET' element.");
    return;
  }

  HTMLElement* old_body = body();
  if (old_body == new_body)
    return;

  if (old_body)
    documentElement()->ReplaceChild(new_body, old_body, exception_state);
  else
    documentElement()->AppendChild(new_body, exception_state);
}

void Document::WillInsertBody() {
  if (GetFrame())
    GetFrame()->Loader().Client()->DispatchWillInsertBody();
  // If we get to the <body> try to resume commits since we should have content
  // to paint now.
  // TODO(esprehn): Is this really optimal? We might start producing frames
  // for very little content, should we wait for some heuristic like
  // isVisuallyNonEmpty() ?
  BeginLifecycleUpdatesIfRenderingReady();
}

HTMLHeadElement* Document::head() const {
  Node* de = documentElement();
  if (!de)
    return 0;

  return Traversal<HTMLHeadElement>::FirstChild(*de);
}

Element* Document::ViewportDefiningElement(
    const ComputedStyle* root_style) const {
  // If a BODY element sets non-visible overflow, it is to be propagated to the
  // viewport, as long as the following conditions are all met:
  // (1) The root element is HTML.
  // (2) It is the primary BODY element (we only assert for this, expecting
  //     callers to behave).
  // (3) The root element has visible overflow.
  // Otherwise it's the root element's properties that are to be propagated.
  Element* root_element = documentElement();
  Element* body_element = body();
  if (!root_element)
    return 0;
  if (!root_style) {
    root_style = root_element->GetComputedStyle();
    if (!root_style)
      return 0;
  }
  if (body_element && root_style->IsOverflowVisible() &&
      isHTMLHtmlElement(*root_element))
    return body_element;
  return root_element;
}

void Document::close(ExceptionState& exception_state) {
  // FIXME: We should follow the specification more closely:
  //        http://www.whatwg.org/specs/web-apps/current-work/#dom-document-close

  if (ImportLoader()) {
    exception_state.ThrowDOMException(
        kInvalidStateError, "Imported document doesn't support close().");
    return;
  }

  if (!IsHTMLDocument()) {
    exception_state.ThrowDOMException(kInvalidStateError,
                                      "Only HTML documents support close().");
    return;
  }

  if (throw_on_dynamic_markup_insertion_count_) {
    exception_state.ThrowDOMException(
        kInvalidStateError,
        "Custom Element constructor should not use close().");
    return;
  }

  close();
}

void Document::close() {
  if (!GetScriptableDocumentParser() ||
      !GetScriptableDocumentParser()->WasCreatedByScript() ||
      !GetScriptableDocumentParser()->IsParsing())
    return;

  parser_->Finish();
  if (!parser_ || !parser_->IsParsing())
    SetReadyState(kComplete);
  CheckCompleted();
}

void Document::ImplicitClose() {
  DCHECK(!InStyleRecalc());
  DCHECK(parser_);

  load_event_progress_ = kLoadEventInProgress;

  // We have to clear the parser, in case someone document.write()s from the
  // onLoad event handler, as in Radar 3206524.
  DetachParser();

  if (GetFrame() && CanExecuteScripts(kNotAboutToExecuteScript)) {
    ImageLoader::DispatchPendingLoadEvents();
    ImageLoader::DispatchPendingErrorEvents();
  }

  // JS running below could remove the frame or destroy the LayoutView so we
  // call those two functions repeatedly and don't save them on the stack.

  // To align the HTML load event and the SVGLoad event for the outermost <svg>
  // element, fire it from here, instead of doing it from
  // SVGElement::finishedParsingChildren.
  if (SvgExtensions())
    AccessSVGExtensions().DispatchSVGLoadEventToOutermostSVGElements();

  if (this->domWindow())
    this->domWindow()->DocumentWasClosed();

  if (GetFrame()) {
    GetFrame()->Loader().Client()->DispatchDidHandleOnloadEvents();
    Loader()->GetApplicationCacheHost()->StopDeferringEvents();
  }

  if (!GetFrame()) {
    load_event_progress_ = kLoadEventCompleted;
    return;
  }

  // Make sure both the initial layout and reflow happen after the onload
  // fires. This will improve onload scores, and other browsers do it.
  // If they wanna cheat, we can too. -dwh

  if (GetFrame()->GetNavigationScheduler().LocationChangePending() &&
      ElapsedTime() < kCLayoutScheduleThreshold) {
    // Just bail out. Before or during the onload we were shifted to another
    // page.  The old i-Bench suite does this. When this happens don't bother
    // painting or laying out.
    load_event_progress_ = kLoadEventCompleted;
    return;
  }

  // We used to force a synchronous display and flush here.  This really isn't
  // necessary and can in fact be actively harmful if pages are loading at a
  // rate of > 60fps
  // (if your platform is syncing flushes and limiting them to 60fps).
  if (!LocalOwner() || (LocalOwner()->GetLayoutObject() &&
                        !LocalOwner()->GetLayoutObject()->NeedsLayout())) {
    UpdateStyleAndLayoutTree();

    // Always do a layout after loading if needed.
    if (View() && !GetLayoutViewItem().IsNull() &&
        (!GetLayoutViewItem().FirstChild() ||
         GetLayoutViewItem().NeedsLayout()))
      View()->UpdateLayout();
  }

  load_event_progress_ = kLoadEventCompleted;

  if (GetFrame() && !GetLayoutViewItem().IsNull() &&
      GetSettings()->GetAccessibilityEnabled()) {
    if (AXObjectCache* cache = AxObjectCache()) {
      if (this == &AxObjectCacheOwner())
        cache->HandleLoadComplete(this);
      else
        cache->HandleLayoutComplete(this);
    }
  }

  if (SvgExtensions())
    AccessSVGExtensions().StartAnimations();
}

static bool AllDescendantsAreComplete(Frame* frame) {
  if (!frame)
    return true;
  for (Frame* child = frame->Tree().FirstChild(); child;
       child = child->Tree().TraverseNext(frame)) {
    if (child->IsLoading())
      return false;
  }
  return true;
}

bool Document::ShouldComplete() {
  return parsing_state_ == kFinishedParsing && HaveImportsLoaded() &&
         !fetcher_->BlockingRequestCount() && !IsDelayingLoadEvent() &&
         load_event_progress_ != kLoadEventInProgress &&
         AllDescendantsAreComplete(frame_);
}

void Document::CheckCompleted() {
  if (!ShouldComplete())
    return;

  if (frame_) {
    frame_->Client()->RunScriptsAtDocumentIdle();

    // Injected scripts may have disconnected this frame.
    if (!frame_)
      return;

    // Check again, because runScriptsAtDocumentIdle() may have delayed the load
    // event.
    if (!ShouldComplete())
      return;
  }

  // OK, completed. Fire load completion events as needed.
  SetReadyState(kComplete);
  if (LoadEventStillNeeded())
    ImplicitClose();

  // The readystatechanged or load event may have disconnected this frame.
  if (!frame_ || !frame_->IsAttached())
    return;
  frame_->GetNavigationScheduler().StartTimer();
  View()->HandleLoadCompleted();
  // The document itself is complete, but if a child frame was restarted due to
  // an event, this document is still considered to be in progress.
  if (!AllDescendantsAreComplete(frame_))
    return;

  // No need to repeat if we've already notified this load as finished.
  if (!Loader()->SentDidFinishLoad()) {
    if (frame_->IsMainFrame())
      ViewportDescription().ReportMobilePageStats(frame_);
    Loader()->SetSentDidFinishLoad();
    frame_->Client()->DispatchDidFinishLoad();
    if (!frame_)
      return;
  }

  frame_->Loader().DidFinishNavigation();
}

bool Document::DispatchBeforeUnloadEvent(ChromeClient& chrome_client,
                                         bool is_reload,
                                         bool& did_allow_navigation) {
  if (!dom_window_)
    return true;

  if (!body())
    return true;

  if (ProcessingBeforeUnload())
    return false;

  BeforeUnloadEvent* before_unload_event = BeforeUnloadEvent::Create();
  before_unload_event->initEvent(EventTypeNames::beforeunload, false, true);
  load_event_progress_ = kBeforeUnloadEventInProgress;
  dom_window_->DispatchEvent(before_unload_event, this);
  load_event_progress_ = kBeforeUnloadEventCompleted;
  if (!before_unload_event->defaultPrevented())
    DefaultEventHandler(before_unload_event);
  if (!GetFrame() || before_unload_event->returnValue().IsNull())
    return true;

  if (!GetFrame()->HasReceivedUserGesture()) {
    AddConsoleMessage(ConsoleMessage::Create(
        kJSMessageSource, kErrorMessageLevel,
        "Blocked attempt to show a 'beforeunload' confirmation panel for a "
        "frame that never had a user gesture since its load. "
        "https://www.chromestatus.com/feature/5082396709879808"));
    return true;
  }

  if (did_allow_navigation) {
    AddConsoleMessage(ConsoleMessage::Create(
        kJSMessageSource, kErrorMessageLevel,
        "Blocked attempt to show multiple 'beforeunload' confirmation panels "
        "for a single navigation."));
    return true;
  }

  String text = before_unload_event->returnValue();
  if (chrome_client.OpenBeforeUnloadConfirmPanel(text, frame_, is_reload)) {
    did_allow_navigation = true;
    return true;
  }
  return false;
}

void Document::DispatchUnloadEvents() {
  PluginScriptForbiddenScope forbid_plugin_destructor_scripting;
  if (parser_)
    parser_->StopParsing();

  if (load_event_progress_ == kLoadEventNotRun)
    return;

  if (load_event_progress_ <= kUnloadEventInProgress) {
    if (GetPage())
      GetPage()->WillUnloadDocument(*this);
    Element* current_focused_element = FocusedElement();
    if (isHTMLInputElement(current_focused_element))
      toHTMLInputElement(*current_focused_element).EndEditing();
    if (load_event_progress_ < kPageHideInProgress) {
      load_event_progress_ = kPageHideInProgress;
      if (LocalDOMWindow* window = domWindow())
        window->DispatchEvent(
            PageTransitionEvent::Create(EventTypeNames::pagehide, false), this);
      if (!frame_)
        return;

      PageVisibilityState visibility_state = GetPageVisibilityState();
      load_event_progress_ = kUnloadVisibilityChangeInProgress;
      if (visibility_state != kPageVisibilityStateHidden) {
        // Dispatch visibilitychange event, but don't bother doing
        // other notifications as we're about to be unloaded.
        DispatchEvent(Event::CreateBubble(EventTypeNames::visibilitychange));
        DispatchEvent(
            Event::CreateBubble(EventTypeNames::webkitvisibilitychange));
      }
      if (!frame_)
        return;

      DocumentLoader* document_loader =
          frame_->Loader().ProvisionalDocumentLoader();
      load_event_progress_ = kUnloadEventInProgress;
      Event* unload_event(Event::Create(EventTypeNames::unload));
      if (document_loader && !document_loader->GetTiming().UnloadEventStart() &&
          !document_loader->GetTiming().UnloadEventEnd()) {
        DocumentLoadTiming& timing = document_loader->GetTiming();
        DCHECK(timing.NavigationStart());
        timing.MarkUnloadEventStart();
        frame_->DomWindow()->DispatchEvent(unload_event, this);
        timing.MarkUnloadEventEnd();
      } else {
        frame_->DomWindow()->DispatchEvent(unload_event, frame_->GetDocument());
      }
    }
    load_event_progress_ = kUnloadEventHandled;
  }

  if (!frame_)
    return;

  // Don't remove event listeners from a transitional empty document (see
  // https://bugs.webkit.org/show_bug.cgi?id=28716 for more information).
  bool keep_event_listeners =
      frame_->Loader().ProvisionalDocumentLoader() &&
      frame_->ShouldReuseDefaultView(
          frame_->Loader().ProvisionalDocumentLoader()->Url());
  if (!keep_event_listeners)
    RemoveAllEventListenersRecursively();
}

Document::PageDismissalType Document::PageDismissalEventBeingDispatched()
    const {
  switch (load_event_progress_) {
    case kBeforeUnloadEventInProgress:
      return kBeforeUnloadDismissal;
    case kPageHideInProgress:
      return kPageHideDismissal;
    case kUnloadVisibilityChangeInProgress:
      return kUnloadVisibilityChangeDismissal;
    case kUnloadEventInProgress:
      return kUnloadDismissal;

    case kLoadEventNotRun:
    case kLoadEventInProgress:
    case kLoadEventCompleted:
    case kBeforeUnloadEventCompleted:
    case kUnloadEventHandled:
      return kNoDismissal;
  }
  NOTREACHED();
  return kNoDismissal;
}

void Document::SetParsingState(ParsingState parsing_state) {
  parsing_state_ = parsing_state;

  if (Parsing() && !element_data_cache_)
    element_data_cache_ = ElementDataCache::Create();
}

bool Document::ShouldScheduleLayout() const {
  // This function will only be called when FrameView thinks a layout is needed.
  // This enforces a couple extra rules.
  //
  //    (a) Only schedule a layout once the stylesheets are loaded.
  //    (b) Only schedule layout once we have a body element.
  if (!IsActive())
    return false;

  if (IsRenderingReady() && body())
    return true;

  if (documentElement() && !isHTMLHtmlElement(*documentElement()))
    return true;

  return false;
}

int Document::ElapsedTime() const {
  return static_cast<int>((CurrentTime() - start_time_) * 1000);
}

bool Document::CanCreateHistoryEntry() const {
  // TODO(japhet): This flag controls an intervention to require a user gesture
  // or a long time on page in order for a content-initiated navigation to add
  // an entry to the back/forward list. Removing the flag and making this the
  // default will require updating a couple hundred tests that currently depend
  // on creating history entries without user gestures. I'm waiting to update
  // the tests until the feature is proven to minimize churn.
  // https://bugs.chromium.org/p/chromium/issues/detail?id=638198
  if (!GetSettings() || !GetSettings()->GetHistoryEntryRequiresUserGesture())
    return true;
  if (frame_->HasReceivedUserGesture())
    return true;
  return ElapsedTime() >= kElapsedTimeForHistoryEntryWithoutUserGestureMS;
}

void Document::write(const SegmentedString& text,
                     Document* entered_document,
                     ExceptionState& exception_state) {
  if (ImportLoader()) {
    exception_state.ThrowDOMException(
        kInvalidStateError, "Imported document doesn't support write().");
    return;
  }

  if (!IsHTMLDocument()) {
    exception_state.ThrowDOMException(kInvalidStateError,
                                      "Only HTML documents support write().");
    return;
  }

  if (throw_on_dynamic_markup_insertion_count_) {
    exception_state.ThrowDOMException(
        kInvalidStateError,
        "Custom Element constructor should not use write().");
    return;
  }

  if (entered_document &&
      !GetSecurityOrigin()->IsSameSchemeHostPortAndSuborigin(
          entered_document->GetSecurityOrigin())) {
    exception_state.ThrowSecurityError(
        "Can only call write() on same-origin documents.");
    return;
  }

  NestingLevelIncrementer nesting_level_incrementer(write_recursion_depth_);

  write_recursion_is_too_deep_ =
      (write_recursion_depth_ > 1) && write_recursion_is_too_deep_;
  write_recursion_is_too_deep_ =
      (write_recursion_depth_ > kCMaxWriteRecursionDepth) ||
      write_recursion_is_too_deep_;

  if (write_recursion_is_too_deep_)
    return;

  bool has_insertion_point = parser_ && parser_->HasInsertionPoint();

  if (!has_insertion_point && ignore_destructive_write_count_) {
    AddConsoleMessage(
        ConsoleMessage::Create(kJSMessageSource, kWarningMessageLevel,
                               ExceptionMessages::FailedToExecute(
                                   "write", "Document",
                                   "It isn't possible to write into a document "
                                   "from an asynchronously-loaded external "
                                   "script unless it is explicitly opened.")));
    return;
  }

  if (!has_insertion_point)
    open(entered_document, ASSERT_NO_EXCEPTION);

  DCHECK(parser_);
  PerformanceMonitor::ReportGenericViolation(
      this, PerformanceMonitor::kDiscouragedAPIUse,
      "Avoid using document.write().", 0, nullptr);
  probe::breakableLocation(this, "Document.write");
  parser_->insert(text);
}

void Document::write(const String& text,
                     Document* entered_document,
                     ExceptionState& exception_state) {
  write(SegmentedString(text), entered_document, exception_state);
}

void Document::writeln(const String& text,
                       Document* entered_document,
                       ExceptionState& exception_state) {
  write(text, entered_document, exception_state);
  if (exception_state.HadException())
    return;
  write("\n", entered_document);
}

void Document::write(LocalDOMWindow* calling_window,
                     const Vector<String>& text,
                     ExceptionState& exception_state) {
  DCHECK(calling_window);
  StringBuilder builder;
  for (const String& string : text)
    builder.Append(string);
  write(builder.ToString(), calling_window->document(), exception_state);
}

void Document::writeln(LocalDOMWindow* calling_window,
                       const Vector<String>& text,
                       ExceptionState& exception_state) {
  DCHECK(calling_window);
  StringBuilder builder;
  for (const String& string : text)
    builder.Append(string);
  writeln(builder.ToString(), calling_window->document(), exception_state);
}

const KURL& Document::VirtualURL() const {
  return url_;
}

KURL Document::VirtualCompleteURL(const String& url) const {
  return CompleteURL(url);
}

DOMTimerCoordinator* Document::Timers() {
  return &timers_;
}

EventTarget* Document::ErrorEventTarget() {
  return domWindow();
}

void Document::ExceptionThrown(ErrorEvent* event) {
  MainThreadDebugger::Instance()->ExceptionThrown(this, event);
}

KURL Document::urlForBinding() {
  if (!Url().IsNull()) {
    return Url();
  }
  return BlankURL();
}

void Document::SetURL(const KURL& url) {
  const KURL& new_url = url.IsEmpty() ? BlankURL() : url;
  if (new_url == url_)
    return;

  url_ = new_url;
  access_entry_from_url_ = nullptr;
  UpdateBaseURL();
  GetContextFeatures().UrlDidChange(this);
}

KURL Document::ValidBaseElementURL() const {
  if (base_element_url_.IsValid())
    return base_element_url_;

  return KURL();
}

void Document::UpdateBaseURL() {
  KURL old_base_url = base_url_;
  // DOM 3 Core: When the Document supports the feature "HTML" [DOM Level 2
  // HTML], the base URI is computed using first the value of the href attribute
  // of the HTML BASE element if any, and the value of the documentURI attribute
  // from the Document interface otherwise (which we store, preparsed, in
  // m_url).
  if (!base_element_url_.IsEmpty())
    base_url_ = base_element_url_;
  else if (!base_url_override_.IsEmpty())
    base_url_ = base_url_override_;
  else
    base_url_ = url_;

  GetSelectorQueryCache().Invalidate();

  if (!base_url_.IsValid())
    base_url_ = KURL();

  if (elem_sheet_) {
    // Element sheet is silly. It never contains anything.
    DCHECK(!elem_sheet_->Contents()->RuleCount());
    elem_sheet_ = CSSStyleSheet::CreateInline(*this, base_url_);
  }

  if (!EqualIgnoringFragmentIdentifier(old_base_url, base_url_)) {
    // Base URL change changes any relative visited links.
    // FIXME: There are other URLs in the tree that would need to be
    // re-evaluated on dynamic base URL change. Style should be invalidated too.
    for (HTMLAnchorElement& anchor :
         Traversal<HTMLAnchorElement>::StartsAfter(*this))
      anchor.InvalidateCachedVisitedLinkHash();
  }
}

const KURL& Document::BaseURL() const {
  if (!base_url_.IsNull())
    return base_url_;
  return BlankURL();
}

void Document::SetBaseURLOverride(const KURL& url) {
  base_url_override_ = url;
  UpdateBaseURL();
}

void Document::ProcessBaseElement() {
  UseCounter::Count(*this, UseCounter::kBaseElement);

  // Find the first href attribute in a base element and the first target
  // attribute in a base element.
  const AtomicString* href = 0;
  const AtomicString* target = 0;
  for (HTMLBaseElement* base = Traversal<HTMLBaseElement>::FirstWithin(*this);
       base && (!href || !target);
       base = Traversal<HTMLBaseElement>::Next(*base)) {
    if (!href) {
      const AtomicString& value = base->FastGetAttribute(hrefAttr);
      if (!value.IsNull())
        href = &value;
    }
    if (!target) {
      const AtomicString& value = base->FastGetAttribute(targetAttr);
      if (!value.IsNull())
        target = &value;
    }
    if (GetContentSecurityPolicy()->IsActive()) {
      UseCounter::Count(*this,
                        UseCounter::kContentSecurityPolicyWithBaseElement);
    }
  }

  // FIXME: Since this doesn't share code with completeURL it may not handle
  // encodings correctly.
  KURL base_element_url;
  if (href) {
    String stripped_href = StripLeadingAndTrailingHTMLSpaces(*href);
    if (!stripped_href.IsEmpty())
      base_element_url = KURL(Url(), stripped_href);
  }

  if (!base_element_url.IsEmpty()) {
    if (base_element_url.ProtocolIsData()) {
      UseCounter::Count(*this, UseCounter::kBaseWithDataHref);
      AddConsoleMessage(ConsoleMessage::Create(
          kSecurityMessageSource, kErrorMessageLevel,
          "'data:' URLs may not be used as base URLs for a document."));
    }
    if (!this->GetSecurityOrigin()->CanRequest(base_element_url))
      UseCounter::Count(*this, UseCounter::kBaseWithCrossOriginHref);
  }

  if (base_element_url != base_element_url_ &&
      !base_element_url.ProtocolIsData() &&
      GetContentSecurityPolicy()->AllowBaseURI(base_element_url)) {
    base_element_url_ = base_element_url;
    UpdateBaseURL();
  }

  if (target) {
    if (target->Contains('\n') || target->Contains('\r'))
      UseCounter::Count(*this, UseCounter::kBaseWithNewlinesInTarget);
    if (target->Contains('<'))
      UseCounter::Count(*this, UseCounter::kBaseWithOpenBracketInTarget);
    base_target_ = *target;
  } else {
    base_target_ = g_null_atom;
  }
}

String Document::UserAgent() const {
  return GetFrame() ? GetFrame()->Loader().UserAgent() : String();
}

void Document::DisableEval(const String& error_message) {
  if (!GetFrame())
    return;

  GetFrame()->GetScriptController().DisableEval(error_message);
}

void Document::DidLoadAllImports() {
  if (!HaveScriptBlockingStylesheetsLoaded())
    return;
  if (!ImportLoader())
    StyleResolverMayHaveChanged();
  DidLoadAllScriptBlockingResources();
}

void Document::DidAddPendingStylesheetInBody() {
  if (ScriptableDocumentParser* parser = GetScriptableDocumentParser())
    parser->DidAddPendingStylesheetInBody();
}

void Document::DidRemoveAllPendingStylesheet() {
  StyleResolverMayHaveChanged();

  // Only imports on master documents can trigger rendering.
  if (HTMLImportLoader* import = ImportLoader())
    import->DidRemoveAllPendingStylesheet();
  if (!HaveImportsLoaded())
    return;
  DidLoadAllScriptBlockingResources();
}

void Document::DidRemoveAllPendingBodyStylesheets() {
  if (ScriptableDocumentParser* parser = GetScriptableDocumentParser())
    parser->DidLoadAllBodyStylesheets();
}

void Document::DidLoadAllScriptBlockingResources() {
  // Use wrapWeakPersistent because the task should not keep this Document alive
  // just for executing scripts.
  execute_scripts_waiting_for_resources_task_handle_ =
      TaskRunnerHelper::Get(TaskType::kNetworking, this)
          ->PostCancellableTask(
              BLINK_FROM_HERE,
              WTF::Bind(&Document::ExecuteScriptsWaitingForResources,
                        WrapWeakPersistent(this)));

  if (IsHTMLDocument() && body()) {
    // For HTML if we have no more stylesheets to load and we're past the body
    // tag, we should have something to paint so resume.
    BeginLifecycleUpdatesIfRenderingReady();
  } else if (!IsHTMLDocument() && documentElement()) {
    // For non-HTML there is no body so resume as soon as the sheets are loaded.
    BeginLifecycleUpdatesIfRenderingReady();
  }

  if (goto_anchor_needed_after_stylesheets_load_ && View())
    View()->ProcessUrlFragment(url_);
}

void Document::ExecuteScriptsWaitingForResources() {
  if (!IsScriptExecutionReady())
    return;
  if (ScriptableDocumentParser* parser = GetScriptableDocumentParser())
    parser->ExecuteScriptsWaitingForResources();
}

CSSStyleSheet& Document::ElementSheet() {
  if (!elem_sheet_)
    elem_sheet_ = CSSStyleSheet::CreateInline(*this, base_url_);
  return *elem_sheet_;
}

void Document::MaybeHandleHttpRefresh(const String& content,
                                      HttpRefreshType http_refresh_type) {
  if (is_view_source_ || !frame_)
    return;

  double delay;
  String refresh_url_string;
  if (!ParseHTTPRefresh(content,
                        http_refresh_type == kHttpRefreshFromMetaTag
                            ? IsHTMLSpace<UChar>
                            : nullptr,
                        delay, refresh_url_string))
    return;
  KURL refresh_url =
      refresh_url_string.IsEmpty() ? Url() : CompleteURL(refresh_url_string);

  if (refresh_url.ProtocolIsJavaScript()) {
    String message =
        "Refused to refresh " + url_.ElidedString() + " to a javascript: URL";
    AddConsoleMessage(ConsoleMessage::Create(kSecurityMessageSource,
                                             kErrorMessageLevel, message));
    return;
  }

  if (http_refresh_type == kHttpRefreshFromMetaTag &&
      IsSandboxed(kSandboxAutomaticFeatures)) {
    String message =
        "Refused to execute the redirect specified via '<meta "
        "http-equiv='refresh' content='...'>'. The document is sandboxed, and "
        "the 'allow-scripts' keyword is not set.";
    AddConsoleMessage(ConsoleMessage::Create(kSecurityMessageSource,
                                             kErrorMessageLevel, message));
    return;
  }
  frame_->GetNavigationScheduler().ScheduleRedirect(delay, refresh_url);
}

bool Document::ShouldMergeWithLegacyDescription(
    ViewportDescription::Type origin) const {
  return GetSettings() && GetSettings()->GetViewportMetaMergeContentQuirk() &&
         legacy_viewport_description_.IsMetaViewportType() &&
         legacy_viewport_description_.type == origin;
}

void Document::SetViewportDescription(
    const ViewportDescription& viewport_description) {
  if (viewport_description.IsLegacyViewportType()) {
    if (viewport_description == legacy_viewport_description_)
      return;
    legacy_viewport_description_ = viewport_description;
  } else {
    if (viewport_description == viewport_description_)
      return;
    viewport_description_ = viewport_description;

    // The UA-defined min-width is considered specifically by Android WebView
    // quirks mode.
    if (!viewport_description.IsSpecifiedByAuthor())
      viewport_default_min_width_ = viewport_description.min_width;
  }

  UpdateViewportDescription();
}

ViewportDescription Document::GetViewportDescription() const {
  ViewportDescription applied_viewport_description = viewport_description_;
  bool viewport_meta_enabled =
      GetSettings() && GetSettings()->GetViewportMetaEnabled();
  if (legacy_viewport_description_.type !=
          ViewportDescription::kUserAgentStyleSheet &&
      viewport_meta_enabled)
    applied_viewport_description = legacy_viewport_description_;
  if (ShouldOverrideLegacyDescription(viewport_description_.type))
    applied_viewport_description = viewport_description_;

  return applied_viewport_description;
}

void Document::UpdateViewportDescription() {
  if (GetFrame() && GetFrame()->IsMainFrame()) {
    GetPage()->GetChromeClient().DispatchViewportPropertiesDidChange(
        GetViewportDescription());
  }
}

String Document::OutgoingReferrer() const {
  if (GetSecurityOrigin()->IsUnique()) {
    // Return |no-referrer|.
    return String();
  }

  // See http://www.whatwg.org/specs/web-apps/current-work/#fetching-resources
  // for why we walk the parent chain for srcdoc documents.
  const Document* referrer_document = this;
  if (LocalFrame* frame = frame_) {
    while (frame->GetDocument()->IsSrcdocDocument()) {
      // Srcdoc documents must be local within the containing frame.
      frame = ToLocalFrame(frame->Tree().Parent());
      // Srcdoc documents cannot be top-level documents, by definition,
      // because they need to be contained in iframes with the srcdoc.
      DCHECK(frame);
    }
    referrer_document = frame->GetDocument();
  }
  return referrer_document->url_.StrippedForUseAsReferrer();
}

ReferrerPolicy Document::GetReferrerPolicy() const {
  ReferrerPolicy policy = ExecutionContext::GetReferrerPolicy();
  // For srcdoc documents without their own policy, walk up the frame
  // tree to find the document that is either not a srcdoc or doesn't
  // have its own policy. This algorithm is defined in
  // https://html.spec.whatwg.org/multipage/browsers.html#set-up-a-browsing-context-environment-settings-object.
  if (!frame_ || policy != kReferrerPolicyDefault || !IsSrcdocDocument()) {
    return policy;
  }
  LocalFrame* frame = ToLocalFrame(frame_->Tree().Parent());
  DCHECK(frame);
  return frame->GetDocument()->GetReferrerPolicy();
}

MouseEventWithHitTestResults Document::PerformMouseEventHitTest(
    const HitTestRequest& request,
    const LayoutPoint& document_point,
    const WebMouseEvent& event) {
  DCHECK(GetLayoutViewItem().IsNull() || GetLayoutViewItem().IsLayoutView());

  // LayoutView::hitTest causes a layout, and we don't want to hit that until
  // the first layout because until then, there is nothing shown on the screen -
  // the user can't have intentionally clicked on something belonging to this
  // page.  Furthermore, mousemove events before the first layout should not
  // lead to a premature layout() happening, which could show a flash of white.
  // See also the similar code in EventHandler::hitTestResultAtPoint.
  if (GetLayoutViewItem().IsNull() || !View() || !View()->DidFirstLayout())
    return MouseEventWithHitTestResults(event,
                                        HitTestResult(request, LayoutPoint()));

  HitTestResult result(request, document_point);
  GetLayoutViewItem().HitTest(result);

  if (!request.ReadOnly())
    UpdateHoverActiveState(request, result.InnerElement());

  if (isHTMLCanvasElement(result.InnerNode())) {
    HitTestCanvasResult* hit_test_canvas_result =
        toHTMLCanvasElement(result.InnerNode())
            ->GetControlAndIdIfHitRegionExists(result.PointInInnerNodeFrame());
    if (hit_test_canvas_result->GetControl()) {
      result.SetInnerNode(hit_test_canvas_result->GetControl());
    }
    result.SetCanvasRegionId(hit_test_canvas_result->GetId());
  }

  return MouseEventWithHitTestResults(event, result);
}

// DOM Section 1.1.1
bool Document::ChildTypeAllowed(NodeType type) const {
  switch (type) {
    case kAttributeNode:
    case kCdataSectionNode:
    case kDocumentFragmentNode:
    case kDocumentNode:
    case kTextNode:
      return false;
    case kCommentNode:
    case kProcessingInstructionNode:
      return true;
    case kDocumentTypeNode:
    case kElementNode:
      // Documents may contain no more than one of each of these.
      // (One Element and one DocumentType.)
      for (Node& c : NodeTraversal::ChildrenOf(*this)) {
        if (c.getNodeType() == type)
          return false;
      }
      return true;
  }
  return false;
}

bool Document::CanAcceptChild(const Node& new_child,
                              const Node* old_child,
                              ExceptionState& exception_state) const {
  if (old_child && old_child->getNodeType() == new_child.getNodeType())
    return true;

  int num_doctypes = 0;
  int num_elements = 0;

  // First, check how many doctypes and elements we have, not counting
  // the child we're about to remove.
  for (Node& child : NodeTraversal::ChildrenOf(*this)) {
    if (old_child && *old_child == child)
      continue;

    switch (child.getNodeType()) {
      case kDocumentTypeNode:
        num_doctypes++;
        break;
      case kElementNode:
        num_elements++;
        break;
      default:
        break;
    }
  }

  // Then, see how many doctypes and elements might be added by the new child.
  if (new_child.IsDocumentFragment()) {
    for (Node& child :
         NodeTraversal::ChildrenOf(ToDocumentFragment(new_child))) {
      switch (child.getNodeType()) {
        case kAttributeNode:
        case kCdataSectionNode:
        case kDocumentFragmentNode:
        case kDocumentNode:
        case kTextNode:
          exception_state.ThrowDOMException(
              kHierarchyRequestError,
              "Nodes of type '" + new_child.nodeName() +
                  "' may not be inserted inside nodes of type '#document'.");
          return false;
        case kCommentNode:
        case kProcessingInstructionNode:
          break;
        case kDocumentTypeNode:
          num_doctypes++;
          break;
        case kElementNode:
          num_elements++;
          break;
      }
    }
  } else {
    switch (new_child.getNodeType()) {
      case kAttributeNode:
      case kCdataSectionNode:
      case kDocumentFragmentNode:
      case kDocumentNode:
      case kTextNode:
        exception_state.ThrowDOMException(
            kHierarchyRequestError,
            "Nodes of type '" + new_child.nodeName() +
                "' may not be inserted inside nodes of type '#document'.");
        return false;
      case kCommentNode:
      case kProcessingInstructionNode:
        return true;
      case kDocumentTypeNode:
        num_doctypes++;
        break;
      case kElementNode:
        num_elements++;
        break;
    }
  }

  if (num_elements > 1 || num_doctypes > 1) {
    exception_state.ThrowDOMException(
        kHierarchyRequestError,
        String::Format("Only one %s on document allowed.",
                       num_elements > 1 ? "element" : "doctype"));
    return false;
  }

  return true;
}

Node* Document::cloneNode(bool deep, ExceptionState&) {
  Document* clone = CloneDocumentWithoutChildren();
  clone->CloneDataFromDocument(*this);
  if (deep)
    CloneChildNodes(clone);
  return clone;
}

Document* Document::CloneDocumentWithoutChildren() {
  DocumentInit init(Url());
  if (IsXMLDocument()) {
    if (IsXHTMLDocument())
      return XMLDocument::CreateXHTML(
          init.WithRegistrationContext(RegistrationContext()));
    return XMLDocument::Create(init);
  }
  return Create(init);
}

void Document::CloneDataFromDocument(const Document& other) {
  SetCompatibilityMode(other.GetCompatibilityMode());
  SetEncodingData(other.encoding_data_);
  SetContextFeatures(other.GetContextFeatures());
  SetSecurityOrigin(other.GetSecurityOrigin()->IsolatedCopy());
  SetMimeType(other.contentType());
}

bool Document::IsSecureContextImpl() const {
  // There may be exceptions for the secure context check defined for certain
  // schemes. The exceptions are applied only to the special scheme and to
  // sandboxed URLs from those origins, but *not* to any children.
  //
  // For example:
  //   <iframe src="http://host">
  //     <iframe src="scheme-has-exception://host"></iframe>
  //     <iframe sandbox src="scheme-has-exception://host"></iframe>
  //   </iframe>
  // both inner iframes pass this check, assuming that the scheme
  // "scheme-has-exception:" is granted an exception.
  //
  // However,
  //   <iframe src="http://host">
  //     <iframe sandbox src="http://host"></iframe>
  //   </iframe>
  // would fail the check (that is, sandbox does not grant an exception itself).
  //
  // Additionally, with
  //   <iframe src="scheme-has-exception://host">
  //     <iframe src="http://host"></iframe>
  //     <iframe sandbox src="http://host"></iframe>
  //   </iframe>
  // both inner iframes would fail the check, even though the outermost iframe
  // passes.
  //
  // In all cases, a frame must be potentially trustworthy in addition to
  // having an exception listed in order for the exception to be granted.
  if (!GetSecurityOrigin()->IsPotentiallyTrustworthy())
    return false;

  if (SchemeRegistry::SchemeShouldBypassSecureContextCheck(
          GetSecurityOrigin()->Protocol()))
    return true;

  if (!frame_)
    return true;
  Frame* parent = frame_->Tree().Parent();
  while (parent) {
    if (!parent->GetSecurityContext()
             ->GetSecurityOrigin()
             ->IsPotentiallyTrustworthy())
      return false;
    parent = parent->Tree().Parent();
  }
  return true;
}

StyleSheetList& Document::StyleSheets() {
  if (!style_sheet_list_)
    style_sheet_list_ = StyleSheetList::Create(this);
  return *style_sheet_list_;
}

String Document::preferredStylesheetSet() const {
  return style_engine_->PreferredStylesheetSetName();
}

String Document::selectedStylesheetSet() const {
  return style_engine_->SelectedStylesheetSetName();
}

void Document::setSelectedStylesheetSet(const String& a_string) {
  GetStyleEngine().SetSelectedStylesheetSetName(a_string);
}

void Document::EvaluateMediaQueryListIfNeeded() {
  if (!evaluate_media_queries_on_style_recalc_)
    return;
  EvaluateMediaQueryList();
  evaluate_media_queries_on_style_recalc_ = false;
}

void Document::EvaluateMediaQueryList() {
  if (media_query_matcher_)
    media_query_matcher_->MediaFeaturesChanged();
}

void Document::SetResizedForViewportUnits() {
  if (media_query_matcher_)
    media_query_matcher_->ViewportChanged();
  if (!HasViewportUnits())
    return;
  EnsureStyleResolver().SetResizedForViewportUnits();
  SetNeedsStyleRecalcForViewportUnits();
}

void Document::ClearResizedForViewportUnits() {
  EnsureStyleResolver().ClearResizedForViewportUnits();
}

void Document::StyleResolverMayHaveChanged() {
  if (HasNodesWithPlaceholderStyle()) {
    SetNeedsStyleRecalc(kSubtreeStyleChange,
                        StyleChangeReasonForTracing::Create(
                            StyleChangeReason::kCleanupPlaceholderStyles));
  }

  if (DidLayoutWithPendingStylesheets() &&
      !GetStyleEngine().HasPendingScriptBlockingSheets()) {
    // We need to manually repaint because we avoid doing all repaints in layout
    // or style recalc while sheets are still loading to avoid FOUC.
    pending_sheet_layout_ = kIgnoreLayoutWithPendingSheets;

    DCHECK(!GetLayoutViewItem().IsNull() || ImportsController());
    if (!GetLayoutViewItem().IsNull())
      GetLayoutViewItem().InvalidatePaintForViewAndCompositedLayers();
  }
}

void Document::SetHoverElement(Element* new_hover_element) {
  hover_element_ = new_hover_element;
}

void Document::SetActiveHoverElement(Element* new_active_element) {
  if (!new_active_element) {
    active_hover_element_.Clear();
    return;
  }

  active_hover_element_ = new_active_element;
}

void Document::RemoveFocusedElementOfSubtree(Node* node,
                                             bool among_children_only) {
  if (!focused_element_)
    return;

  // We can't be focused if we're not in the document.
  if (!node->isConnected())
    return;
  bool contains =
      node->IsShadowIncludingInclusiveAncestorOf(focused_element_.Get());
  if (contains && (focused_element_ != node || !among_children_only))
    ClearFocusedElement();
}

static Element* SkipDisplayNoneAncestors(Element* element) {
  for (; element; element = FlatTreeTraversal::ParentElement(*element)) {
    if (element->GetLayoutObject() || element->HasDisplayContentsStyle())
      return element;
  }
  return nullptr;
}

void Document::HoveredElementDetached(Element& element) {
  if (!hover_element_)
    return;
  if (element != hover_element_)
    return;

  hover_element_->UpdateDistribution();
  hover_element_ = SkipDisplayNoneAncestors(&element);

  // If the mouse cursor is not visible, do not clear existing
  // hover effects on the ancestors of |element| and do not invoke
  // new hover effects on any other element.
  if (!GetPage()->IsCursorVisible())
    return;

  if (GetFrame())
    GetFrame()->GetEventHandler().ScheduleHoverStateUpdate();
}

void Document::ActiveChainNodeDetached(Element& element) {
  if (element == active_hover_element_)
    active_hover_element_ = SkipDisplayNoneAncestors(&element);
}

const Vector<AnnotatedRegionValue>& Document::AnnotatedRegions() const {
  return annotated_regions_;
}

void Document::SetAnnotatedRegions(
    const Vector<AnnotatedRegionValue>& regions) {
  annotated_regions_ = regions;
  SetAnnotatedRegionsDirty(false);
}

bool Document::SetFocusedElement(Element* new_focused_element,
                                 const FocusParams& params) {
  DCHECK(!lifecycle_.InDetach());

  clear_focused_element_timer_.Stop();

  // Make sure newFocusedNode is actually in this document
  if (new_focused_element && (new_focused_element->GetDocument() != this))
    return true;

  if (NodeChildRemovalTracker::IsBeingRemoved(new_focused_element))
    return true;

  if (focused_element_ == new_focused_element)
    return true;

  bool focus_change_blocked = false;
  Element* old_focused_element = focused_element_;
  focused_element_ = nullptr;

  UpdateDistribution();
  Node* ancestor = (old_focused_element && old_focused_element->isConnected() &&
                    new_focused_element)
                       ? FlatTreeTraversal::CommonAncestor(*old_focused_element,
                                                           *new_focused_element)
                       : nullptr;

  // Remove focus from the existing focus node (if any)
  if (old_focused_element) {
    old_focused_element->SetFocused(false, params.type);
    old_focused_element->SetHasFocusWithinUpToAncestor(false, ancestor);

    // Dispatch the blur event and let the node do any other blur related
    // activities (important for text fields)
    // If page lost focus, blur event will have already been dispatched
    if (GetPage() && (GetPage()->GetFocusController().IsFocused())) {
      old_focused_element->DispatchBlurEvent(new_focused_element, params.type,
                                             params.source_capabilities);
      if (focused_element_) {
        // handler shifted focus
        focus_change_blocked = true;
        new_focused_element = nullptr;
      }

      // 'focusout' is a DOM level 3 name for the bubbling blur event.
      old_focused_element->DispatchFocusOutEvent(EventTypeNames::focusout,
                                                 new_focused_element,
                                                 params.source_capabilities);
      // 'DOMFocusOut' is a DOM level 2 name for compatibility.
      // FIXME: We should remove firing DOMFocusOutEvent event when we are sure
      // no content depends on it, probably when <rdar://problem/8503958> is
      // resolved.
      old_focused_element->DispatchFocusOutEvent(EventTypeNames::DOMFocusOut,
                                                 new_focused_element,
                                                 params.source_capabilities);

      if (focused_element_) {
        // handler shifted focus
        focus_change_blocked = true;
        new_focused_element = nullptr;
      }
    }
  }

  if (new_focused_element)
    UpdateStyleAndLayoutTreeForNode(new_focused_element);
  if (new_focused_element && new_focused_element->IsFocusable()) {
    if (IsRootEditableElement(*new_focused_element) &&
        !AcceptsEditingFocus(*new_focused_element)) {
      // delegate blocks focus change
      focus_change_blocked = true;
      goto SetFocusedElementDone;
    }
    // Set focus on the new node
    focused_element_ = new_focused_element;
    SetSequentialFocusNavigationStartingPoint(focused_element_.Get());

    focused_element_->SetFocused(true, params.type);
    focused_element_->SetHasFocusWithinUpToAncestor(true, ancestor);

    // Element::setFocused for frames can dispatch events.
    if (focused_element_ != new_focused_element) {
      focus_change_blocked = true;
      goto SetFocusedElementDone;
    }
    CancelFocusAppearanceUpdate();
    focused_element_->UpdateFocusAppearance(params.selection_behavior);

    // Dispatch the focus event and let the node do any other focus related
    // activities (important for text fields)
    // If page lost focus, event will be dispatched on page focus, don't
    // duplicate
    if (GetPage() && (GetPage()->GetFocusController().IsFocused())) {
      focused_element_->DispatchFocusEvent(old_focused_element, params.type,
                                           params.source_capabilities);

      if (focused_element_ != new_focused_element) {
        // handler shifted focus
        focus_change_blocked = true;
        goto SetFocusedElementDone;
      }
      // DOM level 3 bubbling focus event.
      focused_element_->DispatchFocusInEvent(EventTypeNames::focusin,
                                             old_focused_element, params.type,
                                             params.source_capabilities);

      if (focused_element_ != new_focused_element) {
        // handler shifted focus
        focus_change_blocked = true;
        goto SetFocusedElementDone;
      }

      // For DOM level 2 compatibility.
      // FIXME: We should remove firing DOMFocusInEvent event when we are sure
      // no content depends on it, probably when <rdar://problem/8503958> is m.
      focused_element_->DispatchFocusInEvent(EventTypeNames::DOMFocusIn,
                                             old_focused_element, params.type,
                                             params.source_capabilities);

      if (focused_element_ != new_focused_element) {
        // handler shifted focus
        focus_change_blocked = true;
        goto SetFocusedElementDone;
      }
    }

    if (IsRootEditableElement(*focused_element_))
      GetFrame()->GetSpellChecker().DidBeginEditing(focused_element_.Get());
  }

  if (!focus_change_blocked && focused_element_) {
    // Create the AXObject cache in a focus change because Chromium relies on
    // it.
    if (AXObjectCache* cache = AxObjectCache())
      cache->HandleFocusedUIElementChanged(old_focused_element,
                                           new_focused_element);
  }

  if (!focus_change_blocked && GetPage()) {
    GetPage()->GetChromeClient().FocusedNodeChanged(old_focused_element,
                                                    focused_element_.Get());
  }

SetFocusedElementDone:
  UpdateStyleAndLayoutTree();
  if (LocalFrame* frame = this->GetFrame())
    frame->Selection().DidChangeFocus();
  return !focus_change_blocked;
}

void Document::ClearFocusedElement() {
  SetFocusedElement(nullptr, FocusParams(SelectionBehaviorOnFocus::kNone,
                                         kWebFocusTypeNone, nullptr));
}

void Document::SetSequentialFocusNavigationStartingPoint(Node* node) {
  if (!frame_)
    return;
  if (!node) {
    sequential_focus_navigation_starting_point_ = nullptr;
    return;
  }
  DCHECK_EQ(node->GetDocument(), this);
  if (!sequential_focus_navigation_starting_point_)
    sequential_focus_navigation_starting_point_ = Range::Create(*this);
  sequential_focus_navigation_starting_point_->selectNodeContents(
      node, ASSERT_NO_EXCEPTION);
}

Element* Document::SequentialFocusNavigationStartingPoint(
    WebFocusType type) const {
  if (focused_element_)
    return focused_element_.Get();
  if (!sequential_focus_navigation_starting_point_)
    return nullptr;
  if (!sequential_focus_navigation_starting_point_->collapsed()) {
    Node* node = sequential_focus_navigation_starting_point_->startContainer();
    DCHECK_EQ(node,
              sequential_focus_navigation_starting_point_->endContainer());
    if (node->IsElementNode())
      return ToElement(node);
    if (Element* neighbor_element = type == kWebFocusTypeForward
                                        ? ElementTraversal::Previous(*node)
                                        : ElementTraversal::Next(*node))
      return neighbor_element;
    return node->ParentOrShadowHostElement();
  }

  // Range::selectNodeContents didn't select contents because the element had
  // no children.
  if (sequential_focus_navigation_starting_point_->startContainer()
          ->IsElementNode() &&
      !sequential_focus_navigation_starting_point_->startContainer()
           ->hasChildren() &&
      sequential_focus_navigation_starting_point_->startOffset() == 0)
    return ToElement(
        sequential_focus_navigation_starting_point_->startContainer());

  // A node selected by Range::selectNodeContents was removed from the
  // document tree.
  if (Node* next_node =
          sequential_focus_navigation_starting_point_->FirstNode()) {
    if (type == kWebFocusTypeForward)
      return ElementTraversal::Previous(*next_node);
    if (next_node->IsElementNode())
      return ToElement(next_node);
    return ElementTraversal::Next(*next_node);
  }
  return nullptr;
}

void Document::SetCSSTarget(Element* new_target) {
  if (css_target_)
    css_target_->PseudoStateChanged(CSSSelector::kPseudoTarget);
  css_target_ = new_target;
  if (css_target_)
    css_target_->PseudoStateChanged(CSSSelector::kPseudoTarget);
}

static void LiveNodeListBaseWriteBarrier(void* parent,
                                         const LiveNodeListBase* list) {
  if (IsHTMLCollectionType(list->GetType())) {
    ScriptWrappableVisitor::WriteBarrier(
        parent, static_cast<const HTMLCollection*>(list));
  } else {
    ScriptWrappableVisitor::WriteBarrier(
        parent, static_cast<const LiveNodeList*>(list));
  }
}

void Document::RegisterNodeList(const LiveNodeListBase* list) {
  DCHECK(!node_lists_[list->InvalidationType()].Contains(list));
  node_lists_[list->InvalidationType()].insert(list);
  LiveNodeListBaseWriteBarrier(this, list);
  if (list->IsRootedAtTreeScope())
    lists_invalidated_at_document_.insert(list);
}

void Document::UnregisterNodeList(const LiveNodeListBase* list) {
  DCHECK(node_lists_[list->InvalidationType()].Contains(list));
  node_lists_[list->InvalidationType()].erase(list);
  if (list->IsRootedAtTreeScope()) {
    DCHECK(lists_invalidated_at_document_.Contains(list));
    lists_invalidated_at_document_.erase(list);
  }
}

void Document::RegisterNodeListWithIdNameCache(const LiveNodeListBase* list) {
  DCHECK(!node_lists_[kInvalidateOnIdNameAttrChange].Contains(list));
  node_lists_[kInvalidateOnIdNameAttrChange].insert(list);
  LiveNodeListBaseWriteBarrier(this, list);
}

void Document::UnregisterNodeListWithIdNameCache(const LiveNodeListBase* list) {
  DCHECK(node_lists_[kInvalidateOnIdNameAttrChange].Contains(list));
  node_lists_[kInvalidateOnIdNameAttrChange].erase(list);
}

void Document::AttachNodeIterator(NodeIterator* ni) {
  node_iterators_.insert(ni);
}

void Document::DetachNodeIterator(NodeIterator* ni) {
  // The node iterator can be detached without having been attached if its root
  // node didn't have a document when the iterator was created, but has it now.
  node_iterators_.erase(ni);
}

void Document::MoveNodeIteratorsToNewDocument(Node& node,
                                              Document& new_document) {
  HeapHashSet<WeakMember<NodeIterator>> node_iterators_list = node_iterators_;
  for (NodeIterator* ni : node_iterators_list) {
    if (ni->root() == node) {
      DetachNodeIterator(ni);
      new_document.AttachNodeIterator(ni);
    }
  }
}

void Document::DidMoveTreeToNewDocument(const Node& root) {
  DCHECK_NE(root.GetDocument(), this);
  if (!ranges_.IsEmpty()) {
    AttachedRangeSet ranges = ranges_;
    for (Range* range : ranges)
      range->UpdateOwnerDocumentIfNeeded();
  }
  NotifyMoveTreeToNewDocument(root);
}

void Document::NodeChildrenWillBeRemoved(ContainerNode& container) {
  EventDispatchForbiddenScope assert_no_event_dispatch;
  for (Range* range : ranges_)
    range->NodeChildrenWillBeRemoved(container);

  for (NodeIterator* ni : node_iterators_) {
    for (Node& n : NodeTraversal::ChildrenOf(container))
      ni->NodeWillBeRemoved(n);
  }

  NotifyNodeChildrenWillBeRemoved(container);

  if (ContainsV1ShadowTree()) {
    for (Node& n : NodeTraversal::ChildrenOf(container))
      n.CheckSlotChangeBeforeRemoved();
  }
}

void Document::NodeWillBeRemoved(Node& n) {
  for (NodeIterator* ni : node_iterators_)
    ni->NodeWillBeRemoved(n);

  for (Range* range : ranges_)
    range->NodeWillBeRemoved(n);

  NotifyNodeWillBeRemoved(n);

  if (ContainsV1ShadowTree())
    n.CheckSlotChangeBeforeRemoved();

  if (n.InActiveDocument() && n.IsElementNode())
    GetStyleEngine().ElementWillBeRemoved(ToElement(n));
}

void Document::DidInsertText(const CharacterData& text,
                             unsigned offset,
                             unsigned length) {
  for (Range* range : ranges_)
    range->DidInsertText(text, offset, length);
}

void Document::DidRemoveText(const CharacterData& text,
                             unsigned offset,
                             unsigned length) {
  for (Range* range : ranges_)
    range->DidRemoveText(text, offset, length);
}

void Document::DidMergeTextNodes(const Text& merged_node,
                                 const Text& node_to_be_removed,
                                 unsigned old_length) {
  NodeWithIndex node_to_be_removed_with_index(
      const_cast<Text&>(node_to_be_removed));
  if (!ranges_.IsEmpty()) {
    for (Range* range : ranges_)
      range->DidMergeTextNodes(node_to_be_removed_with_index, old_length);
  }

  NotifyMergeTextNodes(merged_node, node_to_be_removed_with_index, old_length);

  // FIXME: This should update markers for spelling and grammar checking.
}

void Document::DidSplitTextNode(const Text& old_node) {
  for (Range* range : ranges_)
    range->DidSplitTextNode(old_node);

  NotifySplitTextNode(old_node);

  // FIXME: This should update markers for spelling and grammar checking.
}

void Document::SetWindowAttributeEventListener(const AtomicString& event_type,
                                               EventListener* listener) {
  LocalDOMWindow* dom_window = this->domWindow();
  if (!dom_window)
    return;
  dom_window->SetAttributeEventListener(event_type, listener);
}

EventListener* Document::GetWindowAttributeEventListener(
    const AtomicString& event_type) {
  LocalDOMWindow* dom_window = this->domWindow();
  if (!dom_window)
    return 0;
  return dom_window->GetAttributeEventListener(event_type);
}

EventQueue* Document::GetEventQueue() const {
  if (!dom_window_)
    return 0;
  return dom_window_->GetEventQueue();
}

void Document::EnqueueAnimationFrameTask(std::unique_ptr<WTF::Closure> task) {
  EnsureScriptedAnimationController().EnqueueTask(std::move(task));
}

void Document::EnqueueAnimationFrameEvent(Event* event) {
  EnsureScriptedAnimationController().EnqueueEvent(event);
}

void Document::EnqueueUniqueAnimationFrameEvent(Event* event) {
  EnsureScriptedAnimationController().EnqueuePerFrameEvent(event);
}

void Document::EnqueueScrollEventForNode(Node* target) {
  // Per the W3C CSSOM View Module only scroll events fired at the document
  // should bubble.
  Event* scroll_event = target->IsDocumentNode()
                            ? Event::CreateBubble(EventTypeNames::scroll)
                            : Event::Create(EventTypeNames::scroll);
  scroll_event->SetTarget(target);
  EnsureScriptedAnimationController().EnqueuePerFrameEvent(scroll_event);
}

void Document::EnqueueResizeEvent() {
  Event* event = Event::Create(EventTypeNames::resize);
  event->SetTarget(domWindow());
  EnsureScriptedAnimationController().EnqueuePerFrameEvent(event);
}

void Document::EnqueueMediaQueryChangeListeners(
    HeapVector<Member<MediaQueryListListener>>& listeners) {
  EnsureScriptedAnimationController().EnqueueMediaQueryChangeListeners(
      listeners);
}

void Document::EnqueueVisualViewportScrollEvent() {
  VisualViewportScrollEvent* event = VisualViewportScrollEvent::Create();
  event->SetTarget(domWindow()->visualViewport());
  EnsureScriptedAnimationController().EnqueuePerFrameEvent(event);
}

void Document::EnqueueVisualViewportResizeEvent() {
  VisualViewportResizeEvent* event = VisualViewportResizeEvent::Create();
  event->SetTarget(domWindow()->visualViewport());
  EnsureScriptedAnimationController().EnqueuePerFrameEvent(event);
}

void Document::DispatchEventsForPrinting() {
  if (!scripted_animation_controller_)
    return;
  scripted_animation_controller_->DispatchEventsAndCallbacksForPrinting();
}

Document::EventFactorySet& Document::EventFactories() {
  DEFINE_STATIC_LOCAL(EventFactorySet, event_factory, ());
  return event_factory;
}

const OriginAccessEntry& Document::AccessEntryFromURL() {
  if (!access_entry_from_url_) {
    access_entry_from_url_ = WTF::WrapUnique(
        new OriginAccessEntry(Url().Protocol(), Url().Host(),
                              OriginAccessEntry::kAllowRegisterableDomains));
  }
  return *access_entry_from_url_;
}

void Document::SendSensitiveInputVisibility() {
  if (sensitive_input_visibility_task_.IsActive())
    return;

  sensitive_input_visibility_task_ =
      TaskRunnerHelper::Get(TaskType::kUnspecedLoading, this)
          ->PostCancellableTask(
              BLINK_FROM_HERE,
              WTF::Bind(&Document::SendSensitiveInputVisibilityInternal,
                        WrapWeakPersistent(this)));
}

void Document::SendSensitiveInputVisibilityInternal() {
  if (!GetFrame())
    return;

  mojom::blink::SensitiveInputVisibilityServicePtr sensitive_input_service_ptr;
  GetFrame()->GetInterfaceProvider()->GetInterface(
      mojo::MakeRequest(&sensitive_input_service_ptr));
  if (password_count_ > 0) {
    sensitive_input_service_ptr->PasswordFieldVisibleInInsecureContext();
    return;
  }
  sensitive_input_service_ptr->AllPasswordFieldsInInsecureContextInvisible();
}

void Document::RunExecutionContextTask(
    std::unique_ptr<ExecutionContextTask> task,
    bool is_instrumented) {
  probe::AsyncTask async_task(this, task.get(), nullptr, is_instrumented);
  task->PerformTask(this);
}

void Document::RegisterEventFactory(
    std::unique_ptr<EventFactoryBase> event_factory) {
  DCHECK(!EventFactories().Contains(event_factory.get()));
  EventFactories().insert(std::move(event_factory));
}

Event* Document::createEvent(ScriptState* script_state,
                             const String& event_type,
                             ExceptionState& exception_state) {
  Event* event = nullptr;
  ExecutionContext* execution_context = ExecutionContext::From(script_state);
  for (const auto& factory : EventFactories()) {
    event = factory->Create(execution_context, event_type);
    if (event) {
      // createEvent for TouchEvent should throw DOM exception if touch event
      // feature detection is not enabled. See crbug.com/392584#c22
      if (DeprecatedEqualIgnoringCase(event_type, "TouchEvent") &&
          !RuntimeEnabledFeatures::touchEventFeatureDetectionEnabled())
        break;
      return event;
    }
  }
  exception_state.ThrowDOMException(
      kNotSupportedError,
      "The provided event type ('" + event_type + "') is invalid.");
  return nullptr;
}

void Document::AddMutationEventListenerTypeIfEnabled(
    ListenerType listener_type) {
  if (ContextFeatures::MutationEventsEnabled(this))
    AddListenerType(listener_type);
}

void Document::AddListenerTypeIfNeeded(const AtomicString& event_type,
                                       EventTarget& event_target) {
  if (event_type == EventTypeNames::DOMSubtreeModified) {
    UseCounter::Count(*this, UseCounter::kDOMSubtreeModifiedEvent);
    AddMutationEventListenerTypeIfEnabled(kDOMSubtreeModifiedListener);
  } else if (event_type == EventTypeNames::DOMNodeInserted) {
    UseCounter::Count(*this, UseCounter::kDOMNodeInsertedEvent);
    AddMutationEventListenerTypeIfEnabled(kDOMNodeInsertedListener);
  } else if (event_type == EventTypeNames::DOMNodeRemoved) {
    UseCounter::Count(*this, UseCounter::kDOMNodeRemovedEvent);
    AddMutationEventListenerTypeIfEnabled(kDOMNodeRemovedListener);
  } else if (event_type == EventTypeNames::DOMNodeRemovedFromDocument) {
    UseCounter::Count(*this, UseCounter::kDOMNodeRemovedFromDocumentEvent);
    AddMutationEventListenerTypeIfEnabled(kDOMNodeRemovedFromDocumentListener);
  } else if (event_type == EventTypeNames::DOMNodeInsertedIntoDocument) {
    UseCounter::Count(*this, UseCounter::kDOMNodeInsertedIntoDocumentEvent);
    AddMutationEventListenerTypeIfEnabled(kDOMNodeInsertedIntoDocumentListener);
  } else if (event_type == EventTypeNames::DOMCharacterDataModified) {
    UseCounter::Count(*this, UseCounter::kDOMCharacterDataModifiedEvent);
    AddMutationEventListenerTypeIfEnabled(kDOMCharacterDataModifiedListener);
  } else if (event_type == EventTypeNames::webkitAnimationStart ||
             event_type == EventTypeNames::animationstart) {
    AddListenerType(kAnimationStartListener);
  } else if (event_type == EventTypeNames::webkitAnimationEnd ||
             event_type == EventTypeNames::animationend) {
    AddListenerType(kAnimationEndListener);
  } else if (event_type == EventTypeNames::webkitAnimationIteration ||
             event_type == EventTypeNames::animationiteration) {
    AddListenerType(kAnimationIterationListener);
    if (View()) {
      // Need to re-evaluate time-to-effect-change for any running animations.
      View()->ScheduleAnimation();
    }
  } else if (event_type == EventTypeNames::webkitTransitionEnd ||
             event_type == EventTypeNames::transitionend) {
    AddListenerType(kTransitionEndListener);
  } else if (event_type == EventTypeNames::scroll) {
    AddListenerType(kScrollListener);
  } else if (event_type == EventTypeNames::load) {
    if (Node* node = event_target.ToNode()) {
      if (isHTMLStyleElement(*node)) {
        AddListenerType(kLoadListenerAtCapturePhaseOrAtStyleElement);
        return;
      }
    }
    if (event_target.HasCapturingEventListeners(event_type))
      AddListenerType(kLoadListenerAtCapturePhaseOrAtStyleElement);
  }
}

HTMLFrameOwnerElement* Document::LocalOwner() const {
  if (!GetFrame())
    return 0;
  // FIXME: This probably breaks the attempts to layout after a load is finished
  // in implicitClose(), and probably tons of other things...
  return GetFrame()->DeprecatedLocalOwner();
}

void Document::WillChangeFrameOwnerProperties(int margin_width,
                                              int margin_height,
                                              ScrollbarMode scrolling_mode,
                                              bool is_display_none) {
  DCHECK(GetFrame() && GetFrame()->Owner());
  FrameOwner* owner = GetFrame()->Owner();

  if (documentElement()) {
    if (is_display_none != owner->IsDisplayNone())
      documentElement()->LazyReattachIfAttached();
  }

  if (!body())
    return;

  if (margin_width != owner->MarginWidth())
    body()->SetIntegralAttribute(marginwidthAttr, margin_width);
  if (margin_height != owner->MarginHeight())
    body()->SetIntegralAttribute(marginheightAttr, margin_height);
  if (scrolling_mode != owner->ScrollingMode() && View())
    View()->SetNeedsLayout();
}

bool Document::IsInInvisibleSubframe() const {
  if (!LocalOwner())
    return false;  // this is a local root element

  // TODO(bokan): This looks like it doesn't work in OOPIF.
  DCHECK(GetFrame());
  return GetFrame()->OwnerLayoutItem().IsNull();
}

String Document::cookie(ExceptionState& exception_state) const {
  if (GetSettings() && !GetSettings()->GetCookieEnabled())
    return String();

  // FIXME: The HTML5 DOM spec states that this attribute can raise an
  // InvalidStateError exception on getting if the Document has no
  // browsing context.

  if (!GetSecurityOrigin()->CanAccessCookies()) {
    if (IsSandboxed(kSandboxOrigin))
      exception_state.ThrowSecurityError(
          "The document is sandboxed and lacks the 'allow-same-origin' flag.");
    else if (Url().ProtocolIs("data"))
      exception_state.ThrowSecurityError(
          "Cookies are disabled inside 'data:' URLs.");
    else
      exception_state.ThrowSecurityError("Access is denied for this document.");
    return String();
  }

  // Suborigins are cookie-averse and thus should always return the empty
  // string, unless the 'unsafe-cookies' option is provided.
  if (GetSecurityOrigin()->HasSuborigin() &&
      !GetSecurityOrigin()->GetSuborigin()->PolicyContains(
          Suborigin::SuboriginPolicyOptions::kUnsafeCookies))
    return String();

  KURL cookie_url = this->CookieURL();
  if (cookie_url.IsEmpty())
    return String();

  return Cookies(this, cookie_url);
}

void Document::setCookie(const String& value, ExceptionState& exception_state) {
  if (GetSettings() && !GetSettings()->GetCookieEnabled())
    return;

  // FIXME: The HTML5 DOM spec states that this attribute can raise an
  // InvalidStateError exception on setting if the Document has no
  // browsing context.

  if (!GetSecurityOrigin()->CanAccessCookies()) {
    if (IsSandboxed(kSandboxOrigin))
      exception_state.ThrowSecurityError(
          "The document is sandboxed and lacks the 'allow-same-origin' flag.");
    else if (Url().ProtocolIs("data"))
      exception_state.ThrowSecurityError(
          "Cookies are disabled inside 'data:' URLs.");
    else
      exception_state.ThrowSecurityError("Access is denied for this document.");
    return;
  }

  // Suborigins are cookie-averse and thus setting should be a no-op, unless
  // the 'unsafe-cookies' option is provided.
  if (GetSecurityOrigin()->HasSuborigin() &&
      !GetSecurityOrigin()->GetSuborigin()->PolicyContains(
          Suborigin::SuboriginPolicyOptions::kUnsafeCookies))
    return;

  KURL cookie_url = this->CookieURL();
  if (cookie_url.IsEmpty())
    return;

  SetCookies(this, cookie_url, value);
}

const AtomicString& Document::referrer() const {
  if (Loader())
    return Loader()->GetRequest().HttpReferrer();
  return g_null_atom;
}

String Document::domain() const {
  return GetSecurityOrigin()->Domain();
}

void Document::setDomain(const String& raw_domain,
                         ExceptionState& exception_state) {
  UseCounter::Count(*this, UseCounter::kDocumentSetDomain);

  if (IsSandboxed(kSandboxDocumentDomain)) {
    exception_state.ThrowSecurityError(
        "Assignment is forbidden for sandboxed iframes.");
    return;
  }

  if (SchemeRegistry::IsDomainRelaxationForbiddenForURLScheme(
          GetSecurityOrigin()->Protocol())) {
    exception_state.ThrowSecurityError("Assignment is forbidden for the '" +
                                       GetSecurityOrigin()->Protocol() +
                                       "' scheme.");
    return;
  }

  bool success = false;
  String new_domain = SecurityOrigin::CanonicalizeHost(raw_domain, &success);
  if (!success) {
    exception_state.ThrowSecurityError("'" + raw_domain +
                                       "' could not be parsed properly.");
    return;
  }

  if (new_domain.IsEmpty()) {
    exception_state.ThrowSecurityError("'" + new_domain +
                                       "' is an empty domain.");
    return;
  }

  OriginAccessEntry access_entry(GetSecurityOrigin()->Protocol(), new_domain,
                                 OriginAccessEntry::kAllowSubdomains);
  OriginAccessEntry::MatchResult result =
      access_entry.MatchesOrigin(*GetSecurityOrigin());
  if (result == OriginAccessEntry::kDoesNotMatchOrigin) {
    exception_state.ThrowSecurityError(
        "'" + new_domain + "' is not a suffix of '" + domain() + "'.");
    return;
  }

  if (result == OriginAccessEntry::kMatchesOriginButIsPublicSuffix) {
    exception_state.ThrowSecurityError("'" + new_domain +
                                       "' is a top-level domain.");
    return;
  }

  if (frame_) {
    bool was_cross_domain = frame_->IsCrossOriginSubframe();
    GetSecurityOrigin()->SetDomainFromDOM(new_domain);
    if (View() && (was_cross_domain != frame_->IsCrossOriginSubframe()))
      View()->CrossOriginStatusChanged();

    frame_->GetScriptController().UpdateSecurityOrigin(GetSecurityOrigin());
  }
}

// http://www.whatwg.org/specs/web-apps/current-work/#dom-document-lastmodified
String Document::lastModified() const {
  DateComponents date;
  bool found_date = false;
  if (frame_) {
    if (DocumentLoader* document_loader = Loader()) {
      const AtomicString& http_last_modified =
          document_loader->GetResponse().HttpHeaderField(
              HTTPNames::Last_Modified);
      if (!http_last_modified.IsEmpty()) {
        double date_value = ParseDate(http_last_modified);
        if (!std::isnan(date_value)) {
          date.SetMillisecondsSinceEpochForDateTime(
              ConvertToLocalTime(date_value));
          found_date = true;
        }
      }
    }
  }
  // FIXME: If this document came from the file system, the HTML5
  // specificiation tells us to read the last modification date from the file
  // system.
  if (!found_date)
    date.SetMillisecondsSinceEpochForDateTime(
        ConvertToLocalTime(CurrentTimeMS()));
  return String::Format("%02d/%02d/%04d %02d:%02d:%02d", date.Month() + 1,
                        date.MonthDay(), date.FullYear(), date.Hour(),
                        date.Minute(), date.Second());
}

const KURL Document::FirstPartyForCookies() const {
  // TODO(mkwst): This doesn't properly handle HTML Import documents.

  // If this is an imported document, grab its master document's first-party:
  if (ImportsController() && ImportsController()->Master() &&
      ImportsController()->Master() != this)
    return ImportsController()->Master()->FirstPartyForCookies();

  if (!GetFrame())
    return SecurityOrigin::UrlWithUniqueSecurityOrigin();

  // TODO(mkwst): This doesn't correctly handle sandboxed documents; we want to
  // look at their URL, but we can't because we don't know what it is.
  Frame& top = GetFrame()->Tree().Top();
  KURL top_document_url =
      top.IsLocalFrame()
          ? ToLocalFrame(top).GetDocument()->Url()
          : KURL(KURL(),
                 top.GetSecurityContext()->GetSecurityOrigin()->ToString());
  if (SchemeRegistry::ShouldTreatURLSchemeAsFirstPartyWhenTopLevel(
          top_document_url.Protocol()))
    return top_document_url;

  // We're intentionally using the URL of each document rather than the
  // document's SecurityOrigin.  Sandboxing a document into a unique origin
  // shouldn't effect first-/third-party status for cookies and site data.
  const OriginAccessEntry& access_entry =
      top.IsLocalFrame()
          ? ToLocalFrame(top).GetDocument()->AccessEntryFromURL()
          : OriginAccessEntry(top_document_url.Protocol(),
                              top_document_url.Host(),
                              OriginAccessEntry::kAllowRegisterableDomains);
  const Frame* current_frame = GetFrame();
  while (current_frame) {
    // Skip over srcdoc documents, as they are always same-origin with their
    // closest non-srcdoc parent.
    while (current_frame->IsLocalFrame() &&
           ToLocalFrame(current_frame)->GetDocument()->IsSrcdocDocument())
      current_frame = current_frame->Tree().Parent();
    DCHECK(current_frame);

    // We use 'matchesDomain' here, as it turns out that some folks embed HTTPS
    // login forms
    // into HTTP pages; we should allow this kind of upgrade.
    if (access_entry.MatchesDomain(
            *current_frame->GetSecurityContext()->GetSecurityOrigin()) ==
        OriginAccessEntry::kDoesNotMatchOrigin)
      return SecurityOrigin::UrlWithUniqueSecurityOrigin();

    current_frame = current_frame->Tree().Parent();
  }

  return top_document_url;
}

static bool IsValidNameNonASCII(const LChar* characters, unsigned length) {
  if (!IsValidNameStart(characters[0]))
    return false;

  for (unsigned i = 1; i < length; ++i) {
    if (!IsValidNamePart(characters[i]))
      return false;
  }

  return true;
}

static bool IsValidNameNonASCII(const UChar* characters, unsigned length) {
  for (unsigned i = 0; i < length;) {
    bool first = i == 0;
    UChar32 c;
    U16_NEXT(characters, i, length, c);  // Increments i.
    if (first ? !IsValidNameStart(c) : !IsValidNamePart(c))
      return false;
  }

  return true;
}

template <typename CharType>
static inline bool IsValidNameASCII(const CharType* characters,
                                    unsigned length) {
  CharType c = characters[0];
  if (!(IsASCIIAlpha(c) || c == ':' || c == '_'))
    return false;

  for (unsigned i = 1; i < length; ++i) {
    c = characters[i];
    if (!(IsASCIIAlphanumeric(c) || c == ':' || c == '_' || c == '-' ||
          c == '.'))
      return false;
  }

  return true;
}

bool Document::IsValidName(const String& name) {
  unsigned length = name.length();
  if (!length)
    return false;

  if (name.Is8Bit()) {
    const LChar* characters = name.Characters8();

    if (IsValidNameASCII(characters, length))
      return true;

    return IsValidNameNonASCII(characters, length);
  }

  const UChar* characters = name.Characters16();

  if (IsValidNameASCII(characters, length))
    return true;

  return IsValidNameNonASCII(characters, length);
}

enum QualifiedNameStatus {
  kQNValid,
  kQNMultipleColons,
  kQNInvalidStartChar,
  kQNInvalidChar,
  kQNEmptyPrefix,
  kQNEmptyLocalName
};

struct ParseQualifiedNameResult {
  QualifiedNameStatus status;
  UChar32 character;
  ParseQualifiedNameResult() {}
  explicit ParseQualifiedNameResult(QualifiedNameStatus status)
      : status(status) {}
  ParseQualifiedNameResult(QualifiedNameStatus status, UChar32 character)
      : status(status), character(character) {}
};

template <typename CharType>
static ParseQualifiedNameResult ParseQualifiedNameInternal(
    const AtomicString& qualified_name,
    const CharType* characters,
    unsigned length,
    AtomicString& prefix,
    AtomicString& local_name) {
  bool name_start = true;
  bool saw_colon = false;
  int colon_pos = 0;

  for (unsigned i = 0; i < length;) {
    UChar32 c;
    U16_NEXT(characters, i, length, c)
    if (c == ':') {
      if (saw_colon)
        return ParseQualifiedNameResult(kQNMultipleColons);
      name_start = true;
      saw_colon = true;
      colon_pos = i - 1;
    } else if (name_start) {
      if (!IsValidNameStart(c))
        return ParseQualifiedNameResult(kQNInvalidStartChar, c);
      name_start = false;
    } else {
      if (!IsValidNamePart(c))
        return ParseQualifiedNameResult(kQNInvalidChar, c);
    }
  }

  if (!saw_colon) {
    prefix = g_null_atom;
    local_name = qualified_name;
  } else {
    prefix = AtomicString(characters, colon_pos);
    if (prefix.IsEmpty())
      return ParseQualifiedNameResult(kQNEmptyPrefix);
    int prefix_start = colon_pos + 1;
    local_name = AtomicString(characters + prefix_start, length - prefix_start);
  }

  if (local_name.IsEmpty())
    return ParseQualifiedNameResult(kQNEmptyLocalName);

  return ParseQualifiedNameResult(kQNValid);
}

bool Document::ParseQualifiedName(const AtomicString& qualified_name,
                                  AtomicString& prefix,
                                  AtomicString& local_name,
                                  ExceptionState& exception_state) {
  unsigned length = qualified_name.length();

  if (!length) {
    exception_state.ThrowDOMException(kInvalidCharacterError,
                                      "The qualified name provided is empty.");
    return false;
  }

  ParseQualifiedNameResult return_value;
  if (qualified_name.Is8Bit())
    return_value =
        ParseQualifiedNameInternal(qualified_name, qualified_name.Characters8(),
                                   length, prefix, local_name);
  else
    return_value = ParseQualifiedNameInternal(qualified_name,
                                              qualified_name.Characters16(),
                                              length, prefix, local_name);
  if (return_value.status == kQNValid)
    return true;

  StringBuilder message;
  message.Append("The qualified name provided ('");
  message.Append(qualified_name);
  message.Append("') ");

  if (return_value.status == kQNMultipleColons) {
    message.Append("contains multiple colons.");
  } else if (return_value.status == kQNInvalidStartChar) {
    message.Append("contains the invalid name-start character '");
    message.Append(return_value.character);
    message.Append("'.");
  } else if (return_value.status == kQNInvalidChar) {
    message.Append("contains the invalid character '");
    message.Append(return_value.character);
    message.Append("'.");
  } else if (return_value.status == kQNEmptyPrefix) {
    message.Append("has an empty namespace prefix.");
  } else {
    DCHECK_EQ(return_value.status, kQNEmptyLocalName);
    message.Append("has an empty local name.");
  }

  exception_state.ThrowDOMException(kInvalidCharacterError, message.ToString());
  return false;
}

void Document::SetEncodingData(const DocumentEncodingData& new_data) {
  // It's possible for the encoding of the document to change while we're
  // decoding data. That can only occur while we're processing the <head>
  // portion of the document. There isn't much user-visible content in the
  // <head>, but there is the <title> element. This function detects that
  // situation and re-decodes the document's title so that the user doesn't see
  // an incorrectly decoded title in the title bar.
  if (title_element_ && Encoding() != new_data.Encoding() &&
      !ElementTraversal::FirstWithin(*title_element_) &&
      Encoding() == Latin1Encoding() &&
      title_element_->textContent().ContainsOnlyLatin1()) {
    CString original_bytes = title_element_->textContent().Latin1();
    std::unique_ptr<TextCodec> codec = NewTextCodec(new_data.Encoding());
    String correctly_decoded_title = codec->Decode(
        original_bytes.data(), original_bytes.length(), WTF::kDataEOF);
    title_element_->setTextContent(correctly_decoded_title);
  }

  DCHECK(new_data.Encoding().IsValid());
  encoding_data_ = new_data;

  // FIXME: Should be removed as part of
  // https://code.google.com/p/chromium/issues/detail?id=319643
  bool should_use_visual_ordering =
      encoding_data_.Encoding().UsesVisualOrdering();
  if (should_use_visual_ordering != visually_ordered_) {
    visually_ordered_ = should_use_visual_ordering;
    // FIXME: How is possible to not have a layoutObject here?
    if (!GetLayoutViewItem().IsNull()) {
      GetLayoutViewItem().MutableStyleRef().SetRtlOrdering(
          visually_ordered_ ? EOrder::kVisual : EOrder::kLogical);
    }
    SetNeedsStyleRecalc(kSubtreeStyleChange,
                        StyleChangeReasonForTracing::Create(
                            StyleChangeReason::kVisuallyOrdered));
  }
}

KURL Document::CompleteURL(const String& url) const {
  KURL completed = CompleteURLWithOverride(url, base_url_);

  if (completed.WhitespaceRemoved()) {
    if (completed.ProtocolIsInHTTPFamily()) {
      UseCounter::Count(*this,
                        UseCounter::kDocumentCompleteURLHTTPContainingNewline);
      bool less_than = url.Contains('<');
      if (less_than) {
        UseCounter::Count(
            *this,
            UseCounter::kDocumentCompleteURLHTTPContainingNewlineAndLessThan);
      }
    } else {
      UseCounter::Count(
          *this, UseCounter::kDocumentCompleteURLNonHTTPContainingNewline);
    }
  }
  return completed;
}

KURL Document::CompleteURLWithOverride(const String& url,
                                       const KURL& base_url_override) const {
  DCHECK(base_url_override.IsEmpty() || base_url_override.IsValid());

  // Always return a null URL when passed a null string.
  // FIXME: Should we change the KURL constructor to have this behavior?
  // See also [CSS]StyleSheet::completeURL(const String&)
  if (url.IsNull())
    return KURL();
  // This logic is deliberately spread over many statements in an attempt to
  // track down http://crbug.com/312410.
  const KURL& base_url = BaseURLForOverride(base_url_override);
  if (!Encoding().IsValid())
    return KURL(base_url, url);
  return KURL(base_url, url, Encoding());
}

const KURL& Document::BaseURLForOverride(const KURL& base_url_override) const {
  // This logic is deliberately spread over many statements in an attempt to
  // track down http://crbug.com/312410.
  const KURL* base_url_from_parent = 0;
  bool should_use_parent_base_url = base_url_override.IsEmpty();
  if (!should_use_parent_base_url) {
    const KURL& about_blank_url = BlankURL();
    should_use_parent_base_url = (base_url_override == about_blank_url);
  }
  if (should_use_parent_base_url) {
    if (Document* parent = ParentDocument())
      base_url_from_parent = &parent->BaseURL();
  }
  return base_url_from_parent ? *base_url_from_parent : base_url_override;
}

// static
bool Document::ShouldInheritSecurityOriginFromOwner(const KURL& url) {
  // https://html.spec.whatwg.org/multipage/browsers.html#origin
  //
  // If a Document is the initial "about:blank" document The origin and
  // effective script origin of the Document are those it was assigned when its
  // browsing context was created.
  //
  // Note: We generalize this to all "blank" URLs and invalid URLs because we
  // treat all of these URLs as about:blank.
  return url.IsEmpty() || url.ProtocolIsAbout();
}

KURL Document::OpenSearchDescriptionURL() {
  static const char kOpenSearchMIMEType[] =
      "application/opensearchdescription+xml";
  static const char kOpenSearchRelation[] = "search";

  // FIXME: Why do only top-level frames have openSearchDescriptionURLs?
  if (!GetFrame() || GetFrame()->Tree().Parent())
    return KURL();

  // FIXME: Why do we need to wait for load completion?
  if (!LoadEventFinished())
    return KURL();

  if (!head())
    return KURL();

  for (HTMLLinkElement* link_element =
           Traversal<HTMLLinkElement>::FirstChild(*head());
       link_element;
       link_element = Traversal<HTMLLinkElement>::NextSibling(*link_element)) {
    if (!DeprecatedEqualIgnoringCase(link_element->GetType(),
                                     kOpenSearchMIMEType) ||
        !DeprecatedEqualIgnoringCase(link_element->Rel(), kOpenSearchRelation))
      continue;
    if (link_element->Href().IsEmpty())
      continue;

    // Count usage; perhaps we can lock this to secure contexts.
    UseCounter::Feature osd_disposition;
    RefPtr<SecurityOrigin> target =
        SecurityOrigin::Create(link_element->Href());
    if (IsSecureContext()) {
      osd_disposition = target->IsPotentiallyTrustworthy()
                            ? UseCounter::kOpenSearchSecureOriginSecureTarget
                            : UseCounter::kOpenSearchSecureOriginInsecureTarget;
    } else {
      osd_disposition =
          target->IsPotentiallyTrustworthy()
              ? UseCounter::kOpenSearchInsecureOriginSecureTarget
              : UseCounter::kOpenSearchInsecureOriginInsecureTarget;
    }
    UseCounter::Count(*this, osd_disposition);

    return link_element->Href();
  }

  return KURL();
}

void Document::currentScriptForBinding(
    HTMLScriptElementOrSVGScriptElement& script_element) const {
  if (!current_script_stack_.IsEmpty()) {
    if (ScriptElementBase* script_element_base = current_script_stack_.back())
      script_element_base->SetScriptElementForBinding(script_element);
  }
}

void Document::PushCurrentScript(ScriptElementBase* new_current_script) {
  current_script_stack_.push_back(new_current_script);
}

void Document::PopCurrentScript(ScriptElementBase* script) {
  DCHECK(!current_script_stack_.IsEmpty());
  DCHECK_EQ(current_script_stack_.back(), script);
  current_script_stack_.pop_back();
}

void Document::SetTransformSource(std::unique_ptr<TransformSource> source) {
  transform_source_ = std::move(source);
}

String Document::designMode() const {
  return InDesignMode() ? "on" : "off";
}

void Document::setDesignMode(const String& value) {
  bool new_value = design_mode_;
  if (DeprecatedEqualIgnoringCase(value, "on")) {
    new_value = true;
    UseCounter::Count(*this, UseCounter::kDocumentDesignModeEnabeld);
  } else if (DeprecatedEqualIgnoringCase(value, "off")) {
    new_value = false;
  }
  if (new_value == design_mode_)
    return;
  design_mode_ = new_value;
  SetNeedsStyleRecalc(kSubtreeStyleChange, StyleChangeReasonForTracing::Create(
                                               StyleChangeReason::kDesignMode));
}

Document* Document::ParentDocument() const {
  if (!frame_)
    return 0;
  Frame* parent = frame_->Tree().Parent();
  if (!parent || !parent->IsLocalFrame())
    return 0;
  return ToLocalFrame(parent)->GetDocument();
}

Document& Document::TopDocument() const {
  // FIXME: Not clear what topDocument() should do in the OOPI case--should it
  // return the topmost available Document, or something else?
  Document* doc = const_cast<Document*>(this);
  for (HTMLFrameOwnerElement* element = doc->LocalOwner(); element;
       element = doc->LocalOwner())
    doc = &element->GetDocument();

  DCHECK(doc);
  return *doc;
}

Document* Document::ContextDocument() {
  if (context_document_)
    return context_document_;
  if (frame_)
    return this;
  return nullptr;
}

Attr* Document::createAttribute(const AtomicString& name,
                                ExceptionState& exception_state) {
  return createAttributeNS(g_null_atom, ConvertLocalName(name), exception_state,
                           true);
}

Attr* Document::createAttributeNS(const AtomicString& namespace_uri,
                                  const AtomicString& qualified_name,
                                  ExceptionState& exception_state,
                                  bool should_ignore_namespace_checks) {
  AtomicString prefix, local_name;
  if (!ParseQualifiedName(qualified_name, prefix, local_name, exception_state))
    return nullptr;

  QualifiedName q_name(prefix, local_name, namespace_uri);

  if (!should_ignore_namespace_checks &&
      !HasValidNamespaceForAttributes(q_name)) {
    exception_state.ThrowDOMException(
        kNamespaceError,
        "The namespace URI provided ('" + namespace_uri +
            "') is not valid for the qualified name provided ('" +
            qualified_name + "').");
    return nullptr;
  }

  return Attr::Create(*this, q_name, g_empty_atom);
}

const SVGDocumentExtensions* Document::SvgExtensions() {
  return svg_extensions_.Get();
}

SVGDocumentExtensions& Document::AccessSVGExtensions() {
  if (!svg_extensions_)
    svg_extensions_ = new SVGDocumentExtensions(this);
  return *svg_extensions_;
}

bool Document::HasSVGRootNode() const {
  return isSVGSVGElement(documentElement());
}

HTMLCollection* Document::images() {
  return EnsureCachedCollection<HTMLCollection>(kDocImages);
}

HTMLCollection* Document::applets() {
  return EnsureCachedCollection<HTMLCollection>(kDocApplets);
}

HTMLCollection* Document::embeds() {
  return EnsureCachedCollection<HTMLCollection>(kDocEmbeds);
}

HTMLCollection* Document::scripts() {
  return EnsureCachedCollection<HTMLCollection>(kDocScripts);
}

HTMLCollection* Document::links() {
  return EnsureCachedCollection<HTMLCollection>(kDocLinks);
}

HTMLCollection* Document::forms() {
  return EnsureCachedCollection<HTMLCollection>(kDocForms);
}

HTMLCollection* Document::anchors() {
  return EnsureCachedCollection<HTMLCollection>(kDocAnchors);
}

HTMLAllCollection* Document::all() {
  return EnsureCachedCollection<HTMLAllCollection>(kDocAll);
}

HTMLCollection* Document::WindowNamedItems(const AtomicString& name) {
  return EnsureCachedCollection<WindowNameCollection>(kWindowNamedItems, name);
}

DocumentNameCollection* Document::DocumentNamedItems(const AtomicString& name) {
  return EnsureCachedCollection<DocumentNameCollection>(kDocumentNamedItems,
                                                        name);
}

LocalDOMWindow* Document::defaultView() const {
  // The HTML spec requires to return null if the document is detached from the
  // DOM.  However, |dom_window_| is not cleared on the detachment.  So, we need
  // to check |frame_| to tell whether the document is attached or not.
  return frame_ ? dom_window_ : nullptr;
}

void Document::FinishedParsing() {
  DCHECK(!GetScriptableDocumentParser() || !parser_->IsParsing());
  DCHECK(!GetScriptableDocumentParser() || ready_state_ != kLoading);
  SetParsingState(kInDOMContentLoaded);
  DocumentParserTiming::From(*this).MarkParserStop();

  // FIXME: DOMContentLoaded is dispatched synchronously, but this should be
  // dispatched in a queued task, see https://crbug.com/425790
  if (!document_timing_.DomContentLoadedEventStart())
    document_timing_.MarkDomContentLoadedEventStart();
  DispatchEvent(Event::CreateBubble(EventTypeNames::DOMContentLoaded));
  if (!document_timing_.DomContentLoadedEventEnd())
    document_timing_.MarkDomContentLoadedEventEnd();
  SetParsingState(kFinishedParsing);

  // Ensure Custom Element callbacks are drained before DOMContentLoaded.
  // FIXME: Remove this ad-hoc checkpoint when DOMContentLoaded is dispatched in
  // a queued task, which will do a checkpoint anyway. https://crbug.com/425790
  Microtask::PerformCheckpoint(V8PerIsolateData::MainThreadIsolate());

  ScriptableDocumentParser* parser = GetScriptableDocumentParser();
  well_formed_ = parser && parser->WellFormed();

  if (LocalFrame* frame = this->GetFrame()) {
    // Don't update the layout tree if we haven't requested the main resource
    // yet to avoid adding extra latency. Note that the first layout tree update
    // can be expensive since it triggers the parsing of the default stylesheets
    // which are compiled-in.
    const bool main_resource_was_already_requested =
        frame->Loader().StateMachine()->CommittedFirstRealDocumentLoad();

    // FrameLoader::finishedParsing() might end up calling
    // Document::implicitClose() if all resource loads are
    // complete. HTMLObjectElements can start loading their resources from post
    // attach callbacks triggered by recalcStyle().  This means if we parse out
    // an <object> tag and then reach the end of the document without updating
    // styles, we might not have yet started the resource load and might fire
    // the window load event too early.  To avoid this we force the styles to be
    // up to date before calling FrameLoader::finishedParsing().  See
    // https://bugs.webkit.org/show_bug.cgi?id=36864 starting around comment 35.
    if (main_resource_was_already_requested)
      UpdateStyleAndLayoutTree();

    BeginLifecycleUpdatesIfRenderingReady();

    frame->Loader().FinishedParsing();

    TRACE_EVENT_INSTANT1("devtools.timeline", "MarkDOMContent",
                         TRACE_EVENT_SCOPE_THREAD, "data",
                         InspectorMarkLoadEvent::Data(frame));
    probe::domContentLoadedEventFired(frame);
  }

  // Schedule dropping of the ElementDataCache. We keep it alive for a while
  // after parsing finishes so that dynamically inserted content can also
  // benefit from sharing optimizations.  Note that we don't refresh the timer
  // on cache access since that could lead to huge caches being kept alive
  // indefinitely by something innocuous like JS setting .innerHTML repeatedly
  // on a timer.
  element_data_cache_clear_timer_.StartOneShot(10, BLINK_FROM_HERE);

  // Parser should have picked up all preloads by now
  fetcher_->ClearPreloads(ResourceFetcher::kClearSpeculativeMarkupPreloads);

  if (IsPrefetchOnly())
    WebPrerenderingSupport::Current()->PrefetchFinished();
}

void Document::ElementDataCacheClearTimerFired(TimerBase*) {
  element_data_cache_.Clear();
}

void Document::BeginLifecycleUpdatesIfRenderingReady() {
  if (!IsActive())
    return;
  if (!IsRenderingReady())
    return;
  View()->BeginLifecycleUpdates();
}

Vector<IconURL> Document::IconURLs(int icon_types_mask) {
  IconURL first_favicon;
  IconURL first_touch_icon;
  IconURL first_touch_precomposed_icon;
  Vector<IconURL> secondary_icons;

  using TraversalFunction = HTMLLinkElement* (*)(const Node&);
  TraversalFunction find_next_candidate =
      &Traversal<HTMLLinkElement>::NextSibling;

  HTMLLinkElement* first_element = nullptr;
  if (head()) {
    first_element = Traversal<HTMLLinkElement>::FirstChild(*head());
  } else if (IsSVGDocument() && isSVGSVGElement(documentElement())) {
    first_element = Traversal<HTMLLinkElement>::FirstWithin(*documentElement());
    find_next_candidate = &Traversal<HTMLLinkElement>::Next;
  }

  // Start from the first child node so that icons seen later take precedence as
  // required by the spec.
  for (HTMLLinkElement* link_element = first_element; link_element;
       link_element = find_next_candidate(*link_element)) {
    if (!(link_element->GetIconType() & icon_types_mask))
      continue;
    if (link_element->Href().IsEmpty())
      continue;

    IconURL new_url(link_element->Href(), link_element->IconSizes(),
                    link_element->GetType(), link_element->GetIconType());
    if (link_element->GetIconType() == kFavicon) {
      if (first_favicon.icon_type_ != kInvalidIcon)
        secondary_icons.push_back(first_favicon);
      first_favicon = new_url;
    } else if (link_element->GetIconType() == kTouchIcon) {
      if (first_touch_icon.icon_type_ != kInvalidIcon)
        secondary_icons.push_back(first_touch_icon);
      first_touch_icon = new_url;
    } else if (link_element->GetIconType() == kTouchPrecomposedIcon) {
      if (first_touch_precomposed_icon.icon_type_ != kInvalidIcon)
        secondary_icons.push_back(first_touch_precomposed_icon);
      first_touch_precomposed_icon = new_url;
    } else {
      NOTREACHED();
    }
  }

  Vector<IconURL> icon_urls;
  if (first_favicon.icon_type_ != kInvalidIcon)
    icon_urls.push_back(first_favicon);
  else if (url_.ProtocolIsInHTTPFamily() && icon_types_mask & kFavicon)
    icon_urls.push_back(IconURL::DefaultFavicon(url_));

  if (first_touch_icon.icon_type_ != kInvalidIcon)
    icon_urls.push_back(first_touch_icon);
  if (first_touch_precomposed_icon.icon_type_ != kInvalidIcon)
    icon_urls.push_back(first_touch_precomposed_icon);
  for (int i = secondary_icons.size() - 1; i >= 0; --i)
    icon_urls.push_back(secondary_icons[i]);
  return icon_urls;
}

Color Document::ThemeColor() const {
  auto root_element = documentElement();
  if (!root_element)
    return Color();
  for (HTMLMetaElement& meta_element :
       Traversal<HTMLMetaElement>::DescendantsOf(*root_element)) {
    Color color = Color::kTransparent;
    if (DeprecatedEqualIgnoringCase(meta_element.GetName(), "theme-color") &&
        CSSParser::ParseColor(
            color, meta_element.Content().GetString().StripWhiteSpace(), true))
      return color;
  }
  return Color();
}

HTMLLinkElement* Document::LinkManifest() const {
  HTMLHeadElement* head = this->head();
  if (!head)
    return 0;

  // The first link element with a manifest rel must be used. Others are
  // ignored.
  for (HTMLLinkElement* link_element =
           Traversal<HTMLLinkElement>::FirstChild(*head);
       link_element;
       link_element = Traversal<HTMLLinkElement>::NextSibling(*link_element)) {
    if (!link_element->RelAttribute().IsManifest())
      continue;
    return link_element;
  }

  return 0;
}

void Document::InitSecurityContext(const DocumentInit& initializer) {
  DCHECK(!GetSecurityOrigin());

  if (!initializer.HasSecurityContext()) {
    // No source for a security context.
    // This can occur via document.implementation.createDocument().
    cookie_url_ = KURL(kParsedURLString, g_empty_string);
    SetSecurityOrigin(SecurityOrigin::CreateUnique());
    InitContentSecurityPolicy();
    // Unique security origins cannot have a suborigin
    return;
  }

  // In the common case, create the security context from the currently
  // loading URL with a fresh content security policy.
  EnforceSandboxFlags(initializer.GetSandboxFlags());
  SetInsecureRequestPolicy(initializer.GetInsecureRequestPolicy());
  if (initializer.InsecureNavigationsToUpgrade()) {
    for (auto to_upgrade : *initializer.InsecureNavigationsToUpgrade())
      AddInsecureNavigationUpgrade(to_upgrade);
  }

  if (IsSandboxed(kSandboxOrigin)) {
    cookie_url_ = url_;
    SetSecurityOrigin(SecurityOrigin::CreateUnique());
    // If we're supposed to inherit our security origin from our
    // owner, but we're also sandboxed, the only things we inherit are
    // the origin's potential trustworthiness and the ability to
    // load local resources. The latter lets about:blank iframes in
    // file:// URL documents load images and other resources from
    // the file system.
    if (initializer.Owner() &&
        initializer.Owner()->GetSecurityOrigin()->IsPotentiallyTrustworthy())
      GetSecurityOrigin()->SetUniqueOriginIsPotentiallyTrustworthy(true);
    if (initializer.Owner() &&
        initializer.Owner()->GetSecurityOrigin()->CanLoadLocalResources())
      GetSecurityOrigin()->GrantLoadLocalResources();
  } else if (initializer.Owner()) {
    cookie_url_ = initializer.Owner()->CookieURL();
    // We alias the SecurityOrigins to match Firefox, see Bug 15313
    // https://bugs.webkit.org/show_bug.cgi?id=15313
    SetSecurityOrigin(initializer.Owner()->GetSecurityOrigin());
  } else {
    cookie_url_ = url_;
    SetSecurityOrigin(SecurityOrigin::Create(url_));
  }

  // Set the address space before setting up CSP, as the latter may override
  // the former via the 'treat-as-public-address' directive (see
  // https://mikewest.github.io/cors-rfc1918/#csp).
  if (initializer.IsHostedInReservedIPRange()) {
    SetAddressSpace(GetSecurityOrigin()->IsLocalhost()
                        ? kWebAddressSpaceLocal
                        : kWebAddressSpacePrivate);
  } else if (GetSecurityOrigin()->IsLocal()) {
    // "Local" security origins (like 'file://...') are treated as having
    // a local address space.
    //
    // TODO(mkwst): It's not entirely clear that this is a good idea.
    SetAddressSpace(kWebAddressSpaceLocal);
  } else {
    SetAddressSpace(kWebAddressSpacePublic);
  }

  if (ImportsController()) {
    // If this document is an HTML import, grab a reference to it's master
    // document's Content Security Policy. We don't call
    // 'initContentSecurityPolicy' in this case, as we can't rebind the master
    // document's policy object: its ExecutionContext needs to remain tied to
    // the master document.
    SetContentSecurityPolicy(
        ImportsController()->Master()->GetContentSecurityPolicy());
  } else {
    InitContentSecurityPolicy();
  }

  if (GetSecurityOrigin()->HasSuborigin())
    EnforceSuborigin(*GetSecurityOrigin()->GetSuborigin());

  if (Settings* settings = initializer.GetSettings()) {
    if (!settings->GetWebSecurityEnabled()) {
      // Web security is turned off. We should let this document access every
      // other document. This is used primary by testing harnesses for web
      // sites.
      GetSecurityOrigin()->GrantUniversalAccess();
    } else if (GetSecurityOrigin()->IsLocal()) {
      if (settings->GetAllowUniversalAccessFromFileURLs()) {
        // Some clients want local URLs to have universal access, but that
        // setting is dangerous for other clients.
        GetSecurityOrigin()->GrantUniversalAccess();
      } else if (!settings->GetAllowFileAccessFromFileURLs()) {
        // Some clients do not want local URLs to have access to other local
        // URLs.
        GetSecurityOrigin()->BlockLocalAccessFromLocalOrigin();
      }
    }
  }

  if (initializer.ShouldTreatURLAsSrcdocDocument()) {
    is_srcdoc_document_ = true;
    SetBaseURLOverride(initializer.ParentBaseURL());
  }

  if (GetSecurityOrigin()->IsUnique() &&
      SecurityOrigin::Create(url_)->IsPotentiallyTrustworthy())
    GetSecurityOrigin()->SetUniqueOriginIsPotentiallyTrustworthy(true);

  if (GetSecurityOrigin()->HasSuborigin())
    EnforceSuborigin(*GetSecurityOrigin()->GetSuborigin());
}

void Document::InitContentSecurityPolicy(ContentSecurityPolicy* csp) {
  SetContentSecurityPolicy(csp ? csp : ContentSecurityPolicy::Create());

  // We inherit the parent/opener's CSP for documents with "local" schemes:
  // 'about', 'blob', 'data', and 'filesystem'. We also inherit CSP for
  // documents with empty/invalid URLs because we treat those URLs as
  // 'about:blank' in Blink.
  //
  // https://w3c.github.io/webappsec-csp/#initialize-document-csp
  //
  // TODO(dcheng): This is similar enough to work we're doing in
  // 'DocumentLoader::ensureWriter' that it might make sense to combine them.
  if (frame_) {
    Frame* inherit_from = frame_->Tree().Parent() ? frame_->Tree().Parent()
                                                  : frame_->Client()->Opener();
    if (inherit_from && frame_ != inherit_from) {
      DCHECK(inherit_from->GetSecurityContext() &&
             inherit_from->GetSecurityContext()->GetContentSecurityPolicy());
      ContentSecurityPolicy* policy_to_inherit =
          inherit_from->GetSecurityContext()->GetContentSecurityPolicy();
      if (url_.IsEmpty() || url_.ProtocolIsAbout() || url_.ProtocolIsData() ||
          url_.ProtocolIs("blob") || url_.ProtocolIs("filesystem")) {
        GetContentSecurityPolicy()->CopyStateFrom(policy_to_inherit);
      }
      // Plugin documents inherit their parent/opener's 'plugin-types' directive
      // regardless of URL.
      if (IsPluginDocument())
        GetContentSecurityPolicy()->CopyPluginTypesFrom(policy_to_inherit);
    }
  }
  GetContentSecurityPolicy()->BindToExecutionContext(this);
}

bool Document::IsSecureTransitionTo(const KURL& url) const {
  RefPtr<SecurityOrigin> other = SecurityOrigin::Create(url);
  return GetSecurityOrigin()->CanAccess(other.Get());
}

bool Document::CanExecuteScripts(ReasonForCallingCanExecuteScripts reason) {
  if (IsSandboxed(kSandboxScripts)) {
    // FIXME: This message should be moved off the console once a solution to
    // https://bugs.webkit.org/show_bug.cgi?id=103274 exists.
    if (reason == kAboutToExecuteScript) {
      AddConsoleMessage(ConsoleMessage::Create(
          kSecurityMessageSource, kErrorMessageLevel,
          "Blocked script execution in '" + Url().ElidedString() +
              "' because the document's frame is sandboxed and the "
              "'allow-scripts' permission is not set."));
    }
    return false;
  }

  DCHECK(GetFrame())
      << "you are querying canExecuteScripts on a non contextDocument.";

  ContentSettingsClient* settings_client =
      GetFrame()->GetContentSettingsClient();
  if (!settings_client)
    return false;

  Settings* settings = GetFrame()->GetSettings();
  if (!settings_client->AllowScript(settings && settings->GetScriptEnabled())) {
    if (reason == kAboutToExecuteScript)
      settings_client->DidNotAllowScript();

    return false;
  }

  return true;
}

bool Document::IsRenderingReady() const {
  return style_engine_->IgnoringPendingStylesheets() ||
         (HaveImportsLoaded() && HaveRenderBlockingStylesheetsLoaded());
}

bool Document::AllowInlineEventHandler(Node* node,
                                       EventListener* listener,
                                       const String& context_url,
                                       const WTF::OrdinalNumber& context_line) {
  Element* element = node && node->IsElementNode() ? ToElement(node) : nullptr;
  if (!ContentSecurityPolicy::ShouldBypassMainWorld(this) &&
      !GetContentSecurityPolicy()->AllowInlineEventHandler(
          element, listener->Code(), context_url, context_line))
    return false;

  // HTML says that inline script needs browsing context to create its execution
  // environment.
  // http://www.whatwg.org/specs/web-apps/current-work/multipage/webappapis.html#event-handler-attributes
  // Also, if the listening node came from other document, which happens on
  // context-less event dispatching, we also need to ask the owner document of
  // the node.
  LocalFrame* frame = ExecutingFrame();
  if (!frame)
    return false;
  if (!ContextDocument()->CanExecuteScripts(kNotAboutToExecuteScript))
    return false;
  if (node && node->GetDocument() != this &&
      !node->GetDocument().AllowInlineEventHandler(node, listener, context_url,
                                                   context_line))
    return false;

  return true;
}

void Document::EnforceSandboxFlags(SandboxFlags mask) {
  RefPtr<SecurityOrigin> stand_in_origin = GetSecurityOrigin();
  ApplySandboxFlags(mask);
  // Send a notification if the origin has been updated.
  if (stand_in_origin && !stand_in_origin->IsUnique() &&
      GetSecurityOrigin()->IsUnique()) {
    GetSecurityOrigin()->SetUniqueOriginIsPotentiallyTrustworthy(
        stand_in_origin->IsPotentiallyTrustworthy());
    if (GetFrame())
      GetFrame()->Loader().Client()->DidUpdateToUniqueOrigin();
  }
}

void Document::UpdateSecurityOrigin(PassRefPtr<SecurityOrigin> origin) {
  SetSecurityOrigin(std::move(origin));
  DidUpdateSecurityOrigin();
}

String Document::origin() const {
  return GetSecurityOrigin()->ToString();
}

String Document::suborigin() const {
  return GetSecurityOrigin()->HasSuborigin()
             ? GetSecurityOrigin()->GetSuborigin()->GetName()
             : String();
}

void Document::DidUpdateSecurityOrigin() {
  if (!frame_)
    return;
  frame_->GetScriptController().UpdateSecurityOrigin(GetSecurityOrigin());
}

bool Document::IsContextThread() const {
  return IsMainThread();
}

void Document::UpdateFocusAppearanceLater() {
  if (!update_focus_appearance_timer_.IsActive())
    update_focus_appearance_timer_.StartOneShot(0, BLINK_FROM_HERE);
}

void Document::CancelFocusAppearanceUpdate() {
  update_focus_appearance_timer_.Stop();
}

void Document::UpdateFocusAppearanceTimerFired(TimerBase*) {
  Element* element = FocusedElement();
  if (!element)
    return;
  UpdateStyleAndLayout();
  if (element->IsFocusable())
    element->UpdateFocusAppearance(SelectionBehaviorOnFocus::kRestore);
}

void Document::AttachRange(Range* range) {
  DCHECK(!ranges_.Contains(range));
  ranges_.insert(range);
}

void Document::DetachRange(Range* range) {
  // We don't ASSERT m_ranges.contains(range) to allow us to call this
  // unconditionally to fix: https://bugs.webkit.org/show_bug.cgi?id=26044
  ranges_.erase(range);
}

void Document::InitDNSPrefetch() {
  Settings* settings = this->GetSettings();

  have_explicitly_disabled_dns_prefetch_ = false;
  is_dns_prefetch_enabled_ = settings && settings->GetDNSPrefetchingEnabled() &&
                             GetSecurityOrigin()->Protocol() == "http";

  // Inherit DNS prefetch opt-out from parent frame
  if (Document* parent = ParentDocument()) {
    if (!parent->IsDNSPrefetchEnabled())
      is_dns_prefetch_enabled_ = false;
  }
}

void Document::ParseDNSPrefetchControlHeader(
    const String& dns_prefetch_control) {
  if (DeprecatedEqualIgnoringCase(dns_prefetch_control, "on") &&
      !have_explicitly_disabled_dns_prefetch_) {
    is_dns_prefetch_enabled_ = true;
    return;
  }

  is_dns_prefetch_enabled_ = false;
  have_explicitly_disabled_dns_prefetch_ = true;
}

IntersectionObserverController* Document::GetIntersectionObserverController() {
  return intersection_observer_controller_;
}

IntersectionObserverController&
Document::EnsureIntersectionObserverController() {
  if (!intersection_observer_controller_)
    intersection_observer_controller_ =
        IntersectionObserverController::Create(this);
  return *intersection_observer_controller_;
}

ResizeObserverController& Document::EnsureResizeObserverController() {
  if (!resize_observer_controller_)
    resize_observer_controller_ = new ResizeObserverController();
  return *resize_observer_controller_;
}

static void RunAddConsoleMessageTask(MessageSource source,
                                     MessageLevel level,
                                     const String& message,
                                     ExecutionContext* context) {
  context->AddConsoleMessage(ConsoleMessage::Create(source, level, message));
}

void Document::AddConsoleMessage(ConsoleMessage* console_message) {
  if (!IsContextThread()) {
    TaskRunnerHelper::Get(TaskType::kUnthrottled, this)
        ->PostTask(BLINK_FROM_HERE,
                   CrossThreadBind(
                       &RunAddConsoleMessageTask, console_message->Source(),
                       console_message->Level(), console_message->Message(),
                       WrapCrossThreadPersistent(this)));
    return;
  }

  if (!frame_)
    return;

  if (console_message->Location()->IsUnknown()) {
    // TODO(dgozman): capture correct location at call places instead.
    unsigned line_number = 0;
    if (!IsInDocumentWrite() && GetScriptableDocumentParser()) {
      ScriptableDocumentParser* parser = GetScriptableDocumentParser();
      if (parser->IsParsingAtLineNumber())
        line_number = parser->LineNumber().OneBasedInt();
    }
    console_message = ConsoleMessage::Create(
        console_message->Source(), console_message->Level(),
        console_message->Message(),
        SourceLocation::Create(Url().GetString(), line_number, 0, nullptr));
  }
  frame_->Console().AddMessage(console_message);
}

void Document::PostTask(TaskType task_type,
                        const WebTraceLocation& location,
                        std::unique_ptr<ExecutionContextTask> task,
                        const String& task_name_for_instrumentation) {
  if (!task_name_for_instrumentation.IsEmpty()) {
    probe::AsyncTaskScheduled(this, task_name_for_instrumentation, task.get());
  }

  TaskRunnerHelper::Get(task_type, this)
      ->PostTask(location,
                 CrossThreadBind(&Document::RunExecutionContextTask,
                                 WrapCrossThreadWeakPersistent(this),
                                 WTF::Passed(std::move(task)),
                                 !task_name_for_instrumentation.IsEmpty()));
}

void Document::TasksWereSuspended() {
  GetScriptRunner()->Suspend();

  if (parser_)
    parser_->SuspendScheduledTasks();
  if (scripted_animation_controller_)
    scripted_animation_controller_->Suspend();
}

void Document::TasksWereResumed() {
  GetScriptRunner()->Resume();

  if (parser_)
    parser_->ResumeScheduledTasks();
  if (scripted_animation_controller_)
    scripted_animation_controller_->Resume();

  MutationObserver::ResumeSuspendedObservers();
  if (dom_window_)
    DOMWindowPerformance::performance(*dom_window_)->ResumeSuspendedObservers();
}

bool Document::TasksNeedSuspension() {
  Page* page = this->GetPage();
  return page && page->Suspended();
}

void Document::AddToTopLayer(Element* element, const Element* before) {
  if (element->IsInTopLayer())
    return;

  DCHECK(!top_layer_elements_.Contains(element));
  DCHECK(!before || top_layer_elements_.Contains(before));
  if (before) {
    size_t before_position = top_layer_elements_.Find(before);
    top_layer_elements_.insert(before_position, element);
  } else {
    top_layer_elements_.push_back(element);
  }
  element->SetIsInTopLayer(true);
}

void Document::RemoveFromTopLayer(Element* element) {
  if (!element->IsInTopLayer())
    return;
  size_t position = top_layer_elements_.Find(element);
  DCHECK_NE(position, kNotFound);
  top_layer_elements_.erase(position);
  element->SetIsInTopLayer(false);
}

HTMLDialogElement* Document::ActiveModalDialog() const {
  if (top_layer_elements_.IsEmpty())
    return 0;
  return toHTMLDialogElement(top_layer_elements_.back().Get());
}

void Document::exitPointerLock() {
  if (!GetPage())
    return;
  if (Element* target = GetPage()->GetPointerLockController().GetElement()) {
    if (target->GetDocument() != this)
      return;
    GetPage()->GetPointerLockController().RequestPointerUnlock();
  }
}

Element* Document::PointerLockElement() const {
  if (!GetPage() || GetPage()->GetPointerLockController().LockPending())
    return 0;
  if (Element* element = GetPage()->GetPointerLockController().GetElement()) {
    if (element->GetDocument() == this)
      return element;
  }
  return 0;
}

void Document::SuppressLoadEvent() {
  if (!LoadEventFinished())
    load_event_progress_ = kLoadEventCompleted;
}

void Document::DecrementLoadEventDelayCount() {
  DCHECK(load_event_delay_count_);
  --load_event_delay_count_;

  if (!load_event_delay_count_)
    CheckLoadEventSoon();
}

void Document::DecrementLoadEventDelayCountAndCheckLoadEvent() {
  DCHECK(load_event_delay_count_);
  --load_event_delay_count_;

  if (!load_event_delay_count_)
    CheckCompleted();
}

void Document::CheckLoadEventSoon() {
  if (GetFrame() && !load_event_delay_timer_.IsActive())
    load_event_delay_timer_.StartOneShot(0, BLINK_FROM_HERE);
}

bool Document::IsDelayingLoadEvent() {
  // Always delay load events until after garbage collection.
  // This way we don't have to explicitly delay load events via
  // incrementLoadEventDelayCount and decrementLoadEventDelayCount in
  // Node destructors.
  if (ThreadState::Current()->SweepForbidden()) {
    if (!load_event_delay_count_)
      CheckLoadEventSoon();
    return true;
  }
  return load_event_delay_count_;
}

void Document::LoadEventDelayTimerFired(TimerBase*) {
  CheckCompleted();
}

void Document::LoadPluginsSoon() {
  // FIXME: Remove this timer once we don't need to compute layout to load
  // plugins.
  if (!plugin_loading_timer_.IsActive())
    plugin_loading_timer_.StartOneShot(0, BLINK_FROM_HERE);
}

void Document::PluginLoadingTimerFired(TimerBase*) {
  UpdateStyleAndLayout();
}

ScriptedAnimationController& Document::EnsureScriptedAnimationController() {
  if (!scripted_animation_controller_) {
    scripted_animation_controller_ = ScriptedAnimationController::Create(this);
    // We need to make sure that we don't start up the animation controller on a
    // background tab, for example.
    if (!GetPage())
      scripted_animation_controller_->Suspend();
  }
  return *scripted_animation_controller_;
}

int Document::RequestAnimationFrame(FrameRequestCallback* callback) {
  return EnsureScriptedAnimationController().RegisterCallback(callback);
}

void Document::CancelAnimationFrame(int id) {
  if (!scripted_animation_controller_)
    return;
  scripted_animation_controller_->CancelCallback(id);
}

void Document::ServiceScriptedAnimations(
    double monotonic_animation_start_time) {
  if (!scripted_animation_controller_)
    return;
  scripted_animation_controller_->ServiceScriptedAnimations(
      monotonic_animation_start_time);
}

ScriptedIdleTaskController& Document::EnsureScriptedIdleTaskController() {
  if (!scripted_idle_task_controller_)
    scripted_idle_task_controller_ = ScriptedIdleTaskController::Create(this);
  return *scripted_idle_task_controller_;
}

int Document::RequestIdleCallback(IdleRequestCallback* callback,
                                  const IdleRequestOptions& options) {
  return EnsureScriptedIdleTaskController().RegisterCallback(callback, options);
}

void Document::CancelIdleCallback(int id) {
  if (!scripted_idle_task_controller_)
    return;
  scripted_idle_task_controller_->CancelCallback(id);
}

Touch* Document::createTouch(DOMWindow* window,
                             EventTarget* target,
                             int identifier,
                             double page_x,
                             double page_y,
                             double screen_x,
                             double screen_y,
                             double radius_x,
                             double radius_y,
                             float rotation_angle,
                             float force) const {
  // Match behavior from when these types were integers, and avoid surprises
  // from someone explicitly
  // passing Infinity/NaN.
  if (!std::isfinite(page_x))
    page_x = 0;
  if (!std::isfinite(page_y))
    page_y = 0;
  if (!std::isfinite(screen_x))
    screen_x = 0;
  if (!std::isfinite(screen_y))
    screen_y = 0;
  if (!std::isfinite(radius_x))
    radius_x = 0;
  if (!std::isfinite(radius_y))
    radius_y = 0;
  if (!std::isfinite(rotation_angle))
    rotation_angle = 0;
  if (!std::isfinite(force))
    force = 0;

  if (radius_x || radius_y || rotation_angle || force)
    UseCounter::Count(*this,
                      UseCounter::kDocumentCreateTouchMoreThanSevenArguments);

  // FIXME: It's not clear from the documentation at
  // http://developer.apple.com/library/safari/#documentation/UserExperience/Reference/DocumentAdditionsReference/DocumentAdditions/DocumentAdditions.html
  // when this method should throw and nor is it by inspection of iOS behavior.
  // It would be nice to verify any cases where it throws under iOS and
  // implement them here. See https://bugs.webkit.org/show_bug.cgi?id=47819
  LocalFrame* frame = window && window->IsLocalDOMWindow()
                          ? blink::ToLocalDOMWindow(window)->GetFrame()
                          : this->GetFrame();
  return Touch::Create(
      frame, target, identifier, FloatPoint(screen_x, screen_y),
      FloatPoint(page_x, page_y), FloatSize(radius_x, radius_y), rotation_angle,
      force, String());
}

TouchList* Document::createTouchList(HeapVector<Member<Touch>>& touches) const {
  return TouchList::Adopt(touches);
}

DocumentLoader* Document::Loader() const {
  if (!frame_)
    return 0;

  DocumentLoader* loader = frame_->Loader().GetDocumentLoader();
  if (!loader)
    return 0;

  if (frame_->GetDocument() != this)
    return 0;

  return loader;
}

Node* EventTargetNodeForDocument(Document* doc) {
  if (!doc)
    return 0;
  Node* node = doc->FocusedElement();
  if (!node && doc->IsPluginDocument()) {
    PluginDocument* plugin_document = ToPluginDocument(doc);
    node = plugin_document->PluginNode();
  }
  if (!node && doc->IsHTMLDocument())
    node = doc->body();
  if (!node)
    node = doc->documentElement();
  return node;
}

void Document::AdjustFloatQuadsForScrollAndAbsoluteZoom(
    Vector<FloatQuad>& quads,
    LayoutObject& layout_object) {
  if (!View())
    return;

  LayoutRect visible_content_rect(View()->VisibleContentRect());
  for (size_t i = 0; i < quads.size(); ++i) {
    quads[i].Move(-FloatSize(visible_content_rect.X().ToFloat(),
                             visible_content_rect.Y().ToFloat()));
    AdjustFloatQuadForAbsoluteZoom(quads[i], layout_object);
  }
}

void Document::AdjustFloatRectForScrollAndAbsoluteZoom(
    FloatRect& rect,
    LayoutObject& layout_object) {
  if (!View())
    return;

  LayoutRect visible_content_rect(View()->VisibleContentRect());
  rect.Move(-FloatSize(visible_content_rect.X().ToFloat(),
                       visible_content_rect.Y().ToFloat()));
  AdjustFloatRectForAbsoluteZoom(rect, layout_object);
}

void Document::SetThreadedParsingEnabledForTesting(bool enabled) {
  g_threaded_parsing_enabled_for_testing = enabled;
}

bool Document::ThreadedParsingEnabledForTesting() {
  return g_threaded_parsing_enabled_for_testing;
}

SnapCoordinator* Document::GetSnapCoordinator() {
  if (RuntimeEnabledFeatures::cssScrollSnapPointsEnabled() &&
      !snap_coordinator_)
    snap_coordinator_ = SnapCoordinator::Create();

  return snap_coordinator_.Get();
}

void Document::SetContextFeatures(ContextFeatures& features) {
  context_features_ = &features;
}

// TODO(mustaq) |request| parameter maybe a misuse of HitTestRequest in
// updateHoverActiveState() since the function doesn't bother with hit-testing.
void Document::UpdateHoverActiveState(const HitTestRequest& request,
                                      Element* inner_element) {
  DCHECK(!request.ReadOnly());

  if (request.Active() && frame_)
    frame_->GetEventHandler().NotifyElementActivated();

  Element* inner_element_in_document = inner_element;

  while (inner_element_in_document &&
         inner_element_in_document->GetDocument() != this) {
    inner_element_in_document->GetDocument().UpdateHoverActiveState(
        request, inner_element_in_document);
    inner_element_in_document =
        inner_element_in_document->GetDocument().LocalOwner();
  }

  UpdateDistribution();
  Element* old_active_element = ActiveHoverElement();
  if (old_active_element && !request.Active()) {
    // The oldActiveElement layoutObject is null, dropped on :active by setting
    // display: none, for instance. We still need to clear the ActiveChain as
    // the mouse is released.
    for (Element* element = old_active_element; element;
         element = FlatTreeTraversal::ParentElement(*element)) {
      element->SetActive(false);
      user_action_elements_.SetInActiveChain(element, false);
    }
    SetActiveHoverElement(nullptr);
  } else {
    Element* new_active_element = inner_element_in_document;
    if (!old_active_element && new_active_element &&
        !new_active_element->IsDisabledFormControl() && request.Active() &&
        !request.TouchMove()) {
      // We are setting the :active chain and freezing it. If future moves
      // happen, they will need to reference this chain.
      for (Element* element = new_active_element; element;
           element = FlatTreeTraversal::ParentElement(*element)) {
        user_action_elements_.SetInActiveChain(element, true);
      }
      SetActiveHoverElement(new_active_element);
    }
  }
  // If the mouse has just been pressed, set :active on the chain. Those (and
  // only those) nodes should remain :active until the mouse is released.
  bool allow_active_changes = !old_active_element && ActiveHoverElement();

  // If the mouse is down and if this is a mouse move event, we want to restrict
  // changes in :hover/:active to only apply to elements that are in the :active
  // chain that we froze at the time the mouse went down.
  bool must_be_in_active_chain = request.Active() && request.Move();

  Element* old_hover_element = HoverElement();

  // The passed in innerElement may not be a result of a hit test for the
  // current up-to-date flat/layout tree. That means the element may be
  // display:none at this point. Skip up the ancestor chain until we reach an
  // element with a layoutObject or a display:contents element.
  Element* new_hover_element =
      SkipDisplayNoneAncestors(inner_element_in_document);

  // Update our current hover element.
  SetHoverElement(new_hover_element);

  Node* ancestor_element = nullptr;
  if (old_hover_element && old_hover_element->isConnected() &&
      new_hover_element) {
    Node* ancestor = FlatTreeTraversal::CommonAncestor(*old_hover_element,
                                                       *new_hover_element);
    if (ancestor && ancestor->IsElementNode())
      ancestor_element = ToElement(ancestor);
  }

  HeapVector<Member<Element>, 32> elements_to_remove_from_chain;
  HeapVector<Member<Element>, 32> elements_to_add_to_chain;

  if (old_hover_element != new_hover_element) {
    // The old hover path only needs to be cleared up to (and not including) the
    // common ancestor;
    //
    // FIXME(ecobos@igalia.com): oldHoverElement may be disconnected from the
    // tree already. This is due to our handling of m_hoverElement in
    // hoveredElementDetached (which assumes all the parents are hovered) and
    // mustBeInActiveChain (which makes this not hold).
    //
    // In that case, none of the nodes in the chain have the flags, so there's
    // no problem in skipping this step.
    if (old_hover_element && old_hover_element->isConnected()) {
      for (Element* curr = old_hover_element; curr && curr != ancestor_element;
           curr = FlatTreeTraversal::ParentElement(*curr)) {
        if (!must_be_in_active_chain || curr->InActiveChain())
          elements_to_remove_from_chain.push_back(curr);
      }
    }
  }

  // Now set the hover state for our new object up to the root.
  for (Element* curr = new_hover_element; curr;
       curr = FlatTreeTraversal::ParentElement(*curr)) {
    if (!must_be_in_active_chain || curr->InActiveChain())
      elements_to_add_to_chain.push_back(curr);
  }

  for (Element* element : elements_to_remove_from_chain)
    element->SetHovered(false);

  bool saw_common_ancestor = false;
  for (Element* element : elements_to_add_to_chain) {
    // Elements past the common ancestor do not change hover state, but might
    // change active state.
    if (element == ancestor_element)
      saw_common_ancestor = true;
    if (allow_active_changes)
      element->SetActive(true);
    if (!saw_common_ancestor || element == hover_element_)
      element->SetHovered(true);
  }
}

bool Document::HaveScriptBlockingStylesheetsLoaded() const {
  return style_engine_->HaveScriptBlockingStylesheetsLoaded();
}

bool Document::HaveRenderBlockingStylesheetsLoaded() const {
  if (RuntimeEnabledFeatures::cssInBodyDoesNotBlockPaintEnabled())
    return style_engine_->HaveRenderBlockingStylesheetsLoaded();
  return style_engine_->HaveScriptBlockingStylesheetsLoaded();
}

Locale& Document::GetCachedLocale(const AtomicString& locale) {
  AtomicString locale_key = locale;
  if (locale.IsEmpty() ||
      !RuntimeEnabledFeatures::langAttributeAwareFormControlUIEnabled())
    return Locale::DefaultLocale();
  LocaleIdentifierToLocaleMap::AddResult result =
      locale_cache_.insert(locale_key, nullptr);
  if (result.is_new_entry)
    result.stored_value->value = Locale::Create(locale_key);
  return *(result.stored_value->value);
}

AnimationClock& Document::GetAnimationClock() {
  DCHECK(GetPage());
  return GetPage()->Animator().Clock();
}

Document& Document::EnsureTemplateDocument() {
  if (IsTemplateDocument())
    return *this;

  if (template_document_)
    return *template_document_;

  if (IsHTMLDocument()) {
    DocumentInit init = DocumentInit::FromContext(ContextDocument(), BlankURL())
                            .WithNewRegistrationContext();
    template_document_ = HTMLDocument::Create(init);
  } else {
    template_document_ = Document::Create(DocumentInit(BlankURL()));
  }

  template_document_->template_document_host_ = this;  // balanced in dtor.

  return *template_document_.Get();
}

void Document::DidAssociateFormControl(Element* element) {
  if (!GetFrame() || !GetFrame()->GetPage() || !HasFinishedParsing())
    return;

  // We add a slight delay because this could be called rapidly.
  if (!did_associate_form_controls_timer_.IsActive())
    did_associate_form_controls_timer_.StartOneShot(0.3, BLINK_FROM_HERE);
}

void Document::DidAssociateFormControlsTimerFired(TimerBase* timer) {
  DCHECK_EQ(timer, &did_associate_form_controls_timer_);
  if (!GetFrame() || !GetFrame()->GetPage())
    return;

  GetFrame()->GetPage()->GetChromeClient().DidAssociateFormControlsAfterLoad(
      GetFrame());
}

float Document::DevicePixelRatio() const {
  return frame_ ? frame_->DevicePixelRatio() : 1.0;
}

TextAutosizer* Document::GetTextAutosizer() {
  if (!text_autosizer_)
    text_autosizer_ = TextAutosizer::Create(this);
  return text_autosizer_.Get();
}

void Document::SetAutofocusElement(Element* element) {
  if (!element) {
    autofocus_element_ = nullptr;
    return;
  }
  if (has_autofocused_)
    return;
  has_autofocused_ = true;
  DCHECK(!autofocus_element_);
  autofocus_element_ = element;
  TaskRunnerHelper::Get(TaskType::kUserInteraction, this)
      ->PostTask(BLINK_FROM_HERE,
                 WTF::Bind(&RunAutofocusTask, WrapWeakPersistent(this)));
}

Element* Document::ActiveElement() const {
  if (Element* element = AdjustedFocusedElement())
    return element;
  return body();
}

bool Document::hasFocus() const {
  return GetPage() && GetPage()->GetFocusController().IsDocumentFocused(*this);
}

template <unsigned type>
bool ShouldInvalidateNodeListCachesForAttr(
    const HeapHashSet<WeakMember<const LiveNodeListBase>> node_lists[],
    const QualifiedName& attr_name) {
  if (!node_lists[type].IsEmpty() &&
      LiveNodeListBase::ShouldInvalidateTypeOnAttributeChange(
          static_cast<NodeListInvalidationType>(type), attr_name))
    return true;
  return ShouldInvalidateNodeListCachesForAttr<type + 1>(node_lists, attr_name);
}

template <>
bool ShouldInvalidateNodeListCachesForAttr<kNumNodeListInvalidationTypes>(
    const HeapHashSet<WeakMember<const LiveNodeListBase>>[],
    const QualifiedName&) {
  return false;
}

bool Document::ShouldInvalidateNodeListCaches(
    const QualifiedName* attr_name) const {
  if (attr_name) {
    return ShouldInvalidateNodeListCachesForAttr<
        kDoNotInvalidateOnAttributeChanges + 1>(node_lists_, *attr_name);
  }

  for (int type = 0; type < kNumNodeListInvalidationTypes; ++type) {
    if (!node_lists_[type].IsEmpty())
      return true;
  }

  return false;
}

void Document::InvalidateNodeListCaches(const QualifiedName* attr_name) {
  for (const LiveNodeListBase* list : lists_invalidated_at_document_)
    list->InvalidateCacheForAttribute(attr_name);
}

void Document::PlatformColorsChanged() {
  if (!IsActive())
    return;

  GetStyleEngine().PlatformColorsChanged();
}

bool Document::IsSecureContext(String& error_message) const {
  if (!IsSecureContext()) {
    error_message = SecurityOrigin::IsPotentiallyTrustworthyErrorMessage();
    return false;
  }
  return true;
}

bool Document::IsSecureContext() const {
  bool is_secure = IsSecureContextImpl();
  if (GetSandboxFlags() != kSandboxNone) {
    UseCounter::Count(
        *this, is_secure
                   ? UseCounter::kSecureContextCheckForSandboxedOriginPassed
                   : UseCounter::kSecureContextCheckForSandboxedOriginFailed);
  }
  UseCounter::Count(*this, is_secure ? UseCounter::kSecureContextCheckPassed
                                     : UseCounter::kSecureContextCheckFailed);
  return is_secure;
}

void Document::EnforceInsecureRequestPolicy(WebInsecureRequestPolicy policy) {
  // Combine the new policy with the existing policy, as a base policy may be
  // inherited from a remote parent before this page's policy is set. In other
  // words, insecure requests should be upgraded or blocked if _either_ the
  // existing policy or the newly enforced policy triggers upgrades or
  // blockage.
  SetInsecureRequestPolicy(GetInsecureRequestPolicy() | policy);
  if (GetFrame())
    GetFrame()->Loader().Client()->DidEnforceInsecureRequestPolicy(
        GetInsecureRequestPolicy());
}

void Document::SetShadowCascadeOrder(ShadowCascadeOrder order) {
  DCHECK_NE(order, ShadowCascadeOrder::kShadowCascadeNone);

  if (order == shadow_cascade_order_)
    return;

  if (order == ShadowCascadeOrder::kShadowCascadeV0) {
    may_contain_v0_shadow_ = true;
    if (shadow_cascade_order_ == ShadowCascadeOrder::kShadowCascadeV1)
      UseCounter::Count(*this, UseCounter::kMixedShadowRootV0AndV1);
  }

  // For V0 -> V1 upgrade, we need style recalculation for the whole document.
  if (shadow_cascade_order_ == ShadowCascadeOrder::kShadowCascadeV0 &&
      order == ShadowCascadeOrder::kShadowCascadeV1) {
    this->SetNeedsStyleRecalc(
        kSubtreeStyleChange,
        StyleChangeReasonForTracing::Create(StyleChangeReason::kShadow));
    UseCounter::Count(*this, UseCounter::kMixedShadowRootV0AndV1);
  }

  if (order > shadow_cascade_order_)
    shadow_cascade_order_ = order;
}

LayoutViewItem Document::GetLayoutViewItem() const {
  return LayoutViewItem(layout_view_);
}

PropertyRegistry* Document::GetPropertyRegistry() {
  // TODO(timloh): When the flag is removed, return a reference instead.
  if (!property_registry_ && RuntimeEnabledFeatures::cssVariables2Enabled())
    property_registry_ = PropertyRegistry::Create();
  return property_registry_;
}

const PropertyRegistry* Document::GetPropertyRegistry() const {
  return const_cast<Document*>(this)->GetPropertyRegistry();
}

void Document::IncrementPasswordCount() {
  ++password_count_;
  if (IsSecureContext() || password_count_ != 1) {
    // The browser process only cares about passwords on pages where the
    // top-level URL is not secure. Secure contexts must have a top-level
    // URL that is secure, so there is no need to send notifications for
    // password fields in secure contexts.
    //
    // Also, only send a message on the first visible password field; the
    // browser process doesn't care about the presence of additional
    // password fields beyond that.
    return;
  }
  SendSensitiveInputVisibility();
}

void Document::DecrementPasswordCount() {
  DCHECK_GT(password_count_, 0u);
  --password_count_;
  if (IsSecureContext() || password_count_ > 0)
    return;
  SendSensitiveInputVisibility();
}

CoreProbeSink* Document::GetProbeSink() {
  LocalFrame* frame = GetFrame();
  if (!frame && TemplateDocumentHost())
    frame = TemplateDocumentHost()->GetFrame();
  return probe::ToCoreProbeSink(frame);
}

DEFINE_TRACE(Document) {
  visitor->Trace(imports_controller_);
  visitor->Trace(doc_type_);
  visitor->Trace(implementation_);
  visitor->Trace(autofocus_element_);
  visitor->Trace(focused_element_);
  visitor->Trace(sequential_focus_navigation_starting_point_);
  visitor->Trace(hover_element_);
  visitor->Trace(active_hover_element_);
  visitor->Trace(document_element_);
  visitor->Trace(root_scroller_controller_);
  visitor->Trace(title_element_);
  visitor->Trace(ax_object_cache_);
  visitor->Trace(markers_);
  visitor->Trace(css_target_);
  visitor->Trace(current_script_stack_);
  visitor->Trace(script_runner_);
  visitor->Trace(lists_invalidated_at_document_);
  for (int i = 0; i < kNumNodeListInvalidationTypes; ++i)
    visitor->Trace(node_lists_[i]);
  visitor->Trace(top_layer_elements_);
  visitor->Trace(elem_sheet_);
  visitor->Trace(node_iterators_);
  visitor->Trace(ranges_);
  visitor->Trace(style_engine_);
  visitor->Trace(form_controller_);
  visitor->Trace(visited_link_state_);
  visitor->Trace(frame_);
  visitor->Trace(dom_window_);
  visitor->Trace(fetcher_);
  visitor->Trace(parser_);
  visitor->Trace(context_features_);
  visitor->Trace(style_sheet_list_);
  visitor->Trace(document_timing_);
  visitor->Trace(media_query_matcher_);
  visitor->Trace(scripted_animation_controller_);
  visitor->Trace(scripted_idle_task_controller_);
  visitor->Trace(text_autosizer_);
  visitor->Trace(registration_context_);
  visitor->Trace(custom_element_microtask_run_queue_);
  visitor->Trace(element_data_cache_);
  visitor->Trace(use_elements_needing_update_);
  visitor->Trace(timers_);
  visitor->Trace(template_document_);
  visitor->Trace(template_document_host_);
  visitor->Trace(user_action_elements_);
  visitor->Trace(svg_extensions_);
  visitor->Trace(timeline_);
  visitor->Trace(compositor_pending_animations_);
  visitor->Trace(context_document_);
  visitor->Trace(canvas_font_cache_);
  visitor->Trace(intersection_observer_controller_);
  visitor->Trace(snap_coordinator_);
  visitor->Trace(resize_observer_controller_);
  visitor->Trace(property_registry_);
  visitor->Trace(network_state_observer_);
  Supplementable<Document>::Trace(visitor);
  TreeScope::Trace(visitor);
  ContainerNode::Trace(visitor);
  ExecutionContext::Trace(visitor);
  SecurityContext::Trace(visitor);
  SynchronousMutationNotifier::Trace(visitor);
}

void Document::RecordDeferredLoadReason(WouldLoadReason reason) {
  DCHECK(would_load_reason_ == WouldLoadReason::kInvalid ||
         reason != WouldLoadReason::kCreated);
  DCHECK(reason != WouldLoadReason::kInvalid);
  DCHECK(GetFrame());
  DCHECK(GetFrame()->IsCrossOriginSubframe());
  if (reason <= would_load_reason_ ||
      !GetFrame()->Loader().StateMachine()->CommittedFirstRealDocumentLoad())
    return;
  for (int i = static_cast<int>(would_load_reason_) + 1;
       i <= static_cast<int>(reason); ++i)
    RecordLoadReasonToHistogram(static_cast<WouldLoadReason>(i));
  would_load_reason_ = reason;
}

DEFINE_TRACE_WRAPPERS(Document) {
  // node_lists_ are traced in their corresponding NodeListsNodeData, keeping
  // them only alive for live nodes. Otherwise we would keep lists of dead
  // nodes alive that have not yet been invalidated.
  visitor->TraceWrappers(imports_controller_);
  visitor->TraceWrappers(implementation_);
  visitor->TraceWrappers(style_sheet_list_);
  visitor->TraceWrappers(style_engine_);
  // Cannot trace in Supplementable<Document> as it is part of platform/ and
  // thus cannot refer to ScriptWrappableVisitor.
  visitor->TraceWrappers(
      static_cast<FontFaceSet*>(Supplementable<Document>::supplements_.at(
          FontFaceSet::SupplementName())));
  ContainerNode::TraceWrappers(visitor);
}

template class CORE_TEMPLATE_EXPORT Supplement<Document>;

}  // namespace blink

#ifndef NDEBUG
static WeakDocumentSet& liveDocumentSet() {
  DEFINE_STATIC_LOCAL(WeakDocumentSet, set, ());
  return set;
}

void showLiveDocumentInstances() {
  WeakDocumentSet& set = liveDocumentSet();
  fprintf(stderr, "There are %u documents currently alive:\n", set.size());
  for (blink::Document* document : set)
    fprintf(stderr, "- Document %p URL: %s\n", document,
            document->Url().GetString().Utf8().data());
}
#endif
