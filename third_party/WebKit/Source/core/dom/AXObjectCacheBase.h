// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef AXObjectCacheBase_h
#define AXObjectCacheBase_h

#include "core/CoreExport.h"
#include "core/dom/AXObjectCache.h"

namespace blink {

class Node;
class AXObjectImpl;

// AXObjectCacheBase is a temporary class that sits between AXObjectCache and
// AXObjectImpl and contains methods required by web/ that we don't want to be
// available in the public API (AXObjectCache).
// TODO(dmazzoni): Once all dependencies in web/ use this class instead of
// AXObjectCacheImpl, refactor usages to use AXObjectCache instead (introducing
// new public API methods or similar) and remove this class.
class CORE_EXPORT AXObjectCacheBase : public AXObjectCache {
  WTF_MAKE_NONCOPYABLE(AXObjectCacheBase);

 public:
  virtual ~AXObjectCacheBase();

  virtual AXObjectImpl* Get(Node*) = 0;

 protected:
  AXObjectCacheBase();
};

// This is the only subclass of AXObjectCache.
DEFINE_TYPE_CASTS(AXObjectCacheBase, AXObjectCache, cache, true, true);

}  // namespace blink

#endif
