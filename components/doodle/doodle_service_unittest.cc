// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/doodle/doodle_service.h"

#include <memory>
#include <utility>
#include <vector>

#include "base/bind.h"
#include "base/memory/ptr_util.h"
#include "base/memory/ref_counted.h"
#include "base/test/histogram_tester.h"
#include "base/test/mock_callback.h"
#include "base/test/simple_test_tick_clock.h"
#include "base/test/test_mock_time_task_runner.h"
#include "base/threading/thread_task_runner_handle.h"
#include "base/time/time.h"
#include "components/image_fetcher/core/image_fetcher.h"
#include "components/image_fetcher/core/request_metadata.h"
#include "components/prefs/testing_pref_service.h"
#include "net/traffic_annotation/network_traffic_annotation_test_helper.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/image/image.h"
#include "ui/gfx/image/image_unittest_util.h"

using image_fetcher::ImageFetcher;
using image_fetcher::RequestMetadata;
using testing::_;
using testing::Eq;
using testing::StrictMock;
using testing::Not;

namespace doodle {

namespace {

class FakeDoodleFetcher : public DoodleFetcher {
 public:
  FakeDoodleFetcher() = default;
  ~FakeDoodleFetcher() override = default;

  void FetchDoodle(FinishedCallback callback) override {
    callbacks_.push_back(std::move(callback));
  }

  bool IsFetchInProgress() const override { return !callbacks_.empty(); }

  size_t num_pending_callbacks() const { return callbacks_.size(); }

  void ServeAllCallbacks(DoodleState state,
                         base::TimeDelta time_to_live,
                         const base::Optional<DoodleConfig>& config) {
    for (auto& callback : callbacks_) {
      std::move(callback).Run(state, time_to_live, config);
    }
    callbacks_.clear();
  }

 private:
  std::vector<FinishedCallback> callbacks_;
};

class MockDoodleObserver : public DoodleService::Observer {
 public:
  MOCK_METHOD1(OnDoodleConfigUpdated,
               void(const base::Optional<DoodleConfig>&));
  MOCK_METHOD1(OnDoodleConfigRevalidated, void(bool));
};

class FakeImageFetcher : public ImageFetcher {
 public:
  FakeImageFetcher() = default;
  ~FakeImageFetcher() override = default;

  void SetImageFetcherDelegate(image_fetcher::ImageFetcherDelegate*) override {
    NOTREACHED();
  }

  void SetDataUseServiceName(DataUseServiceName) override {
    // Ignored.
  }

  void SetImageDownloadLimit(base::Optional<int64_t>) override {
    // Ignored.
  }

  void SetDesiredImageFrameSize(const gfx::Size&) override {
    // Ignored.
  }

  void StartOrQueueNetworkRequest(
      const std::string& id,
      const GURL& url,
      const ImageFetcherCallback& callback,
      const net::NetworkTrafficAnnotationTag&) override {
    // For simplicity, the fake doesn't support multiple concurrent requests.
    DCHECK(!HasPendingRequest());

    pending_id_ = id;
    pending_url_ = url;
    pending_callback_ = callback;
  }

  image_fetcher::ImageDecoder* GetImageDecoder() override {
    NOTREACHED();
    return nullptr;
  }

  bool HasPendingRequest() const { return !pending_callback_.is_null(); }

  const GURL& pending_url() const { return pending_url_; }

  void RespondToPendingRequest(const gfx::Image& image) {
    DCHECK(HasPendingRequest());

    RequestMetadata metadata;
    metadata.http_response_code = 200;
    pending_callback_.Run(pending_id_, image, metadata);

    pending_id_.clear();
    pending_url_ = GURL();
    pending_callback_.Reset();
  }

 private:
  std::string pending_id_;
  GURL pending_url_;
  ImageFetcherCallback pending_callback_;
};

DoodleConfig CreateConfig(DoodleType type) {
  return DoodleConfig(type, DoodleImage(GURL("https://doodle.com/image.jpg")));
}

MATCHER(IsEmptyImage, "") {
  return arg.IsEmpty();
}

}  // namespace

class DoodleServiceTest : public testing::Test {
 public:
  DoodleServiceTest()
      : task_runner_(new base::TestMockTimeTaskRunner()),
        task_runner_handle_(task_runner_),
        tick_clock_(task_runner_->GetMockTickClock()),
        fetcher_(nullptr),
        expiry_timer_(nullptr) {
    DoodleService::RegisterProfilePrefs(pref_service_.registry());

    task_runner_->FastForwardBy(base::TimeDelta::FromHours(12345));

    // Set the minimum refresh interval to 0 by default, so tests don't have to
    // worry about it. The tests that care set it explicitly.
    RecreateServiceWithZeroRefreshInterval();
  }

  void TearDown() override { DestroyService(); }

  void DestroyService() {
    if (image_fetcher_) {
      // Make sure we didn't receive an unexpected image request.
      ASSERT_FALSE(image_fetcher_->HasPendingRequest());
    }

    fetcher_ = nullptr;
    expiry_timer_ = nullptr;
    image_fetcher_ = nullptr;

    service_ = nullptr;
  }

  void RecreateServiceWithZeroRefreshInterval() {
    RecreateService(/*min_refresh_interval=*/base::TimeDelta());
  }

  void RecreateService(base::Optional<base::TimeDelta> refresh_interval) {
    auto expiry_timer = base::MakeUnique<base::OneShotTimer>(tick_clock_.get());
    expiry_timer->SetTaskRunner(task_runner_);
    expiry_timer_ = expiry_timer.get();

    auto fetcher = base::MakeUnique<FakeDoodleFetcher>();
    fetcher_ = fetcher.get();

    auto image_fetcher = base::MakeUnique<FakeImageFetcher>();
    image_fetcher_ = image_fetcher.get();

    service_ = base::MakeUnique<DoodleService>(
        &pref_service_, std::move(fetcher), std::move(expiry_timer),
        task_runner_->GetMockClock(), task_runner_->GetMockTickClock(),
        refresh_interval, std::move(image_fetcher));
  }

  DoodleService* service() { return service_.get(); }
  FakeDoodleFetcher* fetcher() { return fetcher_; }
  FakeImageFetcher* image_fetcher() { return image_fetcher_; }

  base::TestMockTimeTaskRunner* task_runner() { return task_runner_.get(); }

 private:
  TestingPrefServiceSimple pref_service_;

  scoped_refptr<base::TestMockTimeTaskRunner> task_runner_;
  base::ThreadTaskRunnerHandle task_runner_handle_;
  std::unique_ptr<base::TickClock> tick_clock_;

  std::unique_ptr<DoodleService> service_;

  // Weak, owned by the service.
  FakeDoodleFetcher* fetcher_;
  base::OneShotTimer* expiry_timer_;
  FakeImageFetcher* image_fetcher_;
};

TEST_F(DoodleServiceTest, FetchesConfigOnRefresh) {
  ASSERT_THAT(service()->config(), Eq(base::nullopt));

  // Request a refresh of the doodle config.
  service()->Refresh();
  // The request should have arrived at the fetcher.
  EXPECT_THAT(fetcher()->num_pending_callbacks(), Eq(1u));

  // Serve it (with an arbitrary config).
  DoodleConfig config = CreateConfig(DoodleType::SIMPLE);
  fetcher()->ServeAllCallbacks(DoodleState::AVAILABLE,
                               base::TimeDelta::FromHours(1), config);

  // The config should be available.
  EXPECT_THAT(service()->config(), Eq(config));

  // Request a refresh again.
  service()->Refresh();
  // The request should have arrived at the fetcher again.
  EXPECT_THAT(fetcher()->num_pending_callbacks(), Eq(1u));

  // Serve it with a different config.
  DoodleConfig other_config = CreateConfig(DoodleType::SLIDESHOW);
  DCHECK(config != other_config);
  fetcher()->ServeAllCallbacks(DoodleState::AVAILABLE,
                               base::TimeDelta::FromHours(1), other_config);

  // The config should have been updated.
  EXPECT_THAT(service()->config(), Eq(other_config));
}

TEST_F(DoodleServiceTest, CoalescesRefreshCalls) {
  ASSERT_THAT(service()->config(), Eq(base::nullopt));

  // Request a refresh of the doodle config, twice.
  service()->Refresh();
  service()->Refresh();
  // Only one request should have arrived at the fetcher.
  EXPECT_THAT(fetcher()->num_pending_callbacks(), Eq(1u));
}

TEST_F(DoodleServiceTest, PersistsConfig) {
  // Load some doodle config.
  service()->Refresh();
  DoodleConfig config = CreateConfig(DoodleType::SIMPLE);
  config.large_image.url = GURL("https://doodle.com/doodle.jpg");
  fetcher()->ServeAllCallbacks(DoodleState::AVAILABLE,
                               base::TimeDelta::FromHours(1), config);
  ASSERT_THAT(service()->config(), Eq(config));

  // Re-create the service. It should have persisted the config, and load it
  // again automatically.
  RecreateServiceWithZeroRefreshInterval();
  EXPECT_THAT(service()->config(), Eq(config));
}

TEST_F(DoodleServiceTest, FetchesOnCreationIfEmpty) {
  ASSERT_THAT(service()->config(), Eq(base::nullopt));

  // Since there was no cached config, the service should have sent a request.
  EXPECT_THAT(fetcher()->num_pending_callbacks(), Eq(1u));
}

TEST_F(DoodleServiceTest, DoesNotFetchOnCreationWithCachedConfig) {
  // Load some doodle config.
  service()->Refresh();
  DoodleConfig config = CreateConfig(DoodleType::SIMPLE);
  fetcher()->ServeAllCallbacks(DoodleState::AVAILABLE,
                               base::TimeDelta::FromHours(1), config);
  ASSERT_THAT(service()->config(), Eq(config));

  RecreateServiceWithZeroRefreshInterval();

  // Since there was a cached config, there should be no refresh request.
  EXPECT_THAT(fetcher()->num_pending_callbacks(), Eq(0u));
}

TEST_F(DoodleServiceTest, PersistsExpiryDate) {
  // Load some doodle config.
  service()->Refresh();
  DoodleConfig config = CreateConfig(DoodleType::SIMPLE);
  config.large_image.url = GURL("https://doodle.com/doodle.jpg");
  fetcher()->ServeAllCallbacks(DoodleState::AVAILABLE,
                               base::TimeDelta::FromHours(1), config);
  ASSERT_THAT(service()->config(), Eq(config));

  // Destroy the service, and let some time pass.
  DestroyService();
  task_runner()->FastForwardBy(base::TimeDelta::FromMinutes(15));

  // Remove the abandoned expiry task from the task runner.
  ASSERT_THAT(task_runner()->GetPendingTaskCount(), Eq(1u));
  task_runner()->ClearPendingTasks();

  // Re-create the service. The persisted config should have been loaded again.
  RecreateServiceWithZeroRefreshInterval();
  EXPECT_THAT(service()->config(), Eq(config));

  // Its time-to-live should have been updated.
  EXPECT_THAT(task_runner()->GetPendingTaskCount(), Eq(1u));
  EXPECT_THAT(task_runner()->NextPendingTaskDelay(),
              Eq(base::TimeDelta::FromMinutes(45)));
}

TEST_F(DoodleServiceTest, PersistedConfigExpires) {
  // Load some doodle config.
  service()->Refresh();
  DoodleConfig config = CreateConfig(DoodleType::SIMPLE);
  config.large_image.url = GURL("https://doodle.com/doodle.jpg");
  fetcher()->ServeAllCallbacks(DoodleState::AVAILABLE,
                               base::TimeDelta::FromHours(1), config);
  ASSERT_THAT(service()->config(), Eq(config));

  // Destroy the service, and let enough time pass so that the config expires.
  DestroyService();
  task_runner()->FastForwardBy(base::TimeDelta::FromHours(1));

  // Re-create the service. The persisted config should have been discarded
  // because it is expired.
  RecreateServiceWithZeroRefreshInterval();
  EXPECT_THAT(service()->config(), Eq(base::nullopt));
}

TEST_F(DoodleServiceTest, RespectsMinRefreshInterval) {
  // Create a service with the default refresh interval.
  RecreateService(/*min_refresh_interval=*/base::nullopt);

  // Load some doodle config.
  service()->Refresh();
  DoodleConfig config = CreateConfig(DoodleType::SIMPLE);
  config.large_image.url = GURL("https://doodle.com/doodle.jpg");
  fetcher()->ServeAllCallbacks(DoodleState::AVAILABLE,
                               base::TimeDelta::FromHours(1), config);
  ASSERT_THAT(service()->config(), Eq(config));

  // Let some time pass (less than the refresh interval).
  task_runner()->FastForwardBy(base::TimeDelta::FromMinutes(10));

  // Request a refresh. This should get ignored.
  service()->Refresh();
  EXPECT_THAT(fetcher()->num_pending_callbacks(), Eq(0u));

  // Let more time pass, so we get past the refresh interval.
  task_runner()->FastForwardBy(base::TimeDelta::FromMinutes(6));

  // Now the refresh request should be honored again.
  service()->Refresh();
  EXPECT_THAT(fetcher()->num_pending_callbacks(), Eq(1u));
}

TEST_F(DoodleServiceTest, CallsObserverOnConfigReceived) {
  StrictMock<MockDoodleObserver> observer;
  service()->AddObserver(&observer);

  ASSERT_THAT(service()->config(), Eq(base::nullopt));

  // Request a refresh of the doodle config.
  service()->Refresh();
  // The request should have arrived at the fetcher.
  ASSERT_THAT(fetcher()->num_pending_callbacks(), Eq(1u));

  // Serve it (with an arbitrary config). The observer should get notified.
  DoodleConfig config = CreateConfig(DoodleType::SIMPLE);
  EXPECT_CALL(observer, OnDoodleConfigUpdated(Eq(config)));
  fetcher()->ServeAllCallbacks(DoodleState::AVAILABLE,
                               base::TimeDelta::FromHours(1), config);

  // Remove the observer before the service gets destroyed.
  service()->RemoveObserver(&observer);
}

TEST_F(DoodleServiceTest, CallsObserverOnConfigRemoved) {
  // Load some doodle config.
  service()->Refresh();
  DoodleConfig config = CreateConfig(DoodleType::SIMPLE);
  fetcher()->ServeAllCallbacks(DoodleState::AVAILABLE,
                               base::TimeDelta::FromHours(1), config);
  ASSERT_THAT(service()->config(), Eq(config));

  // Register an observer and request a refresh.
  StrictMock<MockDoodleObserver> observer;
  service()->AddObserver(&observer);
  service()->Refresh();
  ASSERT_THAT(fetcher()->num_pending_callbacks(), Eq(1u));

  // Serve the request with an empty doodle config. The observer should get
  // notified.
  EXPECT_CALL(observer, OnDoodleConfigUpdated(Eq(base::nullopt)));
  fetcher()->ServeAllCallbacks(DoodleState::NO_DOODLE, base::TimeDelta(),
                               base::nullopt);

  // Remove the observer before the service gets destroyed.
  service()->RemoveObserver(&observer);
}

TEST_F(DoodleServiceTest, CallsObserverOnConfigUpdated) {
  // Load some doodle config.
  service()->Refresh();
  DoodleConfig config = CreateConfig(DoodleType::SIMPLE);
  fetcher()->ServeAllCallbacks(DoodleState::AVAILABLE,
                               base::TimeDelta::FromHours(1), config);
  ASSERT_THAT(service()->config(), Eq(config));

  // Register an observer and request a refresh.
  StrictMock<MockDoodleObserver> observer;
  service()->AddObserver(&observer);
  service()->Refresh();
  ASSERT_THAT(fetcher()->num_pending_callbacks(), Eq(1u));

  // Serve the request with a different doodle config. The observer should get
  // notified.
  DoodleConfig other_config = CreateConfig(DoodleType::SLIDESHOW);
  DCHECK(config != other_config);
  EXPECT_CALL(observer, OnDoodleConfigUpdated(Eq(other_config)));
  fetcher()->ServeAllCallbacks(DoodleState::AVAILABLE,
                               base::TimeDelta::FromHours(1), other_config);

  // Remove the observer before the service gets destroyed.
  service()->RemoveObserver(&observer);
}

TEST_F(DoodleServiceTest, CallsObserverIfConfigRevalidatedByNetworkRequest) {
  // Load some doodle config.
  service()->Refresh();
  DoodleConfig config = CreateConfig(DoodleType::SIMPLE);
  fetcher()->ServeAllCallbacks(DoodleState::AVAILABLE,
                               base::TimeDelta::FromHours(1), config);
  ASSERT_THAT(service()->config(), Eq(config));

  // Let some time pass (more than the refresh interval).
  task_runner()->FastForwardBy(base::TimeDelta::FromMinutes(16));

  // Register an observer and request a refresh after refresh intervall passed.
  StrictMock<MockDoodleObserver> observer;
  EXPECT_CALL(observer, OnDoodleConfigRevalidated(Eq(/*from_cache=*/false)));
  service()->AddObserver(&observer);
  service()->Refresh();
  ASSERT_THAT(fetcher()->num_pending_callbacks(), Eq(1u));

  // Serve the request with an equivalent doodle config. The observer should
  // get notified about a (non-cached) revalidation.
  DoodleConfig equivalent_config = CreateConfig(DoodleType::SIMPLE);
  DCHECK(config == equivalent_config);
  fetcher()->ServeAllCallbacks(
      DoodleState::AVAILABLE, base::TimeDelta::FromHours(1), equivalent_config);

  // Remove the observer before the service gets destroyed.
  service()->RemoveObserver(&observer);
}

TEST_F(DoodleServiceTest, CallsObserverIfConfigRevalidatedByCache) {
  // Create a service with the default refresh interval.
  RecreateService(/*min_refresh_interval=*/base::nullopt);

  // Load some doodle config.
  service()->Refresh();
  DoodleConfig config = CreateConfig(DoodleType::SIMPLE);
  fetcher()->ServeAllCallbacks(DoodleState::AVAILABLE,
                               base::TimeDelta::FromHours(1), config);
  ASSERT_THAT(service()->config(), Eq(config));

  // Register an observer and request a refresh within refresh intervall.
  StrictMock<MockDoodleObserver> observer;
  EXPECT_CALL(observer, OnDoodleConfigRevalidated(Eq(/*from_cache=*/true)));
  service()->AddObserver(&observer);
  service()->Refresh();
  ASSERT_THAT(fetcher()->num_pending_callbacks(), Eq(0u));

  // Remove the observer before the service gets destroyed.
  service()->RemoveObserver(&observer);
}

TEST_F(DoodleServiceTest, CallsObserverWhenConfigExpires) {
  // Load some doodle config.
  service()->Refresh();
  DoodleConfig config = CreateConfig(DoodleType::SIMPLE);
  fetcher()->ServeAllCallbacks(DoodleState::AVAILABLE,
                               base::TimeDelta::FromHours(1), config);
  ASSERT_THAT(service()->config(), Eq(config));

  // Make sure the task arrived at the timer's task runner.
  EXPECT_THAT(task_runner()->GetPendingTaskCount(), Eq(1u));
  EXPECT_THAT(task_runner()->NextPendingTaskDelay(),
              Eq(base::TimeDelta::FromHours(1)));

  // Register an observer.
  StrictMock<MockDoodleObserver> observer;
  service()->AddObserver(&observer);

  // Fast-forward time so that the expiry task will run. The observer should get
  // notified that there's no config anymore.
  EXPECT_CALL(observer, OnDoodleConfigUpdated(Eq(base::nullopt)));
  task_runner()->FastForwardBy(base::TimeDelta::FromHours(1));

  // Remove the observer before the service gets destroyed.
  service()->RemoveObserver(&observer);
}

TEST_F(DoodleServiceTest, DisregardsAlreadyExpiredConfigs) {
  StrictMock<MockDoodleObserver> observer;
  service()->AddObserver(&observer);

  // If there was no config and an expired config is loaded, not having a config
  // must be revalidated.
  ASSERT_THAT(service()->config(), Eq(base::nullopt));

  // Load an already-expired config.
  service()->Refresh();
  DoodleConfig config = CreateConfig(DoodleType::SIMPLE);
  EXPECT_CALL(observer, OnDoodleConfigRevalidated(Eq(/*from_cache=*/false)));
  fetcher()->ServeAllCallbacks(DoodleState::AVAILABLE,
                               base::TimeDelta::FromSeconds(0), config);
  EXPECT_THAT(service()->config(), Eq(base::nullopt));

  // Load a doodle config as usual. Nothing to see here.
  service()->Refresh();
  EXPECT_CALL(observer, OnDoodleConfigUpdated(Eq(config)));
  fetcher()->ServeAllCallbacks(DoodleState::AVAILABLE,
                               base::TimeDelta::FromHours(1), config);
  ASSERT_THAT(service()->config(), Eq(config));

  // Now load an expired config again. The cached one should go away.
  service()->Refresh();
  EXPECT_CALL(observer, OnDoodleConfigUpdated(Eq(base::nullopt)));
  fetcher()->ServeAllCallbacks(DoodleState::AVAILABLE,
                               base::TimeDelta::FromSeconds(0), config);

  // Remove the observer before the service gets destroyed.
  service()->RemoveObserver(&observer);
}

TEST_F(DoodleServiceTest, ClampsTimeToLive) {
  // Load a config with an excessive time-to-live.
  service()->Refresh();
  DoodleConfig config = CreateConfig(DoodleType::SIMPLE);
  fetcher()->ServeAllCallbacks(DoodleState::AVAILABLE,
                               base::TimeDelta::FromDays(100), config);
  ASSERT_THAT(service()->config(), Eq(config));

  // The time-to-live should have been clamped to a reasonable maximum.
  ASSERT_THAT(task_runner()->GetPendingTaskCount(), Eq(1u));
  EXPECT_THAT(task_runner()->NextPendingTaskDelay(),
              Eq(base::TimeDelta::FromDays(30)));
}

TEST_F(DoodleServiceTest, RecordsMetricsForSuccessfulDownload) {
  base::HistogramTester histograms;

  // Load a doodle config as usual, but let it take some time.
  service()->Refresh();
  task_runner()->FastForwardBy(base::TimeDelta::FromSeconds(5));
  DoodleConfig config = CreateConfig(DoodleType::SIMPLE);
  fetcher()->ServeAllCallbacks(DoodleState::AVAILABLE,
                               base::TimeDelta::FromHours(1), config);
  ASSERT_THAT(service()->config(), Eq(config));

  // Metrics should have been recorded.
  histograms.ExpectUniqueSample("Doodle.ConfigDownloadOutcome",
                                0,  // OUTCOME_NEW_DOODLE
                                1);
  histograms.ExpectTotalCount("Doodle.ConfigDownloadTime", 1);
  histograms.ExpectTimeBucketCount("Doodle.ConfigDownloadTime",
                                   base::TimeDelta::FromSeconds(5), 1);
}

TEST_F(DoodleServiceTest, RecordsMetricsForEmptyDownload) {
  base::HistogramTester histograms;

  // Send a "no doodle" response after some time.
  service()->Refresh();
  task_runner()->FastForwardBy(base::TimeDelta::FromSeconds(5));
  fetcher()->ServeAllCallbacks(DoodleState::NO_DOODLE, base::TimeDelta(),
                               base::nullopt);
  ASSERT_THAT(service()->config(), Eq(base::nullopt));

  // Metrics should have been recorded.
  histograms.ExpectUniqueSample("Doodle.ConfigDownloadOutcome",
                                3,  // OUTCOME_NO_DOODLE
                                1);
  histograms.ExpectTotalCount("Doodle.ConfigDownloadTime", 1);
  histograms.ExpectTimeBucketCount("Doodle.ConfigDownloadTime",
                                   base::TimeDelta::FromSeconds(5), 1);
}

TEST_F(DoodleServiceTest, RecordsMetricsForFailedDownload) {
  base::HistogramTester histograms;

  // Let the download fail after some time.
  service()->Refresh();
  task_runner()->FastForwardBy(base::TimeDelta::FromSeconds(5));
  fetcher()->ServeAllCallbacks(DoodleState::DOWNLOAD_ERROR, base::TimeDelta(),
                               base::nullopt);
  ASSERT_THAT(service()->config(), Eq(base::nullopt));

  // The outcome should have been recorded, but not the time - we only record
  // that for successful downloads.
  histograms.ExpectUniqueSample("Doodle.ConfigDownloadOutcome",
                                5,  // OUTCOME_DOWNLOAD_ERROR
                                1);
  histograms.ExpectTotalCount("Doodle.ConfigDownloadTime", 0);
}

TEST_F(DoodleServiceTest, RecordsMetricsForEarlyRefreshRequest) {
  // Create a service with some refresh interval.
  RecreateService(/*min_refresh_interval=*/base::TimeDelta::FromMinutes(10));

  // Load a doodle config as usual.
  service()->Refresh();
  DoodleConfig config = CreateConfig(DoodleType::SIMPLE);
  fetcher()->ServeAllCallbacks(DoodleState::AVAILABLE,
                               base::TimeDelta::FromHours(1), config);
  ASSERT_THAT(service()->config(), Eq(config));

  base::HistogramTester histograms;

  // Request a refresh before the min refresh interval has passed.
  task_runner()->FastForwardBy(base::TimeDelta::FromMinutes(5));
  service()->Refresh();

  // This should not have resulted in a request.
  ASSERT_THAT(fetcher()->num_pending_callbacks(), Eq(0u));

  // The outcome should still have been recorded, but not the time - we only
  // record that for successful downloads.
  histograms.ExpectUniqueSample("Doodle.ConfigDownloadOutcome",
                                7,  // OUTCOME_REFRESH_INTERVAL_NOT_PASSED
                                1);
  histograms.ExpectTotalCount("Doodle.ConfigDownloadTime", 0);
}

TEST_F(DoodleServiceTest, DoesNotRecordMetricsAtStartup) {
  // Creating the service should not emit any histogram samples.
  base::HistogramTester histograms;
  RecreateServiceWithZeroRefreshInterval();
  ASSERT_THAT(service()->config(), Eq(base::nullopt));
  histograms.ExpectTotalCount("Doodle.ConfigDownloadOutcome", 0);
  histograms.ExpectTotalCount("Doodle.ConfigDownloadTime", 0);
}

TEST_F(DoodleServiceTest, DoesNotRecordMetricsAtStartupWithConfig) {
  // Load a doodle config as usual.
  service()->Refresh();
  DoodleConfig config = CreateConfig(DoodleType::SIMPLE);
  fetcher()->ServeAllCallbacks(DoodleState::AVAILABLE,
                               base::TimeDelta::FromHours(1), config);
  ASSERT_THAT(service()->config(), Eq(config));

  // Recreating the service should not emit any histogram samples, even though
  // a config gets loaded.
  base::HistogramTester histograms;
  RecreateServiceWithZeroRefreshInterval();
  ASSERT_THAT(service()->config(), Eq(config));
  histograms.ExpectTotalCount("Doodle.ConfigDownloadOutcome", 0);
  histograms.ExpectTotalCount("Doodle.ConfigDownloadTime", 0);
}

TEST_F(DoodleServiceTest, DoesNotRecordMetricsWhenConfigExpires) {
  // Load some doodle config.
  service()->Refresh();
  DoodleConfig config = CreateConfig(DoodleType::SIMPLE);
  fetcher()->ServeAllCallbacks(DoodleState::AVAILABLE,
                               base::TimeDelta::FromHours(1), config);
  ASSERT_THAT(service()->config(), Eq(config));

  base::HistogramTester histograms;

  // Fast-forward time so that the config expires.
  task_runner()->FastForwardBy(base::TimeDelta::FromHours(1));
  ASSERT_THAT(service()->config(), Eq(base::nullopt));

  // This should not have resulted in any metrics being emitted.
  histograms.ExpectTotalCount("Doodle.ConfigDownloadOutcome", 0);
  histograms.ExpectTotalCount("Doodle.ConfigDownloadTime", 0);
}

TEST_F(DoodleServiceTest, GetImageWithEmptyConfigReturnsImmediately) {
  ASSERT_THAT(service()->config(), Eq(base::nullopt));

  base::MockCallback<DoodleService::ImageCallback> callback;
  EXPECT_CALL(callback, Run(IsEmptyImage()));

  service()->GetImage(callback.Get());
}

TEST_F(DoodleServiceTest, GetImageFetchesLargeImage) {
  service()->Refresh();
  DoodleConfig config = CreateConfig(DoodleType::SIMPLE);
  fetcher()->ServeAllCallbacks(DoodleState::AVAILABLE,
                               base::TimeDelta::FromHours(1), config);
  ASSERT_THAT(service()->config(), Eq(config));

  base::MockCallback<DoodleService::ImageCallback> callback;
  service()->GetImage(callback.Get());

  EXPECT_EQ(config.large_image.url, image_fetcher()->pending_url());

  EXPECT_CALL(callback, Run(Not(IsEmptyImage())));
  gfx::Image image = gfx::test::CreateImage(1, 1);
  ASSERT_TRUE(image_fetcher()->HasPendingRequest());
  image_fetcher()->RespondToPendingRequest(image);
}

TEST_F(DoodleServiceTest, GetImageFetchesCTAImage) {
  service()->Refresh();
  DoodleConfig config = CreateConfig(DoodleType::SIMPLE);
  // Set a CTA image, which should take precedence over the regular image.
  config.large_image.is_animated_gif = true;
  config.large_cta_image = DoodleImage(GURL("https://doodle.com/cta.jpg"));
  config.large_cta_image->is_cta = true;
  fetcher()->ServeAllCallbacks(DoodleState::AVAILABLE,
                               base::TimeDelta::FromHours(1), config);
  ASSERT_THAT(service()->config(), Eq(config));

  base::MockCallback<DoodleService::ImageCallback> callback;
  service()->GetImage(callback.Get());

  // If the doodle has a CTA image, that should loaded instead of the regular
  // large image.
  EXPECT_EQ(config.large_cta_image->url, image_fetcher()->pending_url());

  EXPECT_CALL(callback, Run(Not(IsEmptyImage())));
  gfx::Image image = gfx::test::CreateImage(1, 1);
  ASSERT_TRUE(image_fetcher()->HasPendingRequest());
  image_fetcher()->RespondToPendingRequest(image);
}

}  // namespace doodle
