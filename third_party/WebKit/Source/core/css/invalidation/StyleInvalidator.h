// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef StyleInvalidator_h
#define StyleInvalidator_h

#include <memory>
#include "core/css/invalidation/PendingInvalidations.h"
#include "platform/heap/Handle.h"
#include "platform/wtf/Noncopyable.h"

namespace blink {

class ContainerNode;
class Document;
class Element;
class HTMLSlotElement;
class InvalidationSet;

// Performs deferred style invalidation for DOM subtrees.
//
// Suppose we have a large DOM tree with the style rules
// .a .b { ... }
// ...
// and user script adds or removes class 'a' from an element.
//
// The cached computed styles for any of the element's
// descendants that have class b are now outdated.
//
// The user script might subsequently make many more DOM
// changes, so we don't immediately traverse the element's
// descendants for class b.
//
// Instead, we record the need for this traversal by
// calling ScheduleInvalidationSetsForNode with
// InvalidationLists obtained from RuleFeatureSet.
//
// When we next read computed styles, for example from
// user script or to render a frame, Invalidate(Document&)
// is called to traverse the DOM and perform all
// the pending style invalidations.
//
// If an element is removed from the DOM tree, we call
// ClearInvalidation(ContainerNode&).
//
// When there are sibling rules and elements are added
// or removed from the tree, we call
// ScheduleSiblingInvalidationsAsDescendants for the
// potentially affected siblings.
//
// When there are pending invalidations for an element's
// siblings, and the element is being removed, we call
// RescheduleSiblingInvalidationsAsDescendants to
// reshedule the invalidations as descendant invalidations
// on the element's parent.
//
// See https://goo.gl/3ane6s and https://goo.gl/z0Z9gn
// for more detailed overviews of style invalidation.
// TODO: unify these documents into an .md file in the repo.

class CORE_EXPORT StyleInvalidator {
  DISALLOW_NEW();
  WTF_MAKE_NONCOPYABLE(StyleInvalidator);

 public:
  StyleInvalidator();
  ~StyleInvalidator();
  void Invalidate(Document&);
  void ScheduleInvalidationSetsForNode(const InvalidationLists&,
                                       ContainerNode&);
  void ScheduleSiblingInvalidationsAsDescendants(
      const InvalidationLists&,
      ContainerNode& scheduling_parent);
  void RescheduleSiblingInvalidationsAsDescendants(Element&);
  void ClearInvalidation(ContainerNode&);

  DEFINE_INLINE_TRACE() { visitor->Trace(pending_invalidation_map_); }

 private:
  struct RecursionData {
    RecursionData()
        : invalidate_custom_pseudo_(false),
          whole_subtree_invalid_(false),
          tree_boundary_crossing_(false),
          insertion_point_crossing_(false),
          invalidates_slotted_(false) {}

    void PushInvalidationSet(const InvalidationSet&);
    bool MatchesCurrentInvalidationSets(Element&) const;
    bool MatchesCurrentInvalidationSetsAsSlotted(Element&) const;

    bool HasInvalidationSets() const {
      return !WholeSubtreeInvalid() && invalidation_sets_.size();
    }

    bool WholeSubtreeInvalid() const { return whole_subtree_invalid_; }
    void SetWholeSubtreeInvalid() { whole_subtree_invalid_ = true; }

    bool TreeBoundaryCrossing() const { return tree_boundary_crossing_; }
    bool InsertionPointCrossing() const { return insertion_point_crossing_; }
    bool InvalidatesSlotted() const { return invalidates_slotted_; }

    using DescendantInvalidationSets = Vector<const InvalidationSet*, 16>;
    DescendantInvalidationSets invalidation_sets_;
    bool invalidate_custom_pseudo_;
    bool whole_subtree_invalid_;
    bool tree_boundary_crossing_;
    bool insertion_point_crossing_;
    bool invalidates_slotted_;
  };

  class SiblingData {
    STACK_ALLOCATED();

   public:
    SiblingData() : element_index_(0) {}

    void PushInvalidationSet(const SiblingInvalidationSet&);
    bool MatchCurrentInvalidationSets(Element&, RecursionData&);

    bool IsEmpty() const { return invalidation_entries_.IsEmpty(); }
    void Advance() { element_index_++; }

   private:
    struct Entry {
      DISALLOW_NEW_EXCEPT_PLACEMENT_NEW();
      Entry(const SiblingInvalidationSet* invalidation_set,
            unsigned invalidation_limit)
          : invalidation_set_(invalidation_set),
            invalidation_limit_(invalidation_limit) {}

      const SiblingInvalidationSet* invalidation_set_;
      unsigned invalidation_limit_;
    };

    Vector<Entry, 16> invalidation_entries_;
    unsigned element_index_;
  };

  bool Invalidate(Element&, RecursionData&, SiblingData&);
  bool InvalidateShadowRootChildren(Element&, RecursionData&);
  bool InvalidateChildren(Element&, RecursionData&);
  void InvalidateSlotDistributedElements(HTMLSlotElement&,
                                         const RecursionData&) const;
  bool CheckInvalidationSetsAgainstElement(Element&,
                                           RecursionData&,
                                           SiblingData&);
  void PushInvalidationSetsForContainerNode(ContainerNode&,
                                            RecursionData&,
                                            SiblingData&);

  class RecursionCheckpoint {
   public:
    RecursionCheckpoint(RecursionData* data)
        : prev_invalidation_sets_size_(data->invalidation_sets_.size()),
          prev_invalidate_custom_pseudo_(data->invalidate_custom_pseudo_),
          prev_whole_subtree_invalid_(data->whole_subtree_invalid_),
          tree_boundary_crossing_(data->tree_boundary_crossing_),
          insertion_point_crossing_(data->insertion_point_crossing_),
          invalidates_slotted_(data->invalidates_slotted_),
          data_(data) {}
    ~RecursionCheckpoint() {
      data_->invalidation_sets_.erase(
          prev_invalidation_sets_size_,
          data_->invalidation_sets_.size() - prev_invalidation_sets_size_);
      data_->invalidate_custom_pseudo_ = prev_invalidate_custom_pseudo_;
      data_->whole_subtree_invalid_ = prev_whole_subtree_invalid_;
      data_->tree_boundary_crossing_ = tree_boundary_crossing_;
      data_->insertion_point_crossing_ = insertion_point_crossing_;
      data_->invalidates_slotted_ = invalidates_slotted_;
    }

   private:
    int prev_invalidation_sets_size_;
    bool prev_invalidate_custom_pseudo_;
    bool prev_whole_subtree_invalid_;
    bool tree_boundary_crossing_;
    bool insertion_point_crossing_;
    bool invalidates_slotted_;
    RecursionData* data_;
  };

  using PendingInvalidationMap =
      HeapHashMap<Member<ContainerNode>, std::unique_ptr<PendingInvalidations>>;

  PendingInvalidations& EnsurePendingInvalidations(ContainerNode&);

  PendingInvalidationMap pending_invalidation_map_;
};

}  // namespace blink

#endif  // StyleInvalidator_h
