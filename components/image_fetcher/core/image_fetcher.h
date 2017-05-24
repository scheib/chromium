// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_IMAGE_FETCHER_CORE_IMAGE_FETCHER_H_
#define COMPONENTS_IMAGE_FETCHER_CORE_IMAGE_FETCHER_H_

#include <string>

#include "base/callback.h"
#include "base/macros.h"
#include "base/optional.h"
#include "components/data_use_measurement/core/data_use_user_data.h"
#include "components/image_fetcher/core/image_fetcher_delegate.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "url/gurl.h"

namespace gfx {
class Image;
class Size;
}

namespace image_fetcher {

class ImageDecoder;

struct RequestMetadata;

// A class used to fetch server images. It can be called from any thread and the
// callback will be called on the thread which initiated the fetch.
class ImageFetcher {
 public:
  ImageFetcher() {}
  virtual ~ImageFetcher() {}

  using ImageFetcherCallback =
      base::Callback<void(const std::string& id,
                          const gfx::Image& image,
                          const RequestMetadata& metadata)>;

  using DataUseServiceName = data_use_measurement::DataUseUserData::ServiceName;

  virtual void SetImageFetcherDelegate(ImageFetcherDelegate* delegate) = 0;

  // Sets a service name against which to track data usage.
  virtual void SetDataUseServiceName(
      DataUseServiceName data_use_service_name) = 0;

  // Sets an upper limit for image downloads that is by default disabled.
  // Setting |max_download_bytes| to a negative value will disable the limit.
  // Already running downloads are immediately affected.
  virtual void SetImageDownloadLimit(
      base::Optional<int64_t> max_download_bytes) = 0;

  // Sets the desired size for images with multiple frames (like .ico files).
  // By default, the image fetcher choses smaller images. Override to choose a
  // frame with a size as close as possible to |size| (trying to take one in
  // larger size if there's no precise match). Passing gfx::Size() as
  // |size| is also supported and will result in chosing the smallest available
  // size.
  virtual void SetDesiredImageFrameSize(const gfx::Size& size) = 0;

  // An empty gfx::Image will be returned to the callback in case the image
  // could not be fetched.
  virtual void StartOrQueueNetworkRequest(
      const std::string& id,
      const GURL& image_url,
      const ImageFetcherCallback& callback,
      const net::NetworkTrafficAnnotationTag& traffic_annotation) = 0;

  virtual ImageDecoder* GetImageDecoder() = 0;

 private:
  DISALLOW_COPY_AND_ASSIGN(ImageFetcher);
};

}  // namespace image_fetcher

#endif  // COMPONENTS_IMAGE_FETCHER_CORE_IMAGE_FETCHER_H_
