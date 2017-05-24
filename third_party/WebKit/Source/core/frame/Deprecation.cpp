// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/frame/Deprecation.h"

#include "core/dom/Document.h"
#include "core/dom/ExecutionContext.h"
#include "core/frame/FrameConsole.h"
#include "core/frame/LocalFrame.h"
#include "core/inspector/ConsoleMessage.h"
#include "core/page/Page.h"
#include "core/workers/WorkerOrWorkletGlobalScope.h"

namespace {

enum Milestone {
  M59,
  M60,
  M61,
  M62,
};

const char* milestoneString(Milestone milestone) {
  // These are the Estimated Stable Dates:
  // https://www.chromium.org/developers/calendar

  switch (milestone) {
    case M59:
      return "M59, around June 2017";
    case M60:
      return "M60, around August 2017";
    case M61:
      return "M61, around September 2017";
    case M62:
      return "M62, around October 2017";
  }

  NOTREACHED();
  return nullptr;
}

String replacedBy(const char* feature, const char* replacement) {
  return String::Format("%s is deprecated. Please use %s instead.", feature,
                        replacement);
}

String willBeRemoved(const char* feature,
                     Milestone milestone,
                     const char* details) {
  return String::Format(
      "%s is deprecated and will be removed in %s. See "
      "https://www.chromestatus.com/features/%s for more details.",
      feature, milestoneString(milestone), details);
}

String replacedWillBeRemoved(const char* feature,
                             const char* replacement,
                             Milestone milestone,
                             const char* details) {
  return String::Format(
      "%s is deprecated and will be removed in %s. Please use %s instead. See "
      "https://www.chromestatus.com/features/%s for more details.",
      feature, milestoneString(milestone), replacement, details);
}

}  // anonymous namespace

namespace blink {

Deprecation::Deprecation() : mute_count_(0) {
  css_property_deprecation_bits_.EnsureSize(numCSSPropertyIDs);
}

Deprecation::~Deprecation() {}

void Deprecation::ClearSuppression() {
  css_property_deprecation_bits_.ClearAll();
}

void Deprecation::MuteForInspector() {
  mute_count_++;
}

void Deprecation::UnmuteForInspector() {
  mute_count_--;
}

void Deprecation::Suppress(CSSPropertyID unresolved_property) {
  DCHECK(isCSSPropertyIDWithName(unresolved_property));
  css_property_deprecation_bits_.QuickSet(unresolved_property);
}

bool Deprecation::IsSuppressed(CSSPropertyID unresolved_property) {
  DCHECK(isCSSPropertyIDWithName(unresolved_property));
  return css_property_deprecation_bits_.QuickGet(unresolved_property);
}

void Deprecation::WarnOnDeprecatedProperties(
    const LocalFrame* frame,
    CSSPropertyID unresolved_property) {
  Page* page = frame ? frame->GetPage() : nullptr;
  if (!page || page->GetDeprecation().mute_count_ ||
      page->GetDeprecation().IsSuppressed(unresolved_property))
    return;

  String message = DeprecationMessage(unresolved_property);
  if (!message.IsEmpty()) {
    page->GetDeprecation().Suppress(unresolved_property);
    ConsoleMessage* console_message = ConsoleMessage::Create(
        kDeprecationMessageSource, kWarningMessageLevel, message);
    frame->Console().AddMessage(console_message);
  }
}

String Deprecation::DeprecationMessage(CSSPropertyID unresolved_property) {
  // TODO: Add a switch here when there are properties that we intend to
  // deprecate.
  // Returning an empty string for now.
  return g_empty_string;
}

void Deprecation::CountDeprecation(const LocalFrame* frame,
                                   UseCounter::Feature feature) {
  if (!frame)
    return;
  Page* page = frame->GetPage();
  if (!page || page->GetDeprecation().mute_count_)
    return;

  if (!page->GetUseCounter().HasRecordedMeasurement(feature)) {
    page->GetUseCounter().RecordMeasurement(feature);
    DCHECK(!DeprecationMessage(feature).IsEmpty());
    ConsoleMessage* console_message =
        ConsoleMessage::Create(kDeprecationMessageSource, kWarningMessageLevel,
                               DeprecationMessage(feature));
    frame->Console().AddMessage(console_message);
  }
}

void Deprecation::CountDeprecation(ExecutionContext* context,
                                   UseCounter::Feature feature) {
  if (!context)
    return;
  if (context->IsDocument()) {
    Deprecation::CountDeprecation(*ToDocument(context), feature);
    return;
  }
  if (context->IsWorkerOrWorkletGlobalScope())
    ToWorkerOrWorkletGlobalScope(context)->CountDeprecation(feature);
}

void Deprecation::CountDeprecation(const Document& document,
                                   UseCounter::Feature feature) {
  Deprecation::CountDeprecation(document.GetFrame(), feature);
}

void Deprecation::CountDeprecationCrossOriginIframe(
    const LocalFrame* frame,
    UseCounter::Feature feature) {
  // Check to see if the frame can script into the top level document.
  SecurityOrigin* security_origin =
      frame->GetSecurityContext()->GetSecurityOrigin();
  Frame& top = frame->Tree().Top();
  if (!security_origin->CanAccess(
          top.GetSecurityContext()->GetSecurityOrigin()))
    CountDeprecation(frame, feature);
}

void Deprecation::CountDeprecationCrossOriginIframe(
    const Document& document,
    UseCounter::Feature feature) {
  LocalFrame* frame = document.GetFrame();
  if (!frame)
    return;
  CountDeprecationCrossOriginIframe(frame, feature);
}

String Deprecation::DeprecationMessage(UseCounter::Feature feature) {
  switch (feature) {
    // Quota
    case UseCounter::kPrefixedStorageInfo:
      return replacedBy("'window.webkitStorageInfo'",
                        "'navigator.webkitTemporaryStorage' or "
                        "'navigator.webkitPersistentStorage'");

    case UseCounter::kConsoleMarkTimeline:
      return replacedBy("'console.markTimeline'", "'console.timeStamp'");

    case UseCounter::kPrefixedVideoSupportsFullscreen:
      return replacedBy("'HTMLVideoElement.webkitSupportsFullscreen'",
                        "'Document.fullscreenEnabled'");

    case UseCounter::kPrefixedVideoDisplayingFullscreen:
      return replacedBy("'HTMLVideoElement.webkitDisplayingFullscreen'",
                        "'Document.fullscreenElement'");

    case UseCounter::kPrefixedVideoEnterFullscreen:
      return replacedBy("'HTMLVideoElement.webkitEnterFullscreen()'",
                        "'Element.requestFullscreen()'");

    case UseCounter::kPrefixedVideoExitFullscreen:
      return replacedBy("'HTMLVideoElement.webkitExitFullscreen()'",
                        "'Document.exitFullscreen()'");

    case UseCounter::kPrefixedVideoEnterFullScreen:
      return replacedBy("'HTMLVideoElement.webkitEnterFullScreen()'",
                        "'Element.requestFullscreen()'");

    case UseCounter::kPrefixedVideoExitFullScreen:
      return replacedBy("'HTMLVideoElement.webkitExitFullScreen()'",
                        "'Document.exitFullscreen()'");

    case UseCounter::kPrefixedRequestAnimationFrame:
      return "'webkitRequestAnimationFrame' is vendor-specific. Please use the "
             "standard 'requestAnimationFrame' instead.";

    case UseCounter::kPrefixedCancelAnimationFrame:
      return "'webkitCancelAnimationFrame' is vendor-specific. Please use the "
             "standard 'cancelAnimationFrame' instead.";

    case UseCounter::kPictureSourceSrc:
      return "<source src> with a <picture> parent is invalid and therefore "
             "ignored. Please use <source srcset> instead.";

    case UseCounter::kConsoleTimeline:
      return replacedBy("'console.timeline'", "'console.time'");

    case UseCounter::kConsoleTimelineEnd:
      return replacedBy("'console.timelineEnd'", "'console.timeEnd'");

    case UseCounter::kXMLHttpRequestSynchronousInNonWorkerOutsideBeforeUnload:
      return "Synchronous XMLHttpRequest on the main thread is deprecated "
             "because of its detrimental effects to the end user's experience. "
             "For more help, check https://xhr.spec.whatwg.org/.";

    case UseCounter::kGetMatchedCSSRules:
      return "'getMatchedCSSRules()' is deprecated. For more help, check "
             "https://code.google.com/p/chromium/issues/detail?id=437569#c2";

    case UseCounter::kPrefixedWindowURL:
      return replacedBy("'webkitURL'", "'URL'");

    case UseCounter::kRangeExpand:
      return replacedBy("'Range.expand()'", "'Selection.modify()'");

    // Blocked subresource requests:
    case UseCounter::kLegacyProtocolEmbeddedAsSubresource:
      return String::Format(
          "Subresource requests using legacy protocols (like `ftp:`) are "
          "are blocked. Please deliver web-accessible resources over modern "
          "protocols like HTTPS. See "
          "https://www.chromestatus.com/feature/5709390967472128 for details.");

    case UseCounter::kRequestedSubresourceWithEmbeddedCredentials:
      return "Subresource requests whose URLs contain embedded credentials "
             "(e.g. `https://user:pass@host/`) are blocked. See "
             "https://www.chromestatus.com/feature/5669008342777856 for more "
             "details.";

    // Powerful features on insecure origins (https://goo.gl/rStTGz)
    case UseCounter::kDeviceMotionInsecureOrigin:
      return "The devicemotion event is deprecated on insecure origins, and "
             "support will be removed in the future. You should consider "
             "switching your application to a secure origin, such as HTTPS. "
             "See https://goo.gl/rStTGz for more details.";

    case UseCounter::kDeviceOrientationInsecureOrigin:
      return "The deviceorientation event is deprecated on insecure origins, "
             "and support will be removed in the future. You should consider "
             "switching your application to a secure origin, such as HTTPS. "
             "See https://goo.gl/rStTGz for more details.";

    case UseCounter::kDeviceOrientationAbsoluteInsecureOrigin:
      return "The deviceorientationabsolute event is deprecated on insecure "
             "origins, and support will be removed in the future. You should "
             "consider switching your application to a secure origin, such as "
             "HTTPS. See https://goo.gl/rStTGz for more details.";

    case UseCounter::kGeolocationInsecureOrigin:
    case UseCounter::kGeolocationInsecureOriginIframe:
      return "getCurrentPosition() and watchPosition() no longer work on "
             "insecure origins. To use this feature, you should consider "
             "switching your application to a secure origin, such as HTTPS. "
             "See https://goo.gl/rStTGz for more details.";

    case UseCounter::kGeolocationInsecureOriginDeprecatedNotRemoved:
    case UseCounter::kGeolocationInsecureOriginIframeDeprecatedNotRemoved:
      return "getCurrentPosition() and watchPosition() are deprecated on "
             "insecure origins. To use this feature, you should consider "
             "switching your application to a secure origin, such as HTTPS. "
             "See https://goo.gl/rStTGz for more details.";

    case UseCounter::kGetUserMediaInsecureOrigin:
    case UseCounter::kGetUserMediaInsecureOriginIframe:
      return "getUserMedia() no longer works on insecure origins. To use this "
             "feature, you should consider switching your application to a "
             "secure origin, such as HTTPS. See https://goo.gl/rStTGz for more "
             "details.";

    case UseCounter::kMediaSourceAbortRemove:
      return "Using SourceBuffer.abort() to abort remove()'s asynchronous "
             "range removal is deprecated due to specification change. Support "
             "will be removed in the future. You should instead await "
             "'updateend'. abort() is intended to only abort an asynchronous "
             "media append or reset parser state. See "
             "https://www.chromestatus.com/features/6107495151960064 for more "
             "details.";
    case UseCounter::kMediaSourceDurationTruncatingBuffered:
      return "Setting MediaSource.duration below the highest presentation "
             "timestamp of any buffered coded frames is deprecated due to "
             "specification change. Support for implicit removal of truncated "
             "buffered media will be removed in the future. You should instead "
             "perform explicit remove(newDuration, oldDuration) on all "
             "sourceBuffers, where newDuration < oldDuration. See "
             "https://www.chromestatus.com/features/6107495151960064 for more "
             "details.";

    case UseCounter::kApplicationCacheManifestSelectInsecureOrigin:
    case UseCounter::kApplicationCacheAPIInsecureOrigin:
      return "Use of the Application Cache is deprecated on insecure origins. "
             "Support will be removed in the future. You should consider "
             "switching your application to a secure origin, such as HTTPS. "
             "See https://goo.gl/rStTGz for more details.";

    case UseCounter::kNotificationInsecureOrigin:
    case UseCounter::kNotificationAPIInsecureOriginIframe:
    case UseCounter::kNotificationPermissionRequestedInsecureOrigin:
      return String::Format(
          "Using the Notification API on insecure origins is "
          "deprecated and will be removed in %s. You should consider "
          "switching your application to a secure origin, such as HTTPS. See "
          "https://goo.gl/rStTGz for more details.",
          milestoneString(M61));

    case UseCounter::kNotificationPermissionRequestedIframe:
      return String::Format(
          "Using the Notification API from an iframe is deprecated and will "
          "be removed in %s. You should consider requesting permission from "
          "the top-level frame or opening a new window instead. See "
          "https://www.chromestatus.com/feature/6451284559265792 for more "
          "details.",
          milestoneString(M61));

    case UseCounter::kElementCreateShadowRootMultiple:
      return "Calling Element.createShadowRoot() for an element which already "
             "hosts a shadow root is deprecated. See "
             "https://www.chromestatus.com/features/4668884095336448 for more "
             "details.";

    case UseCounter::kCSSDeepCombinator:
      return "/deep/ combinator is no longer supported in CSS dynamic profile. "
             "It is now effectively no-op, acting as if it were a descendant "
             "combinator. You should consider to remove it. See "
             "https://www.chromestatus.com/features/4964279606312960 for more "
             "details.";

    case UseCounter::kVREyeParametersOffset:
      return replacedBy("VREyeParameters.offset",
                        "view matrices provided by VRFrameData");

    case UseCounter::kCSSSelectorInternalMediaControlsOverlayCastButton:
      return willBeRemoved(
          "-internal-media-controls-overlay-cast-button selector", M61,
          "5714245488476160");

    case UseCounter::kSelectionAddRangeIntersect:
      return "The behavior that Selection.addRange() merges existing Range and "
             "the specified Range was removed. See "
             "https://www.chromestatus.com/features/6680566019653632 for more "
             "details.";

    case UseCounter::kRtcpMuxPolicyNegotiate:
      return String::Format(
          "The rtcpMuxPolicy option is being considered for "
          "removal and may be removed no earlier than %s. If you depend on it, "
          "please see https://www.chromestatus.com/features/5654810086866944 "
          "for more details.",
          milestoneString(M62));

    case UseCounter::kVibrateWithoutUserGesture:
      return willBeRemoved(
          "A call to navigator.vibrate without user tap on the frame or any "
          "embedded frame",
          M60, "5644273861001216");

    case UseCounter::kChildSrcAllowedWorkerThatScriptSrcBlocked:
      return replacedWillBeRemoved("The 'child-src' directive",
                                   "the 'script-src' directive for Workers",
                                   M60, "5922594955984896");

    case UseCounter::kCanRequestURLHTTPContainingNewline:
      return String::Format(
          "Resource requests whose URLs contain raw newline characters are "
          "deprecated, and may be blocked in %s. Please remove newlines from "
          "places like element attribute values in order to continue loading "
          "those resources. See "
          "https://www.chromestatus.com/features/5735596811091968 for more "
          "details.",
          milestoneString(M60));

    case UseCounter::kV8RTCPeerConnection_GetStreamById_Method:
      return willBeRemoved("RTCPeerConnection.getStreamById()", M62,
                           "5751819573657600");

    case UseCounter::kV8SVGPathElement_GetPathSegAtLength_Method:
      return willBeRemoved("SVGPathElement.getPathSegAtLength", M62,
                           "5638783282184192");

    case UseCounter::kCredentialManagerCredentialRequestOptionsUnmediated:
      return replacedWillBeRemoved(
          "The boolean flag CredentialRequestOptions.unmediated",
          "the CredentialRequestOptions.mediation enum", M62,
          "6076479909658624");

    case UseCounter::kCredentialManagerIdName:
    case UseCounter::kCredentialManagerPasswordName:
    case UseCounter::kCredentialManagerAdditionalData:
    case UseCounter::kCredentialManagerCustomFetch:
      return String::Format(
          "Passing 'PasswordCredential' objects into 'fetch(..., { "
          "credentials: ... })' is deprecated, and will be removed in %s. See "
          "https://www.chromestatus.com/features/5689327799500800 for more "
          "details and https://www.chromium.org/developers/"
          "recent-changes-credential-management-api for migration suggestions.",
          milestoneString(M62));

    // Features that aren't deprecated don't have a deprecation message.
    default:
      return String();
  }
}

}  // namespace blink
