// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.#ifndef WebViewBase_h

#include "core/exported/WebPluginContainerBase.h"

namespace blink {

WebPluginContainerBase::WebPluginContainerBase(LocalFrame* frame)
    : ContextClient(frame) {}

DEFINE_TRACE(WebPluginContainerBase) {
  ContextClient::Trace(visitor);
  PluginView::Trace(visitor);
}

}  // namespace blink
