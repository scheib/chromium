// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/printing/service/public/cpp/pdf_compositor_service_factory.h"

#include "components/printing/service/pdf_compositor_service.h"
#include "content/public/utility/utility_thread.h"
#include "third_party/WebKit/public/platform/WebImageGenerator.h"
#include "third_party/skia/include/core/SkGraphics.h"

namespace printing {

std::unique_ptr<service_manager::Service> CreatePdfCompositorService(
    const std::string& creator) {
  content::UtilityThread::Get()->EnsureBlinkInitialized();
  // Hook up blink's codecs so skia can call them.
  SkGraphics::SetImageGeneratorFromEncodedDataFactory(
      blink::WebImageGenerator::Create);
  return printing::PdfCompositorService::Create(creator);
}

}  // namespace printing
