// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/editing/markers/SpellCheckMarkerListImpl.h"

#include "core/editing/markers/DocumentMarkerListEditor.h"
#include "core/editing/markers/RenderedDocumentMarker.h"

namespace blink {

bool SpellCheckMarkerListImpl::IsEmpty() const {
  return markers_.IsEmpty();
}

void SpellCheckMarkerListImpl::Add(DocumentMarker* marker) {
  RenderedDocumentMarker* rendered_marker =
      RenderedDocumentMarker::Create(*marker);
  if (markers_.IsEmpty() ||
      markers_.back()->EndOffset() < marker->StartOffset()) {
    markers_.push_back(rendered_marker);
    return;
  }

  auto first_overlapping = std::lower_bound(
      markers_.begin(), markers_.end(), rendered_marker,
      [](const Member<DocumentMarker>& marker_in_list,
         const DocumentMarker* marker_to_insert) {
        return marker_in_list->EndOffset() < marker_to_insert->StartOffset();
      });

  size_t index = first_overlapping - markers_.begin();
  markers_.insert(index, rendered_marker);
  const auto inserted = markers_.begin() + index;
  first_overlapping = inserted + 1;
  // TODO(rlanday): optimize this loop so it runs in O(N) time and not O(N^2)
  for (const auto i = first_overlapping;
       i != markers_.end() &&
       (*i)->StartOffset() <= (*inserted)->EndOffset();) {
    (*inserted)->SetStartOffset(
        std::min((*inserted)->StartOffset(), (*i)->StartOffset()));
    (*inserted)->SetEndOffset(
        std::max((*inserted)->EndOffset(), (*i)->EndOffset()));
    markers_.erase(i - markers_.begin());
  }
}

void SpellCheckMarkerListImpl::Clear() {
  markers_.clear();
}

const HeapVector<Member<DocumentMarker>>& SpellCheckMarkerListImpl::GetMarkers()
    const {
  return markers_;
}

bool SpellCheckMarkerListImpl::MoveMarkers(int length,
                                           DocumentMarkerList* dst_list) {
  return DocumentMarkerListEditor::MoveMarkers(&markers_, length, dst_list);
}

bool SpellCheckMarkerListImpl::RemoveMarkers(unsigned start_offset,
                                             int length) {
  return DocumentMarkerListEditor::RemoveMarkers(&markers_, start_offset,
                                                 length);
}

bool SpellCheckMarkerListImpl::ShiftMarkers(unsigned offset,
                                            unsigned old_length,
                                            unsigned new_length) {
  return DocumentMarkerListEditor::ShiftMarkers(&markers_, offset, old_length,
                                                new_length);
}

DEFINE_TRACE(SpellCheckMarkerListImpl) {
  visitor->Trace(markers_);
  DocumentMarkerList::Trace(visitor);
}

bool SpellCheckMarkerListImpl::RemoveMarkersUnderWords(
    const String& node_text,
    const Vector<String>& words) {
  bool removed_markers = false;
  for (size_t j = markers_.size(); j > 0; --j) {
    const DocumentMarker& marker = *markers_[j - 1];
    const unsigned start = marker.StartOffset();
    const unsigned length = marker.EndOffset() - marker.StartOffset();
    const String& marker_text = node_text.Substring(start, length);
    if (words.Contains(marker_text)) {
      markers_.erase(j - 1);
      removed_markers = true;
    }
  }
  return removed_markers;
}

}  // namespace blink
