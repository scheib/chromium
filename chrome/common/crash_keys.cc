// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/common/crash_keys.h"

#include "base/base_switches.h"
#include "base/command_line.h"
#include "base/format_macros.h"
#include "base/logging.h"
#include "base/macros.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "build/build_config.h"
#include "chrome/common/chrome_switches.h"
#include "components/flags_ui/flags_ui_switches.h"
#include "content/public/common/content_switches.h"
#include "gpu/config/gpu_crash_keys.h"

#if defined(OS_CHROMEOS)
#include "chrome/common/chrome_switches.h"
#include "gpu/command_buffer/service/gpu_switches.h"
#include "ui/gl/gl_switches.h"
#endif

namespace crash_keys {

const char kActiveURL[] = "url-chunk";

const char kFontKeyName[] = "font_key_name";

const char kExtensionID[] = "extension-%" PRIuS;
const char kNumExtensionsCount[] = "num-extensions";

const char kShutdownType[] = "shutdown-type";
const char kBrowserUnpinTrace[] = "browser-unpin-trace";

#if defined(OS_WIN)
const char kHungRendererOutstandingAckCount[] = "hung-outstanding-acks";
const char kHungRendererOutstandingEventType[] = "hung-outstanding-event-type";
const char kHungRendererLastEventType[] = "hung-last-event-type";
const char kHungRendererReason[] = "hung-reason";

const char kThirdPartyModulesLoaded[] = "third-party-modules-loaded";
const char kThirdPartyModulesNotLoaded[] = "third-party-modules-not-loaded";

const char kIsEnterpriseManaged[] = "is-enterprise-managed";

// Registry values used to determine Chrome's update channel; see
// https://crbug.com/579504.
const char kApValue[] = "ap";
const char kCohortName[] = "cohort-name";
#endif

const char kInputEventFilterSendFailure[] = "input-event-filter-send-failure";

const char kPrinterInfo[] = "prn-info-%" PRIuS;

#if defined(OS_CHROMEOS)
const char kNumberOfUsers[] = "num-users";
#endif

#if defined(OS_MACOSX)
namespace mac {

const char kFirstNSException[] = "firstexception";
const char kFirstNSExceptionTrace[] = "firstexception_bt";

const char kLastNSException[] = "lastexception";
const char kLastNSExceptionTrace[] = "lastexception_bt";

const char kNSException[] = "nsexception";
const char kNSExceptionTrace[] = "nsexception_bt";

const char kSendAction[] = "sendaction";

const char kNSEvent[] = "nsevent";

const char kToolbarNibInfo[] = "toolbar-nib-info";

}  // namespace mac
#endif

const char kViewCount[] = "view-count";

const char kZeroEncodeDetails[] = "zero-encode-details";

size_t RegisterChromeCrashKeys() {
  // The following keys may be chunked by the underlying crash logging system,
  // but ultimately constitute a single key-value pair.
  //
  // If you're adding keys here, please also add them to the following lists:
  // chrome/app/chrome_crash_reporter_client_win.cc::RegisterCrashKeysHelper(),
  // android_webview/common/crash_reporter/crash_keys.cc::
  //     RegisterWebViewCrashKeys(),
  // chromecast/crash/cast_crash_keys.cc::RegisterCastCrashKeys().
  base::debug::CrashKey fixed_keys[] = {
#if defined(OS_MACOSX) || defined(OS_WIN)
    {kMetricsClientId, kSmallSize},
#else
    {kClientId, kSmallSize},
#endif
    {kChannel, kSmallSize},
    {kActiveURL, kLargeSize},
    {kNumVariations, kSmallSize},
    {kVariations, kHugeSize},
    {kNumExtensionsCount, kSmallSize},
    {kShutdownType, kSmallSize},
    {kBrowserUnpinTrace, kMediumSize},
#if defined(OS_WIN)
    {kApValue, kSmallSize},
    {kCohortName, kSmallSize},
#endif  // defined(OS_WIN)
#if !defined(OS_ANDROID)
    {gpu::crash_keys::kGPUVendorID, kSmallSize},
    {gpu::crash_keys::kGPUDeviceID, kSmallSize},
#endif
    {gpu::crash_keys::kGPUDriverVersion, kSmallSize},
    {gpu::crash_keys::kGPUPixelShaderVersion, kSmallSize},
    {gpu::crash_keys::kGPUVertexShaderVersion, kSmallSize},
#if defined(OS_MACOSX)
    {gpu::crash_keys::kGPUGLVersion, kSmallSize},
#elif defined(OS_POSIX)
    {gpu::crash_keys::kGPUVendor, kSmallSize},
    {gpu::crash_keys::kGPURenderer, kSmallSize},
#endif

    // content/:
    {"bad_message_reason", kSmallSize},
    {"discardable-memory-allocated", kSmallSize},
    {"discardable-memory-free", kSmallSize},
    {kFontKeyName, kSmallSize},
    {"mojo-message-error", kMediumSize},
    {"ppapi_path", kMediumSize},
    {"subresource_url", kLargeSize},
    {"total-discardable-memory-allocated", kSmallSize},
#if defined(OS_WIN)
    {kHungRendererOutstandingAckCount, kSmallSize},
    {kHungRendererOutstandingEventType, kSmallSize},
    {kHungRendererLastEventType, kSmallSize},
    {kHungRendererReason, kSmallSize},
    {kThirdPartyModulesLoaded, kSmallSize},
    {kThirdPartyModulesNotLoaded, kSmallSize},
    {kIsEnterpriseManaged, kSmallSize},
#endif
    {kInputEventFilterSendFailure, kSmallSize},
#if defined(OS_CHROMEOS)
    {kNumberOfUsers, kSmallSize},
    // Temporary for https://crbug.com/629521
    {"mmap_params", kSmallSize},
    {"buffer_size", kSmallSize},
    {"errno", kSmallSize},
    {"number_of_fds", kSmallSize},
#endif
#if defined(OS_MACOSX)
    {mac::kFirstNSException, kMediumSize},
    {mac::kFirstNSExceptionTrace, kMediumSize},
    {mac::kLastNSException, kMediumSize},
    {mac::kLastNSExceptionTrace, kMediumSize},
    {mac::kNSException, kMediumSize},
    {mac::kNSExceptionTrace, kMediumSize},
    {mac::kSendAction, kMediumSize},
    {mac::kNSEvent, kMediumSize},
    {mac::kToolbarNibInfo, kMediumSize},
    {mac::kZombie, kMediumSize},
    {mac::kZombieTrace, kMediumSize},
    // content/:
    {"channel_error_bt", kMediumSize},
    {"remove_route_bt", kMediumSize},
    {"rwhvm_window", kMediumSize},
    // media/:
#endif
    {kBug464926CrashKey, kSmallSize},
    {kViewCount, kSmallSize},

    // media/:
    {kZeroEncodeDetails, kSmallSize},

    // gin/:
    {"v8-ignition", kSmallSize},

    // sandbox/:
#if defined(OS_LINUX)
    {"seccomp-sigsys", kMediumSize},
#endif

    // Temporary for https://crbug.com/591478.
    {"initrf_parent_proxy_exists", kSmallSize},
    {"initrf_render_view_is_live", kSmallSize},
    {"initrf_parent_is_in_same_site_instance", kSmallSize},
    {"initrf_parent_process_is_live", kSmallSize},
    {"initrf_root_is_in_same_site_instance", kSmallSize},
    {"initrf_root_is_in_same_site_instance_as_parent", kSmallSize},
    {"initrf_root_process_is_live", kSmallSize},
    {"initrf_root_proxy_is_live", kSmallSize},

    // Temporary for https://crbug.com/626802.
    {"newframe_routing_id", kSmallSize},
    {"newframe_proxy_id", kSmallSize},
    {"newframe_opener_id", kSmallSize},
    {"newframe_parent_id", kSmallSize},
    {"newframe_widget_id", kSmallSize},
    {"newframe_widget_hidden", kSmallSize},
    {"newframe_replicated_origin", kSmallSize},
    {"newframe_oopifs_possible", kSmallSize},

    // Temporary for https://crbug.com/612711.
    {"aci_wrong_sp_extension_id", kSmallSize},

    // Temporary for https://crbug.com/668633.
    {"swdh_set_hosted_version_worker_pid", crash_keys::kSmallSize},
    {"swdh_set_hosted_version_host_pid", crash_keys::kSmallSize},
    {"swdh_set_hosted_version_is_new_process", crash_keys::kSmallSize},
    {"swdh_set_hosted_version_restart_count", crash_keys::kSmallSize},

    // Temporary for https://crbug.com/697745.
    {"engine_params", crash_keys::kMediumSize},
    {"engine1_params", crash_keys::kMediumSize},
    {"engine2_params", crash_keys::kMediumSize},

    // Temporary for http://crbug.com/703649.
    {"field_trial_shmem_create_error", crash_keys::kSmallSize},
    {"field_trial_shmem_map_error", crash_keys::kSmallSize},
  };

  // This dynamic set of keys is used for sets of key value pairs when gathering
  // a collection of data, like command line switches or extension IDs.
  std::vector<base::debug::CrashKey> keys(
      fixed_keys, fixed_keys + arraysize(fixed_keys));

  crash_keys::GetCrashKeysForCommandLineSwitches(&keys);

  // Register the extension IDs.
  {
    static char formatted_keys[kExtensionIDMaxCount][sizeof(kExtensionID) + 1] =
        {{ 0 }};
    const size_t formatted_key_len = sizeof(formatted_keys[0]);
    for (size_t i = 0; i < kExtensionIDMaxCount; ++i) {
      int n = base::snprintf(
          formatted_keys[i], formatted_key_len, kExtensionID, i + 1);
      DCHECK_GT(n, 0);
      base::debug::CrashKey crash_key = { formatted_keys[i], kSmallSize };
      keys.push_back(crash_key);
    }
  }

  // Register the printer info.
  {
    static char formatted_keys[kPrinterInfoCount][sizeof(kPrinterInfo) + 1] =
        {{ 0 }};
    const size_t formatted_key_len = sizeof(formatted_keys[0]);
    for (size_t i = 0; i < kPrinterInfoCount; ++i) {
      // Key names are 1-indexed.
      int n = base::snprintf(
          formatted_keys[i], formatted_key_len, kPrinterInfo, i + 1);
      DCHECK_GT(n, 0);
      base::debug::CrashKey crash_key = { formatted_keys[i], kSmallSize };
      keys.push_back(crash_key);
    }
  }

  return base::debug::InitCrashKeys(&keys.at(0), keys.size(), kChunkMaxLength);
}

static bool IsBoringSwitch(const std::string& flag) {
  static const char* const kIgnoreSwitches[] = {
    switches::kEnableLogging,
    switches::kFlagSwitchesBegin,
    switches::kFlagSwitchesEnd,
    switches::kLoggingLevel,
    switches::kProcessType,
    switches::kV,
    switches::kVModule,
#if defined(OS_WIN)
    switches::kForceFieldTrials,
#elif defined(OS_MACOSX)
    switches::kMetricsClientID,
#elif defined(OS_CHROMEOS)
    switches::kPpapiFlashArgs,
    switches::kPpapiFlashPath,
    switches::kRegisterPepperPlugins,
    switches::kUIPrioritizeInGpuProcess,
    switches::kUseGL,
    switches::kUserDataDir,
    // Cros/CC flags are specified as raw strings to avoid dependency.
    "child-wallpaper-large",
    "child-wallpaper-small",
    "default-wallpaper-large",
    "default-wallpaper-small",
    "guest-wallpaper-large",
    "guest-wallpaper-small",
    "enterprise-enable-forced-re-enrollment",
    "enterprise-enrollment-initial-modulus",
    "enterprise-enrollment-modulus-limit",
    "login-profile",
    "login-user",
    "max-unused-resource-memory-usage-percentage",
    "termination-message-file",
    "use-cras",
#endif
  };

#if defined(OS_WIN)
  // Just about everything has this, don't bother.
  if (base::StartsWith(flag, "/prefetch:", base::CompareCase::SENSITIVE))
    return true;
#endif

  if (!base::StartsWith(flag, "--", base::CompareCase::SENSITIVE))
    return false;
  size_t end = flag.find("=");
  size_t len = (end == std::string::npos) ? flag.length() - 2 : end - 2;
  for (size_t i = 0; i < arraysize(kIgnoreSwitches); ++i) {
    if (flag.compare(2, len, kIgnoreSwitches[i]) == 0)
      return true;
  }
  return false;
}

void SetCrashKeysFromCommandLine(const base::CommandLine& command_line) {
  return SetSwitchesFromCommandLine(command_line, &IsBoringSwitch);
}

void SetActiveExtensions(const std::set<std::string>& extensions) {
  base::debug::SetCrashKeyValue(kNumExtensionsCount,
      base::StringPrintf("%" PRIuS, extensions.size()));

  std::set<std::string>::const_iterator it = extensions.begin();
  for (size_t i = 0; i < kExtensionIDMaxCount; ++i) {
    std::string key = base::StringPrintf(kExtensionID, i + 1);
    if (it == extensions.end()) {
      base::debug::ClearCrashKey(key);
    } else {
      base::debug::SetCrashKeyValue(key, *it);
      ++it;
    }
  }
}

ScopedPrinterInfo::ScopedPrinterInfo(const base::StringPiece& data) {
  std::vector<std::string> info = base::SplitString(
      data.as_string(), ";", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);
  for (size_t i = 0; i < kPrinterInfoCount; ++i) {
    std::string key = base::StringPrintf(kPrinterInfo, i + 1);
    std::string value;
    if (i < info.size())
      value = info[i];
    base::debug::SetCrashKeyValue(key, value);
  }
}

ScopedPrinterInfo::~ScopedPrinterInfo() {
  for (size_t i = 0; i < kPrinterInfoCount; ++i) {
    std::string key = base::StringPrintf(kPrinterInfo, i + 1);
    base::debug::ClearCrashKey(key);
  }
}

}  // namespace crash_keys
