// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/flag_descriptions.h"

namespace flag_descriptions {

//  Material Design version of chrome://bookmarks

const char kEnableMaterialDesignBookmarksName[] =
    "Enable Material Design bookmarks";

const char kEnableMaterialDesignBookmarksDescription[] =
    "If enabled, the chrome://bookmarks/ URL loads the Material Design "
    "bookmarks page.";

//  Material Design version of chrome://policy

const char kEnableMaterialDesignPolicyPageName[] =
    "Enable Material Design policy page";

const char kEnableMaterialDesignPolicyPageDescription[] =
    "If enabled, the chrome://md-policy URL loads the Material Design "
    "policy page.";

//  Material Design version of chrome://settings

const char kEnableMaterialDesignSettingsName[] =
    "Enable Material Design settings";

const char kEnableMaterialDesignSettingsDescription[] =
    "If enabled, the chrome://settings/ URL loads the Material Design "
    "settings page.";

//  Material Design version of chrome://extensions

const char kEnableMaterialDesignExtensionsName[] =
    "Enable Material Design extensions";

const char kEnableMaterialDesignExtensionsDescription[] =
    "If enabled, the chrome://extensions/ URL loads the Material Design "
    "extensions page.";

//  Material Design version of feedback form

const char kEnableMaterialDesignFeedbackName[] =
    "Enable Material Design feedback";

const char kEnableMaterialDesignFeedbackDescription[] =
    "If enabled, reporting an issue will load the Material Design feedback "
    "UI.";

const char kContextualSuggestionsCarouselName[] =
    "Enable Contextual Suggestions";

const char kContextualSuggestionsCarouselDescription[] =
    "If enabled, shows contextual suggestions in a horizontal carousel in "
    "bottom sheet content.";

//  Report URL to SafeSearch

const char kSafeSearchUrlReportingName[] = "SafeSearch URLs reporting.";

const char kSafeSearchUrlReportingDescription[] =
    "If enabled, inappropriate URLs can be reported back to SafeSearch.";

//  Device scale factor change in content crbug.com/485650.

const char kEnableUseZoomForDsfName[] =
    "Use Blink's zoom for device scale factor.";

const char kEnableUseZoomForDsfDescription[] =
    "If enabled, Blink uses its zooming mechanism to scale content for "
    "device scale factor.";

const char kEnableUseZoomForDsfChoiceDefault[] = "Default";

const char kEnableUseZoomForDsfChoiceEnabled[] = "Enabled";

const char kEnableUseZoomForDsfChoiceDisabled[] = "Disabled";

const char kNostatePrefetchName[] = "No-State Prefetch";

const char kNostatePrefetchDescription[] =
    R"*("No-State Prefetch" pre-downloads resources to improve load )*"
    R"*(times. "Prerender" does a full pre-rendering of the page, to )*"
    R"*(improve load times even more. "Simple Load" does nothing and is )*"
    R"*(similar to disabling the feature, but collects more metrics for )*"
    R"*(comparison purposes.)*";

const char kSpeculativePrefetchName[] = "Speculative Prefetch";

const char kSpeculativePrefetchDescription[] =
    R"*("Speculative Prefetch" fetches likely resources early to improve )*"
    R"*(load times, based on a local database (see chrome://predictors). )*"
    R"*("Learning" means that only the database construction is enabled, )*"
    R"*("Prefetching" that learning and prefetching are enabled.)*";

const char kOffMainThreadFetchName[] = "Off-main-thread fetch for Web Workers";

const char kOffMainThreadFetchDescription[] =
    "If enabled, the resource fetches from worker threads will not be blocked "
    "by the busy main thread.";

//  Force Tablet Mode

const char kForceTabletModeName[] = "Force Tablet Mode";

const char kForceTabletModeDescription[] =
    R"*(This flag can be used to force a certain mode on to a chromebook, )*"
    R"*(despite its current orientation. "TouchView" means that the )*"
    R"*(chromebook will act as if it were in touch view mode. "Clamshell" )*"
    R"*(means that the chromebook will act as if it were in clamshell )*"
    R"*(mode . "Auto" means that the chromebook will alternate between )*"
    R"*(the two, based on its orientation.)*";

const char kForceTabletModeTouchview[] = "TouchView";

const char kForceTabletModeClamshell[] = "Clamshell";

const char kForceTabletModeAuto[] = "Auto (default)";

//  Print Preview features
#if !defined(OS_WIN) && !defined(OS_MACOSX)
const char kPrintPdfAsImageName[] = "Print Pdf as Image";

const char kPrintPdfAsImageDescription[] =
    "If enabled, an option to print PDF files as images will be available "
    "in print preview.";
#endif

#if !defined(DISABLE_NACL)

const char kNaclName[] = "Native Client";
const char kNaclDescription[] =
    "Support Native Client for all web applications, even those that were "
    "not installed from the Chrome Web Store.";

const char kNaclDebugName[] = "Native Client GDB-based debugging";
const char kNaclDebugDescription[] =
    "Enable GDB debug stub. This will stop a Native Client application on "
    "startup and wait for nacl-gdb (from the NaCl SDK) to attach to it.";

const char kPnaclSubzeroName[] = "Force PNaCl Subzero";
const char kPnaclSubzeroDescription[] =
    "Force the use of PNaCl's fast Subzero translator for all pexe files.";

const char kNaclDebugMaskName[] =
    "Restrict Native Client GDB-based debugging by pattern";
const char kNaclDebugMaskDescription[] =
    "Restricts Native Client application GDB-based debugging by URL of "
    "manifest file. Native Client GDB-based debugging must be enabled for "
    "this option to work.";
const char kNaclDebugMaskChoiceDebugAll[] = "Debug everything.";
const char kNaclDebugMaskChoiceExcludeUtilsPnacl[] =
    "Debug everything except secure shell and the PNaCl translator.";
const char kNaclDebugMaskChoiceIncludeDebug[] =
    "Debug only if manifest URL ends with debug.nmf.";

#endif

const char kEnableHttpFormWarningName[] =
    "Show in-form warnings for sensitive fields when the top-level page is "
    "not HTTPS";

const char kEnableHttpFormWarningDescription[] =
    "Attaches a warning UI to any password or credit card fields detected "
    "when the top-level page is not HTTPS";

const char kMarkHttpAsName[] = "Mark non-secure origins as non-secure";

const char kMarkHttpAsDescription[] = "Change the UI treatment for HTTP pages";

const char kMarkHttpAsDangerous[] = "Always mark HTTP as actively dangerous";

//  Material design of the Incognito NTP.

extern const char kMaterialDesignIncognitoNTPName[] =
    "Material Design Incognito NTP.";

extern const char kMaterialDesignIncognitoNTPDescription[] =
    "If enabled, the Incognito New Tab page uses the new material design "
    "with a better readable text.";

const char kSavePageAsMhtmlName[] = "Save Page as MHTML";

const char kSavePageAsMhtmlDescription[] =
    "Enables saving pages as MHTML: a single text file containing HTML and "
    "all sub-resources.";

//  Flag and values for MHTML Geenrator options lab.

const char kMhtmlGeneratorOptionName[] = "MHTML Generation Option";

const char kMhtmlGeneratorOptionDescription[] =
    "Provides experimental options for MHTML file generator.";

const char kMhtmlSkipNostoreMain[] = "Skips no-store main frame.";

const char kMhtmlSkipNostoreAll[] = "Skips all no-store resources.";

const char kDeviceDiscoveryNotificationsName[] =
    "Device Discovery Notifications";

const char kDeviceDiscoveryNotificationsDescription[] =
    "Device discovery notifications on local network.";

#if defined(OS_WIN)

const char kCloudPrintXpsName[] = "XPS in Google Cloud Print";

const char kCloudPrintXpsDescription[] =
    "XPS enables advanced options for classic printers connected to the "
    "Cloud Print with Chrome. Printers must be re-connected after changing "
    "this flag.";

#endif  // defined(OS_WIN)

const char kLoadMediaRouterComponentExtensionName[] =
    "Load Media Router Component Extension";

const char kLoadMediaRouterComponentExtensionDescription[] =
    "Loads the Media Router component extension at startup.";

const char kPrintPreviewRegisterPromosName[] =
    "Print Preview Registration Promos";

const char kPrintPreviewRegisterPromosDescription[] =
    "Enable registering unregistered cloud printers from print preview.";

const char kScrollPredictionName[] = "Scroll prediction";

const char kTopChromeMd[] = "UI Layout for the browser's top chrome";

const char kTopChromeMdDescription[] =
    R"*(Toggles between normal and touch (formerly "hybrid") layouts.)*";

const char kTopChromeMdMaterial[] = "Normal";

const char kTopChromeMdMaterialHybrid[] = "Touch";

const char kSiteDetails[] = "Site Details";

const char kSiteDetailsDescription[] =
    "Adds UI in MD Settings to view all content settings for a specific "
    "origin.";

const char kSiteSettings[] = "Site settings with All sites and Site details";

const char kSiteSettingsDescription[] =
    "Adds new ways of viewing Site settings.";

const char kSecondaryUiMd[] =
    "Material Design in the rest of the browser's native UI";

const char kSecondaryUiMdDescription[] =
    "Extends the --top-chrome-md setting to secondary UI (bubbles, dialogs, "
    "etc.). On Mac, this enables MacViews, which uses toolkit-views for "
    "native browser dialogs.";

const char kScrollPredictionDescription[] =
    "Predicts the finger's future position during scrolls allowing time to "
    "render the frame before the finger is there.";

const char kAddToShelfName[] = "Add to shelf";

const char kAddToShelfDescription[] =
    "Enable the display of add to shelf banners, which prompt a user to add "
    "a web app to their shelf, or other platform-specific equivalent.";

const char kBypassAppBannerEngagementChecksName[] =
    "Bypass user engagement checks";

const char kBypassAppBannerEngagementChecksDescription[] =
    "Bypasses user engagement checks for displaying app banners, such as "
    "requiring that users have visited the site before and that the banner "
    "hasn't been shown recently. This allows developers to test that other "
    "eligibility requirements for showing app banners, such as having a "
    "manifest, are met.";

#if defined(OS_ANDROID)

const char kAccessibilityTabSwitcherName[] = "Accessibility Tab Switcher";

const char kAccessibilityTabSwitcherDescription[] =
    "Enable the accessibility tab switcher for Android.";

const char kAndroidAutofillAccessibilityName[] = "Autofill Accessibility";

const char kAndroidAutofillAccessibilityDescription[] =
    "Enable accessibility for autofill popup.";

const char kEnablePhysicalWebName[] = "Enable the Physical Web.";

const char kEnablePhysicalWebDescription[] =
    "Enable scanning for URLs from Physical Web objects.";

#endif  // defined(OS_ANDROID)

const char kTouchEventsName[] = "Touch Events API";

const char kTouchEventsDescription[] =
    "Force Touch Events API feature detection to always be enabled or "
    "disabled, or to be enabled when a touchscreen is detected on startup "
    "(Automatic, the default).";

const char kTouchAdjustmentName[] = "Touch adjustment";

const char kTouchAdjustmentDescription[] =
    "Refine the position of a touch gesture in order to compensate for "
    "touches having poor resolution compared to a mouse.";

const char kCompositedLayerBordersName[] = "Composited render layer borders";

const char kCompositedLayerBordersDescription[] =
    "Renders a border around composited Render Layers to help debug and "
    "study layer compositing.";

const char kGlCompositedTextureQuadBordersName[] =
    "GL composited texture quad borders";

const char kGlCompositedTextureQuadBordersDescription[] =
    "Renders a border around GL composited texture quads to help debug and "
    "study overlay support.";

const char kShowOverdrawFeedbackName[] = "Show overdraw feedback";

const char kShowOverdrawFeedbackDescription[] =
    "Visualize overdraw by color-coding elements based on if they have "
    "other elements drawn underneath.";

const char kUiPartialSwapName[] = "Partial swap";

const char kUiPartialSwapDescription[] = "Sets partial swap behavior.";

const char kDebugShortcutsName[] = "Debugging keyboard shortcuts";

const char kIgnoreGpuBlacklistName[] = "Override software rendering list";

const char kIgnoreGpuBlacklistDescription[] =
    "Overrides the built-in software rendering list and enables "
    "GPU-acceleration on unsupported system configurations.";

const char kInertVisualViewportName[] = "Inert visual viewport.";

const char kInertVisualViewportDescription[] =
    "Experiment to have all APIs reflect the layout viewport. This will "
    "make window.scroll properties relative to the layout viewport.";

const char kColorCorrectRenderingName[] = "Color correct rendering";

const char kColorCorrectRenderingDescription[] =
    "Enables color correct rendering of web content.";

const char kExperimentalCanvasFeaturesName[] = "Experimental canvas features";

const char kExperimentalCanvasFeaturesDescription[] =
    "Enables the use of experimental canvas features which are still in "
    "development.";

const char kAccelerated2dCanvasName[] = "Accelerated 2D canvas";

const char kAccelerated2dCanvasDescription[] =
    "Enables the use of the GPU to perform 2d canvas rendering instead of "
    "using software rendering.";

const char kDisplayList2dCanvasName[] = "Display list 2D canvas";

const char kDisplayList2dCanvasDescription[] =
    "Enables the use of display lists to record 2D canvas commands. This "
    "allows 2D canvas rasterization to be performed on separate thread.";

const char kEnable2dCanvasDynamicRenderingModeSwitchingName[] =
    "Enable 2D canvas dynamic rendering mode switching.";

const char kEnable2dCanvasDynamicRenderingModeSwitchingDescription[] =
    "There are multiple implementations of the graphics rendering pipeline "
    "for the 2D canvas. These different implementations have different "
    "performance characteristics. Turning on this flag allows canvas 2D "
    "contexts to switch between these implementations on the fly based on "
    "how the canvas is used in order to increase performance. For example, "
    "going from an implementation that uses the GPU to one that doesn't.";

const char kExperimentalExtensionApisName[] = "Experimental Extension APIs";

const char kExperimentalExtensionApisDescription[] =
    "Enables experimental extension APIs. Note that the extension gallery "
    "doesn't allow you to upload extensions that use experimental APIs.";

const char kExtensionsOnChromeUrlsName[] = "Extensions on chrome:// URLs";

const char kExtensionsOnChromeUrlsDescription[] =
    "Enables running extensions on chrome:// URLs, where extensions "
    "explicitly request this permission.";

const char kFastUnloadName[] = "Fast tab/window close";

const char kFastUnloadDescription[] =
    "Enables fast tab/window closing - runs a tab's onunload js handler "
    "independently of the GUI.";

const char kUserConsentForExtensionScriptsName[] =
    "User consent for extension scripts";

const char kUserConsentForExtensionScriptsDescription[] =
    "Require user consent for an extension running a script on the page, if "
    "the extension requested permission to run on all urls.";

const char kHistoryRequiresUserGestureName[] =
    "New history entries require a user gesture.";

const char kHistoryRequiresUserGestureDescription[] =
    "Require a user gesture to add a history entry.";

const char kHyperlinkAuditingName[] = "Hyperlink auditing";

const char kHyperlinkAuditingDescription[] = "Sends hyperlink auditing pings.";

#if defined(OS_ANDROID)

const char kContextualSearchName[] = "Contextual Search";

const char kContextualSearchDescription[] =
    "Whether or not Contextual Search is enabled.";

const char kContextualSearchContextualCardsBarIntegration[] =
    "Contextual Search - Contextual Cards Integration";

const char kContextualSearchContextualCardsBarIntegrationDescription[] =
    "Whether or not integration of Contextual Cards data in the Contextual "
    "Search Bar is enabled.";

const char kContextualSearchSingleActionsName[] =
    "Contextual Search - Single Actions";

const char kContextualSearchSingleActionsDescription[] =
    "Whether or not single actions using Contextual Cards data in the "
    "Contextual Search Bar is enabled.";

const char kContextualSearchUrlActionsName[] =
    "Contextual Search - URL Actions";

const char kContextualSearchUrlActionsDescription[] =
    "Whether or not URL actions using Contextual Cards data in the "
    "Contextual Search Bar is enabled.";

#endif  // defined(OS_ANDROID)

const char kSmoothScrollingName[] = "Smooth Scrolling";

const char kSmoothScrollingDescription[] =
    "Animate smoothly when scrolling page content.";

const char kOverlayScrollbarsName[] = "Overlay Scrollbars";

const char kOverlayScrollbarsDescription[] =
    "Enable the experimental overlay scrollbars implementation. You must "
    "also enable threaded compositing to have the scrollbars animate.";

const char kShowAutofillTypePredictionsName[] = "Show Autofill predictions";

const char kShowAutofillTypePredictionsDescription[] =
    "Annotates web forms with Autofill field type predictions as "
    "placeholder text.";

const char kTcpFastOpenName[] = "TCP Fast Open";

const char kTcpFastOpenDescription[] =
    "Enable the option to send extra authentication information in the "
    "initial SYN packet for a previously connected client, allowing faster "
    "data send start.";

const char kTouchDragDropName[] = "Touch initiated drag and drop";

const char kTouchDragDropDescription[] =
    "Touch drag and drop can be initiated through long press on a draggable "
    "element.";

const char kTouchSelectionStrategyName[] = "Touch text selection strategy";

const char kTouchSelectionStrategyDescription[] =
    "Controls how text selection granularity changes when touch text "
    "selection handles are dragged. Non-default behavior is experimental.";

const char kTouchSelectionStrategyCharacter[] = "Character";

const char kTouchSelectionStrategyDirection[] = "Direction";

const char kWalletServiceUseSandboxName[] =
    "Use Google Payments sandbox servers";

const char kWalletServiceUseSandboxDescription[] =
    "For developers: use the sandbox service for Google Payments API "
    "calls.";

const char kOverscrollHistoryNavigationName[] = "Overscroll history navigation";

const char kOverscrollHistoryNavigationDescription[] =
    "Experimental history navigation in response to horizontal overscroll.";

const char kOverscrollHistoryNavigationSimpleUi[] = "Simple";

const char kOverscrollStartThresholdName[] = "Overscroll start threshold";

const char kOverscrollStartThresholdDescription[] =
    "Changes overscroll start threshold relative to the default value.";

const char kOverscrollStartThreshold133Percent[] = "133%";

const char kOverscrollStartThreshold166Percent[] = "166%";

const char kOverscrollStartThreshold200Percent[] = "200%";

const char kScrollEndEffectName[] = "Scroll end effect";

const char kScrollEndEffectDescription[] =
    "Experimental scroll end effect in response to vertical overscroll.";

const char kWebgl2Name[] = "WebGL 2.0";

const char kWebgl2Description[] = "Allow web applications to access WebGL 2.0.";

const char kWebglDraftExtensionsName[] = "WebGL Draft Extensions";

const char kWebglDraftExtensionsDescription[] =
    "Enabling this option allows web applications to access the WebGL "
    "Extensions that are still in draft status.";

const char kWebrtcHwDecodingName[] = "WebRTC hardware video decoding";

const char kWebrtcHwDecodingDescription[] =
    "Support in WebRTC for decoding video streams using platform hardware.";

const char kWebrtcHwEncodingName[] = "WebRTC hardware video encoding";

const char kWebrtcHwEncodingDescription[] =
    "Support in WebRTC for encoding video streams using platform hardware.";

const char kWebrtcHwH264EncodingName[] = "WebRTC hardware h264 video encoding";

const char kWebrtcHwH264EncodingDescription[] =
    "Support in WebRTC for encoding h264 video streams using platform "
    "hardware.";

const char kWebrtcHwVP8EncodingName[] = "WebRTC hardware vp8 video encoding";

const char kWebrtcHwVP8EncodingDescription[] =
    "Support in WebRTC for encoding vp8 video streams using platform "
    "hardware.";

const char kWebrtcSrtpAesGcmName[] =
    "Negotiation with GCM cipher suites for SRTP in WebRTC";

const char kWebrtcSrtpAesGcmDescription[] =
    "When enabled, WebRTC will try to negotiate GCM cipher suites for "
    "SRTP.";

const char kWebrtcStunOriginName[] = "WebRTC Stun origin header";

const char kWebrtcStunOriginDescription[] =
    "When enabled, Stun messages generated by WebRTC will contain the "
    "Origin header.";

const char kWebrtcEchoCanceller3Name[] = "WebRTC Echo Canceller 3.";

const char kWebrtcEchoCanceller3Description[] =
    "Experimental WebRTC echo canceller (AEC3).";

#if defined(OS_ANDROID)

const char kMediaScreenCaptureName[] = "Experimental ScreenCapture.";

const char kMediaScreenCaptureDescription[] =
    "Enable this option for experimental ScreenCapture feature on Android.";

#endif  // defined(OS_ANDROID)

#if BUILDFLAG(ENABLE_WEBRTC)

const char kWebrtcH264WithOpenh264FfmpegName[] =
    "WebRTC H.264 software video encoder/decoder";

const char kWebrtcH264WithOpenh264FfmpegDescription[] =
    "When enabled, an H.264 software video encoder/decoder pair is "
    "included. If a hardware encoder/decoder is also available it may be "
    "used instead of this encoder/decoder.";

#endif  // BUILDFLAG(ENABLE_WEBRTC)

const char kWebvrName[] = "WebVR";

const char kWebvrDescription[] =
    "Enabling this option allows web applications to access experimental "
    "Virtual Reality APIs.";

#if BUILDFLAG(ENABLE_VR)

const char kWebvrExperimentalRenderingName[] =
    "WebVR experimental rendering optimizations";

const char kWebvrExperimentalRenderingDescription[] =
    "Enabling this option activates experimental rendering path "
    "optimizations for WebVR.";

#endif  // BUILDFLAG(ENABLE_VR)

const char kGamepadExtensionsName[] = "Gamepad Extensions";

const char kGamepadExtensionsDescription[] =
    "Enabling this option allows web applications to access experimental "
    "extensions to the Gamepad APIs.";

#if defined(OS_ANDROID)

const char kNewPhotoPickerName[] = "Enable new Photopicker";

const char kNewPhotoPickerDescription[] =
    "Activates the new picker for selecting photos.";

#endif  // defined(OS_ANDROID)

#if defined(OS_ANDROID)

const char kEnableOskOverscrollName[] = "Enable OSK Overscroll";

const char kEnableOskOverscrollDescription[] =
    "Enable OSK overscroll support. With this flag on, the OSK will only "
    "resize the visual viewport.";

#endif  // defined(OS_ANDROID)

const char kQuicName[] = "Experimental QUIC protocol";

const char kQuicDescription[] = "Enable experimental QUIC protocol support.";

const char kSslVersionMaxName[] = "Maximum TLS version enabled.";

const char kSslVersionMaxDescription[] = "Set maximum enabled TLS version.";

const char kSslVersionMaxTls12[] = "TLS 1.2";

const char kSslVersionMaxTls13[] = "TLS 1.3";

const char kEnableTokenBindingName[] = "Token Binding.";

const char kEnableTokenBindingDescription[] = "Enable Token Binding support.";

const char kPassiveDocumentEventListenersDescription[] =
    "Forces touchstart, and touchmove event listeners on document level "
    "targets (which haven't requested otherwise) to be treated as passive.";

const char kPassiveDocumentEventListenersName[] =
    "Document Level Event Listeners Passive Default";

const char kPassiveEventListenersDueToFlingDescription[] =
    "Forces touchstart, and first touchmove per scroll event listeners "
    "during fling to be treated as passive.";

const char kPassiveEventListenersDueToFlingName[] =
    "Touch Event Listeners Passive Default During Fling";

const char kPassiveEventListenerTrue[] = "True (when unspecified)";

const char kPassiveEventListenerForceAllTrue[] = "Force All True";

const char kPassiveEventListenerDefaultName[] =
    "Passive Event Listener Override";

const char kPassiveEventListenerDefaultDescription[] =
    "Forces touchstart, touchmove, mousewheel and wheel event listeners "
    "(which haven't requested otherwise) to be treated as passive. This "
    "will break touch/wheel behavior on some websites but is useful for "
    "demonstrating the potential performance benefits of adopting passive "
    "event listeners.";

const char kImportantSitesInCbdName[] =
    "Important sites options in clear browsing data dialog";

const char kImportantSitesInCbdDescription[] =
    "Include the option to whitelist important sites in the clear browsing "
    "data dialog.";

#if defined(USE_ASH)

const char kAshShelfColorName[] = "Shelf color in Chrome OS system UI";

const char kAshShelfColorDescription[] =
    "Enables/disables the shelf color to be a derived from the wallpaper. The "
    "--ash-shelf-color-scheme flag defines how that color is derived.";

const char kAshShelfColorScheme[] = "Shelf color scheme in Chrome OS System UI";

const char kAshShelfColorSchemeDescription[] =
    "Specify how the color is derived from the wallpaper. This flag is only "
    "used when the --ash-shelf-color flag is enabled. Defaults to Dark & Muted";

const char kAshShelfColorSchemeLightVibrant[] = "Light & Vibrant";

const char kAshShelfColorSchemeNormalVibrant[] = "Normal & Vibrant";

const char kAshShelfColorSchemeDarkVibrant[] = "Dark & Vibrant";

const char kAshShelfColorSchemeLightMuted[] = "Light & Muted";

const char kAshShelfColorSchemeNormalMuted[] = "Normal & Muted";

const char kAshShelfColorSchemeDarkMuted[] = "Dark & Muted";

const char kAshEnableMirroredScreenName[] = "Enable mirrored screen mode.";

const char kAshEnableMirroredScreenDescription[] =
    "Enable the mirrored screen mode. This mode flips the screen image "
    "horizontally.";

const char kAshDisableSmoothScreenRotationName[] =
    "Disable smooth rotation animations.";

const char kAshDisableSmoothScreenRotationDescription[] =
    "Disable smooth rotation animations.";

const char kMaterialDesignInkDropAnimationFast[] = "Fast";

const char kMaterialDesignInkDropAnimationSlow[] = "Slow";

const char kMaterialDesignInkDropAnimationSpeedName[] =
    "Material Design Ink Drop Animation Speed";

const char kMaterialDesignInkDropAnimationSpeedDescription[] =
    "Sets the speed of the experimental visual feedback animations for "
    "material design.";

const char kUiSlowAnimationsName[] = "Slow UI animations";

const char kUiSlowAnimationsDescription[] = "Makes all UI animations slow.";

const char kUiShowCompositedLayerBordersName[] =
    "Show UI composited layer borders";

const char kUiShowCompositedLayerBordersDescription[] =
    "Show border around composited layers created by UI.";

const char kUiShowCompositedLayerBordersRenderPass[] = "RenderPass";

const char kUiShowCompositedLayerBordersSurface[] = "Surface";

const char kUiShowCompositedLayerBordersLayer[] = "Layer";

const char kUiShowCompositedLayerBordersAll[] = "All";

#endif  // defined(USE_ASH)

const char kJavascriptHarmonyShippingName[] =
    "Latest stable JavaScript features";

const char kJavascriptHarmonyShippingDescription[] =
    "Some web pages use legacy or non-standard JavaScript extensions that "
    "may conflict with the latest JavaScript features. This flag allows "
    "disabling support of those features for compatibility with such "
    "pages.";

const char kJavascriptHarmonyName[] = "Experimental JavaScript";

const char kJavascriptHarmonyDescription[] =
    "Enable web pages to use experimental JavaScript features.";

const char kV8DisableIgnitionTurboName[] =
    "Classic JavaScript Compilation Pipeline";

const char kV8DisableIgnitionTurboDescription[] =
    "Enables V8's classic compilation pipeline for JavaScript execution "
    "(disabling V8's new Ignition interpreter and TurboFan compiler)";

const char kEnableAsmWasmName[] =
    "Experimental Validate Asm.js and convert to WebAssembly when valid.";

const char kEnableAsmWasmDescription[] =
    R"*(Validate Asm.js when "use asm" is present and then convert to )*"
    R"*(WebAssembly.)*";

const char kEnableSharedArrayBufferName[] =
    "Experimental enabled SharedArrayBuffer support in JavaScript.";

const char kEnableSharedArrayBufferDescription[] =
    "Enable SharedArrayBuffer support in JavaScript.";

const char kEnableWasmName[] = "WebAssembly structured cloning support.";
const char kEnableWasmDescription[] =
    "Enable web pages to use WebAssembly structured cloning.";

const char kEnableWasmStreamingName[] =
    "WebAssembly streaming compile/instantiate support.";
const char kEnableWasmStreamingDescription[] =
    "WebAssembly.{compile|instantiate} taking a Response as parameter.";

#if defined(OS_ANDROID)

const char kMediaDocumentDownloadButtonName[] =
    "Download button when opening a page with media url.";

const char kMediaDocumentDownloadButtonDescription[] =
    "Allow a download button to show up when opening a page with media "
    "url.";

#endif  // defined(OS_ANDROID)

const char kSoftwareRasterizerName[] = "3D software rasterizer";

const char kSoftwareRasterizerDescription[] =
    "Fall back to a 3D software rasterizer when the GPU cannot be used.";

const char kGpuRasterizationName[] = "GPU rasterization";

const char kGpuRasterizationDescription[] =
    "Use GPU to rasterize web content. Requires impl-side painting.";

const char kForceGpuRasterization[] = "Force-enabled for all layers";

const char kGpuRasterizationMsaaSampleCountName[] =
    "GPU rasterization MSAA sample count.";

const char kGpuRasterizationMsaaSampleCountDescription[] =
    "Specify the number of MSAA samples for GPU rasterization.";

const char kGpuRasterizationMsaaSampleCountZero[] = "0";

const char kGpuRasterizationMsaaSampleCountTwo[] = "2";

const char kGpuRasterizationMsaaSampleCountFour[] = "4";

const char kGpuRasterizationMsaaSampleCountEight[] = "8";

const char kGpuRasterizationMsaaSampleCountSixteen[] = "16";

const char kSlimmingPaintInvalidationName[] = "Slimming paint invalidation.";

const char kSlimmingPaintInvalidationDescription[] =
    "Whether to enable a new paint invalidation system.";

const char kExperimentalSecurityFeaturesName[] =
    "Potentially annoying security features";

const char kExperimentalSecurityFeaturesDescription[] =
    "Enables several security features that will likely break one or more "
    "pages that you visit on a daily basis. Strict mixed content checking, "
    "for example. And locking powerful features to secure contexts. This "
    "flag will probably annoy you.";

const char kExperimentalWebPlatformFeaturesName[] =
    "Experimental Web Platform features";

const char kExperimentalWebPlatformFeaturesDescription[] =
    "Enables experimental Web Platform features that are in development.";

const char kExperimentalPointerEventName[] = "Pointer Events";

const char kExperimentalPointerEventDescription[] =
    "Enables support for the Pointer Events API. This is intended only for "
    "testing by web developers.";

const char kOriginTrialsName[] = "Origin Trials";

const char kOriginTrialsDescription[] =
    "Enables origin trials for controlling access to feature/API "
    "experiments.";

const char kBleAdvertisingInExtensionsName[] = "BLE Advertising in Chrome Apps";

const char kBleAdvertisingInExtensionsDescription[] =
    "Enables BLE Advertising in Chrome Apps. BLE Advertising might "
    "interfere with regular use of Bluetooth Low Energy features.";

const char kDevtoolsExperimentsName[] = "Developer Tools experiments";

const char kDevtoolsExperimentsDescription[] =
    "Enables Developer Tools experiments. Use Settings panel in Developer "
    "Tools to toggle individual experiments.";

const char kSilentDebuggerExtensionApiName[] = "Silent Debugging";

const char kSilentDebuggerExtensionApiDescription[] =
    "Do not show the infobar when an extension attaches to a page via "
    "chrome.debugger API. This is required to debug extension background "
    "pages.";

const char kShowTouchHudName[] = "Show HUD for touch points";

const char kShowTouchHudDescription[] =
    "Enables a heads-up display at the top-left corner of the screen that "
    "lists information about the touch-points on the screen.";

const char kPreferHtmlOverPluginsName[] = "Prefer HTML over Flash";

const char kPreferHtmlOverPluginsDescription[] =
    "Prefer HTML content by hiding Flash from the list of plugins.";

const char kAllowNaclSocketApiName[] = "NaCl Socket API.";

const char kAllowNaclSocketApiDescription[] =
    "Allows applications to use NaCl Socket API. Use only to test NaCl "
    "plugins.";

const char kRunAllFlashInAllowModeName[] =
    R"*(Run all Flash content when Flash setting is set to "allow")*";

const char kRunAllFlashInAllowModeDescription[] =
    R"*(For sites that have been set to "allow" Flash content, run all )*"
    R"*(content including any that has been deemed unimportant.)*";

const char kPinchScaleName[] = "Pinch scale";

const char kPinchScaleDescription[] =
    "Enables experimental support for scale using pinch.";

const char kReducedReferrerGranularityName[] =
    "Reduce default 'referer' header granularity.";

const char kReducedReferrerGranularityDescription[] =
    "If a page hasn't set an explicit referrer policy, setting this flag "
    "will reduce the amount of information in the 'referer' header for "
    "cross-origin requests.";

#if defined(OS_CHROMEOS)

const char kUseMusDescription[] = "Enable the Mojo UI service.";

const char kUseMusName[] = "Mus";

const char kEnableMashDescription[] =
    "Mash (UI, Chrome and ash in separate services)";

const char kEnableMusDescription[] =
    "Mus (UI in separate service, Chrome and ash in same service)";

const char kAllowTouchpadThreeFingerClickName[] = "Touchpad three-finger-click";

const char kAllowTouchpadThreeFingerClickDescription[] =
    "Enables touchpad three-finger-click as middle button.";

const char kAshEnableUnifiedDesktopName[] = "Unified desktop mode";

const char kAshEnableUnifiedDesktopDescription[] =
    "Enable unified desktop mode which allows a window to span multiple "
    "displays.";

const char kBootAnimationName[] = "Boot animation";

const char kBootAnimationDescription[] =
    "Wallpaper boot animation (except for OOBE case).";

const char kTetherName[] = "Instant Tethering";

const char kTetherDescription[] =
    "Enables Instant Tethering. Instant Tethering allows your nearby Google "
    "phone to share its Internet connection with this device.";

#endif  // defined(OS_CHROMEOS)

const char kAcceleratedVideoDecodeName[] = "Hardware-accelerated video decode";

const char kAcceleratedVideoDecodeDescription[] =
    "Hardware-accelerated video decode where available.";

const char kEnableHDRName[] = "HDR mode";

const char kEnableHDRDescription[] =
    "Enables HDR support on compatible displays.";

const char kCloudImportName[] = "Cloud Import";

const char kCloudImportDescription[] = "Allows the cloud-import feature.";

const char kRequestTabletSiteName[] =
    "Request tablet site option in the settings menu";

const char kRequestTabletSiteDescription[] =
    "Allows the user to request tablet site. Web content is often optimized "
    "for tablet devices. When this option is selected the user agent string "
    "is changed to indicate a tablet device. Web content optimized for "
    "tablets is received there after for the current tab.";

const char kDebugPackedAppName[] = "Debugging for packed apps";

const char kDebugPackedAppDescription[] =
    "Enables debugging context menu options such as Inspect Element for "
    "packed applications.";

const char kDropSyncCredentialName[] =
    "Drop sync credentials from password manager";

const char kDropSyncCredentialDescription[] =
    "The password manager will not offer to save the credential used to "
    "sync.";

const char kPasswordGenerationName[] = "Password generation";

const char kPasswordGenerationDescription[] =
    "Allow the user to have Chrome generate passwords when it detects "
    "account creation pages.";

const char kPasswordForceSavingName[] = "Force-saving of passwords";

const char kPasswordForceSavingDescription[] =
    "Allow the user to manually enforce password saving instead of relying "
    "on password manager's heuristics.";

const char kManualPasswordGenerationName[] = "Manual password generation.";

const char kManualPasswordGenerationDescription[] =
    "Show a 'Generate Password' option on the context menu for all password "
    "fields.";

const char kShowAutofillSignaturesName[] = "Show autofill signatures.";

const char kShowAutofillSignaturesDescription[] =
    "Annotates web forms with Autofill signatures as HTML attributes.";

const char kSuggestionsWithSubStringMatchName[] =
    "Substring matching for Autofill suggestions";

const char kSuggestionsWithSubStringMatchDescription[] =
    "Match Autofill suggestions based on substrings (token prefixes) rather "
    "than just prefixes.";

const char kAffiliationBasedMatchingName[] =
    "Affiliation based matching in password manager";

const char kAffiliationBasedMatchingDescription[] =
    "Allow credentials stored for Android applications to be filled into "
    "corresponding websites.";

const char kProtectSyncCredentialName[] = "Autofill sync credential";

const char kProtectSyncCredentialDescription[] =
    "How the password manager handles autofill for the sync credential.";

const char kPasswordImportExportName[] = "Password import and export";

const char kPasswordImportExportDescription[] =
    "Import and Export functionality in password settings.";

const char kProtectSyncCredentialOnReauthName[] =
    "Autofill sync credential only for transactional reauth pages";

const char kProtectSyncCredentialOnReauthDescription[] =
    "How the password manager handles autofill for the sync credential only "
    "for transactional reauth pages.";

const char kIconNtpName[] = "Large icons on the New Tab page";

const char kIconNtpDescription[] =
    "Enable the experimental New Tab page using large icons.";

const char kPushApiBackgroundModeName[] = "Enable Push API background mode";

const char kPushApiBackgroundModeDescription[] =
    "Enable background mode for the Push API. This allows Chrome to "
    "continue running after the last window is closed, and to launch at OS "
    "startup, if the Push API needs it.";

const char kEnableNavigationTracingName[] = "Enable navigation tracing";

const char kEnableNavigationTracingDescription[] =
    "This is to be used in conjunction with the trace-upload-url flag. "
    "WARNING: When enabled, Chrome will record performance data for every "
    "navigation and upload it to the URL specified by the trace-upload-url "
    "flag. The trace may include personally identifiable information (PII) "
    "such as the titles and URLs of websites you visit.";

const char kTraceUploadUrlName[] = "Trace label for navigation tracing";

const char kTraceUploadUrlDescription[] =
    "This is to be used in conjunction with the enable-navigation-tracing "
    "flag. Please select the label that best describes the recorded traces. "
    "This will choose the destination the traces are uploaded to. If you "
    "are not sure, select other. If left empty, no traces will be "
    "uploaded.";

const char kDisableAudioForDesktopShareName[] =
    "Disable Audio For Desktop Share";

const char kDisableAudioForDesktopShareDescription[] =
    "With this flag on, desktop share picker window will not let the user "
    "choose whether to share audio.";

const char kDisableTabForDesktopShareName[] =
    "Disable Desktop Share with tab source";

const char kDisableTabForDesktopShareDescription[] =
    "This flag controls whether users can choose a tab for desktop share.";

const char kTraceUploadUrlChoiceOther[] = "Other";

const char kTraceUploadUrlChoiceEmloading[] = "emloading";

const char kTraceUploadUrlChoiceQa[] = "QA";

const char kTraceUploadUrlChoiceTesting[] = "Testing";

const char kSupervisedUserManagedBookmarksFolderName[] =
    "Managed bookmarks for supervised users";

const char kSupervisedUserManagedBookmarksFolderDescription[] =
    "Enable the managed bookmarks folder for supervised users.";

const char kSyncAppListName[] = "App Launcher sync";

const char kSyncAppListDescription[] =
    "Enable App Launcher sync. This also enables Folders where available "
    "(non OSX).";

const char kDriveSearchInChromeLauncherName[] =
    "Drive Search in Chrome App Launcher";

const char kDriveSearchInChromeLauncherDescription[] =
    "Files from Drive will show up when searching the Chrome App Launcher.";

const char kV8CacheOptionsName[] = "V8 caching mode.";

const char kV8CacheOptionsDescription[] =
    "Caching mode for the V8 JavaScript engine.";

const char kV8CacheOptionsParse[] = "Cache V8 parser data.";

const char kV8CacheOptionsCode[] = "Cache V8 compiler data.";

const char kV8CacheStrategiesForCacheStorageName[] =
    "V8 caching strategy for CacheStorage.";

const char kV8CacheStrategiesForCacheStorageDescription[] =
    "Caching strategy of scripts in CacheStorage for the V8 JavaScript "
    "engine.";

const char kV8CacheStrategiesForCacheStorageNormal[] = "Normal";

const char kV8CacheStrategiesForCacheStorageAggressive[] = "Aggressive";

const char kMemoryCoordinatorName[] = "Memory coordinator";

const char kMemoryCoordinatorDescription[] =
    "Enable memory coordinator instead of memory pressure listeners.";

const char kServiceWorkerNavigationPreloadName[] =
    "Service worker navigation preload.";

const char kServiceWorkerNavigationPreloadDescription[] =
    "Enable web pages to use the experimental service worker navigation "
    "preload API.";

//  Data Reduction Proxy

const char kDataReductionProxyLoFiName[] = "Data Saver Lo-Fi mode";

const char kDataReductionProxyLoFiDescription[] =
    "Forces Data Saver Lo-Fi mode to be always enabled, enabled only on "
    "cellular connections, or disabled. Data Saver must be enabled for "
    "Lo-Fi mode to be used.";

const char kDataReductionProxyLoFiAlwaysOn[] = "Always on";

const char kDataReductionProxyLoFiCellularOnly[] = "Cellular only";

const char kDataReductionProxyLoFiDisabled[] = "Disable";

const char kDataReductionProxyLoFiSlowConnectionsOnly[] =
    "Slow connections only";

const char kEnableDataReductionProxyLitePageName[] =
    "Lite pages for Data Saver Lo-Fi mode";

const char kEnableDataReductionProxyLitePageDescription[] =
    "Enable lite pages in Data Saver Lo-Fi mode. Previews of pages will be "
    "shown instead of image placeholders when Lo-Fi is on. Data Saver and "
    "Lo-Fi must be enabled for lite pages to be shown.";

const char kDataReductionProxyCarrierTestName[] =
    "Enable a carrier-specific Data Reduction Proxy for testing.";

const char kDataReductionProxyCarrierTestDescription[] =
    "Use a carrier-specific Data Reduction Proxy for testing.";

const char kEnableDataReductionProxySavingsPromoName[] =
    "Data Saver 1 MB Savings Promo";

const char kEnableDataReductionProxySavingsPromoDescription[] =
    "Enable a Data Saver promo for 1 MB of savings. If Data Saver has "
    "already saved 1 MB of data, then the promo will not be shown. Data "
    "Saver must be enabled for the promo to be shown.";

#if defined(OS_ANDROID)

const char kEnableDataReductionProxyMainMenuName[] =
    "Enable Data Saver main menu footer";

const char kEnableDataReductionProxyMainMenuDescription[] =
    "Enables the Data Saver footer in the main menu";

const char kEnableDataReductionProxySiteBreakdownName[] =
    "Data Saver Site Breakdown";

const char kEnableDataReductionProxySiteBreakdownDescription[] =
    "Enable the site breakdown on the Data Saver settings page.";

const char kEnableOfflinePreviewsName[] = "Offline Page Previews";

const char kEnableOfflinePreviewsDescription[] =
    "Enable showing offline page previews on slow networks.";

const char kEnableClientLoFiName[] = "Client-side Lo-Fi previews";

const char kEnableClientLoFiDescription[] =
    "Enable showing low fidelity images on some pages on slow networks.";

#endif  // defined(OS_ANDROID)

const char kLcdTextName[] = "LCD text antialiasing";

const char kLcdTextDescription[] =
    "If disabled, text is rendered with grayscale antialiasing instead of "
    "LCD (subpixel) when doing accelerated compositing.";

const char kDistanceFieldTextName[] = "Distance field text";

const char kDistanceFieldTextDescription[] =
    "Text is rendered with signed distance fields rather than bitmap alpha "
    "masks.";

const char kZeroCopyName[] = "Zero-copy rasterizer";

const char kZeroCopyDescription[] =
    "Raster threads write directly to GPU memory associated with tiles.";

const char kHideInactiveStackedTabCloseButtonsName[] =
    "Hiding close buttons on inactive tabs when stacked";

const char kHideInactiveStackedTabCloseButtonsDescription[] =
    "Hides the close buttons of inactive tabs when the tabstrip is in "
    "stacked mode.";

const char kDefaultTileWidthName[] = "Default tile width";

const char kDefaultTileWidthDescription[] = "Specify the default tile width.";

const char kDefaultTileWidthShort[] = "128";

const char kDefaultTileWidthTall[] = "256";

const char kDefaultTileWidthGrande[] = "512";

const char kDefaultTileWidthVenti[] = "1024";

const char kDefaultTileHeightName[] = "Default tile height";

const char kDefaultTileHeightDescription[] = "Specify the default tile height.";

const char kDefaultTileHeightShort[] = "128";

const char kDefaultTileHeightTall[] = "256";

const char kDefaultTileHeightGrande[] = "512";

const char kDefaultTileHeightVenti[] = "1024";

const char kNumRasterThreadsName[] = "Number of raster threads";

const char kNumRasterThreadsDescription[] =
    "Specify the number of raster threads.";

const char kNumRasterThreadsOne[] = "1";

const char kNumRasterThreadsTwo[] = "2";

const char kNumRasterThreadsThree[] = "3";

const char kNumRasterThreadsFour[] = "4";

const char kResetAppListInstallStateName[] =
    "Reset the App Launcher install state on every restart.";

const char kResetAppListInstallStateDescription[] =
    "Reset the App Launcher install state on every restart. While this flag "
    "is set, Chrome will forget the launcher has been installed each time "
    "it starts. This is used for testing the App Launcher install flow.";

#if defined(OS_CHROMEOS)

const char kFirstRunUiTransitionsName[] =
    "Animated transitions in the first-run tutorial";

const char kFirstRunUiTransitionsDescription[] =
    "Transitions during first-run tutorial are animated.";

#endif  // defined(OS_CHROMEOS)

const char kNewBookmarkAppsName[] = "The new bookmark app system";

const char kNewBookmarkAppsDescription[] =
    "Enables the new system for creating bookmark apps.";

#if defined(OS_MACOSX)

const char kHostedAppsInWindowsName[] =
    "Allow hosted apps to be opened in windows";

const char kHostedAppsInWindowsDescription[] =
    "Allows hosted apps to be opened in windows instead of being limited to "
    "tabs.";

const char kTabDetachingInFullscreenName[] =
    "Allow tab detaching in fullscreen";

const char kTabDetachingInFullscreenDescription[] =
    "Allow tabs to detach from the tabstrip when in fullscreen mode on "
    "Mac.";

const char kFullscreenToolbarRevealName[] =
    "Enables the toolbar in fullscreen to reveal itself.";

const char kFullscreenToolbarRevealDescription[] =
    "Reveal the toolbar in fullscreen for a short period when the tab strip "
    "has changed.";

const char kTabStripKeyboardFocusName[] = "Tab Strip Keyboard Focus";

const char kTabStripKeyboardFocusDescription[] =
    "Enable keyboard focus for the tabs in the tab strip.";

#endif  // defined(OS_MACOSX)

const char kHostedAppShimCreationName[] =
    "Creation of app shims for hosted apps on Mac";

const char kHostedAppShimCreationDescription[] =
    "Create app shims on Mac when creating a hosted app.";

const char kHostedAppQuitNotificationName[] =
    "Quit notification for hosted apps";

const char kHostedAppQuitNotificationDescription[] =
    "Display a notification when quitting Chrome if hosted apps are "
    "currently running.";

#if defined(OS_ANDROID)

const char kPullToRefreshEffectName[] = "The pull-to-refresh effect";

const char kPullToRefreshEffectDescription[] =
    "Page reloads triggered by vertically overscrolling content.";

const char kTranslateCompactUIName[] = "New Translate Infobar";

const char kTranslateCompactUIDescription[] =
    "Enable the new Translate compact infobar UI.";

#endif  // defined(OS_ANDROID)

#if defined(OS_MACOSX)

const char kTranslateNewUxName[] = "New Translate UX";

const char kTranslateNewUxDescription[] =
    "Enable the new Translate bubble UX is offered instead of the infobar.";

#endif  // defined(OS_MACOSX)

const char kTranslate2016q2UiName[] = "Translate 2016Q2 UI";

const char kTranslate2016q2UiDescription[] =
    "Improved triggering logic and look for Translate Bubble UI";

const char kTranslateLanguageByUlpName[] = "Translate Language by ULP";

const char kTranslateLanguageByUlpDescription[] =
    "Improved translate target language and triggering logic by considering "
    "information from User Language Profile (ULP).";

const char kViewsRectBasedTargetingName[] = "Rect-based targeting in views";

const char kViewsRectBasedTargetingDescription[] =
    "Rect-based targeting uses a heuristic to determine the most probable "
    "target of a gesture, where the touch region is represented by a "
    "rectangle.";

const char kPermissionActionReportingName[] = "Permission Action Reporting";

const char kPermissionActionReportingDescription[] =
    "Enables permission action reporting to Safe Browsing servers for opted "
    "in users.";

const char kPermissionsBlacklistName[] = "Permissions Blacklist";

const char kPermissionsBlacklistDescription[] =
    "Enables the Permissions Blacklist, which blocks permissions for "
    "blacklisted sites for Safe Browsing users.";

const char kThreadedScrollingName[] = "Threaded scrolling";

const char kThreadedScrollingDescription[] =
    "Threaded handling of scroll-related input events. Disabling this will "
    "force all such scroll events to be handled on the main thread. Note "
    "that this can dramatically hurt scrolling performance of most websites "
    "and is intended for testing purposes only.";

const char kHarfbuzzRendertextName[] = "HarfBuzz for UI text";

const char kHarfbuzzRendertextDescription[] =
    "Enable cross-platform HarfBuzz layout engine for UI text. Doesn't "
    "affect web content.";

const char kEmbeddedExtensionOptionsName[] = "Embedded extension options";

const char kEmbeddedExtensionOptionsDescription[] =
    "Display extension options as an embedded element in "
    "chrome://extensions rather than opening a new tab.";

const char kTabAudioMutingName[] = "Tab audio muting UI control";

const char kTabAudioMutingDescription[] =
    "When enabled, the audio indicators in the tab strip double as tab "
    "audio mute controls. This also adds commands in the tab context menu "
    "for quickly muting multiple selected tabs.";

const char kEasyUnlockBluetoothLowEnergyDiscoveryName[] =
    "Smart Lock Bluetooth Low Energy Discovery";

const char kEasyUnlockBluetoothLowEnergyDiscoveryDescription[] =
    "Enables a Smart Lock setting that allows Chromebook to discover phones "
    "over Bluetooth Low Energy in order to unlock the Chromebook when the "
    "phone is in its proximity.";

const char kEasyUnlockProximityDetectionName[] =
    "Smart Lock proximity detection";

const char kEasyUnlockProximityDetectionDescription[] =
    "Enables a Smart Lock setting that restricts unlocking to only work "
    "when your phone is very close to (roughly, within an arm's length of) "
    "the Chrome device.";

const char kWifiCredentialSyncName[] = "WiFi credential sync";

const char kWifiCredentialSyncDescription[] =
    "Enables synchronizing WiFi network settings across devices. When "
    "enabled, the WiFi credential datatype is registered with Chrome Sync, "
    "and WiFi credentials are synchronized subject to user preferences. "
    "(See also, chrome://settings/syncSetup.)";

const char kSyncSandboxName[] = "Use Chrome Sync sandbox";

const char kSyncSandboxDescription[] =
    "Connects to the testing server for Chrome Sync.";

const char kDatasaverPromptName[] = "Cellular Data Saver Prompt";

const char kDatasaverPromptDescription[] =
    "Enables a prompt, which appears when a cellular network connection is "
    "detected, to take the user to the Data Saver extension page on Chrome "
    "Web Store.";

const char kDatasaverPromptDemoMode[] = "Demo mode";

const char kTrySupportedChannelLayoutsName[] =
    "Causes audio output streams to check if channel layouts other than the "
    "default hardware layout are available.";

const char kTrySupportedChannelLayoutsDescription[] =
    "Causes audio output streams to check if channel layouts other than the "
    "default hardware layout are available. Turning this on will allow the "
    "OS to do stereo to surround expansion if supported. May expose third "
    "party driver bugs, use with caution.";

#if defined(OS_MACOSX)

const char kAppInfoDialogName[] = "Toolkit-Views App Info Dialog.";

const char kAppInfoDialogDescription[] =
    "Makes the Toolkit-Views based App Info dialog accessible from "
    "chrome://apps or chrome://extensions in place of the native extension "
    "permissions dialog, or the details link (which is a link to the Web "
    "Store).";

const char kMacViewsNativeAppWindowsName[] = "Toolkit-Views App Windows.";

const char kMacViewsNativeAppWindowsDescription[] =
    "Controls whether to use Toolkit-Views based Chrome App windows.";

const char kMacViewsTaskManagerName[] = "Toolkit-Views Task Manager.";

const char kMacViewsTaskManagerDescription[] =
    "Controls whether to use the Toolkit-Views based Task Manager.";

const char kAppWindowCyclingName[] = "Custom Window Cycling for Chrome Apps.";

const char kAppWindowCyclingDescription[] =
    "Changes the behavior of Cmd+` when a Chrome App becomes active. When "
    "enabled, Chrome Apps will not be cycled when Cmd+` is pressed from a "
    "browser window, and browser windows will not be cycled when a Chrome "
    "App is active.";

#endif  // defined(OS_MACOSX)

#if defined(OS_CHROMEOS)

const char kAcceleratedMjpegDecodeName[] =
    "Hardware-accelerated mjpeg decode for captured frame";

const char kAcceleratedMjpegDecodeDescription[] =
    "Enable hardware-accelerated mjpeg decode for captured frame where "
    "available.";

#endif  // defined(OS_CHROMEOS)

const char kSimplifiedFullscreenUiName[] =
    "Simplified full screen / mouse lock UI.";

const char kSimplifiedFullscreenUiDescription[] =
    "A simplified new user experience when entering page-triggered full "
    "screen or mouse pointer lock states.";

const char kExperimentalKeyboardLockUiName[] = "Experimental keyboard lock UI.";

const char kExperimentalKeyboardLockUiDescription[] =
    "An experimental full screen with keyboard lock mode requiring users to "
    "hold Esc to exit.";

#if defined(OS_ANDROID)

const char kProgressBarAnimationName[] =
    "Android phone page loading progress bar animation";

const char kProgressBarAnimationDescription[] =
    "Configures Android phone page loading progress bar animation.";

const char kProgressBarAnimationLinear[] = "Linear";

const char kProgressBarAnimationSmooth[] = "Smooth";

const char kProgressBarAnimationSmoothIndeterminate[] = "Smooth indeterminate";

const char kProgressBarAnimationFastStart[] = "Fast start";

const char kProgressBarCompletionName[] =
    "Android phone page load progress bar completion time.";

const char kProgressBarCompletionDescription[] =
    "Configures Android phone page loading progress bar completion time.";

const char kProgressBarCompletionLoadEvent[] =
    R"*(Top loading frame's onload event ("everything" is done in the )*"
    R"*(page, historical behavior).)*";

const char kProgressBarCompletionResourcesBeforeDcl[] =
    "Main frame's domContentLoaded and all resources loads started before "
    "domContentLoaded (iframes ignored).";

const char kProgressBarCompletionDomContentLoaded[] =
    "Main frame's domContentLoaded (iframes ignored).";

const char kProgressBarCompletionResourcesBeforeDclAndSameOriginIframes[] =
    "domContentLoaded and all resources loads started before "
    "domContentLoaded (main frame and same origin iframes).";

#endif  // defined(OS_ANDROID)

const char kDisallowDocWrittenScriptsUiName[] =
    "Block scripts loaded via document.write";

const char kDisallowDocWrittenScriptsUiDescription[] =
    "Disallows fetches for third-party parser-blocking scripts inserted "
    "into the main frame via document.write.";

#if defined(OS_WIN)

const char kEnableAppcontainerName[] = "Enable AppContainer Lockdown.";

const char kEnableAppcontainerDescription[] =
    "Enables the use of an AppContainer on sandboxed processes to improve "
    "security.";

#endif  // defined(OS_WIN)

#if defined(TOOLKIT_VIEWS) || (defined(OS_MACOSX) && !defined(OS_IOS))

const char kShowCertLinkOnPageInfoName[] = "Show certificate link";

const char kShowCertLinkOnPageInfoDescription[] =
    "Add a link from the Page Info bubble to the certificate viewer for HTTPS "
    "sites.";

#endif  // defined(TOOLKIT_VIEWS) || (defined(OS_MACOSX) && !defined(OS_IOS))

#if defined(TOOLKIT_VIEWS) || defined(OS_ANDROID)

const char kAutofillCreditCardUploadName[] =
    "Enable offering upload of Autofilled credit cards";

const char kAutofillCreditCardUploadDescription[] =
    "Enables a new option to upload credit cards to Google Payments for "
    "sync to all Chrome devices.";

#endif  // defined(TOOLKIT_VIEWS) || defined(OS_ANDROID)

const char kForceUiDirectionName[] = "Force UI direction";

const char kForceUiDirectionDescription[] =
    "Explicitly force the UI to left-to-right (LTR) or right-to-left (RTL) "
    "mode, overriding the default direction of the UI language.";

const char kForceTextDirectionName[] = "Force text direction";

const char kForceTextDirectionDescription[] =
    "Explicitly force the per-character directionality of UI text to "
    "left-to-right (LTR) or right-to-left (RTL) mode, overriding the "
    "default direction of the character language.";

const char kForceDirectionLtr[] = "Left-to-right";

const char kForceDirectionRtl[] = "Right-to-left";

#if defined(OS_WIN) || defined(OS_LINUX)

const char kEnableInputImeApiName[] = "Enable Input IME API";

const char kEnableInputImeApiDescription[] =
    "Enable the use of chrome.input.ime API.";

#endif  // defined(OS_WIN) || defined(OS_LINUX)

const char kEnableGroupedHistoryName[] = "Group history by domain";

const char kEnableGroupedHistoryDescription[] =
    "Group history by website domain (i.e. google.com) on "
    "chrome://history.";

const char kSaveasMenuLabelExperimentName[] =
    "Switch 'Save as' menu labels to 'Download'";

const char kSaveasMenuLabelExperimentDescription[] =
    "Enables an experiment to switch menu labels that use 'Save as...' to "
    "'Download'.";

const char kEnableEnumeratingAudioDevicesName[] =
    "Experimentally enable enumerating audio devices.";

const char kEnableEnumeratingAudioDevicesDescription[] =
    "Experimentally enable the use of enumerating audio devices.";

const char kNewUsbBackendName[] = "Enable new USB backend";

const char kNewUsbBackendDescription[] =
    "Enables the new experimental USB backend for Windows.";

const char kNewOmniboxAnswerTypesName[] =
    "New omnibox answers in suggest types";

const char kNewOmniboxAnswerTypesDescription[] =
    "Enables new types of answers in the omnibox suggest drop-down: "
    "currency conversions, dictionary definitions, sports scores, "
    "translations, and when is.";

const char kEnableZeroSuggestRedirectToChromeName[] =
    "Experimental contextual omnibox suggestion";

const char kEnableZeroSuggestRedirectToChromeDescription[] =
    "Change omnibox contextual suggestions to an experimental source. Note "
    "that this is not an on/off switch for contextual omnibox and it only "
    "applies to suggestions provided before the user starts typing a URL or "
    "a search query (i.e. zero suggest).";

const char kFillOnAccountSelectName[] = "Fill passwords on account selection";

const char kFillOnAccountSelectDescription[] =
    "Filling of passwords when an account is explicitly selected by the "
    "user rather than autofilling credentials on page load.";

const char kEnableClearBrowsingDataCountersName[] =
    "Enable Clear browsing data counters.";

const char kEnableClearBrowsingDataCountersDescription[] =
    "Shows data volume counters in the Clear browsing data dialog.";

#if defined(OS_ANDROID)

const char kTabsInCbdName[] = "Enable tabs for the Clear Browsing Data dialog.";

const char kTabsInCbdDescription[] =
    "Enables a basic and an advanced tab for the Clear Browsing Data "
    "dialog.";

#endif  // defined(OS_ANDROID)

const char kNotificationsNativeFlagName[] = "Enable native notifications.";

const char kNotificationsNativeFlagDescription[] =
    "Enable support for using the native notification toasts and "
    "notification center on platforms where these are available.";

#if defined(OS_ANDROID)

const char kEnableAndroidSpellcheckerDescription[] =
    "Enables use of the Android spellchecker.";

const char kEnableAndroidSpellcheckerName[] = "Enable spell checking";

#endif  // defined(OS_ANDROID)

const char kEnableWebNotificationCustomLayoutsName[] =
    "Enable custom layouts for Web Notifications.";

const char kEnableWebNotificationCustomLayoutsDescription[] =
    "Enable custom layouts for Web Notifications. They will have subtle "
    "layout improvements that are otherwise not possible.";

const char kAccountConsistencyName[] =
    "Identity consistency between browser and cookie jar";

const char kAccountConsistencyDescription[] =
    "When enabled, the browser manages signing in and out of Google "
    "accounts.";

const char kGoogleProfileInfoName[] = "Google profile name and icon";

const char kGoogleProfileInfoDescription[] =
    "Enables using Google information to populate the profile name and icon "
    "in the avatar menu.";

const char kOfferStoreUnmaskedWalletCardsName[] =
    "Google Payments card saving checkbox";

const char kOfferStoreUnmaskedWalletCardsDescription[] =
    "Show the checkbox to offer local saving of a credit card downloaded "
    "from the server.";

const char kOfflineAutoReloadName[] = "Offline Auto-Reload Mode";

const char kOfflineAutoReloadDescription[] =
    "Pages that fail to load while the browser is offline will be "
    "auto-reloaded when the browser is online again.";

const char kOfflineAutoReloadVisibleOnlyName[] =
    "Only Auto-Reload Visible Tabs";

const char kOfflineAutoReloadVisibleOnlyDescription[] =
    "Pages that fail to load while the browser is offline will only be "
    "auto-reloaded if their tab is visible.";

const char kShowSavedCopyName[] = "Show Saved Copy Button";

const char kShowSavedCopyDescription[] =
    "When a page fails to load, if a stale copy of the page exists in the "
    "browser cache, a button will be presented to allow the user to load "
    "that stale copy. The primary enabling choice puts the button in the "
    "most salient position on the error page; the secondary enabling choice "
    "puts it secondary to the reload button.";

const char kEnableShowSavedCopyPrimary[] = "Enable: Primary";

const char kEnableShowSavedCopySecondary[] = "Enable: Secondary";

const char kDisableShowSavedCopy[] = "Disable";

#if defined(OS_CHROMEOS)

const char kVirtualKeyboardName[] = "Virtual Keyboard";

const char kVirtualKeyboardDescription[] = "Enable virtual keyboard support.";

const char kVirtualKeyboardOverscrollName[] = "Virtual Keyboard Overscroll";

const char kVirtualKeyboardOverscrollDescription[] =
    "Enables virtual keyboard overscroll support.";

const char kInputViewName[] = "Input views";

const char kInputViewDescription[] =
    "Enable IME extensions to supply custom views for user input such as "
    "virtual keyboards.";

const char kNewKoreanImeName[] = "New Korean IME";

const char kNewKoreanImeDescription[] =
    "New Korean IME, which is based on Google Input Tools' HMM engine.";

const char kPhysicalKeyboardAutocorrectName[] = "Physical keyboard autocorrect";

const char kPhysicalKeyboardAutocorrectDescription[] =
    "Enable physical keyboard autocorrect for US keyboard, which can "
    "provide suggestions as typing on physical keyboard.";

const char kVoiceInputName[] = "Voice input on virtual keyboard";

const char kVoiceInputDescription[] =
    "Enables voice input on virtual keyboard.";

const char kExperimentalInputViewFeaturesName[] =
    "Experimental input view features";

const char kExperimentalInputViewFeaturesDescription[] =
    "Enable experimental features for IME input views.";

const char kFloatingVirtualKeyboardName[] = "Floating virtual keyboard.";

const char kFloatingVirtualKeyboardDescription[] =
    "Enable/Disable floating virtual keyboard.";

const char kGestureTypingName[] = "Gesture typing for the virtual keyboard.";

const char kGestureTypingDescription[] =
    "Enable/Disable gesture typing option in the settings page for the "
    "virtual keyboard.";

const char kGestureEditingName[] = "Gesture editing for the virtual keyboard.";

const char kGestureEditingDescription[] =
    "Enable/Disable gesture editing option in the settings page for the "
    "virtual keyboard.";

const char kCaptivePortalBypassProxyName[] =
    "Bypass proxy for Captive Portal Authorization";

const char kCaptivePortalBypassProxyDescription[] =
    "If proxy is configured, it usually prevents from authorization on "
    "different captive portals. This enables opening captive portal "
    "authorization dialog in a separate window, which ignores proxy "
    "settings.";

const char kTouchscreenCalibrationName[] =
    "Enable/disable touchscreen calibration option in material design "
    "settings";

const char kTouchscreenCalibrationDescription[] =
    "If enabled, the user can calibrate the touch screen displays in "
    "chrome://md-settings/display.";

const char kTeamDrivesName[] = "Enable Team Drives Integration";
const char kTeamDrivesDescription[] =
    "If enabled, files under Team Drives will appear in the Files app.";

#endif  // defined(OS_CHROMEOS)

//  Strings for controlling credit card assist feature in about:flags.

const char kCreditCardAssistName[] = "Credit Card Assisted Filling";

const char kCreditCardAssistDescription[] =
    "Enable assisted credit card filling on certain sites.";

//  Strings for controlling credit card scanning feature in about:flags.

//  Simple Cache Backend experiment.

const char kSimpleCacheBackendName[] = "Simple Cache for HTTP";

const char kSimpleCacheBackendDescription[] =
    "The Simple Cache for HTTP is a new cache. It relies on the filesystem "
    "for disk space allocation.";

//  Spelling feedback field trial.

const char kSpellingFeedbackFieldTrialName[] = "Spelling Feedback Field Trial";

const char kSpellingFeedbackFieldTrialDescription[] =
    "Enable the field trial for sending user feedback to spelling service.";

//  Web MIDI API.

const char kWebMidiName[] = "Web MIDI API";

const char kWebMidiDescription[] = "Enable Web MIDI API experimental support.";

//  Site per process mode

const char kSitePerProcessName[] = "Strict site isolation";

const char kSitePerProcessDescription[] =
    "Highly experimental security mode that ensures each renderer process "
    "contains pages from at most one site. In this mode, out-of-process "
    "iframes will be used whenever an iframe is cross-site.";

//  Top document isolation mode

const char kTopDocumentIsolationName[] = "Top document isolation";

const char kTopDocumentIsolationDescription[] =
    "Highly experimental performance mode where cross-site iframes are kept "
    "in a separate process from the top document. In this mode, iframes "
    "from different third-party sites will be allowed to share a process.";

//  Cross process guest frames isolation mode

const char kCrossProcessGuestViewIsolationName[] =
    "Cross process frames for guests";

const char kCrossProcessGuestViewIsolationDescription[] =
    "Highly experimental where guests such as &lt;webview> are implemented "
    "on the out-of-process iframe infrastructure.";

//  Task Scheduler

const char kBrowserTaskSchedulerName[] = "Task Scheduler";

const char kBrowserTaskSchedulerDescription[] =
    "Enables redirection of some task posting APIs to the task scheduler.";

//  Arc authorization

#if defined(OS_CHROMEOS)

const char kArcUseAuthEndpointName[] = "Android apps authorization point";

const char kArcUseAuthEndpointDescription[] =
    "Enable Android apps authorization point to automatic sign-in in OptIn "
    "flow.";

#endif  // defined(OS_CHROMEOS)

//  Autofill experiment flags

const char kSingleClickAutofillName[] = "Single-click autofill";

const char kSingleClickAutofillDescription[] =
    "Make autofill suggestions on initial mouse click on a form element.";

#if defined(OS_ANDROID)

const char kAutofillAccessoryViewName[] =
    "Autofill suggestions as keyboard accessory view";

const char kAutofillAccessoryViewDescription[] =
    "Shows Autofill suggestions on top of the keyboard rather than in a "
    "dropdown.";

#endif  // defined(OS_ANDROID)

//  Reader mode experiment flags

#if defined(OS_ANDROID)

const char kReaderModeHeuristicsName[] = "Reader Mode triggering";

const char kReaderModeHeuristicsDescription[] =
    "Determines what pages the Reader Mode button is shown on.";

const char kReaderModeHeuristicsMarkup[] = "With article structured markup";

const char kReaderModeHeuristicsAdaboost[] = "Appears to be an article";

const char kReaderModeHeuristicsAlwaysOff[] = "Never";

const char kReaderModeHeuristicsAlwaysOn[] = "Always";

#endif  // defined(OS_ANDROID)

//  Chrome home flags

#if defined(OS_ANDROID)

const char kChromeHomeName[] = "Chrome Home";

const char kChromeHomeDescription[] = "Enables Chrome Home on Android.";

const char kChromeHomeExpandButtonName[] = "Chrome Home Expand Button";

const char kChromeHomeExpandButtonDescription[] =
    "Enables the expand button for Chrome Home.";

const char kChromeHomeNtpRedesignName[] = "Chrome Home NTP Redesign";

const char kChromeHomeNtpRedesignDescription[] =
    "Enables the NTP redesign for Chrome Home.";

#endif  // defined(OS_ANDROID)

//  In-Product Help flags

#if defined(OS_ANDROID)

const char kEnableIphDemoModeName[] = "In-Product Help Demo Mode";

const char kEnableIphDemoModeDescription[] =
    "Enables In-Product Help demo mode on Android.";

#endif  // defined(OS_ANDROID)

//  Settings window flags

const char kSettingsWindowName[] = "Show settings in a window";

const char kSettingsWindowDescription[] =
    "Settings will be shown in a dedicated window instead of as a browser "
    "tab.";

//  Mixed content issue workaround flags

#if defined(OS_ANDROID)

//  Flag strings for seccomp-bpf sandbox flag.

const char kSeccompFilterSandboxAndroidName[] = "Seccomp-bpf renderer sandbox";

const char kSeccompFilterSandboxAndroidDescription[] =
    "Renderers will have a second-layer sandbox provided by seccomp-bpf. "
    "This requires kernel features only available on select Android "
    "versions.";

#endif  // defined(OS_ANDROID)

//  Extension Content Verification

const char kExtensionContentVerificationName[] =
    "Extension Content Verification";

const char kExtensionContentVerificationDescription[] =
    "This flag can be used to turn on verification that the contents of the "
    "files on disk for extensions from the webstore match what they're "
    "expected to be. This can be used to turn on this feature if it would "
    "not otherwise have been turned on, but cannot be used to turn it off "
    "(because this setting can be tampered with by malware).";

const char kExtensionContentVerificationBootstrap[] =
    "Bootstrap (get expected hashes, but do not enforce them)";

const char kExtensionContentVerificationEnforce[] =
    "Enforce (try to get hashes, and enforce them if successful)";

const char kExtensionContentVerificationEnforceStrict[] =
    "Enforce strict (hard fail if we can't get hashes)";

//  Built-in hotword detection display strings

const char kExperimentalHotwordHardwareName[] =
    "Simulated hardware 'Ok Google' features";

const char kExperimentalHotwordHardwareDescription[] =
    "Enables an experimental version of 'Ok Google' hotword detection "
    "features that have a hardware dependency.";

//  Message center strings

const char kMessageCenterAlwaysScrollUpUponRemovalName[] =
    "Experiments that message center always scroll up upon notification "
    "removal";

const char kMessageCenterAlwaysScrollUpUponRemovalDescription[] =
    "Enables experiment that message center always scroll up when a "
    "notification is removed.";

const char kCastStreamingHwEncodingName[] =
    "Cast Streaming hardware video encoding";

const char kCastStreamingHwEncodingDescription[] =
    "This option enables support in Cast Streaming for encoding video "
    "streams using platform hardware.";

const char kAllowInsecureLocalhostName[] =
    "Allow invalid certificates for resources loaded from localhost.";

const char kAllowInsecureLocalhostDescription[] =
    "Allows requests to localhost over HTTPS even when an invalid "
    "certificate is presented.";

#if defined(OS_WIN) || defined(OS_MACOSX)

//  Tab discarding

const char kAutomaticTabDiscardingName[] = "Automatic tab discarding";

const char kAutomaticTabDiscardingDescription[] =
    "If enabled, tabs get automatically discarded from memory when the "
    "system memory is low. Discarded tabs are still visible on the tab "
    "strip and get reloaded when clicked on. Info about discarded tabs can "
    "be found at chrome://discards.";

#endif  // defined(OS_WIN) || defined(OS_MACOSX)

#if defined(OS_ANDROID)

const char kOfflineBookmarksName[] = "Enable offline bookmarks";

const char kOfflineBookmarksDescription[] =
    "Enable saving bookmarked pages for offline viewing.";

const char kNtpOfflinePagesName[] = "Enable NTP offline pages";

const char kNtpOfflinePagesDescription[] =
    "Enables badging of offline pages on the New Tab page. Only relevant if "
    "offline pages are enabled.";

const char kOfflinePagesAsyncDownloadName[] =
    R"*(Enables showing "DOWNLOAD PAGE LATER" button in error pages.)*";

const char kOfflinePagesAsyncDownloadDescription[] =
    R"*(Enables showing "DOWNLOAD PAGE LATER" button in error pages such )*"
    R"*(that the user can click on it to download the page later.)*";

const char kOfflinePagesSvelteConcurrentLoadingName[] =
    "Enables concurrent background loading on svelte.";

const char kOfflinePagesSvelteConcurrentLoadingDescription[] =
    "Enables concurrent background loading (or downloading) of pages on "
    "Android svelte (512MB RAM) devices. Otherwise, background loading will "
    "happen when the svelte device is idle.";

const char kOfflinePagesLoadSignalCollectingName[] =
    "Enables collecting load timing data for offline page snapshots.";

const char kOfflinePagesLoadSignalCollectingDescription[] =
    "Enables loading completeness data collection while writing an offline "
    "page.  This data is collected in the snapshotted offline page to allow "
    "data analysis to improve deciding when to make the offline snapshot.";

const char kOfflinePagesPrefetchingName[] =
    "Enables suggested offline pages to be prefetched.";

const char kOfflinePagesPrefetchingDescription[] =
    "Enables suggested offline pages to be prefetched, so useful content is "
    "available while offline.";

const char kOfflinePagesSharingName[] = "Enables offline pages to be shared.";

const char kOfflinePagesSharingDescription[] =
    "Enables the saved offline pages to be shared via other applications.";

const char kBackgroundLoaderForDownloadsName[] =
    "Enables background downloading of pages.";

const char kBackgroundLoaderForDownloadsDescription[] =
    "Enables downloading pages in the background in case page is not yet "
    "loaded in current tab.";

const char kNewBackgroundLoaderName[] =
    "Use background loader instead of prerenderer to load pages.";

const char kNewBackgroundLoaderDescription[] =
    "Use background loader instead of prerenderer to asynchronously "
    "download pages.";

const char kNtpPopularSitesName[] = "Show popular sites on the New Tab page";

const char kNtpPopularSitesDescription[] =
    "Pre-populate the New Tab page with popular sites.";

const char kNtpSwitchToExistingTabName[] =
    "Switch to an existing tab for New Tab Page suggestions.";

const char kNtpSwitchToExistingTabDescription[] =
    "When opening a suggested webpage from the New Tab Page, if a tab is "
    "already open for the suggestion, switch to that one instead of loading "
    "the suggestion in the new tab.";

const char kNtpSwitchToExistingTabMatchUrl[] = "Match by URL";

const char kNtpSwitchToExistingTabMatchHost[] = "Match by Hostname";

const char kUseAndroidMidiApiName[] = "Use Android Midi API";

const char kUseAndroidMidiApiDescription[] =
    "Use Android Midi API for WebMIDI (effective only with Android M+ "
    "devices).";

const char kWebPaymentsModifiersName[] = "Enable web payment modifiers";

const char kWebPaymentsModifiersDescription[] =
    "If the website provides modifiers in the payment request, show the "
    "custom total for each payment instrument, update the shopping cart "
    "when instruments are switched, and send modified payment method "
    "specific data to the payment app.";

const char kXGEOVisibleNetworksName[] = "Enable XGEO Visible Networks";

const char kXGEOVisibleNetworksDescription[] =
    "If location permissions are granted, include visible networks in the XGEO "
    "Header for omnibox queries. This will only happen if location is not "
    "fresh or not available (for example, due to a cold start).";

#endif  // defined(OS_ANDROID)

#if defined(OS_WIN)

//  Exporting tracing events to ETW

const char kTraceExportEventsToEtwName[] =
    "Enable exporting of tracing events to ETW.";

const char kTraceExportEventsToEtwDesription[] =
    "If enabled, trace events will be exported to the Event Tracing for "
    "Windows (ETW) and can then be captured by tools such as UIForETW or "
    "Xperf.";

const char kMergeKeyCharEventsName[] =
    "Enable or disable merging merging the key event (WM_KEY*) with char "
    "event (WM_CHAR).";

const char kMergeKeyCharEventsDescription[] =
    "If disabled, Chrome will handle WM_KEY* and WM_CHAR separatedly.";

const char kUseWinrtMidiApiName[] = "Use Windows Runtime MIDI API";

const char kUseWinrtMidiApiDescription[] =
    "Use Windows Runtime MIDI API for WebMIDI (effective only on Windows 10 "
    "or later).";

#endif  // defined(OS_WIN)

#if defined(OS_ANDROID)

//  Data Use

//  Update Menu Item Flags

const char kUpdateMenuItemName[] = "Force show update menu item";

const char kUpdateMenuItemDescription[] =
    R"*(When enabled, an "Update Chrome" item will be shown in the app )*"
    R"*(menu.)*";

const char kUpdateMenuItemCustomSummaryDescription[] =
    "When this flag and the force show update menu item flag are enabled, a "
    "custom summary string will be displayed below the update menu item.";

const char kUpdateMenuItemCustomSummaryName[] =
    "Update menu item custom summary";

const char kUpdateMenuBadgeName[] = "Force show update menu badge";

const char kUpdateMenuBadgeDescription[] =
    "When enabled, an update badge will be shown on the app menu button.";

const char kSetMarketUrlForTestingName[] = "Set market URL for testing";

const char kSetMarketUrlForTestingDescription[] =
    "When enabled, sets the market URL for use in testing the update menu "
    "item.";

#endif  // defined(OS_ANDROID)

#if defined(OS_ANDROID)

const char kHerbPrototypeChoicesName[] = "Switch preferred flavor of Herb";

const char kHerbPrototypeChoicesDescription[] =
    "Switching this option changes which tab management prototype is being "
    "tested.";

const char kHerbPrototypeFlavorElderberry[] =
    "ELDERBERRY: All View Intents in CCT v2";

const char kEnableSpecialLocaleName[] =
    "Enable custom logic for special locales.";

const char kEnableSpecialLocaleDescription[] =
    "Enable custom logic for special locales. In this mode, Chrome might "
    "behave differently in some locales.";

//  WebApks

const char kEnableWebapk[] = "Enable improved add to Home screen";

const char kEnableWebapkDescription[] =
    R"*(Packages "Progressive Web Apps" so that they can integrate more )*"
    R"*(deeply with Android. A Chrome server is used to package sites. In )*"
    R"*(Chrome Canary and Chrome Dev, this requires “Untrusted )*"
    R"*(sources” to be enabled in Android security settings.)*";

#endif  // defined(OS_ANDROID)

const char kEnableBrotliName[] = "Brotli Content-Encoding.";

const char kEnableBrotliDescription[] =
    "Enable Brotli Content-Encoding support.";

const char kEnableWebfontsInterventionName[] =
    "New version of User Agent Intervention for WebFonts loading.";

const char kEnableWebfontsInterventionDescription[] =
    "Enable New version of User Agent Intervention for WebFonts loading.";

const char kEnableWebfontsInterventionV2ChoiceDefault[] = "Default";

const char kEnableWebfontsInterventionV2ChoiceEnabledWith2g[] = "Enabled: 2G";

const char kEnableWebfontsInterventionV2ChoiceEnabledWith3g[] = "Enabled: 3G";

const char kEnableWebfontsInterventionV2ChoiceEnabledWithSlow2g[] =
    "Enabled: Slow 2G";

const char kEnableWebfontsInterventionV2ChoiceDisabled[] = "Disabled";

const char kEnableWebfontsInterventionTriggerName[] =
    "Trigger User Agent Intervention for WebFonts loading always.";

const char kEnableWebfontsInterventionTriggerDescription[] =
    "Enable to trigger User Agent Intervention for WebFonts loading always. "
    "This flag affects only when the intervention is enabled.";

const char kEnableScrollAnchoringName[] = "Scroll Anchoring";

const char kEnableScrollAnchoringDescription[] =
    "Adjusts scroll position to prevent visible jumps when offscreen "
    "content changes.";

#if defined(OS_CHROMEOS)

const char kDisableNativeCupsName[] = "Native CUPS";

const char kDisableNativeCupsDescription[] =
    "Disable the use of the native CUPS printing backend.";

const char kEnableAndroidWallpapersAppName[] = "Android Wallpapers App";

const char kEnableAndroidWallpapersAppDescription[] =
    "Enables the Android Wallpapers App as the default Wallpaper App on "
    "Chrome OS.";

const char kEnableTouchSupportForScreenMagnifierName[] =
    "Touch support for screen magnifier";

const char kEnableTouchSupportForScreenMagnifierDescription[] =
    "Enables touch support for screen magnifier";

const char kEnableZipArchiverOnFileManagerName[] =
    "ZIP archiver for Drive";

const char kEnableZipArchiverOnFileManagerDescription[] =
    "Enable the ability to archive and unpack files on Drive in the Files app";

const char kCrOSComponentName[] = "Chrome OS Component";

const char kCrOSComponentDescription[] =
    "Enable the use of Chrome OS Component to download Chrome OS features "
    "on-demand as components.";

#endif  // defined(OS_CHROMEOS)

#if defined(OS_ANDROID)

const char kContentSuggestionsCategoryOrderName[] =
    "Default content suggestions category order (e.g. on NTP)";

const char kContentSuggestionsCategoryOrderDescription[] =
    "Set default order of content suggestion categories (e.g. on the NTP).";

const char kContentSuggestionsCategoryRankerName[] =
    "Content suggestions category ranker (e.g. on NTP)";

const char kContentSuggestionsCategoryRankerDescription[] =
    "Set category ranker to order categories of content suggestions (e.g. "
    "on the NTP).";

const char kEnableNtpSnippetsVisibilityName[] =
    "Make New Tab Page Snippets more visible.";

const char kEnableNtpSnippetsVisibilityDescription[] =
    "If enabled, the NTP snippets will become more discoverable with a "
    "larger portion of the first card above the fold.";

const char kEnableContentSuggestionsNewFaviconServerName[] =
    "Get favicons for content suggestions from a new server.";

const char kEnableContentSuggestionsNewFaviconServerDescription[] =
    "If enabled, the content suggestions (on the NTP) will get favicons from a "
    "new favicon server.";

const char kEnableFaviconsFromWebManifestName[] =
    "Load favicons from Web Manifests";

const char kEnableFaviconsFromWebManifestDescription[] =
    "Fetch Web Manifests on page load to read favicons from them.";

const char kEnableNtpMostLikelyFaviconsFromServerName[] =
    "Download favicons for NTP tiles from Google.";

const char kEnableNtpMostLikelyFaviconsFromServerDescription[] =
    "If enabled, missing favicons for NTP tiles get downloaded from Google. "
    "This only applies to tiles that originate from synced history.";

const char kEnableContentSuggestionsSettingsName[] =
    "Show content suggestions settings.";

const char kEnableContentSuggestionsSettingsDescription[] =
    "If enabled, the content suggestions settings will be available from the "
    "main settings menu.";

const char kEnableContentSuggestionsShowSummaryName[] =
    "Show content suggestions summaries.";

const char kEnableContentSuggestionsShowSummaryDescription[] =
    "If enabled, the content suggestions summaries will be shown.";

const char kEnableNtpRemoteSuggestionsName[] =
    "Show server-side suggestions on the New Tab page";

const char kEnableNtpRemoteSuggestionsDescription[] =
    "If enabled, the list of content suggestions on the New Tab page (see "
    "#enable-ntp-snippets) will contain server-side suggestions (e.g., "
    "Articles for you). Furthermore, it allows to override the source used "
    "to retrieve these server-side suggestions.";

const char kEnableNtpRecentOfflineTabSuggestionsName[] =
    "Show recent offline tabs on the New Tab page";

const char kEnableNtpRecentOfflineTabSuggestionsDescription[] =
    "If enabled, the list of content suggestions on the New Tab page (see "
    "#enable-ntp-snippets) will contain pages that were captured offline "
    "during browsing (see #offlining-recent-pages)";

const char kEnableNtpAssetDownloadSuggestionsName[] =
    "Show asset downloads on the New Tab page";

const char kEnableNtpAssetDownloadSuggestionsDescription[] =
    "If enabled, the list of content suggestions on the New Tab page (see "
    "#enable-ntp-snippets) will contain assets (e.g. books, pictures, "
    "audio) that the user downloaded for later use.";

const char kEnableNtpOfflinePageDownloadSuggestionsName[] =
    "Show offline page downloads on the New Tab page";

const char kEnableNtpOfflinePageDownloadSuggestionsDescription[] =
    "If enabled, the list of content suggestions on the New Tab page (see "
    "#enable-ntp-snippets) will contain pages that the user downloaded for "
    "later use.";

const char kEnableNtpBookmarkSuggestionsName[] =
    "Show recently visited bookmarks on the New Tab page";

const char kEnableNtpBookmarkSuggestionsDescription[] =
    "If enabled, the list of content suggestions on the New Tab page (see "
    "#enable-ntp-snippets) will contain recently visited bookmarks.";

const char kEnableNtpPhysicalWebPageSuggestionsName[] =
    "Show Physical Web pages on the New Tab page";

const char kEnableNtpPhysicalWebPageSuggestionsDescription[] =
    "If enabled, the list of content suggestions on the New Tab page (see "
    "#enable-ntp-snippets) will contain pages that are available through "
    "Physical Web (see #enable-physical-web)";

const char kEnableNtpForeignSessionsSuggestionsName[] =
    "Show recent foreign tabs on the New Tab page";

const char kEnableNtpForeignSessionsSuggestionsDescription[] =
    "If enabled, the list of content suggestions on the New Tab page (see "
    "#enable-ntp-snippets) will contain recent foreign tabs.";

const char kEnableNtpSuggestionsNotificationsName[] =
    "Notify about new content suggestions available at the New Tab page";

const char kEnableNtpSuggestionsNotificationsDescription[] =
    "If enabled, notifications will inform about new content suggestions on "
    "the New Tab page (see #enable-ntp-snippets).";

const char kNtpCondensedLayoutName[] = "Condensed NTP layout";

const char kNtpCondensedLayoutDescription[] =
    "Show a condensed layout on the New Tab Page.";

const char kNtpCondensedTileLayoutName[] = "Condensed NTP tile layout";

const char kNtpCondensedTileLayoutDescription[] =
    "Show a condensed tile layout on the New Tab Page.";

const char kNtpGoogleGInOmniboxName[] = "Google G in New Tab Page omnibox";

const char kNtpGoogleGInOmniboxDescription[] =
    "Show a Google G in the omnibox on the New Tab Page.";

#endif  // defined(OS_ANDROID)

#if defined(OS_ANDROID)

const char kOffliningRecentPagesName[] =
    "Enable offlining of recently visited pages";

const char kOffliningRecentPagesDescription[] =
    "Enable storing recently visited pages locally for offline use. "
    "Requires Offline Pages to be enabled.";

const char kOfflinePagesCtName[] = "Enable Offline Pages CT features.";

const char kOfflinePagesCtDescription[] = "Enable Offline Pages CT features.";

#endif  // defined(OS_ANDROID)

#if defined(OS_ANDROID)

const char kEnableExpandedAutofillCreditCardPopupLayoutName[] =
    "Use expanded autofill credit card popup layout.";

const char kEnableExpandedAutofillCreditCardPopupLayoutDescription[] =
    "If enabled, displays autofill credit card popup using expanded "
    "layout.";

#endif  // defined(OS_ANDROID)

const char kEnableAutofillCreditCardLastUsedDateDisplayName[] =
    "Display the last used date of a credit card in autofill.";

const char kEnableAutofillCreditCardLastUsedDateDisplayDescription[] =
    "If enabled, display the last used date of a credit card in autofill.";

const char kEnableAutofillCreditCardUploadCvcPromptName[] =
    "Enable requesting missing CVC during Autofill credit card upload";

const char kEnableAutofillCreditCardUploadCvcPromptDescription[] =
    "If enabled, requests missing CVC when offering to upload credit cards to "
    "Google Payments.";

#if !defined(OS_ANDROID) && defined(GOOGLE_CHROME_BUILD)

const char kGoogleBrandedContextMenuName[] =
    "Google branding in the context menu";

const char kGoogleBrandedContextMenuDescription[] =
    "Shows a Google icon next to context menu items powered by Google "
    "services.";

#endif  // !defined(OS_ANDROID) && defined(GOOGLE_CHROME_BUILD)

const char kEnableWebUsbName[] = "WebUSB";

const char kEnableWebUsbDescription[] = "Enable WebUSB support.";

const char kEnableGenericSensorName[] = "Generic Sensor";

const char kEnableGenericSensorDescription[] =
    "Enable sensor APIs based on Generic Sensor API.";

const char kFontCacheScalingName[] = "FontCache scaling";

const char kFontCacheScalingDescription[] =
    "Reuse a cached font in the renderer to serve different sizes of font "
    "for faster layout.";

const char kFramebustingName[] =
    "Framebusting requires same-origin or a user gesture";

const char kFramebustingDescription[] =
    "Don't permit an iframe to navigate the top level browsing context "
    "unless they are same-origin or the iframe is processing a user "
    "gesture.";

const char kVibrateRequiresUserGestureName[] =
    "Requiring user gesture for the Vibration API";

const char kVibrateRequiresUserGestureDescription[] =
    "Block the Vibration API if no user gesture has been received on "
    "the frame or any embedded frame.";

#if defined(OS_ANDROID)
#if BUILDFLAG(ENABLE_VR)
const char kEnableVrShellName[] = "Enable Chrome VR.";

const char kEnableVrShellDescription[] =
    "Allow browsing with a VR headset if available for this device.";

const char kVrCustomTabBrowsingName[] = "Enable Custom Tab browsing in VR.";

const char kVrCustomTabBrowsingDescription[] =
    "Allow browsing with a VR headset in a Custom Tab if available for this "
    "device.";

const char kWebVrAutopresentName[] = "Enable WebVr auto presentation";

const char kWebVrAutopresentDescription[] =
    "Allows auto presentation of WebVr content from trusted first-party apps";
#endif  // BUILDFLAG(ENABLE_VR)
#endif  // defined(OS_ANDROID)

//  Web payments

const char kWebPaymentsName[] = "Web Payments";

const char kWebPaymentsDescription[] =
    "Enable Web Payments API integration, a JavaScript API for merchants.";

#if defined(OS_ANDROID)

const char kEnableAndroidPayIntegrationV1Name[] = "Enable Android Pay v1";

const char kEnableAndroidPayIntegrationV1Description[] =
    "Enable integration with Android Pay using the first version of the "
    "API";

const char kEnableAndroidPayIntegrationV2Name[] = "Enable Android Pay v2";

const char kEnableAndroidPayIntegrationV2Description[] =
    "Enable integration with Android Pay using the second version of the "
    "API";

const char kEnableWebPaymentsSingleAppUiSkipName[] =
    "Enable Web Payments single app UI skip";

const char kEnableWebPaymentsSingleAppUiSkipDescription[] =
    "Enable Web Payments to skip showing its UI if the developer specifies "
    "a single app.";

const char kAndroidPaymentAppsName[] = "Android payment apps";

const char kAndroidPaymentAppsDescription[] =
    "Enable third party Android apps to integrate as payment apps";

const char kServiceWorkerPaymentAppsName[] = "Service Worker payment apps";

const char kServiceWorkerPaymentAppsDescription[] =
    "Enable Service Worker applications to integrate as payment apps";

#endif  // defined(OS_ANDROID)

const char kFeaturePolicyName[] = "Feature Policy";

const char kFeaturePolicyDescription[] =
    "Enables granting and removing access to features through the "
    "Feature-Policy HTTP header.";

//  Audio rendering mixing experiment strings.

const char kNewAudioRenderingMixingStrategyName[] =
    "New audio rendering mixing strategy";

const char kNewAudioRenderingMixingStrategyDescription[] =
    "Use the new audio rendering mixing strategy.";

//  Background video track disabling experiment strings.

const char kBackgroundVideoTrackOptimizationName[] =
    "Optimize background video playback.";

const char kBackgroundVideoTrackOptimizationDescription[] =
    "Disable video tracks when the video is played in the background to "
    "optimize performance.";

//  New remote playback pipeline experiment strings.

const char kNewRemotePlaybackPipelineName[] =
    "Enable the new remote playback pipeline.";

const char kNewRemotePlaybackPipelineDescription[] =
    "Enable the new pipeline for playing media element remotely via "
    "RemotePlayback API or native controls.";

//  Video fullscreen with orientation lock experiment strings.

const char kVideoFullscreenOrientationLockName[] =
    "Lock screen orientation when playing a video fullscreen.";

const char kVideoFullscreenOrientationLockDescription[] =
    "Lock the screen orientation of the device to match video orientation "
    "when a video goes fullscreen. Only on phones.";

//  Video rotate-to-fullscreen experiment strings.

const char kVideoRotateToFullscreenName[] =
    "Rotate-to-fullscreen gesture for videos.";

const char kVideoRotateToFullscreenDescription[] =
    "Enter/exit fullscreen when device is rotated to/from the orientation of "
    "the video. Only on phones.";

//  Expensive background timer throttling flag

const char kExpensiveBackgroundTimerThrottlingName[] =
    "Throttle expensive background timers";

const char kExpensiveBackgroundTimerThrottlingDescription[] =
    "Enables intervention to limit CPU usage of background timers to 1%.";

//  Enable default MediaSession flag

#if !defined(OS_ANDROID)

const char kEnableAudioFocusName[] = "Manage audio focus across tabs";

const char kEnableAudioFocusDescription[] =
    "Manage audio focus across tabs to improve the audio mixing.";

const char kEnableAudioFocusDisabled[] = "Disabled";

const char kEnableAudioFocusEnabled[] = "Enabled";

const char kEnableAudioFocusEnabledDuckFlash[] =
    "Enabled (Flash lowers volume when interrupted by other sound, "
    "experimental)";

#endif  // !defined(OS_ANDROID)

#if defined(OS_WIN)

const char kGdiTextPrinting[] = "GDI Text Printing";

const char kGdiTextPrintingDescription[] =
    "Use GDI to print text as simply text";

#endif  // defined(OS_WIN)

#if defined(OS_ANDROID)

const char kModalPermissionPromptsName[] = "Modal Permission Prompts";

const char kModalPermissionPromptsDescription[] =
    "Whether to use permission dialogs in place of permission infobars.";

#endif  // defined(OS_ANDROID)

#if !defined(OS_MACOSX)

const char kPermissionPromptPersistenceToggleName[] =
    "Persistence Toggle in Permission Prompts";

const char kPermissionPromptPersistenceToggleDescription[] =
    "Whether to display a persistence toggle in permission prompts.";

#endif  // !defined(OS_MACOSX)

#if defined(OS_ANDROID)

const char kNoCreditCardAbort[] = "No Credit Card Abort";

const char kNoCreditCardAbortDescription[] =
    "Whether or not the No Credit Card Abort is enabled.";

#endif  // defined(OS_ANDROID)

//  Consistent omnibox geolocation

#if defined(OS_ANDROID)

const char kEnableConsistentOmniboxGeolocationName[] =
    "Have consistent omnibox geolocation access.";

const char kEnableConsistentOmniboxGeolocationDescription[] =
    "Have consistent geolocation access between the omnibox and default "
    "search engine.";

#endif  // defined(OS_ANDROID)

//  Media Remoting chrome://flags strings

const char kMediaRemotingName[] = "Media Remoting during Cast Tab Mirroring";

const char kMediaRemotingDescription[] =
    "When Casting a tab to a remote device, enabling this turns on an "
    "optimization that forwards the content bitstream directly to the "
    "remote device when a video is fullscreened.";

//  Play Services LSD permission prompt chrome://flags strings

#if defined(OS_ANDROID)

const char kLsdPermissionPromptName[] =
    "Location Settings Dialog Permission Prompt";

const char kLsdPermissionPromptDescription[] =
    "Whether to use the Google Play Services Location Settings Dialog "
    "permission dialog.";

#endif  // defined(OS_ANDROID)

#if defined(OS_WIN)

//  Custom draw the Windows 10 titlebar. crbug.com/505013

const char kWindows10CustomTitlebarName[] = "Custom-drawn Windows 10 Titlebar";

const char kWindows10CustomTitlebarDescription[] =
    "If enabled, Chrome will draw the titlebar and caption buttons instead "
    "of deferring to Windows.";

#endif  // defined(OS_WIN)

#if defined(OS_WIN)

const char kDisablePostscriptPrinting[] = "Disable PostScript Printing";

const char kDisablePostscriptPrintingDescription[] =
    "Disables PostScript generation when printing to PostScript capable "
    "printers, and uses EMF generation in its place.";

#endif  // defined(OS_WIN)

#if defined(OS_ANDROID)

const char kAiaFetchingName[] = "Intermediate Certificate Fetching";

const char kAiaFetchingDescription[] =
    "Enable intermediate certificate fetching when a server does not "
    "provide sufficient certificates to build a chain to a trusted root.";

#endif  // defined(OS_ANDROID)

//  Web MIDI supports MIDIManager dynamic instantiation chrome://flags strings

const char kEnableMidiManagerDynamicInstantiationName[] =
    "MIDIManager dynamic instantiation for Web MIDI.";

const char kEnableMidiManagerDynamicInstantiationDescription[] =
    "Enable MIDIManager dynamic instantiation for Web MIDI.";

//  Desktop iOS promotion chrome://flags strings

#if defined(OS_WIN)

const char kEnableDesktopIosPromotionsName[] = "Desktop to iOS promotions.";

const char kEnableDesktopIosPromotionsDescription[] =
    "Enable Desktop to iOS promotions, and allow users to see them if they "
    "are eligible.";

#endif  // defined(OS_WIN)

#if defined(OS_ANDROID)

const char kEnableCustomFeedbackUiName[] = "Enable Custom Feedback UI";

const char kEnableCustomFeedbackUiDescription[] =
    "Enables a custom feedback UI when submitting feedback through Google "
    "Feedback. Works with Google Play Services v10.2+";

#endif  // defined(OS_ANDROID)

#if defined(OS_CHROMEOS) || defined(OS_LINUX) || defined(OS_MACOSX) || \
    defined(OS_WIN)

const char kOmniboxEntitySuggestionsName[] = "Omnibox entity suggestions";

const char kOmniboxEntitySuggestionsDescription[] =
    "Enable receiving entity suggestions in Omnibox.";

const char kPauseBackgroundTabsName[] = "Pause background tabs";
const char kPauseBackgroundTabsDescription[] =
    "Pause timers in background tabs after 5 minutes on desktop.";

const char kEnableNewAppMenuIconName[] = "Enable the New App Menu Icon";
const char kEnableNewAppMenuIconDescription[] =
    "Use the new app menu icon with update notification animations.";

#endif  // defined(OS_CHROMEOS) || defined(OS_LINUX) || defined(OS_MACOSX) ||
        // defined(OS_WIN)

#if defined(OS_CHROMEOS)

const char kEnableChromevoxArcSupportName[] = "ChromeVox ARC support";

const char kEnableChromevoxArcSupportDescription[] =
    "Enable ChromeVox screen reader features in ARC";

#endif  // defined(OS_CHROMEOS)

const char kMojoLoadingName[] = "Use Mojo IPC for resource loading";

const char kMojoLoadingDescription[] =
    "Use Mojo IPC instead of traditional Chrome IPC for resource loading.";

#if defined(OS_ANDROID)

const char kUseNewDoodleApiName[] = "Use new Doodle API";

const char kUseNewDoodleApiDescription[] =
    "Enables the new API to fetch Doodles for the NTP.";

#endif  // defined(OS_ANDROID)

const char kDebugShortcutsDescription[] =
    "Enables additional keyboard shortcuts that are useful for debugging "
    "Ash.";

const char kMemoryAblationName[] = "Memory ablation experiment";
const char kMemoryAblationDescription[] =
    "Allocates extra memory in the browser process.";

#if defined(OS_ANDROID)

const char kEnableCustomContextMenuName[] = "Enable custom context menu";

const char kEnableCustomContextMenuDescription[] =
    "Enables a new context menu when a link, image, or video is pressed within "
    "Chrome.";

#endif  // defined(OS_ANDROID)

#if defined(OS_CHROMEOS)

//  File Manager

const char kVideoPlayerChromecastSupportName[] =
    "Experimental Chromecast support for Video Player";

const char kVideoPlayerChromecastSupportDescription[] =
    "This option enables experimental Chromecast support for Video Player "
    "app on ChromeOS.";

const char kNewZipUnpackerName[] = "New ZIP unpacker";

const char kNewZipUnpackerDescription[] =
    "New ZIP unpacker flow, based on the File System Provider API.";

const char kShowArcFilesAppName[] = "Show Android Files app";

const char kShowArcFilesAppDescription[] =
    "Show Android Files app in Chrome OS launcher. This is only effective "
    "on a device with access to Play Store.";

const char kOfficeEditingComponentAppName[] =
    "Office Editing for Docs, Sheets & Slides";

const char kOfficeEditingComponentAppDescription[] =
    "Office Editing for Docs, Sheets & Slides for testing purposes.";

const char kDisplayColorCalibrationName[] = "Color calibration of the display";

const char kDisplayColorCalibrationDescription[] =
    "Allow color calibration of the display if the display supports the "
    "feature.";

const char kMemoryPressureThresholdName[] =
    "Memory discard strategy for advanced pressure handling";

const char kMemoryPressureThresholdDescription[] =
    "Memory discarding strategy to use";

const char kConservativeThresholds[] =
    "Conservative memory pressure release strategy";

const char kAggressiveCacheDiscardThresholds[] =
    "Aggressive cache release strategy";

const char kAggressiveTabDiscardThresholds[] =
    "Aggressive tab release strategy";

const char kAggressiveThresholds[] =
    "Aggressive tab and cache release strategy";

const char kWakeOnPacketsName[] = "Wake On Packets";

const char kWakeOnPacketsDescription[] =
    "Enables waking the device based on the receipt of some network "
    "packets.";

const char kQuickUnlockPinName[] = "Quick Unlock (PIN)";

const char kQuickUnlockPinDescription[] =
    "Enabling PIN quick unlock allows you to use a PIN to unlock your ChromeOS "
    "device on the lock screen after you have signed into your device.";

const char kQuickUnlockPinSignin[] = "Enable PIN when logging in.";

const char kQuickUnlockPinSigninDescription[] =
    "Enabling PIN allows you to use a PIN to sign in and unlock your ChromeOS "
    "device. After changing this flag PIN needs to be set up again.";

const char kQuickUnlockFingerprint[] = "Quick Unlock (Fingerprint)";

const char kQuickUnlockFingerprintDescription[] =
    "Enabling fingerprint quick unlock allows you to setup and use a "
    "fingerprint to unlock your Chromebook on the lock screen after you "
    "have signed into your device.";

const char kExperimentalAccessibilityFeaturesName[] =
    "Experimental accessibility features";

const char kExperimentalAccessibilityFeaturesDescription[] =
    "Enable additional accessibility features in the Settings page.";

const char kDisableSystemTimezoneAutomaticDetectionName[] =
    "SystemTimezoneAutomaticDetection policy support";

const char kDisableSystemTimezoneAutomaticDetectionDescription[] =
    "Disable system timezone automatic detection device policy.";

const char kEolNotificationName[] = "Disable Device End of Life notification.";

const char kEolNotificationDescription[] =
    "Disable Notifcation when Device is End of Life.";

//  Stylus strings

const char kForceEnableStylusToolsName[] = "Force enable stylus features";

const char kForceEnableStylusToolsDescription[] =
    "Forces display of the stylus tools menu in the shelf and the stylus "
    "section in settings, even if there is no attached stylus device.";

//  Network portal notification

const char kNetworkPortalNotificationName[] =
    "Notifications about captive portals";

const char kNetworkPortalNotificationDescription[] =
    "If enabled, notification is displayed when device is connected to a "
    "network behind captive portal.";

const char kMtpWriteSupportName[] = "MTP write support";

const char kMtpWriteSupportDescription[] =
    "MTP write support in File System API (and file manager). In-place "
    "editing operations are not supported.";

const char kCrosRegionsModeName[] = "Cros-regions load mode";

const char kCrosRegionsModeDescription[] =
    "This flag controls cros-regions load mode";

const char kCrosRegionsModeDefault[] = "Default";

const char kCrosRegionsModeOverride[] = "Override VPD values.";

const char kCrosRegionsModeHide[] = "Hide VPD values.";

const char kPrinterProviderSearchAppName[] =
    "Chrome Web Store Gallery app for printer drivers";

const char kPrinterProviderSearchAppDescription[] =
    "Enables Chrome Web Store Gallery app for printer drivers. The app "
    "searches Chrome Web Store for extensions that support printing to a "
    "USB printer with specific USB ID.";

const char kArcBootCompleted[] = "Load Android apps automatically";

const char kArcBootCompletedDescription[] =
    "Allow Android apps to start automatically after signing in.";

const char kEnableImeMenuName[] = "Enable opt-in IME menu";

const char kEnableImeMenuDescription[] =
    "Enable access to the new IME menu in the Language Settings page.";

const char kEnableEhvInputName[] =
    "Emoji, handwriting and voice input on opt-in IME menu";

const char kEnableEhvInputDescription[] =
    "Enable access to emoji, handwriting and voice input form opt-in IME "
    "menu.";

const char kEnableEncryptionMigrationName[] =
    "Enable encryption migration of user data";

const char kEnableEncryptionMigrationDescription[] =
    "If enabled and the device supports ARC, the user will be asked to update "
    "the encryption of user data when the user signs in.";

#endif  // #if defined(OS_CHROMEOS)

#if defined(OS_ANDROID)

const char kEnableCopylessPasteName[] = "App Indexing (Copyless Paste)";

const char kEnableCopylessPasteDescription[] =
    "Provide suggestions for text input, based on your recent context. For "
    "example, if you looked at a restaurant website and switched to the Maps "
    "app, the keyboard would offer the name of that restaurant as a suggestion "
    "to enter into the search bar. The data is indexed locally, and never sent "
    "to the server. It's disabled in incognito mode.";

const char kEnableWebNfcName[] = "WebNFC";

const char kEnableWebNfcDescription[] = "Enable WebNFC support.";

#endif  // defined(OS_ANDROID)

const char kEnableIdleTimeSpellCheckingName[] =
    "Enable idle time spell checker";

const char kEnableIdleTimeSpellCheckingDescription[] =
    "Make spell-checking code run only when the browser is idle, so that input "
    "latency is reduced, especially when editing long articles, emails, etc.";

#if defined(OS_ANDROID)

const char kEnableOmniboxClipboardProviderName[] =
    "Omnibox clipboard URL suggestions";

const char kEnableOmniboxClipboardProviderDescription[] =
    "Provide a suggestion of the URL stored in the clipboard (if any) upon "
    "focus in the omnibox.";

#endif  // defined(OS_ANDROID)

const char kAutoplayPolicyName[] = "Autoplay policy";

const char kAutoplayPolicyDescription[] =
    "Policy used when deciding if audio or video is allowed to autoplay.";

const char kAutoplayPolicyNoUserGestureRequired[] =
    "No user gesture is required.";

const char kAutoplayPolicyUserGestureRequired[] = "User gesture is required.";

const char kAutoplayPolicyUserGestureRequiredForCrossOrigin[] =
    "User gesture is required for cross-origin iframes.";

const char kOmniboxDisplayTitleForCurrentUrlName[] =
    "Include title for the current URL in the omnibox";

const char kOmniboxDisplayTitleForCurrentUrlDescription[] =
    "In the event that the omnibox provides suggestions on-focus, the URL of "
    "the current page is provided as the first suggestion without a title. "
    "Enabling this flag causes the title to be displayed.";

const char kOmniboxUIMaxAutocompleteMatchesName[] =
    "Omnibox UI Max Autocomplete Matches";

const char kOmniboxUIMaxAutocompleteMatchesDescription[] =
    "Changes the maximum number af autocomplete matches displayed in the "
    "Omnibox UI.";

const char kOmniboxUIVerticalMarginName[] = "Omnibox UI Vertical Margin";

const char kOmniboxUIVerticalMarginDescription[] =
    "Changes the vertical margin in the Omnibox UI.";

const char kForceEffectiveConnectionTypeName[] =
    "Override effective connection type";

const char kForceEffectiveConnectionTypeDescription[] =
    "Overrides the effective connection type of the current connection "
    "returned by the network quality estimator.";

const char kEffectiveConnectionTypeUnknownDescription[] = "Unknown";
const char kEffectiveConnectionTypeOfflineDescription[] = "Offline";
const char kEffectiveConnectionTypeSlow2GDescription[] = "Slow 2G";
const char kEffectiveConnectionType2GDescription[] = "2G";
const char kEffectiveConnectionType3GDescription[] = "3G";
const char kEffectiveConnectionType4GDescription[] = "4G";

const char kEnableHeapProfilingName[] = "Heap profiling";

const char kEnableHeapProfilingDescription[] = "Enables heap profiling.";

const char kEnableHeapProfilingModePseudo[] = "Enabled (pseudo mode)";

const char kEnableHeapProfilingModeNative[] = "Enabled (native mode)";

const char kEnableHeapProfilingTaskProfiler[] = "Enabled (task mode)";

const char kUseSuggestionsEvenIfFewFeatureName[] =
    "Disable minimum for server-side tile suggestions on NTP.";

const char kUseSuggestionsEvenIfFewFeatureDescription[] =
    "Request server-side suggestions even if there are only very few of them "
    "and use them for tiles on the New Tab Page.";

const char kLocationHardReloadName[] =
    "Experimental change for Location.reload() to trigger a hard-reload.";

const char kLocationHardReloadDescription[] =
    "Enable an experimental change for Location.reload() to trigger a "
    "hard-reload.";

const char kCaptureThumbnailOnLoadFinishedName[] =
    "Capture page thumbnail on load finished";

const char kCaptureThumbnailOnLoadFinishedDescription[] =
    "Capture a page thumbnail (for use on the New Tab page) when the page load "
    "finishes, in addition to other times a thumbnail may be captured.";

#if defined(OS_WIN)

// Name and description of the flag that enables D3D v-sync.
const char kEnableD3DVsync[] = "D3D v-sync";
const char kEnableD3DVsyncDescription[] =
    "Produces v-sync signal by having D3D wait for vertical blanking interval "
    "to occur.";

#endif  // defined(OS_WIN)

#if !defined(OS_ANDROID) && !defined(OS_IOS)

const char kUseGoogleLocalNtpName[] = "Enable using the Google local NTP";
const char kUseGoogleLocalNtpDescription[] =
    "Use the local New Tab page if Google is the default search engine.";

const char kOneGoogleBarOnLocalNtpName[] =
    "Enable the OneGoogleBar on the local NTP";
const char kOneGoogleBarOnLocalNtpDescription[] =
    "Show a OneGoogleBar on the local New Tab page if Google is the default "
    "search engine.";

#endif  // !defined(OS_ANDROID) && !defined(OS_IOS)

#if defined(OS_MACOSX)
extern const char kMacRTLName[] = "Enable RTL";
extern const char kMacRTLDescription[] =
    "Mirrors the UI for RTL language users";
#endif

#if defined(OS_CHROMEOS)

const char kDisableNewVirtualKeyboardBehaviorName[] =
    "New window behavior for the accessibility keyboard";
const char kDisableNewVirtualKeyboardBehaviorDescription[] =
    "Disable new window behavior for the accessibility keyboard "
    "in non-sticky mode (do not change work area in non-sticky mode).";

#endif  // defined(OS_CHROMEOS)

}  // namespace flag_descriptions
