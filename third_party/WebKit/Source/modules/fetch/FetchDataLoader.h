// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FetchDataLoader_h
#define FetchDataLoader_h

#include "core/dom/DOMArrayBuffer.h"
#include "core/streams/Stream.h"
#include "modules/ModulesExport.h"
#include "mojo/public/cpp/system/data_pipe.h"
#include "platform/blob/BlobData.h"
#include "platform/heap/Handle.h"
#include "platform/wtf/Forward.h"

namespace blink {

class BytesConsumer;
class FormData;

// FetchDataLoader subclasses
// 1. take a BytesConsumer,
// 2. read all data, and
// 3. call either didFetchDataLoaded...() on success or
//    difFetchDataLoadFailed() otherwise
//    on the thread where FetchDataLoader is created.
//
// - Client's methods can be called synchronously in start().
// - If FetchDataLoader::cancel() is called, Client's methods will not be
//   called anymore.
class MODULES_EXPORT FetchDataLoader
    : public GarbageCollectedFinalized<FetchDataLoader> {
 public:
  class MODULES_EXPORT Client : public GarbageCollectedMixin {
   public:
    virtual ~Client() {}

    // The method corresponding to createLoaderAs... is called on success.
    virtual void DidFetchDataLoadedBlobHandle(PassRefPtr<BlobDataHandle>) {
      NOTREACHED();
    }
    virtual void DidFetchDataLoadedArrayBuffer(DOMArrayBuffer*) {
      NOTREACHED();
    }
    virtual void DidFetchDataLoadedFormData(FormData*) { NOTREACHED(); }
    virtual void DidFetchDataLoadedString(const String&) { NOTREACHED(); }
    // This is called after all data are read from |handle| and written
    // to |out_data_pipe|, and |out_data_pipe| is closed or aborted.
    virtual void DidFetchDataLoadedDataPipe() { NOTREACHED(); }

    // This function is called when a "custom" FetchDataLoader (none of the
    // ones listed above) finishes loading.
    virtual void DidFetchDataLoadedCustomFormat() { NOTREACHED(); }

    virtual void DidFetchDataLoadFailed() = 0;

    DEFINE_INLINE_VIRTUAL_TRACE() {}
  };

  static FetchDataLoader* CreateLoaderAsBlobHandle(const String& mime_type);
  static FetchDataLoader* CreateLoaderAsArrayBuffer();
  static FetchDataLoader* CreateLoaderAsFailure();
  static FetchDataLoader* CreateLoaderAsFormData(
      const String& multipart_boundary);
  static FetchDataLoader* CreateLoaderAsString();
  static FetchDataLoader* CreateLoaderAsDataPipe(
      mojo::ScopedDataPipeProducerHandle out_data_pipe);

  virtual ~FetchDataLoader() {}

  // |consumer| must not have a client when called.
  virtual void Start(BytesConsumer* /* consumer */, Client*) = 0;

  virtual void Cancel() = 0;

  DEFINE_INLINE_VIRTUAL_TRACE() {}
};

}  // namespace blink

#endif  // FetchDataLoader_h
