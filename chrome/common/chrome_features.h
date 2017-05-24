// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file defines all the public base::FeatureList features for the chrome
// module.

#ifndef CHROME_COMMON_CHROME_FEATURES_H_
#define CHROME_COMMON_CHROME_FEATURES_H_

#include "base/feature_list.h"
#include "chrome/common/features.h"
#include "extensions/features/features.h"
#include "ppapi/features/features.h"
#include "printing/features/features.h"

namespace features {

// All features in alphabetical order. The features should be documented
// alongside the definition of their values in the .cc file.

#if defined(OS_ANDROID)
extern const base::Feature kAllowAutoplayUnmutedInWebappManifestScope;
#endif  // defined(OS_ANDROID)

#if defined(OS_MACOSX)
extern const base::Feature kAppleScriptExecuteJavaScript;
extern const base::Feature kViewsTaskManager;
#endif  // defined(OS_MACOSX)

#if defined(OS_CHROMEOS)
extern const base::Feature kArcMemoryManagement;
#endif  // defined(OS_CHROMEOS)

extern const base::Feature kAssetDownloadSuggestionsFeature;

#if !defined(OS_ANDROID) && !defined(OS_IOS)
extern const base::Feature kAutoDismissingDialogs;
#endif

#if defined(OS_WIN) || defined(OS_MACOSX)
extern const base::Feature kAutomaticTabDiscarding;
#endif  // defined(OS_WIN) || defined(OS_MACOSX)

#if defined(OS_WIN) || defined(OS_LINUX)
extern const base::Feature kBackgroundModeAllowRestart;
#endif  // defined(OS_WIN) || defined(OS_LINUX)

extern const base::Feature kBackspaceGoesBackFeature;

extern const base::Feature kBlockPromptsIfDismissedOften;
extern const base::Feature kBlockPromptsIfIgnoredOften;

#if defined(OS_MACOSX)
extern const base::Feature kBookmarkApps;
#endif

extern const base::Feature kBrowserHangFixesExperiment;

#if defined(OS_MACOSX)
extern const base::Feature kBrowserTouchBar;
extern const base::Feature kTabStripKeyboardFocus;
#endif  // defined(OS_MACOSX)

extern const base::Feature kCaptureThumbnailOnLoadFinished;

extern const base::Feature kCheckInstallabilityForBannerOnLoad;

#if defined(OS_WIN)
extern const base::Feature kCleanupToolUI;
#endif

#if defined(OS_ANDROID)
extern const base::Feature kConsistentOmniboxGeolocation;
#endif

#if defined(OS_ANDROID)
extern const base::Feature kCopylessPaste;
#endif

#if defined(OS_WIN)
extern const base::Feature kDesktopIOSPromotion;
#endif  // defined(OS_WIN)

extern const base::Feature kDisplayPersistenceToggleInPermissionPrompts;

extern const base::Feature kExpectCTReporting;

extern const base::Feature kExperimentalKeyboardLockUI;

#if defined(OS_WIN)
extern const base::Feature kGdiTextPrinting;
#endif

#if defined(OS_CHROMEOS)
extern const base::Feature kHappinessTrackingSystem;
#endif

extern const base::Feature kImportantSitesInCbd;

extern const base::Feature kImprovedRecoveryComponent;

#if defined(GOOGLE_CHROME_BUILD) && defined(OS_LINUX) && !defined(OS_CHROMEOS)
extern const base::Feature kLinuxObsoleteSystemIsEndOfTheLine;
#endif

extern const base::Feature kLsdPermissionPrompt;

#if defined(OS_MACOSX)
extern const base::Feature kMacRTL;
#endif

extern const base::Feature kMaterialDesignBookmarks;

#if BUILDFLAG(ENABLE_EXTENSIONS)
extern const base::Feature kMaterialDesignExtensions;
extern const base::Feature kAcknowledgeNtpOverrideOnDeactivate;
#endif

extern const base::Feature kMaterialDesignIncognitoNTP;

extern const base::Feature kMaterialDesignSettings;

#if !defined(OS_ANDROID) && !defined(OS_IOS)
extern const base::Feature kMediaRemoting;
extern const base::Feature kMediaRouterUIRouteController;
#endif  // !defined(OS_ANDROID) && !defined(OS_IOS)

extern const base::Feature kModalPermissionPrompts;

#if defined(OS_WIN)
extern const base::Feature kModuleDatabase;
#endif

#if BUILDFLAG(ENABLE_NATIVE_NOTIFICATIONS)
extern const base::Feature kNativeNotifications;
#endif

extern const base::Feature kOfflinePageDownloadSuggestionsFeature;

#if !defined(OS_ANDROID) && !defined(OS_IOS)
extern const base::Feature kOneGoogleBarOnLocalNtp;
#endif

extern const base::Feature kPermissionsBlacklist;

#if defined(OS_WIN)
extern const base::Feature kDisablePostScriptPrinting;
#endif

#if BUILDFLAG(ENABLE_PLUGINS)
extern const base::Feature kPreferHtmlOverPlugins;
#endif

extern const base::Feature kPrefService;

#if defined(OS_CHROMEOS)
extern const base::Feature kPreloadLockScreen;
#endif

#if BUILDFLAG(ENABLE_PRINT_PREVIEW) && !defined(OS_WIN) && !defined(OS_MACOSX)
extern const base::Feature kPrintPdfAsImage;
#endif

extern const base::Feature kPushMessagingBackgroundMode;

#if defined(OS_CHROMEOS)
extern const base::Feature kRuntimeMemoryLeakDetector;
#endif  // defined(OS_CHROMEOS)

#if BUILDFLAG(ENABLE_PLUGINS)
extern const base::Feature kRunAllFlashInAllowMode;
#endif

extern const base::Feature kSafeSearchUrlReporting;

extern const base::Feature kSimplifiedFullscreenUI;

extern const base::Feature kSiteDetails;

#if defined(SYZYASAN)
extern const base::Feature kSyzyasanDeferredFree;
#endif

extern const base::Feature kTabsInCbd;

extern const base::Feature kUseGoogleLocalNtp;

extern const base::Feature kUseGroupedPermissionInfobars;

extern const base::Feature kUsePermissionManagerForMediaRequests;

#if defined(OS_CHROMEOS)
extern const base::Feature kOptInImeMenu;

extern const base::Feature kQuickUnlockPin;

extern const base::Feature kQuickUnlockPinSignin;

extern const base::Feature kQuickUnlockFingerprint;

extern const base::Feature kEHVInputOnImeMenu;

extern const base::Feature kCrosCompUpdates;

extern const base::Feature kCrOSComponent;
#endif  // defined(OS_CHROMEOS)

extern const base::Feature kPageLoadMetricsMojofication;

bool PrefServiceEnabled();

// DON'T ADD RANDOM STUFF HERE. Put it in the main section above in
// alphabetical order, or in one of the ifdefs (also in order in each section).

}  // namespace features

#endif  // CHROME_COMMON_CHROME_FEATURES_H_
