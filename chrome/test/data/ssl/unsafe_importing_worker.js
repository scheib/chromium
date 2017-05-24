// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
var message = "failed";
try {
  importScripts("REPLACE_WITH_IMPORTED_JS_URL");
} catch(ex) {
}
postMessage(message);
