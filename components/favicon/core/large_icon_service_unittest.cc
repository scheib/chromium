// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/favicon/core/large_icon_service.h"

#include <deque>
#include <memory>

#include "base/bind.h"
#include "base/macros.h"
#include "base/memory/ptr_util.h"
#include "base/memory/ref_counted_memory.h"
#include "base/message_loop/message_loop.h"
#include "base/run_loop.h"
#include "base/task/cancelable_task_tracker.h"
#include "base/test/histogram_tester.h"
#include "base/test/mock_callback.h"
#include "base/threading/thread_task_runner_handle.h"
#include "components/favicon/core/favicon_client.h"
#include "components/favicon/core/test/mock_favicon_service.h"
#include "components/favicon_base/fallback_icon_style.h"
#include "components/favicon_base/favicon_types.h"
#include "components/image_fetcher/core/image_fetcher.h"
#include "components/image_fetcher/core/request_metadata.h"
#include "components/variations/variations_params_manager.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/gfx/codec/png_codec.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/image/image.h"
#include "ui/gfx/image/image_skia.h"
#include "url/gurl.h"

namespace favicon {
namespace {

using testing::IsEmpty;
using testing::IsNull;
using testing::Eq;
using testing::NiceMock;
using testing::Return;
using testing::SaveArg;
using testing::_;

const char kDummyUrl[] = "http://www.example.com";
const char kDummyIconUrl[] = "http://www.example.com/touch_icon.png";
const SkColor kTestColor = SK_ColorRED;

ACTION_P(PostFetchReply, p0) {
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE, base::Bind(arg2, arg0, p0, image_fetcher::RequestMetadata()));
}

ACTION_P2(PostFetchReplyWithMetadata, p0, p1) {
  base::ThreadTaskRunnerHandle::Get()->PostTask(FROM_HERE,
                                                base::Bind(arg2, arg0, p0, p1));
}

ACTION_P(PostBoolReply, p0) {
  base::ThreadTaskRunnerHandle::Get()->PostTask(FROM_HERE,
                                                base::Bind(arg4, p0));
}

SkBitmap CreateTestSkBitmap(int w, int h, SkColor color) {
  SkBitmap bitmap;
  bitmap.allocN32Pixels(w, h);
  bitmap.eraseColor(color);
  return bitmap;
}

favicon_base::FaviconRawBitmapResult CreateTestBitmapResult(int w,
                                                            int h,
                                                            SkColor color) {
  favicon_base::FaviconRawBitmapResult result;
  result.expired = false;

  // Create bitmap and fill with |color|.
  scoped_refptr<base::RefCountedBytes> data(new base::RefCountedBytes());
  SkBitmap bitmap;
  bitmap.allocN32Pixels(w, h);
  bitmap.eraseColor(color);
  gfx::PNGCodec::EncodeBGRASkBitmap(bitmap, false, &data->data());
  result.bitmap_data = data;

  result.pixel_size = gfx::Size(w, h);
  result.icon_url = GURL(kDummyIconUrl);
  result.icon_type = favicon_base::TOUCH_ICON;
  CHECK(result.is_valid());
  return result;
}

bool HasBackgroundColor(
    const favicon_base::FallbackIconStyle& fallback_icon_style,
    SkColor color) {
  return !fallback_icon_style.is_default_background_color &&
         fallback_icon_style.background_color == color;
}

class MockImageFetcher : public image_fetcher::ImageFetcher {
 public:
  MOCK_METHOD1(SetImageFetcherDelegate,
               void(image_fetcher::ImageFetcherDelegate* delegate));
  MOCK_METHOD1(SetDataUseServiceName,
               void(image_fetcher::ImageFetcher::DataUseServiceName name));
  MOCK_METHOD1(SetImageDownloadLimit,
               void(base::Optional<int64_t> max_download_bytes));
  MOCK_METHOD1(SetDesiredImageFrameSize, void(const gfx::Size& size));
  MOCK_METHOD4(StartOrQueueNetworkRequest,
               void(const std::string&,
                    const GURL&,
                    const ImageFetcherCallback&,
                    const net::NetworkTrafficAnnotationTag&));
  MOCK_METHOD0(GetImageDecoder, image_fetcher::ImageDecoder*());
};

class LargeIconServiceTest : public testing::Test {
 public:
  LargeIconServiceTest()
      : mock_image_fetcher_(new NiceMock<MockImageFetcher>()),
        large_icon_service_(&mock_favicon_service_,
                            base::ThreadTaskRunnerHandle::Get(),
                            base::WrapUnique(mock_image_fetcher_)) {}

  ~LargeIconServiceTest() override {}

 protected:
  base::MessageLoopForIO loop_;

  NiceMock<MockImageFetcher>* mock_image_fetcher_;
  testing::NiceMock<MockFaviconService> mock_favicon_service_;
  LargeIconService large_icon_service_;
  base::HistogramTester histogram_tester_;

 private:
  DISALLOW_COPY_AND_ASSIGN(LargeIconServiceTest);
};

TEST_F(LargeIconServiceTest, ShouldGetFromGoogleServer) {
  const GURL kExpectedServerUrl(
      "https://t0.gstatic.com/faviconV2?client=chrome&drop_404_icon=true"
      "&check_seen=true&size=61&min_size=42&max_size=122"
      "&fallback_opts=TYPE,SIZE,URL&url=http://www.example.com/");

  EXPECT_CALL(mock_favicon_service_, UnableToDownloadFavicon(_)).Times(0);

  base::MockCallback<base::Callback<void(bool success)>> callback;
  EXPECT_CALL(*mock_image_fetcher_,
              StartOrQueueNetworkRequest(_, kExpectedServerUrl, _, _))
      .WillOnce(PostFetchReply(gfx::Image::CreateFrom1xBitmap(
          CreateTestSkBitmap(64, 64, kTestColor))));
  EXPECT_CALL(mock_favicon_service_,
              SetLastResortFavicons(GURL(kDummyUrl), kExpectedServerUrl,
                                    favicon_base::IconType::TOUCH_ICON, _, _))
      .WillOnce(PostBoolReply(true));

  large_icon_service_
      .GetLargeIconOrFallbackStyleFromGoogleServerSkippingLocalCache(
          GURL(kDummyUrl), /*min_source_size_in_pixel=*/42,
          /*desired_size_in_pixel=*/61, callback.Get());

  EXPECT_CALL(callback, Run(true));
  base::RunLoop().RunUntilIdle();
  histogram_tester_.ExpectUniqueSample(
      "Favicons.LargeIconService.DownloadedSize", 64, /*expected_count=*/1);
}

TEST_F(LargeIconServiceTest, ShouldGetFromGoogleServerWithCustomUrl) {
  variations::testing::VariationParamsManager variation_params(
      "LargeIconServiceFetching",
      {{"request_format",
        "https://t0.gstatic.com/"
        "faviconV2?size=%d&min_size=%d&max_size=%d&url=%s"},
       {"enforced_min_size_in_pixel", "43"},
       {"desired_to_max_size_factor", "1.5"}},
      {"LargeIconServiceFetching"});
  const GURL kExpectedServerUrl(
      "https://t0.gstatic.com/"
      "faviconV2?size=61&min_size=43&max_size=91&url=http://www.example.com/");

  EXPECT_CALL(mock_favicon_service_, UnableToDownloadFavicon(_)).Times(0);

  base::MockCallback<base::Callback<void(bool success)>> callback;
  EXPECT_CALL(*mock_image_fetcher_,
              StartOrQueueNetworkRequest(_, kExpectedServerUrl, _, _))
      .WillOnce(PostFetchReply(gfx::Image::CreateFrom1xBitmap(
          CreateTestSkBitmap(64, 64, kTestColor))));
  EXPECT_CALL(mock_favicon_service_,
              SetLastResortFavicons(GURL(kDummyUrl), kExpectedServerUrl,
                                    favicon_base::IconType::TOUCH_ICON, _, _))
      .WillOnce(PostBoolReply(true));

  large_icon_service_
      .GetLargeIconOrFallbackStyleFromGoogleServerSkippingLocalCache(
          GURL(kDummyUrl), /*min_source_size_in_pixel=*/42,
          /*desired_size_in_pixel=*/61, callback.Get());

  EXPECT_CALL(callback, Run(true));
  base::RunLoop().RunUntilIdle();
}

TEST_F(LargeIconServiceTest, ShouldGetFromGoogleServerWithOriginalUrl) {
  const GURL kExpectedServerUrl(
      "https://t0.gstatic.com/faviconV2?client=chrome&drop_404_icon=true"
      "&check_seen=true&size=61&min_size=42&max_size=122"
      "&fallback_opts=TYPE,SIZE,URL&url=http://www.example.com/");
  const GURL kExpectedOriginalUrl("http://www.example.com/favicon.png");

  image_fetcher::RequestMetadata expected_metadata;
  expected_metadata.content_location_header = kExpectedOriginalUrl.spec();
  EXPECT_CALL(*mock_image_fetcher_,
              StartOrQueueNetworkRequest(_, kExpectedServerUrl, _, _))
      .WillOnce(PostFetchReplyWithMetadata(
          gfx::Image::CreateFrom1xBitmap(
              CreateTestSkBitmap(64, 64, kTestColor)),
          expected_metadata));
  EXPECT_CALL(mock_favicon_service_,
              SetLastResortFavicons(GURL(kDummyUrl), kExpectedOriginalUrl,
                                    favicon_base::IconType::TOUCH_ICON, _, _))
      .WillOnce(PostBoolReply(true));

  base::MockCallback<base::Callback<void(bool success)>> callback;
  large_icon_service_
      .GetLargeIconOrFallbackStyleFromGoogleServerSkippingLocalCache(
          GURL(kDummyUrl), /*min_source_size_in_pixel=*/42,
          /*desired_size_in_pixel=*/61, callback.Get());

  EXPECT_CALL(callback, Run(true));
  base::RunLoop().RunUntilIdle();
}

TEST_F(LargeIconServiceTest, ShouldTrimQueryParametersForGoogleServer) {
  const GURL kDummyUrlWithQuery("http://www.example.com?foo=1");
  const GURL kExpectedServerUrl(
      "https://t0.gstatic.com/faviconV2?client=chrome&drop_404_icon=true"
      "&check_seen=true&size=61&min_size=42&max_size=122"
      "&fallback_opts=TYPE,SIZE,URL&url=http://www.example.com/");

  EXPECT_CALL(*mock_image_fetcher_,
              StartOrQueueNetworkRequest(_, kExpectedServerUrl, _, _))
      .WillOnce(PostFetchReply(gfx::Image::CreateFrom1xBitmap(
          CreateTestSkBitmap(64, 64, kTestColor))));
  // Verify that the non-trimmed page URL is used when writing to the database.
  EXPECT_CALL(mock_favicon_service_,
              SetLastResortFavicons(_, kExpectedServerUrl, _, _, _));

  large_icon_service_
      .GetLargeIconOrFallbackStyleFromGoogleServerSkippingLocalCache(
          GURL(kDummyUrlWithQuery), /*min_source_size_in_pixel=*/42,
          /*desired_size_in_pixel=*/61, base::Callback<void(bool success)>());

  base::RunLoop().RunUntilIdle();
}

TEST_F(LargeIconServiceTest, ShouldNotQueryGoogleServerIfInvalidScheme) {
  const GURL kDummyFtpUrl("ftp://www.example.com");

  EXPECT_CALL(*mock_image_fetcher_, StartOrQueueNetworkRequest(_, _, _, _))
      .Times(0);

  base::MockCallback<base::Callback<void(bool success)>> callback;

  large_icon_service_
      .GetLargeIconOrFallbackStyleFromGoogleServerSkippingLocalCache(
          GURL(kDummyFtpUrl), /*min_source_size_in_pixel=*/42,
          /*desired_size_in_pixel=*/61, callback.Get());

  EXPECT_CALL(callback, Run(false));
  base::RunLoop().RunUntilIdle();
  EXPECT_THAT(histogram_tester_.GetAllSamples(
                  "Favicons.LargeIconService.DownloadedSize"),
              IsEmpty());
}

TEST_F(LargeIconServiceTest, ShouldReportUnavailableIfFetchFromServerFails) {
  const GURL kDummyUrlWithQuery("http://www.example.com?foo=1");
  const GURL kExpectedServerUrl(
      "https://t0.gstatic.com/faviconV2?client=chrome&drop_404_icon=true"
      "&check_seen=true&size=61&min_size=42&max_size=122"
      "&fallback_opts=TYPE,SIZE,URL&url=http://www.example.com/");

  EXPECT_CALL(mock_favicon_service_, SetLastResortFavicons(_, _, _, _, _))
      .Times(0);

  base::MockCallback<base::Callback<void(bool success)>> callback;
  EXPECT_CALL(*mock_image_fetcher_,
              StartOrQueueNetworkRequest(_, kExpectedServerUrl, _, _))
      .WillOnce(PostFetchReply(gfx::Image()));
  EXPECT_CALL(mock_favicon_service_,
              UnableToDownloadFavicon(kExpectedServerUrl));

  large_icon_service_
      .GetLargeIconOrFallbackStyleFromGoogleServerSkippingLocalCache(
          kDummyUrlWithQuery, /*min_source_size_in_pixel=*/42,
          /*desired_size_in_pixel=*/61, callback.Get());

  EXPECT_CALL(callback, Run(false));
  base::RunLoop().RunUntilIdle();
  // Verify that download failure gets recorded.
  histogram_tester_.ExpectUniqueSample(
      "Favicons.LargeIconService.DownloadedSize", 0, /*expected_count=*/1);
}

TEST_F(LargeIconServiceTest, ShouldNotGetFromGoogleServerIfUnavailable) {
  ON_CALL(
      mock_favicon_service_,
      WasUnableToDownloadFavicon(GURL(
          "https://t0.gstatic.com/faviconV2?client=chrome&drop_404_icon=true"
          "&check_seen=true&size=61&min_size=42&max_size=122"
          "&fallback_opts=TYPE,SIZE,URL&url=http://www.example.com/")))
      .WillByDefault(Return(true));

  EXPECT_CALL(mock_favicon_service_, UnableToDownloadFavicon(_)).Times(0);
  EXPECT_CALL(*mock_image_fetcher_, StartOrQueueNetworkRequest(_, _, _, _))
      .Times(0);
  EXPECT_CALL(mock_favicon_service_, SetLastResortFavicons(_, _, _, _, _))
      .Times(0);

  base::MockCallback<base::Callback<void(bool success)>> callback;
  large_icon_service_
      .GetLargeIconOrFallbackStyleFromGoogleServerSkippingLocalCache(
          GURL(kDummyUrl), /*min_source_size_in_pixel=*/42,
          /*desired_size_in_pixel=*/61, callback.Get());

  EXPECT_CALL(callback, Run(false));
  base::RunLoop().RunUntilIdle();
  EXPECT_THAT(histogram_tester_.GetAllSamples(
                  "Favicons.LargeIconService.DownloadedSize"),
              IsEmpty());
}

class LargeIconServiceGetterTest : public LargeIconServiceTest,
                                   public ::testing::WithParamInterface<bool> {
 public:
  LargeIconServiceGetterTest() : LargeIconServiceTest() {}
  ~LargeIconServiceGetterTest() override {}

  void GetLargeIconOrFallbackStyleAndWaitForCallback(
      const GURL& page_url,
      int min_source_size_in_pixel,
      int desired_size_in_pixel) {
    // Switch over testing two analogous functions based on the bool param.
    if (GetParam()) {
      large_icon_service_.GetLargeIconOrFallbackStyle(
          page_url, min_source_size_in_pixel, desired_size_in_pixel,
          base::Bind(&LargeIconServiceGetterTest::RawBitmapResultCallback,
                     base::Unretained(this)),
          &cancelable_task_tracker_);
    } else {
      large_icon_service_.GetLargeIconImageOrFallbackStyle(
          page_url, min_source_size_in_pixel, desired_size_in_pixel,
          base::Bind(&LargeIconServiceGetterTest::ImageResultCallback,
                     base::Unretained(this)),
          &cancelable_task_tracker_);
    }
    base::RunLoop().RunUntilIdle();
  }

  void RawBitmapResultCallback(const favicon_base::LargeIconResult& result) {
    if (result.bitmap.is_valid()) {
      returned_bitmap_size_ =
          base::MakeUnique<gfx::Size>(result.bitmap.pixel_size);
    }
    StoreFallbackStyle(result.fallback_icon_style.get());
  }

  void ImageResultCallback(const favicon_base::LargeIconImageResult& result) {
    if (!result.image.IsEmpty()) {
      returned_bitmap_size_ =
          base::MakeUnique<gfx::Size>(result.image.ToImageSkia()->size());
    }
    StoreFallbackStyle(result.fallback_icon_style.get());
  }

  void StoreFallbackStyle(
      const favicon_base::FallbackIconStyle* fallback_style) {
    if (fallback_style) {
      returned_fallback_style_ =
          base::MakeUnique<favicon_base::FallbackIconStyle>(*fallback_style);
    }
  }

  void InjectMockResult(
      const GURL& page_url,
      const favicon_base::FaviconRawBitmapResult& mock_result) {
    ON_CALL(mock_favicon_service_,
            GetLargestRawFaviconForPageURL(page_url, _, _, _, _))
        .WillByDefault(PostReply<5>(mock_result));
  }

 protected:
  base::CancelableTaskTracker cancelable_task_tracker_;

  std::unique_ptr<favicon_base::FallbackIconStyle> returned_fallback_style_;
  std::unique_ptr<gfx::Size> returned_bitmap_size_;

 private:
  DISALLOW_COPY_AND_ASSIGN(LargeIconServiceGetterTest);
};

TEST_P(LargeIconServiceGetterTest, SameSize) {
  InjectMockResult(GURL(kDummyUrl), CreateTestBitmapResult(24, 24, kTestColor));
  GetLargeIconOrFallbackStyleAndWaitForCallback(
      GURL(kDummyUrl),
      24,   // |min_source_size_in_pixel|
      24);  // |desired_size_in_pixel|
  EXPECT_EQ(gfx::Size(24, 24), *returned_bitmap_size_);
  EXPECT_EQ(nullptr, returned_fallback_style_);
}

TEST_P(LargeIconServiceGetterTest, ScaleDown) {
  InjectMockResult(GURL(kDummyUrl), CreateTestBitmapResult(32, 32, kTestColor));
  GetLargeIconOrFallbackStyleAndWaitForCallback(GURL(kDummyUrl), 24, 24);
  EXPECT_EQ(gfx::Size(24, 24), *returned_bitmap_size_);
  EXPECT_EQ(nullptr, returned_fallback_style_);
}

TEST_P(LargeIconServiceGetterTest, ScaleUp) {
  InjectMockResult(GURL(kDummyUrl), CreateTestBitmapResult(16, 16, kTestColor));
  GetLargeIconOrFallbackStyleAndWaitForCallback(
      GURL(kDummyUrl),
      14,  // Lowered requirement so stored bitmap is admitted.
      24);
  EXPECT_EQ(gfx::Size(24, 24), *returned_bitmap_size_);
  EXPECT_EQ(nullptr, returned_fallback_style_);
}

// |desired_size_in_pixel| == 0 means retrieve original image without scaling.
TEST_P(LargeIconServiceGetterTest, NoScale) {
  InjectMockResult(GURL(kDummyUrl), CreateTestBitmapResult(24, 24, kTestColor));
  GetLargeIconOrFallbackStyleAndWaitForCallback(GURL(kDummyUrl), 16, 0);
  EXPECT_EQ(gfx::Size(24, 24), *returned_bitmap_size_);
  EXPECT_EQ(nullptr, returned_fallback_style_);
}

TEST_P(LargeIconServiceGetterTest, FallbackSinceIconTooSmall) {
  InjectMockResult(GURL(kDummyUrl), CreateTestBitmapResult(16, 16, kTestColor));
  GetLargeIconOrFallbackStyleAndWaitForCallback(GURL(kDummyUrl), 24, 24);
  EXPECT_EQ(nullptr, returned_bitmap_size_);
  EXPECT_TRUE(HasBackgroundColor(*returned_fallback_style_, kTestColor));
  histogram_tester_.ExpectUniqueSample("Favicons.LargeIconService.FallbackSize",
                                       16, /*expected_count=*/1);
}

TEST_P(LargeIconServiceGetterTest, FallbackSinceIconNotSquare) {
  InjectMockResult(GURL(kDummyUrl), CreateTestBitmapResult(24, 32, kTestColor));
  GetLargeIconOrFallbackStyleAndWaitForCallback(GURL(kDummyUrl), 24, 24);
  EXPECT_EQ(nullptr, returned_bitmap_size_);
  EXPECT_TRUE(HasBackgroundColor(*returned_fallback_style_, kTestColor));
  histogram_tester_.ExpectUniqueSample("Favicons.LargeIconService.FallbackSize",
                                       24, /*expected_count=*/1);
}

TEST_P(LargeIconServiceGetterTest, FallbackSinceIconMissing) {
  InjectMockResult(GURL(kDummyUrl), favicon_base::FaviconRawBitmapResult());
  GetLargeIconOrFallbackStyleAndWaitForCallback(GURL(kDummyUrl), 24, 24);
  EXPECT_EQ(nullptr, returned_bitmap_size_);
  EXPECT_TRUE(returned_fallback_style_->is_default_background_color);
  histogram_tester_.ExpectUniqueSample("Favicons.LargeIconService.FallbackSize",
                                       0, /*expected_count=*/1);
}

TEST_P(LargeIconServiceGetterTest, FallbackSinceIconMissingNoScale) {
  InjectMockResult(GURL(kDummyUrl), favicon_base::FaviconRawBitmapResult());
  GetLargeIconOrFallbackStyleAndWaitForCallback(GURL(kDummyUrl), 24, 0);
  EXPECT_EQ(nullptr, returned_bitmap_size_);
  EXPECT_TRUE(returned_fallback_style_->is_default_background_color);
  histogram_tester_.ExpectUniqueSample("Favicons.LargeIconService.FallbackSize",
                                       0, /*expected_count=*/1);
}

// Oddball case where we demand a high resolution icon to scale down. Generates
// fallback even though an icon with the final size is available.
TEST_P(LargeIconServiceGetterTest, FallbackSinceTooPicky) {
  InjectMockResult(GURL(kDummyUrl), CreateTestBitmapResult(24, 24, kTestColor));
  GetLargeIconOrFallbackStyleAndWaitForCallback(GURL(kDummyUrl), 32, 24);
  EXPECT_EQ(nullptr, returned_bitmap_size_);
  EXPECT_TRUE(HasBackgroundColor(*returned_fallback_style_, kTestColor));
  histogram_tester_.ExpectUniqueSample("Favicons.LargeIconService.FallbackSize",
                                       24, /*expected_count=*/1);
}

// Every test will appear with suffix /0 (param false) and /1 (param true), e.g.
//  LargeIconServiceGetterTest.FallbackSinceTooPicky/0: get image.
//  LargeIconServiceGetterTest.FallbackSinceTooPicky/1: get raw bitmap.
INSTANTIATE_TEST_CASE_P(,  // Empty instatiation name.
                        LargeIconServiceGetterTest,
                        ::testing::Values(false, true));

}  // namespace
}  // namespace favicon
