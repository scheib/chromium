// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/large_icon_source.h"

#include <vector>

#include "base/memory/ref_counted_memory.h"
#include "chrome/browser/search/instant_io_context.h"
#include "chrome/common/url_constants.h"
#include "components/favicon/core/large_icon_service.h"
#include "components/favicon_base/fallback_icon_style.h"
#include "components/favicon_base/favicon_types.h"
#include "components/favicon_base/large_icon_url_parser.h"
#include "net/url_request/url_request.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/gfx/codec/png_codec.h"

namespace {

const int kMaxLargeIconSize = 192;  // Arbitrary bound to safeguard endpoint.

}  // namespace

LargeIconSource::LargeIconSource(favicon::LargeIconService* large_icon_service)
    : large_icon_service_(large_icon_service) {}

LargeIconSource::~LargeIconSource() {
}

std::string LargeIconSource::GetSource() const {
  return chrome::kChromeUILargeIconHost;
}

void LargeIconSource::StartDataRequest(
    const std::string& path,
    const content::ResourceRequestInfo::WebContentsGetter& wc_getter,
    const content::URLDataSource::GotDataCallback& callback) {
  if (!large_icon_service_) {
    SendNotFoundResponse(callback);
    return;
  }

  LargeIconUrlParser parser;
  bool success = parser.Parse(path);
  if (!success ||
      parser.size_in_pixels() <= 0 ||
      parser.size_in_pixels() > kMaxLargeIconSize) {
    SendNotFoundResponse(callback);
    return;
  }

  GURL url(parser.url_string());
  if (!url.is_valid()) {
    SendNotFoundResponse(callback);
    return;
  }

  // TODO(beaudoin): Potentially allow icon to be scaled up.
  large_icon_service_->GetLargeIconOrFallbackStyle(
      url,
      parser.size_in_pixels(),  // Reducing this will enable scale up.
      parser.size_in_pixels(),
      base::Bind(&LargeIconSource::OnLargeIconDataAvailable,
                 base::Unretained(this), callback, url,
                 parser.size_in_pixels()),
      &cancelable_task_tracker_);
}

std::string LargeIconSource::GetMimeType(const std::string&) const {
  // We need to explicitly return a mime type, otherwise if the user tries to
  // drag the image they get no extension.
  return "image/png";
}

bool LargeIconSource::AllowCaching() const {
  return false;
}

bool LargeIconSource::ShouldReplaceExistingSource() const {
  // Leave the existing DataSource in place, otherwise we'll drop any pending
  // requests on the floor.
  return false;
}

bool LargeIconSource::ShouldServiceRequest(
    const GURL& url,
    content::ResourceContext* resource_context,
    int render_process_id) const {
  if (url.SchemeIs(chrome::kChromeSearchScheme)) {
    return InstantIOContext::ShouldServiceRequest(url, resource_context,
                                                  render_process_id);
  }
  return URLDataSource::ShouldServiceRequest(url, resource_context,
                                             render_process_id);
}

void LargeIconSource::OnLargeIconDataAvailable(
    const content::URLDataSource::GotDataCallback& callback,
    const GURL& url,
    int size,
    const favicon_base::LargeIconResult& result) {
  if (result.bitmap.is_valid()) {
    callback.Run(result.bitmap.bitmap_data.get());
    return;
  }

  if (!result.fallback_icon_style) {
    SendNotFoundResponse(callback);
    return;
  }

  // RenderFallbackIconBitmap() cannot draw fallback icons on Android. See
  // crbug.com/580922 for details. Return a 1x1 bitmap so that JavaScript can
  // detect that it needs to generate a fallback icon.
  SkBitmap bitmap;
  bitmap.allocN32Pixels(1, 1);
  bitmap.eraseColor(result.fallback_icon_style->background_color);
  std::vector<unsigned char> bitmap_data;
  if (!gfx::PNGCodec::EncodeBGRASkBitmap(bitmap, false, &bitmap_data))
    bitmap_data.clear();

  callback.Run(base::RefCountedBytes::TakeVector(&bitmap_data));
}

void LargeIconSource::SendNotFoundResponse(
    const content::URLDataSource::GotDataCallback& callback) {
  callback.Run(nullptr);
}
