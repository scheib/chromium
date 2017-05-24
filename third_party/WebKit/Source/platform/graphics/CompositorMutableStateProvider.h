// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CompositorMutableStateProvider_h
#define CompositorMutableStateProvider_h

#include <cstdint>
#include <memory>
#include "platform/PlatformExport.h"
#include "platform/graphics/CompositorElementId.h"

namespace cc {
class LayerTreeImpl;
}  // namespace cc

namespace blink {

class CompositorMutableState;
struct CompositorMutations;

// This class is a window onto compositor-owned state. It vends out wrappers
// around per-element bits of this state.
class PLATFORM_EXPORT CompositorMutableStateProvider {
 public:
  CompositorMutableStateProvider(cc::LayerTreeImpl*, CompositorMutations*);
  ~CompositorMutableStateProvider();

  std::unique_ptr<CompositorMutableState> GetMutableStateFor(DOMNodeId);

 private:
  cc::LayerTreeImpl* tree_;
  CompositorMutations* mutations_;
};

}  // namespace blink

#endif  // CompositorMutableStateProvider_h
