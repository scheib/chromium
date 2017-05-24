// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/permissions/permission_context_base.h"

#include <stddef.h>

#include <string>
#include <utility>

#include "base/callback.h"
#include "base/logging.h"
#include "base/memory/ptr_util.h"
#include "base/strings/stringprintf.h"
#include "base/time/time.h"
#include "build/build_config.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/content_settings/host_content_settings_map_factory.h"
#include "chrome/browser/permissions/permission_decision_auto_blocker.h"
#include "chrome/browser/permissions/permission_request.h"
#include "chrome/browser/permissions/permission_request_id.h"
#include "chrome/browser/permissions/permission_request_impl.h"
#include "chrome/browser/permissions/permission_request_manager.h"
#include "chrome/browser/permissions/permission_uma_util.h"
#include "chrome/browser/permissions/permission_util.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/safe_browsing/safe_browsing_service.h"
#include "chrome/common/chrome_features.h"
#include "chrome/common/pref_names.h"
#include "components/content_settings/core/browser/host_content_settings_map.h"
#include "components/prefs/pref_service.h"
#include "components/safe_browsing_db/database_manager.h"
#include "components/variations/variations_associated_data.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/origin_util.h"
#include "url/gurl.h"

#if defined(OS_ANDROID)
#include "chrome/browser/permissions/permission_queue_controller.h"
#endif

namespace {

const char kPermissionBlockedKillSwitchMessage[] =
    "%s permission has been blocked.";

const char kPermissionBlockedRepeatedDismissalsMessage[] =
    "%s permission has been blocked as the user has dismissed the permission "
    "prompt several times. See "
    "https://www.chromestatus.com/features/6443143280984064 for more "
    "information.";

const char kPermissionBlockedRepeatedIgnoresMessage[] =
    "%s permission has been blocked as the user has ignored the permission "
    "prompt several times. See "
    "https://www.chromestatus.com/features/6443143280984064 for more "
    "information.";

const char kPermissionBlockedBlacklistMessage[] =
    "this origin is not allowed to request %s permission.";

void LogPermissionBlockedMessage(content::WebContents* web_contents,
                                 const char* message,
                                 ContentSettingsType type) {
  web_contents->GetMainFrame()->AddMessageToConsole(
      content::CONSOLE_MESSAGE_LEVEL_INFO,
      base::StringPrintf(message,
                         PermissionUtil::GetPermissionString(type).c_str()));
}

}  // namespace

// static
const char PermissionContextBase::kPermissionsKillSwitchFieldStudy[] =
    "PermissionsKillSwitch";
// static
const char PermissionContextBase::kPermissionsKillSwitchBlockedValue[] =
    "blocked";

PermissionContextBase::PermissionContextBase(
    Profile* profile,
    const ContentSettingsType content_settings_type)
    : profile_(profile),
      content_settings_type_(content_settings_type),
      weak_factory_(this) {
#if defined(OS_ANDROID)
  permission_queue_controller_.reset(
      new PermissionQueueController(profile_, content_settings_type_));
#endif
  PermissionDecisionAutoBlocker::UpdateFromVariations();
}

PermissionContextBase::~PermissionContextBase() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
}

void PermissionContextBase::RequestPermission(
    content::WebContents* web_contents,
    const PermissionRequestID& id,
    const GURL& requesting_frame,
    bool user_gesture,
    const BrowserPermissionCallback& callback) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

  GURL requesting_origin = requesting_frame.GetOrigin();
  GURL embedding_origin = web_contents->GetLastCommittedURL().GetOrigin();

  if (!requesting_origin.is_valid() || !embedding_origin.is_valid()) {
    std::string type_name =
        PermissionUtil::GetPermissionString(content_settings_type_);

    DVLOG(1) << "Attempt to use " << type_name
             << " from an invalid URL: " << requesting_origin << ","
             << embedding_origin << " (" << type_name
             << " is not supported in popups)";
    NotifyPermissionSet(id, requesting_origin, embedding_origin, callback,
                        false /* persist */, CONTENT_SETTING_BLOCK);
    return;
  }

  // Synchronously check the content setting to see if the user has already made
  // a decision, or if the origin is under embargo. If so, respect that
  // decision.
  // TODO(raymes): Pass in the RenderFrameHost of the request here.
  PermissionResult result = GetPermissionStatus(
      nullptr /* render_frame_host */, requesting_origin, embedding_origin);

  if (result.content_setting == CONTENT_SETTING_ALLOW ||
      result.content_setting == CONTENT_SETTING_BLOCK) {
    switch (result.source) {
      case PermissionStatusSource::KILL_SWITCH:
        // Block the request and log to the developer console.
        LogPermissionBlockedMessage(web_contents,
                                    kPermissionBlockedKillSwitchMessage,
                                    content_settings_type_);
        callback.Run(CONTENT_SETTING_BLOCK);
        return;
      case PermissionStatusSource::MULTIPLE_DISMISSALS:
        LogPermissionBlockedMessage(web_contents,
                                    kPermissionBlockedRepeatedDismissalsMessage,
                                    content_settings_type_);
        break;
      case PermissionStatusSource::MULTIPLE_IGNORES:
        LogPermissionBlockedMessage(web_contents,
                                    kPermissionBlockedRepeatedIgnoresMessage,
                                    content_settings_type_);
        break;
      case PermissionStatusSource::SAFE_BROWSING_BLACKLIST:
        LogPermissionBlockedMessage(web_contents,
                                    kPermissionBlockedBlacklistMessage,
                                    content_settings_type_);
        break;
      case PermissionStatusSource::UNSPECIFIED:
        break;
    }

    // If we are under embargo, record the embargo reason for which we have
    // suppressed the prompt.
    PermissionUmaUtil::RecordEmbargoPromptSuppressionFromSource(result.source);
    NotifyPermissionSet(id, requesting_origin, embedding_origin, callback,
                        false /* persist */, result.content_setting);
    return;
  }

  // Asynchronously check whether the origin should be blocked from making this
  // permission request, e.g. it may be on the Safe Browsing API blacklist.
  PermissionDecisionAutoBlocker::GetForProfile(profile_)
      ->CheckSafeBrowsingBlacklist(
          web_contents, requesting_origin, content_settings_type_,
          base::Bind(&PermissionContextBase::ContinueRequestPermission,
                     weak_factory_.GetWeakPtr(), web_contents, id,
                     requesting_origin, embedding_origin, user_gesture,
                     callback));
}

void PermissionContextBase::ContinueRequestPermission(
    content::WebContents* web_contents,
    const PermissionRequestID& id,
    const GURL& requesting_origin,
    const GURL& embedding_origin,
    bool user_gesture,
    const BrowserPermissionCallback& callback,
    bool permission_blocked) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (permission_blocked) {
    LogPermissionBlockedMessage(web_contents,
                                kPermissionBlockedBlacklistMessage,
                                content_settings_type_);
    // Permission has been automatically blocked. Record that the prompt was
    // suppressed and that we hit the blacklist.
    PermissionUmaUtil::RecordEmbargoPromptSuppression(
        PermissionEmbargoStatus::PERMISSIONS_BLACKLISTING);
    PermissionUmaUtil::RecordEmbargoStatus(
        PermissionEmbargoStatus::PERMISSIONS_BLACKLISTING);
    callback.Run(CONTENT_SETTING_BLOCK);
    return;
  }

  // We are going to show a prompt now.
  PermissionUmaUtil::PermissionRequested(
      content_settings_type_, requesting_origin, embedding_origin, profile_);
  PermissionUmaUtil::RecordEmbargoPromptSuppression(
      PermissionEmbargoStatus::NOT_EMBARGOED);

  DecidePermission(web_contents, id, requesting_origin, embedding_origin,
                   user_gesture, callback);
}

void PermissionContextBase::UserMadePermissionDecision(
    const PermissionRequestID& id,
    const GURL& requesting_origin,
    const GURL& embedding_origin,
    ContentSetting content_setting) {}

PermissionResult PermissionContextBase::GetPermissionStatus(
    content::RenderFrameHost* render_frame_host,
    const GURL& requesting_origin,
    const GURL& embedding_origin) const {
  // If the permission has been disabled through Finch, block all requests.
  if (IsPermissionKillSwitchOn()) {
    return PermissionResult(CONTENT_SETTING_BLOCK,
                            PermissionStatusSource::KILL_SWITCH);
  }

  if (IsRestrictedToSecureOrigins()) {
    // TODO(raymes): We should check the entire chain of embedders here whenever
    // possible as this corresponds to the requirements of the secure contexts
    // spec and matches what is implemented in blink.
    if (!content::IsOriginSecure(requesting_origin) ||
        !content::IsOriginSecure(embedding_origin)) {
      return PermissionResult(CONTENT_SETTING_BLOCK,
                              PermissionStatusSource::UNSPECIFIED);
    }
  }

  ContentSetting content_setting = GetPermissionStatusInternal(
      render_frame_host, requesting_origin, embedding_origin);
  if (content_setting == CONTENT_SETTING_ASK) {
    PermissionResult result =
        PermissionDecisionAutoBlocker::GetForProfile(profile_)
            ->GetEmbargoResult(requesting_origin, content_settings_type_);
    DCHECK(result.content_setting == CONTENT_SETTING_ASK ||
           result.content_setting == CONTENT_SETTING_BLOCK);
    return result;
  }

  return PermissionResult(content_setting, PermissionStatusSource::UNSPECIFIED);
}

PermissionResult PermissionContextBase::UpdatePermissionStatusWithDeviceStatus(
    PermissionResult result,
    const GURL& requesting_origin,
    const GURL& embedding_origin) const {
  return result;
}

void PermissionContextBase::ResetPermission(const GURL& requesting_origin,
                                            const GURL& embedding_origin) {
  HostContentSettingsMapFactory::GetForProfile(profile_)
      ->SetContentSettingDefaultScope(requesting_origin, embedding_origin,
                                      content_settings_storage_type(),
                                      std::string(), CONTENT_SETTING_DEFAULT);
}

void PermissionContextBase::CancelPermissionRequest(
    content::WebContents* web_contents,
    const PermissionRequestID& id) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

  if (PermissionRequestManager::IsEnabled()) {
    auto it = pending_requests_.find(id.ToString());
    if (it != pending_requests_.end() && web_contents != nullptr &&
        PermissionRequestManager::FromWebContents(web_contents) != nullptr) {
      PermissionRequestManager::FromWebContents(web_contents)
          ->CancelRequest(it->second.get());
    }
  } else {
#if defined(OS_ANDROID)
    GetQueueController()->CancelInfoBarRequest(id);
#else
    NOTREACHED();
#endif
  }
}

bool PermissionContextBase::IsPermissionKillSwitchOn() const {
  const std::string param = variations::GetVariationParamValue(
      kPermissionsKillSwitchFieldStudy,
      PermissionUtil::GetPermissionString(content_settings_type_));

  return param == kPermissionsKillSwitchBlockedValue;
}

ContentSetting PermissionContextBase::GetPermissionStatusInternal(
    content::RenderFrameHost* render_frame_host,
    const GURL& requesting_origin,
    const GURL& embedding_origin) const {
  return HostContentSettingsMapFactory::GetForProfile(profile_)
      ->GetContentSetting(requesting_origin, embedding_origin,
                          content_settings_storage_type(), std::string());
}

void PermissionContextBase::DecidePermission(
    content::WebContents* web_contents,
    const PermissionRequestID& id,
    const GURL& requesting_origin,
    const GURL& embedding_origin,
    bool user_gesture,
    const BrowserPermissionCallback& callback) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

  if (PermissionRequestManager::IsEnabled()) {
    PermissionRequestManager* permission_request_manager =
        PermissionRequestManager::FromWebContents(web_contents);
    // TODO(felt): sometimes |permission_request_manager| is null. This check is
    // meant to prevent crashes. See crbug.com/457091.
    if (!permission_request_manager)
      return;

    std::unique_ptr<PermissionRequest> request_ptr =
        base::MakeUnique<PermissionRequestImpl>(
            requesting_origin, content_settings_type_, profile_, user_gesture,
            base::Bind(&PermissionContextBase::PermissionDecided,
                       weak_factory_.GetWeakPtr(), id, requesting_origin,
                       embedding_origin, user_gesture, callback),
            base::Bind(&PermissionContextBase::CleanUpRequest,
                       weak_factory_.GetWeakPtr(), id));
    PermissionRequest* request = request_ptr.get();

    bool inserted =
        pending_requests_
            .insert(std::make_pair(id.ToString(), std::move(request_ptr)))
            .second;
    DCHECK(inserted) << "Duplicate id " << id.ToString();
    permission_request_manager->AddRequest(request);
  } else {
#if defined(OS_ANDROID)
    GetQueueController()->CreateInfoBarRequest(
        id, requesting_origin, embedding_origin, user_gesture,
        base::Bind(&PermissionContextBase::PermissionDecided,
                   weak_factory_.GetWeakPtr(), id, requesting_origin,
                   embedding_origin, user_gesture, callback,
                   // the queue controller takes care of persisting the
                   // permission
                   false));
#else
    NOTREACHED();
#endif
  }
}

void PermissionContextBase::PermissionDecided(
    const PermissionRequestID& id,
    const GURL& requesting_origin,
    const GURL& embedding_origin,
    bool user_gesture,
    const BrowserPermissionCallback& callback,
    bool persist,
    ContentSetting content_setting) {
  if (PermissionRequestManager::IsEnabled()) {
    // Infobar persistence and its related UMA is tracked on the infobar
    // controller directly.
    PermissionRequestGestureType gesture_type =
        user_gesture ? PermissionRequestGestureType::GESTURE
                     : PermissionRequestGestureType::NO_GESTURE;
    PermissionEmbargoStatus embargo_status =
        PermissionEmbargoStatus::NOT_EMBARGOED;
    DCHECK(content_setting == CONTENT_SETTING_ALLOW ||
           content_setting == CONTENT_SETTING_BLOCK ||
           content_setting == CONTENT_SETTING_DEFAULT);
    if (content_setting == CONTENT_SETTING_ALLOW) {
      PermissionUmaUtil::PermissionGranted(content_settings_type_, gesture_type,
                                           requesting_origin, profile_);
    } else if (content_setting == CONTENT_SETTING_BLOCK) {
      PermissionUmaUtil::PermissionDenied(content_settings_type_, gesture_type,
                                          requesting_origin, profile_);
    } else {
      PermissionUmaUtil::PermissionDismissed(
          content_settings_type_, gesture_type, requesting_origin, profile_);

      if (PermissionDecisionAutoBlocker::GetForProfile(profile_)
              ->RecordDismissAndEmbargo(requesting_origin,
                                        content_settings_type_)) {
        embargo_status = PermissionEmbargoStatus::REPEATED_DISMISSALS;
      }
    }
    PermissionUmaUtil::RecordEmbargoStatus(embargo_status);
  }

  UserMadePermissionDecision(id, requesting_origin, embedding_origin,
                             content_setting);
  NotifyPermissionSet(id, requesting_origin, embedding_origin, callback,
                      persist, content_setting);
}

#if defined(OS_ANDROID)
PermissionQueueController* PermissionContextBase::GetQueueController() {
  return permission_queue_controller_.get();
}
#endif

Profile* PermissionContextBase::profile() const {
  return profile_;
}

void PermissionContextBase::NotifyPermissionSet(
    const PermissionRequestID& id,
    const GURL& requesting_origin,
    const GURL& embedding_origin,
    const BrowserPermissionCallback& callback,
    bool persist,
    ContentSetting content_setting) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

  if (persist)
    UpdateContentSetting(requesting_origin, embedding_origin, content_setting);

  UpdateTabContext(id, requesting_origin,
                   content_setting == CONTENT_SETTING_ALLOW);

  if (content_setting == CONTENT_SETTING_DEFAULT)
    content_setting = CONTENT_SETTING_ASK;

  callback.Run(content_setting);
}

void PermissionContextBase::CleanUpRequest(const PermissionRequestID& id) {
  size_t success = pending_requests_.erase(id.ToString());
  DCHECK(success == 1) << "Missing request " << id.ToString();
}

void PermissionContextBase::UpdateContentSetting(
    const GURL& requesting_origin,
    const GURL& embedding_origin,
    ContentSetting content_setting) {
  DCHECK_EQ(requesting_origin, requesting_origin.GetOrigin());
  DCHECK_EQ(embedding_origin, embedding_origin.GetOrigin());
  DCHECK(content_setting == CONTENT_SETTING_ALLOW ||
         content_setting == CONTENT_SETTING_BLOCK);
  DCHECK(!requesting_origin.SchemeIsFile());
  DCHECK(!embedding_origin.SchemeIsFile());

  HostContentSettingsMapFactory::GetForProfile(profile_)
      ->SetContentSettingDefaultScope(requesting_origin, embedding_origin,
                                      content_settings_storage_type(),
                                      std::string(), content_setting);
}

ContentSettingsType PermissionContextBase::content_settings_storage_type()
    const {
  if (content_settings_type_ == CONTENT_SETTINGS_TYPE_PUSH_MESSAGING)
    return CONTENT_SETTINGS_TYPE_NOTIFICATIONS;
  return content_settings_type_;
}
