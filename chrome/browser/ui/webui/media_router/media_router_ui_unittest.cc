// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/media_router/media_router_ui.h"

#include <utility>

#include "base/bind.h"
#include "base/memory/ptr_util.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/media/router/create_presentation_connection_request.h"
#include "chrome/browser/media/router/mock_media_router.h"
#include "chrome/browser/media/router/mojo/media_router_mojo_test.h"
#include "chrome/browser/media/router/test_helper.h"
#include "chrome/browser/sessions/session_tab_helper.h"
#include "chrome/browser/ui/webui/media_router/media_router_webui_message_handler.h"
#include "chrome/common/media_router/media_route.h"
#include "chrome/common/media_router/media_source_helper.h"
#include "chrome/common/media_router/route_request_result.h"
#include "chrome/grit/generated_resources.h"
#include "chrome/test/base/chrome_render_view_host_test_harness.h"
#include "chrome/test/base/testing_profile.h"
#include "content/public/test/test_web_ui.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/common/extension.h"
#include "extensions/common/extension_builder.h"
#include "extensions/common/test_util.h"
#include "extensions/common/value_builder.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/l10n/l10n_util.h"

using content::WebContents;
using testing::_;
using testing::AnyNumber;
using testing::Invoke;
using testing::Mock;
using testing::Return;
using testing::SaveArg;

namespace media_router {

class MockMediaRouterWebUIMessageHandler
    : public MediaRouterWebUIMessageHandler {
 public:
  explicit MockMediaRouterWebUIMessageHandler(MediaRouterUI* media_router_ui)
      : MediaRouterWebUIMessageHandler(media_router_ui) {}
  ~MockMediaRouterWebUIMessageHandler() override {}

  MOCK_METHOD1(UpdateMediaRouteStatus, void(const MediaStatus& status));
  MOCK_METHOD3(UpdateCastModes,
               void(const CastModeSet& cast_modes,
                    const std::string& source_host,
                    base::Optional<MediaCastMode> forced_cast_mode));
};

class PresentationRequestCallbacks {
 public:
  PresentationRequestCallbacks()
      : expected_error_(content::PresentationError(
            content::PresentationErrorType::PRESENTATION_ERROR_UNKNOWN,
            "")) {}

  explicit PresentationRequestCallbacks(
      const content::PresentationError& expected_error)
      : expected_error_(expected_error) {}

  void Success(const content::PresentationInfo&, const MediaRoute&) {}

  void Error(const content::PresentationError& error) {
    EXPECT_EQ(expected_error_.error_type, error.error_type);
    EXPECT_EQ(expected_error_.message, error.message);
  }

 private:
  content::PresentationError expected_error_;
};

class MediaRouterUITest : public ChromeRenderViewHostTestHarness {
 public:
  MediaRouterUITest() {
    ON_CALL(mock_router_, GetCurrentRoutes())
        .WillByDefault(Return(std::vector<MediaRoute>()));
  }

  void TearDown() override {
    EXPECT_CALL(mock_router_, UnregisterMediaSinksObserver(_))
        .Times(AnyNumber());
    EXPECT_CALL(mock_router_, UnregisterMediaRoutesObserver(_))
        .Times(AnyNumber());
    web_ui_contents_.reset();
    create_session_request_.reset();
    media_router_ui_.reset();
    message_handler_.reset();
    ChromeRenderViewHostTestHarness::TearDown();
  }

  void CreateMediaRouterUIForURL(Profile* profile, const GURL& url) {
    web_contents()->GetController().LoadURL(url, content::Referrer(),
                                            ui::PAGE_TRANSITION_LINK, "");
    content::RenderFrameHostTester::CommitPendingLoad(
        &web_contents()->GetController());
    CreateMediaRouterUI(profile);
  }

  void CreateMediaRouterUI(Profile* profile) {
    SessionTabHelper::CreateForWebContents(web_contents());
    web_ui_contents_.reset(
        WebContents::Create(WebContents::CreateParams(profile)));
    web_ui_.set_web_contents(web_ui_contents_.get());
    media_router_ui_ = base::MakeUnique<MediaRouterUI>(&web_ui_);
    message_handler_ = base::MakeUnique<MockMediaRouterWebUIMessageHandler>(
        media_router_ui_.get());
    EXPECT_CALL(mock_router_, RegisterMediaSinksObserver(_))
        .WillRepeatedly(Invoke([this](MediaSinksObserver* observer) {
          this->media_sinks_observers_.push_back(observer);
          return true;
        }));
    EXPECT_CALL(mock_router_, RegisterMediaRoutesObserver(_))
        .Times(AnyNumber());
    media_router_ui_->InitForTest(&mock_router_, web_contents(),
                                  message_handler_.get(),
                                  std::move(create_session_request_));
    message_handler_->SetWebUIForTest(&web_ui_);
  }

  MediaSink CreateSinkCompatibleWithAllSources() {
    MediaSink sink("sinkId", "sinkName", MediaSink::GENERIC);
    for (auto* observer : media_sinks_observers_)
      observer->OnSinksUpdated({sink}, std::vector<url::Origin>());
    return sink;
  }

  scoped_refptr<MockMediaRouteController> CreateMediaRouteController(
      const MediaRoute::Id& route_id) {
    mojom::MediaControllerPtr mojo_media_controller;
    mojo::MakeRequest(&mojo_media_controller);
    return scoped_refptr<MockMediaRouteController>(new MockMediaRouteController(
        route_id, std::move(mojo_media_controller), &mock_router_));
  }

  // Notifies MediaRouterUI that a route details view has been opened. Expects
  // MediaRouterUI to request a MediaRouteController, and gives it a mock
  // controller. Returns a reference to the mock controller.
  scoped_refptr<MockMediaRouteController> OpenUIDetailsView(
      const MediaRoute::Id& route_id) {
    auto controller = CreateMediaRouteController(route_id);
    MediaSource media_source("mediaSource");
    MediaRoute route(route_id, media_source, "sinkId", "", true, "", true);

    media_router_ui_->OnRoutesUpdated({route}, std::vector<MediaRoute::Id>());
    EXPECT_CALL(mock_router_, GetRouteController(route_id))
        .WillOnce(Return(controller));
    media_router_ui_->OnMediaControllerUIAvailable(route_id);

    return controller;
  }

 protected:
  MockMediaRouter mock_router_;
  content::TestWebUI web_ui_;
  std::unique_ptr<WebContents> web_ui_contents_;
  std::unique_ptr<CreatePresentationConnectionRequest> create_session_request_;
  std::unique_ptr<MediaRouterUI> media_router_ui_;
  std::unique_ptr<MockMediaRouterWebUIMessageHandler> message_handler_;
  std::vector<MediaSinksObserver*> media_sinks_observers_;
};

TEST_F(MediaRouterUITest, RouteCreationTimeoutForTab) {
  CreateMediaRouterUI(profile());
  std::vector<MediaRouteResponseCallback> callbacks;
  EXPECT_CALL(
      mock_router_,
      CreateRoute(_, _, _, _, _, base::TimeDelta::FromSeconds(60), false))
      .WillOnce(SaveArg<4>(&callbacks));
  media_router_ui_->CreateRoute(CreateSinkCompatibleWithAllSources().id(),
                                MediaCastMode::TAB_MIRROR);

  std::string expected_title = l10n_util::GetStringUTF8(
      IDS_MEDIA_ROUTER_ISSUE_CREATE_ROUTE_TIMEOUT_FOR_TAB);
  EXPECT_CALL(mock_router_, AddIssue(IssueTitleEquals(expected_title)));
  std::unique_ptr<RouteRequestResult> result =
      RouteRequestResult::FromError("Timed out", RouteRequestResult::TIMED_OUT);
  for (const auto& callback : callbacks)
    callback.Run(*result);
}

TEST_F(MediaRouterUITest, RouteCreationTimeoutForDesktop) {
  CreateMediaRouterUI(profile());
  std::vector<MediaRouteResponseCallback> callbacks;
  EXPECT_CALL(
      mock_router_,
      CreateRoute(_, _, _, _, _, base::TimeDelta::FromSeconds(120), false))
      .WillOnce(SaveArg<4>(&callbacks));
  media_router_ui_->CreateRoute(CreateSinkCompatibleWithAllSources().id(),
                                MediaCastMode::DESKTOP_MIRROR);

  std::string expected_title = l10n_util::GetStringUTF8(
      IDS_MEDIA_ROUTER_ISSUE_CREATE_ROUTE_TIMEOUT_FOR_DESKTOP);
  EXPECT_CALL(mock_router_, AddIssue(IssueTitleEquals(expected_title)));
  std::unique_ptr<RouteRequestResult> result =
      RouteRequestResult::FromError("Timed out", RouteRequestResult::TIMED_OUT);
  for (const auto& callback : callbacks)
    callback.Run(*result);
}

TEST_F(MediaRouterUITest, RouteCreationTimeoutForPresentation) {
  CreateMediaRouterUI(profile());
  PresentationRequest presentation_request(
      RenderFrameHostId(0, 0), {GURL("https://presentationurl.com")},
      url::Origin(GURL("https://frameurl.fakeurl")));
  media_router_ui_->OnDefaultPresentationChanged(presentation_request);
  std::vector<MediaRouteResponseCallback> callbacks;
  EXPECT_CALL(
      mock_router_,
      CreateRoute(_, _, _, _, _, base::TimeDelta::FromSeconds(20), false))
      .WillOnce(SaveArg<4>(&callbacks));
  media_router_ui_->CreateRoute(CreateSinkCompatibleWithAllSources().id(),
                                MediaCastMode::DEFAULT);

  std::string expected_title =
      l10n_util::GetStringFUTF8(IDS_MEDIA_ROUTER_ISSUE_CREATE_ROUTE_TIMEOUT,
                                base::UTF8ToUTF16("frameurl.fakeurl"));
  EXPECT_CALL(mock_router_, AddIssue(IssueTitleEquals(expected_title)));
  std::unique_ptr<RouteRequestResult> result =
      RouteRequestResult::FromError("Timed out", RouteRequestResult::TIMED_OUT);
  for (const auto& callback : callbacks)
    callback.Run(*result);
}

TEST_F(MediaRouterUITest, RouteCreationParametersCantBeCreated) {
  CreateMediaRouterUI(profile());
  MediaSinkSearchResponseCallback sink_callback;
  EXPECT_CALL(mock_router_, SearchSinks(_, _, _, _, _))
      .WillOnce(SaveArg<4>(&sink_callback));

  // Use DEFAULT mode without setting a PresentationRequest.
  media_router_ui_->SearchSinksAndCreateRoute("sinkId", "search input",
                                              "domain", MediaCastMode::DEFAULT);
  std::string expected_title = l10n_util::GetStringUTF8(
      IDS_MEDIA_ROUTER_ISSUE_CREATE_ROUTE_TIMEOUT_FOR_TAB);
  EXPECT_CALL(mock_router_, AddIssue(IssueTitleEquals(expected_title)));
  sink_callback.Run("foundSinkId");
}

TEST_F(MediaRouterUITest, RouteRequestFromIncognito) {
  CreateMediaRouterUI(profile()->GetOffTheRecordProfile());

  PresentationRequest presentation_request(
      RenderFrameHostId(0, 0), {GURL("https://foo.url.com/")},
      url::Origin(GURL("https://frameUrl")));
  media_router_ui_->OnDefaultPresentationChanged(presentation_request);

  EXPECT_CALL(
      mock_router_,
      CreateRoute(_, _, _, _, _, base::TimeDelta::FromSeconds(20), true));
  media_router_ui_->CreateRoute(CreateSinkCompatibleWithAllSources().id(),
                                MediaCastMode::DEFAULT);
}

TEST_F(MediaRouterUITest, SortedSinks) {
  CreateMediaRouterUI(profile());
  std::vector<MediaSinkWithCastModes> unsorted_sinks;
  std::string sink_id1("sink3");
  std::string sink_name1("B sink");
  MediaSinkWithCastModes sink1(
      MediaSink(sink_id1, sink_name1, MediaSink::IconType::CAST));
  unsorted_sinks.push_back(sink1);

  std::string sink_id2("sink1");
  std::string sink_name2("A sink");
  MediaSinkWithCastModes sink2(
      MediaSink(sink_id2, sink_name2, MediaSink::IconType::CAST));
  unsorted_sinks.push_back(sink2);

  std::string sink_id3("sink2");
  std::string sink_name3("B sink");
  MediaSinkWithCastModes sink3(
      MediaSink(sink_id3, sink_name3, MediaSink::IconType::CAST));
  unsorted_sinks.push_back(sink3);

  // Sorted order is 2, 3, 1.
  media_router_ui_->OnResultsUpdated(unsorted_sinks);
  const auto& sorted_sinks = media_router_ui_->sinks_;
  EXPECT_EQ(sink_name2, sorted_sinks[0].sink.name());
  EXPECT_EQ(sink_id3, sorted_sinks[1].sink.id());
  EXPECT_EQ(sink_id1, sorted_sinks[2].sink.id());
}

TEST_F(MediaRouterUITest, SortSinksByIconType) {
  CreateMediaRouterUI(profile());
  std::vector<MediaSinkWithCastModes> unsorted_sinks;

  MediaSinkWithCastModes sink1(
      MediaSink("id1", "sink", MediaSink::IconType::HANGOUT));
  unsorted_sinks.push_back(sink1);
  MediaSinkWithCastModes sink2(
      MediaSink("id2", "B sink", MediaSink::IconType::CAST_AUDIO_GROUP));
  unsorted_sinks.push_back(sink2);
  MediaSinkWithCastModes sink3(
      MediaSink("id3", "sink", MediaSink::IconType::GENERIC));
  unsorted_sinks.push_back(sink3);
  MediaSinkWithCastModes sink4(
      MediaSink("id4", "A sink", MediaSink::IconType::CAST_AUDIO_GROUP));
  unsorted_sinks.push_back(sink4);
  MediaSinkWithCastModes sink5(
      MediaSink("id5", "sink", MediaSink::IconType::CAST_AUDIO));
  unsorted_sinks.push_back(sink5);
  MediaSinkWithCastModes sink6(
      MediaSink("id6", "sink", MediaSink::IconType::CAST));
  unsorted_sinks.push_back(sink6);

  // Sorted order is CAST, CAST_AUDIO_GROUP "A", CAST_AUDIO_GROUP "B",
  // CAST_AUDIO, HANGOUT, GENERIC.
  media_router_ui_->OnResultsUpdated(unsorted_sinks);
  const auto& sorted_sinks = media_router_ui_->sinks_;
  EXPECT_EQ(sink6.sink.id(), sorted_sinks[0].sink.id());
  EXPECT_EQ(sink4.sink.id(), sorted_sinks[1].sink.id());
  EXPECT_EQ(sink2.sink.id(), sorted_sinks[2].sink.id());
  EXPECT_EQ(sink5.sink.id(), sorted_sinks[3].sink.id());
  EXPECT_EQ(sink1.sink.id(), sorted_sinks[4].sink.id());
  EXPECT_EQ(sink3.sink.id(), sorted_sinks[5].sink.id());
}

TEST_F(MediaRouterUITest, FilterNonDisplayRoutes) {
  CreateMediaRouterUI(profile());

  MediaSource media_source("mediaSource");
  MediaRoute display_route_1("routeId1", media_source, "sinkId1", "desc 1",
                             true, "", true);
  MediaRoute non_display_route_1("routeId2", media_source, "sinkId2", "desc 2",
                                 true, "", false);
  MediaRoute display_route_2("routeId3", media_source, "sinkId2", "desc 2",
                             true, "", true);
  std::vector<MediaRoute> routes;
  routes.push_back(display_route_1);
  routes.push_back(non_display_route_1);
  routes.push_back(display_route_2);

  media_router_ui_->OnRoutesUpdated(routes, std::vector<MediaRoute::Id>());
  ASSERT_EQ(2u, media_router_ui_->routes_.size());
  EXPECT_TRUE(display_route_1.Equals(media_router_ui_->routes_[0]));
  EXPECT_TRUE(media_router_ui_->routes_[0].for_display());
  EXPECT_TRUE(display_route_2.Equals(media_router_ui_->routes_[1]));
  EXPECT_TRUE(media_router_ui_->routes_[1].for_display());
}

TEST_F(MediaRouterUITest, FilterNonDisplayJoinableRoutes) {
  CreateMediaRouterUI(profile());

  MediaSource media_source("mediaSource");
  MediaRoute display_route_1("routeId1", media_source, "sinkId1", "desc 1",
                             true, "", true);
  MediaRoute non_display_route_1("routeId2", media_source, "sinkId2", "desc 2",
                                 true, "", false);
  MediaRoute display_route_2("routeId3", media_source, "sinkId2", "desc 2",
                             true, "", true);
  std::vector<MediaRoute> routes;
  routes.push_back(display_route_1);
  routes.push_back(non_display_route_1);
  routes.push_back(display_route_2);

  std::vector<MediaRoute::Id> joinable_route_ids;
  joinable_route_ids.push_back("routeId1");
  joinable_route_ids.push_back("routeId2");
  joinable_route_ids.push_back("routeId3");

  media_router_ui_->OnRoutesUpdated(routes, joinable_route_ids);
  ASSERT_EQ(2u, media_router_ui_->joinable_route_ids_.size());
  EXPECT_EQ(display_route_1.media_route_id(),
            media_router_ui_->joinable_route_ids_[0]);
  EXPECT_EQ(display_route_2.media_route_id(),
            media_router_ui_->joinable_route_ids_[1]);
}

TEST_F(MediaRouterUITest, UIMediaRoutesObserverAssignsCurrentCastModes) {
  CreateMediaRouterUI(profile());
  SessionID::id_type tab_id = SessionTabHelper::IdForTab(web_contents());
  MediaSource media_source_1(MediaSourceForTab(tab_id));
  MediaSource media_source_2("mediaSource");
  MediaSource media_source_3(MediaSourceForDesktop());
  std::unique_ptr<MediaRouterUI::UIMediaRoutesObserver> observer(
      new MediaRouterUI::UIMediaRoutesObserver(
          &mock_router_, MediaSource::Id(),
          base::Bind(&MediaRouterUI::OnRoutesUpdated,
                     base::Unretained(media_router_ui_.get()))));

  MediaRoute display_route_1("routeId1", media_source_1, "sinkId1", "desc 1",
                             true, "", true);
  MediaRoute non_display_route_1("routeId2", media_source_2, "sinkId2",
                                 "desc 2", true, "", false);
  MediaRoute display_route_2("routeId3", media_source_3, "sinkId2", "desc 2",
                             true, "", true);
  std::vector<MediaRoute> routes;
  routes.push_back(display_route_1);
  routes.push_back(non_display_route_1);
  routes.push_back(display_route_2);

  observer->OnRoutesUpdated(routes, std::vector<MediaRoute::Id>());

  const auto& filtered_routes = media_router_ui_->routes();
  ASSERT_EQ(2u, filtered_routes.size());
  EXPECT_TRUE(display_route_1.Equals(filtered_routes[0]));
  EXPECT_TRUE(filtered_routes[0].for_display());
  EXPECT_TRUE(display_route_2.Equals(filtered_routes[1]));
  EXPECT_TRUE(filtered_routes[1].for_display());

  const auto& current_cast_modes = media_router_ui_->routes_and_cast_modes();
  ASSERT_EQ(2u, current_cast_modes.size());
  auto cast_mode_entry =
      current_cast_modes.find(display_route_1.media_route_id());
  EXPECT_NE(end(current_cast_modes), cast_mode_entry);
  EXPECT_EQ(MediaCastMode::TAB_MIRROR, cast_mode_entry->second);
  cast_mode_entry =
      current_cast_modes.find(non_display_route_1.media_route_id());
  EXPECT_EQ(end(current_cast_modes), cast_mode_entry);
  cast_mode_entry = current_cast_modes.find(display_route_2.media_route_id());
  EXPECT_NE(end(current_cast_modes), cast_mode_entry);
  EXPECT_EQ(MediaCastMode::DESKTOP_MIRROR, cast_mode_entry->second);

  EXPECT_CALL(mock_router_, UnregisterMediaRoutesObserver(_)).Times(1);
  observer.reset();
}

TEST_F(MediaRouterUITest, UIMediaRoutesObserverSkipsUnavailableCastModes) {
  CreateMediaRouterUI(profile());
  MediaSource media_source_1("mediaSource1");
  MediaSource media_source_2("mediaSource2");
  MediaSource media_source_3(MediaSourceForDesktop());
  std::unique_ptr<MediaRouterUI::UIMediaRoutesObserver> observer(
      new MediaRouterUI::UIMediaRoutesObserver(
          &mock_router_, MediaSource::Id(),
          base::Bind(&MediaRouterUI::OnRoutesUpdated,
                     base::Unretained(media_router_ui_.get()))));

  MediaRoute display_route_1("routeId1", media_source_1, "sinkId1", "desc 1",
                             true, "", true);
  MediaRoute non_display_route_1("routeId2", media_source_2, "sinkId2",
                                 "desc 2", true, "", false);
  MediaRoute display_route_2("routeId3", media_source_3, "sinkId2", "desc 2",
                             true, "", true);
  std::vector<MediaRoute> routes;
  routes.push_back(display_route_1);
  routes.push_back(non_display_route_1);
  routes.push_back(display_route_2);

  observer->OnRoutesUpdated(routes, std::vector<MediaRoute::Id>());

  const auto& filtered_routes = media_router_ui_->routes();
  ASSERT_EQ(2u, filtered_routes.size());
  EXPECT_TRUE(display_route_1.Equals(filtered_routes[0]));
  EXPECT_TRUE(filtered_routes[0].for_display());
  EXPECT_TRUE(display_route_2.Equals(filtered_routes[1]));
  EXPECT_TRUE(filtered_routes[1].for_display());

  const auto& current_cast_modes = media_router_ui_->routes_and_cast_modes();
  ASSERT_EQ(1u, current_cast_modes.size());
  auto cast_mode_entry =
      current_cast_modes.find(display_route_1.media_route_id());
  // No observer for source "mediaSource1" means no cast mode for this route.
  EXPECT_EQ(end(current_cast_modes), cast_mode_entry);
  cast_mode_entry =
      current_cast_modes.find(non_display_route_1.media_route_id());
  EXPECT_EQ(end(current_cast_modes), cast_mode_entry);
  cast_mode_entry = current_cast_modes.find(display_route_2.media_route_id());
  EXPECT_NE(end(current_cast_modes), cast_mode_entry);
  EXPECT_EQ(MediaCastMode::DESKTOP_MIRROR, cast_mode_entry->second);

  EXPECT_CALL(mock_router_, UnregisterMediaRoutesObserver(_)).Times(1);
  observer.reset();
}

TEST_F(MediaRouterUITest, GetExtensionNameExtensionPresent) {
  std::string id = "extensionid";
  GURL url = GURL("chrome-extension://" + id);
  std::unique_ptr<extensions::ExtensionRegistry> registry =
      base::MakeUnique<extensions::ExtensionRegistry>(nullptr);
  scoped_refptr<extensions::Extension> app =
      extensions::test_util::BuildApp(extensions::ExtensionBuilder())
          .MergeManifest(extensions::DictionaryBuilder()
                             .Set("name", "test app name")
                             .Build())
          .SetID(id)
          .Build();

  ASSERT_TRUE(registry->AddEnabled(app));
  EXPECT_EQ("test app name",
            MediaRouterUI::GetExtensionName(url, registry.get()));
}

TEST_F(MediaRouterUITest, GetExtensionNameEmptyWhenNotInstalled) {
  std::string id = "extensionid";
  GURL url = GURL("chrome-extension://" + id);
  std::unique_ptr<extensions::ExtensionRegistry> registry =
      base::MakeUnique<extensions::ExtensionRegistry>(nullptr);

  EXPECT_EQ("", MediaRouterUI::GetExtensionName(url, registry.get()));
}

TEST_F(MediaRouterUITest, GetExtensionNameEmptyWhenNotExtensionURL) {
  GURL url = GURL("https://www.google.com");
  std::unique_ptr<extensions::ExtensionRegistry> registry =
      base::MakeUnique<extensions::ExtensionRegistry>(nullptr);

  EXPECT_EQ("", MediaRouterUI::GetExtensionName(url, registry.get()));
}

TEST_F(MediaRouterUITest, NotFoundErrorOnCloseWithNoSinks) {
  content::PresentationError expected_error(
      content::PresentationErrorType::PRESENTATION_ERROR_NO_AVAILABLE_SCREENS,
      "No screens found.");
  PresentationRequestCallbacks request_callbacks(expected_error);
  create_session_request_.reset(new CreatePresentationConnectionRequest(
      RenderFrameHostId(0, 0),
      {GURL("http://google.com/presentation"),
       GURL("http://google.com/presentation2")},
      url::Origin(GURL("http://google.com")),
      base::Bind(&PresentationRequestCallbacks::Success,
                 base::Unretained(&request_callbacks)),
      base::Bind(&PresentationRequestCallbacks::Error,
                 base::Unretained(&request_callbacks))));
  CreateMediaRouterUI(profile());
  // Destroying the UI should return the expected error from above to the error
  // callback.
  media_router_ui_.reset();
}

TEST_F(MediaRouterUITest, NotFoundErrorOnCloseWithNoCompatibleSinks) {
  content::PresentationError expected_error(
      content::PresentationErrorType::PRESENTATION_ERROR_NO_AVAILABLE_SCREENS,
      "No screens found.");
  PresentationRequestCallbacks request_callbacks(expected_error);
  GURL presentation_url("http://google.com/presentation");
  create_session_request_.reset(new CreatePresentationConnectionRequest(
      RenderFrameHostId(0, 0), {presentation_url},
      url::Origin(GURL("http://google.com")),
      base::Bind(&PresentationRequestCallbacks::Success,
                 base::Unretained(&request_callbacks)),
      base::Bind(&PresentationRequestCallbacks::Error,
                 base::Unretained(&request_callbacks))));
  CreateMediaRouterUI(profile());

  // Send a sink to the UI that is compatible with sources other than the
  // presentation url to cause a NotFoundError.
  std::vector<MediaSink> sinks;
  sinks.emplace_back("sink id", "sink name", MediaSink::GENERIC);
  std::vector<url::Origin> origins;
  for (auto* observer : media_sinks_observers_) {
    if (observer->source().id() != presentation_url.spec()) {
      observer->OnSinksUpdated(sinks, origins);
    }
  }
  // Destroying the UI should return the expected error from above to the error
  // callback.
  media_router_ui_.reset();
}

TEST_F(MediaRouterUITest, AbortErrorOnClose) {
  content::PresentationError expected_error(
      content::PresentationErrorType::
          PRESENTATION_ERROR_PRESENTATION_REQUEST_CANCELLED,
      "Dialog closed.");
  PresentationRequestCallbacks request_callbacks(expected_error);
  GURL presentation_url("http://google.com/presentation");
  create_session_request_.reset(new CreatePresentationConnectionRequest(
      RenderFrameHostId(0, 0), {presentation_url},
      url::Origin(GURL("http://google.com")),
      base::Bind(&PresentationRequestCallbacks::Success,
                 base::Unretained(&request_callbacks)),
      base::Bind(&PresentationRequestCallbacks::Error,
                 base::Unretained(&request_callbacks))));
  CreateMediaRouterUI(profile());

  // Send a sink to the UI that is compatible with the presentation url to avoid
  // a NotFoundError.
  std::vector<MediaSink> sinks;
  sinks.emplace_back("sink id", "sink name", MediaSink::GENERIC);
  std::vector<url::Origin> origins;
  MediaSource::Id presentation_source_id =
      MediaSourceForPresentationUrl(presentation_url).id();
  for (auto* observer : media_sinks_observers_) {
    if (observer->source().id() == presentation_source_id) {
      observer->OnSinksUpdated(sinks, origins);
    }
  }
  // Destroying the UI should return the expected error from above to the error
  // callback.
  media_router_ui_.reset();
}

TEST_F(MediaRouterUITest, RecordCastModeSelections) {
  const GURL url_1a = GURL("https://www.example.com/watch?v=AAAA");
  const GURL url_1b = GURL("https://www.example.com/watch?v=BBBB");
  const GURL url_2 = GURL("https://example2.com/0000");
  const GURL url_3 = GURL("https://www3.example.com/index.html");

  CreateMediaRouterUIForURL(profile(), url_1a);
  EXPECT_FALSE(media_router_ui_->UserSelectedTabMirroringForCurrentOrigin());
  media_router_ui_->RecordCastModeSelection(MediaCastMode::TAB_MIRROR);
  EXPECT_TRUE(media_router_ui_->UserSelectedTabMirroringForCurrentOrigin());

  CreateMediaRouterUIForURL(profile(), url_2);
  EXPECT_FALSE(media_router_ui_->UserSelectedTabMirroringForCurrentOrigin());

  CreateMediaRouterUIForURL(profile(), url_1b);
  // |url_1a| and |url_1b| have the same origin, so the selection made for
  // |url_1a| should be retrieved.
  EXPECT_TRUE(media_router_ui_->UserSelectedTabMirroringForCurrentOrigin());
  media_router_ui_->RecordCastModeSelection(MediaCastMode::DEFAULT);
  EXPECT_FALSE(media_router_ui_->UserSelectedTabMirroringForCurrentOrigin());

  media_router_ui_->RecordCastModeSelection(MediaCastMode::TAB_MIRROR);
  CreateMediaRouterUIForURL(profile(), url_3);
  // |url_1a| and |url_3| have the same domain "example.com" but different
  // origins, so their preferences should be separate.
  EXPECT_FALSE(media_router_ui_->UserSelectedTabMirroringForCurrentOrigin());
}

TEST_F(MediaRouterUITest, RecordCastModeSelectionsInIncognito) {
  const GURL url = GURL("https://www.example.com/watch?v=AAAA");

  CreateMediaRouterUIForURL(profile()->GetOffTheRecordProfile(), url);
  EXPECT_FALSE(media_router_ui_->UserSelectedTabMirroringForCurrentOrigin());
  media_router_ui_->RecordCastModeSelection(MediaCastMode::TAB_MIRROR);
  EXPECT_TRUE(media_router_ui_->UserSelectedTabMirroringForCurrentOrigin());

  // Selections recorded in incognito shouldn't be retrieved in the regular
  // profile.
  CreateMediaRouterUIForURL(profile(), url);
  EXPECT_FALSE(media_router_ui_->UserSelectedTabMirroringForCurrentOrigin());
}

TEST_F(MediaRouterUITest, RecordDesktopMirroringCastModeSelection) {
  const GURL url = GURL("https://www.example.com/watch?v=AAAA");
  CreateMediaRouterUIForURL(profile(), url);

  EXPECT_FALSE(media_router_ui_->UserSelectedTabMirroringForCurrentOrigin());
  media_router_ui_->RecordCastModeSelection(MediaCastMode::DESKTOP_MIRROR);
  // Selecting desktop mirroring should not change the recorded preferences.
  EXPECT_FALSE(media_router_ui_->UserSelectedTabMirroringForCurrentOrigin());

  media_router_ui_->RecordCastModeSelection(MediaCastMode::TAB_MIRROR);
  EXPECT_TRUE(media_router_ui_->UserSelectedTabMirroringForCurrentOrigin());
  media_router_ui_->RecordCastModeSelection(MediaCastMode::DESKTOP_MIRROR);
  // Selecting desktop mirroring should not change the recorded preferences.
  EXPECT_TRUE(media_router_ui_->UserSelectedTabMirroringForCurrentOrigin());
}

TEST_F(MediaRouterUITest, OpenAndCloseUIDetailsView) {
  const std::string route_id = "routeId";
  CreateMediaRouterUI(profile());
  OpenUIDetailsView(route_id);

  // When the route details view is closed, the route controller observer should
  // be destroyed, also triggering the destruction of the controller.
  EXPECT_CALL(mock_router_, DetachRouteController(route_id, _));
  media_router_ui_->OnMediaControllerUIClosed();

  EXPECT_TRUE(Mock::VerifyAndClearExpectations(&mock_router_));
}

TEST_F(MediaRouterUITest, SendMediaStatusUpdate) {
  MediaStatus status;
  status.title = "test title";
  CreateMediaRouterUI(profile());
  scoped_refptr<MockMediaRouteController> controller =
      OpenUIDetailsView("routeId");

  // The route controller observer held by MediaRouterUI should send the status
  // update to the message handler.
  EXPECT_CALL(*message_handler_, UpdateMediaRouteStatus(status));
  controller->OnMediaStatusUpdated(status);
}

TEST_F(MediaRouterUITest, SendInitialMediaStatusUpdate) {
  MediaStatus status;
  status.title = "test title";
  std::string route_id = "routeId";
  auto controller = CreateMediaRouteController(route_id);
  controller->OnMediaStatusUpdated(status);

  CreateMediaRouterUI(profile());
  MediaSource media_source("mediaSource");
  MediaRoute route(route_id, media_source, "sinkId", "", true, "", true);
  media_router_ui_->OnRoutesUpdated({route}, std::vector<MediaRoute::Id>());

  // If the controller has already received a media status update, MediaRouterUI
  // should be notified with it when it starts observing the controller.
  EXPECT_CALL(mock_router_, GetRouteController(route_id))
      .WillOnce(Return(controller));
  EXPECT_CALL(*message_handler_, UpdateMediaRouteStatus(status));
  media_router_ui_->OnMediaControllerUIAvailable(route_id);
}

TEST_F(MediaRouterUITest, SetsForcedCastModeWithPresentationURLs) {
  content::PresentationError expected_error(
      content::PresentationErrorType::PRESENTATION_ERROR_NO_AVAILABLE_SCREENS,
      "No screens found.");
  PresentationRequestCallbacks request_callbacks(expected_error);
  create_session_request_.reset(new CreatePresentationConnectionRequest(
      RenderFrameHostId(0, 0),
      {GURL("http://google.com/presentation"),
       GURL("http://google.com/presentation2")},
      url::Origin(GURL("http://google.com")),
      base::Bind(&PresentationRequestCallbacks::Success,
                 base::Unretained(&request_callbacks)),
      base::Bind(&PresentationRequestCallbacks::Error,
                 base::Unretained(&request_callbacks))));

  SessionTabHelper::CreateForWebContents(web_contents());
  web_ui_contents_.reset(
      WebContents::Create(WebContents::CreateParams(profile())));
  web_ui_.set_web_contents(web_ui_contents_.get());
  media_router_ui_ = base::MakeUnique<MediaRouterUI>(&web_ui_);
  message_handler_ = base::MakeUnique<MockMediaRouterWebUIMessageHandler>(
      media_router_ui_.get());
  message_handler_->SetWebUIForTest(&web_ui_);
  EXPECT_CALL(mock_router_, RegisterMediaSinksObserver(_))
      .WillRepeatedly(Invoke([this](MediaSinksObserver* observer) {
        this->media_sinks_observers_.push_back(observer);
        return true;
      }));
  EXPECT_CALL(mock_router_, RegisterMediaRoutesObserver(_)).Times(AnyNumber());
  // For some reason we push two sets of cast modes to the dialog, even when
  // initializing the dialog with a presentation request.  The WebUI can handle
  // the forced mode that is not in the initial cast mode set, but is this a
  // bug?
  CastModeSet expected_modes(
      {MediaCastMode::TAB_MIRROR, MediaCastMode::DESKTOP_MIRROR});
  EXPECT_CALL(
      *message_handler_,
      UpdateCastModes(expected_modes, "",
                      base::Optional<MediaCastMode>(MediaCastMode::DEFAULT)));
  expected_modes.insert(MediaCastMode::DEFAULT);
  EXPECT_CALL(
      *message_handler_,
      UpdateCastModes(expected_modes, "google.com",
                      base::Optional<MediaCastMode>(MediaCastMode::DEFAULT)));
  media_router_ui_->UIInitialized();
  media_router_ui_->InitForTest(&mock_router_, web_contents(),
                                message_handler_.get(),
                                std::move(create_session_request_));
  // |media_router_ui_| takes ownership of |request_callbacks|.
  media_router_ui_.reset();
}

}  // namespace media_router
