// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PRINTING_COMMON_PDF_METAFILE_UTILS_H_
#define PRINTING_COMMON_PDF_METAFILE_UTILS_H_

#include <string>

#include "skia/ext/platform_canvas.h"
#include "third_party/skia/include/core/SkDocument.h"
#include "third_party/skia/include/core/SkRefCnt.h"
#include "third_party/skia/include/core/SkStream.h"

namespace printing {

enum SkiaDocumentType {
  PDF_SKIA_DOCUMENT_TYPE,
  // MSKP is an experimental, fragile, and diagnostic-only document type.
  MSKP_SKIA_DOCUMENT_TYPE,
};

sk_sp<SkDocument> MakePdfDocument(const std::string& creator,
                                  SkWStream* stream);

}  // namespace printing

#endif  // PRINTING_COMMON_PDF_METAFILE_UTILS_H_
