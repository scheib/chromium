// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/page_info/page_info_ui.h"

#include <utility>

#include "base/command_line.h"
#include "base/macros.h"
#include "chrome/browser/content_settings/host_content_settings_map_factory.h"
#include "chrome/browser/permissions/permission_manager.h"
#include "chrome/browser/permissions/permission_result.h"
#include "chrome/browser/permissions/permission_util.h"
#include "chrome/browser/plugins/plugin_utils.h"
#include "chrome/browser/plugins/plugins_field_trial.h"
#include "chrome/common/chrome_features.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/grit/chromium_strings.h"
#include "chrome/grit/generated_resources.h"
#include "chrome/grit/theme_resources.h"
#include "components/strings/grit/components_chromium_strings.h"
#include "components/strings/grit/components_strings.h"
#include "ppapi/features/features.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/gfx/image/image.h"
#include "url/gurl.h"

#if defined(OS_ANDROID)
#include "chrome/browser/android/android_theme_resources.h"
#else
#include "chrome/app/vector_icons/vector_icons.h"
#include "ui/gfx/color_palette.h"
#include "ui/gfx/paint_vector_icon.h"
#endif

namespace {

const int kInvalidResourceID = -1;

// The resource IDs for the strings that are displayed on the permissions
// button if the permission setting is managed by policy.
const int kPermissionButtonTextIDPolicyManaged[] = {
    kInvalidResourceID,
    IDS_PAGE_INFO_PERMISSION_ALLOWED_BY_POLICY,
    IDS_PAGE_INFO_PERMISSION_BLOCKED_BY_POLICY,
    IDS_PAGE_INFO_PERMISSION_ASK_BY_POLICY,
    kInvalidResourceID,
    kInvalidResourceID};
static_assert(arraysize(kPermissionButtonTextIDPolicyManaged) ==
                  CONTENT_SETTING_NUM_SETTINGS,
              "kPermissionButtonTextIDPolicyManaged array size is incorrect");

// The resource IDs for the strings that are displayed on the permissions
// button if the permission setting is managed by an extension.
const int kPermissionButtonTextIDExtensionManaged[] = {
    kInvalidResourceID,
    IDS_PAGE_INFO_PERMISSION_ALLOWED_BY_EXTENSION,
    IDS_PAGE_INFO_PERMISSION_BLOCKED_BY_EXTENSION,
    IDS_PAGE_INFO_PERMISSION_ASK_BY_EXTENSION,
    kInvalidResourceID,
    kInvalidResourceID};
static_assert(arraysize(kPermissionButtonTextIDExtensionManaged) ==
                  CONTENT_SETTING_NUM_SETTINGS,
              "kPermissionButtonTextIDExtensionManaged array size is "
              "incorrect");

// The resource IDs for the strings that are displayed on the permissions
// button if the permission setting is managed by the user.
const int kPermissionButtonTextIDUserManaged[] = {
    kInvalidResourceID,
    IDS_PAGE_INFO_BUTTON_TEXT_ALLOWED_BY_USER,
    IDS_PAGE_INFO_BUTTON_TEXT_BLOCKED_BY_USER,
    IDS_PAGE_INFO_BUTTON_TEXT_ASK_BY_USER,
    kInvalidResourceID,
    IDS_PAGE_INFO_BUTTON_TEXT_DETECT_IMPORTANT_CONTENT_BY_USER};
static_assert(arraysize(kPermissionButtonTextIDUserManaged) ==
                  CONTENT_SETTING_NUM_SETTINGS,
              "kPermissionButtonTextIDUserManaged array size is incorrect");

// The resource IDs for the strings that are displayed on the permissions
// button if the permission setting is the global default setting.
const int kPermissionButtonTextIDDefaultSetting[] = {
    kInvalidResourceID,
    IDS_PAGE_INFO_BUTTON_TEXT_ALLOWED_BY_DEFAULT,
    IDS_PAGE_INFO_BUTTON_TEXT_BLOCKED_BY_DEFAULT,
    IDS_PAGE_INFO_BUTTON_TEXT_ASK_BY_DEFAULT,
    kInvalidResourceID,
    IDS_PAGE_INFO_BUTTON_TEXT_DETECT_IMPORTANT_CONTENT_BY_DEFAULT};
static_assert(arraysize(kPermissionButtonTextIDDefaultSetting) ==
                  CONTENT_SETTING_NUM_SETTINGS,
              "kPermissionButtonTextIDDefaultSetting array size is incorrect");

struct PermissionsUIInfo {
  ContentSettingsType type;
  int string_id;
  int blocked_icon_id;
  int allowed_icon_id;
};

const PermissionsUIInfo kPermissionsUIInfo[] = {
    {CONTENT_SETTINGS_TYPE_COOKIES, 0, IDR_BLOCKED_COOKIES,
     IDR_ACCESSED_COOKIES},
    {CONTENT_SETTINGS_TYPE_IMAGES, IDS_PAGE_INFO_TYPE_IMAGES,
     IDR_BLOCKED_IMAGES, IDR_ALLOWED_IMAGES},
    {CONTENT_SETTINGS_TYPE_JAVASCRIPT, IDS_PAGE_INFO_TYPE_JAVASCRIPT,
     IDR_BLOCKED_JAVASCRIPT, IDR_ALLOWED_JAVASCRIPT},
    {CONTENT_SETTINGS_TYPE_POPUPS, IDS_PAGE_INFO_TYPE_POPUPS,
     IDR_BLOCKED_POPUPS, IDR_ALLOWED_POPUPS},
#if BUILDFLAG(ENABLE_PLUGINS)
    {CONTENT_SETTINGS_TYPE_PLUGINS, IDS_PAGE_INFO_TYPE_FLASH,
     IDR_BLOCKED_PLUGINS, IDR_ALLOWED_PLUGINS},
#endif
    {CONTENT_SETTINGS_TYPE_GEOLOCATION, IDS_PAGE_INFO_TYPE_LOCATION,
     IDR_BLOCKED_LOCATION, IDR_ALLOWED_LOCATION},
    {CONTENT_SETTINGS_TYPE_NOTIFICATIONS, IDS_PAGE_INFO_TYPE_NOTIFICATIONS,
     IDR_BLOCKED_NOTIFICATION, IDR_ALLOWED_NOTIFICATION},
    {CONTENT_SETTINGS_TYPE_MEDIASTREAM_MIC, IDS_PAGE_INFO_TYPE_MIC,
     IDR_BLOCKED_MIC, IDR_ALLOWED_MIC},
    {CONTENT_SETTINGS_TYPE_MEDIASTREAM_CAMERA, IDS_PAGE_INFO_TYPE_CAMERA,
     IDR_BLOCKED_CAMERA, IDR_ALLOWED_CAMERA},
    {CONTENT_SETTINGS_TYPE_AUTOMATIC_DOWNLOADS,
     IDS_AUTOMATIC_DOWNLOADS_TAB_LABEL, IDR_BLOCKED_DOWNLOADS,
     IDR_ALLOWED_DOWNLOADS},
    {CONTENT_SETTINGS_TYPE_MIDI_SYSEX, IDS_PAGE_INFO_TYPE_MIDI_SYSEX,
     IDR_BLOCKED_MIDI_SYSEX, IDR_ALLOWED_MIDI_SYSEX},
    {CONTENT_SETTINGS_TYPE_BACKGROUND_SYNC, IDS_PAGE_INFO_TYPE_BACKGROUND_SYNC,
     IDR_BLOCKED_BACKGROUND_SYNC, IDR_ALLOWED_BACKGROUND_SYNC},
    // Autoplay is Android-only at the moment, and the Page Info popup on
    // Android ignores these block/allow icon pairs, so we can specify 0 there.
    {CONTENT_SETTINGS_TYPE_AUTOPLAY, IDS_PAGE_INFO_TYPE_AUTOPLAY, 0, 0},
    {CONTENT_SETTINGS_TYPE_SUBRESOURCE_FILTER, IDS_SUBRESOURCE_FILTER_HEADER,
     IDR_ALLOWED_SUBRESOURCE_FILTER, IDR_BLOCKED_SUBRESOURCE_FILTER},
};

std::unique_ptr<PageInfoUI::SecurityDescription> CreateSecurityDescription(
    int summary_id,
    int details_id) {
  std::unique_ptr<PageInfoUI::SecurityDescription> security_description(
      new PageInfoUI::SecurityDescription());
  security_description->summary = l10n_util::GetStringUTF16(summary_id);
  security_description->details = l10n_util::GetStringUTF16(details_id);
  return security_description;
}

// Gets the actual setting for a ContentSettingType, taking into account what
// the default setting value is and whether Html5ByDefault is enabled.
ContentSetting GetEffectiveSetting(Profile* profile,
                                   ContentSettingsType type,
                                   ContentSetting setting,
                                   ContentSetting default_setting) {
  ContentSetting effective_setting = setting;
  if (effective_setting == CONTENT_SETTING_DEFAULT)
    effective_setting = default_setting;

#if BUILDFLAG(ENABLE_PLUGINS)
  HostContentSettingsMap* host_content_settings_map =
      HostContentSettingsMapFactory::GetForProfile(profile);
  effective_setting = PluginsFieldTrial::EffectiveContentSetting(
      host_content_settings_map, type, effective_setting);

  // Display the UI string for ASK instead of DETECT for HTML5 by Default.
  // TODO(tommycli): Once HTML5 by Default is shipped and the feature flag
  // is removed, just migrate the actual content setting to ASK.
  if (PluginUtils::ShouldPreferHtmlOverPlugins(host_content_settings_map) &&
      effective_setting == CONTENT_SETTING_DETECT_IMPORTANT_CONTENT) {
    effective_setting = CONTENT_SETTING_ASK;
  }
#endif
  return effective_setting;
}

}  // namespace

PageInfoUI::CookieInfo::CookieInfo() : allowed(-1), blocked(-1) {}

PageInfoUI::PermissionInfo::PermissionInfo()
    : type(CONTENT_SETTINGS_TYPE_DEFAULT),
      setting(CONTENT_SETTING_DEFAULT),
      default_setting(CONTENT_SETTING_DEFAULT),
      source(content_settings::SETTING_SOURCE_NONE),
      is_incognito(false) {}

PageInfoUI::ChosenObjectInfo::ChosenObjectInfo(
    const PageInfo::ChooserUIInfo& ui_info,
    std::unique_ptr<base::DictionaryValue> object)
    : ui_info(ui_info), object(std::move(object)) {}

PageInfoUI::ChosenObjectInfo::~ChosenObjectInfo() {}

PageInfoUI::IdentityInfo::IdentityInfo()
    : identity_status(PageInfo::SITE_IDENTITY_STATUS_UNKNOWN),
      connection_status(PageInfo::SITE_CONNECTION_STATUS_UNKNOWN),
      show_ssl_decision_revoke_button(false) {}

PageInfoUI::IdentityInfo::~IdentityInfo() {}

std::unique_ptr<PageInfoUI::SecurityDescription>
PageInfoUI::IdentityInfo::GetSecurityDescription() const {
  std::unique_ptr<PageInfoUI::SecurityDescription> security_description(
      new PageInfoUI::SecurityDescription());

  switch (identity_status) {
    case PageInfo::SITE_IDENTITY_STATUS_INTERNAL_PAGE:
#if defined(OS_ANDROID)
      // We provide identical summary and detail strings for Android, which
      // deduplicates them in the UI code.
      return CreateSecurityDescription(IDS_PAGE_INFO_INTERNAL_PAGE,
                                       IDS_PAGE_INFO_INTERNAL_PAGE);
#endif
      // Internal pages on desktop have their own UI implementations which
      // should never call this function.
      NOTREACHED();
    case PageInfo::SITE_IDENTITY_STATUS_CERT:
    case PageInfo::SITE_IDENTITY_STATUS_EV_CERT:
    case PageInfo::SITE_IDENTITY_STATUS_CERT_REVOCATION_UNKNOWN:
    case PageInfo::SITE_IDENTITY_STATUS_ADMIN_PROVIDED_CERT:
      switch (connection_status) {
        case PageInfo::SITE_CONNECTION_STATUS_INSECURE_ACTIVE_SUBRESOURCE:
          return CreateSecurityDescription(IDS_PAGE_INFO_NOT_SECURE_SUMMARY,
                                           IDS_PAGE_INFO_NOT_SECURE_DETAILS);
        case PageInfo::SITE_CONNECTION_STATUS_INSECURE_FORM_ACTION:
          return CreateSecurityDescription(IDS_PAGE_INFO_MIXED_CONTENT_SUMMARY,
                                           IDS_PAGE_INFO_NOT_SECURE_DETAILS);
        case PageInfo::SITE_CONNECTION_STATUS_INSECURE_PASSIVE_SUBRESOURCE:
          return CreateSecurityDescription(IDS_PAGE_INFO_MIXED_CONTENT_SUMMARY,
                                           IDS_PAGE_INFO_MIXED_CONTENT_DETAILS);
        default:
          return CreateSecurityDescription(IDS_PAGE_INFO_SECURE_SUMMARY,
                                           IDS_PAGE_INFO_SECURE_DETAILS);
      }
    case PageInfo::SITE_IDENTITY_STATUS_MALWARE:
      return CreateSecurityDescription(IDS_PAGE_INFO_MALWARE_SUMMARY,
                                       IDS_PAGE_INFO_MALWARE_DETAILS);
    case PageInfo::SITE_IDENTITY_STATUS_SOCIAL_ENGINEERING:
      return CreateSecurityDescription(
          IDS_PAGE_INFO_SOCIAL_ENGINEERING_SUMMARY,
          IDS_PAGE_INFO_SOCIAL_ENGINEERING_DETAILS);
    case PageInfo::SITE_IDENTITY_STATUS_UNWANTED_SOFTWARE:
      return CreateSecurityDescription(IDS_PAGE_INFO_UNWANTED_SOFTWARE_SUMMARY,
                                       IDS_PAGE_INFO_UNWANTED_SOFTWARE_DETAILS);
    case PageInfo::SITE_IDENTITY_STATUS_DEPRECATED_SIGNATURE_ALGORITHM:
    case PageInfo::SITE_IDENTITY_STATUS_UNKNOWN:
    case PageInfo::SITE_IDENTITY_STATUS_NO_CERT:
    default:
      return CreateSecurityDescription(IDS_PAGE_INFO_NOT_SECURE_SUMMARY,
                                       IDS_PAGE_INFO_NOT_SECURE_DETAILS);
  }
}

PageInfoUI::~PageInfoUI() {}

// static
base::string16 PageInfoUI::PermissionTypeToUIString(ContentSettingsType type) {
  for (const PermissionsUIInfo& info : kPermissionsUIInfo) {
    if (info.type == type)
      return l10n_util::GetStringUTF16(info.string_id);
  }
  NOTREACHED();
  return base::string16();
}

// static
base::string16 PageInfoUI::PermissionActionToUIString(
    Profile* profile,
    ContentSettingsType type,
    ContentSetting setting,
    ContentSetting default_setting,
    content_settings::SettingSource source) {
  ContentSetting effective_setting =
      GetEffectiveSetting(profile, type, setting, default_setting);
  const int* button_text_ids = NULL;
  switch (source) {
    case content_settings::SETTING_SOURCE_USER:
      if (setting == CONTENT_SETTING_DEFAULT) {
        button_text_ids = kPermissionButtonTextIDDefaultSetting;
        break;
      }
    // Fallthrough.
    case content_settings::SETTING_SOURCE_POLICY:
    case content_settings::SETTING_SOURCE_EXTENSION:
      button_text_ids = kPermissionButtonTextIDUserManaged;
      break;
    case content_settings::SETTING_SOURCE_WHITELIST:
    case content_settings::SETTING_SOURCE_NONE:
    default:
      NOTREACHED();
      return base::string16();
  }
  // The subresource filter permission uses the user managed strings
  // (i.e. Allow / Block).
  if (type == CONTENT_SETTINGS_TYPE_SUBRESOURCE_FILTER)
    button_text_ids = kPermissionButtonTextIDUserManaged;
  int button_text_id = button_text_ids[effective_setting];
  DCHECK_NE(button_text_id, kInvalidResourceID);
  return l10n_util::GetStringUTF16(button_text_id);
}

// static
int PageInfoUI::GetPermissionIconID(ContentSettingsType type,
                                    ContentSetting setting) {
  bool use_blocked = (setting == CONTENT_SETTING_BLOCK);
  for (const PermissionsUIInfo& info : kPermissionsUIInfo) {
    if (info.type == type)
      return use_blocked ? info.blocked_icon_id : info.allowed_icon_id;
  }
  NOTREACHED();
  return IDR_INFO;
}

// static
base::string16 PageInfoUI::PermissionDecisionReasonToUIString(
    Profile* profile,
    const PageInfoUI::PermissionInfo& permission,
    const GURL& url) {
  ContentSetting effective_setting = GetEffectiveSetting(
      profile, permission.type, permission.setting, permission.default_setting);
  int message_id = kInvalidResourceID;
  switch (permission.source) {
    case content_settings::SettingSource::SETTING_SOURCE_POLICY:
      message_id = kPermissionButtonTextIDPolicyManaged[effective_setting];
      break;
    case content_settings::SettingSource::SETTING_SOURCE_EXTENSION:
      message_id = kPermissionButtonTextIDExtensionManaged[effective_setting];
      break;
    default:
      break;
  }

  if (permission.setting == CONTENT_SETTING_BLOCK &&
      PermissionUtil::IsPermission(permission.type)) {
    PermissionResult permission_result =
        PermissionManager::Get(profile)->GetPermissionStatus(permission.type,
                                                             url, url);
    switch (permission_result.source) {
      case PermissionStatusSource::MULTIPLE_DISMISSALS:
      case PermissionStatusSource::SAFE_BROWSING_BLACKLIST:
        message_id = IDS_PAGE_INFO_PERMISSION_AUTOMATICALLY_BLOCKED;
        break;
      default:
        break;
    }
  }

  if (message_id == kInvalidResourceID)
    return base::string16();
  return l10n_util::GetStringUTF16(message_id);
}

// static
SkColor PageInfoUI::GetPermissionDecisionTextColor() {
  return SK_ColorGRAY;
}

// static
const gfx::Image& PageInfoUI::GetPermissionIcon(const PermissionInfo& info) {
  ContentSetting setting = info.setting;
  if (setting == CONTENT_SETTING_DEFAULT)
    setting = info.default_setting;
  ResourceBundle& rb = ResourceBundle::GetSharedInstance();
  return rb.GetNativeImageNamed(GetPermissionIconID(info.type, setting));
}

// static
base::string16 PageInfoUI::ChosenObjectToUIString(
    const ChosenObjectInfo& object) {
  base::string16 name;
  object.object->GetString(object.ui_info.ui_name_key, &name);
  return name;
}

// static
const gfx::Image& PageInfoUI::GetChosenObjectIcon(
    const ChosenObjectInfo& object,
    bool deleted) {
  ResourceBundle& rb = ResourceBundle::GetSharedInstance();
  return rb.GetNativeImageNamed(deleted ? object.ui_info.blocked_icon_id
                                        : object.ui_info.allowed_icon_id);
}

#if defined(OS_ANDROID)
// static
int PageInfoUI::GetIdentityIconID(PageInfo::SiteIdentityStatus status) {
  int resource_id = IDR_PAGEINFO_INFO;
  switch (status) {
    case PageInfo::SITE_IDENTITY_STATUS_UNKNOWN:
    case PageInfo::SITE_IDENTITY_STATUS_INTERNAL_PAGE:
      break;
    case PageInfo::SITE_IDENTITY_STATUS_CERT:
    case PageInfo::SITE_IDENTITY_STATUS_EV_CERT:
      resource_id = IDR_PAGEINFO_GOOD;
      break;
    case PageInfo::SITE_IDENTITY_STATUS_CERT_REVOCATION_UNKNOWN:
      resource_id = IDR_PAGEINFO_WARNING_MINOR;
      break;
    case PageInfo::SITE_IDENTITY_STATUS_NO_CERT:
      resource_id = IDR_PAGEINFO_WARNING_MAJOR;
      break;
    case PageInfo::SITE_IDENTITY_STATUS_ERROR:
      resource_id = IDR_PAGEINFO_BAD;
      break;
    case PageInfo::SITE_IDENTITY_STATUS_ADMIN_PROVIDED_CERT:
      resource_id = IDR_PAGEINFO_ENTERPRISE_MANAGED;
      break;
    case PageInfo::SITE_IDENTITY_STATUS_DEPRECATED_SIGNATURE_ALGORITHM:
      resource_id = IDR_PAGEINFO_WARNING_MINOR;
      break;
    default:
      NOTREACHED();
      break;
  }
  return resource_id;
}

// static
int PageInfoUI::GetConnectionIconID(PageInfo::SiteConnectionStatus status) {
  int resource_id = IDR_PAGEINFO_INFO;
  switch (status) {
    case PageInfo::SITE_CONNECTION_STATUS_UNKNOWN:
    case PageInfo::SITE_CONNECTION_STATUS_INTERNAL_PAGE:
      break;
    case PageInfo::SITE_CONNECTION_STATUS_ENCRYPTED:
      resource_id = IDR_PAGEINFO_GOOD;
      break;
    case PageInfo::SITE_CONNECTION_STATUS_INSECURE_PASSIVE_SUBRESOURCE:
    case PageInfo::SITE_CONNECTION_STATUS_INSECURE_FORM_ACTION:
      resource_id = IDR_PAGEINFO_WARNING_MINOR;
      break;
    case PageInfo::SITE_CONNECTION_STATUS_UNENCRYPTED:
      resource_id = IDR_PAGEINFO_WARNING_MAJOR;
      break;
    case PageInfo::SITE_CONNECTION_STATUS_INSECURE_ACTIVE_SUBRESOURCE:
    case PageInfo::SITE_CONNECTION_STATUS_ENCRYPTED_ERROR:
      resource_id = IDR_PAGEINFO_BAD;
      break;
  }
  return resource_id;
}
#else  // !defined(OS_ANDROID)
// static
const gfx::ImageSkia PageInfoUI::GetCertificateIcon() {
  return gfx::CreateVectorIcon(kCertificateIcon, 16, gfx::kChromeIconGrey);
}
#endif

// static
bool PageInfoUI::ShouldShowCertificateLink() {
  return base::CommandLine::ForCurrentProcess()->HasSwitch(
      switches::kShowCertLink);
}
