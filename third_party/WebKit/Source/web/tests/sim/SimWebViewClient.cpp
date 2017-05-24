// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "web/tests/sim/SimWebViewClient.h"

#include "public/platform/WebLayerTreeView.h"
#include "public/web/WebLocalFrame.h"

namespace blink {

SimWebViewClient::SimWebViewClient(WebLayerTreeView& layer_tree_view)
    : visually_non_empty_layout_count_(0),
      finished_parsing_layout_count_(0),
      finished_loading_layout_count_(0),
      layer_tree_view_(&layer_tree_view) {}

void SimWebViewClient::DidMeaningfulLayout(
    WebMeaningfulLayout meaningful_layout) {
  switch (meaningful_layout) {
    case WebMeaningfulLayout::kVisuallyNonEmpty:
      visually_non_empty_layout_count_++;
      break;
    case WebMeaningfulLayout::kFinishedParsing:
      finished_parsing_layout_count_++;
      break;
    case WebMeaningfulLayout::kFinishedLoading:
      finished_loading_layout_count_++;
      break;
  }
}

WebView* SimWebViewClient::CreateView(WebLocalFrame* opener,
                                      const WebURLRequest&,
                                      const WebWindowFeatures&,
                                      const WebString& name,
                                      WebNavigationPolicy,
                                      bool) {
  return web_view_helper_.InitializeWithOpener(opener, true);
}

}  // namespace blink
