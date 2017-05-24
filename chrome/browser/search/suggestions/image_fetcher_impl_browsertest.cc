// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/image_fetcher/core/image_fetcher_impl.h"

#include <memory>

#include "base/bind.h"
#include "base/files/file_path.h"
#include "base/macros.h"
#include "base/memory/ptr_util.h"
#include "base/run_loop.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/search/suggestions/image_decoder_impl.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "components/image_fetcher/core/image_fetcher_delegate.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/traffic_annotation/network_traffic_annotation_test_helper.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/gfx/image/image.h"
#include "url/gurl.h"

using image_fetcher::ImageFetcher;
using image_fetcher::ImageFetcherImpl;
using image_fetcher::ImageFetcherDelegate;

namespace suggestions {

namespace {

const char kTestUrl[] = "http://go.com/";
const char kTestImagePath[] = "/image_decoding/droids.png";
const char kInvalidImagePath[] = "/DOESNOTEXIST";

const base::FilePath::CharType kDocRoot[] =
    FILE_PATH_LITERAL("chrome/test/data");

class TestImageFetcherDelegate : public ImageFetcherDelegate {
 public:
  TestImageFetcherDelegate()
    : num_delegate_valid_called_(0),
      num_delegate_null_called_(0) {}
  ~TestImageFetcherDelegate() override{};

  // Perform additional tasks when an image has been fetched.
  void OnImageFetched(const std::string& id, const gfx::Image& image) override {
    if (!image.IsEmpty()) {
      num_delegate_valid_called_++;
    } else {
      num_delegate_null_called_++;
    }
  };

  int num_delegate_valid_called() { return num_delegate_valid_called_; }
  int num_delegate_null_called() { return num_delegate_null_called_; }

 private:
  int num_delegate_valid_called_;
  int num_delegate_null_called_;
};

}  // namespace

class ImageFetcherImplBrowserTest : public InProcessBrowserTest {
 protected:
  ImageFetcherImplBrowserTest()
      : num_callback_valid_called_(0), num_callback_null_called_(0) {
    test_server_.ServeFilesFromSourceDirectory(base::FilePath(kDocRoot));
  }

  void SetUpInProcessBrowserTestFixture() override {
    ASSERT_TRUE(test_server_.Start());
    InProcessBrowserTest::SetUpInProcessBrowserTestFixture();
  }

  ImageFetcherImpl* CreateImageFetcher() {
    ImageFetcherImpl* fetcher =
        new ImageFetcherImpl(base::MakeUnique<suggestions::ImageDecoderImpl>(),
                             browser()->profile()->GetRequestContext());
    fetcher->SetImageFetcherDelegate(&delegate_);
    return fetcher;
  }

  void OnImageAvailable(base::RunLoop* loop,
                        const std::string& id,
                        const gfx::Image& image,
                        const image_fetcher::RequestMetadata& metadata) {
    if (!image.IsEmpty()) {
      num_callback_valid_called_++;
    } else {
      num_callback_null_called_++;
    }
    loop->Quit();
  }

  void StartOrQueueNetworkRequestHelper(const GURL& image_url) {
    std::unique_ptr<ImageFetcherImpl> image_fetcher_(CreateImageFetcher());

    base::RunLoop run_loop;
    image_fetcher_->StartOrQueueNetworkRequest(
        kTestUrl, image_url,
        base::Bind(&ImageFetcherImplBrowserTest::OnImageAvailable,
                   base::Unretained(this), &run_loop),
        TRAFFIC_ANNOTATION_FOR_TESTS);
    run_loop.Run();
  }

  int num_callback_valid_called_;
  int num_callback_null_called_;

  net::EmbeddedTestServer test_server_;
  TestImageFetcherDelegate delegate_;

  DISALLOW_COPY_AND_ASSIGN(ImageFetcherImplBrowserTest);
};

IN_PROC_BROWSER_TEST_F(ImageFetcherImplBrowserTest, NormalFetch) {
  GURL image_url(test_server_.GetURL(kTestImagePath).spec());
  StartOrQueueNetworkRequestHelper(image_url);

  EXPECT_EQ(1, num_callback_valid_called_);
  EXPECT_EQ(0, num_callback_null_called_);
  EXPECT_EQ(1, delegate_.num_delegate_valid_called());
  EXPECT_EQ(0, delegate_.num_delegate_null_called());
}

IN_PROC_BROWSER_TEST_F(ImageFetcherImplBrowserTest, MultipleFetch) {
  GURL image_url(test_server_.GetURL(kTestImagePath).spec());

  for (int i = 0; i < 5; i++) {
    StartOrQueueNetworkRequestHelper(image_url);
  }

  EXPECT_EQ(5, num_callback_valid_called_);
  EXPECT_EQ(0, num_callback_null_called_);
  EXPECT_EQ(5, delegate_.num_delegate_valid_called());
  EXPECT_EQ(0, delegate_.num_delegate_null_called());
}

IN_PROC_BROWSER_TEST_F(ImageFetcherImplBrowserTest, InvalidFetch) {
  GURL invalid_image_url(test_server_.GetURL(kInvalidImagePath).spec());
  StartOrQueueNetworkRequestHelper(invalid_image_url);

  EXPECT_EQ(0, num_callback_valid_called_);
  EXPECT_EQ(1, num_callback_null_called_);
  EXPECT_EQ(0, delegate_.num_delegate_valid_called());
  EXPECT_EQ(1, delegate_.num_delegate_null_called());
}

}  // namespace suggestions
