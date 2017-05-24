// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/geolocation/geolocation_permission_context.h"

#include <stddef.h>

#include <set>
#include <string>
#include <utility>
#include <vector>

#include "base/bind.h"
#include "base/command_line.h"
#include "base/containers/hash_tables.h"
#include "base/gtest_prod_util.h"
#include "base/id_map.h"
#include "base/memory/ptr_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/synchronization/waitable_event.h"
#include "base/test/histogram_tester.h"
#include "base/test/scoped_feature_list.h"
#include "base/test/simple_test_clock.h"
#include "base/time/clock.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/content_settings/host_content_settings_map_factory.h"
#include "chrome/browser/content_settings/tab_specific_content_settings.h"
#include "chrome/browser/infobars/infobar_service.h"
#include "chrome/browser/permissions/permission_context_base.h"
#include "chrome/browser/permissions/permission_manager.h"
#include "chrome/browser/permissions/permission_request.h"
#include "chrome/browser/permissions/permission_request_id.h"
#include "chrome/browser/permissions/permission_request_manager.h"
#include "chrome/browser/ui/permission_bubble/mock_permission_prompt_factory.h"
#include "chrome/common/chrome_features.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/test/base/chrome_render_view_host_test_harness.h"
#include "chrome/test/base/testing_profile.h"
#include "components/content_settings/core/browser/host_content_settings_map.h"
#include "components/infobars/core/confirm_infobar_delegate.h"
#include "components/infobars/core/infobar.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/navigation_details.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/mock_render_process_host.h"
#include "content/public/test/test_renderer_host.h"
#include "content/public/test/test_utils.h"
#include "content/public/test/web_contents_tester.h"
#include "extensions/features/features.h"
#include "testing/gtest/include/gtest/gtest.h"

#if defined(OS_ANDROID)
#include "chrome/browser/android/mock_location_settings.h"
#include "chrome/browser/android/search_geolocation/search_geolocation_service.h"
#include "chrome/browser/geolocation/geolocation_permission_context_android.h"
#include "components/location/android/location_settings_dialog_outcome.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/permission_type.h"
#include "third_party/WebKit/public/platform/modules/permissions/permission_status.mojom.h"
#endif

#if BUILDFLAG(ENABLE_EXTENSIONS)
#include "extensions/browser/view_type_utils.h"
#endif

using content::MockRenderProcessHost;


// ClosedInfoBarTracker -------------------------------------------------------

// We need to track which infobars were closed.
class ClosedInfoBarTracker : public content::NotificationObserver {
 public:
  ClosedInfoBarTracker();
  ~ClosedInfoBarTracker() override;

  // content::NotificationObserver:
  void Observe(int type,
               const content::NotificationSource& source,
               const content::NotificationDetails& details) override;

  size_t size() const { return removed_infobars_.size(); }

  bool Contains(infobars::InfoBar* infobar) const;
  void Clear();

 private:
  FRIEND_TEST_ALL_PREFIXES(GeolocationPermissionContextTests, TabDestroyed);
  content::NotificationRegistrar registrar_;
  std::set<infobars::InfoBar*> removed_infobars_;
};

ClosedInfoBarTracker::ClosedInfoBarTracker() {
  registrar_.Add(this, chrome::NOTIFICATION_TAB_CONTENTS_INFOBAR_REMOVED,
                 content::NotificationService::AllSources());
}

ClosedInfoBarTracker::~ClosedInfoBarTracker() {
}

void ClosedInfoBarTracker::Observe(
    int type,
    const content::NotificationSource& source,
    const content::NotificationDetails& details) {
  DCHECK_EQ(chrome::NOTIFICATION_TAB_CONTENTS_INFOBAR_REMOVED, type);
  removed_infobars_.insert(
      content::Details<infobars::InfoBar::RemovedDetails>(details)->first);
}

bool ClosedInfoBarTracker::Contains(infobars::InfoBar* infobar) const {
  return removed_infobars_.count(infobar) != 0;
}

void ClosedInfoBarTracker::Clear() {
  removed_infobars_.clear();
}

#if defined(OS_ANDROID)
// TestSearchEngineDelegate
class TestSearchEngineDelegate
    : public SearchGeolocationService::SearchEngineDelegate {
 public:
  bool IsDSEGoogle() override { return true; }

  url::Origin GetGoogleDSECCTLD() override {
    return url::Origin(GURL(kDSETestUrl));
  }

  void SetDSEChangedCallback(const base::Closure& callback) override {}

  static const char kDSETestUrl[];
};

const char TestSearchEngineDelegate::kDSETestUrl[] = "https://www.dsetest.com";
#endif  // defined(OS_ANDROID)

// GeolocationPermissionContextTests ------------------------------------------

enum class TestType {
  PERMISSION_REQUEST_MANAGER,
  PERMISSION_QUEUE_CONTROLLER,
};

class GeolocationPermissionContextTests
    : public ChromeRenderViewHostTestHarness,
      public ::testing::WithParamInterface<TestType> {
 protected:
  // ChromeRenderViewHostTestHarness:
  void SetUp() override;
  void TearDown() override;

  PermissionRequestID RequestID(int request_id);
  PermissionRequestID RequestIDForTab(int tab, int request_id);
  InfoBarService* infobar_service() {
    return InfoBarService::FromWebContents(web_contents());
  }
  InfoBarService* infobar_service_for_tab(int tab) {
    return InfoBarService::FromWebContents(extra_tabs_[tab].get());
  }

  void RequestGeolocationPermission(content::WebContents* web_contents,
                                    const PermissionRequestID& id,
                                    const GURL& requesting_frame,
                                    bool user_gesture);

  void PermissionResponse(const PermissionRequestID& id,
                          ContentSetting content_setting);
  void CheckPermissionMessageSent(int request_id, bool allowed);
  void CheckPermissionMessageSentForTab(int tab, int request_id, bool allowed);
  void CheckPermissionMessageSentInternal(MockRenderProcessHost* process,
                                          int request_id,
                                          bool allowed);
  void AddNewTab(const GURL& url);
  void CheckTabContentsState(const GURL& requesting_frame,
                             ContentSetting expected_content_setting);
  void SetupRequestManager(content::WebContents* web_contents);
#if defined(OS_ANDROID)
  bool RequestPermissionIsLSDShown(const GURL& origin);
  bool RequestPermissionIsLSDShownWithPermissionPrompt(const GURL& origin);
  void AddDayOffsetForTesting(int days);
  void SetDSEOriginForTesting(const char* dse_origin);
#endif
  void RequestManagerDocumentLoadCompleted();
  void RequestManagerDocumentLoadCompleted(content::WebContents* web_contents);
  ContentSetting GetGeolocationContentSetting(GURL frame_0, GURL frame_1);
  void SetGeolocationContentSetting(GURL frame_0,
                                    GURL frame_1,
                                    ContentSetting content_setting);
  bool HasActivePrompt();
  bool HasActivePrompt(content::WebContents* web_contents);
  void AcceptPrompt();
  void AcceptPrompt(content::WebContents* web_contents);
  void DenyPrompt();
  void ClosePrompt();
  base::string16 GetPromptText();

  void EnableFeature(base::test::ScopedFeatureList* scoped_feature_list,
                     const base::Feature& feature);

  // owned by the browser context
  GeolocationPermissionContext* geolocation_permission_context_;
  ClosedInfoBarTracker closed_infobar_tracker_;
  std::vector<std::unique_ptr<content::WebContents>> extra_tabs_;
  std::vector<std::unique_ptr<MockPermissionPromptFactory>>
      mock_permission_prompt_factories_;

  // A map between renderer child id and a pair represending the bridge id and
  // whether the requested permission was allowed.
  base::hash_map<int, std::pair<int, bool> > responses_;

  // For testing the PermissionRequestManager on Android
  base::test::ScopedFeatureList scoped_feature_list_;
};

PermissionRequestID GeolocationPermissionContextTests::RequestID(
    int request_id) {
  return PermissionRequestID(
      web_contents()->GetRenderProcessHost()->GetID(),
      web_contents()->GetMainFrame()->GetRoutingID(),
      request_id);
}

PermissionRequestID GeolocationPermissionContextTests::RequestIDForTab(
    int tab,
    int request_id) {
  return PermissionRequestID(
      extra_tabs_[tab]->GetRenderProcessHost()->GetID(),
      extra_tabs_[tab]->GetMainFrame()->GetRoutingID(),
      request_id);
}

void GeolocationPermissionContextTests::RequestGeolocationPermission(
    content::WebContents* web_contents,
    const PermissionRequestID& id,
    const GURL& requesting_frame,
    bool user_gesture) {
  geolocation_permission_context_->RequestPermission(
      web_contents, id, requesting_frame, user_gesture,
      base::Bind(&GeolocationPermissionContextTests::PermissionResponse,
                 base::Unretained(this), id));
  content::RunAllBlockingPoolTasksUntilIdle();
}

void GeolocationPermissionContextTests::PermissionResponse(
    const PermissionRequestID& id,
    ContentSetting content_setting) {
  responses_[id.render_process_id()] =
      std::make_pair(id.request_id(), content_setting == CONTENT_SETTING_ALLOW);
}

void GeolocationPermissionContextTests::CheckPermissionMessageSent(
    int request_id,
    bool allowed) {
  CheckPermissionMessageSentInternal(process(), request_id, allowed);
}

void GeolocationPermissionContextTests::CheckPermissionMessageSentForTab(
    int tab,
    int request_id,
    bool allowed) {
  CheckPermissionMessageSentInternal(static_cast<MockRenderProcessHost*>(
      extra_tabs_[tab]->GetRenderProcessHost()),
      request_id, allowed);
}

void GeolocationPermissionContextTests::CheckPermissionMessageSentInternal(
    MockRenderProcessHost* process,
    int request_id,
    bool allowed) {
  ASSERT_EQ(responses_.count(process->GetID()), 1U);
  EXPECT_EQ(request_id, responses_[process->GetID()].first);
  EXPECT_EQ(allowed, responses_[process->GetID()].second);
  responses_.erase(process->GetID());
}

void GeolocationPermissionContextTests::AddNewTab(const GURL& url) {
  content::WebContents* new_tab = CreateTestWebContents();
  new_tab->GetController().LoadURL(
      url, content::Referrer(), ui::PAGE_TRANSITION_TYPED, std::string());
  content::NavigationEntry* entry = new_tab->GetController().GetPendingEntry();
  content::RenderFrameHostTester::For(new_tab->GetMainFrame())
      ->SendNavigate(entry->GetUniqueID(), true, url);

  // Set up required helpers, and make this be as "tabby" as the code requires.
#if BUILDFLAG(ENABLE_EXTENSIONS)
  extensions::SetViewType(new_tab, extensions::VIEW_TYPE_TAB_CONTENTS);
#endif

  if (GetParam() == TestType::PERMISSION_REQUEST_MANAGER)
    SetupRequestManager(new_tab);
  else
    InfoBarService::CreateForWebContents(new_tab);

  extra_tabs_.push_back(base::WrapUnique(new_tab));
}

void GeolocationPermissionContextTests::CheckTabContentsState(
    const GURL& requesting_frame,
    ContentSetting expected_content_setting) {
  TabSpecificContentSettings* content_settings =
      TabSpecificContentSettings::FromWebContents(web_contents());
  const ContentSettingsUsagesState::StateMap& state_map =
      content_settings->geolocation_usages_state().state_map();
  EXPECT_EQ(1U, state_map.count(requesting_frame.GetOrigin()));
  EXPECT_EQ(0U, state_map.count(requesting_frame));
  ContentSettingsUsagesState::StateMap::const_iterator settings =
      state_map.find(requesting_frame.GetOrigin());
  ASSERT_FALSE(settings == state_map.end())
      << "geolocation state not found " << requesting_frame;
  EXPECT_EQ(expected_content_setting, settings->second);
}

void GeolocationPermissionContextTests::SetUp() {
  ChromeRenderViewHostTestHarness::SetUp();

  // Set up required helpers, and make this be as "tabby" as the code requires.
#if BUILDFLAG(ENABLE_EXTENSIONS)
  extensions::SetViewType(web_contents(), extensions::VIEW_TYPE_TAB_CONTENTS);
#endif
  InfoBarService::CreateForWebContents(web_contents());
  TabSpecificContentSettings::CreateForWebContents(web_contents());
  geolocation_permission_context_ = static_cast<GeolocationPermissionContext*>(
      PermissionManager::Get(profile())->GetPermissionContext(
          CONTENT_SETTINGS_TYPE_GEOLOCATION));

  if (GetParam() == TestType::PERMISSION_REQUEST_MANAGER) {
    // This feature enables the PRM for Android and no-ops on desktop.
    scoped_feature_list_.InitAndEnableFeature(
        features::kUseGroupedPermissionInfobars);
    SetupRequestManager(web_contents());
  } else {
    scoped_feature_list_.InitAndDisableFeature(
        features::kUseGroupedPermissionInfobars);
  }

#if defined(OS_ANDROID)
  static_cast<GeolocationPermissionContextAndroid*>(
      geolocation_permission_context_)
      ->SetLocationSettingsForTesting(
          std::unique_ptr<LocationSettings>(new MockLocationSettings()));
  MockLocationSettings::SetLocationStatus(true, true);
  MockLocationSettings::SetCanPromptForAndroidPermission(true);
  MockLocationSettings::SetLocationSettingsDialogStatus(false /* enabled */,
                                                        GRANTED);
  MockLocationSettings::ClearHasShownLocationSettingsDialog();
#endif
}

void GeolocationPermissionContextTests::TearDown() {
  mock_permission_prompt_factories_.clear();
  extra_tabs_.clear();
  ChromeRenderViewHostTestHarness::TearDown();
}

void GeolocationPermissionContextTests::SetupRequestManager(
    content::WebContents* web_contents) {
  // Create PermissionRequestManager.
  PermissionRequestManager::CreateForWebContents(web_contents);
  PermissionRequestManager* permission_request_manager =
      PermissionRequestManager::FromWebContents(web_contents);

  // Create a MockPermissionPromptFactory for the PermissionRequestManager.
  mock_permission_prompt_factories_.push_back(
      base::MakeUnique<MockPermissionPromptFactory>(
          permission_request_manager));

  // Prepare the PermissionRequestManager to display a mock bubble.
  permission_request_manager->DisplayPendingRequests();
}

#if defined(OS_ANDROID)

bool GeolocationPermissionContextTests::RequestPermissionIsLSDShown(
    const GURL& origin) {
  NavigateAndCommit(origin);
  RequestManagerDocumentLoadCompleted();
  MockLocationSettings::ClearHasShownLocationSettingsDialog();
  RequestGeolocationPermission(web_contents(), RequestID(0), origin, true);

  return MockLocationSettings::HasShownLocationSettingsDialog();
}

bool GeolocationPermissionContextTests::
    RequestPermissionIsLSDShownWithPermissionPrompt(const GURL& origin) {
  NavigateAndCommit(origin);
  RequestManagerDocumentLoadCompleted();
  MockLocationSettings::ClearHasShownLocationSettingsDialog();
  RequestGeolocationPermission(web_contents(), RequestID(0), origin, true);

  EXPECT_TRUE(HasActivePrompt());
  AcceptPrompt();

  return MockLocationSettings::HasShownLocationSettingsDialog();
}

void GeolocationPermissionContextTests::AddDayOffsetForTesting(int days) {
  GeolocationPermissionContextAndroid::AddDayOffsetForTesting(days);
}

void GeolocationPermissionContextTests::SetDSEOriginForTesting(
    const char* dse_origin) {
  GeolocationPermissionContextAndroid::SetDSEOriginForTesting(dse_origin);
}
#endif

void GeolocationPermissionContextTests::RequestManagerDocumentLoadCompleted() {
  GeolocationPermissionContextTests::RequestManagerDocumentLoadCompleted(
      web_contents());
}

void GeolocationPermissionContextTests::RequestManagerDocumentLoadCompleted(
    content::WebContents* web_contents) {
  if (GetParam() == TestType::PERMISSION_REQUEST_MANAGER) {
    PermissionRequestManager::FromWebContents(web_contents)
        ->DocumentOnLoadCompletedInMainFrame();
  }
}

ContentSetting GeolocationPermissionContextTests::GetGeolocationContentSetting(
    GURL frame_0, GURL frame_1) {
  return HostContentSettingsMapFactory::GetForProfile(profile())
      ->GetContentSetting(frame_0,
                          frame_1,
                          CONTENT_SETTINGS_TYPE_GEOLOCATION,
                          std::string());
}

void GeolocationPermissionContextTests::SetGeolocationContentSetting(
    GURL frame_0,
    GURL frame_1,
    ContentSetting content_setting) {
  return HostContentSettingsMapFactory::GetForProfile(profile())
      ->SetContentSettingDefaultScope(frame_0, frame_1,
                                      CONTENT_SETTINGS_TYPE_GEOLOCATION,
                                      std::string(), content_setting);
}

bool GeolocationPermissionContextTests::HasActivePrompt() {
  return HasActivePrompt(web_contents());
}

bool GeolocationPermissionContextTests::HasActivePrompt(
    content::WebContents* web_contents) {
  if (GetParam() == TestType::PERMISSION_REQUEST_MANAGER) {
    PermissionRequestManager* manager =
        PermissionRequestManager::FromWebContents(web_contents);
    return manager->IsBubbleVisible();
  }

  return InfoBarService::FromWebContents(web_contents)->infobar_count() > 0;
}

void GeolocationPermissionContextTests::AcceptPrompt() {
  return AcceptPrompt(web_contents());
}

void GeolocationPermissionContextTests::AcceptPrompt(
    content::WebContents* web_contents) {
  if (GetParam() == TestType::PERMISSION_REQUEST_MANAGER) {
    PermissionRequestManager* manager =
        PermissionRequestManager::FromWebContents(web_contents);
    manager->Accept();
  } else {
    InfoBarService* infobar_service =
        InfoBarService::FromWebContents(web_contents);
    infobars::InfoBar* infobar = infobar_service->infobar_at(0);
    ConfirmInfoBarDelegate* infobar_delegate =
        infobar->delegate()->AsConfirmInfoBarDelegate();
    ASSERT_TRUE(infobar_delegate);
    infobar_delegate->Accept();
    infobar_service->RemoveInfoBar(infobar);
    EXPECT_TRUE(closed_infobar_tracker_.Contains(infobar));
  }
}

void GeolocationPermissionContextTests::DenyPrompt() {
  if (GetParam() == TestType::PERMISSION_REQUEST_MANAGER) {
    PermissionRequestManager* manager =
        PermissionRequestManager::FromWebContents(web_contents());
    manager->Deny();
  } else {
    infobars::InfoBar* infobar = infobar_service()->infobar_at(0);
    infobar->delegate()->AsConfirmInfoBarDelegate()->Cancel();
  }
}

void GeolocationPermissionContextTests::ClosePrompt() {
  if (GetParam() == TestType::PERMISSION_REQUEST_MANAGER) {
    PermissionRequestManager* manager =
        PermissionRequestManager::FromWebContents(web_contents());
    manager->Closing();
  } else {
    geolocation_permission_context_->CancelPermissionRequest(web_contents(),
                                                             RequestID(0));
  }
}

base::string16 GeolocationPermissionContextTests::GetPromptText() {
  if (GetParam() == TestType::PERMISSION_REQUEST_MANAGER) {
    PermissionRequestManager* manager =
        PermissionRequestManager::FromWebContents(web_contents());
    PermissionRequest* request = manager->requests_.front();
    return base::ASCIIToUTF16(request->GetOrigin().spec()) +
           request->GetMessageTextFragment();
  }

  infobars::InfoBar* infobar = infobar_service()->infobar_at(0);
  ConfirmInfoBarDelegate* infobar_delegate =
      infobar->delegate()->AsConfirmInfoBarDelegate();
  return infobar_delegate->GetMessageText();
}

void GeolocationPermissionContextTests::EnableFeature(
    base::test::ScopedFeatureList* scoped_feature_list,
    const base::Feature& feature) {
  if (GetParam() == TestType::PERMISSION_REQUEST_MANAGER) {
    scoped_feature_list->InitWithFeatures(
        {features::kUseGroupedPermissionInfobars, feature}, {});
  } else {
    scoped_feature_list->InitWithFeatures(
        {feature}, {features::kUseGroupedPermissionInfobars});
  }
}

// Tests ----------------------------------------------------------------------

TEST_P(GeolocationPermissionContextTests, SinglePermissionPrompt) {
  GURL requesting_frame("https://www.example.com/geolocation");
  NavigateAndCommit(requesting_frame);
  RequestManagerDocumentLoadCompleted();

  EXPECT_FALSE(HasActivePrompt());
  RequestGeolocationPermission(
      web_contents(), RequestID(0), requesting_frame, true);
  ASSERT_TRUE(HasActivePrompt());
}

TEST_P(GeolocationPermissionContextTests,
       SinglePermissionPromptFailsOnInsecureOrigin) {
  GURL requesting_frame("http://www.example.com/geolocation");
  NavigateAndCommit(requesting_frame);
  RequestManagerDocumentLoadCompleted();

  EXPECT_FALSE(HasActivePrompt());
  RequestGeolocationPermission(web_contents(), RequestID(0), requesting_frame,
                               true);
  ASSERT_FALSE(HasActivePrompt());
}

#if defined(OS_ANDROID)
// Tests concerning Android location settings permission
TEST_P(GeolocationPermissionContextTests, GeolocationEnabledDisabled) {
  GURL requesting_frame("https://www.example.com/geolocation");
  NavigateAndCommit(requesting_frame);
  RequestManagerDocumentLoadCompleted();
  MockLocationSettings::SetLocationStatus(true /* android */,
                                          true /* system */);
  EXPECT_FALSE(HasActivePrompt());
  RequestGeolocationPermission(
      web_contents(), RequestID(0), requesting_frame, true);
  EXPECT_TRUE(HasActivePrompt());

  Reload();
  MockLocationSettings::SetLocationStatus(false /* android */,
                                          true /* system */);
  MockLocationSettings::SetCanPromptForAndroidPermission(false);
  EXPECT_FALSE(HasActivePrompt());
  RequestGeolocationPermission(
      web_contents(), RequestID(0), requesting_frame, true);
  EXPECT_FALSE(HasActivePrompt());
}

TEST_P(GeolocationPermissionContextTests, AndroidEnabledCanPrompt) {
  GURL requesting_frame("https://www.example.com/geolocation");
  NavigateAndCommit(requesting_frame);
  RequestManagerDocumentLoadCompleted();
  MockLocationSettings::SetLocationStatus(false /* android */,
                                          true /* system */);
  EXPECT_FALSE(HasActivePrompt());
  RequestGeolocationPermission(
      web_contents(), RequestID(0), requesting_frame, true);
  ASSERT_TRUE(HasActivePrompt());
  AcceptPrompt();
  CheckTabContentsState(requesting_frame, CONTENT_SETTING_ALLOW);
  CheckPermissionMessageSent(0, true);
}

TEST_P(GeolocationPermissionContextTests, AndroidEnabledCantPrompt) {
  GURL requesting_frame("https://www.example.com/geolocation");
  NavigateAndCommit(requesting_frame);
  RequestManagerDocumentLoadCompleted();
  MockLocationSettings::SetLocationStatus(false /* android */,
                                          true /* system */);
  MockLocationSettings::SetCanPromptForAndroidPermission(false);
  EXPECT_FALSE(HasActivePrompt());
  RequestGeolocationPermission(web_contents(), RequestID(0), requesting_frame,
                               true);
  EXPECT_FALSE(HasActivePrompt());
}

TEST_P(GeolocationPermissionContextTests, SystemLocationOffLSDDisabled) {
  base::test::ScopedFeatureList scoped_feature_list;
  EnableFeature(&scoped_feature_list, features::kLsdPermissionPrompt);

  GURL requesting_frame("https://www.example.com/geolocation");
  NavigateAndCommit(requesting_frame);
  RequestManagerDocumentLoadCompleted();
  MockLocationSettings::SetLocationStatus(true /* android */,
                                          false /* system */);
  EXPECT_FALSE(HasActivePrompt());
  RequestGeolocationPermission(
      web_contents(), RequestID(0), requesting_frame, true);
  EXPECT_FALSE(HasActivePrompt());
  EXPECT_FALSE(MockLocationSettings::HasShownLocationSettingsDialog());
}

TEST_P(GeolocationPermissionContextTests, SystemLocationOnNoLSD) {
  base::test::ScopedFeatureList scoped_feature_list;
  EnableFeature(&scoped_feature_list, features::kLsdPermissionPrompt);

  GURL requesting_frame("https://www.example.com/geolocation");
  NavigateAndCommit(requesting_frame);
  RequestManagerDocumentLoadCompleted();
  EXPECT_FALSE(HasActivePrompt());
  RequestGeolocationPermission(web_contents(), RequestID(0), requesting_frame,
                               true);
  ASSERT_TRUE(HasActivePrompt());
  AcceptPrompt();
  CheckTabContentsState(requesting_frame, CONTENT_SETTING_ALLOW);
  CheckPermissionMessageSent(0, true);
  EXPECT_FALSE(MockLocationSettings::HasShownLocationSettingsDialog());
}

TEST_P(GeolocationPermissionContextTests, SystemLocationOffLSDAccept) {
  base::HistogramTester tester;
  base::test::ScopedFeatureList scoped_feature_list;
  EnableFeature(&scoped_feature_list, features::kLsdPermissionPrompt);

  GURL requesting_frame("https://www.example.com/geolocation");
  NavigateAndCommit(requesting_frame);
  RequestManagerDocumentLoadCompleted();
  MockLocationSettings::SetLocationStatus(true /* android */,
                                          false /* system */);
  MockLocationSettings::SetLocationSettingsDialogStatus(true /* enabled */,
                                                        GRANTED);
  EXPECT_FALSE(HasActivePrompt());
  RequestGeolocationPermission(web_contents(), RequestID(0), requesting_frame,
                               true);
  ASSERT_TRUE(HasActivePrompt());
  AcceptPrompt();
  CheckTabContentsState(requesting_frame, CONTENT_SETTING_ALLOW);
  CheckPermissionMessageSent(0, true);
  EXPECT_TRUE(MockLocationSettings::HasShownLocationSettingsDialog());

  tester.ExpectTotalCount("Geolocation.SettingsDialog.ShowEvent.NonDSE", 1);
  tester.ExpectTotalCount("Geolocation.SettingsDialog.AcceptEvent.NonDSE", 1);
  tester.ExpectTotalCount("Geolocation.SettingsDialog.DenyEvent.NonDSE", 0);
}

TEST_P(GeolocationPermissionContextTests, SystemLocationOffLSDReject) {
  base::HistogramTester tester;
  base::test::ScopedFeatureList scoped_feature_list;
  EnableFeature(&scoped_feature_list, features::kLsdPermissionPrompt);

  GURL requesting_frame("https://www.example.com/geolocation");
  NavigateAndCommit(requesting_frame);
  RequestManagerDocumentLoadCompleted();
  MockLocationSettings::SetLocationStatus(true /* android */,
                                          false /* system */);
  MockLocationSettings::SetLocationSettingsDialogStatus(true /* enabled */,
                                                        DENIED);
  EXPECT_FALSE(HasActivePrompt());
  RequestGeolocationPermission(web_contents(), RequestID(0), requesting_frame,
                               true);
  ASSERT_TRUE(HasActivePrompt());
  AcceptPrompt();
  CheckTabContentsState(requesting_frame, CONTENT_SETTING_BLOCK);
  CheckPermissionMessageSent(0, false);
  EXPECT_TRUE(MockLocationSettings::HasShownLocationSettingsDialog());

  tester.ExpectTotalCount("Geolocation.SettingsDialog.ShowEvent.NonDSE", 1);
  tester.ExpectTotalCount("Geolocation.SettingsDialog.AcceptEvent.NonDSE", 0);
  tester.ExpectTotalCount("Geolocation.SettingsDialog.DenyEvent.NonDSE", 1);
}

TEST_P(GeolocationPermissionContextTests, LSDBackOffDifferentSites) {
  base::HistogramTester tester;
  base::test::ScopedFeatureList scoped_feature_list;
  EnableFeature(&scoped_feature_list, features::kLsdPermissionPrompt);

  GURL requesting_frame_1("https://www.example.com/geolocation");
  GURL requesting_frame_2("https://www.example-2.com/geolocation");
  const char* requesting_frame_dse_ptr = "https://www.dse.com/geolocation";
  GURL requesting_frame_dse(requesting_frame_dse_ptr);

  SetDSEOriginForTesting(requesting_frame_dse_ptr);

  // Set all origin geolocation permissions to ALLOW.
  SetGeolocationContentSetting(requesting_frame_1, requesting_frame_1,
                               CONTENT_SETTING_ALLOW);
  SetGeolocationContentSetting(requesting_frame_2, requesting_frame_2,
                               CONTENT_SETTING_ALLOW);
  SetGeolocationContentSetting(requesting_frame_dse, requesting_frame_dse,
                               CONTENT_SETTING_ALLOW);

  // Turn off system location but allow the LSD to be shown, and denied.
  MockLocationSettings::SetLocationStatus(true /* android */,
                                          false /* system */);
  MockLocationSettings::SetLocationSettingsDialogStatus(true /* enabled */,
                                                        DENIED);

  // Now permission requests should trigger the LSD, but the LSD will be denied,
  // putting the requesting origins into backoff. Check that the two non-DSE
  // origins share the same backoff, which is distinct to the DSE origin.
  // First, cancel a LSD prompt on the first non-DSE origin to go into backoff.
  EXPECT_TRUE(RequestPermissionIsLSDShown(requesting_frame_1));

  // Now check that the LSD is prevented on this origin.
  EXPECT_FALSE(RequestPermissionIsLSDShown(requesting_frame_1));

  // Now ask on the other non-DSE origin and check backoff prevented the prompt.
  EXPECT_FALSE(RequestPermissionIsLSDShown(requesting_frame_2));

  // Test that the right histograms are updated.
  tester.ExpectTotalCount("Geolocation.SettingsDialog.ShowEvent.NonDSE", 1);
  tester.ExpectTotalCount("Geolocation.SettingsDialog.ShowEvent.DSE", 0);
  tester.ExpectTotalCount("Geolocation.SettingsDialog.SuppressEvent.NonDSE", 2);
  tester.ExpectTotalCount("Geolocation.SettingsDialog.SuppressEvent.DSE", 0);

  // Now request on the DSE and check that the LSD is shown, as the non-DSE
  // backoff should not apply.
  EXPECT_TRUE(RequestPermissionIsLSDShown(requesting_frame_dse));

  // Now check that the DSE is in backoff.
  EXPECT_FALSE(RequestPermissionIsLSDShown(requesting_frame_dse));

  // Test that the right histograms are updated.
  tester.ExpectTotalCount("Geolocation.SettingsDialog.ShowEvent.NonDSE", 1);
  tester.ExpectTotalCount("Geolocation.SettingsDialog.ShowEvent.DSE", 1);
  tester.ExpectTotalCount("Geolocation.SettingsDialog.SuppressEvent.NonDSE", 2);
  tester.ExpectTotalCount("Geolocation.SettingsDialog.SuppressEvent.DSE", 1);
}

TEST_P(GeolocationPermissionContextTests, LSDBackOffTiming) {
  base::HistogramTester tester;
  base::test::ScopedFeatureList scoped_feature_list;
  EnableFeature(&scoped_feature_list, features::kLsdPermissionPrompt);

  GURL requesting_frame("https://www.example.com/geolocation");
  SetGeolocationContentSetting(requesting_frame, requesting_frame,
                               CONTENT_SETTING_ALLOW);

  // Turn off system location but allow the LSD to be shown, and denied.
  MockLocationSettings::SetLocationStatus(true /* android */,
                                          false /* system */);
  MockLocationSettings::SetLocationSettingsDialogStatus(true /* enabled */,
                                                        DENIED);

  // First, cancel a LSD prompt on the first non-DSE origin to go into backoff.
  EXPECT_TRUE(RequestPermissionIsLSDShown(requesting_frame));
  EXPECT_FALSE(RequestPermissionIsLSDShown(requesting_frame));

  // Check the LSD is prevented in 6 days time.
  AddDayOffsetForTesting(6);
  EXPECT_FALSE(RequestPermissionIsLSDShown(requesting_frame));

  // Check histograms so far.
  tester.ExpectTotalCount("Geolocation.SettingsDialog.ShowEvent.NonDSE", 1);
  tester.ExpectBucketCount("Geolocation.SettingsDialog.ShowEvent.NonDSE",
                           static_cast<base::HistogramBase::Sample>(
                               GeolocationPermissionContextAndroid::
                                   LocationSettingsDialogBackOff::kNoBackOff),
                           1);

  // Check it is shown in one more days time, but then not straight after..
  AddDayOffsetForTesting(1);
  EXPECT_TRUE(RequestPermissionIsLSDShown(requesting_frame));
  EXPECT_FALSE(RequestPermissionIsLSDShown(requesting_frame));

  // Check that it isn't shown 29 days after that.
  AddDayOffsetForTesting(29);
  EXPECT_FALSE(RequestPermissionIsLSDShown(requesting_frame));

  // Check histograms so far.
  tester.ExpectTotalCount("Geolocation.SettingsDialog.ShowEvent.NonDSE", 2);
  tester.ExpectBucketCount("Geolocation.SettingsDialog.ShowEvent.NonDSE",
                           static_cast<base::HistogramBase::Sample>(
                               GeolocationPermissionContextAndroid::
                                   LocationSettingsDialogBackOff::kOneWeek),
                           1);

  // Check it is shown in one more days time, but then not straight after..
  AddDayOffsetForTesting(1);
  EXPECT_TRUE(RequestPermissionIsLSDShown(requesting_frame));
  EXPECT_FALSE(RequestPermissionIsLSDShown(requesting_frame));

  // Check that it isn't shown 89 days after that.
  AddDayOffsetForTesting(89);
  EXPECT_FALSE(RequestPermissionIsLSDShown(requesting_frame));

  // Check histograms so far.
  tester.ExpectTotalCount("Geolocation.SettingsDialog.ShowEvent.NonDSE", 3);
  tester.ExpectBucketCount("Geolocation.SettingsDialog.ShowEvent.NonDSE",
                           static_cast<base::HistogramBase::Sample>(
                               GeolocationPermissionContextAndroid::
                                   LocationSettingsDialogBackOff::kOneMonth),
                           1);

  // Check it is shown in one more days time, but then not straight after..
  AddDayOffsetForTesting(1);
  EXPECT_TRUE(RequestPermissionIsLSDShown(requesting_frame));
  EXPECT_FALSE(RequestPermissionIsLSDShown(requesting_frame));

  // Check that it isn't shown 89 days after that.
  AddDayOffsetForTesting(89);
  EXPECT_FALSE(RequestPermissionIsLSDShown(requesting_frame));

  // Check it is shown in one more days time, but then not straight after..
  AddDayOffsetForTesting(1);
  EXPECT_TRUE(RequestPermissionIsLSDShown(requesting_frame));
  EXPECT_FALSE(RequestPermissionIsLSDShown(requesting_frame));

  // Check histograms so far.
  tester.ExpectTotalCount("Geolocation.SettingsDialog.ShowEvent.NonDSE", 5);
  tester.ExpectBucketCount("Geolocation.SettingsDialog.ShowEvent.NonDSE",
                           static_cast<base::HistogramBase::Sample>(
                               GeolocationPermissionContextAndroid::
                                   LocationSettingsDialogBackOff::kThreeMonths),
                           2);
}

TEST_P(GeolocationPermissionContextTests, LSDBackOffPermissionStatus) {
  base::test::ScopedFeatureList scoped_feature_list;
  EnableFeature(&scoped_feature_list, features::kLsdPermissionPrompt);

  GURL requesting_frame("https://www.example.com/geolocation");
  SetGeolocationContentSetting(requesting_frame, requesting_frame,
                               CONTENT_SETTING_ALLOW);

  // Turn off system location but allow the LSD to be shown, and denied.
  MockLocationSettings::SetLocationStatus(true /* android */,
                                          false /* system */);
  MockLocationSettings::SetLocationSettingsDialogStatus(true /* enabled */,
                                                        DENIED);

  // The permission status should reflect that the LSD will be shown.
  ASSERT_EQ(blink::mojom::PermissionStatus::ASK,
            PermissionManager::Get(profile())->GetPermissionStatus(
                content::PermissionType::GEOLOCATION, requesting_frame,
                requesting_frame));
  EXPECT_TRUE(RequestPermissionIsLSDShown(requesting_frame));

  // Now that the LSD is in backoff, the permission status should reflect it.
  EXPECT_FALSE(RequestPermissionIsLSDShown(requesting_frame));
  ASSERT_EQ(blink::mojom::PermissionStatus::DENIED,
            PermissionManager::Get(profile())->GetPermissionStatus(
                content::PermissionType::GEOLOCATION, requesting_frame,
                requesting_frame));
}

TEST_P(GeolocationPermissionContextTests, LSDBackOffAskPromptsDespiteBackOff) {
  base::test::ScopedFeatureList scoped_feature_list;
  EnableFeature(&scoped_feature_list, features::kLsdPermissionPrompt);

  GURL requesting_frame("https://www.example.com/geolocation");
  SetGeolocationContentSetting(requesting_frame, requesting_frame,
                               CONTENT_SETTING_ALLOW);

  // Turn off system location but allow the LSD to be shown, and denied.
  MockLocationSettings::SetLocationStatus(true /* android */,
                                          false /* system */);
  MockLocationSettings::SetLocationSettingsDialogStatus(true /* enabled */,
                                                        DENIED);

  // First, cancel a LSD prompt on the first non-DSE origin to go into backoff.
  EXPECT_TRUE(RequestPermissionIsLSDShown(requesting_frame));
  EXPECT_FALSE(RequestPermissionIsLSDShown(requesting_frame));

  // Set the content setting back to ASK. The permission status should be
  // prompt, and the LSD prompt should now be shown.
  SetGeolocationContentSetting(requesting_frame, requesting_frame,
                               CONTENT_SETTING_ASK);
  ASSERT_EQ(blink::mojom::PermissionStatus::ASK,
            PermissionManager::Get(profile())->GetPermissionStatus(
                content::PermissionType::GEOLOCATION, requesting_frame,
                requesting_frame));
  EXPECT_TRUE(
      RequestPermissionIsLSDShownWithPermissionPrompt(requesting_frame));
}

TEST_P(GeolocationPermissionContextTests,
       LSDBackOffAcceptPermissionResetsBackOff) {
  base::test::ScopedFeatureList scoped_feature_list;
  EnableFeature(&scoped_feature_list, features::kLsdPermissionPrompt);

  GURL requesting_frame("https://www.example.com/geolocation");
  SetGeolocationContentSetting(requesting_frame, requesting_frame,
                               CONTENT_SETTING_ALLOW);

  // Turn off system location but allow the LSD to be shown, and denied.
  MockLocationSettings::SetLocationStatus(true /* android */,
                                          false /* system */);
  MockLocationSettings::SetLocationSettingsDialogStatus(true /* enabled */,
                                                        DENIED);

  // First, get into the highest backoff state.
  EXPECT_TRUE(RequestPermissionIsLSDShown(requesting_frame));
  AddDayOffsetForTesting(7);
  EXPECT_TRUE(RequestPermissionIsLSDShown(requesting_frame));
  AddDayOffsetForTesting(30);
  EXPECT_TRUE(RequestPermissionIsLSDShown(requesting_frame));
  AddDayOffsetForTesting(90);
  EXPECT_TRUE(RequestPermissionIsLSDShown(requesting_frame));

  // Now accept a permissions prompt.
  SetGeolocationContentSetting(requesting_frame, requesting_frame,
                               CONTENT_SETTING_ASK);
  EXPECT_TRUE(
      RequestPermissionIsLSDShownWithPermissionPrompt(requesting_frame));

  // Denying the LSD stops the content setting from being stored, so explicitly
  // set it to ALLOW.
  SetGeolocationContentSetting(requesting_frame, requesting_frame,
                               CONTENT_SETTING_ALLOW);

  // And check that back in the lowest backoff state.
  EXPECT_FALSE(RequestPermissionIsLSDShown(requesting_frame));
  AddDayOffsetForTesting(7);
  EXPECT_TRUE(RequestPermissionIsLSDShown(requesting_frame));
}

TEST_P(GeolocationPermissionContextTests, LSDBackOffAcceptLSDResetsBackOff) {
  base::test::ScopedFeatureList scoped_feature_list;
  EnableFeature(&scoped_feature_list, features::kLsdPermissionPrompt);

  GURL requesting_frame("https://www.example.com/geolocation");
  SetGeolocationContentSetting(requesting_frame, requesting_frame,
                               CONTENT_SETTING_ALLOW);

  // Turn off system location but allow the LSD to be shown, and denied.
  MockLocationSettings::SetLocationStatus(true /* android */,
                                          false /* system */);
  MockLocationSettings::SetLocationSettingsDialogStatus(true /* enabled */,
                                                        DENIED);

  // First, get into the highest backoff state.
  EXPECT_TRUE(RequestPermissionIsLSDShown(requesting_frame));
  AddDayOffsetForTesting(7);
  EXPECT_TRUE(RequestPermissionIsLSDShown(requesting_frame));
  AddDayOffsetForTesting(30);
  EXPECT_TRUE(RequestPermissionIsLSDShown(requesting_frame));

  // Now accept the LSD.
  AddDayOffsetForTesting(90);
  MockLocationSettings::SetLocationSettingsDialogStatus(true /* enabled */,
                                                        GRANTED);
  EXPECT_TRUE(RequestPermissionIsLSDShown(requesting_frame));

  // Check that not in backoff, and that at the lowest backoff state.
  MockLocationSettings::SetLocationSettingsDialogStatus(true /* enabled */,
                                                        DENIED);
  EXPECT_TRUE(RequestPermissionIsLSDShown(requesting_frame));
  EXPECT_FALSE(RequestPermissionIsLSDShown(requesting_frame));
  AddDayOffsetForTesting(7);
  EXPECT_TRUE(RequestPermissionIsLSDShown(requesting_frame));
}
#endif

TEST_P(GeolocationPermissionContextTests, QueuedPermission) {
  GURL requesting_frame_0("https://www.example.com/geolocation");
  GURL requesting_frame_1("https://www.example-2.com/geolocation");
  EXPECT_EQ(
      CONTENT_SETTING_ASK,
      GetGeolocationContentSetting(requesting_frame_0, requesting_frame_1));
  EXPECT_EQ(
      CONTENT_SETTING_ASK,
      GetGeolocationContentSetting(requesting_frame_1, requesting_frame_1));

  NavigateAndCommit(requesting_frame_0);
  RequestManagerDocumentLoadCompleted();

  // Check that no permission requests have happened yet.
  EXPECT_FALSE(HasActivePrompt());

  // Request permission for two frames.
  RequestGeolocationPermission(
      web_contents(), RequestID(0), requesting_frame_0, true);
  RequestGeolocationPermission(
      web_contents(), RequestID(1), requesting_frame_1, true);
  // Ensure only one infobar is created.
  ASSERT_TRUE(HasActivePrompt());
  base::string16 text_0 = GetPromptText();

  // Accept the first frame.
  AcceptPrompt();
  CheckTabContentsState(requesting_frame_0, CONTENT_SETTING_ALLOW);
  CheckPermissionMessageSent(0, true);

  // Now we should have a new infobar for the second frame.
  ASSERT_TRUE(HasActivePrompt());
  base::string16 text_1 = GetPromptText();

  // Check that the messages differ.
  EXPECT_NE(text_0, text_1);

  // Cancel (block) this frame.
  DenyPrompt();
  CheckTabContentsState(requesting_frame_1, CONTENT_SETTING_BLOCK);
  CheckPermissionMessageSent(1, false);

  // Ensure the persisted permissions are ok.
  EXPECT_EQ(
      CONTENT_SETTING_ALLOW,
      GetGeolocationContentSetting(requesting_frame_0, requesting_frame_0));
  EXPECT_EQ(
      CONTENT_SETTING_BLOCK,
      GetGeolocationContentSetting(requesting_frame_1, requesting_frame_0));
}

TEST_P(GeolocationPermissionContextTests, HashIsIgnored) {
  GURL url_a("https://www.example.com/geolocation#a");
  GURL url_b("https://www.example.com/geolocation#b");

  // Navigate to the first url.
  NavigateAndCommit(url_a);
  RequestManagerDocumentLoadCompleted();

  // Check permission is requested.
  ASSERT_FALSE(HasActivePrompt());
  const bool user_gesture = true;
  RequestGeolocationPermission(web_contents(), RequestID(0), url_a,
                               user_gesture);
  ASSERT_TRUE(HasActivePrompt());

  // Change the hash, we'll still be on the same page.
  NavigateAndCommit(url_b);
  RequestManagerDocumentLoadCompleted();

  // Accept.
  AcceptPrompt();
  CheckTabContentsState(url_a, CONTENT_SETTING_ALLOW);
  CheckTabContentsState(url_b, CONTENT_SETTING_ALLOW);
  CheckPermissionMessageSent(0, true);
}

TEST_P(GeolocationPermissionContextTests, PermissionForFileScheme) {
  // TODO(felt): The bubble is rejecting file:// permission requests.
  // Fix and enable this test. crbug.com/444047
  if (GetParam() == TestType::PERMISSION_REQUEST_MANAGER)
    return;

  GURL requesting_frame("file://example/geolocation.html");
  NavigateAndCommit(requesting_frame);
  RequestManagerDocumentLoadCompleted();

  // Check permission is requested.
  ASSERT_FALSE(HasActivePrompt());
  RequestGeolocationPermission(web_contents(), RequestID(0), requesting_frame,
                               true);
  EXPECT_TRUE(HasActivePrompt());

  // Accept the frame.
  AcceptPrompt();
  CheckTabContentsState(requesting_frame, CONTENT_SETTING_ALLOW);
  CheckPermissionMessageSent(0, true);

  // Make sure the setting is not stored.
  EXPECT_EQ(CONTENT_SETTING_ASK,
            GetGeolocationContentSetting(requesting_frame, requesting_frame));
}

TEST_P(GeolocationPermissionContextTests, CancelGeolocationPermissionRequest) {
  GURL frame_0("https://www.example.com/geolocation");
  GURL frame_1("https://www.example-2.com/geolocation");
  EXPECT_EQ(
      CONTENT_SETTING_ASK, GetGeolocationContentSetting(frame_0, frame_0));
  EXPECT_EQ(
      CONTENT_SETTING_ASK, GetGeolocationContentSetting(frame_1, frame_0));

  NavigateAndCommit(frame_0);
  RequestManagerDocumentLoadCompleted();

  ASSERT_FALSE(HasActivePrompt());

  // Request permission for two frames.
  RequestGeolocationPermission(
      web_contents(), RequestID(0), frame_0, true);
  RequestGeolocationPermission(
      web_contents(), RequestID(1), frame_1, true);

  // Get the first permission request text.
  ASSERT_TRUE(HasActivePrompt());
  base::string16 text_0 = GetPromptText();
  ASSERT_FALSE(text_0.empty());

  // Simulate the frame going away; the request should be removed.
  ClosePrompt();

  // Check that the next pending request is created correctly.
  base::string16 text_1 = GetPromptText();
  EXPECT_NE(text_0, text_1);

  // Allow this frame and check that it worked.
  AcceptPrompt();
  CheckTabContentsState(frame_1, CONTENT_SETTING_ALLOW);
  CheckPermissionMessageSent(1, true);

  // Ensure the persisted permissions are ok.
  EXPECT_EQ(
      CONTENT_SETTING_ASK, GetGeolocationContentSetting(frame_0, frame_0));
  EXPECT_EQ(
      CONTENT_SETTING_ALLOW, GetGeolocationContentSetting(frame_1, frame_0));
}

TEST_P(GeolocationPermissionContextTests, InvalidURL) {
  // Navigate to the first url.
  GURL invalid_embedder("about:blank");
  GURL requesting_frame;
  NavigateAndCommit(invalid_embedder);
  RequestManagerDocumentLoadCompleted();

  // Nothing should be displayed.
  EXPECT_FALSE(HasActivePrompt());
  RequestGeolocationPermission(
      web_contents(), RequestID(0), requesting_frame, true);
  EXPECT_FALSE(HasActivePrompt());
  CheckPermissionMessageSent(0, false);
}

TEST_P(GeolocationPermissionContextTests, SameOriginMultipleTabs) {
  GURL url_a("https://www.example.com/geolocation");
  GURL url_b("https://www.example-2.com/geolocation");
  NavigateAndCommit(url_a);  // Tab A0
  AddNewTab(url_b);          // Tab B (extra_tabs_[0])
  AddNewTab(url_a);          // Tab A1 (extra_tabs_[1])
  RequestManagerDocumentLoadCompleted();
  RequestManagerDocumentLoadCompleted(extra_tabs_[0].get());
  RequestManagerDocumentLoadCompleted(extra_tabs_[1].get());

  // Request permission in all three tabs.
  RequestGeolocationPermission(
      web_contents(), RequestID(0), url_a, true);
  RequestGeolocationPermission(
      extra_tabs_[0].get(), RequestIDForTab(0, 0), url_b, true);
  RequestGeolocationPermission(
      extra_tabs_[1].get(), RequestIDForTab(1, 0), url_a, true);
  ASSERT_TRUE(HasActivePrompt());  // For A0.
  ASSERT_TRUE(HasActivePrompt(extra_tabs_[0].get()));
  ASSERT_TRUE(HasActivePrompt(extra_tabs_[1].get()));

  // Accept the permission in tab A0.
  AcceptPrompt();
  if (GetParam() == TestType::PERMISSION_QUEUE_CONTROLLER) {
    EXPECT_EQ(2U, closed_infobar_tracker_.size());
  }
  CheckPermissionMessageSent(0, true);
  // Because they're the same origin, this will cause tab A1's infobar to
  // disappear. It does not cause the bubble to disappear: crbug.com/443013.
  // TODO(felt): Update this test when the bubble's behavior is changed.
  // Either way, tab B should still have a pending permission request.
  ASSERT_TRUE(HasActivePrompt(extra_tabs_[0].get()));
  if (GetParam() == TestType::PERMISSION_REQUEST_MANAGER) {
    ASSERT_TRUE(HasActivePrompt(extra_tabs_[1].get()));
  } else {
    CheckPermissionMessageSentForTab(1, 0, true);
  }
}

TEST_P(GeolocationPermissionContextTests, QueuedOriginMultipleTabs) {
  GURL url_a("https://www.example.com/geolocation");
  GURL url_b("https://www.example-2.com/geolocation");
  NavigateAndCommit(url_a);  // Tab A0.
  AddNewTab(url_a);          // Tab A1.
  RequestManagerDocumentLoadCompleted();
  RequestManagerDocumentLoadCompleted(extra_tabs_[0].get());

  // Request permission in both tabs; the extra tab will have two permission
  // requests from two origins.
  RequestGeolocationPermission(
      web_contents(), RequestID(0), url_a, true);
  RequestGeolocationPermission(
      extra_tabs_[0].get(), RequestIDForTab(0, 0), url_a, true);
  RequestGeolocationPermission(
      extra_tabs_[0].get(), RequestIDForTab(0, 1), url_b, true);

  ASSERT_TRUE(HasActivePrompt());
  ASSERT_TRUE(HasActivePrompt(extra_tabs_[0].get()));

  // Accept the first request in tab A1.
  AcceptPrompt(extra_tabs_[0].get());
  if (GetParam() == TestType::PERMISSION_QUEUE_CONTROLLER) {
    EXPECT_EQ(2U, closed_infobar_tracker_.size());
  }
  CheckPermissionMessageSentForTab(0, 0, true);

  // Because they're the same origin, this will cause tab A0's infobar to
  // disappear. It does not cause the bubble to disappear: crbug.com/443013.
  // TODO(felt): Update this test when the bubble's behavior is changed.
  if (GetParam() == TestType::PERMISSION_REQUEST_MANAGER) {
    EXPECT_TRUE(HasActivePrompt());
  } else {
    EXPECT_FALSE(HasActivePrompt());
    CheckPermissionMessageSent(0, true);
  }

  // The second request should now be visible in tab A1.
  ASSERT_TRUE(HasActivePrompt(extra_tabs_[0].get()));

  // Accept the second request and check that it's gone.
  AcceptPrompt(extra_tabs_[0].get());
  EXPECT_FALSE(HasActivePrompt(extra_tabs_[0].get()));
}

TEST_P(GeolocationPermissionContextTests, TabDestroyed) {
  GURL requesting_frame_0("https://www.example.com/geolocation");
  GURL requesting_frame_1("https://www.example-2.com/geolocation");
  EXPECT_EQ(
      CONTENT_SETTING_ASK,
      GetGeolocationContentSetting(requesting_frame_0, requesting_frame_0));
  EXPECT_EQ(
      CONTENT_SETTING_ASK,
      GetGeolocationContentSetting(requesting_frame_1, requesting_frame_0));

  NavigateAndCommit(requesting_frame_0);
  RequestManagerDocumentLoadCompleted();

  // Request permission for two frames.
  RequestGeolocationPermission(
      web_contents(), RequestID(0), requesting_frame_0, false);
  RequestGeolocationPermission(
      web_contents(), RequestID(1), requesting_frame_1, false);

  // Ensure only one prompt is created.
  ASSERT_TRUE(HasActivePrompt());

  // Delete the tab contents.
  if (GetParam() == TestType::PERMISSION_QUEUE_CONTROLLER) {
    infobars::InfoBar* infobar = infobar_service()->infobar_at(0);
    DeleteContents();
    ASSERT_EQ(1U, closed_infobar_tracker_.size());
    ASSERT_TRUE(closed_infobar_tracker_.Contains(infobar));
  }

  // The content settings should not have changed.
  EXPECT_EQ(
      CONTENT_SETTING_ASK,
      GetGeolocationContentSetting(requesting_frame_0, requesting_frame_0));
  EXPECT_EQ(
      CONTENT_SETTING_ASK,
      GetGeolocationContentSetting(requesting_frame_1, requesting_frame_0));
}

#if defined(OS_ANDROID)
TEST_P(GeolocationPermissionContextTests, SearchGeolocationInIncognito) {
  base::test::ScopedFeatureList scoped_feature_list;
  EnableFeature(&scoped_feature_list, features::kConsistentOmniboxGeolocation);

  GURL requesting_frame(TestSearchEngineDelegate::kDSETestUrl);
  // The DSE Geolocation setting should be used in incognito if it is BLOCK,
  // but not if it is ALLOW.
  SearchGeolocationService* geo_service =
      SearchGeolocationService::Factory::GetForBrowserContext(profile());
  geo_service->SetSearchEngineDelegateForTest(
      base::MakeUnique<TestSearchEngineDelegate>());

  Profile* otr_profile = profile()->GetOffTheRecordProfile();

  // A DSE setting of ALLOW should not flow through to incognito.
  geo_service->SetDSEGeolocationSetting(true);
  ASSERT_EQ(CONTENT_SETTING_ASK,
            PermissionManager::Get(otr_profile)
                ->GetPermissionStatus(CONTENT_SETTINGS_TYPE_GEOLOCATION,
                                      requesting_frame, requesting_frame)
                .content_setting);

  // Changing the setting to BLOCK should flow through to incognito.
  geo_service->SetDSEGeolocationSetting(false);
  ASSERT_EQ(CONTENT_SETTING_BLOCK,
            PermissionManager::Get(otr_profile)
                ->GetPermissionStatus(CONTENT_SETTINGS_TYPE_GEOLOCATION,
                                      requesting_frame, requesting_frame)
                .content_setting);
}

TEST_P(GeolocationPermissionContextTests,
       GeolocationStatusAndroidDisabledLegacy) {
  GURL requesting_frame("https://www.example.com/geolocation");

  // In these tests the Android permission status should not be taken into
  // account, only the content setting.
  SetGeolocationContentSetting(requesting_frame, requesting_frame,
                               CONTENT_SETTING_ALLOW);
  MockLocationSettings::SetLocationStatus(false /* android */,
                                          true /* system */);
  ASSERT_EQ(blink::mojom::PermissionStatus::GRANTED,
            PermissionManager::Get(profile())->GetPermissionStatus(
                content::PermissionType::GEOLOCATION, requesting_frame,
                requesting_frame));

  SetGeolocationContentSetting(requesting_frame, requesting_frame,
                               CONTENT_SETTING_ASK);
  ASSERT_EQ(blink::mojom::PermissionStatus::ASK,
            PermissionManager::Get(profile())->GetPermissionStatus(
                content::PermissionType::GEOLOCATION, requesting_frame,
                requesting_frame));

  SetGeolocationContentSetting(requesting_frame, requesting_frame,
                               CONTENT_SETTING_BLOCK);
  ASSERT_EQ(blink::mojom::PermissionStatus::DENIED,
            PermissionManager::Get(profile())->GetPermissionStatus(
                content::PermissionType::GEOLOCATION, requesting_frame,
                requesting_frame));
}

TEST_P(GeolocationPermissionContextTests, GeolocationStatusAndroidDisabled) {
  base::test::ScopedFeatureList scoped_feature_list;
  EnableFeature(&scoped_feature_list, features::kLsdPermissionPrompt);

  GURL requesting_frame("https://www.example.com/geolocation");

  // With the Android permission off, but location allowed for a domain, the
  // permission status should be ASK.
  SetGeolocationContentSetting(requesting_frame, requesting_frame,
                               CONTENT_SETTING_ALLOW);
  MockLocationSettings::SetLocationStatus(false /* android */,
                                          true /* system */);
  ASSERT_EQ(blink::mojom::PermissionStatus::ASK,
            PermissionManager::Get(profile())->GetPermissionStatus(
                content::PermissionType::GEOLOCATION, requesting_frame,
                requesting_frame));

  // With the Android permission off, and location blocked for a domain, the
  // permission status should still be BLOCK.
  SetGeolocationContentSetting(requesting_frame, requesting_frame,
                               CONTENT_SETTING_BLOCK);
  ASSERT_EQ(blink::mojom::PermissionStatus::DENIED,
            PermissionManager::Get(profile())->GetPermissionStatus(
                content::PermissionType::GEOLOCATION, requesting_frame,
                requesting_frame));

  // With the Android permission off, and location prompt for a domain, the
  // permission status should still be ASK.
  SetGeolocationContentSetting(requesting_frame, requesting_frame,
                               CONTENT_SETTING_ASK);
  ASSERT_EQ(blink::mojom::PermissionStatus::ASK,
            PermissionManager::Get(profile())->GetPermissionStatus(
                content::PermissionType::GEOLOCATION, requesting_frame,
                requesting_frame));
}

TEST_P(GeolocationPermissionContextTests,
       GeolocationStatusSystemDisabledLegacy) {
  GURL requesting_frame("https://www.example.com/geolocation");

  // In these tests the system permission status should not be taken into
  // account, only the content setting.
  SetGeolocationContentSetting(requesting_frame, requesting_frame,
                               CONTENT_SETTING_ALLOW);
  MockLocationSettings::SetLocationStatus(true /* android */,
                                          false /* system */);
  ASSERT_EQ(blink::mojom::PermissionStatus::GRANTED,
            PermissionManager::Get(profile())->GetPermissionStatus(
                content::PermissionType::GEOLOCATION, requesting_frame,
                requesting_frame));

  SetGeolocationContentSetting(requesting_frame, requesting_frame,
                               CONTENT_SETTING_ASK);
  ASSERT_EQ(blink::mojom::PermissionStatus::ASK,
            PermissionManager::Get(profile())->GetPermissionStatus(
                content::PermissionType::GEOLOCATION, requesting_frame,
                requesting_frame));

  SetGeolocationContentSetting(requesting_frame, requesting_frame,
                               CONTENT_SETTING_BLOCK);
  ASSERT_EQ(blink::mojom::PermissionStatus::DENIED,
            PermissionManager::Get(profile())->GetPermissionStatus(
                content::PermissionType::GEOLOCATION, requesting_frame,
                requesting_frame));
}

TEST_P(GeolocationPermissionContextTests, GeolocationStatusSystemDisabled) {
  base::test::ScopedFeatureList scoped_feature_list;
  EnableFeature(&scoped_feature_list, features::kLsdPermissionPrompt);

  GURL requesting_frame("https://www.example.com/geolocation");

  // With the system permission off, but location allowed for a domain, the
  // permission status should be reflect whether the LSD can be shown.
  SetGeolocationContentSetting(requesting_frame, requesting_frame,
                               CONTENT_SETTING_ALLOW);
  MockLocationSettings::SetLocationStatus(true /* android */,
                                          false /* system */);
  MockLocationSettings::SetLocationSettingsDialogStatus(true /* enabled */,
                                                        DENIED);
  ASSERT_EQ(blink::mojom::PermissionStatus::ASK,
            PermissionManager::Get(profile())->GetPermissionStatus(
                content::PermissionType::GEOLOCATION, requesting_frame,
                requesting_frame));

  MockLocationSettings::SetLocationSettingsDialogStatus(false /* enabled */,
                                                        GRANTED);
  ASSERT_EQ(blink::mojom::PermissionStatus::DENIED,
            PermissionManager::Get(profile())->GetPermissionStatus(
                content::PermissionType::GEOLOCATION, requesting_frame,
                requesting_frame));

  // The result should be the same if the location permission is ASK.
  SetGeolocationContentSetting(requesting_frame, requesting_frame,
                               CONTENT_SETTING_ASK);
  MockLocationSettings::SetLocationSettingsDialogStatus(true /* enabled */,
                                                        GRANTED);
  ASSERT_EQ(blink::mojom::PermissionStatus::ASK,
            PermissionManager::Get(profile())->GetPermissionStatus(
                content::PermissionType::GEOLOCATION, requesting_frame,
                requesting_frame));

  MockLocationSettings::SetLocationSettingsDialogStatus(false /* enabled */,
                                                        GRANTED);
  ASSERT_EQ(blink::mojom::PermissionStatus::DENIED,
            PermissionManager::Get(profile())->GetPermissionStatus(
                content::PermissionType::GEOLOCATION, requesting_frame,
                requesting_frame));

  // With the Android permission off, and location blocked for a domain, the
  // permission status should still be BLOCK.
  SetGeolocationContentSetting(requesting_frame, requesting_frame,
                               CONTENT_SETTING_BLOCK);
  MockLocationSettings::SetLocationSettingsDialogStatus(true /* enabled */,
                                                        GRANTED);
  ASSERT_EQ(blink::mojom::PermissionStatus::DENIED,
            PermissionManager::Get(profile())->GetPermissionStatus(
                content::PermissionType::GEOLOCATION, requesting_frame,
                requesting_frame));
}
#endif  // defined(OS_ANDROID)

#if defined(OS_ANDROID)
INSTANTIATE_TEST_CASE_P(
    GeolocationPermissionContextTestsInstance,
    GeolocationPermissionContextTests,
    ::testing::Values(TestType::PERMISSION_REQUEST_MANAGER,
                      TestType::PERMISSION_QUEUE_CONTROLLER));
#else
INSTANTIATE_TEST_CASE_P(
    GeolocationPermissionContextTestsInstance,
    GeolocationPermissionContextTests,
    ::testing::Values(TestType::PERMISSION_REQUEST_MANAGER));
#endif
