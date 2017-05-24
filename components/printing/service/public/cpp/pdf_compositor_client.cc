// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/printing/service/public/cpp/pdf_compositor_client.h"

#include <utility>

#include "mojo/public/cpp/system/platform_handle.h"

namespace printing {

namespace {

// Helper callback which owns an PdfCompositorPtr until invoked. This keeps the
// PdfCompositor pipe open just long enough to dispatch a reply, at which point
// the reply is forwarded to the wrapped |callback|.
void OnCompositePdf(
    printing::mojom::PdfCompositorPtr compositor,
    printing::mojom::PdfCompositor::CompositePdfCallback callback,
    scoped_refptr<base::SequencedTaskRunner> task_runner,
    mojo::ScopedSharedBufferHandle pdf_handle) {
  task_runner->PostTask(FROM_HERE, base::BindOnce(std::move(callback),
                                                  base::Passed(&pdf_handle)));
}

}  // namespace

PdfCompositorClient::PdfCompositorClient() : compositor_(nullptr) {}

PdfCompositorClient::~PdfCompositorClient() {}

void PdfCompositorClient::Connect(service_manager::Connector* connector) {
  DCHECK(!compositor_.is_bound());
  connector->BindInterface(mojom::kServiceName, &compositor_);
}

void PdfCompositorClient::Composite(
    service_manager::Connector* connector,
    base::SharedMemoryHandle handle,
    size_t data_size,
    mojom::PdfCompositor::CompositePdfCallback callback,
    scoped_refptr<base::SequencedTaskRunner> callback_task_runner) {
  DCHECK(data_size);

  if (!compositor_)
    Connect(connector);

  mojo::ScopedSharedBufferHandle buffer_handle =
      mojo::WrapSharedMemoryHandle(handle, data_size, true);

  compositor_->CompositePdf(
      std::move(buffer_handle),
      base::BindOnce(&OnCompositePdf, base::Passed(&compositor_),
                     std::move(callback), callback_task_runner));
}

}  // namespace printing
