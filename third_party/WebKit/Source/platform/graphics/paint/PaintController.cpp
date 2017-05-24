// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "platform/graphics/paint/PaintController.h"

#include "platform/graphics/GraphicsLayer.h"
#include "platform/graphics/paint/DrawingDisplayItem.h"
#include "platform/instrumentation/tracing/TraceEvent.h"
#include "platform/wtf/AutoReset.h"
#include "platform/wtf/text/StringBuilder.h"
#include "third_party/skia/include/core/SkPictureAnalyzer.h"

#ifndef NDEBUG
#include "platform/graphics/LoggingCanvas.h"
#include <stdio.h>
#endif

static constexpr int kMaxNumberOfSlowPathsBeforeVeto = 5;

namespace blink {

void PaintController::SetTracksRasterInvalidations(bool value) {
  if (value ||
      RuntimeEnabledFeatures::paintUnderInvalidationCheckingEnabled()) {
    raster_invalidation_tracking_info_ =
        WTF::MakeUnique<RasterInvalidationTrackingInfo>();

    // This is called just after a full document cycle update, so all clients in
    // current_paint_artifact_ should be still alive.
    DCHECK(new_display_item_list_.IsEmpty());
    for (const auto& item : current_paint_artifact_.GetDisplayItemList()) {
      raster_invalidation_tracking_info_->old_client_debug_names.Set(
          &item.Client(), item.Client().DebugName());
    }
  } else {
    raster_invalidation_tracking_info_ = nullptr;
  }
}

const PaintArtifact& PaintController::GetPaintArtifact() const {
  DCHECK(new_display_item_list_.IsEmpty());
  DCHECK(new_paint_chunks_.IsInInitialState());
  return current_paint_artifact_;
}

bool PaintController::UseCachedDrawingIfPossible(
    const DisplayItemClient& client,
    DisplayItem::Type type) {
  DCHECK(DisplayItem::IsDrawingType(type));

  if (DisplayItemConstructionIsDisabled())
    return false;

  if (!ClientCacheIsValid(client))
    return false;

  if (RuntimeEnabledFeatures::paintUnderInvalidationCheckingEnabled() &&
      IsCheckingUnderInvalidation()) {
    // We are checking under-invalidation of a subsequence enclosing this
    // display item. Let the client continue to actually paint the display item.
    return false;
  }

  size_t cached_item = FindCachedItem(DisplayItem::Id(client, type));
  if (cached_item == kNotFound) {
    NOTREACHED();
    return false;
  }

  ++num_cached_new_items_;
  EnsureNewDisplayItemListInitialCapacity();
  if (!RuntimeEnabledFeatures::paintUnderInvalidationCheckingEnabled())
    ProcessNewItem(MoveItemFromCurrentListToNewList(cached_item));

  next_item_to_match_ = cached_item + 1;
  // Items before m_nextItemToMatch have been copied so we don't need to index
  // them.
  if (next_item_to_match_ > next_item_to_index_)
    next_item_to_index_ = next_item_to_match_;

  if (RuntimeEnabledFeatures::paintUnderInvalidationCheckingEnabled()) {
    if (!IsCheckingUnderInvalidation()) {
      under_invalidation_checking_begin_ = cached_item;
      under_invalidation_checking_end_ = cached_item + 1;
      under_invalidation_message_prefix_ = "";
    }
    // Return false to let the painter actually paint. We will check if the new
    // painting is the same as the cached one.
    return false;
  }

  return true;
}

bool PaintController::UseCachedSubsequenceIfPossible(
    const DisplayItemClient& client) {
  if (DisplayItemConstructionIsDisabled() || SubsequenceCachingIsDisabled())
    return false;

  if (!ClientCacheIsValid(client))
    return false;

  if (RuntimeEnabledFeatures::paintUnderInvalidationCheckingEnabled() &&
      IsCheckingUnderInvalidation()) {
    // We are checking under-invalidation of an ancestor subsequence enclosing
    // this one. The ancestor subsequence is supposed to have already "copied",
    // so we should let the client continue to actually paint the descendant
    // subsequences without "copying".
    return false;
  }

  SubsequenceMarkers* markers = GetSubsequenceMarkers(client);
  if (!markers) {
    return false;
  }

  // |cachedItem| will point to the first item after the subsequence or end of
  // the current list.
  EnsureNewDisplayItemListInitialCapacity();

  size_t size_before_copy = new_display_item_list_.size();
  CopyCachedSubsequence(markers->start, markers->end);

  if (!RuntimeEnabledFeatures::paintUnderInvalidationCheckingEnabled()) {
    AddCachedSubsequence(client, size_before_copy,
                         new_display_item_list_.size() - 1);
  }

  next_item_to_match_ = markers->end + 1;
  // Items before |m_nextItemToMatch| have been copied so we don't need to index
  // them.
  if (next_item_to_match_ > next_item_to_index_)
    next_item_to_index_ = next_item_to_match_;

  if (RuntimeEnabledFeatures::paintUnderInvalidationCheckingEnabled()) {
    // Return false to let the painter actually paint. We will check if the new
    // painting is the same as the cached one.
    return false;
  }

  return true;
}

PaintController::SubsequenceMarkers* PaintController::GetSubsequenceMarkers(
    const DisplayItemClient& client) {
  auto result = current_cached_subsequences_.find(&client);
  if (result == current_cached_subsequences_.end())
    return nullptr;
  return &result->value;
}

void PaintController::AddCachedSubsequence(const DisplayItemClient& client,
                                           unsigned start,
                                           unsigned end) {
  DCHECK(start <= end);
  DCHECK(end < new_display_item_list_.size());
  if (IsCheckingUnderInvalidation()) {
    SubsequenceMarkers* markers = GetSubsequenceMarkers(client);
    if (!markers) {
      ShowSequenceUnderInvalidationError(
          "under-invalidation : unexpected subsequence", client, start, end);
      CHECK(false);
    }
    if (markers->end - markers->start != end - start) {
      ShowSequenceUnderInvalidationError(
          "under-invalidation: new subsequence wrong length", client, start,
          end);
      CHECK(false);
    }
  }

  DCHECK(new_cached_subsequences_.find(&client) ==
         new_cached_subsequences_.end());

  new_cached_subsequences_.insert(&client, SubsequenceMarkers(start, end));
  last_cached_subsequence_end_ = end;
}

bool PaintController::LastDisplayItemIsNoopBegin() const {
  if (new_display_item_list_.IsEmpty())
    return false;

  const auto& last_display_item = new_display_item_list_.Last();
  return last_display_item.IsBegin() && !last_display_item.DrawsContent();
}

bool PaintController::LastDisplayItemIsSubsequenceEnd() const {
  return !new_cached_subsequences_.IsEmpty() &&
         last_cached_subsequence_end_ == new_display_item_list_.size() - 1;
}

void PaintController::RemoveLastDisplayItem() {
  if (new_display_item_list_.IsEmpty())
    return;

#if DCHECK_IS_ON()
  // Also remove the index pointing to the removed display item.
  IndicesByClientMap::iterator it = new_display_item_indices_by_client_.find(
      &new_display_item_list_.Last().Client());
  if (it != new_display_item_indices_by_client_.end()) {
    Vector<size_t>& indices = it->value;
    if (!indices.IsEmpty() &&
        indices.back() == (new_display_item_list_.size() - 1))
      indices.pop_back();
  }
#endif

  if (RuntimeEnabledFeatures::paintUnderInvalidationCheckingEnabled() &&
      IsCheckingUnderInvalidation()) {
    if (skipped_probable_under_invalidation_count_) {
      --skipped_probable_under_invalidation_count_;
    } else {
      DCHECK(under_invalidation_checking_begin_);
      --under_invalidation_checking_begin_;
    }
  }
  new_display_item_list_.RemoveLast();

  if (RuntimeEnabledFeatures::slimmingPaintV2Enabled())
    new_paint_chunks_.DecrementDisplayItemIndex();
}

const DisplayItem* PaintController::LastDisplayItem(unsigned offset) {
  if (offset < new_display_item_list_.size())
    return &new_display_item_list_[new_display_item_list_.size() - offset - 1];
  return nullptr;
}

#if CHECK_DISPLAY_ITEM_CLIENT_ALIVENESS
void PaintController::BeginShouldKeepAlive(const DisplayItemClient& client) {
  if (!IsSkippingCache()) {
    // Mark the client shouldKeepAlive under this PaintController.
    // The status will end after the new display items are committed.
    client.BeginShouldKeepAlive(this);

    if (!current_subsequence_clients_.IsEmpty()) {
      // Mark the client shouldKeepAlive under the current subsequence.
      // The status will end when the subsequence owner is invalidated or
      // deleted.
      client.BeginShouldKeepAlive(current_subsequence_clients_.back());
    }
  }
}
#endif

void PaintController::ProcessNewItem(DisplayItem& display_item) {
  DCHECK(!construction_disabled_);

#if CHECK_DISPLAY_ITEM_CLIENT_ALIVENESS
  if (display_item.IsCacheable()) {
    BeginShouldKeepAlive(display_item.Client());
  }
#endif

  if (IsSkippingCache())
    display_item.SetSkippedCache();

  if (raster_invalidation_tracking_info_) {
    raster_invalidation_tracking_info_->new_client_debug_names.insert(
        &display_item.Client(), display_item.Client().DebugName());
  }

  if (RuntimeEnabledFeatures::slimmingPaintV2Enabled()) {
    size_t last_chunk_index = new_paint_chunks_.LastChunkIndex();
    if (new_paint_chunks_.IncrementDisplayItemIndex(display_item)) {
      DCHECK(last_chunk_index != new_paint_chunks_.LastChunkIndex());
      if (last_chunk_index != kNotFound) {
        GenerateRasterInvalidations(
            new_paint_chunks_.PaintChunkAt(last_chunk_index));
      }
    }
  }

#if DCHECK_IS_ON()
  // Verify noop begin/end pairs have been removed.
  if (new_display_item_list_.size() >= 2 && display_item.IsEnd()) {
    const auto& begin_display_item =
        new_display_item_list_[new_display_item_list_.size() - 2];
    if (begin_display_item.IsBegin() && !begin_display_item.DrawsContent())
      DCHECK(!display_item.IsEndAndPairedWith(begin_display_item.GetType()));
  }

  size_t index = FindMatchingItemFromIndex(display_item.GetId(),
                                           new_display_item_indices_by_client_,
                                           new_display_item_list_);
  if (index != kNotFound) {
#ifndef NDEBUG
    ShowDebugData();
    WTFLogAlways(
        "DisplayItem %s has duplicated id with previous %s (index=%zu)\n",
        display_item.AsDebugString().Utf8().data(),
        new_display_item_list_[index].AsDebugString().Utf8().data(), index);
#endif
    NOTREACHED();
  }
  AddItemToIndexIfNeeded(display_item, new_display_item_list_.size() - 1,
                         new_display_item_indices_by_client_);
#endif  // DCHECK_IS_ON()

  if (RuntimeEnabledFeatures::paintUnderInvalidationCheckingEnabled())
    CheckUnderInvalidation();

  if (!frame_first_paints_.back().first_painted && display_item.IsDrawing() &&
      // Here we ignore all document-background paintings because we don't
      // know if the background is default. ViewPainter should have called
      // setFirstPainted() if this display item is for non-default
      // background.
      display_item.GetType() != DisplayItem::kDocumentBackground &&
      display_item.DrawsContent()) {
    SetFirstPainted();
  }
}

DisplayItem& PaintController::MoveItemFromCurrentListToNewList(size_t index) {
  items_moved_into_new_list_.resize(
      current_paint_artifact_.GetDisplayItemList().size());
  items_moved_into_new_list_[index] = new_display_item_list_.size();
  return new_display_item_list_.AppendByMoving(
      current_paint_artifact_.GetDisplayItemList()[index]);
}

void PaintController::UpdateCurrentPaintChunkProperties(
    const PaintChunk::Id* id,
    const PaintChunkProperties& new_properties) {
  new_paint_chunks_.UpdateCurrentPaintChunkProperties(id, new_properties);
}

const PaintChunkProperties& PaintController::CurrentPaintChunkProperties()
    const {
  return new_paint_chunks_.CurrentPaintChunkProperties();
}

void PaintController::InvalidateAll() {
  // Can only be called during layout/paintInvalidation, not during painting.
  DCHECK(new_display_item_list_.IsEmpty());
  current_paint_artifact_.Reset();
  current_cache_generation_.Invalidate();
}

bool PaintController::ClientCacheIsValid(
    const DisplayItemClient& client) const {
#if CHECK_DISPLAY_ITEM_CLIENT_ALIVENESS
  CHECK(client.IsAlive());
#endif
  if (IsSkippingCache())
    return false;
  return client.DisplayItemsAreCached(current_cache_generation_);
}

size_t PaintController::FindMatchingItemFromIndex(
    const DisplayItem::Id& id,
    const IndicesByClientMap& display_item_indices_by_client,
    const DisplayItemList& list) {
  IndicesByClientMap::const_iterator it =
      display_item_indices_by_client.find(&id.client);
  if (it == display_item_indices_by_client.end())
    return kNotFound;

  const Vector<size_t>& indices = it->value;
  for (size_t index : indices) {
    const DisplayItem& existing_item = list[index];
    if (!existing_item.HasValidClient())
      continue;
    DCHECK(existing_item.Client() == id.client);
    if (id == existing_item.GetId())
      return index;
  }

  return kNotFound;
}

void PaintController::AddItemToIndexIfNeeded(
    const DisplayItem& display_item,
    size_t index,
    IndicesByClientMap& display_item_indices_by_client) {
  if (!display_item.IsCacheable())
    return;

  IndicesByClientMap::iterator it =
      display_item_indices_by_client.find(&display_item.Client());
  Vector<size_t>& indices =
      it == display_item_indices_by_client.end()
          ? display_item_indices_by_client
                .insert(&display_item.Client(), Vector<size_t>())
                .stored_value->value
          : it->value;
  indices.push_back(index);
}

size_t PaintController::FindCachedItem(const DisplayItem::Id& id) {
  DCHECK(ClientCacheIsValid(id.client));

  // Try to find the item sequentially first. This is fast if the current list
  // and the new list are in the same order around the new item. If found, we
  // don't need to update and lookup the index.
  for (size_t i = next_item_to_match_;
       i < current_paint_artifact_.GetDisplayItemList().size(); ++i) {
    // We encounter an item that has already been copied which indicates we
    // can't do sequential matching.
    const DisplayItem& item = current_paint_artifact_.GetDisplayItemList()[i];
    if (!item.HasValidClient())
      break;
    if (id == item.GetId()) {
#ifndef NDEBUG
      ++num_sequential_matches_;
#endif
      return i;
    }
    // We encounter a different cacheable item which also indicates we can't do
    // sequential matching.
    if (item.IsCacheable())
      break;
  }

  size_t found_index =
      FindMatchingItemFromIndex(id, out_of_order_item_indices_,
                                current_paint_artifact_.GetDisplayItemList());
  if (found_index != kNotFound) {
#ifndef NDEBUG
    ++num_out_of_order_matches_;
#endif
    return found_index;
  }

  return FindOutOfOrderCachedItemForward(id);
}

// Find forward for the item and index all skipped indexable items.
size_t PaintController::FindOutOfOrderCachedItemForward(
    const DisplayItem::Id& id) {
  for (size_t i = next_item_to_index_;
       i < current_paint_artifact_.GetDisplayItemList().size(); ++i) {
    const DisplayItem& item = current_paint_artifact_.GetDisplayItemList()[i];
    DCHECK(item.HasValidClient());
    if (id == item.GetId()) {
#ifndef NDEBUG
      ++num_sequential_matches_;
#endif
      return i;
    }
    if (item.IsCacheable()) {
#ifndef NDEBUG
      ++num_indexed_items_;
#endif
      AddItemToIndexIfNeeded(item, i, out_of_order_item_indices_);
    }
  }

#ifndef NDEBUG
  ShowDebugData();
  LOG(ERROR) << id.client.DebugName() << ":"
             << DisplayItem::TypeAsDebugString(id.type);
#endif

  if (RuntimeEnabledFeatures::paintUnderInvalidationCheckingEnabled())
    CHECK(false) << "Can't find cached display item";

  // We did not find the cached display item. This should be impossible, but may
  // occur if there is a bug in the system, such as under-invalidation,
  // incorrect cache checking or duplicate display ids. In this case, the caller
  // should fall back to repaint the display item.
  return kNotFound;
}

// Copies a cached subsequence from current list to the new list.
// When paintUnderInvaldiationCheckingEnabled() we'll not actually
// copy the subsequence, but mark the begin and end of the subsequence for
// under-invalidation checking.
void PaintController::CopyCachedSubsequence(size_t begin_index,
                                            size_t end_index) {
  AutoReset<size_t> subsequence_begin_index(
      &current_cached_subsequence_begin_index_in_new_list_,
      new_display_item_list_.size());
  DisplayItem* cached_item =
      &current_paint_artifact_.GetDisplayItemList()[begin_index];

  if (RuntimeEnabledFeatures::paintUnderInvalidationCheckingEnabled()) {
    DCHECK(!IsCheckingUnderInvalidation());
    under_invalidation_checking_begin_ = begin_index;
    under_invalidation_message_prefix_ =
        "(In cached subsequence starting with " +
        cached_item->Client().DebugName() + ")";
  }

  Vector<PaintChunk>::const_iterator cached_chunk;
  if (RuntimeEnabledFeatures::slimmingPaintV2Enabled()) {
    cached_chunk =
        current_paint_artifact_.FindChunkByDisplayItemIndex(begin_index);
    DCHECK(cached_chunk != current_paint_artifact_.PaintChunks().end());
    UpdateCurrentPaintChunkProperties(
        cached_chunk->id ? &*cached_chunk->id : nullptr,
        cached_chunk->properties);
  } else {
    // Avoid uninitialized variable error on Windows.
    cached_chunk = current_paint_artifact_.PaintChunks().begin();
  }

  for (size_t current_index = begin_index; current_index <= end_index;
       ++current_index) {
    cached_item = &current_paint_artifact_.GetDisplayItemList()[current_index];
    DCHECK(cached_item->HasValidClient());
    // TODO(chrishtr); remove this hack once crbug.com/712660 is resolved.
    if (!cached_item->HasValidClient())
      continue;
#if CHECK_DISPLAY_ITEM_CLIENT_ALIVENESS
    CHECK(cached_item->Client().IsAlive());
#endif
    ++num_cached_new_items_;
    if (!RuntimeEnabledFeatures::paintUnderInvalidationCheckingEnabled()) {
      if (RuntimeEnabledFeatures::slimmingPaintV2Enabled() &&
          current_index == cached_chunk->end_index) {
        ++cached_chunk;
        DCHECK(cached_chunk != current_paint_artifact_.PaintChunks().end());
        UpdateCurrentPaintChunkProperties(
            cached_chunk->id ? &*cached_chunk->id : nullptr,
            cached_chunk->properties);
      }
      ProcessNewItem(MoveItemFromCurrentListToNewList(current_index));
      if (RuntimeEnabledFeatures::slimmingPaintV2Enabled())
        DCHECK((!new_paint_chunks_.LastChunk().id && !cached_chunk->id) ||
               new_paint_chunks_.LastChunk().Matches(*cached_chunk));
    }
  }

  if (RuntimeEnabledFeatures::paintUnderInvalidationCheckingEnabled()) {
    under_invalidation_checking_end_ = end_index + 1;
    DCHECK(IsCheckingUnderInvalidation());
  }
}

DISABLE_CFI_PERF
static IntRect VisualRectForDisplayItem(
    const DisplayItem& display_item,
    const LayoutSize& offset_from_layout_object) {
  LayoutRect visual_rect = display_item.Client().VisualRect();
  visual_rect.Move(-offset_from_layout_object);
  return EnclosingIntRect(visual_rect);
}

void PaintController::ResetCurrentListIndices() {
  next_item_to_match_ = 0;
  next_item_to_index_ = 0;
  next_chunk_to_match_ = 0;
  under_invalidation_checking_begin_ = 0;
  under_invalidation_checking_end_ = 0;
  skipped_probable_under_invalidation_count_ = 0;
}

DISABLE_CFI_PERF
void PaintController::CommitNewDisplayItems(
    const LayoutSize& offset_from_layout_object) {
  TRACE_EVENT2("blink,benchmark", "PaintController::commitNewDisplayItems",
               "current_display_list_size",
               (int)current_paint_artifact_.GetDisplayItemList().size(),
               "num_non_cached_new_items",
               (int)new_display_item_list_.size() - num_cached_new_items_);
  num_cached_new_items_ = 0;
  // These data structures are used during painting only.
  DCHECK(!IsSkippingCache());
#if DCHECK_IS_ON()
  new_display_item_indices_by_client_.clear();
#endif

  if (RuntimeEnabledFeatures::slimmingPaintV2Enabled() &&
      !new_display_item_list_.IsEmpty())
    GenerateRasterInvalidations(new_paint_chunks_.LastChunk());

  int num_slow_paths = 0;

  current_cache_generation_ =
      DisplayItemClient::CacheGenerationOrInvalidationReason::Next();

  new_cached_subsequences_.swap(current_cached_subsequences_);
  new_cached_subsequences_.clear();
  last_cached_subsequence_end_ = 0;
  for (auto& item : current_cached_subsequences_) {
    item.key->SetDisplayItemsCached(current_cache_generation_);
#if CHECK_DISPLAY_ITEM_CLIENT_ALIVENESS
    DisplayItemClient::EndShouldKeepAliveAllClients(item.key);
    DCHECK(current_subsequence_clients_.IsEmpty());
#endif
  }

  Vector<const DisplayItemClient*> skipped_cache_clients;
  for (const auto& item : new_display_item_list_) {
    // No reason to continue the analysis once we have a veto.
    if (num_slow_paths <= kMaxNumberOfSlowPathsBeforeVeto)
      num_slow_paths += item.NumberOfSlowPaths();

    // TODO(wkorman): Only compute and append visual rect for drawings.
    new_display_item_list_.AppendVisualRect(
        VisualRectForDisplayItem(item, offset_from_layout_object));

    if (item.IsCacheable()) {
      item.Client().SetDisplayItemsCached(current_cache_generation_);
    } else {
      if (item.Client().IsJustCreated())
        item.Client().ClearIsJustCreated();
      if (item.SkippedCache())
        skipped_cache_clients.push_back(&item.Client());
    }
  }

  for (auto* client : skipped_cache_clients)
    client->SetDisplayItemsUncached();

  // The new list will not be appended to again so we can release unused memory.
  new_display_item_list_.ShrinkToFit();

  if (raster_invalidation_tracking_info_) {
    for (const auto& chunk : current_paint_artifact_.PaintChunks())
      raster_invalidation_tracking_info_->map.Remove(&chunk);
  }
  current_paint_artifact_ = PaintArtifact(
      std::move(new_display_item_list_), new_paint_chunks_.ReleasePaintChunks(),
      num_slow_paths <= kMaxNumberOfSlowPathsBeforeVeto);

  ResetCurrentListIndices();
  out_of_order_item_indices_.clear();
  out_of_order_chunk_indices_.clear();
  items_moved_into_new_list_.clear();

  if (RuntimeEnabledFeatures::slimmingPaintV2Enabled()) {
    for (const auto& chunk : current_paint_artifact_.PaintChunks()) {
      if (chunk.id && chunk.id->client.IsJustCreated())
        chunk.id->client.ClearIsJustCreated();
    }
  }

  // We'll allocate the initial buffer when we start the next paint.
  new_display_item_list_ = DisplayItemList(0);

#if CHECK_DISPLAY_ITEM_CLIENT_ALIVENESS
  DisplayItemClient::EndShouldKeepAliveAllClients(this);
#endif

#ifndef NDEBUG
  num_sequential_matches_ = 0;
  num_out_of_order_matches_ = 0;
  num_indexed_items_ = 0;
#endif

  if (raster_invalidation_tracking_info_) {
    raster_invalidation_tracking_info_->old_client_debug_names.clear();
    std::swap(raster_invalidation_tracking_info_->old_client_debug_names,
              raster_invalidation_tracking_info_->new_client_debug_names);
  }
}

size_t PaintController::ApproximateUnsharedMemoryUsage() const {
  size_t memory_usage = sizeof(*this);

  // Memory outside this class due to m_currentPaintArtifact.
  memory_usage += current_paint_artifact_.ApproximateUnsharedMemoryUsage() -
                  sizeof(current_paint_artifact_);

  // TODO(jbroman): If display items begin to have significant external memory
  // usage that's not shared with the embedder, we should account for it here.
  //
  // External objects, shared with the embedder, such as PaintRecord, should be
  // excluded to avoid double counting. It is the embedder's responsibility to
  // count such objects.
  //
  // At time of writing, the only known case of unshared external memory was
  // the rounded clips vector in ClipDisplayItem, which is not expected to
  // contribute significantly to memory usage.

  // Memory outside this class due to m_newDisplayItemList.
  DCHECK(new_display_item_list_.IsEmpty());
  memory_usage += new_display_item_list_.MemoryUsageInBytes();

  return memory_usage;
}

void PaintController::AppendDebugDrawingAfterCommit(
    const DisplayItemClient& display_item_client,
    sk_sp<PaintRecord> record,
    const LayoutSize& offset_from_layout_object) {
  DCHECK(new_display_item_list_.IsEmpty());
  DrawingDisplayItem& display_item =
      current_paint_artifact_.GetDisplayItemList()
          .AllocateAndConstruct<DrawingDisplayItem>(display_item_client,
                                                    DisplayItem::kDebugDrawing,
                                                    std::move(record));
  display_item.SetSkippedCache();
  // TODO(wkorman): Only compute and append visual rect for drawings.
  current_paint_artifact_.GetDisplayItemList().AppendVisualRect(
      VisualRectForDisplayItem(display_item, offset_from_layout_object));
}

void PaintController::GenerateRasterInvalidations(PaintChunk& new_chunk) {
  DCHECK(RuntimeEnabledFeatures::slimmingPaintV2Enabled());
  if (new_chunk.begin_index >=
      current_cached_subsequence_begin_index_in_new_list_)
    return;

  static FloatRect infinite_float_rect(LayoutRect::InfiniteIntRect());
  if (!new_chunk.id) {
    // This chunk is not cacheable, so always invalidate the whole chunk.
    AddRasterInvalidation(
        new_display_item_list_[new_chunk.begin_index].Client(), new_chunk,
        infinite_float_rect);
    return;
  }

  // Try to match old chunk sequentially first.
  const auto& old_chunks = current_paint_artifact_.PaintChunks();
  while (next_chunk_to_match_ < old_chunks.size()) {
    const PaintChunk& old_chunk = old_chunks[next_chunk_to_match_];
    if (new_chunk.Matches(old_chunk)) {
      GenerateRasterInvalidationsComparingChunks(new_chunk, old_chunk);
      ++next_chunk_to_match_;
      return;
    }

    // Add skipped old chunks into the index.
    if (old_chunk.id) {
      auto it = out_of_order_chunk_indices_.find(&old_chunk.id->client);
      Vector<size_t>& indices =
          it == out_of_order_chunk_indices_.end()
              ? out_of_order_chunk_indices_
                    .insert(&old_chunk.id->client, Vector<size_t>())
                    .stored_value->value
              : it->value;
      indices.push_back(next_chunk_to_match_);
    }
    ++next_chunk_to_match_;
  }

  // Sequential matching reaches the end. Find from the out-of-order index.
  auto it = out_of_order_chunk_indices_.find(&new_chunk.id->client);
  if (it != out_of_order_chunk_indices_.end()) {
    for (size_t i : it->value) {
      if (new_chunk.Matches(old_chunks[i])) {
        GenerateRasterInvalidationsComparingChunks(new_chunk, old_chunks[i]);
        return;
      }
    }
  }

  // We reach here because the chunk is new.
  AddRasterInvalidation(new_display_item_list_[new_chunk.begin_index].Client(),
                        new_chunk, infinite_float_rect);
}

void PaintController::AddRasterInvalidation(const DisplayItemClient& client,
                                            PaintChunk& chunk,
                                            const FloatRect& rect) {
  chunk.raster_invalidation_rects.push_back(rect);
  if (raster_invalidation_tracking_info_)
    TrackRasterInvalidation(client, chunk, rect);
}

void PaintController::TrackRasterInvalidation(const DisplayItemClient& client,
                                              PaintChunk& chunk,
                                              const FloatRect& rect) {
  DCHECK(raster_invalidation_tracking_info_);

  RasterInvalidationInfo info;
  info.rect = EnclosingIntRect(rect);
  info.client = &client;
  auto it =
      raster_invalidation_tracking_info_->new_client_debug_names.find(&client);
  if (it == raster_invalidation_tracking_info_->new_client_debug_names.end()) {
    it = raster_invalidation_tracking_info_->old_client_debug_names.find(
        &client);
    // The client should be either in new list or in old list.
    DCHECK(it !=
           raster_invalidation_tracking_info_->old_client_debug_names.end());
    info.reason = PaintInvalidationReason::kDisappeared;
  } else {
    info.reason = client.GetPaintInvalidationReason();
  }
  info.client_debug_name = it->value;
  raster_invalidation_tracking_info_->map.Add(&chunk).invalidations.push_back(
      info);
}

void PaintController::GenerateRasterInvalidationsComparingChunks(
    PaintChunk& new_chunk,
    const PaintChunk& old_chunk) {
  DCHECK(RuntimeEnabledFeatures::slimmingPaintV2Enabled());

  // TODO(wangxianzhu): Handle PaintInvalidationIncremental.
  // TODO(wangxianzhu): Optimize paint offset change.

  HashSet<const DisplayItemClient*> invalidated_clients_in_old_chunk;
  size_t highest_moved_to_index = 0;
  for (size_t old_index = old_chunk.begin_index;
       old_index < old_chunk.end_index; ++old_index) {
    const DisplayItem& old_item =
        current_paint_artifact_.GetDisplayItemList()[old_index];
    const DisplayItemClient* client_to_invalidate = nullptr;

    if (!old_item.HasValidClient()) {
      size_t moved_to_index = items_moved_into_new_list_[old_index];
      if (new_display_item_list_[moved_to_index].DrawsContent()) {
        if (moved_to_index < new_chunk.begin_index ||
            moved_to_index >= new_chunk.end_index) {
          // The item has been moved into another chunk, so need to invalidate
          // it in the old chunk.
          client_to_invalidate =
              &new_display_item_list_[moved_to_index].Client();
          // And invalidate in the new chunk into which the item was moved.
          PaintChunk& moved_to_chunk =
              new_paint_chunks_.FindChunkByDisplayItemIndex(moved_to_index);
          AddRasterInvalidation(*client_to_invalidate, moved_to_chunk,
                                FloatRect(client_to_invalidate->VisualRect()));
        } else if (moved_to_index < highest_moved_to_index) {
          // The item has been moved behind other cached items, so need to
          // invalidate the area that is probably exposed by the item moved
          // earlier.
          client_to_invalidate =
              &new_display_item_list_[moved_to_index].Client();
        } else {
          highest_moved_to_index = moved_to_index;
        }
      }
    } else if (old_item.DrawsContent()) {
      client_to_invalidate = &old_item.Client();
    }
    if (client_to_invalidate &&
        invalidated_clients_in_old_chunk.insert(client_to_invalidate)
            .is_new_entry) {
      AddRasterInvalidation(
          *client_to_invalidate, new_chunk,
          FloatRect(current_paint_artifact_.GetDisplayItemList().VisualRect(
              old_index)));
    }
  }

  HashSet<const DisplayItemClient*> invalidated_clients_in_new_chunk;
  for (size_t new_index = new_chunk.begin_index;
       new_index < new_chunk.end_index; ++new_index) {
    const DisplayItem& new_item = new_display_item_list_[new_index];
    if (new_item.DrawsContent() && !ClientCacheIsValid(new_item.Client()) &&
        invalidated_clients_in_new_chunk.insert(&new_item.Client())
            .is_new_entry) {
      AddRasterInvalidation(new_item.Client(), new_chunk,
                            FloatRect(new_item.Client().VisualRect()));
    }
  }
}

void PaintController::ShowUnderInvalidationError(
    const char* reason,
    const DisplayItem& new_item,
    const DisplayItem* old_item) const {
  LOG(ERROR) << under_invalidation_message_prefix_ << " " << reason;
#ifndef NDEBUG
  LOG(ERROR) << "New display item: " << new_item.AsDebugString();
  LOG(ERROR) << "Old display item: "
             << (old_item ? old_item->AsDebugString() : "None");
#else
  LOG(ERROR) << "Run debug build to get more details.";
#endif
  LOG(ERROR) << "See http://crbug.com/619103.";

#ifndef NDEBUG
  const PaintRecord* new_record = nullptr;
  if (new_item.IsDrawing()) {
    new_record =
        static_cast<const DrawingDisplayItem&>(new_item).GetPaintRecord().get();
  }
  const PaintRecord* old_record = nullptr;
  if (old_item->IsDrawing()) {
    old_record = static_cast<const DrawingDisplayItem*>(old_item)
                     ->GetPaintRecord()
                     .get();
  }
  LOG(INFO) << "new record:\n"
            << (new_record ? RecordAsDebugString(new_record) : "None");
  LOG(INFO) << "old record:\n"
            << (old_record ? RecordAsDebugString(old_record) : "None");

  ShowDebugData();
#endif  // NDEBUG
}

void PaintController::ShowSequenceUnderInvalidationError(
    const char* reason,
    const DisplayItemClient& client,
    int start,
    int end) {
  LOG(ERROR) << under_invalidation_message_prefix_ << " " << reason;
  LOG(ERROR) << "Subsequence client: " << client.DebugName();
#ifndef NDEBUG
//  showDebugData();
#else
  LOG(ERROR) << "Run debug build to get more details.";
#endif
  LOG(ERROR) << "See http://crbug.com/619103.";
}

void PaintController::CheckUnderInvalidation() {
  DCHECK(RuntimeEnabledFeatures::paintUnderInvalidationCheckingEnabled());

  if (!IsCheckingUnderInvalidation())
    return;

  const DisplayItem& new_item = new_display_item_list_.Last();
  size_t old_item_index = under_invalidation_checking_begin_ +
                          skipped_probable_under_invalidation_count_;
  DisplayItem* old_item =
      old_item_index < current_paint_artifact_.GetDisplayItemList().size()
          ? &current_paint_artifact_.GetDisplayItemList()[old_item_index]
          : nullptr;

  bool old_and_new_equal = old_item && new_item.Equals(*old_item);
  if (!old_and_new_equal) {
    if (new_item.IsBegin()) {
      // Temporarily skip mismatching begin display item which may be removed
      // when we remove a no-op pair.
      ++skipped_probable_under_invalidation_count_;
      return;
    }
    if (new_item.IsDrawing() &&
        skipped_probable_under_invalidation_count_ == 1) {
      DCHECK_GE(new_display_item_list_.size(), 2u);
      if (new_display_item_list_[new_display_item_list_.size() - 2].GetType() ==
          DisplayItem::kBeginCompositing) {
        // This might be a drawing item between a pair of begin/end compositing
        // display items that will be folded into a single drawing display item.
        ++skipped_probable_under_invalidation_count_;
        return;
      }
    }
  }

  if (skipped_probable_under_invalidation_count_ || !old_and_new_equal) {
    // If we ever skipped reporting any under-invalidations, report the earliest
    // one.
    ShowUnderInvalidationError(
        "under-invalidation: display item changed",
        new_display_item_list_[new_display_item_list_.size() -
                               skipped_probable_under_invalidation_count_ - 1],
        &current_paint_artifact_
             .GetDisplayItemList()[under_invalidation_checking_begin_]);
    CHECK(false);
  }

  // Discard the forced repainted display item and move the cached item into
  // m_newDisplayItemList. This is to align with the
  // non-under-invalidation-checking path to empty the original cached slot,
  // leaving only disappeared or invalidated display items in the old list after
  // painting.
  new_display_item_list_.RemoveLast();
  MoveItemFromCurrentListToNewList(old_item_index);

  ++under_invalidation_checking_begin_;
}

void PaintController::ShowDebugDataInternal(bool show_paint_records) const {
  WTFLogAlways("current display item list: [%s]\n",
               current_paint_artifact_.GetDisplayItemList()
                   .SubsequenceAsJSON(
                       0, current_paint_artifact_.GetDisplayItemList().size(),
                       show_paint_records
                           ? DisplayItemList::JsonOptions::kShowPaintRecords
                           : DisplayItemList::JsonOptions::kDefault)
                   ->ToPrettyJSONString()
                   .Utf8()
                   .data());
  // debugName() and clientCacheIsValid() can only be called on a live
  // client, so only output it for m_newDisplayItemList, in which we are
  // sure the clients are all alive.
  WTFLogAlways(
      "new display item list: [%s]\n",
      new_display_item_list_
          .SubsequenceAsJSON(
              0, new_display_item_list_.size(),
              show_paint_records
                  ? (DisplayItemList::JsonOptions::kShowPaintRecords |
                     DisplayItemList::JsonOptions::kShowClientDebugName)
                  : DisplayItemList::JsonOptions::kShowClientDebugName)
          ->ToPrettyJSONString()
          .Utf8()
          .data());
}

void PaintController::SetFirstPainted() {
  frame_first_paints_.back().first_painted = true;
}

void PaintController::SetTextPainted() {
  frame_first_paints_.back().text_painted = true;
}

void PaintController::SetImagePainted() {
  frame_first_paints_.back().image_painted = true;
}

void PaintController::BeginFrame(const void* frame) {
  frame_first_paints_.push_back(FrameFirstPaint(frame));
}

FrameFirstPaint PaintController::EndFrame(const void* frame) {
  FrameFirstPaint result = frame_first_paints_.back();
  DCHECK(result.frame == frame);
  frame_first_paints_.pop_back();
  return result;
}

}  // namespace blink
