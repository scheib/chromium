// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/resource_coordinator/public/cpp/tracing/chrome_trace_event_agent.h"
#include "base/bind.h"
#include "base/callback_forward.h"
#include "base/memory/ptr_util.h"
#include "base/message_loop/message_loop.h"
#include "base/run_loop.h"
#include "base/test/trace_event_analyzer.h"
#include "base/trace_event/trace_config.h"
#include "base/trace_event/trace_event.h"
#include "base/trace_event/trace_log.h"
#include "base/values.h"
#include "services/resource_coordinator/public/interfaces/tracing/tracing.mojom.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace tracing {

namespace {
const char kTestCategory[] = "ChromeTraceEventAgentTestCategory";
const char kTestMetadataKey[] = "ChromeTraceEventAgentTestMetadata";
}  // namespace

class MockRecorder : public mojom::Recorder {
 public:
  MockRecorder(mojom::RecorderRequest request)
      : binding_(this, std::move(request)) {
    binding_.set_connection_error_handler(base::BindRepeating(
        &MockRecorder::OnConnectionError, base::Unretained(this)));
  }

  std::string events() const { return events_; }
  std::string metadata() const { return metadata_; }
  void set_quit_closure(base::Closure quit_closure) {
    quit_closure_ = quit_closure;
  }

 private:
  void Append(std::string* dest, const std::string& chunk) {
    if (chunk.empty())
      return;
    if (!dest->empty())
      dest->push_back(',');
    dest->append(chunk);
  }

  void AddChunk(const std::string& chunk) override {
    auto* analyzer = trace_analyzer::TraceAnalyzer::Create("[" + chunk + "]");
    trace_analyzer::TraceEventVector events;
    analyzer->FindEvents(trace_analyzer::Query::EventCategoryIs(kTestCategory),
                         &events);
    for (const auto* event : events) {
      Append(&events_, event->name);
    }
  }

  void AddMetadata(std::unique_ptr<base::DictionaryValue> metadata) override {
    base::DictionaryValue* dict = nullptr;
    EXPECT_TRUE(metadata->GetAsDictionary(&dict));
    std::string value;
    if (dict->GetString(kTestMetadataKey, &value))
      Append(&metadata_, value);
  }

  void OnConnectionError() {
    if (quit_closure_)
      quit_closure_.Run();
  }

  mojo::Binding<mojom::Recorder> binding_;
  std::string events_;
  std::string metadata_;
  base::Closure quit_closure_;
};

class ChromeTraceEventAgentTest : public testing::Test {
 public:
  void SetUp() override {
    message_loop_.reset(new base::MessageLoop());
    agent_.reset(new ChromeTraceEventAgent(nullptr));
  }

  void TearDown() override {
    base::trace_event::TraceLog::GetInstance()->SetDisabled();
    recorder_.reset();
    agent_.reset();
    message_loop_.reset();
  }

  void StartTracing(const std::string& categories, base::Closure quit_closure) {
    mojom::RecorderPtr recorder_ptr;
    recorder_.reset(new MockRecorder(MakeRequest(&recorder_ptr)));
    recorder_->set_quit_closure(quit_closure);
    agent_->StartTracing(
        base::trace_event::TraceConfig(categories, "").ToString(),
        std::move(recorder_ptr), base::BindRepeating([] {}));
  }

  void StopAndFlush() { agent_->StopAndFlush(); }

  void AddMetadataGeneratorFunction(
      ChromeTraceEventAgent::MetadataGeneratorFunction generator) {
    agent_->AddMetadataGeneratorFunction(generator);
  }

  void GetCategories(const std::string& expected_category,
                     base::Closure quit_closure) {
    agent_->GetCategories(base::BindRepeating(
        &ChromeTraceEventAgentTest::OnGetCategoriesReply,
        base::Unretained(this), expected_category, quit_closure));
  }

  void OnGetCategoriesReply(const std::string& expected_category,
                            base::Closure quit_closure,
                            const std::string& categories) {
    EXPECT_FALSE(categories.rfind(expected_category) == std::string::npos);
    quit_closure.Run();
  }

  MockRecorder* recorder() const { return recorder_.get(); }

 private:
  std::unique_ptr<base::MessageLoop> message_loop_;
  std::unique_ptr<ChromeTraceEventAgent> agent_;
  std::unique_ptr<MockRecorder> recorder_;
};

TEST_F(ChromeTraceEventAgentTest, StartTracing) {
  EXPECT_FALSE(base::trace_event::TraceLog::GetInstance()->IsEnabled());
  base::RunLoop run_loop;
  StartTracing("*", run_loop.QuitClosure());
  EXPECT_TRUE(base::trace_event::TraceLog::GetInstance()->IsEnabled());
  StopAndFlush();
  run_loop.Run();
}

TEST_F(ChromeTraceEventAgentTest, StopAndFlushEvents) {
  EXPECT_FALSE(base::trace_event::TraceLog::GetInstance()->IsEnabled());
  base::RunLoop run_loop;
  StartTracing(kTestCategory, run_loop.QuitClosure());
  TRACE_EVENT_INSTANT0(kTestCategory, "event1", TRACE_EVENT_SCOPE_THREAD);
  TRACE_EVENT_INSTANT0(kTestCategory, "event2", TRACE_EVENT_SCOPE_THREAD);
  StopAndFlush();
  run_loop.Run();

  auto* mock_recorder = recorder();
  EXPECT_EQ("event1,event2", mock_recorder->events());
  EXPECT_EQ("", mock_recorder->metadata());
  EXPECT_FALSE(base::trace_event::TraceLog::GetInstance()->IsEnabled());
}

TEST_F(ChromeTraceEventAgentTest, GetCategories) {
  base::RunLoop run_loop;
  TRACE_EVENT_INSTANT0(kTestCategory, "event1", TRACE_EVENT_SCOPE_THREAD);
  GetCategories(kTestCategory, run_loop.QuitClosure());
  run_loop.Run();
}

TEST_F(ChromeTraceEventAgentTest, StopAndFlushMetadata) {
  EXPECT_FALSE(base::trace_event::TraceLog::GetInstance()->IsEnabled());
  base::RunLoop run_loop;
  AddMetadataGeneratorFunction(base::BindRepeating([] {
    std::unique_ptr<base::DictionaryValue> metadata_dict(
        new base::DictionaryValue());
    metadata_dict->SetString(kTestMetadataKey, "test metadata");
    return metadata_dict;
  }));
  StartTracing(kTestCategory, run_loop.QuitClosure());
  StopAndFlush();
  run_loop.Run();

  auto* mock_recorder = recorder();
  EXPECT_EQ("", mock_recorder->events());
  EXPECT_EQ("test metadata", mock_recorder->metadata());
  EXPECT_FALSE(base::trace_event::TraceLog::GetInstance()->IsEnabled());
}
}  // namespace tracing
