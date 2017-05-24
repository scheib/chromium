// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SpellCheckMarkerListImpl_h
#define SpellCheckMarkerListImpl_h

#include "core/editing/markers/DocumentMarkerList.h"

namespace blink {

// Nearly-complete implementation of DocumentMarkerList for Spelling or Grammar
// markers (subclassed by SpellingMarkerListImpl and GrammarMarkerListImpl to
// implement the MarkerType() method). Markers with touching endpoints are
// merged on insert. Markers are kept sorted by start offset in order to be able
// to do this efficiently.
class CORE_EXPORT SpellCheckMarkerListImpl : public DocumentMarkerList {
 public:
  // DocumentMarkerList implementations
  bool IsEmpty() const final;

  void Add(DocumentMarker*) final;
  void Clear() final;

  const HeapVector<Member<DocumentMarker>>& GetMarkers() const final;

  bool MoveMarkers(int length, DocumentMarkerList* dst_list) final;
  bool RemoveMarkers(unsigned start_offset, int length) final;
  bool ShiftMarkers(unsigned offset,
                    unsigned old_length,
                    unsigned new_length) final;

  DECLARE_VIRTUAL_TRACE();

  // SpellCheckMarkerListImpl-specific
  // Returns true if a marker was removed, false otherwise.
  bool RemoveMarkersUnderWords(const String& node_text,
                               const Vector<String>& words);

 protected:
  SpellCheckMarkerListImpl() = default;

 private:
  HeapVector<Member<DocumentMarker>> markers_;

  DISALLOW_COPY_AND_ASSIGN(SpellCheckMarkerListImpl);
};

DEFINE_TYPE_CASTS(SpellCheckMarkerListImpl,
                  DocumentMarkerList,
                  list,
                  list->MarkerType() == DocumentMarker::kSpelling ||
                      list->MarkerType() == DocumentMarker::kGrammar,
                  list.MarkerType() == DocumentMarker::kSpelling ||
                      list.MarkerType() == DocumentMarker::kGrammar);

}  // namespace blink

#endif  // SpellCheckMarkerListImpl_h
