// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/chrome/browser/web_state_list/web_state_list_serialization.h"

#include <memory>

#include "base/bind.h"
#include "base/memory/ptr_util.h"
#import "ios/chrome/browser/sessions/session_window_ios.h"
#import "ios/chrome/browser/web_state_list/fake_web_state_list_delegate.h"
#import "ios/chrome/browser/web_state_list/web_state_list.h"
#import "ios/chrome/browser/web_state_list/web_state_opener.h"
#import "ios/web/public/crw_session_storage.h"
#import "ios/web/public/serializable_user_data_manager.h"
#import "ios/web/public/test/fakes/test_web_state.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "testing/platform_test.h"

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

namespace {

// Serializable sub-class of TestWebState.
class SerializableTestWebState : public web::TestWebState {
 public:
  static std::unique_ptr<web::WebState> Create() {
    return base::MakeUnique<SerializableTestWebState>();
  }

  static std::unique_ptr<web::WebState> CreateWithSessionStorage(
      CRWSessionStorage* session_storage) {
    std::unique_ptr<web::WebState> web_state = Create();
    web::SerializableUserDataManager::FromWebState(web_state.get())
        ->AddSerializableUserData(session_storage.userData);
    return web_state;
  }

 private:
  // web::TestWebState implementation.
  CRWSessionStorage* BuildSessionStorage() override {
    std::unique_ptr<web::SerializableUserData> serializable_user_data =
        web::SerializableUserDataManager::FromWebState(this)
            ->CreateSerializableUserData();

    CRWSessionStorage* session_storage = [[CRWSessionStorage alloc] init];
    [session_storage setSerializableUserData:std::move(serializable_user_data)];
    return session_storage;
  }
};

// Compares whether both WebStateList |original| and |restored| have the same
// opener-opened relationship. The |restored| WebStateList may have additional
// WebState, so only indices from |restored_index| to |count()| are compared.
void ExpectRelationshipIdenticalFrom(int restored_index,
                                     WebStateList* original,
                                     WebStateList* restored) {
  ASSERT_GE(restored_index, 0);
  EXPECT_EQ(original->count(), restored->count() - restored_index);

  for (int index = 0; index < original->count(); ++index) {
    WebStateOpener original_opener = original->GetOpenerOfWebStateAt(index);
    WebStateOpener restored_opener =
        restored->GetOpenerOfWebStateAt(index + restored_index);

    int restored_opener_index =
        restored_opener.opener
            ? restored->GetIndexOfWebState(restored_opener.opener) -
                  restored_index
            : WebStateList::kInvalidIndex;

    EXPECT_EQ(original->GetIndexOfWebState(original_opener.opener),
              restored_opener_index);
    EXPECT_EQ(original_opener.navigation_index,
              restored_opener.navigation_index);
  }
}

}  // namespace

class WebStateListSerializationTest : public PlatformTest {
 public:
  WebStateListSerializationTest() = default;

  WebStateListDelegate* web_state_list_delegate() {
    return &web_state_list_delegate_;
  }

 private:
  FakeWebStateListDelegate web_state_list_delegate_;

  DISALLOW_COPY_AND_ASSIGN(WebStateListSerializationTest);
};

TEST_F(WebStateListSerializationTest, SerializationEmpty) {
  WebStateList original_web_state_list(web_state_list_delegate());
  SessionWindowIOS* session_window =
      SerializeWebStateList(&original_web_state_list);

  EXPECT_EQ(0u, session_window.sessions.count);
  EXPECT_EQ(static_cast<NSUInteger>(NSNotFound), session_window.selectedIndex);
}

TEST_F(WebStateListSerializationTest, SerializationRoundTrip) {
  WebStateList original_web_state_list(web_state_list_delegate());
  original_web_state_list.InsertWebState(0, SerializableTestWebState::Create());
  original_web_state_list.InsertWebState(1, SerializableTestWebState::Create());
  original_web_state_list.InsertWebState(2, SerializableTestWebState::Create());
  original_web_state_list.InsertWebState(3, SerializableTestWebState::Create());
  original_web_state_list.SetOpenerOfWebStateAt(
      1, WebStateOpener(original_web_state_list.GetWebStateAt(0), 3));
  original_web_state_list.SetOpenerOfWebStateAt(
      2, WebStateOpener(original_web_state_list.GetWebStateAt(0), 2));
  original_web_state_list.SetOpenerOfWebStateAt(
      3, WebStateOpener(original_web_state_list.GetWebStateAt(1), 1));
  original_web_state_list.ActivateWebStateAt(1);

  SessionWindowIOS* session_window =
      SerializeWebStateList(&original_web_state_list);

  EXPECT_EQ(4u, session_window.sessions.count);
  EXPECT_EQ(1u, session_window.selectedIndex);

  // Create a deserialized WebStateList and verify its contents.
  WebStateList restored_web_state_list(web_state_list_delegate());
  restored_web_state_list.InsertWebState(0, SerializableTestWebState::Create());
  ASSERT_EQ(1, restored_web_state_list.count());

  DeserializeWebStateList(
      &restored_web_state_list, session_window, false,
      base::BindRepeating(&SerializableTestWebState::CreateWithSessionStorage));

  EXPECT_EQ(5, restored_web_state_list.count());
  EXPECT_EQ(2, restored_web_state_list.active_index());
  ExpectRelationshipIdenticalFrom(1, &original_web_state_list,
                                  &restored_web_state_list);

  // Create a deserialized WebStateList with web usage enabled and verify its
  // contents.
  WebStateList restored_web_state_list_web_usage_enabled(
      web_state_list_delegate());
  std::unique_ptr<web::WebState> webUsageEnabledWebState =
      SerializableTestWebState::Create();
  webUsageEnabledWebState->SetWebUsageEnabled(true);
  restored_web_state_list_web_usage_enabled.InsertWebState(
      0, std::move(webUsageEnabledWebState));
  ASSERT_EQ(1, restored_web_state_list_web_usage_enabled.count());

  DeserializeWebStateList(
      &restored_web_state_list_web_usage_enabled, session_window, true,
      base::BindRepeating(&SerializableTestWebState::CreateWithSessionStorage));

  EXPECT_EQ(5, restored_web_state_list_web_usage_enabled.count());
  EXPECT_EQ(2, restored_web_state_list_web_usage_enabled.active_index());
  ExpectRelationshipIdenticalFrom(1, &original_web_state_list,
                                  &restored_web_state_list_web_usage_enabled);

  // Verify that the WebUsageEnabled bit is set appropriately for the restored
  // WebStateLists.
  ASSERT_EQ(restored_web_state_list_web_usage_enabled.count(),
            restored_web_state_list.count());
  for (int i = 0; i < restored_web_state_list.count(); ++i) {
    EXPECT_TRUE(restored_web_state_list_web_usage_enabled.GetWebStateAt(i)
                    ->IsWebUsageEnabled());
    EXPECT_FALSE(restored_web_state_list.GetWebStateAt(i)->IsWebUsageEnabled());
  }
}
