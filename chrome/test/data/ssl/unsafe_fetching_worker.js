// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
fetch("REPLACE_WITH_TEST_FILE_URL", {mode: 'no-cors'})
  .then(_ => postMessage("loaded"), _ => postMessage("failed"));
