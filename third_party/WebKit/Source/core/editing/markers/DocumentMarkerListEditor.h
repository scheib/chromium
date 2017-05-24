// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DocumentMarkerListEditor_h
#define DocumentMarkerListEditor_h

#include "core/editing/markers/DocumentMarkerList.h"
#include "platform/heap/Handle.h"

namespace blink {

class DocumentMarker;

class DocumentMarkerListEditor {
 public:
  using MarkerList = HeapVector<Member<DocumentMarker>>;

  static void AddMarkerWithoutMergingOverlapping(MarkerList*, DocumentMarker*);

  // Returns true if a marker was moved, false otherwise.
  static bool MoveMarkers(MarkerList* src_list,
                          int length,
                          DocumentMarkerList* dst_list);

  // Returns true if a marker was removed, false otherwise.
  static bool RemoveMarkers(MarkerList*, unsigned start_offset, int length);

  // Returns true if a marker was shifted or removed, false otherwise.
  static bool ShiftMarkers(MarkerList*,
                           unsigned offset,
                           unsigned old_length,
                           unsigned new_length);
};

}  // namespace blink

#endif  // DocumentMarkerListEditor_h
