// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/update_client/ping_manager.h"

#include <memory>
#include <string>
#include <vector>

#include "base/memory/ptr_util.h"
#include "base/memory/ref_counted.h"
#include "base/message_loop/message_loop.h"
#include "base/run_loop.h"
#include "base/threading/thread_task_runner_handle.h"
#include "base/version.h"
#include "components/update_client/component.h"
#include "components/update_client/protocol_builder.h"
#include "components/update_client/test_configurator.h"
#include "components/update_client/update_engine.h"
#include "components/update_client/url_request_post_interceptor.h"
#include "net/url_request/url_request_test_util.h"
#include "testing/gtest/include/gtest/gtest.h"

using std::string;

namespace update_client {

class PingManagerTest : public testing::Test {
 public:
  PingManagerTest();
  ~PingManagerTest() override {}

  void RunThreadsUntilIdle();

  std::unique_ptr<UpdateContext> MakeFakeUpdateContext() const;

  // Overrides from testing::Test.
  void SetUp() override;
  void TearDown() override;

 protected:
  scoped_refptr<TestConfigurator> config_;
  std::unique_ptr<PingManager> ping_manager_;

 private:
  base::MessageLoopForIO loop_;
};

PingManagerTest::PingManagerTest() {}

void PingManagerTest::SetUp() {
  config_ = new TestConfigurator(base::ThreadTaskRunnerHandle::Get(),
                                 base::ThreadTaskRunnerHandle::Get());
  ping_manager_.reset(new PingManager(config_));
}

void PingManagerTest::TearDown() {
  ping_manager_.reset();
  config_ = nullptr;
}

void PingManagerTest::RunThreadsUntilIdle() {
  base::RunLoop().RunUntilIdle();
}

std::unique_ptr<UpdateContext> PingManagerTest::MakeFakeUpdateContext() const {
  return base::MakeUnique<UpdateContext>(
      config_, false, std::vector<std::string>(),
      UpdateClient::CrxDataCallback(), UpdateEngine::NotifyObserversCallback(),
      UpdateEngine::Callback(), nullptr);
}

TEST_F(PingManagerTest, SendPing) {
  std::unique_ptr<InterceptorFactory> interceptor_factory(
      new InterceptorFactory(base::ThreadTaskRunnerHandle::Get()));
  URLRequestPostInterceptor* interceptor =
      interceptor_factory->CreateInterceptor();
  EXPECT_TRUE(interceptor);

  // Test eventresult="1" is sent for successful updates.
  const auto update_context = MakeFakeUpdateContext();

  {
    Component component(*update_context, "abc");

    component.state_ = base::MakeUnique<Component::StateUpdated>(&component);
    component.previous_version_ = base::Version("1.0");
    component.next_version_ = base::Version("2.0");
    component.AppendEvent(BuildUpdateCompleteEventElement(component));

    ping_manager_->SendPing(component);
    base::RunLoop().RunUntilIdle();

    EXPECT_EQ(1, interceptor->GetCount()) << interceptor->GetRequestsAsString();
    EXPECT_NE(string::npos,
              interceptor->GetRequests()[0].find(
                  "<app appid=\"abc\" version=\"1.0\" nextversion=\"2.0\">"
                  "<event eventtype=\"3\" eventresult=\"1\"/></app>"))
        << interceptor->GetRequestsAsString();
    interceptor->Reset();
  }

  {
    // Test eventresult="0" is sent for failed updates.
    Component component(*update_context, "abc");
    component.state_ =
        base::MakeUnique<Component::StateUpdateError>(&component);
    component.previous_version_ = base::Version("1.0");
    component.next_version_ = base::Version("2.0");
    component.AppendEvent(BuildUpdateCompleteEventElement(component));

    ping_manager_->SendPing(component);
    base::RunLoop().RunUntilIdle();

    EXPECT_EQ(1, interceptor->GetCount()) << interceptor->GetRequestsAsString();
    EXPECT_NE(string::npos,
              interceptor->GetRequests()[0].find(
                  "<app appid=\"abc\" version=\"1.0\" nextversion=\"2.0\">"
                  "<event eventtype=\"3\" eventresult=\"0\"/></app>"))
        << interceptor->GetRequestsAsString();
    interceptor->Reset();
  }

  {
    // Test the error values and the fingerprints.
    Component component(*update_context, "abc");
    component.state_ =
        base::MakeUnique<Component::StateUpdateError>(&component);
    component.previous_version_ = base::Version("1.0");
    component.next_version_ = base::Version("2.0");
    component.previous_fp_ = "prev fp";
    component.next_fp_ = "next fp";
    component.error_category_ = 1;
    component.error_code_ = 2;
    component.extra_code1_ = -1;
    component.diff_error_category_ = 10;
    component.diff_error_code_ = 20;
    component.diff_extra_code1_ = -10;
    component.crx_diffurls_.push_back(GURL("http://host/path"));
    component.AppendEvent(BuildUpdateCompleteEventElement(component));

    ping_manager_->SendPing(component);
    base::RunLoop().RunUntilIdle();

    EXPECT_EQ(1, interceptor->GetCount()) << interceptor->GetRequestsAsString();
    EXPECT_NE(string::npos,
              interceptor->GetRequests()[0].find(
                  "<app appid=\"abc\" version=\"1.0\" nextversion=\"2.0\">"
                  "<event eventtype=\"3\" eventresult=\"0\" errorcat=\"1\" "
                  "errorcode=\"2\" extracode1=\"-1\" diffresult=\"0\" "
                  "differrorcat=\"10\" "
                  "differrorcode=\"20\" diffextracode1=\"-10\" "
                  "previousfp=\"prev fp\" nextfp=\"next fp\"/></app>"))
        << interceptor->GetRequestsAsString();
    interceptor->Reset();
  }

  {
    // Test the download metrics.
    Component component(*update_context, "abc");
    component.state_ = base::MakeUnique<Component::StateUpdated>(&component);
    component.previous_version_ = base::Version("1.0");
    component.next_version_ = base::Version("2.0");
    component.AppendEvent(BuildUpdateCompleteEventElement(component));

    CrxDownloader::DownloadMetrics download_metrics;
    download_metrics.url = GURL("http://host1/path1");
    download_metrics.downloader = CrxDownloader::DownloadMetrics::kUrlFetcher;
    download_metrics.error = -1;
    download_metrics.downloaded_bytes = 123;
    download_metrics.total_bytes = 456;
    download_metrics.download_time_ms = 987;
    component.AppendEvent(BuildDownloadCompleteEventElement(download_metrics));

    download_metrics = CrxDownloader::DownloadMetrics();
    download_metrics.url = GURL("http://host2/path2");
    download_metrics.downloader = CrxDownloader::DownloadMetrics::kBits;
    download_metrics.error = 0;
    download_metrics.downloaded_bytes = 1230;
    download_metrics.total_bytes = 4560;
    download_metrics.download_time_ms = 9870;
    component.AppendEvent(BuildDownloadCompleteEventElement(download_metrics));

    ping_manager_->SendPing(component);
    base::RunLoop().RunUntilIdle();

    EXPECT_EQ(1, interceptor->GetCount()) << interceptor->GetRequestsAsString();
    EXPECT_NE(
        string::npos,
        interceptor->GetRequests()[0].find(
            "<app appid=\"abc\" version=\"1.0\" nextversion=\"2.0\">"
            "<event eventtype=\"3\" eventresult=\"1\"/>"
            "<event eventtype=\"14\" eventresult=\"0\" downloader=\"direct\" "
            "errorcode=\"-1\" url=\"http://host1/path1\" downloaded=\"123\" "
            "total=\"456\" download_time_ms=\"987\"/>"
            "<event eventtype=\"14\" eventresult=\"1\" downloader=\"bits\" "
            "url=\"http://host2/path2\" downloaded=\"1230\" total=\"4560\" "
            "download_time_ms=\"9870\"/></app>"))
        << interceptor->GetRequestsAsString();
    interceptor->Reset();
  }

  interceptor_factory.reset();
  base::RunLoop().RunUntilIdle();
}

// Tests that sending the ping fails when the component requires encryption but
// the ping URL is unsecure.
TEST_F(PingManagerTest, RequiresEncryption) {
  config_->SetPingUrl(GURL("http:\\foo\bar"));

  const auto update_context = MakeFakeUpdateContext();

  {
    Component component(*update_context, "abc");
    component.crx_component_.requires_network_encryption = true;

    EXPECT_FALSE(ping_manager_->SendPing(component));
  }

  {
    // Tests that the default for |requires_network_encryption| is true.
    Component component(*update_context, "abc");
    EXPECT_FALSE(ping_manager_->SendPing(component));
  }
}

}  // namespace update_client
