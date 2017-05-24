// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PaintController_h
#define PaintController_h

#include <memory>
#include <utility>
#include "platform/PlatformExport.h"
#include "platform/RuntimeEnabledFeatures.h"
#include "platform/geometry/IntRect.h"
#include "platform/geometry/LayoutPoint.h"
#include "platform/graphics/ContiguousContainer.h"
#include "platform/graphics/paint/DisplayItem.h"
#include "platform/graphics/paint/DisplayItemList.h"
#include "platform/graphics/paint/PaintArtifact.h"
#include "platform/graphics/paint/PaintChunk.h"
#include "platform/graphics/paint/PaintChunker.h"
#include "platform/graphics/paint/RasterInvalidationTracking.h"
#include "platform/graphics/paint/Transform3DDisplayItem.h"
#include "platform/wtf/Alignment.h"
#include "platform/wtf/Assertions.h"
#include "platform/wtf/HashMap.h"
#include "platform/wtf/HashSet.h"
#include "platform/wtf/PtrUtil.h"
#include "platform/wtf/Vector.h"
#include "third_party/skia/include/core/SkRefCnt.h"

namespace blink {

static const size_t kInitialDisplayItemListCapacityBytes = 512;

template class RasterInvalidationTrackingMap<const PaintChunk>;

// FrameFirstPaint stores first-paint, text or image painted for the
// corresponding frame. They are never reset to false. First-paint is defined in
// https://github.com/WICG/paint-timing. It excludes default background paint.
struct FrameFirstPaint {
  FrameFirstPaint(const void* frame)
      : frame(frame),
        first_painted(false),
        text_painted(false),
        image_painted(false) {}

  const void* frame;
  bool first_painted : 1;
  bool text_painted : 1;
  bool image_painted : 1;
};

// Responsible for processing display items as they are produced, and producing
// a final paint artifact when complete. This class includes logic for caching,
// cache invalidation, and merging.
class PLATFORM_EXPORT PaintController {
  WTF_MAKE_NONCOPYABLE(PaintController);
  USING_FAST_MALLOC(PaintController);

 public:
  static std::unique_ptr<PaintController> Create() {
    return WTF::WrapUnique(new PaintController());
  }

  ~PaintController() {
    // New display items should be committed before PaintController is
    // destructed.
    DCHECK(new_display_item_list_.IsEmpty());
#if CHECK_DISPLAY_ITEM_CLIENT_ALIVENESS
    DisplayItemClient::EndShouldKeepAliveAllClients(this);
#endif
  }

  void InvalidateAll();

  // These methods are called during painting.

  // Provide a new set of paint chunk properties to apply to recorded display
  // items, for Slimming Paint v2.
  void UpdateCurrentPaintChunkProperties(const PaintChunk::Id*,
                                         const PaintChunkProperties&);

  // Retrieve the current paint properties.
  const PaintChunkProperties& CurrentPaintChunkProperties() const;

  template <typename DisplayItemClass, typename... Args>
  void CreateAndAppend(Args&&... args) {
    static_assert(WTF::IsSubclass<DisplayItemClass, DisplayItem>::value,
                  "Can only createAndAppend subclasses of DisplayItem.");
    static_assert(
        sizeof(DisplayItemClass) <= kMaximumDisplayItemSize,
        "DisplayItem subclass is larger than kMaximumDisplayItemSize.");

    if (DisplayItemConstructionIsDisabled())
      return;

    EnsureNewDisplayItemListInitialCapacity();
    DisplayItemClass& display_item =
        new_display_item_list_.AllocateAndConstruct<DisplayItemClass>(
            std::forward<Args>(args)...);
    ProcessNewItem(display_item);
  }

  // Creates and appends an ending display item to pair with a preceding
  // beginning item iff the display item actually draws content. For no-op
  // items, rather than creating an ending item, the begin item will
  // instead be removed, thereby maintaining brevity of the list. If display
  // item construction is disabled, no list mutations will be performed.
  template <typename DisplayItemClass, typename... Args>
  void EndItem(Args&&... args) {
    if (DisplayItemConstructionIsDisabled())
      return;
    if (LastDisplayItemIsNoopBegin())
      RemoveLastDisplayItem();
    else
      CreateAndAppend<DisplayItemClass>(std::forward<Args>(args)...);
  }

  // Tries to find the cached drawing display item corresponding to the given
  // parameters. If found, appends the cached display item to the new display
  // list and returns true. Otherwise returns false.
  bool UseCachedDrawingIfPossible(const DisplayItemClient&, DisplayItem::Type);

  // Tries to find the cached subsequence corresponding to the given parameters.
  // If found, copies the cache subsequence to the new display list and returns
  // true. Otherwise returns false.
  bool UseCachedSubsequenceIfPossible(const DisplayItemClient&);

  void AddCachedSubsequence(const DisplayItemClient&,
                            unsigned start,
                            unsigned end);

  // True if the last display item is a begin that doesn't draw content.
  void RemoveLastDisplayItem();
  const DisplayItem* LastDisplayItem(unsigned offset);

  void BeginSkippingCache() { ++skipping_cache_count_; }
  void EndSkippingCache() {
    DCHECK(skipping_cache_count_ > 0);
    --skipping_cache_count_;
  }
  bool IsSkippingCache() const { return skipping_cache_count_; }

  // Must be called when a painting is finished. |offsetFromLayoutObject| is the
  // offset between the space of the GraphicsLayer which owns this
  // PaintController and the coordinate space of the owning LayoutObject.
  void CommitNewDisplayItems(
      const LayoutSize& offset_from_layout_object = LayoutSize());

  // Returns the approximate memory usage, excluding memory likely to be
  // shared with the embedder after copying to WebPaintController.
  // Should only be called right after commitNewDisplayItems.
  size_t ApproximateUnsharedMemoryUsage() const;

  // Get the artifact generated after the last commit.
  const PaintArtifact& GetPaintArtifact() const;
  const DisplayItemList& GetDisplayItemList() const {
    return GetPaintArtifact().GetDisplayItemList();
  }
  const Vector<PaintChunk>& PaintChunks() const {
    return GetPaintArtifact().PaintChunks();
  }

  bool ClientCacheIsValid(const DisplayItemClient&) const;
  bool CacheIsEmpty() const { return current_paint_artifact_.IsEmpty(); }

  // For micro benchmarking of record time.
  bool DisplayItemConstructionIsDisabled() const {
    return construction_disabled_;
  }
  void SetDisplayItemConstructionIsDisabled(const bool disable) {
    construction_disabled_ = disable;
  }
  bool SubsequenceCachingIsDisabled() const {
    return subsequence_caching_disabled_;
  }
  void SetSubsequenceCachingIsDisabled(bool disable) {
    subsequence_caching_disabled_ = disable;
  }

  void SetFirstPainted();
  void SetTextPainted();
  void SetImagePainted();

  // Returns displayItemList added using createAndAppend() since beginning or
  // the last commitNewDisplayItems(). Use with care.
  DisplayItemList& NewDisplayItemList() { return new_display_item_list_; }

  void AppendDebugDrawingAfterCommit(
      const DisplayItemClient&,
      sk_sp<PaintRecord>,
      const LayoutSize& offset_from_layout_object);

  void ShowDebugData() const { ShowDebugDataInternal(false); }
#ifndef NDEBUG
  void ShowDebugDataWithRecords() const { ShowDebugDataInternal(true); }
#endif

#if DCHECK_IS_ON()
  void AssertDisplayItemClientsAreLive();

  enum Usage { kForNormalUsage, kForPaintRecordBuilder };
  void SetUsage(Usage usage) { usage_ = usage; }
  bool IsForPaintRecordBuilder() const {
    return usage_ == kForPaintRecordBuilder;
  }
#endif

  void SetTracksRasterInvalidations(bool value);
  RasterInvalidationTrackingMap<const PaintChunk>*
  PaintChunksRasterInvalidationTrackingMap() {
    return raster_invalidation_tracking_info_
               ? &raster_invalidation_tracking_info_->map
               : nullptr;
  }

#if CHECK_DISPLAY_ITEM_CLIENT_ALIVENESS
  void BeginShouldKeepAlive(const DisplayItemClient&);

  void BeginSubsequence(const DisplayItemClient& client) {
    current_subsequence_clients_.push_back(&client);
    BeginShouldKeepAlive(client);
  }

  void EndSubsequence() { current_subsequence_clients_.pop_back(); }
#endif

  bool LastDisplayItemIsSubsequenceEnd() const;

  void BeginFrame(const void* frame);
  FrameFirstPaint EndFrame(const void* frame);

 protected:
  PaintController()
      : new_display_item_list_(0),
        construction_disabled_(false),
        subsequence_caching_disabled_(false),
        skipping_cache_count_(0),
        num_cached_new_items_(0),
        current_cached_subsequence_begin_index_in_new_list_(kNotFound),
#ifndef NDEBUG
        num_sequential_matches_(0),
        num_out_of_order_matches_(0),
        num_indexed_items_(0),
#endif
        under_invalidation_checking_begin_(0),
        under_invalidation_checking_end_(0),
        last_cached_subsequence_end_(0) {
    ResetCurrentListIndices();
    SetTracksRasterInvalidations(
        RuntimeEnabledFeatures::paintUnderInvalidationCheckingEnabled());

    // frame_first_paints_ should have one null frame since the beginning, so
    // that PaintController is robust even if it paints outside of BeginFrame
    // and EndFrame cycles. It will also enable us to combine the first paint
    // data in this PaintController into another PaintController on which we
    // replay the recorded results in the future.
    frame_first_paints_.push_back(FrameFirstPaint(nullptr));
  }

 private:
  friend class PaintControllerTestBase;
  friend class PaintControllerPaintTestBase;

  bool LastDisplayItemIsNoopBegin() const;

  void EnsureNewDisplayItemListInitialCapacity() {
    if (new_display_item_list_.IsEmpty()) {
      // TODO(wangxianzhu): Consider revisiting this heuristic.
      new_display_item_list_ =
          DisplayItemList(current_paint_artifact_.GetDisplayItemList().IsEmpty()
                              ? kInitialDisplayItemListCapacityBytes
                              : current_paint_artifact_.GetDisplayItemList()
                                    .UsedCapacityInBytes());
    }
  }

  // Set new item state (cache skipping, etc) for a new item.
  void ProcessNewItem(DisplayItem&);
  DisplayItem& MoveItemFromCurrentListToNewList(size_t);

  void ShowDebugDataInternal(bool show_paint_records) const;
  String DisplayItemListAsDebugString(const DisplayItemList&,
                                      bool show_paint_records) const;

  // Maps clients to indices of display items or chunks of each client.
  using IndicesByClientMap = HashMap<const DisplayItemClient*, Vector<size_t>>;

  static size_t FindMatchingItemFromIndex(const DisplayItem::Id&,
                                          const IndicesByClientMap&,
                                          const DisplayItemList&);
  static void AddItemToIndexIfNeeded(const DisplayItem&,
                                     size_t index,
                                     IndicesByClientMap&);

  size_t FindCachedItem(const DisplayItem::Id&);
  size_t FindOutOfOrderCachedItemForward(const DisplayItem::Id&);
  void CopyCachedSubsequence(size_t begin_index, size_t end_index);

  // Resets the indices (e.g. m_nextItemToMatch) of
  // m_currentPaintArtifact.getDisplayItemList() to their initial values. This
  // should be called when the DisplayItemList in m_currentPaintArtifact is
  // newly created, or is changed causing the previous indices to be invalid.
  void ResetCurrentListIndices();

  void GenerateRasterInvalidations(PaintChunk& new_chunk);
  void GenerateRasterInvalidationsComparingChunks(PaintChunk& new_chunk,
                                                  const PaintChunk& old_chunk);
  inline void AddRasterInvalidation(const DisplayItemClient&,
                                    PaintChunk&,
                                    const FloatRect&);
  void TrackRasterInvalidation(const DisplayItemClient&,
                               PaintChunk&,
                               const FloatRect&);

  // The following two methods are for checking under-invalidations
  // (when RuntimeEnabledFeatures::paintUnderInvalidationCheckingEnabled).
  void ShowUnderInvalidationError(const char* reason,
                                  const DisplayItem& new_item,
                                  const DisplayItem* old_item) const;

  void ShowSequenceUnderInvalidationError(const char* reason,
                                          const DisplayItemClient&,
                                          int start,
                                          int end);

  void CheckUnderInvalidation();
  bool IsCheckingUnderInvalidation() const {
    return under_invalidation_checking_end_ -
               under_invalidation_checking_begin_ >
           0;
  }

  struct SubsequenceMarkers {
    SubsequenceMarkers() : start(0), end(0) {}
    SubsequenceMarkers(size_t start_arg, size_t end_arg)
        : start(start_arg), end(end_arg) {}
    // The start and end index within m_currentPaintArtifact of this
    // subsequence.
    size_t start;
    size_t end;
  };

  SubsequenceMarkers* GetSubsequenceMarkers(const DisplayItemClient&);

  // The last complete paint artifact.
  // In SPv2, this includes paint chunks as well as display items.
  PaintArtifact current_paint_artifact_;

  // Data being used to build the next paint artifact.
  DisplayItemList new_display_item_list_;
  PaintChunker new_paint_chunks_;

  // Stores indices into m_newDisplayItemList for display items that have been
  // moved from m_currentPaintArtifact.getDisplayItemList(), indexed by the
  // positions of the display items before the move. The values are undefined
  // for display items that are not moved.
  Vector<size_t> items_moved_into_new_list_;

  // Allows display item construction to be disabled to isolate the costs of
  // construction in performance metrics.
  bool construction_disabled_;

  // Allows subsequence caching to be disabled to test the cost of display item
  // caching.
  bool subsequence_caching_disabled_;

  // A stack recording current frames' first paints.
  Vector<FrameFirstPaint> frame_first_paints_;

  int skipping_cache_count_;

  int num_cached_new_items_;

  // Stores indices to valid cacheable display items in
  // m_currentPaintArtifact.displayItemList() that have not been matched by
  // requests of cached display items (using useCachedDrawingIfPossible() and
  // useCachedSubsequenceIfPossible()) during sequential matching. The indexed
  // items will be matched by later out-of-order requests of cached display
  // items. This ensures that when out-of-order cached display items are
  // requested, we only traverse at most once over the current display list
  // looking for potential matches. Thus we can ensure that the algorithm runs
  // in linear time.
  IndicesByClientMap out_of_order_item_indices_;

  // The next item in the current list for sequential match.
  size_t next_item_to_match_;

  // The next item in the current list to be indexed for out-of-order cache
  // requests.
  size_t next_item_to_index_;

  // Similar to m_outOfOrderItemIndices but
  // - the indices are chunk indices in m_currentPaintArtifacts.paintChunks();
  // - chunks are matched not only for requests of cached display items, but
  //   also non-cached display items.
  IndicesByClientMap out_of_order_chunk_indices_;

  size_t current_cached_subsequence_begin_index_in_new_list_;
  size_t next_chunk_to_match_;

  DisplayItemClient::CacheGenerationOrInvalidationReason
      current_cache_generation_;

#ifndef NDEBUG
  int num_sequential_matches_;
  int num_out_of_order_matches_;
  int num_indexed_items_;
#endif

#if DCHECK_IS_ON()
  // This is used to check duplicated ids during createAndAppend().
  IndicesByClientMap new_display_item_indices_by_client_;

  Usage usage_ = kForNormalUsage;
#endif

  // These are set in useCachedDrawingIfPossible() and
  // useCachedSubsequenceIfPossible() when we could use cached drawing or
  // subsequence and under-invalidation checking is on, indicating the begin and
  // end of the cached drawing or subsequence in the current list. The functions
  // return false to let the client do actual painting, and PaintController will
  // check if the actual painting results are the same as the cached.
  size_t under_invalidation_checking_begin_;
  size_t under_invalidation_checking_end_;

  // Number of probable under-invalidations that have been skipped temporarily
  // because the mismatching display items may be removed in the future because
  // of no-op pairs or compositing folding.
  int skipped_probable_under_invalidation_count_;
  String under_invalidation_message_prefix_;

  struct RasterInvalidationTrackingInfo {
    RasterInvalidationTrackingMap<const PaintChunk> map;
    using ClientDebugNamesMap = HashMap<const DisplayItemClient*, String>;
    ClientDebugNamesMap new_client_debug_names;
    ClientDebugNamesMap old_client_debug_names;
  };
  std::unique_ptr<RasterInvalidationTrackingInfo>
      raster_invalidation_tracking_info_;

#if CHECK_DISPLAY_ITEM_CLIENT_ALIVENESS
  // A stack recording subsequence clients that are currently painting.
  Vector<const DisplayItemClient*> current_subsequence_clients_;
#endif

  typedef HashMap<const DisplayItemClient*, SubsequenceMarkers>
      CachedSubsequenceMap;
  CachedSubsequenceMap current_cached_subsequences_;
  CachedSubsequenceMap new_cached_subsequences_;
  size_t last_cached_subsequence_end_;

  FRIEND_TEST_ALL_PREFIXES(PaintControllerTest, CachedSubsequenceSwapOrder);
  FRIEND_TEST_ALL_PREFIXES(PaintControllerTest, CachedNestedSubsequenceUpdate);
};

}  // namespace blink

#endif  // PaintController_h
