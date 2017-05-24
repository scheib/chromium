// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_LARGE_ICON_SOURCE_H_
#define CHROME_BROWSER_UI_WEBUI_LARGE_ICON_SOURCE_H_

#include <memory>
#include <string>

#include "base/macros.h"
#include "base/task/cancelable_task_tracker.h"
#include "content/public/browser/url_data_source.h"

namespace favicon {
class LargeIconService;
}

namespace favicon_base {
struct LargeIconResult;
}

// LargeIconSource services explicit chrome:// requests for large icons.
//
// Format:
//   chrome://large-icon/size/url
//
// Parameter:
//  'size'             Required (including trailing '/')
//    Positive integer to specify the large icon's size in pixels.
//  'url'              Optional
//    String to specify the page URL of the large icon.
//
//  Example: chrome://large-icon/48/http://www.google.com/
//    This requests a 48x48 large icon for http://www.google.com.
class LargeIconSource : public content::URLDataSource {
 public:
  // |large_icon_service| is owned by caller and may be null.
  explicit LargeIconSource(favicon::LargeIconService* large_icon_service);

  ~LargeIconSource() override;

  // content::URLDataSource implementation.
  std::string GetSource() const override;
  void StartDataRequest(
      const std::string& path,
      const content::ResourceRequestInfo::WebContentsGetter& wc_getter,
      const content::URLDataSource::GotDataCallback& callback) override;
  std::string GetMimeType(const std::string&) const override;
  bool AllowCaching() const override;
  bool ShouldReplaceExistingSource() const override;
  bool ShouldServiceRequest(const GURL& url,
                            content::ResourceContext* resource_context,
                            int render_process_id) const override;

 private:
  // Called with results of large icon retrieval request.
  void OnLargeIconDataAvailable(
      const content::URLDataSource::GotDataCallback& callback,
      const GURL& url,
      int size,
      const favicon_base::LargeIconResult& bitmap_result);

  // Returns null to trigger "Not Found" response.
  void SendNotFoundResponse(
      const content::URLDataSource::GotDataCallback& callback);

  base::CancelableTaskTracker cancelable_task_tracker_;

  // Owned by client.
  favicon::LargeIconService* large_icon_service_;

  DISALLOW_COPY_AND_ASSIGN(LargeIconSource);
};

#endif  // CHROME_BROWSER_UI_WEBUI_LARGE_ICON_SOURCE_H_
