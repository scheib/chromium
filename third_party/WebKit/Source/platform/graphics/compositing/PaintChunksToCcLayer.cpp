// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "platform/graphics/compositing/PaintChunksToCcLayer.h"

#include "cc/paint/compositing_display_item.h"
#include "cc/paint/display_item_list.h"
#include "cc/paint/drawing_display_item.h"
#include "cc/paint/filter_display_item.h"
#include "cc/paint/float_clip_display_item.h"
#include "cc/paint/transform_display_item.h"
#include "platform/graphics/GraphicsContext.h"
#include "platform/graphics/paint/DisplayItemList.h"
#include "platform/graphics/paint/DrawingDisplayItem.h"
#include "platform/graphics/paint/GeometryMapper.h"
#include "platform/graphics/paint/PaintChunk.h"
#include "platform/graphics/paint/PropertyTreeState.h"

namespace blink {

namespace {

enum EndDisplayItemType { kEndTransform, kEndClip, kEndEffect };

// Applies the clips between |localState| and |ancestorState| into a single
// combined cc::FloatClipDisplayItem on |ccList|.
static void ApplyClipsBetweenStates(
    const PropertyTreeState& local_state,
    const PropertyTreeState& ancestor_state,
    cc::DisplayItemList& cc_list,
    Vector<EndDisplayItemType>& end_display_items) {
  DCHECK(local_state.Transform() == ancestor_state.Transform());
#if DCHECK_IS_ON()
  const TransformPaintPropertyNode* transform_node =
      local_state.Clip()->LocalTransformSpace();
  if (transform_node != ancestor_state.Transform()) {
    const TransformationMatrix& local_to_ancestor_matrix =
        GeometryMapper::LocalToAncestorMatrix(transform_node,
                                              ancestor_state.Transform());
    // Clips are only in descendant spaces that are transformed by one
    // or more scrolls.
    DCHECK(local_to_ancestor_matrix.IsIdentityOrTranslation());
  }
#endif

  const FloatClipRect& combined_clip =
      GeometryMapper::LocalToAncestorClipRect(local_state, ancestor_state);

  cc_list.CreateAndAppendPairedBeginItem<cc::FloatClipDisplayItem>(
      gfx::RectF(combined_clip.Rect()));
  end_display_items.push_back(kEndClip);
}

static void RecordPairedBeginDisplayItems(
    const Vector<PropertyTreeState>& paired_states,
    const PropertyTreeState& pending_layer_state,
    cc::DisplayItemList& cc_list,
    Vector<EndDisplayItemType>& end_display_items) {
  PropertyTreeState mapped_clip_destination_space = pending_layer_state;
  PropertyTreeState clip_space = pending_layer_state;
  bool has_clip = false;

  for (Vector<PropertyTreeState>::const_reverse_iterator paired_state =
           paired_states.rbegin();
       paired_state != paired_states.rend(); ++paired_state) {
    switch (paired_state->GetInnermostNode()) {
      case PropertyTreeState::kTransform: {
        if (has_clip) {
          ApplyClipsBetweenStates(clip_space, mapped_clip_destination_space,
                                  cc_list, end_display_items);
          has_clip = false;
        }
        mapped_clip_destination_space = *paired_state;
        clip_space = *paired_state;

        TransformationMatrix matrix = paired_state->Transform()->Matrix();
        matrix.ApplyTransformOrigin(paired_state->Transform()->Origin());

        gfx::Transform transform(gfx::Transform::kSkipInitialization);
        transform.matrix() = TransformationMatrix::ToSkMatrix44(matrix);

        cc_list.CreateAndAppendPairedBeginItem<cc::TransformDisplayItem>(
            transform);
        end_display_items.push_back(kEndTransform);
        break;
      }
      case PropertyTreeState::kClip: {
        // Clips are handled in |applyClips| when ending the iterator, or
        // transitioning between transform spaces. Here we store off the
        // PropertyTreeState of the first found clip, under the transform of
        // pairedState->transform(). All subsequent clips before applying the
        // transform will be applied in applyClips.
        clip_space = *paired_state;
        has_clip = true;
#if DCHECK_IS_ON()
        if (paired_state->Clip()->LocalTransformSpace() !=
            paired_state->Transform()) {
          const TransformationMatrix& local_transform_matrix =
              paired_state->Effect()->LocalTransformSpace()->Matrix();
          // Clips are only in descendant spaces that are transformed by scroll.
          DCHECK(local_transform_matrix.IsIdentityOrTranslation());
        }
#endif
        break;
      }
      case PropertyTreeState::kEffect: {
        // TODO(chrishtr): skip effect and/or compositing display items if
        // not necessary.

        FloatRect clip_rect =
            paired_state->Effect()->OutputClip()->ClipRect().Rect();
        // TODO(chrishtr): specify origin of the filter.
        FloatPoint filter_origin;
        if (paired_state->Effect()->LocalTransformSpace() !=
            paired_state->Transform()) {
          const TransformPaintPropertyNode* transform_node =
              paired_state->Effect()->LocalTransformSpace();
          const TransformationMatrix& local_to_ancestor_matrix =
              GeometryMapper::LocalToAncestorMatrix(transform_node,
                                                    paired_state->Transform());
          // Effects are only in descendant spaces that are transformed by one
          // or more scrolls.
          DCHECK(local_to_ancestor_matrix.IsIdentityOrTranslation());

          clip_rect = local_to_ancestor_matrix.MapRect(clip_rect);
          filter_origin = local_to_ancestor_matrix.MapPoint(filter_origin);
        }

        const bool kLcdTextRequiresOpaqueLayer = true;
        cc_list.CreateAndAppendPairedBeginItem<cc::CompositingDisplayItem>(
            static_cast<uint8_t>(
                gfx::ToFlooredInt(255 * paired_state->Effect()->Opacity())),
            paired_state->Effect()->BlendMode(),
            // TODO(chrishtr): compute bounds as necessary.
            nullptr,
            GraphicsContext::WebCoreColorFilterToSkiaColorFilter(
                paired_state->Effect()->GetColorFilter()),
            kLcdTextRequiresOpaqueLayer);

        cc_list.CreateAndAppendPairedBeginItem<cc::FilterDisplayItem>(
            paired_state->Effect()->Filter().AsCcFilterOperations(), clip_rect,
            gfx::PointF(filter_origin.X(), filter_origin.Y()));

        end_display_items.push_back(kEndEffect);
        break;
      }
      case PropertyTreeState::kNone:
        break;
    }
  }

  if (has_clip) {
    ApplyClipsBetweenStates(clip_space, mapped_clip_destination_space, cc_list,
                            end_display_items);
  }
}

static void RecordPairedEndDisplayItems(
    const Vector<EndDisplayItemType>& end_display_item_types,
    cc::DisplayItemList* cc_list) {
  for (Vector<EndDisplayItemType>::const_reverse_iterator end_type =
           end_display_item_types.rbegin();
       end_type != end_display_item_types.rend(); ++end_type) {
    switch (*end_type) {
      case kEndTransform:
        cc_list->CreateAndAppendPairedEndItem<cc::EndTransformDisplayItem>();
        break;
      case kEndClip:
        cc_list->CreateAndAppendPairedEndItem<cc::EndFloatClipDisplayItem>();
        break;
      case kEndEffect:
        cc_list->CreateAndAppendPairedEndItem<cc::EndFilterDisplayItem>();
        cc_list->CreateAndAppendPairedEndItem<cc::EndCompositingDisplayItem>();
        break;
    }
  }
}

static gfx::Rect g_large_rect(-200000, -200000, 400000, 400000);
static void AppendDisplayItemToCcDisplayItemList(
    const DisplayItem& display_item,
    cc::DisplayItemList* list) {
  DCHECK(DisplayItem::IsDrawingType(display_item.GetType()));
  if (DisplayItem::IsDrawingType(display_item.GetType())) {
    sk_sp<const PaintRecord> record =
        static_cast<const DrawingDisplayItem&>(display_item).GetPaintRecord();
    if (!record)
      return;
    // In theory we would pass the bounds of the record, previously done as:
    // gfx::Rect bounds = gfx::SkIRectToRect(record->cullRect().roundOut());
    // or use the visual rect directly. However, clip content layers attempt
    // to raster in a different space than that of the visual rects. We'll be
    // reworking visual rects further for SPv2, so for now we just pass a
    // visual rect large enough to make sure items raster.
    list->CreateAndAppendDrawingItem<cc::DrawingDisplayItem>(g_large_rect,
                                                             std::move(record));
  }
}

}  // unnamed namespace

scoped_refptr<cc::DisplayItemList> PaintChunksToCcLayer::Convert(
    const Vector<const PaintChunk*>& paint_chunks,
    const PropertyTreeState& layer_state,
    const gfx::Vector2dF& layer_offset,
    const DisplayItemList& display_items) {
  auto cc_list = make_scoped_refptr(new cc::DisplayItemList);

  gfx::Transform counter_offset;
  counter_offset.Translate(-layer_offset.x(), -layer_offset.y());
  cc_list->CreateAndAppendPairedBeginItem<cc::TransformDisplayItem>(
      counter_offset);

  for (const auto* paint_chunk : paint_chunks) {
    const PropertyTreeState* state =
        &paint_chunk->properties.property_tree_state;
    PropertyTreeStateIterator iterator(*state);
    Vector<PropertyTreeState> paired_states;
    for (; state && *state != layer_state; state = iterator.Next()) {
      if (state->GetInnermostNode() != PropertyTreeState::kNone)
        paired_states.push_back(*state);
    }

    // TODO(chrishtr): we can avoid some extra paired display items if
    // multiple PaintChunks share them. We can also collapse clips between
    // transforms into single clips in the same way that PaintLayerClipper does.
    Vector<EndDisplayItemType> end_display_items;

    RecordPairedBeginDisplayItems(paired_states, layer_state, *cc_list.get(),
                                  end_display_items);

    for (const auto& display_item :
         display_items.ItemsInPaintChunk(*paint_chunk))
      AppendDisplayItemToCcDisplayItemList(display_item, cc_list.get());

    RecordPairedEndDisplayItems(end_display_items, cc_list.get());
  }

  cc_list->CreateAndAppendPairedEndItem<cc::EndTransformDisplayItem>();

  cc_list->Finalize();
  return cc_list;
}

}  // namespace blink
