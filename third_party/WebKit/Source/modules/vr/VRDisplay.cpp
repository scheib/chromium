// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "modules/vr/VRDisplay.h"

#include "core/css/StylePropertySet.h"
#include "core/dom/DOMException.h"
#include "core/dom/FrameRequestCallback.h"
#include "core/dom/ScriptedAnimationController.h"
#include "core/dom/TaskRunnerHelper.h"
#include "core/frame/FrameView.h"
#include "core/frame/ImageBitmap.h"
#include "core/frame/UseCounter.h"
#include "core/inspector/ConsoleMessage.h"
#include "core/layout/LayoutView.h"
#include "core/layout/compositing/PaintLayerCompositor.h"
#include "core/loader/DocumentLoader.h"
#include "gpu/command_buffer/client/gles2_interface.h"
#include "gpu/command_buffer/common/mailbox_holder.h"
#include "modules/EventTargetModules.h"
#include "modules/vr/NavigatorVR.h"
#include "modules/vr/VRController.h"
#include "modules/vr/VRDisplayCapabilities.h"
#include "modules/vr/VREyeParameters.h"
#include "modules/vr/VRFrameData.h"
#include "modules/vr/VRLayer.h"
#include "modules/vr/VRPose.h"
#include "modules/vr/VRStageParameters.h"
#include "modules/webgl/WebGLRenderingContextBase.h"
#include "platform/Histogram.h"
#include "platform/UserGestureIndicator.h"
#include "platform/instrumentation/tracing/TraceEvent.h"
#include "platform/wtf/AutoReset.h"
#include "platform/wtf/Time.h"
#include "public/platform/Platform.h"

#include <array>
#include "core/dom/ExecutionContext.h"

namespace blink {

namespace {

VREye StringToVREye(const String& which_eye) {
  if (which_eye == "left")
    return kVREyeLeft;
  if (which_eye == "right")
    return kVREyeRight;
  return kVREyeNone;
}

}  // namespace

VRDisplay::VRDisplay(NavigatorVR* navigator_vr,
                     device::mojom::blink::VRDisplayPtr display,
                     device::mojom::blink::VRDisplayClientRequest request)
    : ContextLifecycleObserver(navigator_vr->GetDocument()),
      navigator_vr_(navigator_vr),
      capabilities_(new VRDisplayCapabilities()),
      eye_parameters_left_(new VREyeParameters()),
      eye_parameters_right_(new VREyeParameters()),
      display_(std::move(display)),
      submit_frame_client_binding_(this),
      display_client_binding_(this, std::move(request)) {}

VRDisplay::~VRDisplay() {}

VRController* VRDisplay::Controller() {
  return navigator_vr_->Controller();
}

void VRDisplay::Update(const device::mojom::blink::VRDisplayInfoPtr& display) {
  display_id_ = display->index;
  display_name_ = display->displayName;
  is_connected_ = true;

  capabilities_->SetHasPosition(display->capabilities->hasPosition);
  capabilities_->SetHasExternalDisplay(
      display->capabilities->hasExternalDisplay);
  capabilities_->SetCanPresent(display->capabilities->canPresent);
  capabilities_->SetMaxLayers(display->capabilities->canPresent ? 1 : 0);

  // Ignore non presenting delegate
  bool is_valid = display->leftEye->renderWidth > 0;
  bool need_on_present_change = false;
  if (is_presenting_ && is_valid && !is_valid_device_for_presenting_) {
    need_on_present_change = true;
  }
  is_valid_device_for_presenting_ = is_valid;
  eye_parameters_left_->Update(display->leftEye);
  eye_parameters_right_->Update(display->rightEye);

  if (!display->stageParameters.is_null()) {
    if (!stage_parameters_)
      stage_parameters_ = new VRStageParameters();
    stage_parameters_->Update(display->stageParameters);
  } else {
    stage_parameters_ = nullptr;
  }

  if (need_on_present_change) {
    OnPresentChange();
  }
}

bool VRDisplay::getFrameData(VRFrameData* frame_data) {
  if (!FocusedOrPresenting() || !frame_pose_ || display_blurred_)
    return false;

  if (!frame_data)
    return false;

  if (depth_near_ == depth_far_)
    return false;

  return frame_data->Update(frame_pose_, eye_parameters_left_,
                            eye_parameters_right_, depth_near_, depth_far_);
}

VREyeParameters* VRDisplay::getEyeParameters(const String& which_eye) {
  switch (StringToVREye(which_eye)) {
    case kVREyeLeft:
      return eye_parameters_left_;
    case kVREyeRight:
      return eye_parameters_right_;
    default:
      return nullptr;
  }
}

int VRDisplay::requestAnimationFrame(FrameRequestCallback* callback) {
  DVLOG(2) << __FUNCTION__;
  Document* doc = this->GetDocument();
  if (!doc)
    return 0;
  pending_vrdisplay_raf_ = true;
  if (!vr_v_sync_provider_.is_bound()) {
    ConnectVSyncProvider();
  } else if (!display_blurred_ && !pending_vsync_) {
    pending_vsync_ = true;
    vr_v_sync_provider_->GetVSync(ConvertToBaseCallback(
        WTF::Bind(&VRDisplay::OnVSync, WrapWeakPersistent(this))));
  }
  callback->use_legacy_time_base_ = false;
  return EnsureScriptedAnimationController(doc).RegisterCallback(callback);
}

void VRDisplay::cancelAnimationFrame(int id) {
  DVLOG(2) << __FUNCTION__;
  if (!scripted_animation_controller_)
    return;
  scripted_animation_controller_->CancelCallback(id);
}

void VRDisplay::OnBlur() {
  DVLOG(1) << __FUNCTION__;
  display_blurred_ = true;
  vr_v_sync_provider_.reset();
  navigator_vr_->EnqueueVREvent(VRDisplayEvent::Create(
      EventTypeNames::vrdisplayblur, true, false, this, ""));
}

void VRDisplay::OnFocus() {
  DVLOG(1) << __FUNCTION__;
  display_blurred_ = false;
  ConnectVSyncProvider();
  navigator_vr_->EnqueueVREvent(VRDisplayEvent::Create(
      EventTypeNames::vrdisplayfocus, true, false, this, ""));
}

void ReportPresentationResult(PresentationResult result) {
  // Note that this is called twice for each call to requestPresent -
  // one to declare that requestPresent was called, and one for the
  // result.
  DEFINE_STATIC_LOCAL(
      EnumerationHistogram, vr_presentation_result_histogram,
      ("VRDisplayPresentResult",
       static_cast<int>(PresentationResult::kPresentationResultMax)));
  vr_presentation_result_histogram.Count(static_cast<int>(result));
}

ScriptPromise VRDisplay::requestPresent(ScriptState* script_state,
                                        const HeapVector<VRLayer>& layers) {
  DVLOG(1) << __FUNCTION__;
  ExecutionContext* execution_context = ExecutionContext::From(script_state);
  UseCounter::Count(execution_context, UseCounter::kVRRequestPresent);
  if (!execution_context->IsSecureContext()) {
    UseCounter::Count(execution_context,
                      UseCounter::kVRRequestPresentInsecureOrigin);
  }

  ReportPresentationResult(PresentationResult::kRequested);

  ScriptPromiseResolver* resolver = ScriptPromiseResolver::Create(script_state);
  ScriptPromise promise = resolver->Promise();

  // If the VRDisplay does not advertise the ability to present reject the
  // request.
  if (!capabilities_->canPresent()) {
    DOMException* exception =
        DOMException::Create(kInvalidStateError, "VRDisplay cannot present.");
    resolver->Reject(exception);
    ReportPresentationResult(PresentationResult::kVRDisplayCannotPresent);
    return promise;
  }

  bool first_present = !is_presenting_;

  // Initiating VR presentation is only allowed in response to a user gesture.
  // If the VRDisplay is already presenting, however, repeated calls are
  // allowed outside a user gesture so that the presented content may be
  // updated.
  if (first_present && !UserGestureIndicator::ProcessingUserGesture() &&
      !in_display_activate_) {
    DOMException* exception = DOMException::Create(
        kInvalidStateError, "API can only be initiated by a user gesture.");
    resolver->Reject(exception);
    ReportPresentationResult(PresentationResult::kNotInitiatedByUserGesture);
    return promise;
  }

  // When we are requesting to start presentation with a user action or the
  // display has activated, record the user action.
  if (first_present &&
      (UserGestureIndicator::ProcessingUserGesture() || in_display_activate_)) {
    Platform::Current()->RecordAction(
        UserMetricsAction("VR.WebVR.requestPresent"));
  }

  // A valid number of layers must be provided in order to present.
  if (layers.size() == 0 || layers.size() > capabilities_->maxLayers()) {
    ForceExitPresent();
    DOMException* exception =
        DOMException::Create(kInvalidStateError, "Invalid number of layers.");
    resolver->Reject(exception);
    ReportPresentationResult(PresentationResult::kInvalidNumberOfLayers);
    return promise;
  }

  // If what we were given has an invalid source, need to exit fullscreen with
  // previous, valid source, so delay m_layer reassignment
  if (layers[0].source().isNull()) {
    ForceExitPresent();
    DOMException* exception =
        DOMException::Create(kInvalidStateError, "Invalid layer source.");
    resolver->Reject(exception);
    ReportPresentationResult(PresentationResult::kInvalidLayerSource);
    return promise;
  }
  layer_ = layers[0];

  CanvasRenderingContext* rendering_context;
  if (layer_.source().isHTMLCanvasElement()) {
    rendering_context =
        layer_.source().getAsHTMLCanvasElement()->RenderingContext();
  } else {
    DCHECK(layer_.source().isOffscreenCanvas());
    rendering_context =
        layer_.source().getAsOffscreenCanvas()->RenderingContext();
  }

  if (!rendering_context || !rendering_context->Is3d()) {
    ForceExitPresent();
    DOMException* exception = DOMException::Create(
        kInvalidStateError, "Layer source must have a WebGLRenderingContext");
    resolver->Reject(exception);
    ReportPresentationResult(
        PresentationResult::kLayerSourceMissingWebGLContext);
    return promise;
  }

  // Save the WebGL script and underlying GL contexts for use by submitFrame().
  rendering_context_ = ToWebGLRenderingContextBase(rendering_context);
  context_gl_ = rendering_context_->ContextGL();

  if ((layer_.leftBounds().size() != 0 && layer_.leftBounds().size() != 4) ||
      (layer_.rightBounds().size() != 0 && layer_.rightBounds().size() != 4)) {
    ForceExitPresent();
    DOMException* exception = DOMException::Create(
        kInvalidStateError,
        "Layer bounds must either be an empty array or have 4 values");
    resolver->Reject(exception);
    ReportPresentationResult(PresentationResult::kInvalidLayerBounds);
    return promise;
  }

  if (!pending_present_resolvers_.IsEmpty()) {
    // If we are waiting on the results of a previous requestPresent call don't
    // fire a new request, just cache the resolver and resolve it when the
    // original request returns.
    pending_present_resolvers_.push_back(resolver);
  } else if (first_present) {
    bool secure_context =
        ExecutionContext::From(script_state)->IsSecureContext();
    if (!display_) {
      ForceExitPresent();
      DOMException* exception = DOMException::Create(
          kInvalidStateError, "The service is no longer active.");
      resolver->Reject(exception);
      return promise;
    }

    pending_present_resolvers_.push_back(resolver);
    submit_frame_client_binding_.Close();
    display_->RequestPresent(
        secure_context,
        submit_frame_client_binding_.CreateInterfacePtrAndBind(),
        ConvertToBaseCallback(
            WTF::Bind(&VRDisplay::OnPresentComplete, WrapPersistent(this))));
    pending_present_request_ = true;
  } else {
    UpdateLayerBounds();
    resolver->Resolve();
    ReportPresentationResult(PresentationResult::kSuccessAlreadyPresenting);
  }

  return promise;
}

void VRDisplay::OnPresentComplete(bool success) {
  pending_present_request_ = false;
  if (success) {
    this->BeginPresent();
  } else {
    this->ForceExitPresent();
    DOMException* exception = DOMException::Create(
        kNotAllowedError, "Presentation request was denied.");

    while (!pending_present_resolvers_.IsEmpty()) {
      ScriptPromiseResolver* resolver = pending_present_resolvers_.TakeFirst();
      resolver->Reject(exception);
    }
  }
}

ScriptPromise VRDisplay::exitPresent(ScriptState* script_state) {
  DVLOG(1) << __FUNCTION__;
  ScriptPromiseResolver* resolver = ScriptPromiseResolver::Create(script_state);
  ScriptPromise promise = resolver->Promise();

  if (!is_presenting_) {
    // Can't stop presenting if we're not presenting.
    DOMException* exception = DOMException::Create(
        kInvalidStateError, "VRDisplay is not presenting.");
    resolver->Reject(exception);
    return promise;
  }

  if (!display_) {
    DOMException* exception =
        DOMException::Create(kInvalidStateError, "VRService is not available.");
    resolver->Reject(exception);
    return promise;
  }
  display_->ExitPresent();

  resolver->Resolve();

  StopPresenting();

  return promise;
}

void VRDisplay::BeginPresent() {
  Document* doc = this->GetDocument();
  if (capabilities_->hasExternalDisplay()) {
    ForceExitPresent();
    DOMException* exception = DOMException::Create(
        kInvalidStateError,
        "VR Presentation not implemented for this VRDisplay.");
    while (!pending_present_resolvers_.IsEmpty()) {
      ScriptPromiseResolver* resolver = pending_present_resolvers_.TakeFirst();
      resolver->Reject(exception);
    }
    ReportPresentationResult(
        PresentationResult::kPresentationNotSupportedByDisplay);
    return;
  } else {
    if (layer_.source().isHTMLCanvasElement()) {
      // TODO(klausw,crbug.com/698923): suppress compositor updates
      // since they aren't needed, they do a fair amount of extra
      // work.
    } else {
      DCHECK(layer_.source().isOffscreenCanvas());
      // TODO(junov, crbug.com/695497): Implement OffscreenCanvas presentation
      ForceExitPresent();
      DOMException* exception = DOMException::Create(
          kInvalidStateError, "OffscreenCanvas presentation not implemented.");
      while (!pending_present_resolvers_.IsEmpty()) {
        ScriptPromiseResolver* resolver =
            pending_present_resolvers_.TakeFirst();
        resolver->Reject(exception);
      }
      ReportPresentationResult(
          PresentationResult::kPresentationNotSupportedByDisplay);
      return;
    }
  }

  if (doc) {
    Platform::Current()->RecordRapporURL("VR.WebVR.PresentSuccess",
                                         WebURL(doc->Url()));
  }

  is_presenting_ = true;
  ReportPresentationResult(PresentationResult::kSuccess);

  UpdateLayerBounds();

  while (!pending_present_resolvers_.IsEmpty()) {
    ScriptPromiseResolver* resolver = pending_present_resolvers_.TakeFirst();
    resolver->Resolve();
  }
  OnPresentChange();

  // For GVR, we shut down normal vsync processing during VR presentation.
  // Run window.rAF once manually so that applications get a chance to
  // schedule a VRDisplay.rAF in case they do so only while presenting.
  if (!pending_vrdisplay_raf_ && !capabilities_->hasExternalDisplay()) {
    double timestamp = WTF::MonotonicallyIncreasingTime();
    Platform::Current()->CurrentThread()->GetWebTaskRunner()->PostTask(
        BLINK_FROM_HERE, WTF::Bind(&VRDisplay::ProcessScheduledWindowAnimations,
                                   WrapWeakPersistent(this), timestamp));
  }
}

// Need to close service if exists and then free rendering context.
void VRDisplay::ForceExitPresent() {
  if (display_) {
    display_->ExitPresent();
  }
  StopPresenting();
}

void VRDisplay::UpdateLayerBounds() {
  if (!display_)
    return;

  // Set up the texture bounds for the provided layer
  device::mojom::blink::VRLayerBoundsPtr left_bounds =
      device::mojom::blink::VRLayerBounds::New();
  device::mojom::blink::VRLayerBoundsPtr right_bounds =
      device::mojom::blink::VRLayerBounds::New();

  if (layer_.leftBounds().size() == 4) {
    left_bounds->left = layer_.leftBounds()[0];
    left_bounds->top = layer_.leftBounds()[1];
    left_bounds->width = layer_.leftBounds()[2];
    left_bounds->height = layer_.leftBounds()[3];
  } else {
    // Left eye defaults
    left_bounds->left = 0.0f;
    left_bounds->top = 0.0f;
    left_bounds->width = 0.5f;
    left_bounds->height = 1.0f;
    layer_.setLeftBounds({0.0f, 0.0f, 0.5f, 1.0f});
  }

  if (layer_.rightBounds().size() == 4) {
    right_bounds->left = layer_.rightBounds()[0];
    right_bounds->top = layer_.rightBounds()[1];
    right_bounds->width = layer_.rightBounds()[2];
    right_bounds->height = layer_.rightBounds()[3];
  } else {
    // Right eye defaults
    right_bounds->left = 0.5f;
    right_bounds->top = 0.0f;
    right_bounds->width = 0.5f;
    right_bounds->height = 1.0f;
    layer_.setRightBounds({0.5f, 0.0f, 0.5f, 1.0f});
  }

  display_->UpdateLayerBounds(vr_frame_id_, std::move(left_bounds),
                              std::move(right_bounds), source_width_,
                              source_height_);
}

HeapVector<VRLayer> VRDisplay::getLayers() {
  HeapVector<VRLayer> layers;

  if (is_presenting_) {
    layers.push_back(layer_);
  }

  return layers;
}

void VRDisplay::submitFrame() {
  if (!display_)
    return;
  TRACE_EVENT1("gpu", "submitFrame", "frame", vr_frame_id_);

  Document* doc = this->GetDocument();
  if (!is_presenting_) {
    if (doc) {
      doc->AddConsoleMessage(ConsoleMessage::Create(
          kRenderingMessageSource, kWarningMessageLevel,
          "submitFrame has no effect when the VRDisplay is not presenting."));
    }
    return;
  }

  if (!in_animation_frame_) {
    if (doc) {
      doc->AddConsoleMessage(
          ConsoleMessage::Create(kRenderingMessageSource, kWarningMessageLevel,
                                 "submitFrame must be called within a "
                                 "VRDisplay.requestAnimationFrame callback."));
    }
    return;
  }

  if (!context_gl_) {
    // Something got confused, we can't submit frames without a GL context.
    return;
  }

  // No frame Id to write before submitting the frame.
  if (vr_frame_id_ < 0) {
    // TODO(klausw): There used to be a submitFrame here, but we can't
    // submit without a frameId and associated pose data. Just drop it.
    return;
  }

  TRACE_EVENT_BEGIN0("gpu", "VRDisplay::Flush_1");
  context_gl_->Flush();
  TRACE_EVENT_END0("gpu", "VRDisplay::Flush_1");

  // Check if the canvas got resized, if yes send a bounds update.
  int current_width = rendering_context_->drawingBufferWidth();
  int current_height = rendering_context_->drawingBufferHeight();
  if ((current_width != source_width_ || current_height != source_height_) &&
      current_width != 0 && current_height != 0) {
    source_width_ = current_width;
    source_height_ = current_height;
    UpdateLayerBounds();
  }

  // There's two types of synchronization needed for submitting frames:
  //
  // - Before submitting, need to wait for the previous frame to be
  //   pulled off the transfer surface to avoid lost frames. This
  //   is currently a compile-time option, normally we always want
  //   to defer this wait to increase parallelism.
  //
  // - After submitting, need to wait for the mailbox to be consumed,
  //   and must remain inside the execution context while waiting.
  //   The waitForPreviousTransferToFinish option defers this wait
  //   until the next frame. That's more efficient, but seems to
  //   cause wobbly framerates.
  bool wait_for_previous_transfer_to_finish =
      RuntimeEnabledFeatures::webVRExperimentalRenderingEnabled();

  if (wait_for_previous_transfer_to_finish) {
    TRACE_EVENT0("gpu", "VRDisplay::waitForPreviousTransferToFinish");
    while (pending_submit_frame_) {
      if (!submit_frame_client_binding_.WaitForIncomingMethodCall()) {
        DLOG(ERROR) << "Failed to receive SubmitFrame response";
        break;
      }
    }
  }

  TRACE_EVENT_BEGIN0("gpu", "VRDisplay::GetImage");
  RefPtr<Image> image_ref = rendering_context_->GetImage(
      kPreferAcceleration, kSnapshotReasonCreateImageBitmap);
  TRACE_EVENT_END0("gpu", "VRDisplay::GetImage");

  // Hardware-accelerated rendering should always be texture backed,
  // as implemented by AcceleratedStaticBitmapImage. Ensure this is
  // the case, don't attempt to render if using an unexpected drawing
  // path.
  if (!image_ref.Get() || !image_ref->IsTextureBacked()) {
    NOTREACHED() << "WebVR requires hardware-accelerated rendering to texture";
    return;
  }

  // The AcceleratedStaticBitmapImage must be kept alive until the
  // mailbox is used via createAndConsumeTextureCHROMIUM, the mailbox
  // itself does not keep it alive. We must keep a reference to the
  // image until the mailbox was consumed.
  StaticBitmapImage* static_image =
      static_cast<StaticBitmapImage*>(image_ref.Get());
  TRACE_EVENT_BEGIN0("gpu", "VRDisplay::EnsureMailbox");
  static_image->EnsureMailbox();
  TRACE_EVENT_END0("gpu", "VRDisplay::EnsureMailbox");

  if (wait_for_previous_transfer_to_finish) {
    // Save a reference to the image to keep it alive until next frame,
    // where we'll wait for the transfer to finish before overwriting
    // it.
    previous_image_ = std::move(image_ref);
  }

  // Create mailbox and sync token for transfer.
  TRACE_EVENT_BEGIN0("gpu", "VRDisplay::GetMailbox");
  auto mailbox = static_image->GetMailbox();
  TRACE_EVENT_END0("gpu", "VRDisplay::GetMailbox");
  // Flush to avoid black screen flashes which appear to be related to
  // "fence sync must be flushed before generating sync token" GL errors.
  TRACE_EVENT_BEGIN0("gpu", "VRDisplay::Flush_2");
  context_gl_->Flush();
  TRACE_EVENT_END0("gpu", "VRDisplay::Flush_2");
  auto sync_token = static_image->GetSyncToken();

  // Wait for the previous render to finish, to avoid losing frames in the
  // Android Surface / GLConsumer pair. TODO(klausw): make this tunable?
  // Other devices may have different preferences. Do this step as late
  // as possible before SubmitFrame to ensure we can do as much work as
  // possible in parallel with the previous frame's rendering.
  {
    TRACE_EVENT0("gpu", "waitForPreviousRenderToFinish");
    while (pending_previous_frame_render_) {
      if (!submit_frame_client_binding_.WaitForIncomingMethodCall()) {
        DLOG(ERROR) << "Failed to receive SubmitFrame response";
        break;
      }
    }
  }

  pending_previous_frame_render_ = true;
  pending_submit_frame_ = true;

  TRACE_EVENT_BEGIN0("gpu", "VRDisplay::SubmitFrame");
  display_->SubmitFrame(vr_frame_id_,
                        gpu::MailboxHolder(mailbox, sync_token, GL_TEXTURE_2D));
  TRACE_EVENT_END0("gpu", "VRDisplay::SubmitFrame");

  // If preserveDrawingBuffer is false, must clear now. Normally this
  // happens as part of compositing, but that's not active while
  // presenting, so run the responsible code directly.
  rendering_context_->MarkCompositedAndClearBackbufferIfNeeded();

  // If we're not deferring the wait for transferring the mailbox,
  // we need to wait for it now to prevent the image going out of
  // scope before its mailbox is retrieved.
  if (!wait_for_previous_transfer_to_finish) {
    TRACE_EVENT0("gpu", "waitForCurrentTransferToFinish");
    while (pending_submit_frame_) {
      if (!submit_frame_client_binding_.WaitForIncomingMethodCall()) {
        DLOG(ERROR) << "Failed to receive SubmitFrame response";
        break;
      }
    }
  }
}

void VRDisplay::OnSubmitFrameTransferred() {
  pending_submit_frame_ = false;
}

void VRDisplay::OnSubmitFrameRendered() {
  pending_previous_frame_render_ = false;
}

Document* VRDisplay::GetDocument() {
  return navigator_vr_->GetDocument();
}

void VRDisplay::OnPresentChange() {
  DVLOG(1) << __FUNCTION__ << ": is_presenting_=" << is_presenting_;
  if (is_presenting_ && !is_valid_device_for_presenting_) {
    DVLOG(1) << __FUNCTION__ << ": device not valid, not sending event";
    return;
  }
  navigator_vr_->EnqueueVREvent(VRDisplayEvent::Create(
      EventTypeNames::vrdisplaypresentchange, true, false, this, ""));
}

void VRDisplay::OnChanged(device::mojom::blink::VRDisplayInfoPtr display) {
  Update(display);
}

void VRDisplay::OnExitPresent() {
  StopPresenting();
}

void VRDisplay::OnConnected() {
  navigator_vr_->EnqueueVREvent(VRDisplayEvent::Create(
      EventTypeNames::vrdisplayconnect, true, false, this, "connect"));
}

void VRDisplay::OnDisconnected() {
  navigator_vr_->EnqueueVREvent(VRDisplayEvent::Create(
      EventTypeNames::vrdisplaydisconnect, true, false, this, "disconnect"));
}

void VRDisplay::StopPresenting() {
  if (is_presenting_) {
    if (!capabilities_->hasExternalDisplay()) {
      if (layer_.source().isHTMLCanvasElement()) {
        // TODO(klausw,crbug.com/698923): If compositor updates are
        // suppressed, restore them here.
      } else {
        // TODO(junov, crbug.com/695497): Implement for OffscreenCanvas
      }
    } else {
      // Can't get into this presentation mode, so nothing to do here.
    }
    is_presenting_ = false;

    OnPresentChange();

    // Record user action for stop presenting.  Note that this could be
    // user-triggered or not.
    Platform::Current()->RecordAction(
        UserMetricsAction("VR.WebVR.StopPresenting"));
  }

  rendering_context_ = nullptr;
  context_gl_ = nullptr;
  pending_submit_frame_ = false;
  pending_previous_frame_render_ = false;
}

void VRDisplay::OnActivate(device::mojom::blink::VRDisplayEventReason reason,
                           const OnActivateCallback& on_handled) {
  AutoReset<bool> activating(&in_display_activate_, true);
  navigator_vr_->DispatchVREvent(VRDisplayEvent::Create(
      EventTypeNames::vrdisplayactivate, true, false, this, reason));
  on_handled.Run(pending_present_request_);
}

void VRDisplay::OnDeactivate(
    device::mojom::blink::VRDisplayEventReason reason) {
  navigator_vr_->EnqueueVREvent(VRDisplayEvent::Create(
      EventTypeNames::vrdisplaydeactivate, true, false, this, reason));
}

void VRDisplay::ProcessScheduledWindowAnimations(double timestamp) {
  TRACE_EVENT1("gpu", "VRDisplay::window.rAF", "frame", vr_frame_id_);
  auto doc = navigator_vr_->GetDocument();
  if (!doc)
    return;
  auto page = doc->GetPage();
  if (!page)
    return;

  bool had_pending_vrdisplay_raf = pending_vrdisplay_raf_;
  // TODO(klausw): update timestamp based on scheduling delay?
  page->Animator().ServiceScriptedAnimations(timestamp);

  if (had_pending_vrdisplay_raf != pending_vrdisplay_raf_) {
    DVLOG(1) << __FUNCTION__
             << ": window.rAF fallback successfully scheduled VRDisplay.rAF";
  }

  if (!pending_vrdisplay_raf_) {
    // There wasn't any call to vrDisplay.rAF, so we will not be getting new
    // frames from now on unless the application schedules one down the road in
    // reaction to a separate event or timeout. TODO(klausw,crbug.com/716087):
    // do something more useful here?
    DVLOG(1) << __FUNCTION__
             << ": no scheduled VRDisplay.requestAnimationFrame, presentation "
                "broken?";
  }
}

void VRDisplay::ProcessScheduledAnimations(double timestamp) {
  DVLOG(2) << __FUNCTION__;
  // Check if we still have a valid context, the animation controller
  // or document may have disappeared since we scheduled this.
  Document* doc = this->GetDocument();
  if (!doc || display_blurred_) {
    DVLOG(2) << __FUNCTION__ << ": early exit, doc=" << doc
             << " display_blurred_=" << display_blurred_;
    return;
  }

  TRACE_EVENT1("gpu", "VRDisplay::OnVSync", "frame", vr_frame_id_);

  if (pending_vrdisplay_raf_ && scripted_animation_controller_) {
    // Run the callback, making sure that in_animation_frame_ is only
    // true for the vrDisplay rAF and not for a legacy window rAF
    // that may be called later.
    AutoReset<bool> animating(&in_animation_frame_, true);
    pending_vrdisplay_raf_ = false;
    scripted_animation_controller_->ServiceScriptedAnimations(timestamp);
  }

  // For GVR, we shut down normal vsync processing during VR presentation.
  // Trigger any callbacks on window.rAF manually so that they run after
  // completing the vrDisplay.rAF processing.
  if (is_presenting_ && !capabilities_->hasExternalDisplay()) {
    Platform::Current()->CurrentThread()->GetWebTaskRunner()->PostTask(
        BLINK_FROM_HERE, WTF::Bind(&VRDisplay::ProcessScheduledWindowAnimations,
                                   WrapWeakPersistent(this), timestamp));
  }
}

void VRDisplay::OnVSync(device::mojom::blink::VRPosePtr pose,
                        mojo::common::mojom::blink::TimeDeltaPtr time,
                        int16_t frame_id,
                        device::mojom::blink::VRVSyncProvider::Status error) {
  DVLOG(2) << __FUNCTION__;
  v_sync_connection_failed_ = false;
  switch (error) {
    case device::mojom::blink::VRVSyncProvider::Status::SUCCESS:
      break;
    case device::mojom::blink::VRVSyncProvider::Status::CLOSING:
      return;
  }
  pending_vsync_ = false;

  WTF::TimeDelta time_delta =
      WTF::TimeDelta::FromMicroseconds(time->microseconds);
  // Ensure a consistent timebase with document rAF.
  if (timebase_ < 0) {
    timebase_ = WTF::MonotonicallyIncreasingTime() - time_delta.InSecondsF();
  }

  frame_pose_ = std::move(pose);
  vr_frame_id_ = frame_id;

  // Post a task to handle scheduled animations after the current
  // execution context finishes, so that we yield to non-mojo tasks in
  // between frames. Executing mojo tasks back to back within the same
  // execution context caused extreme input delay due to processing
  // multiple frames without yielding, see crbug.com/701444. I suspect
  // this is due to WaitForIncomingMethodCall receiving the OnVSync
  // but queueing it for immediate execution since it doesn't match
  // the interface being waited on.
  Platform::Current()->CurrentThread()->GetWebTaskRunner()->PostTask(
      BLINK_FROM_HERE,
      WTF::Bind(&VRDisplay::ProcessScheduledAnimations,
                WrapWeakPersistent(this), timebase_ + time_delta.InSecondsF()));
}

void VRDisplay::ConnectVSyncProvider() {
  if (!FocusedOrPresenting() || vr_v_sync_provider_.is_bound())
    return;
  display_->GetVRVSyncProvider(mojo::MakeRequest(&vr_v_sync_provider_));
  vr_v_sync_provider_.set_connection_error_handler(ConvertToBaseCallback(
      WTF::Bind(&VRDisplay::OnVSyncConnectionError, WrapWeakPersistent(this))));
  if (pending_vrdisplay_raf_ && !display_blurred_) {
    pending_vsync_ = true;
    vr_v_sync_provider_->GetVSync(ConvertToBaseCallback(
        WTF::Bind(&VRDisplay::OnVSync, WrapWeakPersistent(this))));
  }
}

void VRDisplay::OnVSyncConnectionError() {
  vr_v_sync_provider_.reset();
  if (v_sync_connection_failed_)
    return;
  ConnectVSyncProvider();
  v_sync_connection_failed_ = true;
}

ScriptedAnimationController& VRDisplay::EnsureScriptedAnimationController(
    Document* doc) {
  if (!scripted_animation_controller_)
    scripted_animation_controller_ = ScriptedAnimationController::Create(doc);

  return *scripted_animation_controller_;
}

void VRDisplay::Dispose() {
  display_client_binding_.Close();
  vr_v_sync_provider_.reset();
}

ExecutionContext* VRDisplay::GetExecutionContext() const {
  return ContextLifecycleObserver::GetExecutionContext();
}

const AtomicString& VRDisplay::InterfaceName() const {
  return EventTargetNames::VRDisplay;
}

void VRDisplay::ContextDestroyed(ExecutionContext*) {
  ForceExitPresent();
  scripted_animation_controller_.Clear();
}

bool VRDisplay::HasPendingActivity() const {
  // Prevent V8 from garbage collecting the wrapper object if there are
  // event listeners attached to it.
  return GetExecutionContext() && HasEventListeners();
}

void VRDisplay::FocusChanged() {
  // TODO(mthiesse): Blur/focus the display.
  DVLOG(1) << __FUNCTION__;
  vr_v_sync_provider_.reset();
  ConnectVSyncProvider();
}

bool VRDisplay::FocusedOrPresenting() {
  // TODO(mthiesse, crbug.com/687411): Focused state should be determined
  // browser-side to correctly track which display should be receiving input.
  return navigator_vr_->IsFocused() || is_presenting_;
}

DEFINE_TRACE(VRDisplay) {
  EventTargetWithInlineData::Trace(visitor);
  ContextLifecycleObserver::Trace(visitor);
  visitor->Trace(navigator_vr_);
  visitor->Trace(capabilities_);
  visitor->Trace(stage_parameters_);
  visitor->Trace(eye_parameters_left_);
  visitor->Trace(eye_parameters_right_);
  visitor->Trace(layer_);
  visitor->Trace(rendering_context_);
  visitor->Trace(scripted_animation_controller_);
  visitor->Trace(pending_present_resolvers_);
}

}  // namespace blink
