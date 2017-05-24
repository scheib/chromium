// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/editing/markers/TextMatchMarkerListImpl.h"

#include "core/editing/markers/DocumentMarkerListEditor.h"
#include "core/editing/markers/RenderedDocumentMarker.h"

namespace blink {

DocumentMarker::MarkerType TextMatchMarkerListImpl::MarkerType() const {
  return DocumentMarker::kTextMatch;
}

bool TextMatchMarkerListImpl::IsEmpty() const {
  return markers_.IsEmpty();
}

void TextMatchMarkerListImpl::Add(DocumentMarker* marker) {
  DocumentMarkerListEditor::AddMarkerWithoutMergingOverlapping(
      &markers_, RenderedDocumentMarker::Create(*marker));
}

void TextMatchMarkerListImpl::Clear() {
  markers_.clear();
}

const HeapVector<Member<DocumentMarker>>& TextMatchMarkerListImpl::GetMarkers()
    const {
  return markers_;
}

bool TextMatchMarkerListImpl::MoveMarkers(int length,
                                          DocumentMarkerList* dst_list) {
  return DocumentMarkerListEditor::MoveMarkers(&markers_, length, dst_list);
}

bool TextMatchMarkerListImpl::RemoveMarkers(unsigned start_offset, int length) {
  return DocumentMarkerListEditor::RemoveMarkers(&markers_, start_offset,
                                                 length);
}

bool TextMatchMarkerListImpl::ShiftMarkers(unsigned offset,
                                           unsigned old_length,
                                           unsigned new_length) {
  return DocumentMarkerListEditor::ShiftMarkers(&markers_, offset, old_length,
                                                new_length);
}

DEFINE_TRACE(TextMatchMarkerListImpl) {
  visitor->Trace(markers_);
  DocumentMarkerList::Trace(visitor);
}

}  // namespace blink
