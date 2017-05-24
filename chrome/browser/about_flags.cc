// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/about_flags.h"

#include <iterator>
#include <map>
#include <set>
#include <utility>

#include "base/bind.h"
#include "base/callback.h"
#include "base/command_line.h"
#include "base/feature_list.h"
#include "base/i18n/base_i18n_switches.h"
#include "base/macros.h"
#include "base/memory/singleton.h"
#include "base/metrics/histogram_macros.h"
#include "base/metrics/metrics_hashes.h"
#include "base/stl_util.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task_scheduler/switches.h"
#include "base/values.h"
#include "build/build_config.h"
#include "cc/base/switches.h"
#include "chrome/browser/experiments/memory_ablation_experiment.h"
#include "chrome/browser/flag_descriptions.h"
#include "chrome/browser/predictors/resource_prefetch_common.h"
#include "chrome/browser/prerender/prerender_field_trial.h"
#include "chrome/common/channel_info.h"
#include "chrome/common/chrome_content_client.h"
#include "chrome/common/chrome_features.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/features.h"
#include "chrome/common/pause_tabs_field_trial.h"
#include "chrome/grit/chromium_strings.h"
#include "components/autofill/core/browser/autofill_experiments.h"
#include "components/autofill/core/common/autofill_switches.h"
#include "components/autofill/core/common/autofill_util.h"
#include "components/browser_sync/browser_sync_switches.h"
#include "components/cloud_devices/common/cloud_devices_switches.h"
#include "components/data_reduction_proxy/core/common/data_reduction_proxy_features.h"
#include "components/data_reduction_proxy/core/common/data_reduction_proxy_switches.h"
#include "components/dom_distiller/core/dom_distiller_switches.h"
#include "components/error_page/common/error_page_switches.h"
#include "components/favicon/core/features.h"
#include "components/flags_ui/feature_entry.h"
#include "components/flags_ui/feature_entry_macros.h"
#include "components/flags_ui/flags_storage.h"
#include "components/flags_ui/flags_ui_switches.h"
#include "components/nacl/common/nacl_switches.h"
#include "components/ntp_snippets/features.h"
#include "components/ntp_snippets/ntp_snippets_constants.h"
#include "components/ntp_tiles/constants.h"
#include "components/ntp_tiles/switches.h"
#include "components/offline_pages/core/offline_page_feature.h"
#include "components/omnibox/browser/omnibox_field_trial.h"
#include "components/omnibox/browser/omnibox_switches.h"
#include "components/password_manager/core/common/password_manager_features.h"
#include "components/previews/core/previews_features.h"
#include "components/proximity_auth/switches.h"
#include "components/security_state/core/security_state.h"
#include "components/security_state/core/switches.h"
#include "components/signin/core/common/signin_switches.h"
#include "components/spellcheck/common/spellcheck_features.h"
#include "components/spellcheck/common/spellcheck_switches.h"
#include "components/spellcheck/spellcheck_build_features.h"
#include "components/ssl_config/ssl_config_switches.h"
#include "components/strings/grit/components_strings.h"
#include "components/suggestions/features.h"
#include "components/sync/driver/sync_driver_switches.h"
#include "components/tracing/common/tracing_switches.h"
#include "components/translate/core/browser/translate_infobar_delegate.h"
#include "components/translate/core/browser/translate_manager.h"
#include "components/translate/core/browser/translate_prefs.h"
#include "components/version_info/version_info.h"
#include "content/public/common/content_features.h"
#include "content/public/common/content_switches.h"
#include "content/public/common/feature_h264_with_openh264_ffmpeg.h"
#include "content/public/common/features.h"
#include "device/base/features.h"
#include "device/vr/features/features.h"
#include "extensions/features/features.h"
#include "gin/public/gin_features.h"
#include "google_apis/drive/drive_switches.h"
#include "gpu/config/gpu_switches.h"
#include "media/audio/audio_features.h"
#include "media/base/media_switches.h"
#include "media/media_features.h"
#include "media/midi/midi_switches.h"
#include "net/cert/cert_verify_proc_android.h"
#include "net/nqe/effective_connection_type.h"
#include "ppapi/features/features.h"
#include "printing/features/features.h"
#include "services/device/public/cpp/device_features.h"
#include "ui/base/ui_base_switches.h"
#include "ui/compositor/compositor_switches.h"
#include "ui/display/display_switches.h"
#include "ui/events/event_switches.h"
#include "ui/gfx/switches.h"
#include "ui/gl/gl_features.h"
#include "ui/gl/gl_switches.h"
#include "ui/keyboard/keyboard_switches.h"
#include "ui/native_theme/native_theme_features.h"
#include "ui/views/views_switches.h"

#if defined(OS_ANDROID)
#include "chrome/browser/android/chrome_feature_list.h"
#include "components/feature_engagement_tracker/public/feature_constants.h"
#else  // OS_ANDROID
#include "ui/message_center/message_center_switches.h"
#endif  // OS_ANDROID

#if defined(OS_CHROMEOS)
#include "chromeos/chromeos_switches.h"
#include "components/arc/arc_features.h"
#include "third_party/cros_system_api/switches/chrome_switches.h"
#endif  // OS_CHROMEOS

#if defined(OS_MACOSX)
#include "chrome/browser/ui/browser_dialogs.h"
#endif  // OS_MACOSX

#if BUILDFLAG(ENABLE_APP_LIST)
#include "ui/app_list/app_list_switches.h"
#endif  // ENABLE_APP_LIST

#if BUILDFLAG(ENABLE_EXTENSIONS)
#include "extensions/common/switches.h"
#endif  // ENABLE_EXTENSIONS

#if defined(USE_ASH)
#include "ash/ash_switches.h"
#endif  // USE_ASH

#if defined(USE_OZONE)
#include "ui/ozone/public/ozone_switches.h"
#endif  // USE_OZONE

#if defined(OS_WIN)
#include "base/win/windows_version.h"
#endif  // OS_WIN

using flags_ui::FeatureEntry;
using flags_ui::kOsMac;
using flags_ui::kOsWin;
using flags_ui::kOsLinux;
using flags_ui::kOsCrOS;
using flags_ui::kOsAndroid;
using flags_ui::kOsCrOSOwnerOnly;

namespace about_flags {

namespace {

const unsigned kOsAll = kOsMac | kOsWin | kOsLinux | kOsCrOS | kOsAndroid;
const unsigned kOsDesktop = kOsMac | kOsWin | kOsLinux | kOsCrOS;

#if defined(USE_AURA)
const unsigned kOsAura = kOsWin | kOsLinux | kOsCrOS;
#endif  // USE_AURA

const FeatureEntry::Choice kTouchEventFeatureDetectionChoices[] = {
    {flags_ui::kGenericExperimentChoiceAutomatic, "", ""},
    {flags_ui::kGenericExperimentChoiceEnabled,
     switches::kTouchEventFeatureDetection,
     switches::kTouchEventFeatureDetectionEnabled},
    {flags_ui::kGenericExperimentChoiceDisabled,
     switches::kTouchEventFeatureDetection,
     switches::kTouchEventFeatureDetectionDisabled}};

#if defined(USE_AURA)
const FeatureEntry::Choice kOverscrollHistoryNavigationChoices[] = {
    {flags_ui::kGenericExperimentChoiceEnabled, "", ""},
    {flags_ui::kGenericExperimentChoiceDisabled,
     switches::kOverscrollHistoryNavigation, "0"},
    {flag_descriptions::kOverscrollHistoryNavigationSimpleUi,
     switches::kOverscrollHistoryNavigation, "2"}};

const FeatureEntry::Choice kOverscrollStartThresholdChoices[] = {
    {flags_ui::kGenericExperimentChoiceDefault, "", ""},
    {flag_descriptions::kOverscrollStartThreshold133Percent,
     switches::kOverscrollStartThreshold, "133"},
    {flag_descriptions::kOverscrollStartThreshold166Percent,
     switches::kOverscrollStartThreshold, "166"},
    {flag_descriptions::kOverscrollStartThreshold200Percent,
     switches::kOverscrollStartThreshold, "200"}};
#endif  // USE_AURA

const FeatureEntry::Choice kTouchTextSelectionStrategyChoices[] = {
    {flags_ui::kGenericExperimentChoiceDefault, "", ""},
    {flag_descriptions::kTouchSelectionStrategyCharacter,
     switches::kTouchTextSelectionStrategy, "character"},
    {flag_descriptions::kTouchSelectionStrategyDirection,
     switches::kTouchTextSelectionStrategy, "direction"}};

const FeatureEntry::Choice kTraceUploadURL[] = {
    {flags_ui::kGenericExperimentChoiceDisabled, "", ""},
    {flag_descriptions::kTraceUploadUrlChoiceOther, switches::kTraceUploadURL,
     "https://performance-insights.appspot.com/upload?tags=flags,Other"},
    {flag_descriptions::kTraceUploadUrlChoiceEmloading,
     switches::kTraceUploadURL,
     "https://performance-insights.appspot.com/upload?tags=flags,emloading"},
    {flag_descriptions::kTraceUploadUrlChoiceQa, switches::kTraceUploadURL,
     "https://performance-insights.appspot.com/upload?tags=flags,QA"},
    {flag_descriptions::kTraceUploadUrlChoiceTesting, switches::kTraceUploadURL,
     "https://performance-insights.appspot.com/upload?tags=flags,TestingTeam"}};

#if !defined(DISABLE_NACL)
const FeatureEntry::Choice kNaClDebugMaskChoices[] = {
    // Secure shell can be used on ChromeOS for forwarding the TCP port opened
    // by
    // debug stub to a remote machine. Since secure shell uses NaCl, we usually
    // want to avoid debugging that. The PNaCl translator is also a NaCl module,
    // so by default we want to avoid debugging that.
    // NOTE: As the default value must be the empty string, the mask excluding
    // the PNaCl translator and secure shell is substituted elsewhere.
    {flag_descriptions::kNaclDebugMaskChoiceExcludeUtilsPnacl, "", ""},
    {flag_descriptions::kNaclDebugMaskChoiceDebugAll, switches::kNaClDebugMask,
     "*://*"},
    {flag_descriptions::kNaclDebugMaskChoiceIncludeDebug,
     switches::kNaClDebugMask, "*://*/*debug.nmf"}};
#endif  // DISABLE_NACL

const FeatureEntry::Choice kPassiveListenersChoices[] = {
    {flags_ui::kGenericExperimentChoiceDefault, "", ""},
    {flag_descriptions::kPassiveEventListenerTrue,
     switches::kPassiveListenersDefault, "true"},
    {flag_descriptions::kPassiveEventListenerForceAllTrue,
     switches::kPassiveListenersDefault, "forcealltrue"},
};

const FeatureEntry::Choice kMarkHttpAsChoices[] = {
    {flags_ui::kGenericExperimentChoiceDefault, "", ""},
    {flag_descriptions::kMarkHttpAsDangerous,
     security_state::switches::kMarkHttpAs,
     security_state::switches::kMarkHttpAsDangerous}};

const FeatureEntry::Choice kDataReductionProxyLoFiChoices[] = {
    {flags_ui::kGenericExperimentChoiceDefault, "", ""},
    {flag_descriptions::kDataReductionProxyLoFiAlwaysOn,
     data_reduction_proxy::switches::kDataReductionProxyLoFi,
     data_reduction_proxy::switches::kDataReductionProxyLoFiValueAlwaysOn},
    {flag_descriptions::kDataReductionProxyLoFiCellularOnly,
     data_reduction_proxy::switches::kDataReductionProxyLoFi,
     data_reduction_proxy::switches::kDataReductionProxyLoFiValueCellularOnly},
    {flag_descriptions::kDataReductionProxyLoFiDisabled,
     data_reduction_proxy::switches::kDataReductionProxyLoFi,
     data_reduction_proxy::switches::kDataReductionProxyLoFiValueDisabled},
    {flag_descriptions::kDataReductionProxyLoFiSlowConnectionsOnly,
     data_reduction_proxy::switches::kDataReductionProxyLoFi,
     data_reduction_proxy::switches::
         kDataReductionProxyLoFiValueSlowConnectionsOnly}};

const FeatureEntry::Choice kShowSavedCopyChoices[] = {
    {flags_ui::kGenericExperimentChoiceDefault, "", ""},
    {flag_descriptions::kEnableShowSavedCopyPrimary,
     error_page::switches::kShowSavedCopy,
     error_page::switches::kEnableShowSavedCopyPrimary},
    {flag_descriptions::kEnableShowSavedCopySecondary,
     error_page::switches::kShowSavedCopy,
     error_page::switches::kEnableShowSavedCopySecondary},
    {flag_descriptions::kDisableShowSavedCopy,
     error_page::switches::kShowSavedCopy,
     error_page::switches::kDisableShowSavedCopy}};

const FeatureEntry::Choice kDefaultTileWidthChoices[] = {
    {flags_ui::kGenericExperimentChoiceDefault, "", ""},
    {flag_descriptions::kDefaultTileWidthShort, switches::kDefaultTileWidth,
     "128"},
    {flag_descriptions::kDefaultTileWidthTall, switches::kDefaultTileWidth,
     "256"},
    {flag_descriptions::kDefaultTileWidthGrande, switches::kDefaultTileWidth,
     "512"},
    {flag_descriptions::kDefaultTileWidthVenti, switches::kDefaultTileWidth,
     "1024"}};

const FeatureEntry::Choice kDefaultTileHeightChoices[] = {
    {flags_ui::kGenericExperimentChoiceDefault, "", ""},
    {flag_descriptions::kDefaultTileHeightShort, switches::kDefaultTileHeight,
     "128"},
    {flag_descriptions::kDefaultTileHeightTall, switches::kDefaultTileHeight,
     "256"},
    {flag_descriptions::kDefaultTileHeightGrande, switches::kDefaultTileHeight,
     "512"},
    {flag_descriptions::kDefaultTileHeightVenti, switches::kDefaultTileHeight,
     "1024"}};

const FeatureEntry::Choice kSimpleCacheBackendChoices[] = {
    {flags_ui::kGenericExperimentChoiceDefault, "", ""},
    {flags_ui::kGenericExperimentChoiceDisabled,
     switches::kUseSimpleCacheBackend, "off"},
    {flags_ui::kGenericExperimentChoiceEnabled,
     switches::kUseSimpleCacheBackend, "on"}};

#if defined(OS_ANDROID)
const FeatureEntry::Choice kReaderModeHeuristicsChoices[] = {
    {flags_ui::kGenericExperimentChoiceDefault, "", ""},
    {flag_descriptions::kReaderModeHeuristicsMarkup,
     switches::kReaderModeHeuristics,
     switches::reader_mode_heuristics::kOGArticle},
    {flag_descriptions::kReaderModeHeuristicsAdaboost,
     switches::kReaderModeHeuristics,
     switches::reader_mode_heuristics::kAdaBoost},
    {flag_descriptions::kReaderModeHeuristicsAlwaysOn,
     switches::kReaderModeHeuristics,
     switches::reader_mode_heuristics::kAlwaysTrue},
    {flag_descriptions::kReaderModeHeuristicsAlwaysOff,
     switches::kReaderModeHeuristics, switches::reader_mode_heuristics::kNone},
};
#endif  // OS_ANDROID

const FeatureEntry::Choice kNumRasterThreadsChoices[] = {
    {flags_ui::kGenericExperimentChoiceDefault, "", ""},
    {flag_descriptions::kNumRasterThreadsOne, switches::kNumRasterThreads, "1"},
    {flag_descriptions::kNumRasterThreadsTwo, switches::kNumRasterThreads, "2"},
    {flag_descriptions::kNumRasterThreadsThree, switches::kNumRasterThreads,
     "3"},
    {flag_descriptions::kNumRasterThreadsFour, switches::kNumRasterThreads,
     "4"}};

const FeatureEntry::Choice kGpuRasterizationMSAASampleCountChoices[] = {
    {flags_ui::kGenericExperimentChoiceDefault, "", ""},
    {flag_descriptions::kGpuRasterizationMsaaSampleCountZero,
     switches::kGpuRasterizationMSAASampleCount, "0"},
    {flag_descriptions::kGpuRasterizationMsaaSampleCountTwo,
     switches::kGpuRasterizationMSAASampleCount, "2"},
    {flag_descriptions::kGpuRasterizationMsaaSampleCountFour,
     switches::kGpuRasterizationMSAASampleCount, "4"},
    {flag_descriptions::kGpuRasterizationMsaaSampleCountEight,
     switches::kGpuRasterizationMSAASampleCount, "8"},
    {flag_descriptions::kGpuRasterizationMsaaSampleCountSixteen,
     switches::kGpuRasterizationMSAASampleCount, "16"},
};

const FeatureEntry::Choice kMHTMLGeneratorOptionChoices[] = {
    {flags_ui::kGenericExperimentChoiceDefault, "", ""},
    {flag_descriptions::kMhtmlSkipNostoreMain, switches::kMHTMLGeneratorOption,
     switches::kMHTMLSkipNostoreMain},
    {flag_descriptions::kMhtmlSkipNostoreAll, switches::kMHTMLGeneratorOption,
     switches::kMHTMLSkipNostoreAll},
};

const FeatureEntry::Choice kEnableGpuRasterizationChoices[] = {
    {flags_ui::kGenericExperimentChoiceDefault, "", ""},
    {flags_ui::kGenericExperimentChoiceEnabled,
     switches::kEnableGpuRasterization, ""},
    {flags_ui::kGenericExperimentChoiceDisabled,
     switches::kDisableGpuRasterization, ""},
    {flag_descriptions::kForceGpuRasterization,
     switches::kForceGpuRasterization, ""},
};

const FeatureEntry::Choice kColorCorrectRenderingChoices[] = {
    {flags_ui::kGenericExperimentChoiceDefault, "", ""},
    {flags_ui::kGenericExperimentChoiceEnabled,
     switches::kEnableColorCorrectRendering, ""},
    {flags_ui::kGenericExperimentChoiceDisabled, "", ""},
};

const FeatureEntry::Choice kEnableWebGL2Choices[] = {
    {flags_ui::kGenericExperimentChoiceDefault, "", ""},
    {flags_ui::kGenericExperimentChoiceEnabled, switches::kEnableES3APIs, ""},
    {flags_ui::kGenericExperimentChoiceDisabled, switches::kDisableES3APIs, ""},
};

#if defined(OS_CHROMEOS)
const FeatureEntry::Choice kMemoryPressureThresholdChoices[] = {
    {flags_ui::kGenericExperimentChoiceDefault, "", ""},
    {flag_descriptions::kConservativeThresholds,
     chromeos::switches::kMemoryPressureThresholds,
     chromeos::switches::kConservativeThreshold},
    {flag_descriptions::kAggressiveCacheDiscardThresholds,
     chromeos::switches::kMemoryPressureThresholds,
     chromeos::switches::kAggressiveCacheDiscardThreshold},
    {flag_descriptions::kAggressiveTabDiscardThresholds,
     chromeos::switches::kMemoryPressureThresholds,
     chromeos::switches::kAggressiveTabDiscardThreshold},
    {flag_descriptions::kAggressiveThresholds,
     chromeos::switches::kMemoryPressureThresholds,
     chromeos::switches::kAggressiveThreshold},
};
#endif  // OS_CHROMEOS

const FeatureEntry::Choice kExtensionContentVerificationChoices[] = {
    {flags_ui::kGenericExperimentChoiceDefault, "", ""},
    {flag_descriptions::kExtensionContentVerificationBootstrap,
     switches::kExtensionContentVerification,
     switches::kExtensionContentVerificationBootstrap},
    {flag_descriptions::kExtensionContentVerificationEnforce,
     switches::kExtensionContentVerification,
     switches::kExtensionContentVerificationEnforce},
    {flag_descriptions::kExtensionContentVerificationEnforceStrict,
     switches::kExtensionContentVerification,
     switches::kExtensionContentVerificationEnforceStrict},
};

const FeatureEntry::Choice kTopChromeMaterialDesignChoices[] = {
    {flags_ui::kGenericExperimentChoiceDefault, "", ""},
    {flag_descriptions::kTopChromeMdMaterial, switches::kTopChromeMD,
     switches::kTopChromeMDMaterial},
    {flag_descriptions::kTopChromeMdMaterialHybrid, switches::kTopChromeMD,
     switches::kTopChromeMDMaterialHybrid},
};

#if defined(USE_ASH)
const FeatureEntry::Choice kAshShelfColorChoices[] = {
    {flags_ui::kGenericExperimentChoiceDefault, "", ""},
    {flags_ui::kGenericExperimentChoiceEnabled, ash::switches::kAshShelfColor,
     ash::switches::kAshShelfColorEnabled},
    {flags_ui::kGenericExperimentChoiceDisabled, ash::switches::kAshShelfColor,
     ash::switches::kAshShelfColorDisabled}};

const FeatureEntry::Choice kAshShelfColorSchemeChoices[] = {
    {flags_ui::kGenericExperimentChoiceDefault, "", ""},
    {flag_descriptions::kAshShelfColorSchemeLightVibrant,
     ash::switches::kAshShelfColorScheme,
     ash::switches::kAshShelfColorSchemeLightVibrant},
    {flag_descriptions::kAshShelfColorSchemeNormalVibrant,
     ash::switches::kAshShelfColorScheme,
     ash::switches::kAshShelfColorSchemeNormalVibrant},
    {flag_descriptions::kAshShelfColorSchemeDarkVibrant,
     ash::switches::kAshShelfColorScheme,
     ash::switches::kAshShelfColorSchemeDarkVibrant},
    {flag_descriptions::kAshShelfColorSchemeLightMuted,
     ash::switches::kAshShelfColorScheme,
     ash::switches::kAshShelfColorSchemeLightMuted},
    {flag_descriptions::kAshShelfColorSchemeNormalMuted,
     ash::switches::kAshShelfColorScheme,
     ash::switches::kAshShelfColorSchemeNormalMuted},
    {flag_descriptions::kAshShelfColorSchemeDarkMuted,
     ash::switches::kAshShelfColorScheme,
     ash::switches::kAshShelfColorSchemeDarkMuted},
};
#endif  // USE_ASH

#if defined(OS_CHROMEOS)
const FeatureEntry::Choice kAshMaterialDesignInkDropAnimationSpeed[] = {
    {flags_ui::kGenericExperimentChoiceDefault, "", ""},
    {flag_descriptions::kMaterialDesignInkDropAnimationFast,
     switches::kMaterialDesignInkDropAnimationSpeed,
     switches::kMaterialDesignInkDropAnimationSpeedFast},
    {flag_descriptions::kMaterialDesignInkDropAnimationSlow,
     switches::kMaterialDesignInkDropAnimationSpeed,
     switches::kMaterialDesignInkDropAnimationSpeedSlow}};

const FeatureEntry::Choice kDataSaverPromptChoices[] = {
    {flags_ui::kGenericExperimentChoiceDefault, "", ""},
    {flags_ui::kGenericExperimentChoiceEnabled,
     chromeos::switches::kEnableDataSaverPrompt, ""},
    {flags_ui::kGenericExperimentChoiceDisabled,
     chromeos::switches::kDisableDataSaverPrompt, ""},
    {flag_descriptions::kDatasaverPromptDemoMode,
     chromeos::switches::kEnableDataSaverPrompt,
     chromeos::switches::kDataSaverPromptDemoMode},
};

const FeatureEntry::Choice kUseMusChoices[] = {
    {flags_ui::kGenericExperimentChoiceDefault, "", ""},
    {flag_descriptions::kEnableMusDescription, switches::kMus, ""},
    {flag_descriptions::kEnableMashDescription, switches::kMash, ""},
};

const FeatureEntry::Choice kUiShowCompositedLayerBordersChoices[] = {
    {flags_ui::kGenericExperimentChoiceDefault, "", ""},
    {flag_descriptions::kUiShowCompositedLayerBordersRenderPass,
     cc::switches::kUIShowCompositedLayerBorders,
     cc::switches::kCompositedRenderPassBorders},
    {flag_descriptions::kUiShowCompositedLayerBordersSurface,
     cc::switches::kUIShowCompositedLayerBorders,
     cc::switches::kCompositedSurfaceBorders},
    {flag_descriptions::kUiShowCompositedLayerBordersLayer,
     cc::switches::kUIShowCompositedLayerBorders,
     cc::switches::kCompositedLayerBorders},
    {flag_descriptions::kUiShowCompositedLayerBordersAll,
     cc::switches::kUIShowCompositedLayerBorders, ""}};
#endif  // OS_CHROMEOS

const FeatureEntry::Choice kV8CacheOptionsChoices[] = {
    {flags_ui::kGenericExperimentChoiceDefault, "", ""},
    {flags_ui::kGenericExperimentChoiceDisabled, switches::kV8CacheOptions,
     "none"},
    {flag_descriptions::kV8CacheOptionsParse, switches::kV8CacheOptions,
     "parse"},
    {flag_descriptions::kV8CacheOptionsCode, switches::kV8CacheOptions, "code"},
};

const FeatureEntry::Choice kV8CacheStrategiesForCacheStorageChoices[] = {
    {flags_ui::kGenericExperimentChoiceDefault, "", ""},
    {flags_ui::kGenericExperimentChoiceDisabled,
     switches::kV8CacheStrategiesForCacheStorage, "none"},
    {flag_descriptions::kV8CacheStrategiesForCacheStorageNormal,
     switches::kV8CacheStrategiesForCacheStorage, "normal"},
    {flag_descriptions::kV8CacheStrategiesForCacheStorageAggressive,
     switches::kV8CacheStrategiesForCacheStorage, "aggressive"},
};

#if defined(OS_ANDROID)
const FeatureEntry::Choice kProgressBarAnimationChoices[] = {
    {flags_ui::kGenericExperimentChoiceDefault, "", ""},
    {flags_ui::kGenericExperimentChoiceDisabled,
     switches::kProgressBarAnimation, "disabled"},
    {flag_descriptions::kProgressBarAnimationLinear,
     switches::kProgressBarAnimation, "linear"},
    {flag_descriptions::kProgressBarAnimationSmooth,
     switches::kProgressBarAnimation, "smooth"},
    {flag_descriptions::kProgressBarAnimationSmoothIndeterminate,
     switches::kProgressBarAnimation, "smooth-indeterminate"},
    {flag_descriptions::kProgressBarAnimationFastStart,
     switches::kProgressBarAnimation, "fast-start"},
};

const FeatureEntry::Choice kProgressBarCompletionChoices[] = {
    {flags_ui::kGenericExperimentChoiceDefault, "", ""},
    {flag_descriptions::kProgressBarCompletionLoadEvent,
     switches::kProgressBarCompletion, "loadEvent"},
    {flag_descriptions::kProgressBarCompletionResourcesBeforeDcl,
     switches::kProgressBarCompletion, "resourcesBeforeDOMContentLoaded"},
    {flag_descriptions::kProgressBarCompletionDomContentLoaded,
     switches::kProgressBarCompletion, "domContentLoaded"},
    {flag_descriptions::
         kProgressBarCompletionResourcesBeforeDclAndSameOriginIframes,
     switches::kProgressBarCompletion,
     "resourcesBeforeDOMContentLoadedAndSameOriginIFrames"},
};
#endif  // OS_ANDROID

#if defined(OS_CHROMEOS)
const FeatureEntry::Choice kCrosRegionsModeChoices[] = {
    {flag_descriptions::kCrosRegionsModeDefault, "", ""},
    {flag_descriptions::kCrosRegionsModeOverride,
     chromeos::switches::kCrosRegionsMode,
     chromeos::switches::kCrosRegionsModeOverride},
    {flag_descriptions::kCrosRegionsModeHide,
     chromeos::switches::kCrosRegionsMode,
     chromeos::switches::kCrosRegionsModeHide},
};
#endif  // OS_CHROMEOS

const FeatureEntry::Choice kForceUIDirectionChoices[] = {
    {flags_ui::kGenericExperimentChoiceDefault, "", ""},
    {flag_descriptions::kForceDirectionLtr, switches::kForceUIDirection,
     switches::kForceDirectionLTR},
    {flag_descriptions::kForceDirectionRtl, switches::kForceUIDirection,
     switches::kForceDirectionRTL},
};

const FeatureEntry::Choice kForceTextDirectionChoices[] = {
    {flags_ui::kGenericExperimentChoiceDefault, "", ""},
    {flag_descriptions::kForceDirectionLtr, switches::kForceTextDirection,
     switches::kForceDirectionLTR},
    {flag_descriptions::kForceDirectionRtl, switches::kForceTextDirection,
     switches::kForceDirectionRTL},
};

#if defined(OS_ANDROID)
const FeatureEntry::Choice kNtpSwitchToExistingTabChoices[] = {
    {flags_ui::kGenericExperimentChoiceDefault, "", ""},
    {flags_ui::kGenericExperimentChoiceDisabled,
     switches::kNtpSwitchToExistingTab, "disabled"},
    {flag_descriptions::kNtpSwitchToExistingTabMatchUrl,
     switches::kNtpSwitchToExistingTab, "url"},
    {flag_descriptions::kNtpSwitchToExistingTabMatchHost,
     switches::kNtpSwitchToExistingTab, "host"},
};
#endif  // OS_ANDROID

#if defined(OS_CHROMEOS)
const FeatureEntry::Choice kAshForceTabletModeChoices[] = {
    {flags_ui::kGenericExperimentChoiceDefault, "", ""},
    {flag_descriptions::kForceTabletModeTouchview,
     ash::switches::kAshForceTabletMode,
     ash::switches::kAshForceTabletModeTouchView},
    {flag_descriptions::kForceTabletModeClamshell,
     ash::switches::kAshForceTabletMode,
     ash::switches::kAshForceTabletModeClamshell},
    {flag_descriptions::kForceTabletModeAuto,
     ash::switches::kAshForceTabletMode,
     ash::switches::kAshForceTabletModeAuto},
};
#endif  // OS_CHROMEOS

#if defined(OS_ANDROID)
const FeatureEntry::FeatureParam
    kContentSuggestionsCategoryOrderFeatureVariationGeneral[] = {
        {ntp_snippets::kCategoryOrderParameter,
         ntp_snippets::kCategoryOrderGeneral},
};

const FeatureEntry::FeatureParam
    kContentSuggestionsCategoryOrderFeatureVariationEMOriented[] = {
        {ntp_snippets::kCategoryOrderParameter,
         ntp_snippets::kCategoryOrderEmergingMarketsOriented},
};

const FeatureEntry::FeatureVariation
    kContentSuggestionsCategoryOrderFeatureVariations[] = {
        {"(general)", kContentSuggestionsCategoryOrderFeatureVariationGeneral,
         arraysize(kContentSuggestionsCategoryOrderFeatureVariationGeneral),
         nullptr},
        {"(emerging markets oriented)",
         kContentSuggestionsCategoryOrderFeatureVariationEMOriented,
         arraysize(kContentSuggestionsCategoryOrderFeatureVariationEMOriented),
         nullptr}};

const FeatureEntry::FeatureParam
    kContentSuggestionsCategoryRankerFeatureVariationConstant[] = {
        {ntp_snippets::kCategoryRankerParameter,
         ntp_snippets::kCategoryRankerConstantRanker},
};

const FeatureEntry::FeatureParam
    kContentSuggestionsCategoryRankerFeatureVariationClickBased[] = {
        {ntp_snippets::kCategoryRankerParameter,
         ntp_snippets::kCategoryRankerClickBasedRanker},
};

const FeatureEntry::FeatureVariation
    kContentSuggestionsCategoryRankerFeatureVariations[] = {
        {"(constant)",
         kContentSuggestionsCategoryRankerFeatureVariationConstant,
         arraysize(kContentSuggestionsCategoryRankerFeatureVariationConstant),
         nullptr},
        {"(click based)",
         kContentSuggestionsCategoryRankerFeatureVariationClickBased,
         arraysize(kContentSuggestionsCategoryRankerFeatureVariationClickBased),
         nullptr}};
#endif  // OS_ANDROID

#if defined(OS_ANDROID)
const FeatureEntry::FeatureVariation
    kContentSuggestionsNotificationsFeatureVariations[] = {
        {"(notify always)", nullptr, 0, "3313312"}};
#endif  // OS_ANDROID

#if defined(OS_ANDROID)
const FeatureEntry::FeatureVariation kRemoteSuggestionsFeatureVariations[] = {
    {"via content suggestion server (backed by ChromeReader)", nullptr, 0,
     "3313421"},
    {"via content suggestion server (backed by Google Now)", nullptr, 0,
     "3313422"}};
#endif  // OS_ANDROID

#if defined(OS_ANDROID)
const FeatureEntry::FeatureParam kCondensedTileLayoutForSmallScreensEnabled[] =
    {{"condensed_tile_layout_for_small_screens_enabled", "true"}};

const FeatureEntry::FeatureParam kCondensedTileLayoutForLargeScreensEnabled[] =
    {{"condensed_tile_layout_for_large_screens_enabled", "true"}};

const FeatureEntry::FeatureVariation
    kNTPCondensedTileLayoutFeatureVariations[] = {
        {"(small screens)", kCondensedTileLayoutForSmallScreensEnabled,
         arraysize(kCondensedTileLayoutForSmallScreensEnabled), nullptr},
        {"(large screens)", kCondensedTileLayoutForLargeScreensEnabled,
         arraysize(kCondensedTileLayoutForLargeScreensEnabled), nullptr}};
#endif  // OS_ANDROID

#if defined(OS_ANDROID)
const FeatureEntry::Choice kHerbPrototypeChoices[] = {
    {flags_ui::kGenericExperimentChoiceDefault, "", ""},
    {flags_ui::kGenericExperimentChoiceDisabled,
     switches::kTabManagementExperimentTypeDisabled, ""},
    {flag_descriptions::kHerbPrototypeFlavorElderberry,
     switches::kTabManagementExperimentTypeElderberry, ""},
};
#endif  // OS_ANDROID

const FeatureEntry::Choice kEnableUseZoomForDSFChoices[] = {
    {flag_descriptions::kEnableUseZoomForDsfChoiceDefault, "", ""},
    {flag_descriptions::kEnableUseZoomForDsfChoiceEnabled,
     switches::kEnableUseZoomForDSF, "true"},
    {flag_descriptions::kEnableUseZoomForDsfChoiceDisabled,
     switches::kEnableUseZoomForDSF, "false"},
};

const FeatureEntry::Choice kEnableWebFontsInterventionV2Choices[] = {
    {flag_descriptions::kEnableWebfontsInterventionV2ChoiceDefault, "", ""},
    {flag_descriptions::kEnableWebfontsInterventionV2ChoiceEnabledWith2g,
     switches::kEnableWebFontsInterventionV2,
     switches::kEnableWebFontsInterventionV2SwitchValueEnabledWith2G},
    {flag_descriptions::kEnableWebfontsInterventionV2ChoiceEnabledWith3g,
     switches::kEnableWebFontsInterventionV2,
     switches::kEnableWebFontsInterventionV2SwitchValueEnabledWith3G},
    {flag_descriptions::kEnableWebfontsInterventionV2ChoiceEnabledWithSlow2g,
     switches::kEnableWebFontsInterventionV2,
     switches::kEnableWebFontsInterventionV2SwitchValueEnabledWithSlow2G},
    {flag_descriptions::kEnableWebfontsInterventionV2ChoiceDisabled,
     switches::kEnableWebFontsInterventionV2,
     switches::kEnableWebFontsInterventionV2SwitchValueDisabled},
};

const FeatureEntry::Choice kSSLVersionMaxChoices[] = {
    {flags_ui::kGenericExperimentChoiceDefault, "", ""},
    {flag_descriptions::kSslVersionMaxTls12, switches::kSSLVersionMax,
     "tls1.2"},
    {flag_descriptions::kSslVersionMaxTls13, switches::kSSLVersionMax,
     "tls1.3"},
};

#if !defined(OS_ANDROID)
const FeatureEntry::Choice kEnableAudioFocusChoices[] = {
    {flag_descriptions::kEnableAudioFocusDisabled, "", ""},
    {flag_descriptions::kEnableAudioFocusEnabled, switches::kEnableAudioFocus,
     ""},
#if BUILDFLAG(ENABLE_PLUGINS)
    {flag_descriptions::kEnableAudioFocusEnabledDuckFlash,
     switches::kEnableAudioFocus, switches::kEnableAudioFocusDuckFlash},
#endif  // BUILDFLAG(ENABLE_PLUGINS)
};
#endif  // !defined(OS_ANDROID)

const FeatureEntry::Choice kAutoplayPolicyChoices[] = {
    {flags_ui::kGenericExperimentChoiceDefault, "", ""},
    {flag_descriptions::kAutoplayPolicyNoUserGestureRequired,
     switches::kAutoplayPolicy,
     switches::autoplay::kNoUserGestureRequiredPolicy},
#if defined(OS_ANDROID)
    {flag_descriptions::kAutoplayPolicyUserGestureRequired,
     switches::kAutoplayPolicy, switches::autoplay::kUserGestureRequiredPolicy},
#else
    {flag_descriptions::kAutoplayPolicyUserGestureRequiredForCrossOrigin,
     switches::kAutoplayPolicy,
     switches::autoplay::kUserGestureRequiredForCrossOriginPolicy},
#endif
};

const FeatureEntry::Choice kForceEffectiveConnectionTypeChoices[] = {
    {flags_ui::kGenericExperimentChoiceDefault, "", ""},
    {flag_descriptions::kEffectiveConnectionTypeUnknownDescription,
     switches::kForceEffectiveConnectionType,
     net::kEffectiveConnectionTypeUnknown},
    {flag_descriptions::kEffectiveConnectionTypeOfflineDescription,
     switches::kForceEffectiveConnectionType,
     net::kEffectiveConnectionTypeOffline},
    {flag_descriptions::kEffectiveConnectionTypeSlow2GDescription,
     switches::kForceEffectiveConnectionType,
     net::kEffectiveConnectionTypeSlow2G},
    {flag_descriptions::kEffectiveConnectionType2GDescription,
     switches::kForceEffectiveConnectionType, net::kEffectiveConnectionType2G},
    {flag_descriptions::kEffectiveConnectionType3GDescription,
     switches::kForceEffectiveConnectionType, net::kEffectiveConnectionType3G},
    {flag_descriptions::kEffectiveConnectionType4GDescription,
     switches::kForceEffectiveConnectionType, net::kEffectiveConnectionType4G},
};

// Ensure that all effective connection types returned by Network Quality
// Estimator (NQE) are also exposed via flags.
static_assert(net::EFFECTIVE_CONNECTION_TYPE_LAST + 1 ==
                  arraysize(kForceEffectiveConnectionTypeChoices),
              "ECT enum value is not handled.");
static_assert(net::EFFECTIVE_CONNECTION_TYPE_UNKNOWN == 0,
              "ECT enum value is not handled.");
static_assert(net::EFFECTIVE_CONNECTION_TYPE_4G + 1 ==
                  net::EFFECTIVE_CONNECTION_TYPE_LAST,
              "ECT enum value is not handled.");

const FeatureEntry::FeatureParam kNoStatePrefetchEnabled[] = {
    {prerender::kNoStatePrefetchFeatureModeParameterName,
     prerender::kNoStatePrefetchFeatureModeParameterPrefetch}};

const FeatureEntry::FeatureParam kNoStatePrefetchPrerender[] = {
    {prerender::kNoStatePrefetchFeatureModeParameterName,
     prerender::kNoStatePrefetchFeatureModeParameterPrerender}};

const FeatureEntry::FeatureParam kNoStatePrefetchSimpleLoad[] = {
    {prerender::kNoStatePrefetchFeatureModeParameterName,
     prerender::kNoStatePrefetchFeatureModeParameterSimpleLoad}};

const FeatureEntry::FeatureVariation kNoStatePrefetchFeatureVariations[] = {
    {"No-state prefetch", kNoStatePrefetchEnabled,
     arraysize(kNoStatePrefetchEnabled), nullptr},
    {"Prerender", kNoStatePrefetchPrerender,
     arraysize(kNoStatePrefetchPrerender), nullptr},
    {"Simple load", kNoStatePrefetchSimpleLoad,
     arraysize(kNoStatePrefetchSimpleLoad), nullptr}};

const FeatureEntry::FeatureParam kSpeculativeResourcePrefetchingPrefetching[] =
    {{predictors::kModeParamName, predictors::kPrefetchingMode}};

const FeatureEntry::FeatureParam kSpeculativeResourcePrefetchingLearning[] = {
    {predictors::kModeParamName, predictors::kLearningMode}};

const FeatureEntry::FeatureVariation
    kSpeculativeResourcePrefetchingFeatureVariations[] = {
        {"Prefetching", kSpeculativeResourcePrefetchingPrefetching,
         arraysize(kSpeculativeResourcePrefetchingPrefetching), nullptr},
        {"Learning", kSpeculativeResourcePrefetchingLearning,
         arraysize(kSpeculativeResourcePrefetchingLearning), nullptr}};

#if defined(OS_CHROMEOS) || defined(OS_LINUX) || defined(OS_MACOSX) || \
    defined(OS_WIN)
const FeatureEntry::FeatureParam kPauseBackgroundTabsMinimalEngagment[] = {
    {pausetabs::kFeatureName, pausetabs::kModeParamMinimal}};

const FeatureEntry::FeatureParam kPauseBackgroundTabsLowEngagment[] = {
    {pausetabs::kFeatureName, pausetabs::kModeParamLow}};

const FeatureEntry::FeatureParam kPauseBackgroundTabsMediumEngagment[] = {
    {pausetabs::kFeatureName, pausetabs::kModeParamMedium}};

const FeatureEntry::FeatureParam kPauseBackgroundTabsHighEngagment[] = {
    {pausetabs::kFeatureName, pausetabs::kModeParamHigh}};

const FeatureEntry::FeatureParam kPauseBackgroundTabsMaxEngagment[] = {
    {pausetabs::kFeatureName, pausetabs::kModeParamMax}};

const FeatureEntry::FeatureVariation kPauseBackgroundTabsVariations[] = {
    {"minimal engagement", kPauseBackgroundTabsMinimalEngagment,
     arraysize(kPauseBackgroundTabsMinimalEngagment), nullptr},
    {"low engagement", kPauseBackgroundTabsLowEngagment,
     arraysize(kPauseBackgroundTabsLowEngagment), nullptr},
    {"medium engagement", kPauseBackgroundTabsMediumEngagment,
     arraysize(kPauseBackgroundTabsMediumEngagment), nullptr},
    {"high engagement", kPauseBackgroundTabsHighEngagment,
     arraysize(kPauseBackgroundTabsHighEngagment), nullptr},
    {"max engagement", kPauseBackgroundTabsMaxEngagment,
     arraysize(kPauseBackgroundTabsMaxEngagment), nullptr}};
#endif

#if defined(OS_ANDROID)
const FeatureEntry::FeatureParam
kAutofillCreditCardPopupLayoutFeatureVariationIconAtStart[] = {
  {"is_credit_card_icon_at_start", "true"}};

const FeatureEntry::FeatureParam
kAutofillCreditCardPopupLayoutFeatureVariationDropdownItemHeight[] = {
  {"dropdown_item_height", "56"}};

const FeatureEntry::FeatureParam
kAutofillCreditCardPopupLayoutFeatureVariationExpanded[] = {
  {"is_credit_card_icon_at_start", "true"},
  {"dropdown_item_height", "56"},
  {"margin", "18"}};

const FeatureEntry::FeatureVariation
kAutofillCreditCardPopupLayoutFeatureVariations[] = {
    {"Display credit card icon at start",
     kAutofillCreditCardPopupLayoutFeatureVariationIconAtStart,
     arraysize(kAutofillCreditCardPopupLayoutFeatureVariationIconAtStart),
     nullptr},
    {"Increase dropdown item height",
     kAutofillCreditCardPopupLayoutFeatureVariationDropdownItemHeight,
     arraysize(
         kAutofillCreditCardPopupLayoutFeatureVariationDropdownItemHeight),
     nullptr},
    {"Display credit card icon at start and increase dropdown item height",
     kAutofillCreditCardPopupLayoutFeatureVariationExpanded,
     arraysize(kAutofillCreditCardPopupLayoutFeatureVariationExpanded),
     nullptr}};

const FeatureEntry::FeatureParam
    kAutofillKeyboardAccessoryFeatureVariationAnimationDuration[] = {
        {autofill::kAutofillKeyboardAccessoryAnimationDurationKey, "1000"}};

const FeatureEntry::FeatureParam
    kAutofillKeyboardAccessoryFeatureVariationLimitLabelWidth[] = {
        {autofill::kAutofillKeyboardAccessoryLimitLabelWidthKey, "true"}};

const FeatureEntry::FeatureParam
    kAutofillKeyboardAccessoryFeatureVariationShowHint[] = {
        {autofill::kAutofillKeyboardAccessoryHintKey, "true"}};

const FeatureEntry::FeatureParam
    kAutofillKeyboardAccessoryFeatureVariationAnimateWithHint[] = {
        {autofill::kAutofillKeyboardAccessoryAnimationDurationKey, "1000"},
        {autofill::kAutofillKeyboardAccessoryHintKey, "true"}};

const FeatureEntry::FeatureVariation
    kAutofillKeyboardAccessoryFeatureVariations[] = {
        {"Animate", kAutofillKeyboardAccessoryFeatureVariationAnimationDuration,
         arraysize(kAutofillKeyboardAccessoryFeatureVariationAnimationDuration),
         nullptr},
        {"Limit label width",
         kAutofillKeyboardAccessoryFeatureVariationLimitLabelWidth,
         arraysize(kAutofillKeyboardAccessoryFeatureVariationLimitLabelWidth),
         nullptr},
        {"Show hint", kAutofillKeyboardAccessoryFeatureVariationShowHint,
         arraysize(kAutofillKeyboardAccessoryFeatureVariationShowHint),
         nullptr},
        {"Animate with hint",
         kAutofillKeyboardAccessoryFeatureVariationAnimateWithHint,
         arraysize(kAutofillKeyboardAccessoryFeatureVariationAnimateWithHint),
         nullptr}};
#endif  // OS_ANDROID

const FeatureEntry::FeatureParam
    kAutofillCreditCardLastUsedDateFeatureVariationExpDate[] = {
        {"show_expiration_date", "true"}};

const FeatureEntry::FeatureVariation
    kAutofillCreditCardLastUsedDateFeatureVariations[] = {
        {"Display expiration date",
         kAutofillCreditCardLastUsedDateFeatureVariationExpDate,
         arraysize(kAutofillCreditCardLastUsedDateFeatureVariationExpDate),
         nullptr}};

#if defined(OS_ANDROID)
const FeatureEntry::FeatureParam kUseNewDoodleApiTest0[] = {
    {"doodle_override_url",
     "https://www.gstatic.com/chrome/ntp/doodle_test/android0.json"}};
const FeatureEntry::FeatureParam kUseNewDoodleApiTest1[] = {
    {"doodle_override_url",
     "https://www.gstatic.com/chrome/ntp/doodle_test/android1.json"}};
const FeatureEntry::FeatureParam kUseNewDoodleApiTest2[] = {
    {"doodle_override_url",
     "https://www.gstatic.com/chrome/ntp/doodle_test/android2.json"}};
const FeatureEntry::FeatureParam kUseNewDoodleApiTest3[] = {
    {"doodle_override_url",
     "https://www.gstatic.com/chrome/ntp/doodle_test/android3.json"}};
const FeatureEntry::FeatureParam kUseNewDoodleApiTest4[] = {
    {"doodle_override_url",
     "https://www.gstatic.com/chrome/ntp/doodle_test/android4.json"}};
const FeatureEntry::FeatureParam kUseNewDoodleApiTest5[] = {
    {"doodle_override_url",
     "https://www.gstatic.com/chrome/ntp/doodle_test/android5.json"}};
const FeatureEntry::FeatureParam kUseNewDoodleApiTest6[] = {
    {"doodle_override_url",
     "https://www.gstatic.com/chrome/ntp/doodle_test/android6.json"}};
const FeatureEntry::FeatureParam kUseNewDoodleApiTest7[] = {
    {"doodle_override_url",
     "https://www.gstatic.com/chrome/ntp/doodle_test/android7.json"}};
const FeatureEntry::FeatureParam kUseNewDoodleApiTest8[] = {
    {"doodle_override_url",
     "https://www.gstatic.com/chrome/ntp/doodle_test/android8.json"}};

const FeatureEntry::FeatureVariation kUseNewDoodleApiVariations[] = {
    {"(force test doodle 0)", kUseNewDoodleApiTest0,
     arraysize(kUseNewDoodleApiTest0), nullptr},
    {"(force test doodle 1)", kUseNewDoodleApiTest1,
     arraysize(kUseNewDoodleApiTest1), nullptr},
    {"(force test doodle 2)", kUseNewDoodleApiTest2,
     arraysize(kUseNewDoodleApiTest2), nullptr},
    {"(force test doodle 3)", kUseNewDoodleApiTest3,
     arraysize(kUseNewDoodleApiTest3), nullptr},
    {"(force test doodle 4)", kUseNewDoodleApiTest4,
     arraysize(kUseNewDoodleApiTest4), nullptr},
    {"(force test doodle 5)", kUseNewDoodleApiTest5,
     arraysize(kUseNewDoodleApiTest5), nullptr},
    {"(force test doodle 6)", kUseNewDoodleApiTest6,
     arraysize(kUseNewDoodleApiTest6), nullptr},
    {"(force test doodle 7)", kUseNewDoodleApiTest7,
     arraysize(kUseNewDoodleApiTest7), nullptr},
    {"(force test doodle 8)", kUseNewDoodleApiTest8,
     arraysize(kUseNewDoodleApiTest8), nullptr}};
#endif  // OS_ANDROID

const FeatureEntry::FeatureParam kMemoryAblation5MiB_512[] = {
    {kMemoryAblationFeatureSizeParam, "5242880"},
    {kMemoryAblationFeatureMaxRAMParam, "512"}};
const FeatureEntry::FeatureParam kMemoryAblation10MiB_512_1024[] = {
    {kMemoryAblationFeatureSizeParam, "10485760"},
    {kMemoryAblationFeatureMinRAMParam, "512"},
    {kMemoryAblationFeatureMaxRAMParam, "1024"}};
const FeatureEntry::FeatureParam kMemoryAblation20MiB_1024_2048[] = {
    {kMemoryAblationFeatureSizeParam, "20971520"},
    {kMemoryAblationFeatureMinRAMParam, "1024"},
    {kMemoryAblationFeatureMaxRAMParam, "2048"}};
const FeatureEntry::FeatureParam kMemoryAblation30MiB[] = {
    {kMemoryAblationFeatureSizeParam, "31457280"}};
const FeatureEntry::FeatureParam kMemoryAblation40MiB_512_4096[] = {
    {kMemoryAblationFeatureSizeParam, "41943040"},
    {kMemoryAblationFeatureMinRAMParam, "512"},
    {kMemoryAblationFeatureMaxRAMParam, "4096"}};
const FeatureEntry::FeatureParam kMemoryAblation50MiB_2048_4096[] = {
    {kMemoryAblationFeatureSizeParam, "52428800"},
    {kMemoryAblationFeatureMinRAMParam, "2048"},
    {kMemoryAblationFeatureMaxRAMParam, "4096"}};
const FeatureEntry::FeatureParam kMemoryAblation100MiB_4096[] = {
    {kMemoryAblationFeatureSizeParam, "104857600"},
    {kMemoryAblationFeatureMinRAMParam, "4096"}};

const FeatureEntry::FeatureVariation kMemoryAblationFeatureVariations[] = {
    {"5 MiB (RAM <= 512)", kMemoryAblation5MiB_512,
     arraysize(kMemoryAblation5MiB_512), nullptr},
    {"10 MiB (512 <= RAM <= 1024)", kMemoryAblation10MiB_512_1024,
     arraysize(kMemoryAblation10MiB_512_1024), nullptr},
    {"20 MiB (1024 <= RAM <= 2048)", kMemoryAblation20MiB_1024_2048,
     arraysize(kMemoryAblation20MiB_1024_2048), nullptr},
    {"30 MiB (any RAM)", kMemoryAblation30MiB, arraysize(kMemoryAblation30MiB),
     nullptr},
    {"40 MiB (512 <= RAM <= 4096)", kMemoryAblation40MiB_512_4096,
     arraysize(kMemoryAblation40MiB_512_4096), nullptr},
    {"50 MiB (2048 <= RAM <= 4096)", kMemoryAblation50MiB_2048_4096,
     arraysize(kMemoryAblation50MiB_2048_4096), nullptr},
    {"100 MiB (RAM >= 4096)", kMemoryAblation100MiB_4096,
     arraysize(kMemoryAblation100MiB_4096), nullptr}};

#if defined(OS_ANDROID)
const FeatureEntry::FeatureParam kPersistentMenuItemEnabled[] = {
    {"persistent_menu_item_enabled", "true"}};

const FeatureEntry::FeatureVariation kDataReductionMainMenuFeatureVariations[] =
    {{"(persistent)", kPersistentMenuItemEnabled,
      arraysize(kPersistentMenuItemEnabled), nullptr}};
#endif  // OS_ANDROID

const FeatureEntry::Choice kEnableHeapProfilingChoices[] = {
    {flags_ui::kGenericExperimentChoiceDisabled, "", ""},
    {flag_descriptions::kEnableHeapProfilingModePseudo,
     switches::kEnableHeapProfiling, switches::kEnableHeapProfilingModePseudo},
    {flag_descriptions::kEnableHeapProfilingModeNative,
     switches::kEnableHeapProfiling, switches::kEnableHeapProfilingModeNative},
    {flag_descriptions::kEnableHeapProfilingTaskProfiler,
     switches::kEnableHeapProfiling,
     switches::kEnableHeapProfilingTaskProfiler}};

const FeatureEntry::FeatureParam kOmniboxUIMaxAutocompleteMatches4[] = {
    {OmniboxFieldTrial::kUIMaxAutocompleteMatchesParam, "4"}};
const FeatureEntry::FeatureParam kOmniboxUIMaxAutocompleteMatches6[] = {
    {OmniboxFieldTrial::kUIMaxAutocompleteMatchesParam, "6"}};
const FeatureEntry::FeatureParam kOmniboxUIMaxAutocompleteMatches8[] = {
    {OmniboxFieldTrial::kUIMaxAutocompleteMatchesParam, "8"}};
const FeatureEntry::FeatureParam kOmniboxUIMaxAutocompleteMatches10[] = {
    {OmniboxFieldTrial::kUIMaxAutocompleteMatchesParam, "10"}};
const FeatureEntry::FeatureParam kOmniboxUIMaxAutocompleteMatches12[] = {
    {OmniboxFieldTrial::kUIMaxAutocompleteMatchesParam, "12"}};

const FeatureEntry::FeatureVariation
    kOmniboxUIMaxAutocompleteMatchesVariations[] = {
        {"4 matches", kOmniboxUIMaxAutocompleteMatches4,
         arraysize(kOmniboxUIMaxAutocompleteMatches4), nullptr},
        {"6 matches", kOmniboxUIMaxAutocompleteMatches6,
         arraysize(kOmniboxUIMaxAutocompleteMatches6), nullptr},
        {"8 matches", kOmniboxUIMaxAutocompleteMatches8,
         arraysize(kOmniboxUIMaxAutocompleteMatches8), nullptr},
        {"10 matches", kOmniboxUIMaxAutocompleteMatches10,
         arraysize(kOmniboxUIMaxAutocompleteMatches10), nullptr},
        {"12 matches", kOmniboxUIMaxAutocompleteMatches12,
         arraysize(kOmniboxUIMaxAutocompleteMatches12), nullptr}};

const FeatureEntry::FeatureParam kOmniboxUIVerticalMargin4px[] = {
    {OmniboxFieldTrial::kUIVerticalMarginParam, "4"}};
const FeatureEntry::FeatureParam kOmniboxUIVerticalMargin8px[] = {
    {OmniboxFieldTrial::kUIVerticalMarginParam, "8"}};
const FeatureEntry::FeatureParam kOmniboxUIVerticalMargin12px[] = {
    {OmniboxFieldTrial::kUIVerticalMarginParam, "12"}};
const FeatureEntry::FeatureParam kOmniboxUIVerticalMargin16px[] = {
    {OmniboxFieldTrial::kUIVerticalMarginParam, "16"}};
const FeatureEntry::FeatureParam kOmniboxUIVerticalMargin20px[] = {
    {OmniboxFieldTrial::kUIVerticalMarginParam, "20"}};

const FeatureEntry::FeatureVariation kOmniboxUIVerticalMarginVariations[] = {
    {"4px vertical margin", kOmniboxUIVerticalMargin4px,
     arraysize(kOmniboxUIVerticalMargin4px), nullptr},
    {"8px vertical margin", kOmniboxUIVerticalMargin8px,
     arraysize(kOmniboxUIVerticalMargin8px), nullptr},
    {"12px vertical margin", kOmniboxUIVerticalMargin12px,
     arraysize(kOmniboxUIVerticalMargin12px), nullptr},
    {"16px vertical margin", kOmniboxUIVerticalMargin16px,
     arraysize(kOmniboxUIVerticalMargin16px), nullptr},
    {"20px vertical margin", kOmniboxUIVerticalMargin20px,
     arraysize(kOmniboxUIVerticalMargin20px), nullptr}};

// RECORDING USER METRICS FOR FLAGS:
// -----------------------------------------------------------------------------
// The first line of the entry is the internal name.
//
// To add a new entry, add to the end of kFeatureEntries. There are two
// distinct types of entries:
// . SINGLE_VALUE: entry is either on or off. Use the SINGLE_VALUE_TYPE
//   macro for this type supplying the command line to the macro.
// . MULTI_VALUE: a list of choices, the first of which should correspond to a
//   deactivated state for this lab (i.e. no command line option).  To specify
//   this type of entry use the macro MULTI_VALUE_TYPE supplying it the
//   array of choices.
// See the documentation of FeatureEntry for details on the fields.
//
// Usage of about:flags is logged on startup via the "Launch.FlagsAtStartup"
// UMA histogram. This histogram shows the number of startups with a given flag
// enabled. If you'd like to see user counts instead, make sure to switch to
// to "count users" view on the dashboard. When adding new entries, the enum
// "LoginCustomFlags" must be updated in histograms.xml. See note in
// histograms.xml and don't forget to run AboutFlagsHistogramTest unit test
// to calculate and verify checksum.
//
// When adding a new choice, add it to the end of the list.
const FeatureEntry kFeatureEntries[] = {
    {"ignore-gpu-blacklist", flag_descriptions::kIgnoreGpuBlacklistName,
     flag_descriptions::kIgnoreGpuBlacklistDescription, kOsAll,
     SINGLE_VALUE_TYPE(switches::kIgnoreGpuBlacklist)},
    {"enable-experimental-canvas-features",
     flag_descriptions::kExperimentalCanvasFeaturesName,
     flag_descriptions::kExperimentalCanvasFeaturesDescription, kOsAll,
     SINGLE_VALUE_TYPE(switches::kEnableExperimentalCanvasFeatures)},
    {"disable-accelerated-2d-canvas",
     flag_descriptions::kAccelerated2dCanvasName,
     flag_descriptions::kAccelerated2dCanvasDescription, kOsAll,
     SINGLE_DISABLE_VALUE_TYPE(switches::kDisableAccelerated2dCanvas)},
    {"enable-display-list-2d-canvas",
     flag_descriptions::kDisplayList2dCanvasName,
     flag_descriptions::kDisplayList2dCanvasDescription, kOsAll,
     ENABLE_DISABLE_VALUE_TYPE(switches::kEnableDisplayList2dCanvas,
                               switches::kDisableDisplayList2dCanvas)},
    {"enable-canvas-2d-dynamic-rendering-mode-switching",
     flag_descriptions::kEnable2dCanvasDynamicRenderingModeSwitchingName,
     flag_descriptions::kEnable2dCanvasDynamicRenderingModeSwitchingDescription,
     kOsAll,
     SINGLE_VALUE_TYPE(switches::kEnableCanvas2dDynamicRenderingModeSwitching)},
    {"composited-layer-borders", flag_descriptions::kCompositedLayerBordersName,
     flag_descriptions::kCompositedLayerBordersDescription, kOsAll,
     SINGLE_VALUE_TYPE(cc::switches::kShowCompositedLayerBorders)},
    {"gl-composited-texture-quad-borders",
     flag_descriptions::kGlCompositedTextureQuadBordersName,
     flag_descriptions::kGlCompositedTextureQuadBordersDescription, kOsAll,
     SINGLE_VALUE_TYPE(cc::switches::kGlCompositedTextureQuadBorder)},
    {"show-overdraw-feedback", flag_descriptions::kShowOverdrawFeedbackName,
     flag_descriptions::kShowOverdrawFeedbackDescription, kOsAll,
     SINGLE_VALUE_TYPE(cc::switches::kShowOverdrawFeedback)},
    {"ui-disable-partial-swap", flag_descriptions::kUiPartialSwapName,
     flag_descriptions::kUiPartialSwapDescription, kOsAll,
     SINGLE_DISABLE_VALUE_TYPE(switches::kUIDisablePartialSwap)},
#if BUILDFLAG(ENABLE_WEBRTC)
    {"disable-webrtc-hw-decoding", flag_descriptions::kWebrtcHwDecodingName,
     flag_descriptions::kWebrtcHwDecodingDescription, kOsAndroid | kOsCrOS,
     SINGLE_DISABLE_VALUE_TYPE(switches::kDisableWebRtcHWDecoding)},
    {"disable-webrtc-hw-encoding", flag_descriptions::kWebrtcHwEncodingName,
     flag_descriptions::kWebrtcHwEncodingDescription, kOsAndroid | kOsCrOS,
     SINGLE_DISABLE_VALUE_TYPE(switches::kDisableWebRtcHWEncoding)},
    {"enable-webrtc-hw-h264-encoding",
     flag_descriptions::kWebrtcHwH264EncodingName,
     flag_descriptions::kWebrtcHwH264EncodingDescription, kOsAndroid | kOsCrOS,
     FEATURE_VALUE_TYPE(features::kWebRtcHWH264Encoding)},
    {"enable-webrtc-hw-vp8-encoding",
     flag_descriptions::kWebrtcHwVP8EncodingName,
     flag_descriptions::kWebrtcHwVP8EncodingDescription, kOsAndroid | kOsCrOS,
     FEATURE_VALUE_TYPE(features::kWebRtcHWVP8Encoding)},
    {"enable-webrtc-srtp-aes-gcm", flag_descriptions::kWebrtcSrtpAesGcmName,
     flag_descriptions::kWebrtcSrtpAesGcmDescription, kOsAll,
     SINGLE_VALUE_TYPE(switches::kEnableWebRtcSrtpAesGcm)},
    {"enable-webrtc-stun-origin", flag_descriptions::kWebrtcStunOriginName,
     flag_descriptions::kWebrtcStunOriginDescription, kOsAll,
     SINGLE_VALUE_TYPE(switches::kEnableWebRtcStunOrigin)},
    {"WebRtcUseEchoCanceller3", flag_descriptions::kWebrtcEchoCanceller3Name,
     flag_descriptions::kWebrtcEchoCanceller3Description, kOsAll,
     FEATURE_VALUE_TYPE(features::kWebRtcUseEchoCanceller3)},
#endif  // ENABLE_WEBRTC
#if defined(OS_ANDROID)
    {"enable-osk-overscroll", flag_descriptions::kEnableOskOverscrollName,
     flag_descriptions::kEnableOskOverscrollDescription, kOsAndroid,
     SINGLE_VALUE_TYPE(switches::kEnableOSKOverscroll)},
    {"enable-new-photo-picker", flag_descriptions::kNewPhotoPickerName,
     flag_descriptions::kNewPhotoPickerDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kNewPhotoPicker)},
    {"enable-usermedia-screen-capturing",
     flag_descriptions::kMediaScreenCaptureName,
     flag_descriptions::kMediaScreenCaptureDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kUserMediaScreenCapturing)},
#endif  // OS_ANDROID
// Native client is compiled out when DISABLE_NACL is defined.
#if !defined(DISABLE_NACL)
    {"enable-nacl", flag_descriptions::kNaclName,
     flag_descriptions::kNaclDescription, kOsAll,
     SINGLE_VALUE_TYPE(switches::kEnableNaCl)},
    {"enable-nacl-debug", flag_descriptions::kNaclDebugName,
     flag_descriptions::kNaclDebugDescription, kOsDesktop,
     SINGLE_VALUE_TYPE(switches::kEnableNaClDebug)},
    {"force-pnacl-subzero", flag_descriptions::kPnaclSubzeroName,
     flag_descriptions::kPnaclSubzeroDescription, kOsDesktop,
     SINGLE_VALUE_TYPE(switches::kForcePNaClSubzero)},
    {"nacl-debug-mask", flag_descriptions::kNaclDebugMaskName,
     flag_descriptions::kNaclDebugMaskDescription, kOsDesktop,
     MULTI_VALUE_TYPE(kNaClDebugMaskChoices)},
#endif  // DISABLE_NACL
#if BUILDFLAG(ENABLE_EXTENSIONS)
    {"extension-apis", flag_descriptions::kExperimentalExtensionApisName,
     flag_descriptions::kExperimentalExtensionApisDescription, kOsDesktop,
     SINGLE_VALUE_TYPE(extensions::switches::kEnableExperimentalExtensionApis)},
    {"extensions-on-chrome-urls",
     flag_descriptions::kExtensionsOnChromeUrlsName,
     flag_descriptions::kExtensionsOnChromeUrlsDescription, kOsAll,
     SINGLE_VALUE_TYPE(extensions::switches::kExtensionsOnChromeURLs)},
#endif  // ENABLE_EXTENSIONS
    {"enable-fast-unload", flag_descriptions::kFastUnloadName,
     flag_descriptions::kFastUnloadDescription, kOsAll,
     SINGLE_VALUE_TYPE(switches::kEnableFastUnload)},
    {"enable-history-entry-requires-user-gesture",
     flag_descriptions::kHistoryRequiresUserGestureName,
     flag_descriptions::kHistoryRequiresUserGestureDescription, kOsAll,
     SINGLE_VALUE_TYPE(switches::kHistoryEntryRequiresUserGesture)},
    {"disable-hyperlink-auditing", flag_descriptions::kHyperlinkAuditingName,
     flag_descriptions::kHyperlinkAuditingDescription, kOsAll,
     SINGLE_DISABLE_VALUE_TYPE(switches::kNoPings)},
#if defined(OS_ANDROID)
    {"contextual-search", flag_descriptions::kContextualSearchName,
     flag_descriptions::kContextualSearchDescription, kOsAndroid,
     ENABLE_DISABLE_VALUE_TYPE(switches::kEnableContextualSearch,
                               switches::kDisableContextualSearch)},
    {"cs-contextual-cards-single-actions",
     flag_descriptions::kContextualSearchSingleActionsName,
     flag_descriptions::kContextualSearchSingleActionsDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kContextualSearchSingleActions)},
    {"cs-contextual-cards-url-actions",
     flag_descriptions::kContextualSearchUrlActionsName,
     flag_descriptions::kContextualSearchUrlActionsDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kContextualSearchUrlActions)},
#endif  // OS_ANDROID
    {"show-autofill-type-predictions",
     flag_descriptions::kShowAutofillTypePredictionsName,
     flag_descriptions::kShowAutofillTypePredictionsDescription, kOsAll,
     SINGLE_VALUE_TYPE(autofill::switches::kShowAutofillTypePredictions)},
    {"smooth-scrolling", flag_descriptions::kSmoothScrollingName,
     flag_descriptions::kSmoothScrollingDescription,
     // Mac has a separate implementation with its own setting to disable.
     kOsLinux | kOsCrOS | kOsWin | kOsAndroid,
     ENABLE_DISABLE_VALUE_TYPE(switches::kEnableSmoothScrolling,
                               switches::kDisableSmoothScrolling)},
#if defined(USE_AURA)
    {"overlay-scrollbars", flag_descriptions::kOverlayScrollbarsName,
     flag_descriptions::kOverlayScrollbarsDescription,
     // Uses the system preference on Mac (a different implementation).
     // On Android, this is always enabled.
     kOsAura, FEATURE_VALUE_TYPE(features::kOverlayScrollbar)},
#endif  // USE_AURA
    {   // See http://crbug.com/120416 for how to remove this flag.
     "save-page-as-mhtml", flag_descriptions::kSavePageAsMhtmlName,
     flag_descriptions::kSavePageAsMhtmlDescription, kOsMac | kOsWin | kOsLinux,
     SINGLE_VALUE_TYPE(switches::kSavePageAsMHTML)},
    {"mhtml-generator-option", flag_descriptions::kMhtmlGeneratorOptionName,
     flag_descriptions::kMhtmlGeneratorOptionDescription,
     kOsMac | kOsWin | kOsLinux,
     MULTI_VALUE_TYPE(kMHTMLGeneratorOptionChoices)},
    {"enable-quic", flag_descriptions::kQuicName,
     flag_descriptions::kQuicDescription, kOsAll,
     ENABLE_DISABLE_VALUE_TYPE(switches::kEnableQuic, switches::kDisableQuic)},
    {"disable-javascript-harmony-shipping",
     flag_descriptions::kJavascriptHarmonyShippingName,
     flag_descriptions::kJavascriptHarmonyShippingDescription, kOsAll,
     SINGLE_DISABLE_VALUE_TYPE(switches::kDisableJavaScriptHarmonyShipping)},
    {"enable-javascript-harmony", flag_descriptions::kJavascriptHarmonyName,
     flag_descriptions::kJavascriptHarmonyDescription, kOsAll,
     SINGLE_VALUE_TYPE(switches::kJavaScriptHarmony)},
    {"enable-asm-webassembly", flag_descriptions::kEnableAsmWasmName,
     flag_descriptions::kEnableAsmWasmDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kAsmJsToWebAssembly)},
    {"enable-webassembly", flag_descriptions::kEnableWasmName,
     flag_descriptions::kEnableWasmDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kWebAssembly)},
    {"enable-webassembly-streaming",
     flag_descriptions::kEnableWasmStreamingName,
     flag_descriptions::kEnableWasmStreamingDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kWebAssemblyStreaming)},
    {"shared-array-buffer", flag_descriptions::kEnableSharedArrayBufferName,
     flag_descriptions::kEnableSharedArrayBufferDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kSharedArrayBuffer)},
    {"disable-v8-ignition-turbo",
     flag_descriptions::kV8DisableIgnitionTurboName,
     flag_descriptions::kV8DisableIgnitionTurboDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kV8NoTurbo)},
    {"disable-software-rasterizer", flag_descriptions::kSoftwareRasterizerName,
     flag_descriptions::kSoftwareRasterizerDescription,
#if BUILDFLAG(ENABLE_SWIFTSHADER)
     kOsAll,
#else   // BUILDFLAG(ENABLE_SWIFTSHADER)
     0,
#endif  // BUILDFLAG(ENABLE_SWIFTSHADER)
     SINGLE_DISABLE_VALUE_TYPE(switches::kDisableSoftwareRasterizer)},
    {"enable-gpu-rasterization", flag_descriptions::kGpuRasterizationName,
     flag_descriptions::kGpuRasterizationDescription, kOsAll,
     MULTI_VALUE_TYPE(kEnableGpuRasterizationChoices)},
    {"gpu-rasterization-msaa-sample-count",
     flag_descriptions::kGpuRasterizationMsaaSampleCountName,
     flag_descriptions::kGpuRasterizationMsaaSampleCountDescription, kOsAll,
     MULTI_VALUE_TYPE(kGpuRasterizationMSAASampleCountChoices)},
    {"slimming-paint-invalidation",
     flag_descriptions::kSlimmingPaintInvalidationName,
     flag_descriptions::kSlimmingPaintInvalidationDescription, kOsAll,
     ENABLE_DISABLE_VALUE_TYPE(switches::kEnableSlimmingPaintInvalidation,
                               switches::kDisableSlimmingPaintInvalidation)},
    {"enable-experimental-web-platform-features",
     flag_descriptions::kExperimentalWebPlatformFeaturesName,
     flag_descriptions::kExperimentalWebPlatformFeaturesDescription, kOsAll,
     SINGLE_VALUE_TYPE(switches::kEnableExperimentalWebPlatformFeatures)},
#if BUILDFLAG(ENABLE_EXTENSIONS)
    {"enable-ble-advertising-in-apps",
     flag_descriptions::kBleAdvertisingInExtensionsName,
     flag_descriptions::kBleAdvertisingInExtensionsDescription, kOsCrOS,
     SINGLE_VALUE_TYPE(extensions::switches::kEnableBLEAdvertising)},
#endif  // ENABLE_EXTENSIONS
    {"enable-devtools-experiments", flag_descriptions::kDevtoolsExperimentsName,
     flag_descriptions::kDevtoolsExperimentsDescription, kOsDesktop,
     SINGLE_VALUE_TYPE(switches::kEnableDevToolsExperiments)},
    {"silent-debugger-extension-api",
     flag_descriptions::kSilentDebuggerExtensionApiName,
     flag_descriptions::kSilentDebuggerExtensionApiDescription, kOsDesktop,
     SINGLE_VALUE_TYPE(switches::kSilentDebuggerExtensionAPI)},
#if BUILDFLAG(ENABLE_SPELLCHECK) && defined(OS_ANDROID)
    {"enable-android-spellchecker",
     flag_descriptions::kEnableAndroidSpellcheckerDescription,
     flag_descriptions::kEnableAndroidSpellcheckerDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(spellcheck::kAndroidSpellCheckerNonLowEnd)},
#endif  // ENABLE_SPELLCHECK && OS_ANDROID
    {"enable-scroll-prediction", flag_descriptions::kScrollPredictionName,
     flag_descriptions::kScrollPredictionDescription, kOsDesktop,
     SINGLE_VALUE_TYPE(switches::kEnableScrollPrediction)},
    {"top-chrome-md", flag_descriptions::kTopChromeMd,
     flag_descriptions::kTopChromeMdDescription, kOsDesktop,
     MULTI_VALUE_TYPE(kTopChromeMaterialDesignChoices)},
    {"enable-site-details", flag_descriptions::kSiteDetails,
     flag_descriptions::kSiteDetailsDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kSiteDetails)},
    {"enable-site-settings", flag_descriptions::kSiteSettings,
     flag_descriptions::kSiteSettingsDescription, kOsDesktop,
     SINGLE_VALUE_TYPE(switches::kEnableSiteSettings)},
    {"secondary-ui-md", flag_descriptions::kSecondaryUiMd,
     flag_descriptions::kSecondaryUiMdDescription, kOsDesktop,
     SINGLE_VALUE_TYPE(switches::kExtendMdToSecondaryUi)},
    {"touch-events", flag_descriptions::kTouchEventsName,
     flag_descriptions::kTouchEventsDescription, kOsDesktop,
     MULTI_VALUE_TYPE(kTouchEventFeatureDetectionChoices)},
    {"disable-touch-adjustment", flag_descriptions::kTouchAdjustmentName,
     flag_descriptions::kTouchAdjustmentDescription,
     kOsWin | kOsLinux | kOsCrOS | kOsAndroid,
     SINGLE_DISABLE_VALUE_TYPE(switches::kDisableTouchAdjustment)},
#if defined(OS_CHROMEOS)
    {"network-portal-notification",
     flag_descriptions::kNetworkPortalNotificationName,
     flag_descriptions::kNetworkPortalNotificationDescription, kOsCrOS,
     ENABLE_DISABLE_VALUE_TYPE(
         chromeos::switches::kEnableNetworkPortalNotification,
         chromeos::switches::kDisableNetworkPortalNotification)},
#endif  // OS_CHROMEOS
#if defined(OS_ANDROID)
    {"enable-media-document-download-button",
     flag_descriptions::kMediaDocumentDownloadButtonName,
     flag_descriptions::kMediaDocumentDownloadButtonDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(features::kMediaDocumentDownloadButton)},
#endif  // OS_ANDROID
#if BUILDFLAG(ENABLE_PLUGINS)
    {"prefer-html-over-flash", flag_descriptions::kPreferHtmlOverPluginsName,
     flag_descriptions::kPreferHtmlOverPluginsDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kPreferHtmlOverPlugins)},
    {"allow-nacl-socket-api", flag_descriptions::kAllowNaclSocketApiName,
     flag_descriptions::kAllowNaclSocketApiDescription, kOsDesktop,
     SINGLE_VALUE_TYPE_AND_VALUE(switches::kAllowNaClSocketAPI, "*")},
    {"run-all-flash-in-allow-mode",
     flag_descriptions::kRunAllFlashInAllowModeName,
     flag_descriptions::kRunAllFlashInAllowModeDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kRunAllFlashInAllowMode)},
#endif  // ENABLE_PLUGINS
#if defined(OS_CHROMEOS)
    {"mus", flag_descriptions::kUseMusName,
     flag_descriptions::kUseMusDescription, kOsCrOS,
     MULTI_VALUE_TYPE(kUseMusChoices)},
    {"allow-touchpad-three-finger-click",
     flag_descriptions::kAllowTouchpadThreeFingerClickName,
     flag_descriptions::kAllowTouchpadThreeFingerClickDescription, kOsCrOS,
     SINGLE_VALUE_TYPE(chromeos::switches::kEnableTouchpadThreeFingerClick)},
    {"ash-enable-unified-desktop",
     flag_descriptions::kAshEnableUnifiedDesktopName,
     flag_descriptions::kAshEnableUnifiedDesktopDescription, kOsCrOS,
     SINGLE_VALUE_TYPE(switches::kEnableUnifiedDesktop)},
    {"enable-easy-unlock-proximity-detection",
     flag_descriptions::kEasyUnlockProximityDetectionName,
     flag_descriptions::kEasyUnlockProximityDetectionDescription, kOsCrOS,
     SINGLE_VALUE_TYPE(proximity_auth::switches::kEnableProximityDetection)},
    {"enable-easy-unlock-bluetooth-low-energy-detection",
     flag_descriptions::kEasyUnlockBluetoothLowEnergyDiscoveryName,
     flag_descriptions::kEasyUnlockBluetoothLowEnergyDiscoveryDescription,
     kOsCrOS,
     SINGLE_VALUE_TYPE(
         proximity_auth::switches::kEnableBluetoothLowEnergyDiscovery)},
#endif  // OS_CHROMEOS
#if defined(USE_ASH)
    {"show-touch-hud", flag_descriptions::kShowTouchHudName,
     flag_descriptions::kShowTouchHudDescription, kOsAll,
     SINGLE_VALUE_TYPE(ash::switches::kAshTouchHud)},
    {
        "enable-pinch", flag_descriptions::kPinchScaleName,
        flag_descriptions::kPinchScaleDescription, kOsLinux | kOsWin | kOsCrOS,
        ENABLE_DISABLE_VALUE_TYPE(switches::kEnablePinch,
                                  switches::kDisablePinch),
    },
#endif  // USE_ASH
#if defined(OS_CHROMEOS)
    {
        "disable-boot-animation", flag_descriptions::kBootAnimationName,
        flag_descriptions::kBootAnimationDescription, kOsCrOSOwnerOnly,
        SINGLE_DISABLE_VALUE_TYPE(chromeos::switches::kDisableBootAnimation),
    },
    {"enable-video-player-chromecast-support",
     flag_descriptions::kVideoPlayerChromecastSupportName,
     flag_descriptions::kVideoPlayerChromecastSupportDescription, kOsCrOS,
     SINGLE_VALUE_TYPE(
         chromeos::switches::kEnableVideoPlayerChromecastSupport)},
    {
        "disable-office-editing-component-app",
        flag_descriptions::kOfficeEditingComponentAppName,
        flag_descriptions::kOfficeEditingComponentAppDescription, kOsCrOS,
        SINGLE_DISABLE_VALUE_TYPE(
            chromeos::switches::kDisableOfficeEditingComponentApp),
    },
    {
        "disable-display-color-calibration",
        flag_descriptions::kDisplayColorCalibrationName,
        flag_descriptions::kDisplayColorCalibrationDescription, kOsCrOS,
        SINGLE_DISABLE_VALUE_TYPE(::switches::kDisableDisplayColorCalibration),
    },
    {"enable-tether", flag_descriptions::kTetherName,
     flag_descriptions::kTetherDescription, kOsCrOS,
     SINGLE_VALUE_TYPE(chromeos::switches::kEnableTether)},
#endif  // OS_CHROMEOS
    {
        "disable-accelerated-video-decode",
        flag_descriptions::kAcceleratedVideoDecodeName,
        flag_descriptions::kAcceleratedVideoDecodeDescription,
        kOsMac | kOsWin | kOsCrOS | kOsAndroid,
        SINGLE_DISABLE_VALUE_TYPE(switches::kDisableAcceleratedVideoDecode),
    },
#if defined(OS_WIN)
    {
        "enable-hdr", flag_descriptions::kEnableHDRName,
        flag_descriptions::kEnableHDRDescription, kOsWin,
        SINGLE_VALUE_TYPE(switches::kEnableHDR),
    },
#endif  // OS_WIN
#if defined(USE_ASH)
    {
        "ash-debug-shortcuts", flag_descriptions::kDebugShortcutsName,
        flag_descriptions::kDebugShortcutsDescription, kOsAll,
        SINGLE_VALUE_TYPE(ash::switches::kAshDebugShortcuts),
    },
    {
        "ash-enable-mirrored-screen",
        flag_descriptions::kAshEnableMirroredScreenName,
        flag_descriptions::kAshEnableMirroredScreenDescription, kOsCrOS,
        SINGLE_VALUE_TYPE(ash::switches::kAshEnableMirroredScreen),
    },
    {"ash-shelf-color", flag_descriptions::kAshShelfColorName,
     flag_descriptions::kAshShelfColorDescription, kOsCrOS,
     MULTI_VALUE_TYPE(kAshShelfColorChoices)},
    {"ash-shelf-color-scheme", flag_descriptions::kAshShelfColorScheme,
     flag_descriptions::kAshShelfColorSchemeDescription, kOsCrOS,
     MULTI_VALUE_TYPE(kAshShelfColorSchemeChoices)},
#endif  // USE_ASH
#if defined(OS_CHROMEOS)
    {"material-design-ink-drop-animation-speed",
     flag_descriptions::kMaterialDesignInkDropAnimationSpeedName,
     flag_descriptions::kMaterialDesignInkDropAnimationSpeedDescription,
     kOsCrOS, MULTI_VALUE_TYPE(kAshMaterialDesignInkDropAnimationSpeed)},
    {"ui-slow-animations", flag_descriptions::kUiSlowAnimationsName,
     flag_descriptions::kUiSlowAnimationsDescription, kOsCrOS,
     SINGLE_VALUE_TYPE(switches::kUISlowAnimations)},
    {"ui-show-composited-layer-borders",
     flag_descriptions::kUiShowCompositedLayerBordersName,
     flag_descriptions::kUiShowCompositedLayerBordersDescription, kOsCrOS,
     MULTI_VALUE_TYPE(kUiShowCompositedLayerBordersChoices)},
    {"disable-cloud-import", flag_descriptions::kCloudImportName,
     flag_descriptions::kCloudImportDescription, kOsCrOS,
     SINGLE_DISABLE_VALUE_TYPE(chromeos::switches::kDisableCloudImport)},
    {"enable-request-tablet-site", flag_descriptions::kRequestTabletSiteName,
     flag_descriptions::kRequestTabletSiteDescription, kOsCrOS,
     SINGLE_VALUE_TYPE(chromeos::switches::kEnableRequestTabletSite)},
#endif  // OS_CHROMEOS
    {"debug-packed-apps", flag_descriptions::kDebugPackedAppName,
     flag_descriptions::kDebugPackedAppDescription, kOsDesktop,
     SINGLE_VALUE_TYPE(switches::kDebugPackedApps)},
    {"enable-password-generation", flag_descriptions::kPasswordGenerationName,
     flag_descriptions::kPasswordGenerationDescription, kOsAll,
     ENABLE_DISABLE_VALUE_TYPE(autofill::switches::kEnablePasswordGeneration,
                               autofill::switches::kDisablePasswordGeneration)},
    {"enable-password-force-saving",
     flag_descriptions::kPasswordForceSavingName,
     flag_descriptions::kPasswordForceSavingDescription, kOsAll,
     FEATURE_VALUE_TYPE(
         password_manager::features::kEnablePasswordForceSaving)},
    {"enable-manual-password-generation",
     flag_descriptions::kManualPasswordGenerationName,
     flag_descriptions::kManualPasswordGenerationDescription, kOsAll,
     FEATURE_VALUE_TYPE(
         password_manager::features::kEnableManualPasswordGeneration)},
    {"enable-show-autofill-signatures",
     flag_descriptions::kShowAutofillSignaturesName,
     flag_descriptions::kShowAutofillSignaturesDescription, kOsAll,
     SINGLE_VALUE_TYPE(autofill::switches::kShowAutofillSignatures)},
    {"affiliation-based-matching",
     flag_descriptions::kAffiliationBasedMatchingName,
     flag_descriptions::kAffiliationBasedMatchingDescription, kOsAll,
     FEATURE_VALUE_TYPE(password_manager::features::kAffiliationBasedMatching)},
    {"wallet-service-use-sandbox",
     flag_descriptions::kWalletServiceUseSandboxName,
     flag_descriptions::kWalletServiceUseSandboxDescription,
     kOsAndroid | kOsDesktop,
     ENABLE_DISABLE_VALUE_TYPE_AND_VALUE(
         autofill::switches::kWalletServiceUseSandbox,
         "1",
         autofill::switches::kWalletServiceUseSandbox,
         "0")},
#if defined(USE_AURA)
    {"overscroll-history-navigation",
     flag_descriptions::kOverscrollHistoryNavigationName,
     flag_descriptions::kOverscrollHistoryNavigationDescription, kOsAura,
     MULTI_VALUE_TYPE(kOverscrollHistoryNavigationChoices)},
    {"overscroll-start-threshold",
     flag_descriptions::kOverscrollStartThresholdName,
     flag_descriptions::kOverscrollStartThresholdDescription, kOsAura,
     MULTI_VALUE_TYPE(kOverscrollStartThresholdChoices)},
#endif  // USE_AURA
    {"scroll-end-effect", flag_descriptions::kScrollEndEffectName,
     flag_descriptions::kScrollEndEffectDescription, kOsCrOS,
     ENABLE_DISABLE_VALUE_TYPE_AND_VALUE(switches::kScrollEndEffect,
                                         "1",
                                         switches::kScrollEndEffect,
                                         "0")},
    {"enable-touch-drag-drop", flag_descriptions::kTouchDragDropName,
     flag_descriptions::kTouchDragDropDescription, kOsWin | kOsCrOS,
     ENABLE_DISABLE_VALUE_TYPE(switches::kEnableTouchDragDrop,
                               switches::kDisableTouchDragDrop)},
    {"touch-selection-strategy", flag_descriptions::kTouchSelectionStrategyName,
     flag_descriptions::kTouchSelectionStrategyDescription,
     kOsAndroid,  // TODO(mfomitchev): Add CrOS/Win/Linux support soon.
     MULTI_VALUE_TYPE(kTouchTextSelectionStrategyChoices)},
    {"enable-navigation-tracing",
     flag_descriptions::kEnableNavigationTracingName,
     flag_descriptions::kEnableNavigationTracingDescription, kOsAll,
     SINGLE_VALUE_TYPE(switches::kEnableNavigationTracing)},
    {"trace-upload-url", flag_descriptions::kTraceUploadUrlName,
     flag_descriptions::kTraceUploadUrlDescription, kOsAll,
     MULTI_VALUE_TYPE(kTraceUploadURL)},
    {"enable-service-worker-navigation-preload",
     flag_descriptions::kServiceWorkerNavigationPreloadName,
     flag_descriptions::kServiceWorkerNavigationPreloadDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kServiceWorkerNavigationPreload)},
    {"enable-suggestions-with-substring-match",
     flag_descriptions::kSuggestionsWithSubStringMatchName,
     flag_descriptions::kSuggestionsWithSubStringMatchDescription, kOsAll,
     SINGLE_VALUE_TYPE(
         autofill::switches::kEnableSuggestionsWithSubstringMatch)},
    {"enable-supervised-user-managed-bookmarks-folder",
     flag_descriptions::kSupervisedUserManagedBookmarksFolderName,
     flag_descriptions::kSupervisedUserManagedBookmarksFolderDescription,
     kOsAll,
     SINGLE_VALUE_TYPE(switches::kEnableSupervisedUserManagedBookmarksFolder)},
#if BUILDFLAG(ENABLE_APP_LIST)
    {"enable-sync-app-list", flag_descriptions::kSyncAppListName,
     flag_descriptions::kSyncAppListDescription, kOsDesktop,
     ENABLE_DISABLE_VALUE_TYPE(app_list::switches::kEnableSyncAppList,
                               app_list::switches::kDisableSyncAppList)},
#endif  // ENABLE_APP_LIST
    {"lcd-text-aa", flag_descriptions::kLcdTextName,
     flag_descriptions::kLcdTextDescription, kOsDesktop,
     ENABLE_DISABLE_VALUE_TYPE(switches::kEnableLCDText,
                               switches::kDisableLCDText)},
    {"enable-offer-store-unmasked-wallet-cards",
     flag_descriptions::kOfferStoreUnmaskedWalletCardsName,
     flag_descriptions::kOfferStoreUnmaskedWalletCardsDescription, kOsAll,
     ENABLE_DISABLE_VALUE_TYPE(
         autofill::switches::kEnableOfferStoreUnmaskedWalletCards,
         autofill::switches::kDisableOfferStoreUnmaskedWalletCards)},
    {"enable-offline-auto-reload", flag_descriptions::kOfflineAutoReloadName,
     flag_descriptions::kOfflineAutoReloadDescription, kOsAll,
     ENABLE_DISABLE_VALUE_TYPE(switches::kEnableOfflineAutoReload,
                               switches::kDisableOfflineAutoReload)},
    {"enable-offline-auto-reload-visible-only",
     flag_descriptions::kOfflineAutoReloadVisibleOnlyName,
     flag_descriptions::kOfflineAutoReloadVisibleOnlyDescription, kOsAll,
     ENABLE_DISABLE_VALUE_TYPE(switches::kEnableOfflineAutoReloadVisibleOnly,
                               switches::kDisableOfflineAutoReloadVisibleOnly)},
    {"show-saved-copy", flag_descriptions::kShowSavedCopyName,
     flag_descriptions::kShowSavedCopyDescription, kOsAll,
     MULTI_VALUE_TYPE(kShowSavedCopyChoices)},
    {"default-tile-width", flag_descriptions::kDefaultTileWidthName,
     flag_descriptions::kDefaultTileWidthDescription, kOsAll,
     MULTI_VALUE_TYPE(kDefaultTileWidthChoices)},
    {"default-tile-height", flag_descriptions::kDefaultTileHeightName,
     flag_descriptions::kDefaultTileHeightDescription, kOsAll,
     MULTI_VALUE_TYPE(kDefaultTileHeightChoices)},
#if defined(OS_CHROMEOS)
    {"enable-virtual-keyboard", flag_descriptions::kVirtualKeyboardName,
     flag_descriptions::kVirtualKeyboardDescription, kOsCrOS,
     SINGLE_VALUE_TYPE(keyboard::switches::kEnableVirtualKeyboard)},
    {"virtual-keyboard-overscroll",
     flag_descriptions::kVirtualKeyboardOverscrollName,
     flag_descriptions::kVirtualKeyboardOverscrollDescription, kOsCrOS,
     SINGLE_DISABLE_VALUE_TYPE(
         keyboard::switches::kDisableVirtualKeyboardOverscroll)},
    {"input-view", flag_descriptions::kInputViewName,
     flag_descriptions::kInputViewDescription, kOsCrOS,
     SINGLE_DISABLE_VALUE_TYPE(keyboard::switches::kDisableInputView)},
    {"disable-new-korean-ime", flag_descriptions::kNewKoreanImeName,
     flag_descriptions::kNewKoreanImeDescription, kOsCrOS,
     SINGLE_DISABLE_VALUE_TYPE(chromeos::switches::kDisableNewKoreanIme)},
    {"enable-physical-keyboard-autocorrect",
     flag_descriptions::kPhysicalKeyboardAutocorrectName,
     flag_descriptions::kPhysicalKeyboardAutocorrectDescription, kOsCrOS,
     ENABLE_DISABLE_VALUE_TYPE(
         chromeos::switches::kEnablePhysicalKeyboardAutocorrect,
         chromeos::switches::kDisablePhysicalKeyboardAutocorrect)},
    {"disable-voice-input", flag_descriptions::kVoiceInputName,
     flag_descriptions::kVoiceInputDescription, kOsCrOS,
     SINGLE_DISABLE_VALUE_TYPE(keyboard::switches::kDisableVoiceInput)},
    {"experimental-input-view-features",
     flag_descriptions::kExperimentalInputViewFeaturesName,
     flag_descriptions::kExperimentalInputViewFeaturesDescription, kOsCrOS,
     SINGLE_VALUE_TYPE(
         keyboard::switches::kEnableExperimentalInputViewFeatures)},
    {"floating-virtual-keyboard",
     flag_descriptions::kFloatingVirtualKeyboardName,
     flag_descriptions::kFloatingVirtualKeyboardDescription, kOsCrOS,
     SINGLE_VALUE_TYPE(keyboard::switches::kEnableFloatingVirtualKeyboard)},
    {"gesture-typing", flag_descriptions::kGestureTypingName,
     flag_descriptions::kGestureTypingDescription, kOsCrOS,
     SINGLE_DISABLE_VALUE_TYPE(keyboard::switches::kDisableGestureTyping)},
    {"gesture-editing", flag_descriptions::kGestureEditingName,
     flag_descriptions::kGestureEditingDescription, kOsCrOS,
     SINGLE_DISABLE_VALUE_TYPE(keyboard::switches::kDisableGestureEditing)},
#endif  // OS_CHROMEOS
    {"enable-simple-cache-backend", flag_descriptions::kSimpleCacheBackendName,
     flag_descriptions::kSimpleCacheBackendDescription,
     kOsWin | kOsMac | kOsLinux | kOsCrOS,
     MULTI_VALUE_TYPE(kSimpleCacheBackendChoices)},
    {"enable-tcp-fast-open", flag_descriptions::kTcpFastOpenName,
     flag_descriptions::kTcpFastOpenDescription,
     kOsLinux | kOsCrOS | kOsAndroid,
     SINGLE_VALUE_TYPE(switches::kEnableTcpFastOpen)},
#if BUILDFLAG(ENABLE_SERVICE_DISCOVERY)
    {"device-discovery-notifications",
     flag_descriptions::kDeviceDiscoveryNotificationsName,
     flag_descriptions::kDeviceDiscoveryNotificationsDescription, kOsDesktop,
     ENABLE_DISABLE_VALUE_TYPE(switches::kEnableDeviceDiscoveryNotifications,
                               switches::kDisableDeviceDiscoveryNotifications)},
    {"enable-print-preview-register-promos",
     flag_descriptions::kPrintPreviewRegisterPromosName,
     flag_descriptions::kPrintPreviewRegisterPromosDescription, kOsDesktop,
     SINGLE_VALUE_TYPE(switches::kEnablePrintPreviewRegisterPromos)},
#endif  // BUILDFLAG(ENABLE_SERVICE_DISCOVERY)
#if defined(OS_WIN)
    {"enable-cloud-print-xps", flag_descriptions::kCloudPrintXpsName,
     flag_descriptions::kCloudPrintXpsDescription, kOsWin,
     SINGLE_VALUE_TYPE(switches::kEnableCloudPrintXps)},
#endif  // OS_WIN
#if defined(TOOLKIT_VIEWS)
    {"disable-hide-inactive-stacked-tab-close-buttons",
     flag_descriptions::kHideInactiveStackedTabCloseButtonsName,
     flag_descriptions::kHideInactiveStackedTabCloseButtonsDescription,
     kOsCrOS | kOsWin | kOsLinux,
     SINGLE_DISABLE_VALUE_TYPE(
         switches::kDisableHideInactiveStackedTabCloseButtons)},
#endif  // TOOLKIT_VIEWS
#if BUILDFLAG(ENABLE_SPELLCHECK)
    {"enable-spelling-feedback-field-trial",
     flag_descriptions::kSpellingFeedbackFieldTrialName,
     flag_descriptions::kSpellingFeedbackFieldTrialDescription, kOsAll,
     SINGLE_VALUE_TYPE(
         spellcheck::switches::kEnableSpellingFeedbackFieldTrial)},
#endif  // ENABLE_SPELLCHECK
    {"enable-webgl-draft-extensions",
     flag_descriptions::kWebglDraftExtensionsName,
     flag_descriptions::kWebglDraftExtensionsDescription, kOsAll,
     SINGLE_VALUE_TYPE(switches::kEnableWebGLDraftExtensions)},
#if !defined(OS_ANDROID)
    {"enable-account-consistency", flag_descriptions::kAccountConsistencyName,
     flag_descriptions::kAccountConsistencyDescription, kOsDesktop,
     SINGLE_VALUE_TYPE(switches::kEnableAccountConsistency)},
#endif
#if BUILDFLAG(ENABLE_APP_LIST)
    {"reset-app-list-install-state",
     flag_descriptions::kResetAppListInstallStateName,
     flag_descriptions::kResetAppListInstallStateDescription,
     kOsMac | kOsWin | kOsLinux,
     SINGLE_VALUE_TYPE(app_list::switches::kResetAppListInstallState)},
#endif  // BUILDFLAG(ENABLE_APP_LIST)
#if defined(OS_ANDROID)
    {"enable-special-locale", flag_descriptions::kEnableSpecialLocaleName,
     flag_descriptions::kEnableSpecialLocaleDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kSpecialLocaleFeature)},
    {"enable-accessibility-tab-switcher",
     flag_descriptions::kAccessibilityTabSwitcherName,
     flag_descriptions::kAccessibilityTabSwitcherDescription, kOsAndroid,
     SINGLE_VALUE_TYPE(switches::kEnableAccessibilityTabSwitcher)},
    {"enable-android-autofill-accessibility",
     flag_descriptions::kAndroidAutofillAccessibilityName,
     flag_descriptions::kAndroidAutofillAccessibilityDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(features::kAndroidAutofillAccessibility)},
    {"enable-physical-web", flag_descriptions::kEnablePhysicalWebName,
     flag_descriptions::kEnablePhysicalWebDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kPhysicalWebFeature)},
#endif  // OS_ANDROID
    {"enable-zero-copy", flag_descriptions::kZeroCopyName,
     flag_descriptions::kZeroCopyDescription, kOsAll,
     ENABLE_DISABLE_VALUE_TYPE(switches::kEnableZeroCopy,
                               switches::kDisableZeroCopy)},
#if defined(OS_CHROMEOS)
    {"enable-first-run-ui-transitions",
     flag_descriptions::kFirstRunUiTransitionsName,
     flag_descriptions::kFirstRunUiTransitionsDescription, kOsCrOS,
     SINGLE_VALUE_TYPE(chromeos::switches::kEnableFirstRunUITransitions)},
#endif  // OS_CHROMEOS
#if defined(OS_MACOSX)
    {"bookmark-apps", flag_descriptions::kNewBookmarkAppsName,
     flag_descriptions::kNewBookmarkAppsDescription, kOsMac,
     FEATURE_VALUE_TYPE(features::kBookmarkApps)},
    {"disable-hosted-apps-in-windows",
     flag_descriptions::kHostedAppsInWindowsName,
     flag_descriptions::kHostedAppsInWindowsDescription, kOsMac,
     ENABLE_DISABLE_VALUE_TYPE(switches::kEnableHostedAppsInWindows,
                               switches::kDisableHostedAppsInWindows)},
    {"disable-hosted-app-shim-creation",
     flag_descriptions::kHostedAppShimCreationName,
     flag_descriptions::kHostedAppShimCreationDescription, kOsMac,
     SINGLE_DISABLE_VALUE_TYPE(switches::kDisableHostedAppShimCreation)},
    {"enable-hosted-app-quit-notification",
     flag_descriptions::kHostedAppQuitNotificationName,
     flag_descriptions::kHostedAppQuitNotificationDescription, kOsMac,
     SINGLE_VALUE_TYPE(switches::kHostedAppQuitNotification)},
#endif  // OS_MACOSX
#if defined(OS_ANDROID)
    {"disable-pull-to-refresh-effect",
     flag_descriptions::kPullToRefreshEffectName,
     flag_descriptions::kPullToRefreshEffectDescription, kOsAndroid,
     SINGLE_DISABLE_VALUE_TYPE(switches::kDisablePullToRefreshEffect)},
    {"translate-compact-infobar", flag_descriptions::kTranslateCompactUIName,
     flag_descriptions::kTranslateCompactUIDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(translate::kTranslateCompactUI)},
#endif  // OS_ANDROID
#if defined(OS_MACOSX)
    {"enable-translate-new-ux", flag_descriptions::kTranslateNewUxName,
     flag_descriptions::kTranslateNewUxDescription, kOsMac,
     ENABLE_DISABLE_VALUE_TYPE(switches::kEnableTranslateNewUX,
                               switches::kDisableTranslateNewUX)},
#endif  // OS_MACOSX
#if defined(OS_LINUX) || defined(OS_WIN) || defined(OS_CHROMEOS)
    {"translate-2016q2-ui", flag_descriptions::kTranslate2016q2UiName,
     flag_descriptions::kTranslate2016q2UiDescription,
     kOsCrOS | kOsWin | kOsLinux,
     FEATURE_VALUE_TYPE(translate::kTranslateUI2016Q2)},
#endif  // OS_LINUX || OS_WIN || OS_CHROMEOS
    {"translate-lang-by-ulp", flag_descriptions::kTranslateLanguageByUlpName,
     flag_descriptions::kTranslateLanguageByUlpDescription, kOsAll,
     FEATURE_VALUE_TYPE(translate::kTranslateLanguageByULP)},
#if BUILDFLAG(ENABLE_NATIVE_NOTIFICATIONS)
    {"enable-native-notifications",
     flag_descriptions::kNotificationsNativeFlagName,
     flag_descriptions::kNotificationsNativeFlagDescription, kOsMac | kOsLinux,
     FEATURE_VALUE_TYPE(features::kNativeNotifications)},
#endif  // ENABLE_NATIVE_NOTIFICATIONS
#if defined(TOOLKIT_VIEWS)
    {"disable-views-rect-based-targeting",
     flag_descriptions::kViewsRectBasedTargetingName,
     flag_descriptions::kViewsRectBasedTargetingDescription,
     kOsCrOS | kOsWin | kOsLinux,
     SINGLE_DISABLE_VALUE_TYPE(
         views::switches::kDisableViewsRectBasedTargeting)},
#endif  // TOOLKIT_VIEWS
#if defined(OS_ANDROID)
    {"reader-mode-heuristics", flag_descriptions::kReaderModeHeuristicsName,
     flag_descriptions::kReaderModeHeuristicsDescription, kOsAndroid,
     MULTI_VALUE_TYPE(kReaderModeHeuristicsChoices)},
#endif  // OS_ANDROID
#if defined(OS_ANDROID)
    {"enable-chrome-home", flag_descriptions::kChromeHomeName,
     flag_descriptions::kChromeHomeDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kChromeHomeFeature)},
    {"enable-chrome-home-expand-button",
     flag_descriptions::kChromeHomeExpandButtonName,
     flag_descriptions::kChromeHomeExpandButtonDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kChromeHomeExpandButton)},
    {"enable-chrome-home-ntp-design",
     flag_descriptions::kChromeHomeNtpRedesignName,
     flag_descriptions::kChromeHomeNtpRedesignDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kChromeHomeNtpRedesign)},
#endif  // OS_ANDROID
#if defined(OS_ANDROID)
    {"enable-iph-demo-mode", flag_descriptions::kEnableIphDemoModeName,
     flag_descriptions::kEnableIphDemoModeDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(feature_engagement_tracker::kIPHDemoMode)},
#endif  // OS_ANDROID
    {"num-raster-threads", flag_descriptions::kNumRasterThreadsName,
     flag_descriptions::kNumRasterThreadsDescription, kOsAll,
     MULTI_VALUE_TYPE(kNumRasterThreadsChoices)},
    {"enable-permission-action-reporting",
     flag_descriptions::kPermissionActionReportingName,
     flag_descriptions::kPermissionActionReportingDescription, kOsAll,
     ENABLE_DISABLE_VALUE_TYPE(switches::kEnablePermissionActionReporting,
                               switches::kDisablePermissionActionReporting)},
    {"enable-permissions-blacklist",
     flag_descriptions::kPermissionsBlacklistName,
     flag_descriptions::kPermissionsBlacklistDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kPermissionsBlacklist)},
    {"enable-single-click-autofill",
     flag_descriptions::kSingleClickAutofillName,
     flag_descriptions::kSingleClickAutofillDescription, kOsAll,
     ENABLE_DISABLE_VALUE_TYPE(
         autofill::switches::kEnableSingleClickAutofill,
         autofill::switches::kDisableSingleClickAutofill)},
    {"disable-cast-streaming-hw-encoding",
     flag_descriptions::kCastStreamingHwEncodingName,
     flag_descriptions::kCastStreamingHwEncodingDescription, kOsAll,
     SINGLE_DISABLE_VALUE_TYPE(switches::kDisableCastStreamingHWEncoding)},
    {"disable-threaded-scrolling", flag_descriptions::kThreadedScrollingName,
     flag_descriptions::kThreadedScrollingDescription, kOsAll,
     SINGLE_DISABLE_VALUE_TYPE(switches::kDisableThreadedScrolling)},
    {"enable-settings-window", flag_descriptions::kSettingsWindowName,
     flag_descriptions::kSettingsWindowDescription, kOsDesktop,
     ENABLE_DISABLE_VALUE_TYPE(switches::kEnableSettingsWindow,
                               switches::kDisableSettingsWindow)},
    {"inert-visual-viewport", flag_descriptions::kInertVisualViewportName,
     flag_descriptions::kInertVisualViewportDescription, kOsAll,
     SINGLE_VALUE_TYPE(switches::kInertVisualViewport)},
    {"distance-field-text", flag_descriptions::kDistanceFieldTextName,
     flag_descriptions::kDistanceFieldTextDescription, kOsAll,
     ENABLE_DISABLE_VALUE_TYPE(switches::kEnableDistanceFieldText,
                               switches::kDisableDistanceFieldText)},
    {"extension-content-verification",
     flag_descriptions::kExtensionContentVerificationName,
     flag_descriptions::kExtensionContentVerificationDescription, kOsDesktop,
     MULTI_VALUE_TYPE(kExtensionContentVerificationChoices)},
#if BUILDFLAG(ENABLE_EXTENSIONS)
    {"extension-active-script-permission",
     flag_descriptions::kUserConsentForExtensionScriptsName,
     flag_descriptions::kUserConsentForExtensionScriptsDescription, kOsAll,
     SINGLE_VALUE_TYPE(extensions::switches::kEnableScriptsRequireAction)},
#endif  // ENABLE_EXTENSIONS
#if defined(OS_ANDROID)
    {"enable-data-reduction-proxy-carrier-test",
     flag_descriptions::kDataReductionProxyCarrierTestName,
     flag_descriptions::kDataReductionProxyCarrierTestDescription, kOsAndroid,
     SINGLE_VALUE_TYPE(
         data_reduction_proxy::switches::kEnableDataReductionProxyCarrierTest)},
#endif  // OS_ANDROID
    {"enable-hotword-hardware",
     flag_descriptions::kExperimentalHotwordHardwareName,
     flag_descriptions::kExperimentalHotwordHardwareDescription, kOsCrOS,
     SINGLE_VALUE_TYPE(switches::kEnableExperimentalHotwordHardware)},
#if BUILDFLAG(ENABLE_EXTENSIONS)
    {"enable-embedded-extension-options",
     flag_descriptions::kEmbeddedExtensionOptionsName,
     flag_descriptions::kEmbeddedExtensionOptionsDescription, kOsDesktop,
     SINGLE_VALUE_TYPE(extensions::switches::kEnableEmbeddedExtensionOptions)},
#endif  // ENABLE_EXTENSIONS
    {"drop-sync-credential", flag_descriptions::kDropSyncCredentialName,
     flag_descriptions::kDropSyncCredentialDescription, kOsAll,
     FEATURE_VALUE_TYPE(password_manager::features::kDropSyncCredential)},
#if !defined(OS_ANDROID)
    {"enable-message-center-always-scroll-up-upon-notification-removal",
     flag_descriptions::kMessageCenterAlwaysScrollUpUponRemovalName,
     flag_descriptions::kMessageCenterAlwaysScrollUpUponRemovalDescription,
     kOsDesktop,
     SINGLE_VALUE_TYPE(
         switches::kEnableMessageCenterAlwaysScrollUpUponNotificationRemoval)},
#endif  // !OS_ANDROID
    {"enable-md-policy-page",
     flag_descriptions::kEnableMaterialDesignPolicyPageName,
     flag_descriptions::kEnableMaterialDesignPolicyPageDescription, kOsDesktop,
     SINGLE_VALUE_TYPE(switches::kEnableMaterialDesignPolicyPage)},
#if defined(OS_CHROMEOS)
    {"memory-pressure-thresholds",
     flag_descriptions::kMemoryPressureThresholdName,
     flag_descriptions::kMemoryPressureThresholdDescription, kOsCrOS,
     MULTI_VALUE_TYPE(kMemoryPressureThresholdChoices)},
    {"wake-on-wifi-packet", flag_descriptions::kWakeOnPacketsName,
     flag_descriptions::kWakeOnPacketsDescription, kOsCrOSOwnerOnly,
     SINGLE_VALUE_TYPE(chromeos::switches::kWakeOnWifiPacket)},
#endif  // OS_CHROMEOS
    {"enable-memory-coordinator", flag_descriptions::kMemoryCoordinatorName,
     flag_descriptions::kMemoryCoordinatorDescription,
     kOsAndroid | kOsCrOS | kOsLinux | kOsWin,
     FEATURE_VALUE_TYPE(features::kMemoryCoordinator)},
    {"enable-tab-audio-muting", flag_descriptions::kTabAudioMutingName,
     flag_descriptions::kTabAudioMutingDescription, kOsDesktop,
     SINGLE_VALUE_TYPE(switches::kEnableTabAudioMuting)},
    {"reduced-referrer-granularity",
     flag_descriptions::kReducedReferrerGranularityName,
     flag_descriptions::kReducedReferrerGranularityDescription, kOsAll,
     SINGLE_VALUE_TYPE(switches::kReducedReferrerGranularity)},
#if defined(OS_CHROMEOS)
    {"disable-new-zip-unpacker", flag_descriptions::kNewZipUnpackerName,
     flag_descriptions::kNewZipUnpackerDescription, kOsCrOS,
     SINGLE_DISABLE_VALUE_TYPE(chromeos::switches::kDisableNewZIPUnpacker)},
#endif  // OS_CHROMEOS
#if defined(OS_ANDROID)
    {"enable-credit-card-assist", flag_descriptions::kCreditCardAssistName,
     flag_descriptions::kCreditCardAssistDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(autofill::kAutofillCreditCardAssist)},
#endif  // OS_ANDROID
#if defined(OS_CHROMEOS)
    {"disable-captive-portal-bypass-proxy",
     flag_descriptions::kCaptivePortalBypassProxyName,
     flag_descriptions::kCaptivePortalBypassProxyDescription, kOsCrOS,
     SINGLE_DISABLE_VALUE_TYPE(
         chromeos::switches::kDisableCaptivePortalBypassProxy)},
#endif  // OS_CHROMEOS
#if defined(OS_ANDROID)
    {"enable-seccomp-sandbox-android",
     flag_descriptions::kSeccompFilterSandboxAndroidName,
     flag_descriptions::kSeccompFilterSandboxAndroidDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(features::kSeccompSandboxAndroid)},
#endif  // OS_ANDROID
#if defined(OS_CHROMEOS)
    {"enable-wifi-credential-sync", flag_descriptions::kWifiCredentialSyncName,
     flag_descriptions::kWifiCredentialSyncDescription, kOsCrOS,
     SINGLE_VALUE_TYPE(switches::kEnableWifiCredentialSync)},
    {"enable-potentially-annoying-security-features",
     flag_descriptions::kExperimentalSecurityFeaturesName,
     flag_descriptions::kExperimentalSecurityFeaturesDescription, kOsAll,
     SINGLE_VALUE_TYPE(switches::kEnablePotentiallyAnnoyingSecurityFeatures)},
#endif  // OS_CHROMEOS
    {"mark-non-secure-as", flag_descriptions::kMarkHttpAsName,
     flag_descriptions::kMarkHttpAsDescription, kOsAll,
     MULTI_VALUE_TYPE(kMarkHttpAsChoices)},
    {"enable-http-form-warning", flag_descriptions::kEnableHttpFormWarningName,
     flag_descriptions::kEnableHttpFormWarningDescription, kOsAll,
     FEATURE_VALUE_TYPE(security_state::kHttpFormWarningFeature)},
    {"enable-site-per-process", flag_descriptions::kSitePerProcessName,
     flag_descriptions::kSitePerProcessDescription, kOsAll,
     SINGLE_VALUE_TYPE(switches::kSitePerProcess)},
    {"enable-top-document-isolation",
     flag_descriptions::kTopDocumentIsolationName,
     flag_descriptions::kTopDocumentIsolationDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kTopDocumentIsolation)},
    {"enable-use-zoom-for-dsf", flag_descriptions::kEnableUseZoomForDsfName,
     flag_descriptions::kEnableUseZoomForDsfDescription, kOsAll,
     MULTI_VALUE_TYPE(kEnableUseZoomForDSFChoices)},
#if defined(OS_MACOSX)
    {"enable-harfbuzz-rendertext", flag_descriptions::kHarfbuzzRendertextName,
     flag_descriptions::kHarfbuzzRendertextDescription, kOsMac,
     SINGLE_VALUE_TYPE(switches::kEnableHarfBuzzRenderText)},
#endif  // OS_MACOSX
    {"data-reduction-proxy-lo-fi",
     flag_descriptions::kDataReductionProxyLoFiName,
     flag_descriptions::kDataReductionProxyLoFiDescription, kOsAll,
     MULTI_VALUE_TYPE(kDataReductionProxyLoFiChoices)},
    {"enable-data-reduction-proxy-lite-page",
     flag_descriptions::kEnableDataReductionProxyLitePageName,
     flag_descriptions::kEnableDataReductionProxyLitePageDescription, kOsAll,
     SINGLE_VALUE_TYPE(
         data_reduction_proxy::switches::kEnableDataReductionProxyLitePage)},
#if defined(OS_ANDROID)
    {"enable-data-reduction-proxy-savings-promo",
     flag_descriptions::kEnableDataReductionProxySavingsPromoName,
     flag_descriptions::kEnableDataReductionProxySavingsPromoDescription,
     kOsAndroid,
     SINGLE_VALUE_TYPE(data_reduction_proxy::switches::
                           kEnableDataReductionProxySavingsPromo)},
    {"enable-data-reduction-proxy-main-menu",
     flag_descriptions::kEnableDataReductionProxyMainMenuName,
     flag_descriptions::kEnableDataReductionProxyMainMenuDescription,
     kOsAndroid,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         data_reduction_proxy::features::kDataReductionMainMenu,
         kDataReductionMainMenuFeatureVariations,
         "DataReductionProxyMainMenu")},
    {"enable-data-reduction-proxy-site-breakdown",
     flag_descriptions::kEnableDataReductionProxySiteBreakdownName,
     flag_descriptions::kEnableDataReductionProxySiteBreakdownDescription,
     kOsAndroid,
     FEATURE_VALUE_TYPE(
         data_reduction_proxy::features::kDataReductionSiteBreakdown)},
    {"enable-offline-previews", flag_descriptions::kEnableOfflinePreviewsName,
     flag_descriptions::kEnableOfflinePreviewsDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(previews::features::kOfflinePreviews)},
    {"enable-client-lo-fi", flag_descriptions::kEnableClientLoFiName,
     flag_descriptions::kEnableClientLoFiDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(previews::features::kClientLoFi)},
#endif  // OS_ANDROID
    {"allow-insecure-localhost", flag_descriptions::kAllowInsecureLocalhostName,
     flag_descriptions::kAllowInsecureLocalhostDescription, kOsAll,
     SINGLE_VALUE_TYPE(switches::kAllowInsecureLocalhost)},
    {"enable-add-to-shelf", flag_descriptions::kAddToShelfName,
     flag_descriptions::kAddToShelfDescription, kOsDesktop,
     ENABLE_DISABLE_VALUE_TYPE(switches::kEnableAddToShelf,
                               switches::kDisableAddToShelf)},
    {"bypass-app-banner-engagement-checks",
     flag_descriptions::kBypassAppBannerEngagementChecksName,
     flag_descriptions::kBypassAppBannerEngagementChecksDescription, kOsAll,
     SINGLE_VALUE_TYPE(switches::kBypassAppBannerEngagementChecks)},
    {"use-sync-sandbox", flag_descriptions::kSyncSandboxName,
     flag_descriptions::kSyncSandboxDescription, kOsAll,
     SINGLE_VALUE_TYPE_AND_VALUE(
         switches::kSyncServiceURL,
         "https://chrome-sync.sandbox.google.com/chrome-sync/alpha")},
#if !defined(OS_ANDROID)
    {"load-media-router-component-extension",
     flag_descriptions::kLoadMediaRouterComponentExtensionName,
     flag_descriptions::kLoadMediaRouterComponentExtensionDescription,
     kOsDesktop,
     ENABLE_DISABLE_VALUE_TYPE_AND_VALUE(
         switches::kLoadMediaRouterComponentExtension,
         "1",
         switches::kLoadMediaRouterComponentExtension,
         "0")},
#endif  // !OS_ANDROID
// Since Drive Search is not available when app list is disabled, flag guard
// enable-drive-search-in-chrome-launcher flag.
#if BUILDFLAG(ENABLE_APP_LIST)
    {"enable-drive-search-in-app-launcher",
     flag_descriptions::kDriveSearchInChromeLauncherName,
     flag_descriptions::kDriveSearchInChromeLauncherDescription, kOsCrOS,
     ENABLE_DISABLE_VALUE_TYPE(
         app_list::switches::kEnableDriveSearchInChromeLauncher,
         app_list::switches::kDisableDriveSearchInChromeLauncher)},
#endif  // BUILDFLAG(ENABLE_APP_LIST)
#if defined(OS_CHROMEOS)
    {"disable-mtp-write-support", flag_descriptions::kMtpWriteSupportName,
     flag_descriptions::kMtpWriteSupportDescription, kOsCrOS,
     SINGLE_DISABLE_VALUE_TYPE(chromeos::switches::kDisableMtpWriteSupport)},
#endif  // OS_CHROMEOS
#if defined(OS_CHROMEOS)
    {"enable-datasaver-prompt", flag_descriptions::kDatasaverPromptName,
     flag_descriptions::kDatasaverPromptDescription, kOsCrOS,
     MULTI_VALUE_TYPE(kDataSaverPromptChoices)},
#endif  // OS_CHROMEOS
#if defined(OS_ANDROID)
    {"autofill-keyboard-accessory-view",
     flag_descriptions::kAutofillAccessoryViewName,
     flag_descriptions::kAutofillAccessoryViewDescription, kOsAndroid,
     FEATURE_WITH_PARAMS_VALUE_TYPE(autofill::kAutofillKeyboardAccessory,
                                    kAutofillKeyboardAccessoryFeatureVariations,
                                    "AutofillKeyboardAccessoryVariations")},
#endif  // OS_ANDROID
#if defined(OS_WIN)
    {"try-supported-channel-layouts",
     flag_descriptions::kTrySupportedChannelLayoutsName,
     flag_descriptions::kTrySupportedChannelLayoutsDescription, kOsWin,
     SINGLE_VALUE_TYPE(switches::kTrySupportedChannelLayouts)},
#endif  // OS_WIN
#if defined(OS_MACOSX)
    {"app-info-dialog", flag_descriptions::kAppInfoDialogName,
     flag_descriptions::kAppInfoDialogDescription, kOsMac,
     ENABLE_DISABLE_VALUE_TYPE(switches::kEnableAppInfoDialogMac,
                               switches::kDisableAppInfoDialogMac)},
    {"mac-views-native-app-windows",
     flag_descriptions::kMacViewsNativeAppWindowsName,
     flag_descriptions::kMacViewsNativeAppWindowsDescription, kOsMac,
     ENABLE_DISABLE_VALUE_TYPE(switches::kEnableMacViewsNativeAppWindows,
                               switches::kDisableMacViewsNativeAppWindows)},
    {"mac-views-task-manager", flag_descriptions::kMacViewsTaskManagerName,
     flag_descriptions::kMacViewsTaskManagerDescription, kOsMac,
     FEATURE_VALUE_TYPE(features::kViewsTaskManager)},
    {"app-window-cycling", flag_descriptions::kAppWindowCyclingName,
     flag_descriptions::kAppWindowCyclingDescription, kOsMac,
     ENABLE_DISABLE_VALUE_TYPE(switches::kEnableAppWindowCycling,
                               switches::kDisableAppWindowCycling)},
#endif  // OS_MACOSX
    {"enable-webvr", flag_descriptions::kWebvrName,
     flag_descriptions::kWebvrDescription, kOsAll,
     SINGLE_VALUE_TYPE(switches::kEnableWebVR)},
#if BUILDFLAG(ENABLE_VR)
    {"enable-webvr-experimental-rendering",
     flag_descriptions::kWebvrExperimentalRenderingName,
     flag_descriptions::kWebvrExperimentalRenderingDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(features::kWebVRExperimentalRendering)},
#if defined(OS_ANDROID)
    {"enable-vr-shell", flag_descriptions::kEnableVrShellName,
     flag_descriptions::kEnableVrShellDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(features::kVrShell)},
    {"enable-vr-custom-tab-browsing",
     flag_descriptions::kVrCustomTabBrowsingName,
     flag_descriptions::kVrCustomTabBrowsingDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kVrCustomTabBrowsing)},
    {"enable-webvr-autopresent", flag_descriptions::kWebVrAutopresentName,
     flag_descriptions::kWebVrAutopresentDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kWebVrAutopresent)},
#endif  // OS_ANDROID
#endif  // ENABLE_VR
#if defined(OS_CHROMEOS)
    {"disable-accelerated-mjpeg-decode",
     flag_descriptions::kAcceleratedMjpegDecodeName,
     flag_descriptions::kAcceleratedMjpegDecodeDescription, kOsCrOS,
     SINGLE_DISABLE_VALUE_TYPE(switches::kDisableAcceleratedMjpegDecode)},
#endif  // OS_CHROMEOS
    {"v8-cache-options", flag_descriptions::kV8CacheOptionsName,
     flag_descriptions::kV8CacheOptionsDescription, kOsAll,
     MULTI_VALUE_TYPE(kV8CacheOptionsChoices)},
    {"v8-cache-strategies-for-cache-storage",
     flag_descriptions::kV8CacheStrategiesForCacheStorageName,
     flag_descriptions::kV8CacheStrategiesForCacheStorageDescription, kOsAll,
     MULTI_VALUE_TYPE(kV8CacheStrategiesForCacheStorageChoices)},
    {"enable-clear-browsing-data-counters",
     flag_descriptions::kEnableClearBrowsingDataCountersName,
     flag_descriptions::kEnableClearBrowsingDataCountersDescription, kOsAll,
     ENABLE_DISABLE_VALUE_TYPE(switches::kEnableClearBrowsingDataCounters,
                               switches::kDisableClearBrowsingDataCounters)},
    {"simplified-fullscreen-ui", flag_descriptions::kSimplifiedFullscreenUiName,
     flag_descriptions::kSimplifiedFullscreenUiDescription,
     kOsWin | kOsLinux | kOsCrOS,
     FEATURE_VALUE_TYPE(features::kSimplifiedFullscreenUI)},
    {"experimental-keyboard-lock-ui",
     flag_descriptions::kExperimentalKeyboardLockUiName,
     flag_descriptions::kExperimentalKeyboardLockUiDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kExperimentalKeyboardLockUI)},
#if defined(OS_ANDROID)
    {"progress-bar-animation", flag_descriptions::kProgressBarAnimationName,
     flag_descriptions::kProgressBarAnimationDescription, kOsAndroid,
     MULTI_VALUE_TYPE(kProgressBarAnimationChoices)},
    {"progress-bar-completion", flag_descriptions::kProgressBarCompletionName,
     flag_descriptions::kProgressBarCompletionDescription, kOsAndroid,
     MULTI_VALUE_TYPE(kProgressBarCompletionChoices)},
#endif  // OS_ANDROID
#if defined(OS_ANDROID)
    {"offline-bookmarks", flag_descriptions::kOfflineBookmarksName,
     flag_descriptions::kOfflineBookmarksDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(offline_pages::kOfflineBookmarksFeature)},
    {"offline-pages-async-download",
     flag_descriptions::kOfflinePagesAsyncDownloadName,
     flag_descriptions::kOfflinePagesAsyncDownloadDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(offline_pages::kOfflinePagesAsyncDownloadFeature)},
    {"offline-pages-load-signal-collecting",
     flag_descriptions::kOfflinePagesLoadSignalCollectingName,
     flag_descriptions::kOfflinePagesLoadSignalCollectingDescription,
     kOsAndroid,
     FEATURE_VALUE_TYPE(
         offline_pages::kOfflinePagesLoadSignalCollectingFeature)},
    {"offline-pages-sharing", flag_descriptions::kOfflinePagesSharingName,
     flag_descriptions::kOfflinePagesSharingDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(offline_pages::kOfflinePagesSharingFeature)},
    {"offline-pages-prefetching",
     flag_descriptions::kOfflinePagesPrefetchingName,
     flag_descriptions::kOfflinePagesPrefetchingDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(offline_pages::kPrefetchingOfflinePagesFeature)},
    {"background-loader-for-downloads",
     flag_descriptions::kBackgroundLoaderForDownloadsName,
     flag_descriptions::kBackgroundLoaderForDownloadsDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(offline_pages::kBackgroundLoaderForDownloadsFeature)},
    {"background-loader", flag_descriptions::kNewBackgroundLoaderName,
     flag_descriptions::kNewBackgroundLoaderDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(offline_pages::kNewBackgroundLoaderFeature)},
#endif  // OS_ANDROID
    {"disallow-doc-written-script-loads",
     flag_descriptions::kDisallowDocWrittenScriptsUiName,
     flag_descriptions::kDisallowDocWrittenScriptsUiDescription, kOsAll,
     // NOTE: if we want to add additional experiment entries for other
     // features controlled by kBlinkSettings, we'll need to add logic to
     // merge the flag values.
     ENABLE_DISABLE_VALUE_TYPE_AND_VALUE(
         switches::kBlinkSettings,
         "disallowFetchForDocWrittenScriptsInMainFrame=true",
         switches::kBlinkSettings,
         "disallowFetchForDocWrittenScriptsInMainFrame=false")},
#if defined(OS_ANDROID)
    {"enable-ntp-popular-sites", flag_descriptions::kNtpPopularSitesName,
     flag_descriptions::kNtpPopularSitesDescription, kOsAndroid,
     ENABLE_DISABLE_VALUE_TYPE(ntp_tiles::switches::kEnableNTPPopularSites,
                               ntp_tiles::switches::kDisableNTPPopularSites)},
    {"ntp-switch-to-existing-tab",
     flag_descriptions::kNtpSwitchToExistingTabName,
     flag_descriptions::kNtpSwitchToExistingTabDescription, kOsAndroid,
     MULTI_VALUE_TYPE(kNtpSwitchToExistingTabChoices)},
    {"use-android-midi-api", flag_descriptions::kUseAndroidMidiApiName,
     flag_descriptions::kUseAndroidMidiApiDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(midi::features::kMidiManagerAndroid)},
#endif  // OS_ANDROID
#if defined(OS_WIN)
    {"trace-export-events-to-etw",
     flag_descriptions::kTraceExportEventsToEtwName,
     flag_descriptions::kTraceExportEventsToEtwDesription, kOsWin,
     SINGLE_VALUE_TYPE(switches::kTraceExportEventsToETW)},
    {"merge-key-char-events", flag_descriptions::kMergeKeyCharEventsName,
     flag_descriptions::kMergeKeyCharEventsDescription, kOsWin,
     ENABLE_DISABLE_VALUE_TYPE(switches::kEnableMergeKeyCharEvents,
                               switches::kDisableMergeKeyCharEvents)},
    {"use-winrt-midi-api", flag_descriptions::kUseWinrtMidiApiName,
     flag_descriptions::kUseWinrtMidiApiDescription, kOsWin,
     FEATURE_VALUE_TYPE(midi::features::kMidiManagerWinrt)},
#endif  // OS_WIN
#if BUILDFLAG(ENABLE_BACKGROUND)
    {"enable-push-api-background-mode",
     flag_descriptions::kPushApiBackgroundModeName,
     flag_descriptions::kPushApiBackgroundModeDescription,
     kOsMac | kOsWin | kOsLinux,
     ENABLE_DISABLE_VALUE_TYPE(switches::kEnablePushApiBackgroundMode,
                               switches::kDisablePushApiBackgroundMode)},
#endif  // BUILDFLAG(ENABLE_BACKGROUND)
#if defined(OS_CHROMEOS)
    {"cros-regions-mode", flag_descriptions::kCrosRegionsModeName,
     flag_descriptions::kCrosRegionsModeDescription, kOsCrOS,
     MULTI_VALUE_TYPE(kCrosRegionsModeChoices)},
#endif  // OS_CHROMEOS
#if defined(OS_ANDROID)
    {"enable-web-notification-custom-layouts",
     flag_descriptions::kEnableWebNotificationCustomLayoutsName,
     flag_descriptions::kEnableWebNotificationCustomLayoutsDescription,
     kOsAndroid,
     ENABLE_DISABLE_VALUE_TYPE(switches::kEnableWebNotificationCustomLayouts,
                               switches::kDisableWebNotificationCustomLayouts)},
#endif  // OS_ANDROID
#if defined(OS_WIN)
    {"enable-appcontainer", flag_descriptions::kEnableAppcontainerName,
     flag_descriptions::kEnableAppcontainerDescription, kOsWin,
     ENABLE_DISABLE_VALUE_TYPE(switches::kEnableAppContainer,
                               switches::kDisableAppContainer)},
#endif  // OS_WIN
#if defined(TOOLKIT_VIEWS) || defined(OS_ANDROID)
    {"enable-autofill-credit-card-upload",
     flag_descriptions::kAutofillCreditCardUploadName,
     flag_descriptions::kAutofillCreditCardUploadDescription, kOsAll,
     ENABLE_DISABLE_VALUE_TYPE(
         autofill::switches::kEnableOfferUploadCreditCards,
         autofill::switches::kDisableOfferUploadCreditCards)},
#endif  // TOOLKIT_VIEWS || OS_ANDROID
#if defined(OS_ANDROID)
    {"tab-management-experiment-type",
     flag_descriptions::kHerbPrototypeChoicesName,
     flag_descriptions::kHerbPrototypeChoicesDescription, kOsAndroid,
     MULTI_VALUE_TYPE(kHerbPrototypeChoices)},
#endif  // OS_ANDROID
    {"enable-md-bookmarks",
     flag_descriptions::kEnableMaterialDesignBookmarksName,
     flag_descriptions::kEnableMaterialDesignBookmarksDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kMaterialDesignBookmarks)},
    {"enable-md-feedback", flag_descriptions::kEnableMaterialDesignFeedbackName,
     flag_descriptions::kEnableMaterialDesignFeedbackDescription, kOsDesktop,
     SINGLE_VALUE_TYPE(switches::kEnableMaterialDesignFeedback)},
    {"enable-md-incognito-ntp",
     flag_descriptions::kMaterialDesignIncognitoNTPName,
     flag_descriptions::kMaterialDesignIncognitoNTPDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kMaterialDesignIncognitoNTP)},
    {"enable-md-settings", flag_descriptions::kEnableMaterialDesignSettingsName,
     flag_descriptions::kEnableMaterialDesignSettingsDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kMaterialDesignSettings)},
    {"safe-search-url-reporting",
     flag_descriptions::kSafeSearchUrlReportingName,
     flag_descriptions::kSafeSearchUrlReportingDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kSafeSearchUrlReporting)},
    {"force-ui-direction", flag_descriptions::kForceUiDirectionName,
     flag_descriptions::kForceUiDirectionDescription, kOsAll,
     MULTI_VALUE_TYPE(kForceUIDirectionChoices)},
    {"force-text-direction", flag_descriptions::kForceTextDirectionName,
     flag_descriptions::kForceTextDirectionDescription, kOsAll,
     MULTI_VALUE_TYPE(kForceTextDirectionChoices)},
#if BUILDFLAG(ENABLE_EXTENSIONS)
    {"enable-md-extensions",
     flag_descriptions::kEnableMaterialDesignExtensionsName,
     flag_descriptions::kEnableMaterialDesignExtensionsDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kMaterialDesignExtensions)},
#endif  // ENABLE_EXTENSIONS
#if defined(OS_WIN) || defined(OS_LINUX)
    {"enable-input-ime-api", flag_descriptions::kEnableInputImeApiName,
     flag_descriptions::kEnableInputImeApiDescription, kOsWin | kOsLinux,
     ENABLE_DISABLE_VALUE_TYPE(switches::kEnableInputImeAPI,
                               switches::kDisableInputImeAPI)},
#endif  // OS_WIN || OS_LINUX
    {"enable-origin-trials", flag_descriptions::kOriginTrialsName,
     flag_descriptions::kOriginTrialsDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kOriginTrials)},
    {"enable-brotli", flag_descriptions::kEnableBrotliName,
     flag_descriptions::kEnableBrotliDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kBrotliEncoding)},
    {"enable-webusb", flag_descriptions::kEnableWebUsbName,
     flag_descriptions::kEnableWebUsbDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kWebUsb)},
#if defined(OS_ANDROID)
    {"force-show-update-menu-item", flag_descriptions::kUpdateMenuItemName,
     flag_descriptions::kUpdateMenuItemDescription, kOsAndroid,
     SINGLE_VALUE_TYPE(switches::kForceShowUpdateMenuItem)},
    {"update-menu-item-custom-summary",
     flag_descriptions::kUpdateMenuItemCustomSummaryName,
     flag_descriptions::kUpdateMenuItemCustomSummaryDescription, kOsAndroid,
     SINGLE_VALUE_TYPE_AND_VALUE(
         switches::kForceShowUpdateMenuItemCustomSummary,
         "Custom Summary")},
    {"force-show-update-menu-badge", flag_descriptions::kUpdateMenuBadgeName,
     flag_descriptions::kUpdateMenuBadgeDescription, kOsAndroid,
     SINGLE_VALUE_TYPE(switches::kForceShowUpdateMenuBadge)},
    {"set-market-url-for-testing",
     flag_descriptions::kSetMarketUrlForTestingName,
     flag_descriptions::kSetMarketUrlForTestingDescription, kOsAndroid,
     SINGLE_VALUE_TYPE_AND_VALUE(
         switches::kMarketUrlForTesting,
         "https://play.google.com/store/apps/details?id=com.android.chrome")},
#endif  // OS_ANDROID
#if defined(OS_WIN) || defined(OS_MACOSX)
    {"automatic-tab-discarding", flag_descriptions::kAutomaticTabDiscardingName,
     flag_descriptions::kAutomaticTabDiscardingDescription, kOsWin | kOsMac,
     FEATURE_VALUE_TYPE(features::kAutomaticTabDiscarding)},
#endif  // OS_WIN || OS_MACOSX
    {"enable-es3-apis", flag_descriptions::kWebgl2Name,
     flag_descriptions::kWebgl2Description, kOsAll,
     MULTI_VALUE_TYPE(kEnableWebGL2Choices)},
    {"enable-webfonts-intervention-v2",
     flag_descriptions::kEnableWebfontsInterventionName,
     flag_descriptions::kEnableWebfontsInterventionDescription, kOsAll,
     MULTI_VALUE_TYPE(kEnableWebFontsInterventionV2Choices)},
    {"enable-webfonts-intervention-trigger",
     flag_descriptions::kEnableWebfontsInterventionTriggerName,
     flag_descriptions::kEnableWebfontsInterventionTriggerDescription, kOsAll,
     SINGLE_VALUE_TYPE(switches::kEnableWebFontsInterventionTrigger)},
    {"ssl-version-max", flag_descriptions::kSslVersionMaxName,
     flag_descriptions::kSslVersionMaxDescription, kOsAll,
     MULTI_VALUE_TYPE(kSSLVersionMaxChoices)},
    {"enable-token-binding", flag_descriptions::kEnableTokenBindingName,
     flag_descriptions::kEnableTokenBindingDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kTokenBinding)},
    {"enable-scroll-anchoring", flag_descriptions::kEnableScrollAnchoringName,
     flag_descriptions::kEnableScrollAnchoringDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kScrollAnchoring)},
    {"disable-audio-support-for-desktop-share",
     flag_descriptions::kDisableAudioForDesktopShareName,
     flag_descriptions::kDisableAudioForDesktopShareDescription, kOsAll,
     SINGLE_VALUE_TYPE(switches::kDisableAudioSupportForDesktopShare)},
#if BUILDFLAG(ENABLE_EXTENSIONS)
    {"tab-for-desktop-share", flag_descriptions::kDisableTabForDesktopShareName,
     flag_descriptions::kDisableTabForDesktopShareDescription, kOsAll,
     SINGLE_VALUE_TYPE(extensions::switches::kDisableTabForDesktopShare)},
#endif  // ENABLE_EXTENSIONS
#if defined(OS_ANDROID)
    {"content-suggestions-category-order",
     flag_descriptions::kContentSuggestionsCategoryOrderName,
     flag_descriptions::kContentSuggestionsCategoryOrderDescription, kOsAndroid,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         ntp_snippets::kCategoryOrder,
         kContentSuggestionsCategoryOrderFeatureVariations,
         ntp_snippets::kCategoryOrder.name)},
    {"content-suggestions-category-ranker",
     flag_descriptions::kContentSuggestionsCategoryRankerName,
     flag_descriptions::kContentSuggestionsCategoryRankerDescription,
     kOsAndroid,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         ntp_snippets::kCategoryRanker,
         kContentSuggestionsCategoryRankerFeatureVariations,
         ntp_snippets::kCategoryRanker.name)},
    {"enable-ntp-snippets-increased-visibility",
     flag_descriptions::kEnableNtpSnippetsVisibilityName,
     flag_descriptions::kEnableNtpSnippetsVisibilityDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(ntp_snippets::kIncreasedVisibility)},
    {"enable-contextual-suggestions-carousel",
     flag_descriptions::kContextualSuggestionsCarouselName,
     flag_descriptions::kContextualSuggestionsCarouselDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kContextualSuggestionsCarousel)},
    {"enable-content-suggestions-new-favicon-server",
     flag_descriptions::kEnableContentSuggestionsNewFaviconServerName,
     flag_descriptions::kEnableContentSuggestionsNewFaviconServerDescription,
     kOsAndroid,
     FEATURE_VALUE_TYPE(ntp_snippets::kPublisherFaviconsFromNewServerFeature)},
    {"enable-ntp-tiles-favicons-from-server",
     flag_descriptions::kEnableNtpMostLikelyFaviconsFromServerName,
     flag_descriptions::kEnableNtpMostLikelyFaviconsFromServerDescription,
     kOsAndroid,
     FEATURE_VALUE_TYPE(ntp_tiles::kNtpMostLikelyFaviconsFromServerFeature)},
    {"enable-content-suggestions-settings",
     flag_descriptions::kEnableContentSuggestionsSettingsName,
     flag_descriptions::kEnableContentSuggestionsSettingsDescription,
     kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kContentSuggestionsSettings)},
    {"enable-content-suggestions-show-summary",
     flag_descriptions::kEnableContentSuggestionsShowSummaryName,
     flag_descriptions::kEnableContentSuggestionsShowSummaryDescription,
     kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kContentSuggestionsShowSummary)},
    {"enable-favicons-from-web-manifest",
     flag_descriptions::kEnableFaviconsFromWebManifestName,
     flag_descriptions::kEnableFaviconsFromWebManifestDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(favicon::kFaviconsFromWebManifest)},
    {"enable-ntp-remote-suggestions",
     flag_descriptions::kEnableNtpRemoteSuggestionsName,
     flag_descriptions::kEnableNtpRemoteSuggestionsDescription, kOsAndroid,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         ntp_snippets::kArticleSuggestionsFeature,
         kRemoteSuggestionsFeatureVariations,
         ntp_snippets::kArticleSuggestionsFeature.name)},
    {"enable-ntp-recent-offline-tab-suggestions",
     flag_descriptions::kEnableNtpRecentOfflineTabSuggestionsName,
     flag_descriptions::kEnableNtpRecentOfflineTabSuggestionsDescription,
     kOsAndroid,
     FEATURE_VALUE_TYPE(ntp_snippets::kRecentOfflineTabSuggestionsFeature)},
    {"enable-ntp-asset-download-suggestions",
     flag_descriptions::kEnableNtpAssetDownloadSuggestionsName,
     flag_descriptions::kEnableNtpAssetDownloadSuggestionsDescription,
     kOsAndroid,
     FEATURE_VALUE_TYPE(features::kAssetDownloadSuggestionsFeature)},
    {"enable-ntp-offline-page-download-suggestions",
     flag_descriptions::kEnableNtpOfflinePageDownloadSuggestionsName,
     flag_descriptions::kEnableNtpOfflinePageDownloadSuggestionsDescription,
     kOsAndroid,
     FEATURE_VALUE_TYPE(features::kOfflinePageDownloadSuggestionsFeature)},
    {"enable-ntp-bookmark-suggestions",
     flag_descriptions::kEnableNtpBookmarkSuggestionsName,
     flag_descriptions::kEnableNtpBookmarkSuggestionsDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(ntp_snippets::kBookmarkSuggestionsFeature)},
    {"enable-ntp-physical-web-page-suggestions",
     flag_descriptions::kEnableNtpPhysicalWebPageSuggestionsName,
     flag_descriptions::kEnableNtpPhysicalWebPageSuggestionsDescription,
     kOsAndroid,
     FEATURE_VALUE_TYPE(ntp_snippets::kPhysicalWebPageSuggestionsFeature)},
    {"enable-ntp-foreign-sessions-suggestions",
     flag_descriptions::kEnableNtpForeignSessionsSuggestionsName,
     flag_descriptions::kEnableNtpForeignSessionsSuggestionsDescription,
     kOsAndroid,
     FEATURE_VALUE_TYPE(ntp_snippets::kForeignSessionsSuggestionsFeature)},
    {"enable-ntp-suggestions-notifications",
     flag_descriptions::kEnableNtpSuggestionsNotificationsName,
     flag_descriptions::kEnableNtpSuggestionsNotificationsDescription,
     kOsAndroid,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         ntp_snippets::kNotificationsFeature,
         kContentSuggestionsNotificationsFeatureVariations,
         ntp_snippets::kNotificationsFeature.name)},
    {"ntp-condensed-layout", flag_descriptions::kNtpCondensedLayoutName,
     flag_descriptions::kNtpCondensedLayoutDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kNTPCondensedLayoutFeature)},
    {"ntp-condensed-tile-layout",
     flag_descriptions::kNtpCondensedTileLayoutName,
     flag_descriptions::kNtpCondensedTileLayoutDescription, kOsAndroid,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         chrome::android::kNTPCondensedTileLayoutFeature,
         kNTPCondensedTileLayoutFeatureVariations,
         chrome::android::kNTPCondensedTileLayoutFeature.name)},
    {"ntp-google-g-in-omnibox", flag_descriptions::kNtpGoogleGInOmniboxName,
     flag_descriptions::kNtpGoogleGInOmniboxDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::NTPShowGoogleGInOmniboxFeature)},
#endif  // OS_ANDROID
#if BUILDFLAG(ENABLE_WEBRTC) && BUILDFLAG(RTC_USE_H264) && \
    !defined(MEDIA_DISABLE_FFMPEG)
    {"enable-webrtc-h264-with-openh264-ffmpeg",
     flag_descriptions::kWebrtcH264WithOpenh264FfmpegName,
     flag_descriptions::kWebrtcH264WithOpenh264FfmpegDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(content::kWebRtcH264WithOpenH264FFmpeg)},
#endif  // ENABLE_WEBRTC && BUILDFLAG(RTC_USE_H264) && !MEDIA_DISABLE_FFMPEG
#if defined(OS_ANDROID)
    {"offline-pages-ntp", flag_descriptions::kNtpOfflinePagesName,
     flag_descriptions::kNtpOfflinePagesDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kNTPOfflinePagesFeature)},
    {"offlining-recent-pages", flag_descriptions::kOffliningRecentPagesName,
     flag_descriptions::kOffliningRecentPagesDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(offline_pages::kOffliningRecentPagesFeature)},
    {"offline-pages-ct", flag_descriptions::kOfflinePagesCtName,
     flag_descriptions::kOfflinePagesCtDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(offline_pages::kOfflinePagesCTFeature)},
#endif  // OS_ANDROID
    {"protect-sync-credential", flag_descriptions::kProtectSyncCredentialName,
     flag_descriptions::kProtectSyncCredentialDescription, kOsAll,
     FEATURE_VALUE_TYPE(password_manager::features::kProtectSyncCredential)},
    {"protect-sync-credential-on-reauth",
     flag_descriptions::kProtectSyncCredentialOnReauthName,
     flag_descriptions::kProtectSyncCredentialOnReauthDescription, kOsAll,
     FEATURE_VALUE_TYPE(
         password_manager::features::kProtectSyncCredentialOnReauth)},
    {"password-import-export", flag_descriptions::kPasswordImportExportName,
     flag_descriptions::kPasswordImportExportDescription,
     kOsWin | kOsMac | kOsCrOS | kOsLinux,
     FEATURE_VALUE_TYPE(password_manager::features::kPasswordImportExport)},
#if defined(OS_CHROMEOS)
    {"enable-experimental-accessibility-features",
     flag_descriptions::kExperimentalAccessibilityFeaturesName,
     flag_descriptions::kExperimentalAccessibilityFeaturesDescription, kOsCrOS,
     SINGLE_VALUE_TYPE(
         chromeos::switches::kEnableExperimentalAccessibilityFeatures)},
    {"opt-in-ime-menu", flag_descriptions::kEnableImeMenuName,
     flag_descriptions::kEnableImeMenuDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(features::kOptInImeMenu)},
    {"disable-system-timezone-automatic-detection",
     flag_descriptions::kDisableSystemTimezoneAutomaticDetectionName,
     flag_descriptions::kDisableSystemTimezoneAutomaticDetectionDescription,
     kOsCrOS,
     SINGLE_VALUE_TYPE(
         chromeos::switches::kDisableSystemTimezoneAutomaticDetectionPolicy)},
    {"disable-native-cups", flag_descriptions::kDisableNativeCupsName,
     flag_descriptions::kDisableNativeCupsDescription, kOsCrOS,
     SINGLE_VALUE_TYPE(switches::kDisableNativeCups)},
    {"enable-cros-component", flag_descriptions::kCrOSComponentName,
     flag_descriptions::kCrOSComponentDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(features::kCrOSComponent)},
    {"enable-encryption-migration",
     flag_descriptions::kEnableEncryptionMigrationName,
     flag_descriptions::kEnableEncryptionMigrationDescription, kOsCrOS,
     ENABLE_DISABLE_VALUE_TYPE(
         chromeos::switches::kEnableEncryptionMigration,
         chromeos::switches::kDisableEncryptionMigration)},
#endif  // OS_CHROMEOS
#if !defined(OS_ANDROID) && !defined(OS_IOS) && defined(GOOGLE_CHROME_BUILD)
    {"enable-google-branded-context-menu",
     flag_descriptions::kGoogleBrandedContextMenuName,
     flag_descriptions::kGoogleBrandedContextMenuDescription, kOsDesktop,
     SINGLE_VALUE_TYPE(switches::kEnableGoogleBrandedContextMenu)},
#endif  // !OS_ANDROID && !OS_IOS && GOOGLE_CHROME_BUILD
#if defined(OS_MACOSX)
    {"enable-fullscreen-in-tab-detaching",
     flag_descriptions::kTabDetachingInFullscreenName,
     flag_descriptions::kTabDetachingInFullscreenDescription, kOsMac,
     ENABLE_DISABLE_VALUE_TYPE(switches::kEnableFullscreenTabDetaching,
                               switches::kDisableFullscreenTabDetaching)},
    {"enable-fullscreen-toolbar-reveal",
     flag_descriptions::kFullscreenToolbarRevealName,
     flag_descriptions::kFullscreenToolbarRevealDescription, kOsMac,
     SINGLE_VALUE_TYPE(switches::kEnableFullscreenToolbarReveal)},
#endif  // OS_MACOSX
    {"important-sites-in-cbd", flag_descriptions::kImportantSitesInCbdName,
     flag_descriptions::kImportantSitesInCbdDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kImportantSitesInCbd)},
#if defined(OS_ANDROID)
    {"tabs-in-cbd", flag_descriptions::kTabsInCbdName,
     flag_descriptions::kTabsInCbdDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(features::kTabsInCbd)},
#endif  // OS_ANDROID
    {"enable-pointer-events", flag_descriptions::kExperimentalPointerEventName,
     flag_descriptions::kExperimentalPointerEventDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kPointerEvents)},
    {"passive-listener-default",
     flag_descriptions::kPassiveEventListenerDefaultName,
     flag_descriptions::kPassiveEventListenerDefaultDescription, kOsAll,
     MULTI_VALUE_TYPE(kPassiveListenersChoices)},
    {"document-passive-event-listeners",
     flag_descriptions::kPassiveDocumentEventListenersName,
     flag_descriptions::kPassiveDocumentEventListenersDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kPassiveDocumentEventListeners)},
    {"passive-event-listeners-due-to-fling",
     flag_descriptions::kPassiveEventListenersDueToFlingName,
     flag_descriptions::kPassiveEventListenersDueToFlingDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kPassiveEventListenersDueToFling)},
    {"enable-font-cache-scaling", flag_descriptions::kFontCacheScalingName,
     flag_descriptions::kFontCacheScalingDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kFontCacheScaling)},
    {"enable-framebusting-needs-sameorigin-or-usergesture",
     flag_descriptions::kFramebustingName,
     flag_descriptions::kFramebustingDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kFramebustingNeedsSameOriginOrUserGesture)},
    {"vibrate-requires-user-gesture",
     flag_descriptions::kVibrateRequiresUserGestureName,
     flag_descriptions::kVibrateRequiresUserGestureDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kVibrateRequiresUserGesture)},
    {"web-payments", flag_descriptions::kWebPaymentsName,
     flag_descriptions::kWebPaymentsDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kWebPayments)},
#if defined(OS_ANDROID)
    {"enable-android-pay-integration-v1",
     flag_descriptions::kEnableAndroidPayIntegrationV1Name,
     flag_descriptions::kEnableAndroidPayIntegrationV1Description, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kAndroidPayIntegrationV1)},
    {"enable-android-pay-integration-v2",
     flag_descriptions::kEnableAndroidPayIntegrationV2Name,
     flag_descriptions::kEnableAndroidPayIntegrationV2Description, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kAndroidPayIntegrationV2)},
    {"enable-web-payments-single-app-ui-skip",
     flag_descriptions::kEnableWebPaymentsSingleAppUiSkipName,
     flag_descriptions::kEnableWebPaymentsSingleAppUiSkipDescription,
     kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kWebPaymentsSingleAppUiSkip)},
    {"android-payment-apps", flag_descriptions::kAndroidPaymentAppsName,
     flag_descriptions::kAndroidPaymentAppsDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kAndroidPaymentApps)},
    {"service-worker-payment-apps",
     flag_descriptions::kServiceWorkerPaymentAppsName,
     flag_descriptions::kServiceWorkerPaymentAppsDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(features::kServiceWorkerPaymentApps)},
#endif  // OS_ANDROID
#if defined(OS_CHROMEOS)
    {"disable-eol-notification", flag_descriptions::kEolNotificationName,
     flag_descriptions::kEolNotificationDescription, kOsCrOS,
     SINGLE_DISABLE_VALUE_TYPE(chromeos::switches::kDisableEolNotification)},
#endif  // OS_CHROMEOS
    {"fill-on-account-select", flag_descriptions::kFillOnAccountSelectName,
     flag_descriptions::kFillOnAccountSelectDescription, kOsAll,
     FEATURE_VALUE_TYPE(password_manager::features::kFillOnAccountSelect)},
    {"new-audio-rendering-mixing-strategy",
     flag_descriptions::kNewAudioRenderingMixingStrategyName,
     flag_descriptions::kNewAudioRenderingMixingStrategyDescription,
     kOsWin | kOsMac | kOsLinux | kOsAndroid,
     FEATURE_VALUE_TYPE(media::kNewAudioRenderingMixingStrategy)},
    {"disable-background-video-track",
     flag_descriptions::kBackgroundVideoTrackOptimizationName,
     flag_descriptions::kBackgroundVideoTrackOptimizationDescription, kOsAll,
     FEATURE_VALUE_TYPE(media::kBackgroundVideoTrackOptimization)},
    {"enable-new-remote-playback-pipeline",
     flag_descriptions::kNewRemotePlaybackPipelineName,
     flag_descriptions::kNewRemotePlaybackPipelineDescription, kOsAll,
     FEATURE_VALUE_TYPE(media::kNewRemotePlaybackPipeline)},
#if defined(OS_CHROMEOS)
    {"quick-unlock-pin", flag_descriptions::kQuickUnlockPinName,
     flag_descriptions::kQuickUnlockPinDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(features::kQuickUnlockPin)},
    {"quick-unlock-fingerprint", flag_descriptions::kQuickUnlockFingerprint,
     flag_descriptions::kQuickUnlockFingerprintDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(features::kQuickUnlockFingerprint)},
#endif  // OS_CHROMEOS
    {"browser-task-scheduler", flag_descriptions::kBrowserTaskSchedulerName,
     flag_descriptions::kBrowserTaskSchedulerDescription, kOsAll,
     ENABLE_DISABLE_VALUE_TYPE(switches::kEnableBrowserTaskScheduler,
                               switches::kDisableBrowserTaskScheduler)},
#if defined(OS_ANDROID)
    {"enable-improved-a2hs", flag_descriptions::kEnableWebapk,
     flag_descriptions::kEnableWebapkDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kImprovedA2HS)},
    {"no-credit-card-abort", flag_descriptions::kNoCreditCardAbort,
     flag_descriptions::kNoCreditCardAbortDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kNoCreditCardAbort)},
#endif  // OS_ANDROID
    {"enable-feature-policy", flag_descriptions::kFeaturePolicyName,
     flag_descriptions::kFeaturePolicyDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kFeaturePolicy)},
#if defined(OS_CHROMEOS)
    {"enable-emoji-handwriting-voice-on-ime-menu",
     flag_descriptions::kEnableEhvInputName,
     flag_descriptions::kEnableEhvInputDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(features::kEHVInputOnImeMenu)},
#endif  // OS_CHROMEOS
    {"enable-gamepad-extensions", flag_descriptions::kGamepadExtensionsName,
     flag_descriptions::kGamepadExtensionsDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kGamepadExtensions)},
#if defined(OS_CHROMEOS)
    {"arc-use-auth-endpoint", flag_descriptions::kArcUseAuthEndpointName,
     flag_descriptions::kArcUseAuthEndpointDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(arc::kArcUseAuthEndpointFeature)},
    {"arc-boot-completed-broadcast", flag_descriptions::kArcBootCompleted,
     flag_descriptions::kArcBootCompletedDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(arc::kBootCompletedBroadcastFeature)},
#endif  // OS_CHROMEOS
    {"enable-generic-sensor", flag_descriptions::kEnableGenericSensorName,
     flag_descriptions::kEnableGenericSensorDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kGenericSensor)},
    {"expensive-background-timer-throttling",
     flag_descriptions::kExpensiveBackgroundTimerThrottlingName,
     flag_descriptions::kExpensiveBackgroundTimerThrottlingDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kExpensiveBackgroundTimerThrottling)},
#if defined(OS_CHROMEOS)
    {"enumerate-audio-devices",
     flag_descriptions::kEnableEnumeratingAudioDevicesName,
     flag_descriptions::kEnableEnumeratingAudioDevicesDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(features::kEnumerateAudioDevices)},
#endif  // OS_CHROMEOS
#if !defined(OS_ANDROID)
    {"enable-audio-focus", flag_descriptions::kEnableAudioFocusName,
     flag_descriptions::kEnableAudioFocusDescription, kOsDesktop,
     MULTI_VALUE_TYPE(kEnableAudioFocusChoices)},
#endif  // !OS_ANDROID
#if defined(OS_ANDROID)
    {"modal-permission-prompts", flag_descriptions::kModalPermissionPromptsName,
     flag_descriptions::kModalPermissionPromptsDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(features::kModalPermissionPrompts)},
#endif
#if !defined(OS_MACOSX)
    {"permission-prompt-persistence-toggle",
     flag_descriptions::kPermissionPromptPersistenceToggleName,
     flag_descriptions::kPermissionPromptPersistenceToggleDescription,
     kOsWin | kOsCrOS | kOsLinux | kOsAndroid,
     FEATURE_VALUE_TYPE(
         features::kDisplayPersistenceToggleInPermissionPrompts)},
#endif
#if BUILDFLAG(ENABLE_PRINT_PREVIEW) && !defined(OS_WIN) && !defined(OS_MACOSX)
    {"print-pdf-as-image", flag_descriptions::kPrintPdfAsImageName,
     flag_descriptions::kPrintPdfAsImageDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kPrintPdfAsImage)},
#endif
#if defined(OS_ANDROID)
    {"enable-consistent-omnibox-geolocation",
     flag_descriptions::kEnableConsistentOmniboxGeolocationName,
     flag_descriptions::kEnableConsistentOmniboxGeolocationDescription,
     kOsAndroid, FEATURE_VALUE_TYPE(features::kConsistentOmniboxGeolocation)},
    {"concurrent-background-loading-on-svelte",
     flag_descriptions::kOfflinePagesSvelteConcurrentLoadingName,
     flag_descriptions::kOfflinePagesSvelteConcurrentLoadingDescription,
     kOsAndroid,
     FEATURE_VALUE_TYPE(
         offline_pages::kOfflinePagesSvelteConcurrentLoadingFeature)},
    {"web-payments-modifiers", flag_descriptions::kWebPaymentsModifiersName,
     flag_descriptions::kWebPaymentsModifiersDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kWebPaymentsModifiers)},
    {"xgeo-visible-networks", flag_descriptions::kXGEOVisibleNetworksName,
     flag_descriptions::kXGEOVisibleNetworksDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kXGEOVisibleNetworks)},
#endif  // !defined(OS_ANDROID)
    {"cross-process-guests",
     flag_descriptions::kCrossProcessGuestViewIsolationName,
     flag_descriptions::kCrossProcessGuestViewIsolationDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kGuestViewCrossProcessFrames)},
#if !defined(OS_ANDROID) && !defined(OS_IOS)
    {"media-remoting", flag_descriptions::kMediaRemotingName,
     flag_descriptions::kMediaRemotingDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kMediaRemoting)},
#endif
#if defined(OS_ANDROID)
    {"video-fullscreen-orientation-lock",
     flag_descriptions::kVideoFullscreenOrientationLockName,
     flag_descriptions::kVideoFullscreenOrientationLockDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(media::kVideoFullscreenOrientationLock)},
    {"video-rotate-to-fullscreen",
     flag_descriptions::kVideoRotateToFullscreenName,
     flag_descriptions::kVideoRotateToFullscreenDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(media::kVideoRotateToFullscreen)},
#endif
    {"enable-nostate-prefetch", flag_descriptions::kNostatePrefetchName,
     flag_descriptions::kNostatePrefetchDescription, kOsAll,
     FEATURE_WITH_PARAMS_VALUE_TYPE(prerender::kNoStatePrefetchFeature,
                                    kNoStatePrefetchFeatureVariations,
                                    "NoStatePrefetchValidation")},
#if defined(OS_CHROMEOS)
    {"enable-android-wallpapers-app",
     flag_descriptions::kEnableAndroidWallpapersAppName,
     flag_descriptions::kEnableAndroidWallpapersAppDescription, kOsCrOS,
     SINGLE_VALUE_TYPE(chromeos::switches::kEnableAndroidWallpapersApp)},
#endif  // defined(OS_CHROMEOS)

#if defined(OS_ANDROID)
    {"enable-expanded-autofill-credit-card-popup",
     flag_descriptions::kEnableExpandedAutofillCreditCardPopupLayoutName,
     flag_descriptions::kEnableExpandedAutofillCreditCardPopupLayoutDescription,
     kOsAndroid,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         autofill::kAutofillCreditCardPopupLayout,
         kAutofillCreditCardPopupLayoutFeatureVariations,
         // Must be AutofillCreditCardDropdownVariations to prevent DCHECK crash
         // when the flag is manually enabled in a local build.
         "AutofillCreditCardDropdownVariations")},
#endif  // OS_ANDROID
    {"enable-autofill-credit-card-last-used-date-display",
     flag_descriptions::kEnableAutofillCreditCardLastUsedDateDisplayName,
     flag_descriptions::kEnableAutofillCreditCardLastUsedDateDisplayDescription,
     kOsAll,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         autofill::kAutofillCreditCardLastUsedDateDisplay,
         kAutofillCreditCardLastUsedDateFeatureVariations,
         // Must be AutofillCreditCardDropdownVariations to prevent DCHECK crash
         // when the flag is manually enabled in a local build.
         "AutofillCreditCardDropdownVariations")},
    {"enable-autofill-credit-card-upload-cvc-prompt",
     flag_descriptions::kEnableAutofillCreditCardUploadCvcPromptName,
     flag_descriptions::kEnableAutofillCreditCardUploadCvcPromptDescription,
     kOsDesktop,
     FEATURE_VALUE_TYPE(autofill::kAutofillUpstreamRequestCvcIfMissing)},
#if defined(OS_WIN)
    {"windows10-custom-titlebar",
     flag_descriptions::kWindows10CustomTitlebarName,
     flag_descriptions::kWindows10CustomTitlebarDescription, kOsWin,
     SINGLE_VALUE_TYPE(switches::kWindows10CustomTitlebar)},
#endif  // OS_WIN

#if defined(OS_ANDROID)
    {"lsd-permission-prompt", flag_descriptions::kLsdPermissionPromptName,
     flag_descriptions::kLsdPermissionPromptDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(features::kLsdPermissionPrompt)},
#endif

#if defined(OS_CHROMEOS)
    {"enable-touchscreen-calibration",
     flag_descriptions::kTouchscreenCalibrationName,
     flag_descriptions::kTouchscreenCalibrationDescription, kOsCrOS,
     SINGLE_VALUE_TYPE(chromeos::switches::kEnableTouchCalibrationSetting)},
#endif // defined(OS_CHROMEOS)
#if defined(OS_CHROMEOS)
    {"team-drives", flag_descriptions::kTeamDrivesName,
     flag_descriptions::kTeamDrivesDescription, kOsCrOS,
     SINGLE_VALUE_TYPE(google_apis::kEnableTeamDrives)},
#endif  // OS_CHROMEOS

#if defined(OS_WIN)
    {"gdi-text-printing", flag_descriptions::kGdiTextPrinting,
     flag_descriptions::kGdiTextPrintingDescription, kOsWin,
     FEATURE_VALUE_TYPE(features::kGdiTextPrinting)},
    {"postscript-printing", flag_descriptions::kDisablePostscriptPrinting,
     flag_descriptions::kDisablePostscriptPrintingDescription, kOsWin,
     FEATURE_VALUE_TYPE(features::kDisablePostScriptPrinting)},
#endif  // defined(OS_WIN)

#if defined(OS_CHROMEOS)
    {"show-arc-files-app", flag_descriptions::kShowArcFilesAppName,
     flag_descriptions::kShowArcFilesAppDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(arc::kShowArcFilesAppFeature)},
#endif // defined(OS_CHROMEOS)

#if defined(OS_CHROMEOS)
    {"force-enable-stylus-tools",
     flag_descriptions::kForceEnableStylusToolsName,
     flag_descriptions::kForceEnableStylusToolsDescription, kOsCrOS,
     SINGLE_VALUE_TYPE(ash::switches::kAshForceEnableStylusTools)},
#endif // defined(OS_CHROMEOS)

    {"enable-midi-manager-dynamic-instantiation",
     flag_descriptions::kEnableMidiManagerDynamicInstantiationName,
     flag_descriptions::kEnableMidiManagerDynamicInstantiationDescription,
     kOsAll,
     FEATURE_VALUE_TYPE(midi::features::kMidiManagerDynamicInstantiation)},

#if defined(OS_WIN)
    {"new-usb-backend", flag_descriptions::kNewUsbBackendName,
     flag_descriptions::kNewUsbBackendDescription, kOsWin,
     FEATURE_VALUE_TYPE(device::kNewUsbBackend)},
    {"enable-desktop-ios-promotions",
     flag_descriptions::kEnableDesktopIosPromotionsName,
     flag_descriptions::kEnableDesktopIosPromotionsDescription, kOsWin,
     FEATURE_VALUE_TYPE(features::kDesktopIOSPromotion)},
#endif  // defined(OS_WIN)

#if defined(OS_CHROMEOS)
    {"enable-touch-support-for-screen-magnifier",
     flag_descriptions::kEnableTouchSupportForScreenMagnifierName,
     flag_descriptions::kEnableTouchSupportForScreenMagnifierDescription,
     kOsCrOS,
     SINGLE_VALUE_TYPE(
         chromeos::switches::kEnableTouchSupportForScreenMagnifier)},
#endif  // OS_CHROMEOS

    {"enable-zero-suggest-redirect-to-chrome",
     flag_descriptions::kEnableZeroSuggestRedirectToChromeName,
     flag_descriptions::kEnableZeroSuggestRedirectToChromeDescription, kOsAll,
     FEATURE_VALUE_TYPE(omnibox::kZeroSuggestRedirectToChrome)},
    {"new-omnibox-answer-types", flag_descriptions::kNewOmniboxAnswerTypesName,
     flag_descriptions::kNewOmniboxAnswerTypesDescription, kOsAll,
     FEATURE_VALUE_TYPE(omnibox::kNewOmniboxAnswerTypes)},

#if defined(OS_CHROMEOS) || defined(OS_LINUX) || defined(OS_MACOSX) || \
    defined(OS_WIN)
    {"omnibox-entity-suggestions",
     flag_descriptions::kOmniboxEntitySuggestionsName,
     flag_descriptions::kOmniboxEntitySuggestionsDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(omnibox::kOmniboxEntitySuggestions)},
    {"enable-new-app-menu-icon", flag_descriptions::kEnableNewAppMenuIconName,
     flag_descriptions::kEnableNewAppMenuIconDescription, kOsDesktop,
     SINGLE_VALUE_TYPE(switches::kEnableNewAppMenuIcon)},
#endif  // defined(OS_CHROMEOS) || defined(OS_LINUX) || defined(OS_MACOSX) ||
        // defined(OS_WIN)

#if defined(OS_ANDROID)
    {"enable-custom-feedback-ui",
     flag_descriptions::kEnableCustomFeedbackUiName,
     flag_descriptions::kEnableCustomFeedbackUiDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kCustomFeedbackUi)},
#endif  // OS_ANDROID

    {"enable-resource-prefetch", flag_descriptions::kSpeculativePrefetchName,
     flag_descriptions::kSpeculativePrefetchDescription, kOsAll,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         predictors::kSpeculativeResourcePrefetchingFeature,
         kSpeculativeResourcePrefetchingFeatureVariations,
         "SpeculativeResourcePrefetchingValidation")},

    {"enable-off-main-thread-fetch", flag_descriptions::kOffMainThreadFetchName,
     flag_descriptions::kOffMainThreadFetchDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kOffMainThreadFetch)},

#if defined(OS_MACOSX)
    {"tab-strip-keyboard-focus", flag_descriptions::kTabStripKeyboardFocusName,
     flag_descriptions::kTabStripKeyboardFocusDescription, kOsMac,
     FEATURE_VALUE_TYPE(features::kTabStripKeyboardFocus)},
#endif

#if defined(OS_CHROMEOS)
    {"enable-chromevox-arc-support",
     flag_descriptions::kEnableChromevoxArcSupportName,
     flag_descriptions::kEnableChromevoxArcSupportDescription, kOsCrOS,
     SINGLE_VALUE_TYPE(chromeos::switches::kEnableChromeVoxArcSupport)},
#endif  // defined(OS_CHROMEOS)

    {"enable-mojo-loading", flag_descriptions::kMojoLoadingName,
     flag_descriptions::kMojoLoadingDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kLoadingWithMojo)},

#if defined(OS_CHROMEOS)
    {"force-tablet-mode", flag_descriptions::kForceTabletModeName,
     flag_descriptions::kForceTabletModeDescription, kOsCrOS,
     MULTI_VALUE_TYPE(kAshForceTabletModeChoices)},
#endif  // OS_CHROMEOS

#if defined(OS_ANDROID)
    {"use-new-doodle-api", flag_descriptions::kUseNewDoodleApiName,
     flag_descriptions::kUseNewDoodleApiDescription, kOsAndroid,
     FEATURE_WITH_PARAMS_VALUE_TYPE(chrome::android::kUseNewDoodleApi,
                                    kUseNewDoodleApiVariations,
                                    chrome::android::kUseNewDoodleApi.name)},
#endif  // OS_ANDROID

    {"memory-ablation", flag_descriptions::kMemoryAblationName,
     flag_descriptions::kMemoryAblationDescription, kOsAll,
     FEATURE_WITH_PARAMS_VALUE_TYPE(kMemoryAblationFeature,
                                    kMemoryAblationFeatureVariations,
                                    "MemoryAblation")},

#if defined(OS_ANDROID)
    {"enable-custom-context-menu",
     flag_descriptions::kEnableCustomContextMenuName,
     flag_descriptions::kEnableCustomContextMenuDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(chrome::android::kCustomContextMenu)},
#endif  // OS_ANDROID

#if defined(OS_CHROMEOS) || defined(OS_LINUX) || defined(OS_MACOSX) || \
    defined(OS_WIN)
    {pausetabs::kFeatureName, flag_descriptions::kPauseBackgroundTabsName,
     flag_descriptions::kPauseBackgroundTabsDescription, kOsDesktop,
     FEATURE_WITH_PARAMS_VALUE_TYPE(pausetabs::kFeature,
                                    kPauseBackgroundTabsVariations,
                                    "PauseBackgroundTabs")},
#endif

#if defined(USE_ASH)
    {"ash-disable-smooth-screen-rotation",
     flag_descriptions::kAshDisableSmoothScreenRotationName,
     flag_descriptions::kAshDisableSmoothScreenRotationDescription, kOsCrOS,
     SINGLE_DISABLE_VALUE_TYPE(ash::switches::kAshDisableSmoothScreenRotation)},
#endif  // defined(USE_ASH)

#if defined(OS_CHROMEOS)
    {"enable-zip-archiver-on-file-manager",
     flag_descriptions::kEnableZipArchiverOnFileManagerName,
     flag_descriptions::kEnableZipArchiverOnFileManagerDescription, kOsCrOS,
     SINGLE_VALUE_TYPE(chromeos::switches::kEnableZipArchiverOnFileManager)},
#endif  // OS_CHROMEOS

#if defined(OS_ANDROID)
    {"enable-copyless-paste", flag_descriptions::kEnableCopylessPasteName,
     flag_descriptions::kEnableCopylessPasteDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(features::kCopylessPaste)},
#endif

    {"omnibox-display-title-for-current-url",
     flag_descriptions::kOmniboxDisplayTitleForCurrentUrlName,
     flag_descriptions::kOmniboxDisplayTitleForCurrentUrlDescription,
     kOsDesktop, FEATURE_VALUE_TYPE(omnibox::kDisplayTitleForCurrentUrl)},

    {"enable-color-correct-rendering",
     flag_descriptions::kColorCorrectRenderingName,
     flag_descriptions::kColorCorrectRenderingDescription, kOsAll,
     MULTI_VALUE_TYPE(kColorCorrectRenderingChoices)},

#if defined(OS_CHROMEOS)
    {"quick-unlock-pin-signin", flag_descriptions::kQuickUnlockPinSignin,
     flag_descriptions::kQuickUnlockPinSigninDescription, kOsCrOS,
     FEATURE_VALUE_TYPE(features::kQuickUnlockPinSignin)},
#endif  // OS_CHROMEOS

#if defined(OS_ANDROID)
    {"enable-webnfc", flag_descriptions::kEnableWebNfcName,
     flag_descriptions::kEnableWebNfcDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(features::kWebNfc)},
#endif

    {"enable-idle-time-spell-checking",
     flag_descriptions::kEnableIdleTimeSpellCheckingName,
     flag_descriptions::kEnableIdleTimeSpellCheckingDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kIdleTimeSpellChecking)},

#if defined(OS_ANDROID)
    {"enable-clipboard-provider",
     flag_descriptions::kEnableOmniboxClipboardProviderName,
     flag_descriptions::kEnableOmniboxClipboardProviderDescription, kOsAndroid,
     FEATURE_VALUE_TYPE(omnibox::kEnableClipboardProvider)},
#endif

    {"autoplay-policy", flag_descriptions::kAutoplayPolicyName,
     flag_descriptions::kAutoplayPolicyDescription, kOsAll,
     MULTI_VALUE_TYPE(kAutoplayPolicyChoices)},

    {"force-effective-connection-type",
     flag_descriptions::kForceEffectiveConnectionTypeName,
     flag_descriptions::kForceEffectiveConnectionTypeDescription, kOsAll,
     MULTI_VALUE_TYPE(kForceEffectiveConnectionTypeChoices)},

    {"enable-heap-profiling", flag_descriptions::kEnableHeapProfilingName,
     flag_descriptions::kEnableHeapProfilingDescription, kOsAll,
     MULTI_VALUE_TYPE(kEnableHeapProfilingChoices)},

#if defined(TOOLKIT_VIEWS) || (defined(OS_MACOSX) && !defined(OS_IOS))
    {"show-cert-link", flag_descriptions::kShowCertLinkOnPageInfoName,
     flag_descriptions::kShowCertLinkOnPageInfoDescription, kOsDesktop,
     SINGLE_VALUE_TYPE(switches::kShowCertLink)},
#endif

    {"omnibox-ui-max-autocomplete-matches",
     flag_descriptions::kOmniboxUIMaxAutocompleteMatchesName,
     flag_descriptions::kOmniboxUIMaxAutocompleteMatchesDescription, kOsDesktop,
     FEATURE_WITH_PARAMS_VALUE_TYPE(
         omnibox::kUIExperimentMaxAutocompleteMatches,
         kOmniboxUIMaxAutocompleteMatchesVariations,
         "OmniboxUIMaxAutocompleteVariations")},

    {"omnibox-ui-vertical-margin",
     flag_descriptions::kOmniboxUIVerticalMarginName,
     flag_descriptions::kOmniboxUIVerticalMarginDescription, kOsDesktop,
     FEATURE_WITH_PARAMS_VALUE_TYPE(omnibox::kUIExperimentVerticalMargin,
                                    kOmniboxUIVerticalMarginVariations,
                                    "OmniboxUIVerticalMarginVariations")},

    {"use-suggestions-even-if-few",
     flag_descriptions::kUseSuggestionsEvenIfFewFeatureName,
     flag_descriptions::kUseSuggestionsEvenIfFewFeatureDescription, kOsAll,
     FEATURE_VALUE_TYPE(suggestions::kUseSuggestionsEvenIfFewFeature)},

    {"enable-location-hard-reload", flag_descriptions::kLocationHardReloadName,
     flag_descriptions::kLocationHardReloadDescription, kOsAll,
     FEATURE_VALUE_TYPE(features::kLocationHardReload)},

    {"capture-thumbnail-on-load-finished",
     flag_descriptions::kCaptureThumbnailOnLoadFinishedName,
     flag_descriptions::kCaptureThumbnailOnLoadFinishedDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kCaptureThumbnailOnLoadFinished)},

#if defined(OS_WIN)
    {"enable-d3d-vsync", flag_descriptions::kEnableD3DVsync,
     flag_descriptions::kEnableD3DVsyncDescription, kOsWin,
     FEATURE_VALUE_TYPE(features::kD3DVsync)},
#endif  // defined(OS_WIN)

#if !defined(OS_ANDROID) && !defined(OS_IOS)
    {"use-google-local-ntp", flag_descriptions::kUseGoogleLocalNtpName,
     flag_descriptions::kUseGoogleLocalNtpDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kUseGoogleLocalNtp)},

    {"one-google-bar-on-local-ntp",
     flag_descriptions::kOneGoogleBarOnLocalNtpName,
     flag_descriptions::kOneGoogleBarOnLocalNtpDescription, kOsDesktop,
     FEATURE_VALUE_TYPE(features::kOneGoogleBarOnLocalNtp)},
#endif  // !defined(OS_ANDROID) && !defined(OS_IOS)

#if defined(OS_MACOSX)
    {"mac-rtl", flag_descriptions::kMacRTLName,
     flag_descriptions::kMacRTLDescription, kOsMac,
     FEATURE_VALUE_TYPE(features::kMacRTL)},
#endif  // defined(OS_MACOSX)

#if defined(OS_CHROMEOS)
    {"disable-new-virtual-keyboard-behavior",
     flag_descriptions::kDisableNewVirtualKeyboardBehaviorName,
     flag_descriptions::kDisableNewVirtualKeyboardBehaviorDescription, kOsCrOS,
     SINGLE_DISABLE_VALUE_TYPE(switches::kDisableNewVirtualKeyboardBehavior)},
#endif  // defined(OS_CHROMEOS)

    // NOTE: Adding new command-line switches requires adding corresponding
    // entries to enum "LoginCustomFlags" in histograms/enums.xml. See note in
    // enums.xml and don't forget to run AboutFlagsHistogramTest unit test.
};

class FlagsStateSingleton {
 public:
  FlagsStateSingleton()
      : flags_state_(kFeatureEntries, arraysize(kFeatureEntries)) {}
  ~FlagsStateSingleton() {}

  static FlagsStateSingleton* GetInstance() {
    return base::Singleton<FlagsStateSingleton>::get();
  }

  static flags_ui::FlagsState* GetFlagsState() {
    return &GetInstance()->flags_state_;
  }

 private:
  flags_ui::FlagsState flags_state_;

  DISALLOW_COPY_AND_ASSIGN(FlagsStateSingleton);
};

bool SkipConditionalFeatureEntry(const FeatureEntry& entry) {
  version_info::Channel channel = chrome::GetChannel();
#if defined(OS_ANDROID)
  // enable-data-reduction-proxy-dev is only available for the Dev/Beta channel.
  if (!strcmp("enable-data-reduction-proxy-dev", entry.internal_name) &&
      channel != version_info::Channel::BETA &&
      channel != version_info::Channel::DEV) {
    return true;
  }
  // enable-data-reduction-proxy-alt is only available for the Dev channel.
  if (!strcmp("enable-data-reduction-proxy-alt", entry.internal_name) &&
      channel != version_info::Channel::DEV) {
    return true;
  }
  // enable-data-reduction-proxy-carrier-test is only available for Chromium
  // builds and the Canary/Dev channel.
  if (!strcmp("enable-data-reduction-proxy-carrier-test",
              entry.internal_name) &&
      channel != version_info::Channel::DEV &&
      channel != version_info::Channel::CANARY &&
      channel != version_info::Channel::UNKNOWN) {
    return true;
  }
#endif  // OS_ANDROID

#if defined(OS_CHROMEOS)
  // Don't expose --mash/--mus outside of Canary or developer builds.
  if (!strcmp("mus", entry.internal_name) &&
      channel != version_info::Channel::DEV &&
      channel != version_info::Channel::UNKNOWN) {
    return true;
  }
#endif  // defined(OS_CHROMEOS)

  // data-reduction-proxy-lo-fi and enable-data-reduction-proxy-lite-page
  // are only available for Chromium builds and the Canary/Dev/Beta channels.
  if ((!strcmp("data-reduction-proxy-lo-fi", entry.internal_name) ||
       !strcmp("enable-data-reduction-proxy-lite-page",
               entry.internal_name)) &&
      channel != version_info::Channel::BETA &&
      channel != version_info::Channel::DEV &&
      channel != version_info::Channel::CANARY &&
      channel != version_info::Channel::UNKNOWN) {
    return true;
  }

#if defined(OS_WIN)
  // HDR mode works, but displays everything horribly wrong prior to windows 10.
  if (!strcmp("enable-hdr", entry.internal_name) &&
      base::win::GetVersion() < base::win::Version::VERSION_WIN10) {
    return true;
  }
#endif  // OS_WIN

  return false;
}

// Records a set of feature switches (prefixed with "--").
void ReportAboutFlagsHistogramSwitches(const std::string& uma_histogram_name,
                                       const std::set<std::string>& switches) {
  for (const std::string& flag : switches) {
    int uma_id = about_flags::testing::kBadSwitchFormatHistogramId;
    if (base::StartsWith(flag, "--", base::CompareCase::SENSITIVE)) {
      // Skip '--' before switch name.
      std::string switch_name(flag.substr(2));

      // Kill value, if any.
      const size_t value_pos = switch_name.find('=');
      if (value_pos != std::string::npos)
        switch_name.resize(value_pos);

      uma_id = GetSwitchUMAId(switch_name);
    } else {
      NOTREACHED() << "ReportAboutFlagsHistogram(): flag '" << flag
                   << "' has incorrect format.";
    }
    DVLOG(1) << "ReportAboutFlagsHistogram(): histogram='" << uma_histogram_name
             << "' '" << flag << "', uma_id=" << uma_id;

    // Sparse histogram macro does not cache the histogram, so it's safe
    // to use macro with non-static histogram name here.
    UMA_HISTOGRAM_SPARSE_SLOWLY(uma_histogram_name, uma_id);
  }
}

// Records a set of FEATURE_VALUE_TYPE features (suffixed with ":enabled" or
// "disabled", depending on their state).
void ReportAboutFlagsHistogramFeatures(const std::string& uma_histogram_name,
                                       const std::set<std::string>&features) {
  for (const std::string& feature : features) {
    int uma_id = GetSwitchUMAId(feature);
    DVLOG(1) << "ReportAboutFlagsHistogram(): histogram='" << uma_histogram_name
             << "' '" << feature << "', uma_id=" << uma_id;

    // Sparse histogram macro does not cache the histogram, so it's safe
    // to use macro with non-static histogram name here.
    UMA_HISTOGRAM_SPARSE_SLOWLY(uma_histogram_name, uma_id);
  }
}

}  // namespace

void ConvertFlagsToSwitches(flags_ui::FlagsStorage* flags_storage,
                            base::CommandLine* command_line,
                            flags_ui::SentinelsMode sentinels) {
  if (command_line->HasSwitch(switches::kNoExperiments))
    return;

  FlagsStateSingleton::GetFlagsState()->ConvertFlagsToSwitches(
      flags_storage, command_line, sentinels, switches::kEnableFeatures,
      switches::kDisableFeatures);
}

std::vector<std::string> RegisterAllFeatureVariationParameters(
    flags_ui::FlagsStorage* flags_storage,
    base::FeatureList* feature_list) {
  return FlagsStateSingleton::GetFlagsState()
      ->RegisterAllFeatureVariationParameters(flags_storage, feature_list);
}

bool AreSwitchesIdenticalToCurrentCommandLine(
    const base::CommandLine& new_cmdline,
    const base::CommandLine& active_cmdline,
    std::set<base::CommandLine::StringType>* out_difference) {
  const char* extra_flag_sentinel_begin_flag_name = nullptr;
  const char* extra_flag_sentinel_end_flag_name = nullptr;
#if defined(OS_CHROMEOS)
  // Put the flags between --policy-switches--begin and --policy-switches-end on
  // ChromeOS.
  extra_flag_sentinel_begin_flag_name =
      chromeos::switches::kPolicySwitchesBegin;
  extra_flag_sentinel_end_flag_name = chromeos::switches::kPolicySwitchesEnd;
#endif  // OS_CHROMEOS
  return flags_ui::FlagsState::AreSwitchesIdenticalToCurrentCommandLine(
      new_cmdline, active_cmdline, out_difference,
      extra_flag_sentinel_begin_flag_name, extra_flag_sentinel_end_flag_name);
}

void GetFlagFeatureEntries(flags_ui::FlagsStorage* flags_storage,
                           flags_ui::FlagAccess access,
                           base::ListValue* supported_entries,
                           base::ListValue* unsupported_entries) {
  FlagsStateSingleton::GetFlagsState()->GetFlagFeatureEntries(
      flags_storage, access, supported_entries, unsupported_entries,
      base::Bind(&SkipConditionalFeatureEntry));
}

bool IsRestartNeededToCommitChanges() {
  return FlagsStateSingleton::GetFlagsState()->IsRestartNeededToCommitChanges();
}

void SetFeatureEntryEnabled(flags_ui::FlagsStorage* flags_storage,
                            const std::string& internal_name,
                            bool enable) {
  FlagsStateSingleton::GetFlagsState()->SetFeatureEntryEnabled(
      flags_storage, internal_name, enable);
}

void RemoveFlagsSwitches(
    std::map<std::string, base::CommandLine::StringType>* switch_list) {
  FlagsStateSingleton::GetFlagsState()->RemoveFlagsSwitches(switch_list);
}

void ResetAllFlags(flags_ui::FlagsStorage* flags_storage) {
  FlagsStateSingleton::GetFlagsState()->ResetAllFlags(flags_storage);
}

void RecordUMAStatistics(flags_ui::FlagsStorage* flags_storage) {
  const std::set<std::string> switches =
      FlagsStateSingleton::GetFlagsState()->GetSwitchesFromFlags(flags_storage);
  const std::set<std::string> features =
      FlagsStateSingleton::GetFlagsState()->GetFeaturesFromFlags(flags_storage);
  ReportAboutFlagsHistogram("Launch.FlagsAtStartup", switches, features);
}

base::HistogramBase::Sample GetSwitchUMAId(const std::string& switch_name) {
  return static_cast<base::HistogramBase::Sample>(
      base::HashMetricName(switch_name));
}

void ReportAboutFlagsHistogram(
    const std::string& uma_histogram_name,
    const std::set<std::string>& switches,
    const std::set<std::string>& features) {
  ReportAboutFlagsHistogramSwitches(uma_histogram_name, switches);
  ReportAboutFlagsHistogramFeatures(uma_histogram_name, features);
}

namespace testing {

const base::HistogramBase::Sample kBadSwitchFormatHistogramId = 0;

const FeatureEntry* GetFeatureEntries(size_t* count) {
  *count = arraysize(kFeatureEntries);
  return kFeatureEntries;
}

}  // namespace testing

}  // namespace about_flags
