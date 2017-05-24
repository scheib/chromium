// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/favicon/core/favicon_handler.h"

#include <stddef.h>

#include <map>
#include <memory>
#include <set>
#include <vector>

#include "base/macros.h"
#include "base/memory/ptr_util.h"
#include "base/run_loop.h"
#include "base/strings/stringprintf.h"
#include "base/test/histogram_tester.h"
#include "base/test/scoped_feature_list.h"
#include "base/test/scoped_task_environment.h"
#include "base/test/test_simple_task_runner.h"
#include "components/favicon/core/favicon_driver.h"
#include "components/favicon/core/features.h"
#include "components/favicon/core/test/mock_favicon_service.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/layout.h"
#include "ui/gfx/codec/png_codec.h"
#include "ui/gfx/favicon_size.h"
#include "ui/gfx/image/image.h"

namespace favicon {
namespace {

using favicon_base::FAVICON;
using favicon_base::FaviconRawBitmapResult;
using favicon_base::TOUCH_ICON;
using favicon_base::TOUCH_PRECOMPOSED_ICON;
using testing::AnyNumber;
using testing::Assign;
using testing::Contains;
using testing::ElementsAre;
using testing::InSequence;
using testing::Invoke;
using testing::IsEmpty;
using testing::Not;
using testing::Return;
using testing::_;

using DownloadOutcome = FaviconHandler::DownloadOutcome;
using IntVector = std::vector<int>;
using URLVector = std::vector<GURL>;
using BitmapVector = std::vector<SkBitmap>;
using SizeVector = std::vector<gfx::Size>;

MATCHER_P2(ImageSizeIs, width, height, "") {
  *result_listener << "where size is " << arg.Width() << "x" << arg.Height();
  return arg.Size() == gfx::Size(width, height);
}

// |arg| is a gfx::Image.
MATCHER_P(ImageColorIs, expected_color, "") {
  SkBitmap bitmap = arg.AsBitmap();
  if (bitmap.empty()) {
    *result_listener << "expected color but no bitmap data available";
    return false;
  }

  SkColor actual_color = bitmap.getColor(1, 1);
  if (actual_color != expected_color) {
    *result_listener << "expected color "
                     << base::StringPrintf("%08X", expected_color)
                     << " but actual color is "
                     << base::StringPrintf("%08X", actual_color);
    return false;
  }
  return true;
}

SkBitmap CreateBitmapWithEdgeSize(int size, SkColor color) {
  SkBitmap bmp;
  bmp.allocN32Pixels(size, size);
  bmp.eraseColor(color);
  return bmp;
}

// Fill the given data buffer with valid png data.
std::vector<unsigned char> FillBitmapWithEdgeSize(int size, SkColor color) {
  SkBitmap bitmap = CreateBitmapWithEdgeSize(size, color);
  std::vector<unsigned char> output;
  gfx::PNGCodec::EncodeBGRASkBitmap(bitmap, false, &output);
  return output;
}

std::vector<FaviconRawBitmapResult> CreateRawBitmapResult(
    const GURL& icon_url,
    favicon_base::IconType icon_type = FAVICON,
    bool expired = false,
    int edge_size = gfx::kFaviconSize,
    SkColor color = SK_ColorRED) {
  scoped_refptr<base::RefCountedBytes> data(new base::RefCountedBytes());
  data->data() = FillBitmapWithEdgeSize(edge_size, color);
  FaviconRawBitmapResult bitmap_result;
  bitmap_result.expired = expired;
  bitmap_result.bitmap_data = data;
  // Use a pixel size other than (0,0) as (0,0) has a special meaning.
  bitmap_result.pixel_size = gfx::Size(edge_size, edge_size);
  bitmap_result.icon_type = icon_type;
  bitmap_result.icon_url = icon_url;
  return {bitmap_result};
}

// Fake that implements the calls to FaviconHandler::Delegate's DownloadImage(),
// delegated to this class through MockDelegate.
class FakeImageDownloader {
 public:
  struct Response {
    int http_status_code = 404;
    BitmapVector bitmaps;
    SizeVector original_bitmap_sizes;
  };

  // |downloads| must not be nullptr and must outlive this object.
  FakeImageDownloader(URLVector* downloads)
      : downloads_(downloads), next_download_id_(1) {}

  // Implementation of FaviconHalder::Delegate's DownloadImage(). If a given
  // URL is not known (i.e. not previously added via Add()), it produces 404s.
  int DownloadImage(const GURL& url,
                    int max_image_size,
                    FaviconHandler::Delegate::ImageDownloadCallback callback) {
    downloads_->push_back(url);

    const Response& response = responses_[url];
    int download_id = next_download_id_++;
    base::Closure bound_callback =
        base::Bind(callback, download_id, response.http_status_code, url,
                   response.bitmaps, response.original_bitmap_sizes);
    if (url == manual_callback_url_)
      manual_callbacks_.push_back(bound_callback);
    else
      base::ThreadTaskRunnerHandle::Get()->PostTask(FROM_HERE, bound_callback);
    return download_id;
  }

  void Add(const GURL& icon_url,
           const IntVector& sizes,
           const IntVector& original_sizes,
           SkColor color) {
    DCHECK_EQ(sizes.size(), original_sizes.size());
    Response response;
    response.http_status_code = 200;
    for (int size : sizes) {
      response.original_bitmap_sizes.push_back(gfx::Size(size, size));
      response.bitmaps.push_back(CreateBitmapWithEdgeSize(size, color));
    }
    responses_[icon_url] = response;
  }

  // Simpler overload of the function above.
  void Add(const GURL& icon_url, const IntVector& sizes) {
    Add(icon_url, sizes, sizes, SK_ColorRED);
  }

  void AddError(const GURL& icon_url, int http_status_code) {
    Response response;
    response.http_status_code = http_status_code;
    responses_[icon_url] = response;
  }

  // Disables automatic callback for |url|. This is useful for emulating a
  // download taking a long time. The callback for DownloadImage() will be
  // stored in |manual_callbacks_|.
  void SetRunCallbackManuallyForUrl(const GURL& url) {
    manual_callback_url_ = url;
  }

  // Returns whether an ongoing download exists for a url previously selected
  // via SetRunCallbackManuallyForUrl().
  bool HasPendingManualCallback() { return !manual_callbacks_.empty(); }

  // Triggers responses for downloads previously selected for manual triggering
  // via SetRunCallbackManuallyForUrl().
  bool RunCallbackManually() {
    if (!HasPendingManualCallback())
      return false;
    for (base::Closure& callback : std::move(manual_callbacks_))
      callback.Run();
    return true;
  }

 private:
  URLVector* downloads_;
  int next_download_id_;

  // URL to disable automatic callbacks for.
  GURL manual_callback_url_;

  // Callback for DownloadImage() request for |manual_callback_url_|.
  std::vector<base::Closure> manual_callbacks_;

  // Registered responses.
  std::map<GURL, Response> responses_;

  DISALLOW_COPY_AND_ASSIGN(FakeImageDownloader);
};

// Fake that implements the calls to FaviconHandler::Delegate's
// DownloadManifest(), delegated to this class through MockDelegate.
class FakeManifestDownloader {
 public:
  struct Response {
    std::vector<favicon::FaviconURL> favicon_urls;
  };

  // |downloads| must not be nullptr and must outlive this object.
  FakeManifestDownloader(URLVector* downloads) : downloads_(downloads) {}

  // Implementation of FaviconHalder::Delegate's DownloadManifest(). If a given
  // URL is not known (i.e. not previously added via Add()), it produces 404s.
  void DownloadManifest(
      const GURL& url,
      FaviconHandler::Delegate::ManifestDownloadCallback callback) {
    downloads_->push_back(url);

    const Response& response = responses_[url];
    base::Closure bound_callback = base::Bind(callback, response.favicon_urls);
    if (url == manual_callback_url_)
      manual_callbacks_.push_back(bound_callback);
    else
      base::ThreadTaskRunnerHandle::Get()->PostTask(FROM_HERE, bound_callback);
  }

  void Add(const GURL& manifest_url,
           const std::vector<favicon::FaviconURL>& favicon_urls) {
    Response response;
    response.favicon_urls = favicon_urls;
    responses_[manifest_url] = response;
  }

  void AddError(const GURL& manifest_url) {
    responses_[manifest_url] = Response();
  }

  // Disables automatic callback for |url|. This is useful for emulating a
  // download taking a long time. The callback for DownloadManifest() will be
  // stored in |manual_callback_|.
  void SetRunCallbackManuallyForUrl(const GURL& url) {
    manual_callback_url_ = url;
  }

  // Returns whether an ongoing download exists for a url previously selected
  // via SetRunCallbackManuallyForUrl().
  bool HasPendingManualCallback() { return !manual_callbacks_.empty(); }

  // Triggers responses for downloads previously selected for manual triggering
  // via SetRunCallbackManuallyForUrl().
  bool RunCallbackManually() {
    if (!HasPendingManualCallback())
      return false;
    for (base::Closure& callback : std::move(manual_callbacks_))
      callback.Run();
    return true;
  }

 private:
  URLVector* downloads_;

  // URL to disable automatic callbacks for.
  GURL manual_callback_url_;

  // Callback for DownloadManifest() request for |manual_callback_url_|.
  std::vector<base::Closure> manual_callbacks_;

  // Registered responses.
  std::map<GURL, Response> responses_;

  DISALLOW_COPY_AND_ASSIGN(FakeManifestDownloader);
};

class MockDelegate : public FaviconHandler::Delegate {
 public:
  MockDelegate()
      : fake_image_downloader_(&downloads_),
        fake_manifest_downloader_(&downloads_) {
    // Delegate image downloading to FakeImageDownloader.
    ON_CALL(*this, DownloadImage(_, _, _))
        .WillByDefault(Invoke(&fake_image_downloader_,
                              &FakeImageDownloader::DownloadImage));
    // Delegate manifest downloading to FakeManifestDownloader.
    ON_CALL(*this, DownloadManifest(_, _))
        .WillByDefault(Invoke(&fake_manifest_downloader_,
                              &FakeManifestDownloader::DownloadManifest));
  }

  MOCK_METHOD3(DownloadImage,
               int(const GURL& url,
                   int max_image_size,
                   ImageDownloadCallback callback));
  MOCK_METHOD2(DownloadManifest,
               void(const GURL& url, ManifestDownloadCallback callback));
  MOCK_METHOD0(IsOffTheRecord, bool());
  MOCK_METHOD1(IsBookmarked, bool(const GURL& url));
  MOCK_METHOD5(OnFaviconUpdated,
               void(const GURL& page_url,
                    FaviconDriverObserver::NotificationIconType type,
                    const GURL& icon_url,
                    bool icon_url_changed,
                    const gfx::Image& image));

  FakeImageDownloader& fake_image_downloader() {
    return fake_image_downloader_;
  }

  FakeManifestDownloader& fake_manifest_downloader() {
    return fake_manifest_downloader_;
  }

  // Returns pending and completed download URLs.
  const URLVector& downloads() const { return downloads_; }

  void ClearDownloads() { downloads_.clear(); }

 private:
  // Pending and completed download URLs.
  URLVector downloads_;
  FakeImageDownloader fake_image_downloader_;
  FakeManifestDownloader fake_manifest_downloader_;
};

// FakeFaviconService mimics a FaviconService backend that allows setting up
// test data stored via Store(). If Store() has not been called for a
// particular URL, the callback is called with empty database results.
class FakeFaviconService {
 public:
  FakeFaviconService()
      : manual_callback_task_runner_(new base::TestSimpleTaskRunner()) {}

  // Stores favicon with bitmap data in |results| at |page_url| and |icon_url|.
  void Store(const GURL& page_url,
             const GURL& icon_url,
             const std::vector<favicon_base::FaviconRawBitmapResult>& result) {
    results_[icon_url] = result;
    results_[page_url] = result;
  }

  // Returns pending and completed database request URLs.
  const URLVector& db_requests() const { return db_requests_; }

  void ClearDbRequests() { db_requests_.clear(); }

  base::CancelableTaskTracker::TaskId GetFavicon(
      const GURL& icon_url,
      favicon_base::IconType icon_type,
      int desired_size_in_dip,
      const favicon_base::FaviconResultsCallback& callback,
      base::CancelableTaskTracker* tracker) {
    return GetFaviconForPageOrIconURL(icon_url, callback, tracker);
  }

  base::CancelableTaskTracker::TaskId GetFaviconForPageURL(
      const GURL& page_url,
      int icon_types,
      int desired_size_in_dip,
      const favicon_base::FaviconResultsCallback& callback,
      base::CancelableTaskTracker* tracker) {
    return GetFaviconForPageOrIconURL(page_url, callback, tracker);
  }

  base::CancelableTaskTracker::TaskId UpdateFaviconMappingsAndFetch(
      const GURL& page_url,
      const GURL& icon_url,
      favicon_base::IconType icon_type,
      int desired_size_in_dip,
      const favicon_base::FaviconResultsCallback& callback,
      base::CancelableTaskTracker* tracker) {
    return GetFaviconForPageOrIconURL(icon_url, callback, tracker);
  }

  // Disables automatic callback for |url|. This is useful for emulating a
  // DB lookup taking a long time. The callback for
  // GetFaviconForPageOrIconURL() will be stored in |manual_callback_|.
  void SetRunCallbackManuallyForUrl(const GURL& url) {
    manual_callback_url_ = url;
  }

  // Returns whether an ongoing lookup exists for a url previously selected
  // via SetRunCallbackManuallyForUrl().
  bool HasPendingManualCallback() {
    return manual_callback_task_runner_->HasPendingTask();
  }

  // Triggers the response for a lookup previously selected for manual
  // triggering via SetRunCallbackManuallyForUrl().
  bool RunCallbackManually() {
    if (!HasPendingManualCallback())
      return false;
    manual_callback_task_runner_->RunPendingTasks();
    return true;
  }

 private:
  base::CancelableTaskTracker::TaskId GetFaviconForPageOrIconURL(
      const GURL& page_or_icon_url,
      const favicon_base::FaviconResultsCallback& callback,
      base::CancelableTaskTracker* tracker) {
    db_requests_.push_back(page_or_icon_url);

    base::Closure bound_callback =
        base::Bind(callback, results_[page_or_icon_url]);

    if (page_or_icon_url != manual_callback_url_) {
      return tracker->PostTask(base::ThreadTaskRunnerHandle::Get().get(),
                               FROM_HERE, bound_callback);
    }

    // We use PostTaskAndReply() to cause |callback| being run in the current
    // TaskRunner.
    return tracker->PostTaskAndReply(manual_callback_task_runner_.get(),
                                     FROM_HERE, base::Bind(&base::DoNothing),
                                     bound_callback);
  }

  std::map<GURL, std::vector<favicon_base::FaviconRawBitmapResult>> results_;
  URLVector db_requests_;

  // URL to disable automatic callbacks for.
  GURL manual_callback_url_;

  // Callback for GetFaviconForPageOrIconURL() request for
  // |manual_callback_url_|.
  scoped_refptr<base::TestSimpleTaskRunner> manual_callback_task_runner_;

  DISALLOW_COPY_AND_ASSIGN(FakeFaviconService);
};

// MockFaviconService subclass that delegates DB reads to FakeFaviconService.
class MockFaviconServiceWithFake : public MockFaviconService {
 public:
  MockFaviconServiceWithFake() {
    // Delegate the various methods that read from the DB.
    ON_CALL(*this, GetFavicon(_, _, _, _, _))
        .WillByDefault(Invoke(&fake_, &FakeFaviconService::GetFavicon));
    ON_CALL(*this, GetFaviconForPageURL(_, _, _, _, _))
        .WillByDefault(
            Invoke(&fake_, &FakeFaviconService::GetFaviconForPageURL));
    ON_CALL(*this, UpdateFaviconMappingsAndFetch(_, _, _, _, _, _))
        .WillByDefault(
            Invoke(&fake_, &FakeFaviconService::UpdateFaviconMappingsAndFetch));
  }

  FakeFaviconService* fake() { return &fake_; }

 private:
  FakeFaviconService fake_;

  DISALLOW_COPY_AND_ASSIGN(MockFaviconServiceWithFake);
};

class FaviconHandlerTest : public testing::Test {
 protected:
  const std::vector<gfx::Size> kEmptySizes;

  // Some known icons for which download will succeed.
  const GURL kPageURL = GURL("http://www.google.com");
  const GURL kIconURL10x10 = GURL("http://www.google.com/favicon10x10");
  const GURL kIconURL12x12 = GURL("http://www.google.com/favicon12x12");
  const GURL kIconURL16x16 = GURL("http://www.google.com/favicon16x16");
  const GURL kIconURL64x64 = GURL("http://www.google.com/favicon64x64");

  FaviconHandlerTest()
      : scoped_task_environment_(
            base::test::ScopedTaskEnvironment::MainThreadType::UI) {
    // Register various known icon URLs.
    delegate_.fake_image_downloader().Add(kIconURL10x10, IntVector{10});
    delegate_.fake_image_downloader().Add(kIconURL12x12, IntVector{12});
    delegate_.fake_image_downloader().Add(kIconURL16x16, IntVector{16});
    delegate_.fake_image_downloader().Add(kIconURL64x64, IntVector{64});

    // The score computed by SelectFaviconFrames() is dependent on the supported
    // scale factors of the platform. It is used for determining the goodness of
    // a downloaded bitmap in FaviconHandler::OnDidDownloadFavicon().
    // Force the values of the scale factors so that the tests produce the same
    // results on all platforms.
    scoped_set_supported_scale_factors_.reset(
        new ui::test::ScopedSetSupportedScaleFactors({ui::SCALE_FACTOR_100P}));
  }

  bool VerifyAndClearExpectations() {
    base::RunLoop().RunUntilIdle();
    favicon_service_.fake()->ClearDbRequests();
    delegate_.ClearDownloads();
    return testing::Mock::VerifyAndClearExpectations(&favicon_service_) &&
           testing::Mock::VerifyAndClearExpectations(&delegate_);
  }

  // Creates a new handler and feeds in the page URL and the candidates.
  // Returns the handler in case tests want to exercise further steps.
  std::unique_ptr<FaviconHandler> RunHandlerWithCandidates(
      FaviconDriverObserver::NotificationIconType handler_type,
      const std::vector<favicon::FaviconURL>& candidates,
      const GURL& manifest_url = GURL()) {
    auto handler = base::MakeUnique<FaviconHandler>(&favicon_service_,
                                                    &delegate_, handler_type);
    handler->FetchFavicon(kPageURL);
    // The first RunUntilIdle() causes the FaviconService lookups be faster than
    // OnUpdateCandidates(), which is the most likely scenario.
    base::RunLoop().RunUntilIdle();
    handler->OnUpdateCandidates(kPageURL, candidates, manifest_url);
    base::RunLoop().RunUntilIdle();
    return handler;
  }

  // Same as above, but for the simplest case where all types are FAVICON and
  // no sizes are provided, using a FaviconHandler of type NON_TOUCH_16_DIP.
  std::unique_ptr<FaviconHandler> RunHandlerWithSimpleFaviconCandidates(
      const std::vector<GURL>& urls,
      const GURL& manifest_url = GURL()) {
    std::vector<favicon::FaviconURL> candidates;
    for (const GURL& url : urls) {
      candidates.emplace_back(url, FAVICON, kEmptySizes);
    }
    return RunHandlerWithCandidates(FaviconDriverObserver::NON_TOUCH_16_DIP,
                                    candidates, manifest_url);
  }

  base::test::ScopedTaskEnvironment scoped_task_environment_;
  std::unique_ptr<ui::test::ScopedSetSupportedScaleFactors>
      scoped_set_supported_scale_factors_;
  testing::NiceMock<MockFaviconServiceWithFake> favicon_service_;
  testing::NiceMock<MockDelegate> delegate_;
};

TEST_F(FaviconHandlerTest, GetFaviconFromHistory) {
  base::HistogramTester histogram_tester;
  const GURL kIconURL("http://www.google.com/favicon");

  favicon_service_.fake()->Store(kPageURL, kIconURL,
                                 CreateRawBitmapResult(kIconURL));

  EXPECT_CALL(delegate_, OnFaviconUpdated(
                             kPageURL, FaviconDriverObserver::NON_TOUCH_16_DIP,
                             kIconURL, /*icon_url_changed=*/true, _));

  RunHandlerWithSimpleFaviconCandidates({kIconURL});
  EXPECT_THAT(delegate_.downloads(), IsEmpty());
  EXPECT_THAT(
      histogram_tester.GetAllSamples("Favicons.DownloadAttempts.LargeIcons"),
      IsEmpty());
  EXPECT_THAT(
      histogram_tester.GetAllSamples("Favicons.DownloadAttempts.Favicons"),
      IsEmpty());
}

// Test that UpdateFaviconsAndFetch() is called with the appropriate parameters
// when there is data in the database for neither the page URL nor the icon URL.
TEST_F(FaviconHandlerTest, UpdateFaviconMappingsAndFetch) {
  EXPECT_CALL(favicon_service_,
              UpdateFaviconMappingsAndFetch(kPageURL, kIconURL16x16, FAVICON,
                                            /*desired_size_in_dip=*/16, _, _));

  RunHandlerWithSimpleFaviconCandidates({kIconURL16x16});
}

// Test that the FaviconHandler process finishes when:
// - There is data in the database for neither the page URL nor the icon URL.
// AND
// - FaviconService::GetFaviconForPageURL() callback returns before
//   FaviconHandler::OnUpdateCandidates() is called.
TEST_F(FaviconHandlerTest, DownloadUnknownFaviconIfCandidatesSlower) {
  // Defer the database lookup completion to control the exact timing.
  favicon_service_.fake()->SetRunCallbackManuallyForUrl(kPageURL);

  EXPECT_CALL(favicon_service_, SetFavicons(_, _, _, _)).Times(0);
  EXPECT_CALL(delegate_, OnFaviconUpdated(_, _, _, _, _)).Times(0);

  FaviconHandler handler(&favicon_service_, &delegate_,
                         FaviconDriverObserver::NON_TOUCH_16_DIP);
  handler.FetchFavicon(kPageURL);
  base::RunLoop().RunUntilIdle();
  // Database lookup for |kPageURL| is ongoing.
  ASSERT_TRUE(favicon_service_.fake()->HasPendingManualCallback());
  // Causes FaviconService lookups be faster than OnUpdateCandidates().
  ASSERT_TRUE(favicon_service_.fake()->RunCallbackManually());
  ASSERT_TRUE(VerifyAndClearExpectations());

  EXPECT_CALL(favicon_service_, SetFavicons(kPageURL, kIconURL16x16, FAVICON,
                                            ImageSizeIs(16, 16)));
  EXPECT_CALL(delegate_, OnFaviconUpdated(
                             kPageURL, FaviconDriverObserver::NON_TOUCH_16_DIP,
                             kIconURL16x16, /*icon_url_changed=*/true, _));
  // Feed in favicons now that the database lookup is completed.
  handler.OnUpdateCandidates(
      kPageURL, {FaviconURL(kIconURL16x16, FAVICON, kEmptySizes)}, GURL());
  base::RunLoop().RunUntilIdle();

  EXPECT_THAT(favicon_service_.fake()->db_requests(),
              ElementsAre(kIconURL16x16));
  EXPECT_THAT(delegate_.downloads(), ElementsAre(kIconURL16x16));
}

// Test that the FaviconHandler process finishes when:
// - There is data in the database for neither the page URL nor the icon URL.
// AND
// - FaviconService::GetFaviconForPageURL() callback returns after
//   FaviconHandler::OnUpdateCandidates() is called.
TEST_F(FaviconHandlerTest, DownloadUnknownFaviconIfCandidatesFaster) {
  // Defer the database lookup completion to control the exact timing.
  favicon_service_.fake()->SetRunCallbackManuallyForUrl(kPageURL);

  EXPECT_CALL(favicon_service_, SetFavicons(_, _, _, _)).Times(0);
  EXPECT_CALL(delegate_, OnFaviconUpdated(_, _, _, _, _)).Times(0);

  FaviconHandler handler(&favicon_service_, &delegate_,
                         FaviconDriverObserver::NON_TOUCH_16_DIP);
  handler.FetchFavicon(kPageURL);
  base::RunLoop().RunUntilIdle();
  // Feed in favicons before completing the database lookup.
  handler.OnUpdateCandidates(
      kPageURL, {FaviconURL(kIconURL16x16, FAVICON, kEmptySizes)}, GURL());

  ASSERT_TRUE(VerifyAndClearExpectations());
  // Database lookup for |kPageURL| is ongoing.
  ASSERT_TRUE(favicon_service_.fake()->HasPendingManualCallback());

  EXPECT_CALL(favicon_service_, SetFavicons(kPageURL, kIconURL16x16, FAVICON,
                                            ImageSizeIs(16, 16)));
  EXPECT_CALL(delegate_, OnFaviconUpdated(_, _, kIconURL16x16, _, _));

  // Complete the lookup for |kPageURL|.
  ASSERT_TRUE(favicon_service_.fake()->RunCallbackManually());
  base::RunLoop().RunUntilIdle();

  EXPECT_THAT(favicon_service_.fake()->db_requests(),
              ElementsAre(kIconURL16x16));
  EXPECT_THAT(delegate_.downloads(), ElementsAre(kIconURL16x16));
}

// Test that the FaviconHandler process does not save anything to the database
// for incognito tabs.
TEST_F(FaviconHandlerTest, DownloadUnknownFaviconInIncognito) {
  ON_CALL(delegate_, IsOffTheRecord()).WillByDefault(Return(true));

  // No writes expected.
  EXPECT_CALL(favicon_service_, UpdateFaviconMappingsAndFetch(_, _, _, _, _, _))
      .Times(0);
  EXPECT_CALL(favicon_service_, SetFavicons(_, _, _, _)).Times(0);

  EXPECT_CALL(delegate_, OnFaviconUpdated(_, _, kIconURL16x16, _, _));

  RunHandlerWithSimpleFaviconCandidates({kIconURL16x16});
  EXPECT_THAT(delegate_.downloads(), ElementsAre(kIconURL16x16));
  EXPECT_THAT(favicon_service_.fake()->db_requests(),
              ElementsAre(kPageURL, kIconURL16x16));
}

// Test that the FaviconHandler saves a favicon if the page is bookmarked, even
// in incognito.
TEST_F(FaviconHandlerTest, DownloadBookmarkedFaviconInIncognito) {
  ON_CALL(delegate_, IsOffTheRecord()).WillByDefault(Return(true));
  ON_CALL(delegate_, IsBookmarked(kPageURL)).WillByDefault(Return(true));

  EXPECT_CALL(favicon_service_, UpdateFaviconMappingsAndFetch(_, _, _, _, _, _))
      .Times(0);

  EXPECT_CALL(favicon_service_, SetFavicons(_, kIconURL16x16, _, _));

  RunHandlerWithSimpleFaviconCandidates({kIconURL16x16});
  EXPECT_THAT(delegate_.downloads(), ElementsAre(kIconURL16x16));
}

// Test that the icon is redownloaded if the icon cached for the page URL
// expired.
TEST_F(FaviconHandlerTest, RedownloadExpiredPageUrlFavicon) {
  const GURL kIconURL("http://www.google.com/favicon");
  const SkColor kOldColor = SK_ColorBLUE;
  const SkColor kNewColor = SK_ColorGREEN;

  favicon_service_.fake()->Store(
      kPageURL, kIconURL,
      CreateRawBitmapResult(kIconURL, FAVICON, /*expired=*/true,
                            gfx::kFaviconSize, kOldColor));

  delegate_.fake_image_downloader().Add(kIconURL, IntVector{gfx::kFaviconSize},
                                        IntVector{gfx::kFaviconSize},
                                        kNewColor);

  EXPECT_CALL(favicon_service_,
              SetFavicons(_, kIconURL, _, ImageColorIs(kNewColor)));

  InSequence seq;
  EXPECT_CALL(delegate_,
              OnFaviconUpdated(_, _, kIconURL, _, ImageColorIs(kOldColor)));
  EXPECT_CALL(delegate_,
              OnFaviconUpdated(_, _, kIconURL, _, ImageColorIs(kNewColor)));

  RunHandlerWithSimpleFaviconCandidates({kIconURL});
  // We know from the |kPageUrl| database request that |kIconURL| has expired. A
  // second request for |kIconURL| should not have been made because it is
  // redundant.
  EXPECT_THAT(favicon_service_.fake()->db_requests(), ElementsAre(kPageURL));
  EXPECT_THAT(delegate_.downloads(), ElementsAre(kIconURL));
}

// Test that FaviconHandler requests the new data when:
// - There is valid data in the database for the page URL.
// AND
// - The icon URL used by the page has changed.
// AND
// - There is no data in database for the new icon URL.
TEST_F(FaviconHandlerTest, UpdateAndDownloadFavicon) {
  const GURL kOldIconURL("http://www.google.com/old_favicon");
  const GURL kNewIconURL = kIconURL16x16;

  favicon_service_.fake()->Store(kPageURL, kOldIconURL,
                                 CreateRawBitmapResult(kOldIconURL));

  InSequence seq;
  EXPECT_CALL(delegate_, OnFaviconUpdated(_, _, kOldIconURL, _, _));
  EXPECT_CALL(favicon_service_, SetFavicons(_, kNewIconURL, _, _));
  EXPECT_CALL(delegate_, OnFaviconUpdated(_, _, kNewIconURL, _, _));

  RunHandlerWithSimpleFaviconCandidates({kNewIconURL});
  EXPECT_THAT(delegate_.downloads(), ElementsAre(kNewIconURL));
}

// If there is data for the page URL in history which is invalid, test that:
// - The invalid data is not sent to the UI.
// - The icon is redownloaded.
TEST_F(FaviconHandlerTest, FaviconInHistoryInvalid) {
  const GURL kIconURL("http://www.google.com/favicon");

  delegate_.fake_image_downloader().Add(kIconURL, IntVector{gfx::kFaviconSize},
                                        IntVector{gfx::kFaviconSize},
                                        SK_ColorBLUE);

  // Set non-empty but invalid data.
  std::vector<FaviconRawBitmapResult> bitmap_result =
      CreateRawBitmapResult(kIconURL);
  // Empty bitmap data is invalid.
  bitmap_result[0].bitmap_data = new base::RefCountedBytes();

  favicon_service_.fake()->Store(kPageURL, kIconURL, bitmap_result);

  EXPECT_CALL(delegate_,
              OnFaviconUpdated(_, _, kIconURL, _, ImageColorIs(SK_ColorBLUE)));

  RunHandlerWithSimpleFaviconCandidates({kIconURL});

  EXPECT_THAT(favicon_service_.fake()->db_requests(), ElementsAre(kPageURL));
  EXPECT_THAT(delegate_.downloads(), ElementsAre(kIconURL));
}

// Test that no downloads are done if a user visits a page which changed its
// favicon URL to a favicon URL which is already cached in the database.
TEST_F(FaviconHandlerTest, UpdateFavicon) {
  const GURL kSomePreviousPageURL("https://www.google.com/previous");
  const GURL kIconURL("http://www.google.com/favicon");
  const GURL kNewIconURL("http://www.google.com/new_favicon");

  favicon_service_.fake()->Store(kPageURL, kIconURL,
                                 CreateRawBitmapResult(kIconURL));
  favicon_service_.fake()->Store(kSomePreviousPageURL, kNewIconURL,
                                 CreateRawBitmapResult(kNewIconURL));

  EXPECT_CALL(favicon_service_, SetFavicons(_, _, _, _)).Times(0);

  InSequence seq;
  EXPECT_CALL(delegate_, OnFaviconUpdated(_, _, kIconURL, _, _));
  EXPECT_CALL(delegate_, OnFaviconUpdated(_, _, kNewIconURL, _, _));

  RunHandlerWithSimpleFaviconCandidates({kNewIconURL});
  EXPECT_THAT(favicon_service_.fake()->db_requests(),
              ElementsAre(kPageURL, kNewIconURL));
  EXPECT_THAT(delegate_.downloads(), IsEmpty());
}

TEST_F(FaviconHandlerTest, Download2ndFaviconURLCandidate) {
  const GURL kIconURLReturning500("http://www.google.com/500.png");

  delegate_.fake_image_downloader().AddError(kIconURLReturning500, 500);

  favicon_service_.fake()->Store(
      kPageURL, kIconURL64x64,
      CreateRawBitmapResult(kIconURL64x64, TOUCH_ICON,
                            /*expired=*/true));

  EXPECT_CALL(delegate_,
              OnFaviconUpdated(kPageURL, FaviconDriverObserver::TOUCH_LARGEST,
                               kIconURL64x64, /*icon_url_changed=*/true, _));
  EXPECT_CALL(delegate_,
              OnFaviconUpdated(kPageURL, FaviconDriverObserver::TOUCH_LARGEST,
                               kIconURL64x64, /*icon_url_changed=*/false, _));

  RunHandlerWithCandidates(
      FaviconDriverObserver::TOUCH_LARGEST,
      {
          FaviconURL(kIconURLReturning500, TOUCH_PRECOMPOSED_ICON, kEmptySizes),
          FaviconURL(kIconURL64x64, TOUCH_ICON, kEmptySizes),
      });
  // First download fails, second succeeds.
  EXPECT_THAT(delegate_.downloads(),
              ElementsAre(kIconURLReturning500, kIconURL64x64));
}

// Test that download data for icon URLs other than the current favicon
// candidate URLs is ignored. This test tests the scenario where a download is
// in flight when FaviconHandler::OnUpdateCandidates() is called.
// TODO(mastiz): Make this test deal with FaviconURLs of type
// favicon_base::FAVICON and add new ones like OnlyDownloadMatchingIconType and
// CallSetFaviconsWithCorrectIconType.
TEST_F(FaviconHandlerTest, UpdateDuringDownloading) {
  const GURL kIconURL1("http://www.google.com/favicon");
  const GURL kIconURL2 = kIconURL16x16;
  const GURL kIconURL3 = kIconURL64x64;

  // Defer the download completion such that RunUntilIdle() doesn't complete
  // the download.
  delegate_.fake_image_downloader().SetRunCallbackManuallyForUrl(kIconURL1);

  delegate_.fake_image_downloader().Add(kIconURL1, IntVector{16});
  delegate_.fake_image_downloader().Add(kIconURL3, IntVector{64});

  std::unique_ptr<FaviconHandler> handler =
      RunHandlerWithSimpleFaviconCandidates({kIconURL1, kIconURL2});

  ASSERT_TRUE(VerifyAndClearExpectations());
  ASSERT_TRUE(delegate_.fake_image_downloader().HasPendingManualCallback());

  // Favicon update should invalidate the ongoing download.
  EXPECT_CALL(favicon_service_, SetFavicons(_, kIconURL3, _, _));
  EXPECT_CALL(delegate_, OnFaviconUpdated(_, _, kIconURL3, _, _));

  handler->OnUpdateCandidates(
      kPageURL, {FaviconURL(kIconURL3, FAVICON, kEmptySizes)}, GURL());

  // Finalizes download, which should be thrown away as the favicon URLs were
  // updated.
  EXPECT_TRUE(delegate_.fake_image_downloader().RunCallbackManually());
  base::RunLoop().RunUntilIdle();

  EXPECT_THAT(favicon_service_.fake()->db_requests(), ElementsAre(kIconURL3));
  EXPECT_THAT(delegate_.downloads(), ElementsAre(kIconURL3));
}

// Test that sending an icon URL update different to the previous icon URL
// update during a database lookup ignores the first icon URL and processes the
// second.
TEST_F(FaviconHandlerTest, UpdateDuringDatabaseLookup) {
  const GURL kIconURL1 = kIconURL16x16;
  const GURL kIconURL2 = kIconURL64x64;

  // Defer the lookup completion such that RunUntilIdle() doesn't complete the
  // lookup.
  favicon_service_.fake()->SetRunCallbackManuallyForUrl(kIconURL1);

  delegate_.fake_image_downloader().Add(kIconURL1, IntVector{16});
  delegate_.fake_image_downloader().Add(kIconURL2, IntVector{64});

  std::unique_ptr<FaviconHandler> handler =
      RunHandlerWithSimpleFaviconCandidates(URLVector{kIconURL1});

  ASSERT_TRUE(VerifyAndClearExpectations());
  ASSERT_TRUE(favicon_service_.fake()->HasPendingManualCallback());

  // SetFavicons() and OnFaviconUpdated() should be called for the new icon URL
  // and not |kIconURL1|.
  EXPECT_CALL(favicon_service_, SetFavicons(_, kIconURL2, _, _));
  EXPECT_CALL(delegate_, OnFaviconUpdated(_, _, kIconURL2, _, _));

  handler->OnUpdateCandidates(
      kPageURL, {FaviconURL(kIconURL2, FAVICON, kEmptySizes)}, GURL());

  // Finalizes the DB lookup, which should be thrown away as the favicon URLs
  // were updated.
  EXPECT_TRUE(favicon_service_.fake()->RunCallbackManually());
  base::RunLoop().RunUntilIdle();

  EXPECT_THAT(favicon_service_.fake()->db_requests(), ElementsAre(kIconURL2));
  EXPECT_THAT(delegate_.downloads(), ElementsAre(kIconURL2));
}

// Test that sending an icon URL update identical to the previous icon URL
// update during image download is a no-op.
TEST_F(FaviconHandlerTest, UpdateSameIconURLsWhileDownloadingShouldBeNoop) {
  const GURL kSlowLoadingIconURL("http://www.google.com/slow_favicon");

  const std::vector<FaviconURL> favicon_urls = {
      FaviconURL(kIconURL64x64, FAVICON, kEmptySizes),
      FaviconURL(kSlowLoadingIconURL, FAVICON, kEmptySizes),
  };

  // Defer the download completion such that RunUntilIdle() doesn't complete
  // the download.
  delegate_.fake_image_downloader().SetRunCallbackManuallyForUrl(
      kSlowLoadingIconURL);
  delegate_.fake_image_downloader().Add(kSlowLoadingIconURL, IntVector{16});

  std::unique_ptr<FaviconHandler> handler = RunHandlerWithCandidates(
      FaviconDriverObserver::NON_TOUCH_16_DIP, favicon_urls);

  ASSERT_THAT(favicon_service_.fake()->db_requests(),
              ElementsAre(kPageURL, kIconURL64x64, kSlowLoadingIconURL));
  ASSERT_TRUE(VerifyAndClearExpectations());
  ASSERT_TRUE(delegate_.fake_image_downloader().HasPendingManualCallback());

  // Calling OnUpdateCandidates() with the same icon URLs should have no effect,
  // despite the ongoing download.
  handler->OnUpdateCandidates(kPageURL, favicon_urls, GURL());
  base::RunLoop().RunUntilIdle();
  EXPECT_THAT(favicon_service_.fake()->db_requests(), IsEmpty());
  EXPECT_THAT(delegate_.downloads(), IsEmpty());

  // Complete the download.
  EXPECT_CALL(favicon_service_, SetFavicons(_, _, _, _));
  EXPECT_CALL(delegate_, OnFaviconUpdated(_, _, _, _, _));
  EXPECT_TRUE(delegate_.fake_image_downloader().RunCallbackManually());
  base::RunLoop().RunUntilIdle();
  EXPECT_THAT(delegate_.downloads(), IsEmpty());
}

// Test that sending an icon URL update identical to the previous icon URL
// update during a database lookup is a no-op.
TEST_F(FaviconHandlerTest, UpdateSameIconURLsWhileDatabaseLookupShouldBeNoop) {
  const std::vector<FaviconURL> favicon_urls = {
      FaviconURL(kIconURL64x64, FAVICON, kEmptySizes),
  };

  favicon_service_.fake()->SetRunCallbackManuallyForUrl(kIconURL64x64);

  std::unique_ptr<FaviconHandler> handler = RunHandlerWithCandidates(
      FaviconDriverObserver::NON_TOUCH_16_DIP, favicon_urls);

  // Ongoing database lookup.
  ASSERT_THAT(favicon_service_.fake()->db_requests(),
              ElementsAre(kPageURL, kIconURL64x64));
  ASSERT_THAT(delegate_.downloads(), IsEmpty());
  ASSERT_TRUE(VerifyAndClearExpectations());
  ASSERT_TRUE(favicon_service_.fake()->HasPendingManualCallback());

  // Calling OnUpdateCandidates() with the same icon URLs should have no effect,
  // despite the ongoing DB lookup.
  handler->OnUpdateCandidates(kPageURL, favicon_urls, GURL());
  base::RunLoop().RunUntilIdle();
  EXPECT_THAT(favicon_service_.fake()->db_requests(), IsEmpty());
  EXPECT_THAT(delegate_.downloads(), IsEmpty());

  // Complete the lookup.
  EXPECT_CALL(favicon_service_, SetFavicons(_, _, _, _));
  EXPECT_CALL(delegate_, OnFaviconUpdated(_, _, _, _, _));
  EXPECT_TRUE(favicon_service_.fake()->RunCallbackManually());
  base::RunLoop().RunUntilIdle();
  EXPECT_THAT(delegate_.downloads(), ElementsAre(kIconURL64x64));
}

// Test that calling OnUpdateFaviconUrl() with the same icon URLs as before is a
// no-op. This is important because OnUpdateFaviconUrl() is called when the page
// finishes loading. This can occur several times for pages with iframes.
TEST_F(FaviconHandlerTest, UpdateSameIconURLsAfterFinishedShouldBeNoop) {
  const std::vector<FaviconURL> favicon_urls = {
      FaviconURL(kIconURL10x10, FAVICON, kEmptySizes),
      FaviconURL(kIconURL16x16, FAVICON, kEmptySizes),
  };

  std::unique_ptr<FaviconHandler> handler = RunHandlerWithCandidates(
      FaviconDriverObserver::NON_TOUCH_16_DIP, favicon_urls);

  ASSERT_TRUE(VerifyAndClearExpectations());

  // Calling OnUpdateCandidates() with identical data should be a no-op.
  EXPECT_CALL(delegate_, OnFaviconUpdated(_, _, _, _, _)).Times(0);
  EXPECT_CALL(favicon_service_, SetFavicons(_, _, _, _)).Times(0);

  handler->OnUpdateCandidates(kPageURL, favicon_urls, GURL());
  base::RunLoop().RunUntilIdle();
  EXPECT_THAT(favicon_service_.fake()->db_requests(), IsEmpty());
  EXPECT_THAT(delegate_.downloads(), IsEmpty());
}

// Fixes crbug.com/544560
// Tests that Delegate::OnFaviconUpdated() is called if:
// - The best icon on the initial page is not the last icon.
// - All of the initial page's icons are downloaded.
// AND
// - JavaScript modifies the page's <link rel="icon"> tags to contain only the
//   last icon.
TEST_F(FaviconHandlerTest,
       OnFaviconAvailableNotificationSentAfterIconURLChange) {
  const GURL kIconURL1(
      "http://wwww.page_which_animates_favicon.com/frame1.png");
  const GURL kIconURL2(
      "http://wwww.page_which_animates_favicon.com/frame2.png");

  // |kIconURL1| is the better match.
  delegate_.fake_image_downloader().Add(kIconURL1, IntVector{15});
  delegate_.fake_image_downloader().Add(kIconURL2, IntVector{10});

  // Two FaviconDriver::OnFaviconUpdated() notifications should be sent for
  // |kIconURL1|, one before and one after the download.
  EXPECT_CALL(delegate_, OnFaviconUpdated(_, _, kIconURL1, _, _));

  std::unique_ptr<FaviconHandler> handler =
      RunHandlerWithSimpleFaviconCandidates({kIconURL1, kIconURL2});

  // Both |kIconURL1| and |kIconURL2| should have been requested from the
  // database and downloaded. |kIconURL2| should have been fetched from the
  // database and downloaded last.
  ASSERT_THAT(delegate_.downloads(), ElementsAre(kIconURL1, kIconURL2));
  ASSERT_THAT(favicon_service_.fake()->db_requests(),
              ElementsAre(kPageURL, kIconURL1, kIconURL2));
  ASSERT_TRUE(VerifyAndClearExpectations());

  // Simulate the page changing it's icon URL to just |kIconURL2| via
  // Javascript.
  EXPECT_CALL(delegate_, OnFaviconUpdated(_, _, kIconURL2, _, _));
  handler->OnUpdateCandidates(
      kPageURL, {FaviconURL(kIconURL2, FAVICON, kEmptySizes)}, GURL());
  base::RunLoop().RunUntilIdle();
}

// Test the favicon which is selected when the web page provides several
// favicons and none of the favicons are cached in history.
// The goal of this test is to be more of an integration test than
// SelectFaviconFramesTest.*.
class FaviconHandlerMultipleFaviconsTest : public FaviconHandlerTest {
 protected:
  FaviconHandlerMultipleFaviconsTest() {
    // Set the supported scale factors to 1x and 2x. This affects the behavior
    // of SelectFaviconFrames().
    scoped_set_supported_scale_factors_.reset();  // Need to delete first.
    scoped_set_supported_scale_factors_.reset(
        new ui::test::ScopedSetSupportedScaleFactors(
            {ui::SCALE_FACTOR_100P, ui::SCALE_FACTOR_200P}));
  }

  // Simulates requesting a favicon for |page_url| given:
  // - We have not previously cached anything in history for |page_url| or for
  //   any of candidates.
  // - The page provides favicons with edge pixel sizes of
  //   |candidate_icon_sizes|.
  // - Candidates are assumed of type FAVICON and the URLs are generated
  //   internally for testing purposes.
  //
  // Returns the chosen size among |candidate_icon_sizes| or -1 if none was
  // chosen.
  int DownloadTillDoneIgnoringHistory(const IntVector& candidate_icon_sizes) {
    std::vector<FaviconURL> candidate_icons;
    int chosen_icon_size = -1;

    for (int icon_size : candidate_icon_sizes) {
      const GURL icon_url(base::StringPrintf(
          "https://www.google.com/generated/%dx%d", icon_size, icon_size));
      // Set up 200 responses for all images, and the corresponding size.
      delegate_.fake_image_downloader().Add(icon_url, IntVector{icon_size});
      // Create test candidates of type FAVICON and a fake URL.
      candidate_icons.emplace_back(icon_url, FAVICON, kEmptySizes);

      ON_CALL(delegate_, OnFaviconUpdated(_, _, icon_url, _, _))
          .WillByDefault(Assign(&chosen_icon_size, icon_size));
    }

    RunHandlerWithCandidates(FaviconDriverObserver::NON_TOUCH_16_DIP,
                             candidate_icons);
    return chosen_icon_size;
  }
};

// Tests that running FaviconHandler
// - On an OS which supports the 1x and 2x scale factor
// - On a page with <link rel="icon"> tags with no "sizes" information.
// Selects the largest exact match. Note that a 32x32 PNG image is not a "true
// exact match" on an OS which supports an 1x and 2x. A "true exact match" is
// a .ico file with 16x16 and 32x32 bitmaps.
TEST_F(FaviconHandlerMultipleFaviconsTest, ChooseLargestExactMatch) {
  EXPECT_EQ(32,
            DownloadTillDoneIgnoringHistory(IntVector{16, 24, 32, 48, 256}));
}

// Test that if there are several single resolution favicons to choose
// from, the exact match is preferred even if it results in upsampling.
TEST_F(FaviconHandlerMultipleFaviconsTest, ChooseExactMatchDespiteUpsampling) {
  EXPECT_EQ(16, DownloadTillDoneIgnoringHistory(IntVector{16, 24, 48, 256}));
}

// Test that favicons which need to be upsampled a little or downsampled
// a little are preferred over huge favicons.
TEST_F(FaviconHandlerMultipleFaviconsTest,
       ChooseMinorDownsamplingOverHugeIcon) {
  EXPECT_EQ(48, DownloadTillDoneIgnoringHistory(IntVector{256, 48}));
}

TEST_F(FaviconHandlerMultipleFaviconsTest, ChooseMinorUpsamplingOverHugeIcon) {
  EXPECT_EQ(17, DownloadTillDoneIgnoringHistory(IntVector{17, 256}));
}

TEST_F(FaviconHandlerTest, Report404) {
  const GURL k404IconURL("http://www.google.com/404.png");

  EXPECT_CALL(favicon_service_, UnableToDownloadFavicon(k404IconURL));

  RunHandlerWithSimpleFaviconCandidates({k404IconURL});
  EXPECT_THAT(delegate_.downloads(), ElementsAre(k404IconURL));
}

// Test that WasUnableToDownloadFavicon() is not called if a download returns
// HTTP status 503.
TEST_F(FaviconHandlerTest, NotReport503) {
  const GURL k503IconURL("http://www.google.com/503.png");

  delegate_.fake_image_downloader().AddError(k503IconURL, 503);

  EXPECT_CALL(favicon_service_, UnableToDownloadFavicon(_)).Times(0);

  RunHandlerWithSimpleFaviconCandidates({k503IconURL});
  EXPECT_THAT(delegate_.downloads(), ElementsAre(k503IconURL));
}

// Test that the best favicon is selected when:
// - The page provides several favicons.
// - Downloading one of the page's icon URLs previously returned a 404.
// - None of the favicons are cached in the Favicons database.
TEST_F(FaviconHandlerTest, MultipleFavicons404) {
  const GURL k404IconURL("http://www.google.com/404.png");

  ON_CALL(favicon_service_, WasUnableToDownloadFavicon(k404IconURL))
      .WillByDefault(Return(true));

  EXPECT_CALL(delegate_, OnFaviconUpdated(_, _, kIconURL64x64, _, _));
  RunHandlerWithSimpleFaviconCandidates({k404IconURL, kIconURL64x64});
  EXPECT_THAT(delegate_.downloads(), ElementsAre(kIconURL64x64));
}

// Test that the best favicon is selected when:
// - The page provides several favicons.
// - Downloading the last page icon URL previously returned a 404.
// - None of the favicons are cached in the Favicons database.
// - All of the icons are downloaded because none of the icons have the ideal
//   size.
// - The 404 icon is last.
TEST_F(FaviconHandlerTest, MultipleFaviconsLast404) {
  const GURL k404IconURL("http://www.google.com/404.png");

  ON_CALL(favicon_service_, WasUnableToDownloadFavicon(k404IconURL))
      .WillByDefault(Return(true));

  EXPECT_CALL(delegate_, OnFaviconUpdated(_, _, kIconURL64x64, _, _));
  RunHandlerWithSimpleFaviconCandidates({kIconURL64x64, k404IconURL});
  EXPECT_THAT(delegate_.downloads(), ElementsAre(kIconURL64x64));
}

// Test that no favicon is selected when:
// - The page provides several favicons.
// - Downloading the page's icons has previously returned a 404.
// - None of the favicons are cached in the Favicons database.
TEST_F(FaviconHandlerTest, MultipleFaviconsAll404) {
  const GURL k404IconURL1("http://www.google.com/a/404.png");
  const GURL k404IconURL2("http://www.google.com/b/404.png");

  ON_CALL(favicon_service_, WasUnableToDownloadFavicon(k404IconURL1))
      .WillByDefault(Return(true));
  ON_CALL(favicon_service_, WasUnableToDownloadFavicon(k404IconURL2))
      .WillByDefault(Return(true));

  EXPECT_CALL(delegate_, OnFaviconUpdated(_, _, _, _, _)).Times(0);
  RunHandlerWithSimpleFaviconCandidates({k404IconURL1, k404IconURL2});
  EXPECT_THAT(delegate_.downloads(), IsEmpty());
}

// Test that no favicon is selected when the page's only icon uses an invalid
// URL syntax.
TEST_F(FaviconHandlerTest, FaviconInvalidURL) {
  const GURL kInvalidFormatURL("invalid");
  ASSERT_TRUE(kInvalidFormatURL.is_empty());

  EXPECT_CALL(delegate_, OnFaviconUpdated(_, _, _, _, _)).Times(0);

  RunHandlerWithSimpleFaviconCandidates({kInvalidFormatURL});
  EXPECT_THAT(delegate_.downloads(), IsEmpty());
}

TEST_F(FaviconHandlerTest, TestSortFavicon) {
  // Names represent the bitmap sizes per icon.
  const GURL kIconURL1_17("http://www.google.com/a");
  const GURL kIconURL1024_512("http://www.google.com/b");
  const GURL kIconURL16_14("http://www.google.com/c");
  const GURL kIconURLWithoutSize1("http://www.google.com/d");
  const GURL kIconURLWithoutSize2("http://www.google.com/e");

  const std::vector<favicon::FaviconURL> kSourceIconURLs{
      FaviconURL(kIconURL1_17, FAVICON, {gfx::Size(1, 1), gfx::Size(17, 17)}),
      FaviconURL(kIconURL1024_512, FAVICON,
                 {gfx::Size(1024, 1024), gfx::Size(512, 512)}),
      FaviconURL(kIconURL16_14, FAVICON,
                 {gfx::Size(16, 16), gfx::Size(14, 14)}),
      FaviconURL(kIconURLWithoutSize1, FAVICON, kEmptySizes),
      FaviconURL(kIconURLWithoutSize2, FAVICON, kEmptySizes)};

  std::unique_ptr<FaviconHandler> handler = RunHandlerWithCandidates(
      FaviconDriverObserver::NON_TOUCH_LARGEST, kSourceIconURLs);

  EXPECT_THAT(
      handler->GetIconURLs(),
      ElementsAre(
          // The 512x512 bitmap is the best match for the desired size.
          kIconURL1024_512, kIconURL1_17, kIconURL16_14,
          // The rest of bitmaps come in order, there is no "sizes" attribute.
          kIconURLWithoutSize1, kIconURLWithoutSize2));
}

TEST_F(FaviconHandlerTest, TestDownloadLargestFavicon) {
  // Names represent the bitmap sizes per icon.
  const GURL kIconURL1024_512("http://www.google.com/a");
  const GURL kIconURL15_14("http://www.google.com/b");
  const GURL kIconURL16_512("http://www.google.com/c");
  const GURL kIconURLWithoutSize1("http://www.google.com/d");
  const GURL kIconURLWithoutSize2("http://www.google.com/e");

  RunHandlerWithCandidates(
      FaviconDriverObserver::NON_TOUCH_LARGEST,
      {FaviconURL(kIconURL1024_512, FAVICON,
                  {gfx::Size(1024, 1024), gfx::Size(512, 512)}),
       FaviconURL(kIconURL15_14, FAVICON,
                  {gfx::Size(15, 15), gfx::Size(14, 14)}),
       FaviconURL(kIconURL16_512, FAVICON,
                  {gfx::Size(16, 16), gfx::Size(512, 512)}),
       FaviconURL(kIconURLWithoutSize1, FAVICON, kEmptySizes),
       FaviconURL(kIconURLWithoutSize2, FAVICON, kEmptySizes)});

  // Icon URLs are not registered and hence 404s will be produced, which
  // allows checking whether the icons were requested according to their size.
  // The favicons should have been requested in decreasing order of their sizes.
  // Favicons without any <link sizes=""> attribute should have been downloaded
  // last.
  EXPECT_THAT(delegate_.downloads(),
              ElementsAre(kIconURL1024_512, kIconURL16_512, kIconURL15_14,
                          kIconURLWithoutSize1, kIconURLWithoutSize2));
}

TEST_F(FaviconHandlerTest, TestSelectLargestFavicon) {
  const GURL kIconURL1("http://www.google.com/b");
  const GURL kIconURL2("http://www.google.com/c");

  delegate_.fake_image_downloader().Add(kIconURL1, IntVector{15});
  delegate_.fake_image_downloader().Add(kIconURL2, IntVector{14, 16});

  // Verify NotifyFaviconAvailable().
  EXPECT_CALL(delegate_,
              OnFaviconUpdated(_, FaviconDriverObserver::NON_TOUCH_LARGEST,
                               kIconURL2, _, _));

  RunHandlerWithCandidates(
      FaviconDriverObserver::NON_TOUCH_LARGEST,
      {FaviconURL(kIconURL1, FAVICON, {gfx::Size(15, 15)}),
       FaviconURL(kIconURL2, FAVICON, {gfx::Size(14, 14), gfx::Size(16, 16)})});

  EXPECT_THAT(delegate_.downloads(), ElementsAre(kIconURL2));
}

TEST_F(FaviconHandlerTest, TestFaviconWasScaledAfterDownload) {
  const int kMaximalSize = FaviconHandler::GetMaximalIconSize(
      FaviconDriverObserver::NON_TOUCH_LARGEST);

  const GURL kIconURL1("http://www.google.com/b");
  const GURL kIconURL2("http://www.google.com/c");

  const int kOriginalSize1 = kMaximalSize + 1;
  const int kOriginalSize2 = kMaximalSize + 2;

  delegate_.fake_image_downloader().Add(kIconURL1, IntVector{kMaximalSize},
                                        IntVector{kOriginalSize1},
                                        SK_ColorBLUE);
  delegate_.fake_image_downloader().Add(kIconURL2, IntVector{kMaximalSize},
                                        IntVector{kOriginalSize2},
                                        SK_ColorBLUE);

  // Verify the best bitmap was selected (although smaller than |kIconURL2|)
  // and that it was scaled down to |kMaximalSize|.
  EXPECT_CALL(delegate_,
              OnFaviconUpdated(_, _, kIconURL1, _,
                               ImageSizeIs(kMaximalSize, kMaximalSize)));

  RunHandlerWithCandidates(
      FaviconDriverObserver::NON_TOUCH_LARGEST,
      {FaviconURL(kIconURL1, FAVICON,
                  SizeVector{gfx::Size(kOriginalSize1, kOriginalSize1)}),
       FaviconURL(kIconURL2, FAVICON,
                  SizeVector{gfx::Size(kOriginalSize2, kOriginalSize2)})});

  EXPECT_THAT(delegate_.downloads(), ElementsAre(kIconURL1));
}

// Test that if several icons are downloaded because the icons are smaller than
// expected that OnFaviconUpdated() is called with the largest downloaded
// bitmap.
TEST_F(FaviconHandlerTest, TestKeepDownloadedLargestFavicon) {
  EXPECT_CALL(delegate_,
              OnFaviconUpdated(_, _, kIconURL12x12, _, ImageSizeIs(12, 12)));

  RunHandlerWithCandidates(
      FaviconDriverObserver::NON_TOUCH_LARGEST,
      {FaviconURL(kIconURL10x10, FAVICON, SizeVector{gfx::Size(16, 16)}),
       FaviconURL(kIconURL12x12, FAVICON, SizeVector{gfx::Size(15, 15)}),
       FaviconURL(kIconURL16x16, FAVICON, kEmptySizes)});
}

TEST_F(FaviconHandlerTest, TestRecordMultipleDownloadAttempts) {
  base::HistogramTester histogram_tester;

  // Try to download the three failing icons and end up logging three attempts.
  RunHandlerWithCandidates(
      FaviconDriverObserver::NON_TOUCH_LARGEST,
      {FaviconURL(GURL("http://www.google.com/a"), FAVICON, kEmptySizes),
       FaviconURL(GURL("http://www.google.com/b"), FAVICON, kEmptySizes),
       FaviconURL(GURL("http://www.google.com/c"), FAVICON, kEmptySizes)});

  EXPECT_THAT(
      histogram_tester.GetAllSamples("Favicons.DownloadAttempts.LargeIcons"),
      ElementsAre(base::Bucket(/*sample=*/3, /*expected_count=*/1)));
  EXPECT_THAT(
      histogram_tester.GetAllSamples("Favicons.DownloadAttempts.Favicons"),
      IsEmpty());
  EXPECT_THAT(
      histogram_tester.GetAllSamples("Favicons.DownloadAttempts.TouchIcons"),
      IsEmpty());
}

TEST_F(FaviconHandlerTest, TestRecordSingleFaviconDownloadAttempt) {
  base::HistogramTester histogram_tester;

  RunHandlerWithSimpleFaviconCandidates({kIconURL16x16});

  EXPECT_THAT(
      histogram_tester.GetAllSamples("Favicons.DownloadAttempts.Favicons"),
      ElementsAre(base::Bucket(/*sample=*/1, /*expected_count=*/1)));
  EXPECT_THAT(
      histogram_tester.GetAllSamples("Favicons.DownloadAttempts.LargeIcons"),
      IsEmpty());
  EXPECT_THAT(
      histogram_tester.GetAllSamples("Favicons.DownloadAttempts.TouchIcons"),
      IsEmpty());
  EXPECT_THAT(
      histogram_tester.GetAllSamples("Favicons.DownloadOutcome"),
      ElementsAre(base::Bucket(static_cast<int>(DownloadOutcome::SUCCEEDED),
                               /*expected_count=*/1)));
}

TEST_F(FaviconHandlerTest, TestRecordSingleLargeIconDownloadAttempt) {
  base::HistogramTester histogram_tester;

  RunHandlerWithCandidates(FaviconDriverObserver::NON_TOUCH_LARGEST,
                           {FaviconURL(kIconURL64x64, FAVICON, kEmptySizes)});

  EXPECT_THAT(
      histogram_tester.GetAllSamples("Favicons.DownloadAttempts.Favicons"),
      IsEmpty());
  EXPECT_THAT(
      histogram_tester.GetAllSamples("Favicons.DownloadAttempts.LargeIcons"),
      ElementsAre(base::Bucket(/*sample=*/1, /*expected_count=*/1)));
  EXPECT_THAT(
      histogram_tester.GetAllSamples("Favicons.DownloadAttempts.TouchIcons"),
      IsEmpty());
  EXPECT_THAT(
      histogram_tester.GetAllSamples("Favicons.DownloadOutcome"),
      ElementsAre(base::Bucket(static_cast<int>(DownloadOutcome::SUCCEEDED),
                               /*expected_count=*/1)));
}

TEST_F(FaviconHandlerTest, TestRecordSingleTouchIconDownloadAttempt) {
  base::HistogramTester histogram_tester;
  RunHandlerWithCandidates(
      FaviconDriverObserver::TOUCH_LARGEST,
      {FaviconURL(kIconURL64x64, TOUCH_ICON, kEmptySizes)});

  EXPECT_THAT(
      histogram_tester.GetAllSamples("Favicons.DownloadAttempts.LargeIcons"),
      IsEmpty());
  EXPECT_THAT(
      histogram_tester.GetAllSamples("Favicons.DownloadAttempts.Favicons"),
      IsEmpty());
  EXPECT_THAT(
      histogram_tester.GetAllSamples("Favicons.DownloadAttempts.TouchIcons"),
      ElementsAre(base::Bucket(/*sample=*/1, /*expected_count=*/1)));
  EXPECT_THAT(
      histogram_tester.GetAllSamples("Favicons.DownloadOutcome"),
      ElementsAre(base::Bucket(static_cast<int>(DownloadOutcome::SUCCEEDED),
                               /*expected_count=*/1)));
}

TEST_F(FaviconHandlerTest, TestRecordDownloadAttemptsFinishedByCache) {
  const GURL kIconURL1024x1024("http://www.google.com/a-404-ing-icon");
  base::HistogramTester histogram_tester;
  favicon_service_.fake()->Store(
      GURL("http://so.de"), kIconURL64x64,
      CreateRawBitmapResult(kIconURL64x64, FAVICON, /*expired=*/false, 64));

  RunHandlerWithCandidates(
      FaviconDriverObserver::NON_TOUCH_LARGEST,
      {FaviconURL(kIconURL1024x1024, FAVICON, {gfx::Size(1024, 1024)}),
       FaviconURL(kIconURL12x12, FAVICON, {gfx::Size(12, 12)}),
       FaviconURL(kIconURL64x64, FAVICON, {gfx::Size(64, 64)})});

  // Should try only the first (receive 404) and get second icon from cache.
  EXPECT_THAT(delegate_.downloads(), ElementsAre(kIconURL1024x1024));

  EXPECT_THAT(
      histogram_tester.GetAllSamples("Favicons.DownloadAttempts.LargeIcons"),
      ElementsAre(base::Bucket(/*sample=*/1, /*expected_count=*/1)));
  EXPECT_THAT(
      histogram_tester.GetAllSamples("Favicons.DownloadAttempts.Favicons"),
      IsEmpty());
  EXPECT_THAT(
      histogram_tester.GetAllSamples("Favicons.DownloadAttempts.TouchIcons"),
      IsEmpty());
}

TEST_F(FaviconHandlerTest, TestRecordSingleDownloadAttemptForRefreshingIcons) {
  base::HistogramTester histogram_tester;
  favicon_service_.fake()->Store(
      GURL("http://www.google.com/ps"), kIconURL16x16,
      CreateRawBitmapResult(kIconURL16x16, FAVICON, /*expired=*/true));

  RunHandlerWithSimpleFaviconCandidates({kIconURL16x16});

  EXPECT_THAT(
      histogram_tester.GetAllSamples("Favicons.DownloadAttempts.Favicons"),
      ElementsAre(base::Bucket(/*sample=*/1, /*expected_count=*/1)));
}

TEST_F(FaviconHandlerTest, TestRecordFailingDownloadAttempt) {
  base::HistogramTester histogram_tester;
  const GURL k404IconURL("http://www.google.com/404.png");

  delegate_.fake_image_downloader().AddError(k404IconURL, 404);

  EXPECT_CALL(favicon_service_, UnableToDownloadFavicon(k404IconURL));

  RunHandlerWithSimpleFaviconCandidates({k404IconURL});

  EXPECT_THAT(
      histogram_tester.GetAllSamples("Favicons.DownloadOutcome"),
      ElementsAre(base::Bucket(static_cast<int>(DownloadOutcome::FAILED),
                               /*expected_count=*/1)));
}

TEST_F(FaviconHandlerTest, TestRecordSkippedDownloadForKnownFailingUrl) {
  base::HistogramTester histogram_tester;
  const GURL k404IconURL("http://www.google.com/404.png");

  ON_CALL(favicon_service_, WasUnableToDownloadFavicon(k404IconURL))
      .WillByDefault(Return(true));

  RunHandlerWithSimpleFaviconCandidates({k404IconURL});

  EXPECT_THAT(
      histogram_tester.GetAllSamples("Favicons.DownloadOutcome"),
      ElementsAre(base::Bucket(static_cast<int>(DownloadOutcome::SKIPPED),
                               /*expected_count=*/1)));
}

// Test that the support for Web Manifest is disabled by default, unless the
// feature is enabled.
TEST_F(FaviconHandlerTest, IgnoreWebManifestByDefault) {
  const GURL kManifestURL("http://www.google.com/manifest.json");

  RunHandlerWithSimpleFaviconCandidates({kIconURL16x16}, kManifestURL);
  EXPECT_THAT(favicon_service_.fake()->db_requests(),
              Not(Contains(kManifestURL)));
  EXPECT_THAT(delegate_.downloads(), Not(Contains(kManifestURL)));
}

class FaviconHandlerManifestsEnabledTest : public FaviconHandlerTest {
 protected:
  const GURL kManifestURL = GURL("http://www.google.com/manifest.json");

  FaviconHandlerManifestsEnabledTest() {
    override_features_.InitAndEnableFeature(kFaviconsFromWebManifest);
  }

 private:
  base::test::ScopedFeatureList override_features_;

  DISALLOW_COPY_AND_ASSIGN(FaviconHandlerManifestsEnabledTest);
};

// Test that a favicon corresponding to a web manifest is reported when:
// - There is data in the favicon database for the manifest URL.
// AND
// - FaviconService::OnFaviconDataForManifestFromFaviconService() runs before
//   FaviconHandler::OnUpdateCandidates() is called.
TEST_F(FaviconHandlerManifestsEnabledTest,
       GetFaviconFromManifestInHistoryIfCandidatesSlower) {
  favicon_service_.fake()->Store(kPageURL, kManifestURL,
                                 CreateRawBitmapResult(kManifestURL));

  EXPECT_CALL(favicon_service_, UnableToDownloadFavicon(_)).Times(0);

  EXPECT_CALL(favicon_service_,
              UpdateFaviconMappingsAndFetch(_, kManifestURL, FAVICON,
                                            /*desired_size_in_dip=*/16, _, _));
  EXPECT_CALL(delegate_,
              OnFaviconUpdated(_, FaviconDriverObserver::NON_TOUCH_16_DIP,
                               kManifestURL, _, _));

  RunHandlerWithSimpleFaviconCandidates({kIconURL12x12}, kManifestURL);
  EXPECT_THAT(favicon_service_.fake()->db_requests(),
              ElementsAre(kPageURL, kManifestURL));
  EXPECT_THAT(delegate_.downloads(), IsEmpty());
}

// Test that a favicon corresponding to a web manifest is reported when:
// - There is data in the favicon database for the manifest URL.
// AND
// - FaviconHandler::OnUpdateCandidates() is called before
//   FaviconService::OnFaviconDataForManifestFromFaviconService() runs.
TEST_F(FaviconHandlerManifestsEnabledTest,
       GetFaviconFromManifestInHistoryIfCandidatesFaster) {
  favicon_service_.fake()->Store(kPageURL, kManifestURL,
                                 CreateRawBitmapResult(kManifestURL));
  // Defer the database lookup completion to control the exact timing.
  favicon_service_.fake()->SetRunCallbackManuallyForUrl(kManifestURL);

  EXPECT_CALL(favicon_service_, UnableToDownloadFavicon(_)).Times(0);

  EXPECT_CALL(favicon_service_,
              UpdateFaviconMappingsAndFetch(_, kManifestURL, FAVICON,
                                            /*desired_size_in_dip=*/16, _, _));
  EXPECT_CALL(delegate_,
              OnFaviconUpdated(_, FaviconDriverObserver::NON_TOUCH_16_DIP,
                               kManifestURL, _, _));

  std::unique_ptr<FaviconHandler> handler =
      RunHandlerWithSimpleFaviconCandidates({kIconURL12x12}, kManifestURL);
  ASSERT_TRUE(favicon_service_.fake()->HasPendingManualCallback());

  // Complete the lookup.
  EXPECT_TRUE(favicon_service_.fake()->RunCallbackManually());
  base::RunLoop().RunUntilIdle();

  EXPECT_THAT(favicon_service_.fake()->db_requests(),
              ElementsAre(kPageURL, kManifestURL));
  EXPECT_THAT(delegate_.downloads(), IsEmpty());
}

// Test that a favicon corresponding to a web manifest is reported when there is
// data in the database for neither the page URL nor the manifest URL.
TEST_F(FaviconHandlerManifestsEnabledTest, GetFaviconFromUnknownManifest) {
  const std::vector<favicon::FaviconURL> kManifestIcons = {
      FaviconURL(kIconURL16x16, FAVICON, kEmptySizes),
  };

  delegate_.fake_manifest_downloader().Add(kManifestURL, kManifestIcons);

  EXPECT_CALL(favicon_service_, UnableToDownloadFavicon(_)).Times(0);

  EXPECT_CALL(favicon_service_, SetFavicons(_, kManifestURL, FAVICON, _));
  EXPECT_CALL(delegate_, OnFaviconUpdated(_, _, kManifestURL, _, _));

  RunHandlerWithSimpleFaviconCandidates({kIconURL12x12}, kManifestURL);
  EXPECT_THAT(favicon_service_.fake()->db_requests(),
              ElementsAre(kPageURL, kManifestURL));
  EXPECT_THAT(delegate_.downloads(), ElementsAre(kManifestURL, kIconURL16x16));
}

// Test that the manifest and icon are redownloaded if the icon cached for the
// page URL expired.
TEST_F(FaviconHandlerManifestsEnabledTest, GetFaviconFromExpiredManifest) {
  const std::vector<favicon::FaviconURL> kManifestIcons = {
      FaviconURL(kIconURL64x64, FAVICON, kEmptySizes),
  };

  favicon_service_.fake()->Store(kPageURL, kManifestURL,
                                 CreateRawBitmapResult(kManifestURL, FAVICON,
                                                       /*expired=*/true));
  delegate_.fake_manifest_downloader().Add(kManifestURL, kManifestIcons);

  EXPECT_CALL(delegate_, OnFaviconUpdated(_, _, kManifestURL, _, _)).Times(2);
  EXPECT_CALL(favicon_service_, SetFavicons(_, kManifestURL, _, _));

  RunHandlerWithSimpleFaviconCandidates({kIconURL12x12}, kManifestURL);
  EXPECT_THAT(favicon_service_.fake()->db_requests(),
              ElementsAre(kPageURL, kManifestURL));
  EXPECT_THAT(delegate_.downloads(), ElementsAre(kManifestURL, kIconURL64x64));
}

// Test that the manifest and icon are redownloaded if the icon cached for the
// manifest URL expired, which was observed during a visit to a different page
// URL.
TEST_F(FaviconHandlerManifestsEnabledTest,
       GetFaviconFromExpiredManifestLinkedFromOtherPage) {
  const GURL kSomePreviousPageURL("https://www.google.com/previous");
  const std::vector<favicon::FaviconURL> kManifestIcons = {
      FaviconURL(kIconURL64x64, FAVICON, kEmptySizes),
  };

  favicon_service_.fake()->Store(kSomePreviousPageURL, kManifestURL,
                                 CreateRawBitmapResult(kManifestURL, FAVICON,
                                                       /*expired=*/true));
  delegate_.fake_manifest_downloader().Add(kManifestURL, kManifestIcons);

  EXPECT_CALL(delegate_, OnFaviconUpdated(_, _, kManifestURL, _, _)).Times(2);
  EXPECT_CALL(favicon_service_, SetFavicons(_, kManifestURL, _, _));

  RunHandlerWithSimpleFaviconCandidates({kIconURL12x12}, kManifestURL);
  EXPECT_THAT(favicon_service_.fake()->db_requests(),
              ElementsAre(kPageURL, kManifestURL));
  EXPECT_THAT(delegate_.downloads(), ElementsAre(kManifestURL, kIconURL64x64));
}

// Test that a favicon corresponding to a web manifest is reported when:
// - There is data in the database for neither the page URL nor the manifest
//   URL.
// - There is data in the database for the icon URL listed in the manifest.
TEST_F(FaviconHandlerManifestsEnabledTest,
       GetFaviconFromUnknownManifestButKnownIcon) {
  const GURL kSomePreviousPageURL("https://www.google.com/previous");
  const std::vector<favicon::FaviconURL> kManifestIcons = {
      FaviconURL(kIconURL16x16, FAVICON, kEmptySizes),
  };

  favicon_service_.fake()->Store(kSomePreviousPageURL, kIconURL16x16,
                                 CreateRawBitmapResult(kIconURL16x16));
  delegate_.fake_manifest_downloader().Add(kManifestURL, kManifestIcons);

  EXPECT_CALL(favicon_service_, SetFavicons(_, kManifestURL, _, _));
  EXPECT_CALL(delegate_, OnFaviconUpdated(_, _, kManifestURL, _, _));

  RunHandlerWithSimpleFaviconCandidates(URLVector(), kManifestURL);
  EXPECT_THAT(favicon_service_.fake()->db_requests(),
              ElementsAre(kPageURL, kManifestURL));
  // This is because, in the current implementation, FaviconHandler only checks
  // whether there is an icon cached with the manifest URL as the "icon URL"
  // when a page has a non-empty Web Manifest.
  EXPECT_THAT(delegate_.downloads(), ElementsAre(kManifestURL, kIconURL16x16));
}

// Test a manifest that returns a 404 gets blacklisted via
// UnableToDownloadFavicon() AND that the regular favicon is selected as
// fallback.
TEST_F(FaviconHandlerManifestsEnabledTest, UnknownManifestReturning404) {
  EXPECT_CALL(favicon_service_, UnableToDownloadFavicon(kManifestURL));
  EXPECT_CALL(favicon_service_, SetFavicons(_, kIconURL12x12, _, _));

  RunHandlerWithSimpleFaviconCandidates({kIconURL12x12}, kManifestURL);
  EXPECT_THAT(favicon_service_.fake()->db_requests(),
              ElementsAre(kPageURL, kManifestURL, kIconURL12x12));
  EXPECT_THAT(delegate_.downloads(), ElementsAre(kManifestURL, kIconURL12x12));
}

// Test that a manifest that was previously blacklisted via
// UnableToDownloadFavicon() is ignored and that the regular favicon is selected
// as fallback.
TEST_F(FaviconHandlerManifestsEnabledTest, IgnoreManifestWithPrior404) {
  ON_CALL(favicon_service_, WasUnableToDownloadFavicon(kManifestURL))
      .WillByDefault(Return(true));

  EXPECT_CALL(favicon_service_, SetFavicons(_, kIconURL12x12, _, _));

  RunHandlerWithSimpleFaviconCandidates({kIconURL12x12}, kManifestURL);
  EXPECT_THAT(favicon_service_.fake()->db_requests(),
              ElementsAre(kPageURL, kIconURL12x12));
  EXPECT_THAT(delegate_.downloads(), ElementsAre(kIconURL12x12));
}

// Test that the regular favicon is selected when:
// - The page links to a Web Manifest.
// - The Web Manifest does not contain any icon URLs (it is not a 404).
// - The page has an icon URL provided via a <link rel="icon"> tag.
// - The database does not know about the page URL, manifest URL or icon URL.
TEST_F(FaviconHandlerManifestsEnabledTest, UnknownManifestWithoutIcons) {
  delegate_.fake_manifest_downloader().Add(kManifestURL,
                                           std::vector<favicon::FaviconURL>());

  // UnableToDownloadFavicon() is expected to prevent repeated downloads of the
  // same manifest (which is not otherwise cached, since it doesn't contain
  // icons).
  EXPECT_CALL(favicon_service_, UnableToDownloadFavicon(kManifestURL));
  EXPECT_CALL(favicon_service_, SetFavicons(_, kIconURL12x12, _, _));

  RunHandlerWithSimpleFaviconCandidates({kIconURL12x12}, kManifestURL);
  EXPECT_THAT(favicon_service_.fake()->db_requests(),
              ElementsAre(kPageURL, kManifestURL, kIconURL12x12));
  EXPECT_THAT(delegate_.downloads(), ElementsAre(kManifestURL, kIconURL12x12));
}

// Test that the regular favicon is selected when:
// - The page links to a Web Manifest.
// - The Web Manifest does not contain any icon URLs (it is not a 404).
// - The page has an icon URL provided via a <link rel="icon"> tag.
// - The database does not know about the page URL.
// - The database does not know about the manifest URL.
// - The database knows about the icon URL.
TEST_F(FaviconHandlerManifestsEnabledTest,
       UnknownManifestWithoutIconsAndKnownRegularIcons) {
  const GURL kSomePreviousPageURL("https://www.google.com/previous");

  delegate_.fake_manifest_downloader().Add(kManifestURL,
                                           std::vector<favicon::FaviconURL>());
  favicon_service_.fake()->Store(kSomePreviousPageURL, kIconURL12x12,
                                 CreateRawBitmapResult(kIconURL12x12));

  EXPECT_CALL(favicon_service_, SetFavicons(_, _, _, _)).Times(0);

  // UnableToDownloadFavicon() is expected to prevent repeated downloads of the
  // same manifest (which is not otherwise cached, since it doesn't contain
  // icons).
  EXPECT_CALL(favicon_service_, UnableToDownloadFavicon(kManifestURL));
  EXPECT_CALL(favicon_service_,
              UpdateFaviconMappingsAndFetch(_, kManifestURL, _, _, _, _));
  EXPECT_CALL(favicon_service_,
              UpdateFaviconMappingsAndFetch(_, kIconURL12x12, _, _, _, _));
  EXPECT_CALL(delegate_, OnFaviconUpdated(_, _, kIconURL12x12, _, _));

  RunHandlerWithSimpleFaviconCandidates({kIconURL12x12}, kManifestURL);
  EXPECT_THAT(favicon_service_.fake()->db_requests(),
              ElementsAre(kPageURL, kManifestURL, kIconURL12x12));
  EXPECT_THAT(delegate_.downloads(), ElementsAre(kManifestURL));
}

// Test that the database remains unmodified when:
// - The page links to a Web Manifest.
// - The Web Manifest does not contain any icon URLs (it is not a 404).
// - The page has an icon URL provided via a <link rel="icon"> tag.
// - The database has a mapping between the page URL to the favicon URL.
TEST_F(FaviconHandlerManifestsEnabledTest,
       UnknownManifestWithoutIconsAndRegularIconInHistory) {
  delegate_.fake_manifest_downloader().Add(kManifestURL,
                                           std::vector<favicon::FaviconURL>());
  favicon_service_.fake()->Store(kPageURL, kIconURL16x16,
                                 CreateRawBitmapResult(kIconURL16x16));

  EXPECT_CALL(delegate_, OnFaviconUpdated(_, _, kIconURL16x16, _, _));
  EXPECT_CALL(favicon_service_,
              UpdateFaviconMappingsAndFetch(_, kManifestURL, _, _, _, _));

  RunHandlerWithSimpleFaviconCandidates({kIconURL16x16}, kManifestURL);

  ASSERT_THAT(favicon_service_.fake()->db_requests(),
              ElementsAre(kPageURL, kManifestURL));
  EXPECT_THAT(delegate_.downloads(), ElementsAre(kManifestURL));
}

// Test that Delegate::OnFaviconUpdated() is called if a page uses Javascript to
// modify the page's <link rel="manifest"> tag to point to a different manifest.
TEST_F(FaviconHandlerManifestsEnabledTest, ManifestUpdateViaJavascript) {
  const GURL kManifestURL1("http://www.google.com/manifest1.json");
  const GURL kManifestURL2("http://www.google.com/manifest2.json");
  const std::vector<favicon::FaviconURL> kManifestIcons1 = {
      FaviconURL(kIconURL64x64, FAVICON, kEmptySizes),
  };
  const std::vector<favicon::FaviconURL> kManifestIcons2 = {
      FaviconURL(kIconURL10x10, FAVICON, kEmptySizes),
  };

  delegate_.fake_manifest_downloader().Add(kManifestURL1, kManifestIcons1);
  delegate_.fake_manifest_downloader().Add(kManifestURL2, kManifestIcons2);

  EXPECT_CALL(delegate_, OnFaviconUpdated(_, _, kManifestURL1, _, _));

  std::unique_ptr<FaviconHandler> handler =
      RunHandlerWithSimpleFaviconCandidates({kIconURL12x12}, kManifestURL1);
  ASSERT_THAT(favicon_service_.fake()->db_requests(),
              ElementsAre(kPageURL, kManifestURL1));
  EXPECT_THAT(delegate_.downloads(), ElementsAre(kManifestURL1, kIconURL64x64));
  ASSERT_TRUE(VerifyAndClearExpectations());

  // Simulate the page changing it's manifest URL via Javascript.
  EXPECT_CALL(delegate_, OnFaviconUpdated(_, _, kManifestURL2, _, _));
  handler->OnUpdateCandidates(kPageURL,
                              {FaviconURL(kIconURL12x12, FAVICON, kEmptySizes)},
                              kManifestURL2);
  base::RunLoop().RunUntilIdle();
  ASSERT_THAT(favicon_service_.fake()->db_requests(),
              ElementsAre(kManifestURL2));
  EXPECT_THAT(delegate_.downloads(), ElementsAre(kManifestURL2, kIconURL10x10));
}

// Test that Delegate::OnFaviconUpdated() is called if a page uses Javascript to
// remove the page's <link rel="manifest"> tag (i.e. no web manifest) WHILE a
// lookup to the history database is ongoing for the manifest URL.
TEST_F(FaviconHandlerManifestsEnabledTest,
       RemoveManifestViaJavascriptWhileDatabaseLookup) {
  const std::vector<favicon::FaviconURL> kManifestIcons = {
      FaviconURL(kIconURL64x64, FAVICON, kEmptySizes),
  };

  delegate_.fake_manifest_downloader().Add(kManifestURL, kManifestIcons);
  // Defer the database lookup completion to control the exact timing.
  favicon_service_.fake()->SetRunCallbackManuallyForUrl(kManifestURL);

  std::unique_ptr<FaviconHandler> handler =
      RunHandlerWithSimpleFaviconCandidates({kIconURL12x12}, kManifestURL);
  ASSERT_THAT(favicon_service_.fake()->db_requests(),
              ElementsAre(kPageURL, kManifestURL));
  // Database lookup for |kManifestURL| is ongoing.
  ASSERT_TRUE(favicon_service_.fake()->HasPendingManualCallback());

  // Simulate the page changing it's manifest URL to empty via Javascript.
  EXPECT_CALL(delegate_, OnFaviconUpdated(_, _, kIconURL12x12, _, _));
  handler->OnUpdateCandidates(
      kPageURL, {FaviconURL(kIconURL12x12, FAVICON, kEmptySizes)}, GURL());
  // Complete the lookup.
  EXPECT_TRUE(favicon_service_.fake()->RunCallbackManually());
  base::RunLoop().RunUntilIdle();
  ASSERT_THAT(favicon_service_.fake()->db_requests(),
              ElementsAre(kPageURL, kManifestURL, kIconURL12x12));
  EXPECT_THAT(delegate_.downloads(), ElementsAre(kIconURL12x12));
}

// Test that Delegate::OnFaviconUpdated() is called a page without manifest uses
// Javascript to add a <link rel="manifest"> tag (i.e. a new web manifest) WHILE
// a lookup to the history database is ongoing for the icon URL.
TEST_F(FaviconHandlerManifestsEnabledTest,
       AddManifestViaJavascriptWhileDatabaseLookup) {
  const std::vector<favicon::FaviconURL> kManifestIcons = {
      FaviconURL(kIconURL64x64, FAVICON, kEmptySizes),
  };

  delegate_.fake_manifest_downloader().Add(kManifestURL, kManifestIcons);
  // Defer the database lookup completion to control the exact timing.
  favicon_service_.fake()->SetRunCallbackManuallyForUrl(kIconURL12x12);

  std::unique_ptr<FaviconHandler> handler =
      RunHandlerWithSimpleFaviconCandidates({kIconURL12x12});
  ASSERT_THAT(favicon_service_.fake()->db_requests(),
              ElementsAre(kPageURL, kIconURL12x12));
  // Database lookup for |kIconURL12x12| is ongoing.
  ASSERT_TRUE(favicon_service_.fake()->HasPendingManualCallback());

  // Simulate the page changing it's manifest URL to |kManifestURL| via
  // Javascript.
  EXPECT_CALL(delegate_, OnFaviconUpdated(_, _, kManifestURL, _, _));
  handler->OnUpdateCandidates(kPageURL,
                              {FaviconURL(kIconURL12x12, FAVICON, kEmptySizes)},
                              kManifestURL);
  // Complete the lookup.
  EXPECT_TRUE(favicon_service_.fake()->RunCallbackManually());
  base::RunLoop().RunUntilIdle();
  ASSERT_THAT(favicon_service_.fake()->db_requests(),
              ElementsAre(kPageURL, kIconURL12x12, kManifestURL));
  EXPECT_THAT(delegate_.downloads(), ElementsAre(kManifestURL, kIconURL64x64));
}

// Test that SetFavicons() is not called when:
// - The page doesn't initially link to a Web Manifest.
// - The page has an icon URL provided via a <link rel="icon"> tag.
// - The database does not know about the page URL or icon URL.
// - While the icon is being downloaded, the page uses Javascript to add a
//   <link rel="manifest"> tag.
// - The database has bitmap data for the manifest URL.
TEST_F(FaviconHandlerManifestsEnabledTest,
       AddKnownManifestViaJavascriptWhileImageDownload) {
  const GURL kSomePreviousPageURL("https://www.google.com/previous");

  favicon_service_.fake()->Store(kSomePreviousPageURL, kManifestURL,
                                 CreateRawBitmapResult(kManifestURL));

  // Defer the image download completion to control the exact timing.
  delegate_.fake_image_downloader().SetRunCallbackManuallyForUrl(kIconURL16x16);

  std::unique_ptr<FaviconHandler> handler =
      RunHandlerWithSimpleFaviconCandidates({kIconURL16x16});

  ASSERT_TRUE(VerifyAndClearExpectations());
  ASSERT_TRUE(delegate_.fake_image_downloader().HasPendingManualCallback());

  // Simulate the page changing it's manifest URL to |kManifestURL| via
  // Javascript. Should invalidate the ongoing image download.
  EXPECT_CALL(favicon_service_, SetFavicons(_, _, _, _)).Times(0);
  EXPECT_CALL(delegate_, OnFaviconUpdated(_, _, kManifestURL, _, _));

  handler->OnUpdateCandidates(kPageURL,
                              {FaviconURL(kIconURL16x16, FAVICON, kEmptySizes)},
                              kManifestURL);

  // Finalizes download, which should be thrown away as the manifest URL was
  // provided.
  EXPECT_TRUE(delegate_.fake_image_downloader().RunCallbackManually());
  base::RunLoop().RunUntilIdle();
}

// Test that SetFavicons() is called with the icon URL when:
// - The page doesn't initially link to a Web Manifest.
// - The page has an icon URL provided via a <link rel="icon"> tag.
// - The database does not know about the page URL or icon URL.
// - During the database lookup, the page uses Javascript to add a
//   <link rel="manifest"> tag.
// - The database does not know about the manifest URL.
// - The manifest contains no icons.
TEST_F(FaviconHandlerManifestsEnabledTest,
       AddManifestWithoutIconsViaJavascriptWhileDatabaseLookup) {
  delegate_.fake_manifest_downloader().Add(kManifestURL,
                                           std::vector<favicon::FaviconURL>());

  // Defer the database lookup completion to control the exact timing.
  favicon_service_.fake()->SetRunCallbackManuallyForUrl(kIconURL16x16);

  EXPECT_CALL(favicon_service_, SetFavicons(_, _, _, _)).Times(0);
  EXPECT_CALL(delegate_, OnFaviconUpdated(_, _, _, _, _)).Times(0);

  std::unique_ptr<FaviconHandler> handler =
      RunHandlerWithSimpleFaviconCandidates({kIconURL16x16});

  ASSERT_TRUE(VerifyAndClearExpectations());
  ASSERT_TRUE(favicon_service_.fake()->HasPendingManualCallback());

  EXPECT_CALL(favicon_service_, SetFavicons(_, kIconURL16x16, _, _));
  EXPECT_CALL(delegate_, OnFaviconUpdated(_, _, kIconURL16x16, _, _));

  handler->OnUpdateCandidates(kPageURL,
                              {FaviconURL(kIconURL16x16, FAVICON, kEmptySizes)},
                              kManifestURL);

  // Finalizes lookup, which should be thrown away as the manifest URLs was
  // provided.
  EXPECT_TRUE(favicon_service_.fake()->RunCallbackManually());
  base::RunLoop().RunUntilIdle();

  // The manifest URL interrupted the original processing of kIconURL16x16, but
  // a second one should have been started.
  EXPECT_TRUE(favicon_service_.fake()->RunCallbackManually());
  base::RunLoop().RunUntilIdle();
}

// Test that SetFavicons() is called when:
// - The page links to one Web Manifest, which contains one icon.
// - The database does not know about the page URL, icon URL or manifest URL.
// - During image download, the page updates the manifest URL to point to
//   another manifest.
// - The second manifest contains the same icons as the first.
TEST_F(FaviconHandlerManifestsEnabledTest,
       UpdateManifestWithSameIconURLsWhileDownloading) {
  const GURL kManifestURL1("http://www.google.com/manifest1.json");
  const GURL kManifestURL2("http://www.google.com/manifest2.json");
  const std::vector<favicon::FaviconURL> kManifestIcons = {
      FaviconURL(kIconURL64x64, FAVICON, kEmptySizes),
  };

  delegate_.fake_manifest_downloader().Add(kManifestURL1, kManifestIcons);
  delegate_.fake_manifest_downloader().Add(kManifestURL2, kManifestIcons);

  // Defer the download completion to control the exact timing.
  delegate_.fake_image_downloader().SetRunCallbackManuallyForUrl(kIconURL64x64);

  std::unique_ptr<FaviconHandler> handler =
      RunHandlerWithSimpleFaviconCandidates(URLVector(), kManifestURL1);

  EXPECT_THAT(delegate_.downloads(), ElementsAre(kManifestURL1, kIconURL64x64));
  ASSERT_TRUE(VerifyAndClearExpectations());
  ASSERT_TRUE(delegate_.fake_image_downloader().HasPendingManualCallback());

  // Calling OnUpdateCandidates() with a different manifest URL should trigger
  // its download.
  handler->OnUpdateCandidates(kPageURL, std::vector<favicon::FaviconURL>(),
                              kManifestURL2);
  base::RunLoop().RunUntilIdle();
  EXPECT_THAT(delegate_.downloads(), ElementsAre(kManifestURL2, kIconURL64x64));

  // Complete the download.
  EXPECT_CALL(favicon_service_, SetFavicons(_, kManifestURL2, _, _));
  EXPECT_CALL(delegate_, OnFaviconUpdated(_, _, kManifestURL2, _, _));
  EXPECT_TRUE(delegate_.fake_image_downloader().RunCallbackManually());
  base::RunLoop().RunUntilIdle();
}

}  // namespace
}  // namespace favicon
