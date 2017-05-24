// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/paint/SVGFilterPainter.h"

#include "core/layout/svg/LayoutSVGResourceFilter.h"
#include "core/paint/FilterEffectBuilder.h"
#include "core/paint/LayoutObjectDrawingRecorder.h"
#include "core/svg/SVGFilterElement.h"
#include "core/svg/graphics/filters/SVGFilterBuilder.h"
#include "platform/graphics/filters/Filter.h"
#include "platform/graphics/filters/SkiaImageFilterBuilder.h"
#include "platform/graphics/filters/SourceGraphic.h"
#include "platform/wtf/PtrUtil.h"

namespace blink {

GraphicsContext* SVGFilterRecordingContext::BeginContent() {
  // Create a new context so the contents of the filter can be drawn and cached.
  paint_controller_ = PaintController::Create();
  context_ = WTF::WrapUnique(new GraphicsContext(*paint_controller_));

  // Content painted into a new PaintRecord in SPv2 will have an
  // independent property tree set.
  if (RuntimeEnabledFeatures::slimmingPaintV2Enabled()) {
    paint_controller_->UpdateCurrentPaintChunkProperties(
        nullptr, PropertyTreeState::Root());
  }
  return context_.get();
}

sk_sp<PaintRecord> SVGFilterRecordingContext::EndContent(
    const FloatRect& bounds) {
  // Use the context that contains the filtered content.
  DCHECK(paint_controller_);
  DCHECK(context_);
  context_->BeginRecording(bounds);
  paint_controller_->CommitNewDisplayItems();

  paint_controller_->GetPaintArtifact().Replay(bounds, *context_);

  sk_sp<PaintRecord> content = context_->EndRecording();
  // Content is cached by the source graphic so temporaries can be freed.
  paint_controller_ = nullptr;
  context_ = nullptr;
  return content;
}

void SVGFilterRecordingContext::Abort() {
  if (!paint_controller_)
    return;
  EndContent(FloatRect());
}

static void PaintFilteredContent(GraphicsContext& context,
                                 const LayoutObject& object,
                                 const FloatRect& bounds,
                                 FilterEffect* effect) {
  if (LayoutObjectDrawingRecorder::UseCachedDrawingIfPossible(
          context, object, DisplayItem::kSVGFilter))
    return;

  LayoutObjectDrawingRecorder recorder(context, object, DisplayItem::kSVGFilter,
                                       bounds);
  sk_sp<SkImageFilter> image_filter =
      SkiaImageFilterBuilder::Build(effect, kInterpolationSpaceSRGB);
  context.Save();

  // Clip drawing of filtered image to the minimum required paint rect.
  context.ClipRect(effect->MapRect(object.StrokeBoundingBox()));

  context.BeginLayer(1, SkBlendMode::kSrcOver, &bounds, kColorFilterNone,
                     std::move(image_filter));
  context.EndLayer();
  context.Restore();
}

GraphicsContext* SVGFilterPainter::PrepareEffect(
    const LayoutObject& object,
    SVGFilterRecordingContext& recording_context) {
  filter_.ClearInvalidationMask();

  if (FilterData* filter_data = filter_.GetFilterDataForLayoutObject(&object)) {
    // If the filterData already exists we do not need to record the content
    // to be filtered. This can occur if the content was previously recorded
    // or we are in a cycle.
    if (filter_data->state_ == FilterData::kPaintingFilter)
      filter_data->state_ = FilterData::kPaintingFilterCycleDetected;

    if (filter_data->state_ == FilterData::kRecordingContent)
      filter_data->state_ = FilterData::kRecordingContentCycleDetected;

    return nullptr;
  }

  SVGFilterGraphNodeMap* node_map = SVGFilterGraphNodeMap::Create();
  FilterEffectBuilder builder(nullptr, object.ObjectBoundingBox(), 1);
  Filter* filter = builder.BuildReferenceFilter(
      toSVGFilterElement(*filter_.GetElement()), nullptr, node_map);
  if (!filter || !filter->LastEffect())
    return nullptr;

  IntRect source_region = EnclosingIntRect(
      Intersection(filter->FilterRegion(), object.StrokeBoundingBox()));
  filter->GetSourceGraphic()->SetSourceRect(source_region);

  FilterData* filter_data = FilterData::Create();
  filter_data->last_effect = filter->LastEffect();
  filter_data->node_map = node_map;
  DCHECK_EQ(filter_data->state_, FilterData::kInitial);

  // TODO(pdr): Can this be moved out of painter?
  filter_.SetFilterDataForLayoutObject(const_cast<LayoutObject*>(&object),
                                       filter_data);
  filter_data->state_ = FilterData::kRecordingContent;
  return recording_context.BeginContent();
}

void SVGFilterPainter::FinishEffect(
    const LayoutObject& object,
    SVGFilterRecordingContext& recording_context) {
  FilterData* filter_data = filter_.GetFilterDataForLayoutObject(&object);
  if (!filter_data) {
    // Our state was torn down while we were being painted (selection style for
    // <text> can have this effect), or it was never created (invalid filter.)
    // In the former case we may have been in the process of recording content,
    // so make sure we put recording state into a consistent state.
    recording_context.Abort();
    return;
  }

  // A painting cycle can occur when an FeImage references a source that
  // makes use of the FeImage itself. This is the first place we would hit
  // the cycle so we reset the state and continue.
  if (filter_data->state_ == FilterData::kPaintingFilterCycleDetected) {
    filter_data->state_ = FilterData::kPaintingFilter;
    return;
  }
  if (filter_data->state_ == FilterData::kRecordingContentCycleDetected) {
    filter_data->state_ = FilterData::kRecordingContent;
    return;
  }

  // Check for RecordingContent here because we may can be re-painting
  // without re-recording the contents to be filtered.
  Filter* filter = filter_data->last_effect->GetFilter();
  FloatRect bounds = filter->FilterRegion();
  if (filter_data->state_ == FilterData::kRecordingContent) {
    DCHECK(filter->GetSourceGraphic());
    sk_sp<PaintRecord> content = recording_context.EndContent(bounds);
    SkiaImageFilterBuilder::BuildSourceGraphic(filter->GetSourceGraphic(),
                                               std::move(content));
    filter_data->state_ = FilterData::kReadyToPaint;
  }

  DCHECK_EQ(filter_data->state_, FilterData::kReadyToPaint);
  filter_data->state_ = FilterData::kPaintingFilter;
  PaintFilteredContent(recording_context.PaintingContext(), object, bounds,
                       filter_data->last_effect);
  filter_data->state_ = FilterData::kReadyToPaint;
}

}  // namespace blink
