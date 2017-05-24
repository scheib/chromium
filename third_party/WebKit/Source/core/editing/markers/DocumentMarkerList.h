// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DocumentMarkerList_h
#define DocumentMarkerList_h

#include "core/CoreExport.h"
#include "core/editing/markers/DocumentMarker.h"
#include "platform/heap/Handle.h"

namespace blink {

class DocumentMarker;

// This is an interface implemented by classes that DocumentMarkerController
// uses to store DocumentMarkers. Different implementations can be written
// to handle different MarkerTypes (e.g. to provide more optimized handling of
// MarkerTypes with different insertion/retrieval patterns, or to provide
// different behavior for certain MarkerTypes).
class CORE_EXPORT DocumentMarkerList
    : public GarbageCollectedFinalized<DocumentMarkerList> {
 public:
  virtual ~DocumentMarkerList();

  // Returns the single marker type supported by the list implementation.
  virtual DocumentMarker::MarkerType MarkerType() const = 0;

  virtual bool IsEmpty() const = 0;

  virtual void Add(DocumentMarker*) = 0;
  virtual void Clear() = 0;

  virtual const HeapVector<Member<DocumentMarker>>& GetMarkers() const = 0;

  // Returns true if at least one marker is copied, false otherwise
  virtual bool MoveMarkers(int length, DocumentMarkerList* dst_list) = 0;

  // Returns true if at least one marker is removed, false otherwise
  virtual bool RemoveMarkers(unsigned start_offset, int length) = 0;

  // Returns true if at least one marker is shifted or removed, false otherwise
  virtual bool ShiftMarkers(unsigned offset,
                            unsigned old_length,
                            unsigned new_length) = 0;

  DEFINE_INLINE_VIRTUAL_TRACE() {}

 protected:
  DocumentMarkerList();

 private:
  DISALLOW_COPY_AND_ASSIGN(DocumentMarkerList);
};

}  // namespace blink

#endif  // DocumentMarkerList_h
