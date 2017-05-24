// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef EXTENSIONS_BROWSER_API_MEDIA_PERCEPTION_PRIVATE_MEDIA_PERCEPTION_PRIVATE_API_H_
#define EXTENSIONS_BROWSER_API_MEDIA_PERCEPTION_PRIVATE_MEDIA_PERCEPTION_PRIVATE_API_H_

#include "extensions/browser/extension_function.h"

namespace extensions {

class MediaPerceptionPrivateGetStateFunction
    : public UIThreadExtensionFunction {
 public:
  MediaPerceptionPrivateGetStateFunction();
  DECLARE_EXTENSION_FUNCTION("mediaPerceptionPrivate.getState",
                             MEDIAPERCEPTIONPRIVATE_GETSTATE);

 private:
  ~MediaPerceptionPrivateGetStateFunction() override;

  // ExtensionFunction:
  ResponseAction Run() override;

  DISALLOW_COPY_AND_ASSIGN(MediaPerceptionPrivateGetStateFunction);
};

class MediaPerceptionPrivateSetStateFunction
    : public UIThreadExtensionFunction {
 public:
  MediaPerceptionPrivateSetStateFunction();
  DECLARE_EXTENSION_FUNCTION("mediaPerceptionPrivate.setState",
                             MEDIAPERCEPTIONPRIVATE_SETSTATE);

 private:
  ~MediaPerceptionPrivateSetStateFunction() override;

  // ExtensionFunction:
  ResponseAction Run() override;

  DISALLOW_COPY_AND_ASSIGN(MediaPerceptionPrivateSetStateFunction);
};

class MediaPerceptionPrivateGetDiagnosticsFunction
    : public UIThreadExtensionFunction {
 public:
  MediaPerceptionPrivateGetDiagnosticsFunction();
  DECLARE_EXTENSION_FUNCTION("mediaPerceptionPrivate.getDiagnostics",
                             MEDIAPERCEPTIONPRIVATE_GETDIAGNOSTICS);

 private:
  ~MediaPerceptionPrivateGetDiagnosticsFunction() override;

  // ExtensionFunction:
  ResponseAction Run() override;

  DISALLOW_COPY_AND_ASSIGN(MediaPerceptionPrivateGetDiagnosticsFunction);
};

}  // namespace extensions

#endif  // EXTENSIONS_BROWSER_API_MEDIA_PERCEPTION_PRIVATE_MEDIA_PERCEPTION_PRIVATE_API_H_
