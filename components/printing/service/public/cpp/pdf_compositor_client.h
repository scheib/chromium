// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_PRINTING_SERVICE_PUBLIC_CPP_PDF_COMPOSITOR_CLIENT_H_
#define COMPONENTS_PRINTING_SERVICE_PUBLIC_CPP_PDF_COMPOSITOR_CLIENT_H_

#include "base/memory/shared_memory_handle.h"
#include "components/printing/service/public/interfaces/pdf_compositor.mojom.h"
#include "services/service_manager/public/cpp/connector.h"

namespace printing {

// Helper class to composite a pdf via the pdf_compositor service.
class PdfCompositorClient {
 public:
  PdfCompositorClient();
  ~PdfCompositorClient();

  // Composite the final picture and convert into a PDF file.
  void Composite(service_manager::Connector* connector,
                 base::SharedMemoryHandle handle,
                 size_t data_size,
                 mojom::PdfCompositor::CompositePdfCallback callback,
                 scoped_refptr<base::SequencedTaskRunner> callback_task_runner);

 private:
  // Connect to the service.
  void Connect(service_manager::Connector* connector);

  mojom::PdfCompositorPtr compositor_;

  DISALLOW_COPY_AND_ASSIGN(PdfCompositorClient);
};

}  // namespace printing

#endif  // COMPONENTS_PRINTING_SERVICE_PUBLIC_CPP_PDF_COMPOSITOR_CLIENT_H_
