// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/favicon/core/features.h"

#include "base/feature_list.h"

namespace favicon {

const base::Feature kFaviconsFromWebManifest{"FaviconsFromWebManifest",
                                             base::FEATURE_DISABLED_BY_DEFAULT};

}  // namespace favicon
