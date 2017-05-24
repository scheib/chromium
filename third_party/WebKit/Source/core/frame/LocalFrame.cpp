/*
 * Copyright (C) 1998, 1999 Torben Weis <weis@kde.org>
 *                     1999 Lars Knoll <knoll@kde.org>
 *                     1999 Antti Koivisto <koivisto@kde.org>
 *                     2000 Simon Hausmann <hausmann@kde.org>
 *                     2000 Stefan Schimanski <1Stein@gmx.de>
 *                     2001 George Staikos <staikos@kde.org>
 * Copyright (C) 2004, 2005, 2006, 2007, 2008, 2009, 2010, 2011 Apple Inc. All
 * rights reserved.
 * Copyright (C) 2005 Alexey Proskuryakov <ap@nypop.com>
 * Copyright (C) 2008 Nokia Corporation and/or its subsidiary(-ies)
 * Copyright (C) 2008 Eric Seidel <eric@webkit.org>
 * Copyright (C) 2008 Google Inc.
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

#include "core/frame/LocalFrame.h"

#include <memory>

#include "bindings/core/v8/ScriptController.h"
#include "core/CoreProbeSink.h"
#include "core/dom/ChildFrameDisconnector.h"
#include "core/dom/DocumentType.h"
#include "core/dom/StyleChangeReason.h"
#include "core/editing/EditingUtilities.h"
#include "core/editing/Editor.h"
#include "core/editing/FrameSelection.h"
#include "core/editing/InputMethodController.h"
#include "core/editing/serializers/Serialization.h"
#include "core/editing/spellcheck/SpellChecker.h"
#include "core/events/Event.h"
#include "core/frame/ContentSettingsClient.h"
#include "core/frame/EventHandlerRegistry.h"
#include "core/frame/FrameConsole.h"
#include "core/frame/FrameView.h"
#include "core/frame/LocalDOMWindow.h"
#include "core/frame/LocalFrameClient.h"
#include "core/frame/PerformanceMonitor.h"
#include "core/frame/Settings.h"
#include "core/frame/VisualViewport.h"
#include "core/html/HTMLFrameElementBase.h"
#include "core/html/HTMLPlugInElement.h"
#include "core/input/EventHandler.h"
#include "core/inspector/ConsoleMessage.h"
#include "core/layout/HitTestResult.h"
#include "core/layout/LayoutView.h"
#include "core/layout/api/LayoutPartItem.h"
#include "core/layout/api/LayoutViewItem.h"
#include "core/layout/compositing/PaintLayerCompositor.h"
#include "core/loader/DocumentLoader.h"
#include "core/loader/FrameLoadRequest.h"
#include "core/loader/NavigationScheduler.h"
#include "core/page/ChromeClient.h"
#include "core/page/FocusController.h"
#include "core/page/Page.h"
#include "core/page/scrolling/ScrollingCoordinator.h"
#include "core/paint/ObjectPainter.h"
#include "core/paint/PaintInfo.h"
#include "core/paint/PaintLayer.h"
#include "core/paint/PaintLayerPainter.h"
#include "core/paint/TransformRecorder.h"
#include "core/probe/CoreProbes.h"
#include "core/svg/SVGDocumentExtensions.h"
#include "core/timing/Performance.h"
#include "platform/DragImage.h"
#include "platform/PluginScriptForbiddenScope.h"
#include "platform/RuntimeEnabledFeatures.h"
#include "platform/ScriptForbiddenScope.h"
#include "platform/WebFrameScheduler.h"
#include "platform/graphics/GraphicsContext.h"
#include "platform/graphics/StaticBitmapImage.h"
#include "platform/graphics/paint/ClipRecorder.h"
#include "platform/graphics/paint/PaintCanvas.h"
#include "platform/graphics/paint/PaintController.h"
#include "platform/graphics/paint/PaintRecordBuilder.h"
#include "platform/graphics/paint/TransformDisplayItem.h"
#include "platform/json/JSONValues.h"
#include "platform/loader/fetch/FetchParameters.h"
#include "platform/loader/fetch/ResourceFetcher.h"
#include "platform/loader/fetch/ResourceRequest.h"
#include "platform/plugins/PluginData.h"
#include "platform/scheduler/renderer/web_view_scheduler.h"
#include "platform/text/TextStream.h"
#include "platform/wtf/PtrUtil.h"
#include "platform/wtf/StdLibExtras.h"
#include "public/platform/InterfaceProvider.h"
#include "public/platform/InterfaceRegistry.h"
#include "public/platform/WebScreenInfo.h"
#include "public/platform/WebURLRequest.h"
#include "third_party/skia/include/core/SkImage.h"
#include "third_party/skia/include/core/SkSurface.h"

namespace blink {

using namespace HTMLNames;

namespace {

// Converts from bounds in CSS space to device space based on the given
// frame.
static FloatRect DeviceSpaceBounds(const FloatRect css_bounds,
                                   const LocalFrame& frame) {
  float device_scale_factor = frame.GetPage()->DeviceScaleFactorDeprecated();
  float page_scale_factor = frame.GetPage()->GetVisualViewport().Scale();
  FloatRect device_bounds(css_bounds);
  device_bounds.SetWidth(css_bounds.Width() * device_scale_factor *
                         page_scale_factor);
  device_bounds.SetHeight(css_bounds.Height() * device_scale_factor *
                          page_scale_factor);
  return device_bounds;
}

// Returns a DragImage whose bitmap contains |contents|, positioned and scaled
// in device space.
static std::unique_ptr<DragImage> CreateDragImage(
    const LocalFrame& frame,
    float opacity,
    RespectImageOrientationEnum image_orientation,
    const FloatRect& css_bounds,
    PaintRecordBuilder& builder,
    const PropertyTreeState& property_tree_state) {
  float device_scale_factor = frame.GetPage()->DeviceScaleFactorDeprecated();
  float page_scale_factor = frame.GetPage()->GetVisualViewport().Scale();

  FloatRect device_bounds = DeviceSpaceBounds(css_bounds, frame);

  AffineTransform transform;
  transform.Scale(device_scale_factor * page_scale_factor);
  transform.Translate(-device_bounds.X(), -device_bounds.Y());

  // Rasterize upfront, since DragImage::create() is going to do it anyway
  // (SkImage::asLegacyBitmap).
  SkSurfaceProps surface_props(0, kUnknown_SkPixelGeometry);
  sk_sp<SkSurface> surface = SkSurface::MakeRasterN32Premul(
      device_bounds.Width(), device_bounds.Height(), &surface_props);
  if (!surface)
    return nullptr;

  SkiaPaintCanvas skia_paint_canvas(surface->getCanvas());
  skia_paint_canvas.concat(AffineTransformToSkMatrix(transform));
  builder.EndRecording(skia_paint_canvas, property_tree_state);

  RefPtr<Image> image = StaticBitmapImage::Create(surface->makeImageSnapshot());
  float screen_device_scale_factor =
      frame.GetPage()->GetChromeClient().GetScreenInfo().device_scale_factor;

  return DragImage::Create(image.Get(), image_orientation,
                           screen_device_scale_factor, kInterpolationHigh,
                           opacity);
}

class DraggedNodeImageBuilder {
  STACK_ALLOCATED();

 public:
  DraggedNodeImageBuilder(const LocalFrame& local_frame, Node& node)
      : local_frame_(&local_frame),
        node_(&node)
#if DCHECK_IS_ON()
        ,
        dom_tree_version_(node.GetDocument().DomTreeVersion())
#endif
  {
    for (Node& descendant : NodeTraversal::InclusiveDescendantsOf(*node_))
      descendant.SetDragged(true);
  }

  ~DraggedNodeImageBuilder() {
#if DCHECK_IS_ON()
    DCHECK_EQ(dom_tree_version_, node_->GetDocument().DomTreeVersion());
#endif
    for (Node& descendant : NodeTraversal::InclusiveDescendantsOf(*node_))
      descendant.SetDragged(false);
  }

  std::unique_ptr<DragImage> CreateImage() {
#if DCHECK_IS_ON()
    DCHECK_EQ(dom_tree_version_, node_->GetDocument().DomTreeVersion());
#endif
    // Construct layout object for |m_node| with pseudo class "-webkit-drag"
    local_frame_->View()->UpdateAllLifecyclePhasesExceptPaint();
    LayoutObject* const dragged_layout_object = node_->GetLayoutObject();
    if (!dragged_layout_object)
      return nullptr;
    // Paint starting at the nearest stacking context, clipped to the object
    // itself. This will also paint the contents behind the object if the
    // object contains transparency and there are other elements in the same
    // stacking context which stacked below.
    PaintLayer* layer = dragged_layout_object->EnclosingLayer();
    if (!layer->StackingNode()->IsStackingContext())
      layer = layer->StackingNode()->AncestorStackingContextNode()->Layer();
    IntRect absolute_bounding_box =
        dragged_layout_object->AbsoluteBoundingBoxRectIncludingDescendants();
    FloatRect bounding_box =
        layer->GetLayoutObject()
            .AbsoluteToLocalQuad(FloatQuad(absolute_bounding_box),
                                 kUseTransforms)
            .BoundingBox();
    PaintLayerPaintingInfo painting_info(layer, LayoutRect(bounding_box),
                                         kGlobalPaintFlattenCompositingLayers,
                                         LayoutSize());
    PaintLayerFlags flags = kPaintLayerHaveTransparency |
                            kPaintLayerAppliedTransform |
                            kPaintLayerUncachedClipRects;
    PaintRecordBuilder builder(DeviceSpaceBounds(bounding_box, *local_frame_));
    PaintLayerPainter(*layer).Paint(builder.Context(), painting_info, flags);
    PropertyTreeState border_box_properties = PropertyTreeState::Root();
    if (RuntimeEnabledFeatures::slimmingPaintV2Enabled()) {
      border_box_properties =
          *layer->GetLayoutObject().LocalBorderBoxProperties();
    }
    return CreateDragImage(
        *local_frame_, 1.0f,
        LayoutObject::ShouldRespectImageOrientation(dragged_layout_object),
        bounding_box, builder, border_box_properties);
  }

 private:
  const Member<const LocalFrame> local_frame_;
  const Member<Node> node_;
#if DCHECK_IS_ON()
  const uint64_t dom_tree_version_;
#endif
};

inline float ParentPageZoomFactor(LocalFrame* frame) {
  Frame* parent = frame->Tree().Parent();
  if (!parent || !parent->IsLocalFrame())
    return 1;
  return ToLocalFrame(parent)->PageZoomFactor();
}

inline float ParentTextZoomFactor(LocalFrame* frame) {
  Frame* parent = frame->Tree().Parent();
  if (!parent || !parent->IsLocalFrame())
    return 1;
  return ToLocalFrame(parent)->TextZoomFactor();
}

using FrameInitCallbackVector = WTF::Vector<LocalFrame::FrameInitCallback>;
FrameInitCallbackVector& GetInitializationVector() {
  DEFINE_THREAD_SAFE_STATIC_LOCAL(FrameInitCallbackVector,
                                  initialization_vector,
                                  new FrameInitCallbackVector());
  return initialization_vector;
}

}  // namespace

template class CORE_TEMPLATE_EXPORT Supplement<LocalFrame>;

LocalFrame* LocalFrame::Create(LocalFrameClient* client,
                               Page& page,
                               FrameOwner* owner,
                               InterfaceProvider* interface_provider,
                               InterfaceRegistry* interface_registry) {
  LocalFrame* frame = new LocalFrame(
      client, page, owner,
      interface_provider ? interface_provider
                         : InterfaceProvider::GetEmptyInterfaceProvider(),
      interface_registry ? interface_registry
                         : InterfaceRegistry::GetEmptyInterfaceRegistry());
  probe::frameAttachedToParent(frame);
  return frame;
}

void LocalFrame::Init() {
  DCHECK(!GetInitializationVector().IsEmpty());
  for (auto& initilization_callback : GetInitializationVector()) {
    initilization_callback(this);
  }

  loader_.Init();
}

void LocalFrame::SetView(FrameView* view) {
  DCHECK(!view_ || view_ != view);
  DCHECK(!GetDocument() || !GetDocument()->IsActive());

  GetEventHandler().Clear();

  view_ = view;
}

void LocalFrame::CreateView(const IntSize& viewport_size,
                            const Color& background_color,
                            ScrollbarMode horizontal_scrollbar_mode,
                            bool horizontal_lock,
                            ScrollbarMode vertical_scrollbar_mode,
                            bool vertical_lock) {
  DCHECK(this);
  DCHECK(GetPage());

  bool is_local_root = this->IsLocalRoot();

  if (is_local_root && View())
    View()->SetParentVisible(false);

  SetView(nullptr);

  FrameView* frame_view = nullptr;
  if (is_local_root) {
    frame_view = FrameView::Create(*this, viewport_size);

    // The layout size is set by WebViewImpl to support @viewport
    frame_view->SetLayoutSizeFixedToFrameSize(false);
  } else {
    frame_view = FrameView::Create(*this);
  }

  frame_view->SetScrollbarModes(horizontal_scrollbar_mode,
                                vertical_scrollbar_mode, horizontal_lock,
                                vertical_lock);

  SetView(frame_view);

  frame_view->UpdateBaseBackgroundColorRecursively(background_color);

  if (is_local_root)
    frame_view->SetParentVisible(true);

  // FIXME: Not clear what the right thing for OOPI is here.
  if (!OwnerLayoutItem().IsNull()) {
    HTMLFrameOwnerElement* owner = DeprecatedLocalOwner();
    DCHECK(owner);
    // FIXME: OOPI might lead to us temporarily lying to a frame and telling it
    // that it's owned by a FrameOwner that knows nothing about it. If we're
    // lying to this frame, don't let it clobber the existing widget.
    if (owner->ContentFrame() == this)
      owner->SetWidget(frame_view);
  }

  if (Owner())
    View()->SetCanHaveScrollbars(Owner()->ScrollingMode() !=
                                 kScrollbarAlwaysOff);
}

LocalFrame::~LocalFrame() {
  // Verify that the FrameView has been cleared as part of detaching
  // the frame owner.
  DCHECK(!view_);
}

DEFINE_TRACE(LocalFrame) {
  visitor->Trace(probe_sink_);
  visitor->Trace(performance_monitor_);
  visitor->Trace(loader_);
  visitor->Trace(navigation_scheduler_);
  visitor->Trace(view_);
  visitor->Trace(dom_window_);
  visitor->Trace(page_popup_owner_);
  visitor->Trace(script_controller_);
  visitor->Trace(editor_);
  visitor->Trace(spell_checker_);
  visitor->Trace(selection_);
  visitor->Trace(event_handler_);
  visitor->Trace(console_);
  visitor->Trace(input_method_controller_);
  Frame::Trace(visitor);
  Supplementable<LocalFrame>::Trace(visitor);
}

void LocalFrame::Navigate(Document& origin_document,
                          const KURL& url,
                          bool replace_current_item,
                          UserGestureStatus user_gesture_status) {
  navigation_scheduler_->ScheduleLocationChange(&origin_document, url,
                                                replace_current_item);
}

void LocalFrame::Navigate(const FrameLoadRequest& request) {
  loader_.Load(request);
}

void LocalFrame::Reload(FrameLoadType load_type,
                        ClientRedirectPolicy client_redirect_policy) {
  DCHECK(IsReloadLoadType(load_type));
  if (client_redirect_policy == ClientRedirectPolicy::kNotClientRedirect) {
    if (!loader_.GetDocumentLoader()->GetHistoryItem())
      return;
    FrameLoadRequest request = FrameLoadRequest(
        nullptr, loader_.ResourceRequestForReload(load_type, KURL(),
                                                  client_redirect_policy));
    request.SetClientRedirect(client_redirect_policy);
    loader_.Load(request, load_type);
  } else {
    DCHECK_EQ(RuntimeEnabledFeatures::locationHardReloadEnabled()
                  ? kFrameLoadTypeReloadBypassingCache
                  : kFrameLoadTypeReload,
              load_type);
    navigation_scheduler_->ScheduleReload();
  }
}

void LocalFrame::Detach(FrameDetachType type) {
  // Note that detach() can be re-entered, so it's not possible to
  // DCHECK(isAttached()) here.
  lifecycle_.AdvanceTo(FrameLifecycle::kDetaching);

  if (IsLocalRoot())
    performance_monitor_->Shutdown();

  PluginScriptForbiddenScope forbid_plugin_destructor_scripting;
  loader_.StopAllLoaders();
  // Don't allow any new child frames to load in this frame: attaching a new
  // child frame during or after detaching children results in an attached
  // frame on a detached DOM tree, which is bad.
  SubframeLoadingDisabler disabler(*GetDocument());
  loader_.DispatchUnloadEvent();
  DetachChildren();

  // All done if detaching the subframes brought about a detach of this frame
  // also.
  if (!Client())
    return;

  // stopAllLoaders() needs to be called after detachChildren(), because
  // detachChildren() will trigger the unload event handlers of any child
  // frames, and those event handlers might start a new subresource load in this
  // frame.
  loader_.StopAllLoaders();
  loader_.Detach();
  GetDocument()->Shutdown();
  // This is the earliest that scripting can be disabled:
  // - FrameLoader::detach() can fire XHR abort events
  // - Document::shutdown()'s deferred widget updates can run script.
  ScriptForbiddenScope forbid_script;
  loader_.Clear();
  if (!Client())
    return;

  Client()->WillBeDetached();
  // Notify ScriptController that the frame is closing, since its cleanup ends
  // up calling back to LocalFrameClient via WindowProxy.
  GetScriptController().ClearForClose();
  SetView(nullptr);

  page_->GetEventHandlerRegistry().DidRemoveAllEventHandlers(*DomWindow());

  DomWindow()->FrameDestroyed();

  // TODO: Page should take care of updating focus/scrolling instead of Frame.
  // TODO: It's unclear as to why this is called more than once, but it is,
  // so page() could be null.
  if (GetPage() && GetPage()->GetFocusController().FocusedFrame() == this)
    GetPage()->GetFocusController().SetFocusedFrame(nullptr);

  if (GetPage() && GetPage()->GetScrollingCoordinator() && view_)
    GetPage()->GetScrollingCoordinator()->WillDestroyScrollableArea(
        view_.Get());

  probe::frameDetachedFromParent(this);
  Frame::Detach(type);

  supplements_.clear();
  frame_scheduler_.reset();
  WeakIdentifierMap<LocalFrame>::NotifyObjectDestroyed(this);
  lifecycle_.AdvanceTo(FrameLifecycle::kDetached);
}

bool LocalFrame::PrepareForCommit() {
  return Loader().PrepareForCommit();
}

SecurityContext* LocalFrame::GetSecurityContext() const {
  return GetDocument();
}

void LocalFrame::PrintNavigationErrorMessage(const Frame& target_frame,
                                             const char* reason) {
  // URLs aren't available for RemoteFrames, so the error message uses their
  // origin instead.
  String target_frame_description =
      target_frame.IsLocalFrame()
          ? "with URL '" +
                ToLocalFrame(target_frame).GetDocument()->Url().GetString() +
                "'"
          : "with origin '" +
                target_frame.GetSecurityContext()
                    ->GetSecurityOrigin()
                    ->ToString() +
                "'";
  String message =
      "Unsafe JavaScript attempt to initiate navigation for frame " +
      target_frame_description + " from frame with URL '" +
      GetDocument()->Url().GetString() + "'. " + reason + "\n";

  DomWindow()->PrintErrorMessage(message);
}

void LocalFrame::PrintNavigationWarning(const String& message) {
  console_->AddMessage(
      ConsoleMessage::Create(kJSMessageSource, kWarningMessageLevel, message));
}

bool LocalFrame::ShouldClose() {
  // TODO(dcheng): This should be fixed to dispatch beforeunload events to
  // both local and remote frames.
  return loader_.ShouldClose();
}

void LocalFrame::DetachChildren() {
  DCHECK(loader_.StateMachine()->CreatingInitialEmptyDocument() ||
         GetDocument());

  if (Document* document = this->GetDocument())
    ChildFrameDisconnector(*document).Disconnect();
}

void LocalFrame::DocumentAttached() {
  DCHECK(GetDocument());
  Selection().DocumentAttached(GetDocument());
  GetInputMethodController().DocumentAttached(GetDocument());
  GetSpellChecker().DocumentAttached(GetDocument());
  if (IsMainFrame())
    has_received_user_gesture_ = false;
}

LocalWindowProxy* LocalFrame::WindowProxy(DOMWrapperWorld& world) {
  return ToLocalWindowProxy(Frame::GetWindowProxy(world));
}

LocalDOMWindow* LocalFrame::DomWindow() const {
  return ToLocalDOMWindow(dom_window_);
}

void LocalFrame::SetDOMWindow(LocalDOMWindow* dom_window) {
  if (dom_window)
    GetScriptController().ClearWindowProxy();

  if (this->DomWindow())
    this->DomWindow()->Reset();
  dom_window_ = dom_window;
}

Document* LocalFrame::GetDocument() const {
  return DomWindow() ? DomWindow()->document() : nullptr;
}

void LocalFrame::SetPagePopupOwner(Element& owner) {
  page_popup_owner_ = &owner;
}

LayoutView* LocalFrame::ContentLayoutObject() const {
  return GetDocument() ? GetDocument()->GetLayoutView() : nullptr;
}

LayoutViewItem LocalFrame::ContentLayoutItem() const {
  return LayoutViewItem(ContentLayoutObject());
}

void LocalFrame::DidChangeVisibilityState() {
  if (GetDocument())
    GetDocument()->DidChangeVisibilityState();

  Frame::DidChangeVisibilityState();
}

LocalFrame& LocalFrame::LocalFrameRoot() const {
  const LocalFrame* cur_frame = this;
  while (cur_frame && cur_frame->Tree().Parent() &&
         cur_frame->Tree().Parent()->IsLocalFrame())
    cur_frame = ToLocalFrame(cur_frame->Tree().Parent());

  return const_cast<LocalFrame&>(*cur_frame);
}

bool LocalFrame::IsCrossOriginSubframe() const {
  const SecurityOrigin* security_origin =
      GetSecurityContext()->GetSecurityOrigin();
  return !security_origin->CanAccess(
      Tree().Top().GetSecurityContext()->GetSecurityOrigin());
}

void LocalFrame::SetPrinting(bool printing,
                             const FloatSize& page_size,
                             const FloatSize& original_page_size,
                             float maximum_shrink_ratio) {
  // In setting printing, we should not validate resources already cached for
  // the document.  See https://bugs.webkit.org/show_bug.cgi?id=43704
  ResourceCacheValidationSuppressor validation_suppressor(
      GetDocument()->Fetcher());

  GetDocument()->SetPrinting(printing ? Document::kPrinting
                                      : Document::kFinishingPrinting);
  View()->AdjustMediaTypeForPrinting(printing);

  if (ShouldUsePrintingLayout()) {
    View()->ForceLayoutForPagination(page_size, original_page_size,
                                     maximum_shrink_ratio);
  } else {
    if (LayoutView* layout_view = View()->GetLayoutView()) {
      layout_view->SetPreferredLogicalWidthsDirty();
      layout_view->SetNeedsLayout(LayoutInvalidationReason::kPrintingChanged);
      layout_view->SetShouldDoFullPaintInvalidationForViewAndAllDescendants();
    }
    View()->UpdateLayout();
    View()->AdjustViewSize();
  }

  // Subframes of the one we're printing don't lay out to the page size.
  for (Frame* child = Tree().FirstChild(); child;
       child = child->Tree().NextSibling()) {
    if (child->IsLocalFrame())
      ToLocalFrame(child)->SetPrinting(printing, FloatSize(), FloatSize(), 0);
  }

  if (RuntimeEnabledFeatures::slimmingPaintInvalidationEnabled())
    View()->SetSubtreeNeedsPaintPropertyUpdate();

  if (!printing)
    GetDocument()->SetPrinting(Document::kNotPrinting);
}

bool LocalFrame::ShouldUsePrintingLayout() const {
  // Only top frame being printed should be fit to page size.
  // Subframes should be constrained by parents only.
  return GetDocument()->Printing() &&
         (!Tree().Parent() || !Tree().Parent()->IsLocalFrame() ||
          !ToLocalFrame(Tree().Parent())->GetDocument()->Printing());
}

FloatSize LocalFrame::ResizePageRectsKeepingRatio(
    const FloatSize& original_size,
    const FloatSize& expected_size) const {
  if (ContentLayoutItem().IsNull())
    return FloatSize();

  bool is_horizontal = ContentLayoutItem().Style()->IsHorizontalWritingMode();
  float width = original_size.Width();
  float height = original_size.Height();
  if (!is_horizontal)
    std::swap(width, height);
  DCHECK_GT(fabs(width), std::numeric_limits<float>::epsilon());
  float ratio = height / width;

  float result_width =
      floorf(is_horizontal ? expected_size.Width() : expected_size.Height());
  float result_height = floorf(result_width * ratio);
  if (!is_horizontal)
    std::swap(result_width, result_height);
  return FloatSize(result_width, result_height);
}

void LocalFrame::SetPageZoomFactor(float factor) {
  SetPageAndTextZoomFactors(factor, text_zoom_factor_);
}

void LocalFrame::SetTextZoomFactor(float factor) {
  SetPageAndTextZoomFactors(page_zoom_factor_, factor);
}

void LocalFrame::SetPageAndTextZoomFactors(float page_zoom_factor,
                                           float text_zoom_factor) {
  if (page_zoom_factor_ == page_zoom_factor &&
      text_zoom_factor_ == text_zoom_factor)
    return;

  Page* page = this->GetPage();
  if (!page)
    return;

  Document* document = this->GetDocument();
  if (!document)
    return;

  // Respect SVGs zoomAndPan="disabled" property in standalone SVG documents.
  // FIXME: How to handle compound documents + zoomAndPan="disabled"? Needs SVG
  // WG clarification.
  if (document->IsSVGDocument()) {
    if (!document->AccessSVGExtensions().ZoomAndPanEnabled())
      return;
  }

  if (page_zoom_factor_ != page_zoom_factor) {
    if (FrameView* view = this->View()) {
      // Update the scroll position when doing a full page zoom, so the content
      // stays in relatively the same position.
      ScrollableArea* scrollable_area = view->LayoutViewportScrollableArea();
      ScrollOffset scroll_offset = scrollable_area->GetScrollOffset();
      float percent_difference = (page_zoom_factor / page_zoom_factor_);
      scrollable_area->SetScrollOffset(
          ScrollOffset(scroll_offset.Width() * percent_difference,
                       scroll_offset.Height() * percent_difference),
          kProgrammaticScroll);
    }
  }

  page_zoom_factor_ = page_zoom_factor;
  text_zoom_factor_ = text_zoom_factor;

  for (Frame* child = Tree().FirstChild(); child;
       child = child->Tree().NextSibling()) {
    if (child->IsLocalFrame())
      ToLocalFrame(child)->SetPageAndTextZoomFactors(page_zoom_factor_,
                                                     text_zoom_factor_);
  }

  document->MediaQueryAffectingValueChanged();
  document->SetNeedsStyleRecalc(
      kSubtreeStyleChange,
      StyleChangeReasonForTracing::Create(StyleChangeReason::kZoom));
  document->UpdateStyleAndLayoutIgnorePendingStylesheets();
}

void LocalFrame::DeviceScaleFactorChanged() {
  GetDocument()->MediaQueryAffectingValueChanged();
  GetDocument()->SetNeedsStyleRecalc(
      kSubtreeStyleChange,
      StyleChangeReasonForTracing::Create(StyleChangeReason::kZoom));
  for (Frame* child = Tree().FirstChild(); child;
       child = child->Tree().NextSibling()) {
    if (child->IsLocalFrame())
      ToLocalFrame(child)->DeviceScaleFactorChanged();
  }
}

double LocalFrame::DevicePixelRatio() const {
  if (!page_)
    return 0;

  double ratio = page_->DeviceScaleFactorDeprecated();
  ratio *= PageZoomFactor();
  return ratio;
}

std::unique_ptr<DragImage> LocalFrame::NodeImage(Node& node) {
  DraggedNodeImageBuilder image_node(*this, node);
  return image_node.CreateImage();
}

std::unique_ptr<DragImage> LocalFrame::DragImageForSelection(float opacity) {
  if (!Selection().ComputeVisibleSelectionInDOMTreeDeprecated().IsRange())
    return nullptr;

  view_->UpdateAllLifecyclePhasesExceptPaint();
  DCHECK(GetDocument()->IsActive());

  FloatRect painting_rect = FloatRect(Selection().Bounds());
  GlobalPaintFlags paint_flags =
      kGlobalPaintSelectionOnly | kGlobalPaintFlattenCompositingLayers;

  PaintRecordBuilder builder(DeviceSpaceBounds(painting_rect, *this));
  view_->PaintContents(builder.Context(), paint_flags,
                       EnclosingIntRect(painting_rect));
  return CreateDragImage(*this, opacity, kDoNotRespectImageOrientation,
                         painting_rect, builder, PropertyTreeState::Root());
}

String LocalFrame::SelectedText() const {
  return Selection().SelectedText();
}

String LocalFrame::SelectedTextForClipboard() const {
  if (!GetDocument())
    return g_empty_string;
  DCHECK(!GetDocument()->NeedsLayoutTreeUpdate());
  return Selection().SelectedTextForClipboard();
}

PositionWithAffinity LocalFrame::PositionForPoint(const IntPoint& frame_point) {
  HitTestResult result = GetEventHandler().HitTestResultAtPoint(frame_point);
  Node* node = result.InnerNodeOrImageMapImage();
  if (!node)
    return PositionWithAffinity();
  LayoutObject* layout_object = node->GetLayoutObject();
  if (!layout_object)
    return PositionWithAffinity();
  const PositionWithAffinity position =
      layout_object->PositionForPoint(result.LocalPoint());
  if (position.IsNull())
    return PositionWithAffinity(FirstPositionInOrBeforeNode(node));
  return position;
}

Document* LocalFrame::DocumentAtPoint(const IntPoint& point_in_root_frame) {
  if (!View())
    return nullptr;

  IntPoint pt = View()->RootFrameToContents(point_in_root_frame);

  if (ContentLayoutItem().IsNull())
    return nullptr;
  HitTestResult result = GetEventHandler().HitTestResultAtPoint(
      pt, HitTestRequest::kReadOnly | HitTestRequest::kActive);
  return result.InnerNode() ? &result.InnerNode()->GetDocument() : nullptr;
}

EphemeralRange LocalFrame::RangeForPoint(const IntPoint& frame_point) {
  const PositionWithAffinity position_with_affinity =
      PositionForPoint(frame_point);
  if (position_with_affinity.IsNull())
    return EphemeralRange();

  VisiblePosition position = CreateVisiblePosition(position_with_affinity);
  VisiblePosition previous = PreviousPositionOf(position);
  if (previous.IsNotNull()) {
    const EphemeralRange previous_character_range =
        MakeRange(previous, position);
    IntRect rect = GetEditor().FirstRectForRange(previous_character_range);
    if (rect.Contains(frame_point))
      return EphemeralRange(previous_character_range);
  }

  VisiblePosition next = NextPositionOf(position);
  const EphemeralRange next_character_range = MakeRange(position, next);
  if (next_character_range.IsNotNull()) {
    IntRect rect = GetEditor().FirstRectForRange(next_character_range);
    if (rect.Contains(frame_point))
      return EphemeralRange(next_character_range);
  }

  return EphemeralRange();
}

bool LocalFrame::ShouldReuseDefaultView(const KURL& url) const {
  // Secure transitions can only happen when navigating from the initial empty
  // document.
  if (!Loader().StateMachine()->IsDisplayingInitialEmptyDocument())
    return false;

  return GetDocument()->IsSecureTransitionTo(url);
}

void LocalFrame::RemoveSpellingMarkersUnderWords(const Vector<String>& words) {
  GetSpellChecker().RemoveSpellingMarkersUnderWords(words);
}

String LocalFrame::LayerTreeAsText(unsigned flags) const {
  if (ContentLayoutItem().IsNull())
    return String();

  std::unique_ptr<JSONObject> layers;
  if (RuntimeEnabledFeatures::slimmingPaintV2Enabled()) {
    layers = View()->CompositedLayersAsJSON(static_cast<LayerTreeFlags>(flags));
  } else {
    layers = ContentLayoutItem().Compositor()->LayerTreeAsJSON(
        static_cast<LayerTreeFlags>(flags));
  }

  if (flags & kLayerTreeIncludesPaintInvalidations) {
    std::unique_ptr<JSONArray> object_paint_invalidations =
        view_->TrackedObjectPaintInvalidationsAsJSON();
    if (object_paint_invalidations && object_paint_invalidations->size()) {
      if (!layers)
        layers = JSONObject::Create();
      layers->SetArray("objectPaintInvalidations",
                       std::move(object_paint_invalidations));
    }
  }

  return layers ? layers->ToPrettyJSONString() : String();
}

bool LocalFrame::ShouldThrottleRendering() const {
  return View() && View()->ShouldThrottleRendering();
}

void LocalFrame::RegisterInitializationCallback(FrameInitCallback callback) {
  GetInitializationVector().push_back(callback);
}

inline LocalFrame::LocalFrame(LocalFrameClient* client,
                              Page& page,
                              FrameOwner* owner,
                              InterfaceProvider* interface_provider,
                              InterfaceRegistry* interface_registry)
    : Frame(client, page, owner, LocalWindowProxyManager::Create(*this)),
      frame_scheduler_(page.GetChromeClient().CreateFrameScheduler(
          client->GetFrameBlameContext())),
      loader_(this),
      navigation_scheduler_(NavigationScheduler::Create(this)),
      script_controller_(ScriptController::Create(
          *this,
          *static_cast<LocalWindowProxyManager*>(GetWindowProxyManager()))),
      editor_(Editor::Create(*this)),
      spell_checker_(SpellChecker::Create(*this)),
      selection_(FrameSelection::Create(*this)),
      event_handler_(new EventHandler(*this)),
      console_(FrameConsole::Create(*this)),
      input_method_controller_(InputMethodController::Create(*this)),
      navigation_disable_count_(0),
      page_zoom_factor_(ParentPageZoomFactor(this)),
      text_zoom_factor_(ParentTextZoomFactor(this)),
      in_view_source_mode_(false),
      interface_provider_(interface_provider),
      interface_registry_(interface_registry) {
  if (IsLocalRoot()) {
    probe_sink_ = new CoreProbeSink();
    performance_monitor_ = new PerformanceMonitor(this);
  } else {
    probe_sink_ = LocalFrameRoot().probe_sink_;
    performance_monitor_ = LocalFrameRoot().performance_monitor_;
  }
}

WebFrameScheduler* LocalFrame::FrameScheduler() {
  return frame_scheduler_.get();
}

void LocalFrame::ScheduleVisualUpdateUnlessThrottled() {
  if (ShouldThrottleRendering())
    return;
  GetPage()->Animator().ScheduleVisualUpdate(this);
}

LocalFrameClient* LocalFrame::Client() const {
  return static_cast<LocalFrameClient*>(Frame::Client());
}

ContentSettingsClient* LocalFrame::GetContentSettingsClient() {
  return Client() ? &Client()->GetContentSettingsClient() : nullptr;
}

PluginData* LocalFrame::GetPluginData() const {
  if (!Loader().AllowPlugins(kNotAboutToInstantiatePlugin))
    return nullptr;
  return GetPage()->GetPluginData(
      Tree().Top().GetSecurityContext()->GetSecurityOrigin());
}

DEFINE_WEAK_IDENTIFIER_MAP(LocalFrame);

FrameNavigationDisabler::FrameNavigationDisabler(LocalFrame& frame)
    : frame_(&frame) {
  frame_->DisableNavigation();
}

FrameNavigationDisabler::~FrameNavigationDisabler() {
  frame_->EnableNavigation();
}

ScopedFrameBlamer::ScopedFrameBlamer(LocalFrame* frame) : frame_(frame) {
  if (frame_ && frame_->Client() && frame_->Client()->GetFrameBlameContext())
    frame_->Client()->GetFrameBlameContext()->Enter();
}

ScopedFrameBlamer::~ScopedFrameBlamer() {
  if (frame_ && frame_->Client() && frame_->Client()->GetFrameBlameContext())
    frame_->Client()->GetFrameBlameContext()->Leave();
}

void LocalFrame::MaybeAllowImagePlaceholder(FetchParameters& params) const {
  if (GetSettings() && GetSettings()->GetFetchImagePlaceholders()) {
    params.SetAllowImagePlaceholder();
    return;
  }

  if (Client() &&
      Client()->ShouldUseClientLoFiForRequest(params.GetResourceRequest())) {
    params.MutableResourceRequest().SetPreviewsState(
        params.GetResourceRequest().GetPreviewsState() |
        WebURLRequest::kClientLoFiOn);
    params.SetAllowImagePlaceholder();
  }
}

std::unique_ptr<WebURLLoader> LocalFrame::CreateURLLoader() {
  return Client()->CreateURLLoader();
}

}  // namespace blink
