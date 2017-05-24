// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/update_client/protocol_builder.h"

#include <stdint.h>

#include "base/guid.h"
#include "base/logging.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/sys_info.h"
#include "build/build_config.h"
#include "components/update_client/component.h"
#include "components/update_client/configurator.h"
#include "components/update_client/persisted_data.h"
#include "components/update_client/protocol_parser.h"
#include "components/update_client/update_query_params.h"
#include "components/update_client/updater_state.h"
#include "components/update_client/utils.h"

#if defined(OS_WIN)
#include "base/win/windows_version.h"
#endif

namespace update_client {

namespace {

// Returns a sanitized version of the brand or an empty string otherwise.
std::string SanitizeBrand(const std::string& brand) {
  return IsValidBrand(brand) ? brand : std::string("");
}

// Filters invalid attributes from |installer_attributes|.
InstallerAttributes SanitizeInstallerAttributes(
    const InstallerAttributes& installer_attributes) {
  InstallerAttributes sanitized_attrs;
  for (const auto& attr : installer_attributes) {
    if (IsValidInstallerAttribute(attr))
      sanitized_attrs.insert(attr);
  }
  return sanitized_attrs;
}

// Returns the amount of physical memory in GB, rounded to the nearest GB.
int GetPhysicalMemoryGB() {
  const double kOneGB = 1024 * 1024 * 1024;
  const int64_t phys_mem = base::SysInfo::AmountOfPhysicalMemory();
  return static_cast<int>(std::floor(0.5 + phys_mem / kOneGB));
}

std::string GetOSVersion() {
#if defined(OS_WIN)
  const auto ver = base::win::OSInfo::GetInstance()->version_number();
  return base::StringPrintf("%d.%d.%d.%d", ver.major, ver.minor, ver.build,
                            ver.patch);
#else
  return base::SysInfo().OperatingSystemVersion();
#endif
}

std::string GetServicePack() {
#if defined(OS_WIN)
  return base::win::OSInfo::GetInstance()->service_pack_str();
#else
  return std::string();
#endif
}

// Returns a string literal corresponding to the value of the downloader |d|.
const char* DownloaderToString(CrxDownloader::DownloadMetrics::Downloader d) {
  switch (d) {
    case CrxDownloader::DownloadMetrics::kUrlFetcher:
      return "direct";
    case CrxDownloader::DownloadMetrics::kBits:
      return "bits";
    default:
      return "unknown";
  }
}

}  // namespace

std::string BuildDownloadCompleteEventElement(
    const CrxDownloader::DownloadMetrics& metrics) {
  using base::StringAppendF;

  std::string event("<event eventtype=\"14\"");
  StringAppendF(&event, " eventresult=\"%d\"", metrics.error == 0);
  StringAppendF(&event, " downloader=\"%s\"",
                DownloaderToString(metrics.downloader));
  if (metrics.error) {
    StringAppendF(&event, " errorcode=\"%d\"", metrics.error);
  }
  StringAppendF(&event, " url=\"%s\"", metrics.url.spec().c_str());

  // -1 means that the  byte counts are not known.
  if (metrics.downloaded_bytes != -1) {
    StringAppendF(&event, " downloaded=\"%s\"",
                  base::Int64ToString(metrics.downloaded_bytes).c_str());
  }
  if (metrics.total_bytes != -1) {
    StringAppendF(&event, " total=\"%s\"",
                  base::Int64ToString(metrics.total_bytes).c_str());
  }

  if (metrics.download_time_ms) {
    StringAppendF(&event, " download_time_ms=\"%s\"",
                  base::Uint64ToString(metrics.download_time_ms).c_str());
  }
  StringAppendF(&event, "/>");
  return event;
}

std::string BuildUpdateCompleteEventElement(const Component& component) {
  DCHECK(component.state() == ComponentState::kUpdateError ||
         component.state() == ComponentState::kUpdated);

  using base::StringAppendF;

  std::string event("<event eventtype=\"3\"");
  const int event_result = component.state() == ComponentState::kUpdated;
  StringAppendF(&event, " eventresult=\"%d\"", event_result);
  if (component.error_category())
    StringAppendF(&event, " errorcat=\"%d\"", component.error_category());
  if (component.error_code())
    StringAppendF(&event, " errorcode=\"%d\"", component.error_code());
  if (component.extra_code1())
    StringAppendF(&event, " extracode1=\"%d\"", component.extra_code1());
  if (HasDiffUpdate(component))
    StringAppendF(&event, " diffresult=\"%d\"",
                  !component.diff_update_failed());
  if (component.diff_error_category()) {
    StringAppendF(&event, " differrorcat=\"%d\"",
                  component.diff_error_category());
  }
  if (component.diff_error_code())
    StringAppendF(&event, " differrorcode=\"%d\"", component.diff_error_code());
  if (component.diff_extra_code1()) {
    StringAppendF(&event, " diffextracode1=\"%d\"",
                  component.diff_extra_code1());
  }
  if (!component.previous_fp().empty())
    StringAppendF(&event, " previousfp=\"%s\"",
                  component.previous_fp().c_str());
  if (!component.next_fp().empty())
    StringAppendF(&event, " nextfp=\"%s\"", component.next_fp().c_str());
  StringAppendF(&event, "/>");
  return event;
}

std::string BuildUninstalledEventElement(const Component& component) {
  DCHECK(component.state() == ComponentState::kUninstalled);

  using base::StringAppendF;

  std::string event("<event eventtype=\"4\" eventresult=\"1\"");
  if (component.extra_code1())
    StringAppendF(&event, " extracode1=\"%d\"", component.extra_code1());
  StringAppendF(&event, "/>");
  return event;
}

std::string BuildProtocolRequest(
    const std::string& prod_id,
    const std::string& browser_version,
    const std::string& channel,
    const std::string& lang,
    const std::string& os_long_name,
    const std::string& download_preference,
    const std::string& request_body,
    const std::string& additional_attributes,
    const std::unique_ptr<UpdaterState::Attributes>& updater_state_attributes) {
  std::string request = base::StringPrintf(
      "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
      "<request protocol=\"%s\" ",
      kProtocolVersion);

  if (!additional_attributes.empty())
    base::StringAppendF(&request, "%s ", additional_attributes.c_str());

  // Chrome version and platform information.
  base::StringAppendF(
      &request,
      "version=\"%s-%s\" prodversion=\"%s\" "
      "requestid=\"{%s}\" lang=\"%s\" updaterchannel=\"%s\" prodchannel=\"%s\" "
      "os=\"%s\" arch=\"%s\" nacl_arch=\"%s\"",
      prod_id.c_str(),  // "version" is prefixed by prod_id.
      browser_version.c_str(),
      browser_version.c_str(),            // "prodversion"
      base::GenerateGUID().c_str(),       // "requestid"
      lang.c_str(),                       // "lang"
      channel.c_str(),                    // "updaterchannel"
      channel.c_str(),                    // "prodchannel"
      UpdateQueryParams::GetOS(),         // "os"
      UpdateQueryParams::GetArch(),       // "arch"
      UpdateQueryParams::GetNaclArch());  // "nacl_arch"
#if defined(OS_WIN)
  const bool is_wow64(base::win::OSInfo::GetInstance()->wow64_status() ==
                      base::win::OSInfo::WOW64_ENABLED);
  if (is_wow64)
    base::StringAppendF(&request, " wow64=\"1\"");
#endif
  if (!download_preference.empty())
    base::StringAppendF(&request, " dlpref=\"%s\"",
                        download_preference.c_str());
  if (updater_state_attributes &&
      updater_state_attributes->count(UpdaterState::kIsEnterpriseManaged)) {
    base::StringAppendF(
        &request, " %s=\"%s\"",  // domainjoined
        UpdaterState::kIsEnterpriseManaged,
        (*updater_state_attributes)[UpdaterState::kIsEnterpriseManaged]
            .c_str());
  }
  base::StringAppendF(&request, ">");

  // HW platform information.
  base::StringAppendF(&request, "<hw physmemory=\"%d\"/>",
                      GetPhysicalMemoryGB());  // "physmem" in GB.

  // OS version and platform information.
  const std::string os_version = GetOSVersion();
  const std::string os_sp = GetServicePack();
  base::StringAppendF(
      &request, "<os platform=\"%s\" arch=\"%s\"",
      os_long_name.c_str(),                                    // "platform"
      base::SysInfo().OperatingSystemArchitecture().c_str());  // "arch"
  if (!os_version.empty())
    base::StringAppendF(&request, " version=\"%s\"", os_version.c_str());
  if (!os_sp.empty())
    base::StringAppendF(&request, " sp=\"%s\"", os_sp.c_str());
  base::StringAppendF(&request, "/>");

#if defined(GOOGLE_CHROME_BUILD)
  // Updater state.
  if (updater_state_attributes) {
    base::StringAppendF(&request, "<updater");
    for (const auto& attr : *updater_state_attributes) {
      if (attr.first != UpdaterState::kIsEnterpriseManaged) {
        base::StringAppendF(&request, " %s=\"%s\"", attr.first.c_str(),
                            attr.second.c_str());
      }
    }
    base::StringAppendF(&request, "/>");
  }
#endif  // GOOGLE_CHROME_BUILD

  // The actual payload of the request.
  base::StringAppendF(&request, "%s</request>", request_body.c_str());

  return request;
}

std::string BuildUpdateCheckRequest(
    const Configurator& config,
    const std::vector<std::string>& ids_checked,
    const IdToComponentPtrMap& components,
    PersistedData* metadata,
    const std::string& additional_attributes,
    bool enabled_component_updates,
    const std::unique_ptr<UpdaterState::Attributes>& updater_state_attributes) {
  const std::string brand(SanitizeBrand(config.GetBrand()));
  std::string app_elements;
  for (const auto& id : ids_checked) {
    DCHECK_EQ(1u, components.count(id));
    const Component& component = *components.at(id);

    const update_client::InstallerAttributes installer_attributes(
        SanitizeInstallerAttributes(
            component.crx_component().installer_attributes));
    std::string app("<app ");
    base::StringAppendF(&app, "appid=\"%s\" version=\"%s\"",
                        component.id().c_str(),
                        component.crx_component().version.GetString().c_str());
    if (!brand.empty())
      base::StringAppendF(&app, " brand=\"%s\"", brand.c_str());
    if (component.on_demand())
      base::StringAppendF(&app, " installsource=\"ondemand\"");
    for (const auto& attr : installer_attributes) {
      base::StringAppendF(&app, " %s=\"%s\"", attr.first.c_str(),
                          attr.second.c_str());
    }
    const std::string cohort = metadata->GetCohort(component.id());
    const std::string cohort_name = metadata->GetCohortName(component.id());
    const std::string cohort_hint = metadata->GetCohortHint(component.id());
    if (!cohort.empty())
      base::StringAppendF(&app, " cohort=\"%s\"", cohort.c_str());
    if (!cohort_name.empty())
      base::StringAppendF(&app, " cohortname=\"%s\"", cohort_name.c_str());
    if (!cohort_hint.empty())
      base::StringAppendF(&app, " cohorthint=\"%s\"", cohort_hint.c_str());
    base::StringAppendF(&app, ">");

    base::StringAppendF(&app, "<updatecheck");
    if (component.crx_component()
            .supports_group_policy_enable_component_updates &&
        !enabled_component_updates) {
      base::StringAppendF(&app, " updatedisabled=\"true\"");
    }
    base::StringAppendF(&app, "/>");

    base::StringAppendF(&app, "<ping rd=\"%d\" ping_freshness=\"%s\"/>",
                        metadata->GetDateLastRollCall(component.id()),
                        metadata->GetPingFreshness(component.id()).c_str());
    if (!component.crx_component().fingerprint.empty()) {
      base::StringAppendF(&app,
                          "<packages>"
                          "<package fp=\"%s\"/>"
                          "</packages>",
                          component.crx_component().fingerprint.c_str());
    }
    base::StringAppendF(&app, "</app>");
    app_elements.append(app);
    VLOG(1) << "Appending to update request: " << app;
  }

  // Include the updater state in the update check request.
  return BuildProtocolRequest(
      config.GetProdId(), config.GetBrowserVersion().GetString(),
      config.GetChannel(), config.GetLang(), config.GetOSLongName(),
      config.GetDownloadPreference(), app_elements, additional_attributes,
      updater_state_attributes);
}

std::string BuildEventPingRequest(const Configurator& config,
                                  const Component& component) {
  DCHECK(component.state() == ComponentState::kUpdateError ||
         component.state() == ComponentState::kUpdated ||
         component.state() == ComponentState::kUninstalled);

  const char app_element_format[] =
      "<app appid=\"%s\" version=\"%s\" nextversion=\"%s\">"
      "%s"
      "</app>";

  const std::string app_element(base::StringPrintf(
      app_element_format,
      component.id().c_str(),                              // "appid"
      component.previous_version().GetString().c_str(),    // "version"
      component.next_version().GetString().c_str(),        // "nextversion"
      base::JoinString(component.events(), "").c_str()));  // events

  // The ping request does not include any updater state.
  return BuildProtocolRequest(
      config.GetProdId(), config.GetBrowserVersion().GetString(),
      config.GetChannel(), config.GetLang(), config.GetOSLongName(),
      config.GetDownloadPreference(), app_element, "", nullptr);
}

}  // namespace update_client
