/*
 * Copyright (C) 2003, 2004, 2005, 2006, 2009 Apple Inc. All rights reserved.
 * Copyright (C) 2013 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "platform/graphics/GraphicsContext.h"

#include <memory>
#include "platform/geometry/FloatRect.h"
#include "platform/geometry/FloatRoundedRect.h"
#include "platform/geometry/IntRect.h"
#include "platform/graphics/GraphicsContextStateSaver.h"
#include "platform/graphics/ImageBuffer.h"
#include "platform/graphics/InterpolationSpace.h"
#include "platform/graphics/Path.h"
#include "platform/graphics/paint/PaintController.h"
#include "platform/graphics/paint/PaintRecord.h"
#include "platform/graphics/paint/PaintRecorder.h"
#include "platform/instrumentation/tracing/TraceEvent.h"
#include "platform/weborigin/KURL.h"
#include "platform/wtf/Assertions.h"
#include "platform/wtf/MathExtras.h"
#include "skia/ext/platform_canvas.h"
#include "third_party/skia/include/core/SkAnnotation.h"
#include "third_party/skia/include/core/SkColorFilter.h"
#include "third_party/skia/include/core/SkData.h"
#include "third_party/skia/include/core/SkRRect.h"
#include "third_party/skia/include/core/SkRefCnt.h"
#include "third_party/skia/include/effects/SkLumaColorFilter.h"
#include "third_party/skia/include/effects/SkPictureImageFilter.h"
#include "third_party/skia/include/pathops/SkPathOps.h"
#include "third_party/skia/include/utils/SkNullCanvas.h"

namespace blink {

GraphicsContext::GraphicsContext(PaintController& paint_controller,
                                 DisabledMode disable_context_or_painting,
                                 SkMetaData* meta_data)
    : canvas_(nullptr),
      paint_controller_(paint_controller),
      paint_state_stack_(),
      paint_state_index_(0),
#if DCHECK_IS_ON()
      layer_count_(0),
      disable_destruction_checks_(false),
      in_drawing_recorder_(false),
#endif
      disabled_state_(disable_context_or_painting),
      device_scale_factor_(1.0f),
      printing_(false),
      has_meta_data_(!!meta_data) {
  if (meta_data)
    meta_data_ = *meta_data;

  // FIXME: Do some tests to determine how many states are typically used, and
  // allocate several here.
  paint_state_stack_.push_back(GraphicsContextState::Create());
  paint_state_ = paint_state_stack_.back().get();

  if (ContextDisabled()) {
    DEFINE_STATIC_LOCAL(SkCanvas*, null_sk_canvas,
                        (SkMakeNullCanvas().release()));
    DEFINE_STATIC_LOCAL(SkiaPaintCanvas, null_canvas, (null_sk_canvas));
    canvas_ = &null_canvas;
  }
}

GraphicsContext::~GraphicsContext() {
#if DCHECK_IS_ON()
  if (!disable_destruction_checks_) {
    DCHECK(!paint_state_index_);
    DCHECK(!paint_state_->SaveCount());
    DCHECK(!layer_count_);
    DCHECK(!SaveCount());
  }
#endif
}

void GraphicsContext::Save() {
  if (ContextDisabled())
    return;

  paint_state_->IncrementSaveCount();

  DCHECK(canvas_);
  canvas_->save();
}

void GraphicsContext::Restore() {
  if (ContextDisabled())
    return;

  if (!paint_state_index_ && !paint_state_->SaveCount()) {
    DLOG(ERROR) << "ERROR void GraphicsContext::restore() stack is empty";
    return;
  }

  if (paint_state_->SaveCount()) {
    paint_state_->DecrementSaveCount();
  } else {
    paint_state_index_--;
    paint_state_ = paint_state_stack_[paint_state_index_].get();
  }

  DCHECK(canvas_);
  canvas_->restore();
}

#if DCHECK_IS_ON()
unsigned GraphicsContext::SaveCount() const {
  // Each m_paintStateStack entry implies an additional save op
  // (on top of its own saveCount), except for the first frame.
  unsigned count = paint_state_index_;
  DCHECK_GE(paint_state_stack_.size(), paint_state_index_);
  for (unsigned i = 0; i <= paint_state_index_; ++i)
    count += paint_state_stack_[i]->SaveCount();

  return count;
}
#endif

void GraphicsContext::SaveLayer(const SkRect* bounds, const PaintFlags* flags) {
  if (ContextDisabled())
    return;

  DCHECK(canvas_);
  canvas_->saveLayer(bounds, flags);
}

void GraphicsContext::RestoreLayer() {
  if (ContextDisabled())
    return;

  DCHECK(canvas_);

  canvas_->restore();
}

#if DCHECK_IS_ON()
void GraphicsContext::SetInDrawingRecorder(bool val) {
  // Nested drawing recorers are not allowed.
  DCHECK(!val || !in_drawing_recorder_);
  in_drawing_recorder_ = val;
}
#endif

void GraphicsContext::SetShadow(
    const FloatSize& offset,
    float blur,
    const Color& color,
    DrawLooperBuilder::ShadowTransformMode shadow_transform_mode,
    DrawLooperBuilder::ShadowAlphaMode shadow_alpha_mode,
    ShadowMode shadow_mode) {
  if (ContextDisabled())
    return;

  DrawLooperBuilder draw_looper_builder;
  if (!color.Alpha()) {
    // When shadow-only but there is no shadow, we use an empty draw looper
    // to disable rendering of the source primitive.  When not shadow-only, we
    // clear the looper.
    SetDrawLooper(shadow_mode != kDrawShadowOnly
                      ? nullptr
                      : draw_looper_builder.DetachDrawLooper());
    return;
  }

  draw_looper_builder.AddShadow(offset, blur, color, shadow_transform_mode,
                                shadow_alpha_mode);
  if (shadow_mode == kDrawShadowAndForeground) {
    draw_looper_builder.AddUnmodifiedContent();
  }
  SetDrawLooper(draw_looper_builder.DetachDrawLooper());
}

void GraphicsContext::SetDrawLooper(sk_sp<SkDrawLooper> draw_looper) {
  if (ContextDisabled())
    return;

  MutableState()->SetDrawLooper(std::move(draw_looper));
}

SkColorFilter* GraphicsContext::GetColorFilter() const {
  return ImmutableState()->GetColorFilter();
}

void GraphicsContext::SetColorFilter(ColorFilter color_filter) {
  GraphicsContextState* state_to_set = MutableState();

  // We only support one active color filter at the moment. If (when) this
  // becomes a problem, we should switch to using color filter chains (Skia work
  // in progress).
  DCHECK(!state_to_set->GetColorFilter());
  state_to_set->SetColorFilter(
      WebCoreColorFilterToSkiaColorFilter(color_filter));
}

void GraphicsContext::Concat(const SkMatrix& matrix) {
  if (ContextDisabled())
    return;

  DCHECK(canvas_);

  canvas_->concat(matrix);
}

void GraphicsContext::BeginLayer(float opacity,
                                 SkBlendMode xfermode,
                                 const FloatRect* bounds,
                                 ColorFilter color_filter,
                                 sk_sp<SkImageFilter> image_filter) {
  if (ContextDisabled())
    return;

  PaintFlags layer_flags;
  layer_flags.setAlpha(static_cast<unsigned char>(opacity * 255));
  layer_flags.setBlendMode(xfermode);
  layer_flags.setColorFilter(WebCoreColorFilterToSkiaColorFilter(color_filter));
  layer_flags.setImageFilter(std::move(image_filter));

  if (bounds) {
    SkRect sk_bounds = *bounds;
    SaveLayer(&sk_bounds, &layer_flags);
  } else {
    SaveLayer(nullptr, &layer_flags);
  }

#if DCHECK_IS_ON()
  ++layer_count_;
#endif
}

void GraphicsContext::EndLayer() {
  if (ContextDisabled())
    return;

  RestoreLayer();

#if DCHECK_IS_ON()
  DCHECK_GT(layer_count_--, 0);
#endif
}

void GraphicsContext::BeginRecording(const FloatRect& bounds) {
  if (ContextDisabled())
    return;

  DCHECK(!canvas_);
  canvas_ = paint_recorder_.beginRecording(bounds);
  if (has_meta_data_)
    canvas_->getMetaData() = meta_data_;
}

namespace {

sk_sp<PaintRecord> CreateEmptyPaintRecord() {
  PaintRecorder recorder;
  recorder.beginRecording(SkRect::MakeEmpty());
  return recorder.finishRecordingAsPicture();
}

}  // anonymous namespace

sk_sp<PaintRecord> GraphicsContext::EndRecording() {
  if (ContextDisabled()) {
    // Clients expect endRecording() to always return a non-null paint record.
    // Cache an empty one to minimize overhead when disabled.
    DEFINE_STATIC_LOCAL(sk_sp<PaintRecord>, empty_paint_record,
                        (CreateEmptyPaintRecord()));
    return empty_paint_record;
  }

  sk_sp<PaintRecord> record = paint_recorder_.finishRecordingAsPicture();
  canvas_ = nullptr;
  DCHECK(record);
  return record;
}

void GraphicsContext::DrawRecord(sk_sp<const PaintRecord> record) {
  if (ContextDisabled() || !record || record->cullRect().isEmpty())
    return;

  DCHECK(canvas_);
  canvas_->drawPicture(std::move(record));
}

void GraphicsContext::CompositeRecord(sk_sp<PaintRecord> record,
                                      const FloatRect& dest,
                                      const FloatRect& src,
                                      SkBlendMode op) {
  if (ContextDisabled() || !record)
    return;
  DCHECK(canvas_);

  PaintFlags flags;
  flags.setBlendMode(op);
  canvas_->save();
  SkRect source_bounds = src;
  SkRect sk_bounds = dest;
  SkMatrix transform;
  transform.setRectToRect(source_bounds, sk_bounds, SkMatrix::kFill_ScaleToFit);
  canvas_->concat(transform);
  flags.setImageFilter(SkPictureImageFilter::MakeForLocalSpace(
      ToSkPicture(record), source_bounds,
      static_cast<SkFilterQuality>(ImageInterpolationQuality())));
  canvas_->saveLayer(&source_bounds, &flags);
  canvas_->restore();
  canvas_->restore();
}

namespace {

int AdjustedFocusRingOffset(int offset) {
#if OS(MACOSX)
  return offset + 2;
#else
  return 0;
#endif
}

}  // anonymous ns

int GraphicsContext::FocusRingOutsetExtent(int offset, int width) {
  // Unlike normal outlines (whole width is outside of the offset), focus
  // rings are drawn with the center of the path aligned with the offset, so
  // only half of the width is outside of the offset.
  return AdjustedFocusRingOffset(offset) + (width + 1) / 2;
}

void GraphicsContext::DrawFocusRingPath(const SkPath& path,
                                        const Color& color,
                                        float width) {
  DrawPlatformFocusRing(path, canvas_, color.Rgb(), width);
}

void GraphicsContext::DrawFocusRingRect(const SkRect& rect,
                                        const Color& color,
                                        float width) {
  DrawPlatformFocusRing(rect, canvas_, color.Rgb(), width);
}

void GraphicsContext::DrawFocusRing(const Path& focus_ring_path,
                                    float width,
                                    int offset,
                                    const Color& color) {
  // FIXME: Implement support for offset.
  if (ContextDisabled())
    return;

  DrawFocusRingPath(focus_ring_path.GetSkPath(), color, width);
}

void GraphicsContext::DrawFocusRing(const Vector<IntRect>& rects,
                                    float width,
                                    int offset,
                                    const Color& color) {
  if (ContextDisabled())
    return;

  unsigned rect_count = rects.size();
  if (!rect_count)
    return;

  SkRegion focus_ring_region;
  offset = AdjustedFocusRingOffset(offset);
  for (unsigned i = 0; i < rect_count; i++) {
    SkIRect r = rects[i];
    if (r.isEmpty())
      continue;
    r.outset(offset, offset);
    focus_ring_region.op(r, SkRegion::kUnion_Op);
  }

  if (focus_ring_region.isEmpty())
    return;

  if (focus_ring_region.isRect()) {
    DrawFocusRingRect(SkRect::Make(focus_ring_region.getBounds()), color,
                      width);
  } else {
    SkPath path;
    if (focus_ring_region.getBoundaryPath(&path))
      DrawFocusRingPath(path, color, width);
  }
}

static inline FloatRect AreaCastingShadowInHole(
    const FloatRect& hole_rect,
    float shadow_blur,
    float shadow_spread,
    const FloatSize& shadow_offset) {
  FloatRect bounds(hole_rect);

  bounds.Inflate(shadow_blur);

  if (shadow_spread < 0)
    bounds.Inflate(-shadow_spread);

  FloatRect offset_bounds = bounds;
  offset_bounds.Move(-shadow_offset);
  return UnionRect(bounds, offset_bounds);
}

void GraphicsContext::DrawInnerShadow(const FloatRoundedRect& rect,
                                      const Color& shadow_color,
                                      const FloatSize& shadow_offset,
                                      float shadow_blur,
                                      float shadow_spread,
                                      Edges clipped_edges) {
  if (ContextDisabled())
    return;

  FloatRect hole_rect(rect.Rect());
  hole_rect.Inflate(-shadow_spread);

  if (hole_rect.IsEmpty()) {
    FillRoundedRect(rect, shadow_color);
    return;
  }

  if (clipped_edges & kLeftEdge) {
    hole_rect.Move(-std::max(shadow_offset.Width(), 0.0f) - shadow_blur, 0);
    hole_rect.SetWidth(hole_rect.Width() +
                       std::max(shadow_offset.Width(), 0.0f) + shadow_blur);
  }
  if (clipped_edges & kTopEdge) {
    hole_rect.Move(0, -std::max(shadow_offset.Height(), 0.0f) - shadow_blur);
    hole_rect.SetHeight(hole_rect.Height() +
                        std::max(shadow_offset.Height(), 0.0f) + shadow_blur);
  }
  if (clipped_edges & kRightEdge)
    hole_rect.SetWidth(hole_rect.Width() -
                       std::min(shadow_offset.Width(), 0.0f) + shadow_blur);
  if (clipped_edges & kBottomEdge)
    hole_rect.SetHeight(hole_rect.Height() -
                        std::min(shadow_offset.Height(), 0.0f) + shadow_blur);

  Color fill_color(shadow_color.Red(), shadow_color.Green(),
                   shadow_color.Blue(), 255);

  FloatRect outer_rect = AreaCastingShadowInHole(rect.Rect(), shadow_blur,
                                                 shadow_spread, shadow_offset);
  FloatRoundedRect rounded_hole(hole_rect, rect.GetRadii());

  GraphicsContextStateSaver state_saver(*this);
  if (rect.IsRounded()) {
    ClipRoundedRect(rect);
    if (shadow_spread < 0)
      rounded_hole.ExpandRadii(-shadow_spread);
    else
      rounded_hole.ShrinkRadii(shadow_spread);
  } else {
    Clip(rect.Rect());
  }

  DrawLooperBuilder draw_looper_builder;
  draw_looper_builder.AddShadow(FloatSize(shadow_offset), shadow_blur,
                                shadow_color,
                                DrawLooperBuilder::kShadowRespectsTransforms,
                                DrawLooperBuilder::kShadowIgnoresAlpha);
  SetDrawLooper(draw_looper_builder.DetachDrawLooper());
  FillRectWithRoundedHole(outer_rect, rounded_hole, fill_color);
}

void GraphicsContext::DrawLine(const IntPoint& point1, const IntPoint& point2) {
  if (ContextDisabled())
    return;
  DCHECK(canvas_);

  StrokeStyle pen_style = GetStrokeStyle();
  if (pen_style == kNoStroke)
    return;

  FloatPoint p1 = point1;
  FloatPoint p2 = point2;
  bool is_vertical_line = (p1.X() == p2.X());
  int width = roundf(StrokeThickness());

  // We know these are vertical or horizontal lines, so the length will just
  // be the sum of the displacement component vectors give or take 1 -
  // probably worth the speed up of no square root, which also won't be exact.
  FloatSize disp = p2 - p1;
  int length = SkScalarRoundToInt(disp.Width() + disp.Height());
  PaintFlags flags(ImmutableState()->StrokeFlags(length));

  if (StrokeData::StrokeIsDashed(width, GetStrokeStyle())) {
    // Do a rect fill of our endpoints.  This ensures we always have the
    // appearance of being a border. We then draw the actual dotted/dashed
    // line.
    SkRect r1, r2;
    r1.set(p1.X(), p1.Y(), p1.X() + width, p1.Y() + width);
    r2.set(p2.X(), p2.Y(), p2.X() + width, p2.Y() + width);

    if (is_vertical_line) {
      r1.offset(-width / 2, 0);
      r2.offset(-width / 2, -width);
    } else {
      r1.offset(0, -width / 2);
      r2.offset(-width, -width / 2);
    }
    PaintFlags fill_flags;
    fill_flags.setColor(flags.getColor());
    DrawRect(r1, fill_flags);
    DrawRect(r2, fill_flags);
  } else if (GetStrokeStyle() == kDottedStroke) {
    // We draw thick dotted lines with 0 length dash strokes and round endcaps,
    // producing circles. The endcaps extend beyond the line's endpoints,
    // so move the start and end in.
    if (is_vertical_line) {
      p1.SetY(p1.Y() + width / 2.f);
      p2.SetY(p2.Y() - width / 2.f);
    } else {
      p1.SetX(p1.X() + width / 2.f);
      p2.SetX(p2.X() - width / 2.f);
    }
  }

  AdjustLineToPixelBoundaries(p1, p2, width, pen_style);
  canvas_->drawLine(p1.X(), p1.Y(), p2.X(), p2.Y(), flags);
}

void GraphicsContext::DrawLineForText(const FloatPoint& pt, float width) {
  if (ContextDisabled())
    return;

  if (width <= 0)
    return;

  PaintFlags flags;
  switch (GetStrokeStyle()) {
    case kNoStroke:
    case kSolidStroke:
    case kDoubleStroke: {
      int thickness = SkMax32(static_cast<int>(StrokeThickness()), 1);
      SkRect r;
      r.fLeft = WebCoreFloatToSkScalar(pt.X());
      // Avoid anti-aliasing lines. Currently, these are always horizontal.
      // Round to nearest pixel to match text and other content.
      r.fTop = WebCoreFloatToSkScalar(floorf(pt.Y() + 0.5f));
      r.fRight = r.fLeft + WebCoreFloatToSkScalar(width);
      r.fBottom = r.fTop + SkIntToScalar(thickness);
      flags = ImmutableState()->FillFlags();
      // Text lines are drawn using the stroke color.
      flags.setColor(StrokeColor().Rgb());
      DrawRect(r, flags);
      return;
    }
    case kDottedStroke:
    case kDashedStroke: {
      int y = floorf(pt.Y() + std::max<float>(StrokeThickness() / 2.0f, 0.5f));
      DrawLine(IntPoint(pt.X(), y), IntPoint(pt.X() + width, y));
      return;
    }
    case kWavyStroke:
    default:
      break;
  }

  NOTREACHED();
}

// Draws a filled rectangle with a stroked border.
void GraphicsContext::DrawRect(const IntRect& rect) {
  if (ContextDisabled())
    return;

  DCHECK(!rect.IsEmpty());
  if (rect.IsEmpty())
    return;

  SkRect sk_rect = rect;
  if (ImmutableState()->FillColor().Alpha())
    DrawRect(sk_rect, ImmutableState()->FillFlags());

  if (ImmutableState()->GetStrokeData().Style() != kNoStroke &&
      ImmutableState()->StrokeColor().Alpha()) {
    // Stroke a width: 1 inset border
    PaintFlags flags(ImmutableState()->FillFlags());
    flags.setColor(StrokeColor().Rgb());
    flags.setStyle(PaintFlags::kStroke_Style);
    flags.setStrokeWidth(1);

    sk_rect.inset(0.5f, 0.5f);
    DrawRect(sk_rect, flags);
  }
}

template <typename TextPaintInfo>
void GraphicsContext::DrawTextInternal(const Font& font,
                                       const TextPaintInfo& text_info,
                                       const FloatPoint& point,
                                       const PaintFlags& flags) {
  if (ContextDisabled())
    return;

  if (font.DrawText(canvas_, text_info, point, device_scale_factor_, flags))
    paint_controller_.SetTextPainted();
}

void GraphicsContext::DrawText(const Font& font,
                               const TextRunPaintInfo& text_info,
                               const FloatPoint& point,
                               const PaintFlags& flags) {
  DrawTextInternal(font, text_info, point, flags);
}

void GraphicsContext::DrawText(const Font& font,
                               const TextFragmentPaintInfo& text_info,
                               const FloatPoint& point,
                               const PaintFlags& flags) {
  DrawTextInternal(font, text_info, point, flags);
}

template <typename DrawTextFunc>
void GraphicsContext::DrawTextPasses(const DrawTextFunc& draw_text) {
  TextDrawingModeFlags mode_flags = TextDrawingMode();

  if (mode_flags & kTextModeFill) {
    draw_text(ImmutableState()->FillFlags());
  }

  if ((mode_flags & kTextModeStroke) && GetStrokeStyle() != kNoStroke &&
      StrokeThickness() > 0) {
    PaintFlags stroke_flags(ImmutableState()->StrokeFlags());
    if (mode_flags & kTextModeFill) {
      // shadow was already applied during fill pass
      stroke_flags.setLooper(0);
    }
    draw_text(stroke_flags);
  }
}

template <typename TextPaintInfo>
void GraphicsContext::DrawTextInternal(const Font& font,
                                       const TextPaintInfo& text_info,
                                       const FloatPoint& point) {
  if (ContextDisabled())
    return;

  DrawTextPasses([&font, &text_info, &point, this](const PaintFlags& flags) {
    if (font.DrawText(canvas_, text_info, point, device_scale_factor_, flags))
      paint_controller_.SetTextPainted();
  });
}

void GraphicsContext::DrawText(const Font& font,
                               const TextRunPaintInfo& text_info,
                               const FloatPoint& point) {
  DrawTextInternal(font, text_info, point);
}

void GraphicsContext::DrawText(const Font& font,
                               const TextFragmentPaintInfo& text_info,
                               const FloatPoint& point) {
  DrawTextInternal(font, text_info, point);
}

template <typename TextPaintInfo>
void GraphicsContext::DrawEmphasisMarksInternal(const Font& font,
                                                const TextPaintInfo& text_info,
                                                const AtomicString& mark,
                                                const FloatPoint& point) {
  if (ContextDisabled())
    return;

  DrawTextPasses(
      [&font, &text_info, &mark, &point, this](const PaintFlags& flags) {
        font.DrawEmphasisMarks(canvas_, text_info, mark, point,
                               device_scale_factor_, flags);
      });
}

void GraphicsContext::DrawEmphasisMarks(const Font& font,
                                        const TextRunPaintInfo& text_info,
                                        const AtomicString& mark,
                                        const FloatPoint& point) {
  DrawEmphasisMarksInternal(font, text_info, mark, point);
}

void GraphicsContext::DrawEmphasisMarks(const Font& font,
                                        const TextFragmentPaintInfo& text_info,
                                        const AtomicString& mark,
                                        const FloatPoint& point) {
  DrawEmphasisMarksInternal(font, text_info, mark, point);
}

void GraphicsContext::DrawBidiText(
    const Font& font,
    const TextRunPaintInfo& run_info,
    const FloatPoint& point,
    Font::CustomFontNotReadyAction custom_font_not_ready_action) {
  if (ContextDisabled())
    return;

  DrawTextPasses([&font, &run_info, &point, custom_font_not_ready_action,
                  this](const PaintFlags& flags) {
    if (font.DrawBidiText(canvas_, run_info, point,
                          custom_font_not_ready_action, device_scale_factor_,
                          flags))
      paint_controller_.SetTextPainted();
  });
}

void GraphicsContext::DrawHighlightForText(const Font& font,
                                           const TextRun& run,
                                           const FloatPoint& point,
                                           int h,
                                           const Color& background_color,
                                           int from,
                                           int to) {
  if (ContextDisabled())
    return;

  FillRect(font.SelectionRectForText(run, point, h, from, to),
           background_color);
}

void GraphicsContext::DrawImage(
    Image* image,
    const FloatRect& dest,
    const FloatRect* src_ptr,
    SkBlendMode op,
    RespectImageOrientationEnum should_respect_image_orientation) {
  if (ContextDisabled() || !image)
    return;

  const FloatRect src = src_ptr ? *src_ptr : image->Rect();

  PaintFlags image_flags = ImmutableState()->FillFlags();
  image_flags.setBlendMode(op);
  image_flags.setColor(SK_ColorBLACK);
  image_flags.setFilterQuality(ComputeFilterQuality(image, dest, src));
  image_flags.setAntiAlias(ShouldAntialias());
  image->Draw(canvas_, image_flags, dest, src, should_respect_image_orientation,
              Image::kClampImageToSourceRect);
  paint_controller_.SetImagePainted();
}

void GraphicsContext::DrawImageRRect(
    Image* image,
    const FloatRoundedRect& dest,
    const FloatRect& src_rect,
    SkBlendMode op,
    RespectImageOrientationEnum respect_orientation) {
  if (ContextDisabled() || !image)
    return;

  if (!dest.IsRounded()) {
    DrawImage(image, dest.Rect(), &src_rect, op, respect_orientation);
    return;
  }

  DCHECK(dest.IsRenderable());

  const FloatRect visible_src = Intersection(src_rect, image->Rect());
  if (dest.IsEmpty() || visible_src.IsEmpty())
    return;

  PaintFlags image_flags = ImmutableState()->FillFlags();
  image_flags.setBlendMode(op);
  image_flags.setColor(SK_ColorBLACK);
  image_flags.setFilterQuality(
      ComputeFilterQuality(image, dest.Rect(), src_rect));
  image_flags.setAntiAlias(ShouldAntialias());

  bool use_shader = (visible_src == src_rect) &&
                    (respect_orientation == kDoNotRespectImageOrientation);
  if (use_shader) {
    const SkMatrix local_matrix = SkMatrix::MakeRectToRect(
        visible_src, dest.Rect(), SkMatrix::kFill_ScaleToFit);
    use_shader = image->ApplyShader(image_flags, local_matrix);
  }

  if (use_shader) {
    // Shader-based fast path.
    canvas_->drawRRect(dest, image_flags);
  } else {
    // Clip-based fallback.
    PaintCanvasAutoRestore auto_restore(canvas_, true);
    canvas_->clipRRect(dest, image_flags.isAntiAlias());
    image->Draw(canvas_, image_flags, dest.Rect(), src_rect,
                respect_orientation, Image::kClampImageToSourceRect);
  }

  paint_controller_.SetImagePainted();
}

SkFilterQuality GraphicsContext::ComputeFilterQuality(
    Image* image,
    const FloatRect& dest,
    const FloatRect& src) const {
  InterpolationQuality resampling;
  if (Printing()) {
    resampling = kInterpolationNone;
  } else if (image->CurrentFrameIsLazyDecoded()) {
    resampling = kInterpolationHigh;
  } else {
    resampling = ComputeInterpolationQuality(
        SkScalarToFloat(src.Width()), SkScalarToFloat(src.Height()),
        SkScalarToFloat(dest.Width()), SkScalarToFloat(dest.Height()),
        image->CurrentFrameIsComplete());

    if (resampling == kInterpolationNone) {
      // FIXME: This is to not break tests (it results in the filter bitmap flag
      // being set to true). We need to decide if we respect InterpolationNone
      // being returned from computeInterpolationQuality.
      resampling = kInterpolationLow;
    }
  }
  return static_cast<SkFilterQuality>(
      LimitInterpolationQuality(*this, resampling));
}

void GraphicsContext::DrawTiledImage(Image* image,
                                     const FloatRect& dest_rect,
                                     const FloatPoint& src_point,
                                     const FloatSize& tile_size,
                                     SkBlendMode op,
                                     const FloatSize& repeat_spacing) {
  if (ContextDisabled() || !image)
    return;
  image->DrawTiledBackground(*this, dest_rect, src_point, tile_size, op,
                             repeat_spacing);
  paint_controller_.SetImagePainted();
}

void GraphicsContext::DrawTiledImage(Image* image,
                                     const FloatRect& dest,
                                     const FloatRect& src_rect,
                                     const FloatSize& tile_scale_factor,
                                     Image::TileRule h_rule,
                                     Image::TileRule v_rule,
                                     SkBlendMode op) {
  if (ContextDisabled() || !image)
    return;

  if (h_rule == Image::kStretchTile && v_rule == Image::kStretchTile) {
    // Just do a scale.
    DrawImage(image, dest, &src_rect, op);
    return;
  }

  image->DrawTiledBorder(*this, dest, src_rect, tile_scale_factor, h_rule,
                         v_rule, op);
  paint_controller_.SetImagePainted();
}

void GraphicsContext::DrawOval(const SkRect& oval, const PaintFlags& flags) {
  if (ContextDisabled())
    return;
  DCHECK(canvas_);

  canvas_->drawOval(oval, flags);
}

void GraphicsContext::DrawPath(const SkPath& path, const PaintFlags& flags) {
  if (ContextDisabled())
    return;
  DCHECK(canvas_);

  canvas_->drawPath(path, flags);
}

void GraphicsContext::DrawRect(const SkRect& rect, const PaintFlags& flags) {
  if (ContextDisabled())
    return;
  DCHECK(canvas_);

  canvas_->drawRect(rect, flags);
}

void GraphicsContext::DrawRRect(const SkRRect& rrect, const PaintFlags& flags) {
  if (ContextDisabled())
    return;
  DCHECK(canvas_);

  canvas_->drawRRect(rrect, flags);
}

void GraphicsContext::FillPath(const Path& path_to_fill) {
  if (ContextDisabled() || path_to_fill.IsEmpty())
    return;

  DrawPath(path_to_fill.GetSkPath(), ImmutableState()->FillFlags());
}

void GraphicsContext::FillRect(const FloatRect& rect) {
  if (ContextDisabled())
    return;

  DrawRect(rect, ImmutableState()->FillFlags());
}

void GraphicsContext::FillRect(const FloatRect& rect,
                               const Color& color,
                               SkBlendMode xfer_mode) {
  if (ContextDisabled())
    return;

  PaintFlags flags = ImmutableState()->FillFlags();
  flags.setColor(color.Rgb());
  flags.setBlendMode(xfer_mode);

  DrawRect(rect, flags);
}

void GraphicsContext::FillRoundedRect(const FloatRoundedRect& rrect,
                                      const Color& color) {
  if (ContextDisabled())
    return;

  if (!rrect.IsRounded() || !rrect.IsRenderable()) {
    FillRect(rrect.Rect(), color);
    return;
  }

  if (color == FillColor()) {
    DrawRRect(rrect, ImmutableState()->FillFlags());
    return;
  }

  PaintFlags flags = ImmutableState()->FillFlags();
  flags.setColor(color.Rgb());

  DrawRRect(rrect, flags);
}

namespace {

bool IsSimpleDRRect(const FloatRoundedRect& outer,
                    const FloatRoundedRect& inner) {
  // A DRRect is "simple" (i.e. can be drawn as a rrect stroke) if
  //   1) all sides have the same width
  const FloatSize stroke_size =
      inner.Rect().MinXMinYCorner() - outer.Rect().MinXMinYCorner();
  if (!WebCoreFloatNearlyEqual(stroke_size.AspectRatio(), 1) ||
      !WebCoreFloatNearlyEqual(stroke_size.Width(),
                               outer.Rect().MaxX() - inner.Rect().MaxX()) ||
      !WebCoreFloatNearlyEqual(stroke_size.Height(),
                               outer.Rect().MaxY() - inner.Rect().MaxY()))
    return false;

  // and
  //   2) the inner radii are not constrained
  const FloatRoundedRect::Radii& o_radii = outer.GetRadii();
  const FloatRoundedRect::Radii& i_radii = inner.GetRadii();
  if (!WebCoreFloatNearlyEqual(o_radii.TopLeft().Width() - stroke_size.Width(),
                               i_radii.TopLeft().Width()) ||
      !WebCoreFloatNearlyEqual(
          o_radii.TopLeft().Height() - stroke_size.Height(),
          i_radii.TopLeft().Height()) ||
      !WebCoreFloatNearlyEqual(o_radii.TopRight().Width() - stroke_size.Width(),
                               i_radii.TopRight().Width()) ||
      !WebCoreFloatNearlyEqual(
          o_radii.TopRight().Height() - stroke_size.Height(),
          i_radii.TopRight().Height()) ||
      !WebCoreFloatNearlyEqual(
          o_radii.BottomRight().Width() - stroke_size.Width(),
          i_radii.BottomRight().Width()) ||
      !WebCoreFloatNearlyEqual(
          o_radii.BottomRight().Height() - stroke_size.Height(),
          i_radii.BottomRight().Height()) ||
      !WebCoreFloatNearlyEqual(
          o_radii.BottomLeft().Width() - stroke_size.Width(),
          i_radii.BottomLeft().Width()) ||
      !WebCoreFloatNearlyEqual(
          o_radii.BottomLeft().Height() - stroke_size.Height(),
          i_radii.BottomLeft().Height()))
    return false;

  // We also ignore DRRects with a very thick relative stroke (shapes which are
  // mostly filled by the stroke): Skia's stroke outline can diverge
  // significantly from the outer/inner contours in some edge cases, so we fall
  // back to drawDRRect() instead.
  const float kMaxStrokeToSizeRatio = 0.75f;
  if (2 * stroke_size.Width() / outer.Rect().Width() > kMaxStrokeToSizeRatio ||
      2 * stroke_size.Height() / outer.Rect().Height() > kMaxStrokeToSizeRatio)
    return false;

  return true;
}

}  // anonymous namespace

void GraphicsContext::FillDRRect(const FloatRoundedRect& outer,
                                 const FloatRoundedRect& inner,
                                 const Color& color) {
  if (ContextDisabled())
    return;
  DCHECK(canvas_);

  if (!IsSimpleDRRect(outer, inner)) {
    if (color == FillColor()) {
      canvas_->drawDRRect(outer, inner, ImmutableState()->FillFlags());
    } else {
      PaintFlags flags(ImmutableState()->FillFlags());
      flags.setColor(color.Rgb());
      canvas_->drawDRRect(outer, inner, flags);
    }

    return;
  }

  // We can draw this as a stroked rrect.
  float stroke_width = inner.Rect().X() - outer.Rect().X();
  SkRRect stroke_r_rect = outer;
  stroke_r_rect.inset(stroke_width / 2, stroke_width / 2);

  PaintFlags stroke_flags(ImmutableState()->FillFlags());
  stroke_flags.setColor(color.Rgb());
  stroke_flags.setStyle(PaintFlags::kStroke_Style);
  stroke_flags.setStrokeWidth(stroke_width);

  canvas_->drawRRect(stroke_r_rect, stroke_flags);
}

void GraphicsContext::FillEllipse(const FloatRect& ellipse) {
  if (ContextDisabled())
    return;

  DrawOval(ellipse, ImmutableState()->FillFlags());
}

void GraphicsContext::StrokePath(const Path& path_to_stroke) {
  if (ContextDisabled() || path_to_stroke.IsEmpty())
    return;

  DrawPath(path_to_stroke.GetSkPath(), ImmutableState()->StrokeFlags());
}

void GraphicsContext::StrokeRect(const FloatRect& rect, float line_width) {
  if (ContextDisabled())
    return;

  PaintFlags flags(ImmutableState()->StrokeFlags());
  flags.setStrokeWidth(WebCoreFloatToSkScalar(line_width));
  // Reset the dash effect to account for the width
  ImmutableState()->GetStrokeData().SetupPaintDashPathEffect(&flags, 0);
  // strokerect has special rules for CSS when the rect is degenerate:
  // if width==0 && height==0, do nothing
  // if width==0 || height==0, then just draw line for the other dimension
  SkRect r(rect);
  bool valid_w = r.width() > 0;
  bool valid_h = r.height() > 0;
  if (valid_w && valid_h) {
    DrawRect(r, flags);
  } else if (valid_w || valid_h) {
    // we are expected to respect the lineJoin, so we can't just call
    // drawLine -- we have to create a path that doubles back on itself.
    SkPath path;
    path.moveTo(r.fLeft, r.fTop);
    path.lineTo(r.fRight, r.fBottom);
    path.close();
    DrawPath(path, flags);
  }
}

void GraphicsContext::StrokeEllipse(const FloatRect& ellipse) {
  if (ContextDisabled())
    return;

  DrawOval(ellipse, ImmutableState()->StrokeFlags());
}

void GraphicsContext::ClipRoundedRect(const FloatRoundedRect& rrect,
                                      SkClipOp clip_op,
                                      AntiAliasingMode should_antialias) {
  if (ContextDisabled())
    return;

  if (!rrect.IsRounded()) {
    ClipRect(rrect.Rect(), should_antialias, clip_op);
    return;
  }

  ClipRRect(rrect, should_antialias, clip_op);
}

void GraphicsContext::ClipOut(const Path& path_to_clip) {
  if (ContextDisabled())
    return;

  // Use const_cast and temporarily toggle the inverse fill type instead of
  // copying the path.
  SkPath& path = const_cast<SkPath&>(path_to_clip.GetSkPath());
  path.toggleInverseFillType();
  ClipPath(path, kAntiAliased);
  path.toggleInverseFillType();
}

void GraphicsContext::ClipOutRoundedRect(const FloatRoundedRect& rect) {
  if (ContextDisabled())
    return;

  ClipRoundedRect(rect, SkClipOp::kDifference);
}

void GraphicsContext::ClipRect(const SkRect& rect,
                               AntiAliasingMode aa,
                               SkClipOp op) {
  if (ContextDisabled())
    return;
  DCHECK(canvas_);

  canvas_->clipRect(rect, op, aa == kAntiAliased);
}

void GraphicsContext::ClipPath(const SkPath& path,
                               AntiAliasingMode aa,
                               SkClipOp op) {
  if (ContextDisabled())
    return;
  DCHECK(canvas_);

  canvas_->clipPath(path, op, aa == kAntiAliased);
}

void GraphicsContext::ClipRRect(const SkRRect& rect,
                                AntiAliasingMode aa,
                                SkClipOp op) {
  if (ContextDisabled())
    return;
  DCHECK(canvas_);

  canvas_->clipRRect(rect, op, aa == kAntiAliased);
}

void GraphicsContext::Rotate(float angle_in_radians) {
  if (ContextDisabled())
    return;
  DCHECK(canvas_);

  canvas_->rotate(
      WebCoreFloatToSkScalar(angle_in_radians * (180.0f / 3.14159265f)));
}

void GraphicsContext::Translate(float x, float y) {
  if (ContextDisabled())
    return;
  DCHECK(canvas_);

  if (!x && !y)
    return;

  canvas_->translate(WebCoreFloatToSkScalar(x), WebCoreFloatToSkScalar(y));
}

void GraphicsContext::Scale(float x, float y) {
  if (ContextDisabled())
    return;
  DCHECK(canvas_);

  canvas_->scale(WebCoreFloatToSkScalar(x), WebCoreFloatToSkScalar(y));
}

void GraphicsContext::SetURLForRect(const KURL& link,
                                    const IntRect& dest_rect) {
  if (ContextDisabled())
    return;
  DCHECK(canvas_);

  sk_sp<SkData> url(SkData::MakeWithCString(link.GetString().Utf8().data()));
  canvas_->Annotate(PaintCanvas::AnnotationType::URL, dest_rect,
                    std::move(url));
}

void GraphicsContext::SetURLFragmentForRect(const String& dest_name,
                                            const IntRect& rect) {
  if (ContextDisabled())
    return;
  DCHECK(canvas_);

  sk_sp<SkData> sk_dest_name(SkData::MakeWithCString(dest_name.Utf8().data()));
  canvas_->Annotate(PaintCanvas::AnnotationType::LINK_TO_DESTINATION, rect,
                    std::move(sk_dest_name));
}

void GraphicsContext::SetURLDestinationLocation(const String& name,
                                                const IntPoint& location) {
  if (ContextDisabled())
    return;
  DCHECK(canvas_);

  SkRect rect = SkRect::MakeXYWH(location.X(), location.Y(), 0, 0);
  sk_sp<SkData> sk_name(SkData::MakeWithCString(name.Utf8().data()));
  canvas_->Annotate(PaintCanvas::AnnotationType::NAMED_DESTINATION, rect,
                    std::move(sk_name));
}

void GraphicsContext::ConcatCTM(const AffineTransform& affine) {
  Concat(AffineTransformToSkMatrix(affine));
}

void GraphicsContext::FillRectWithRoundedHole(
    const FloatRect& rect,
    const FloatRoundedRect& rounded_hole_rect,
    const Color& color) {
  if (ContextDisabled())
    return;

  PaintFlags flags(ImmutableState()->FillFlags());
  flags.setColor(color.Rgb());
  canvas_->drawDRRect(SkRRect::MakeRect(rect), rounded_hole_rect, flags);
}

void GraphicsContext::AdjustLineToPixelBoundaries(FloatPoint& p1,
                                                  FloatPoint& p2,
                                                  float stroke_width,
                                                  StrokeStyle pen_style) {
  // For odd widths, we add in 0.5 to the appropriate x/y so that the float
  // arithmetic works out.  For example, with a border width of 3, WebKit will
  // pass us (y1+y2)/2, e.g., (50+53)/2 = 103/2 = 51 when we want 51.5.  It is
  // always true that an even width gave us a perfect position, but an odd width
  // gave us a position that is off by exactly 0.5.
  if (StrokeData::StrokeIsDashed(stroke_width, pen_style)) {
    if (p1.X() == p2.X()) {
      p1.SetY(p1.Y() + stroke_width);
      p2.SetY(p2.Y() - stroke_width);
    } else {
      p1.SetX(p1.X() + stroke_width);
      p2.SetX(p2.X() - stroke_width);
    }
  }

  if (static_cast<int>(stroke_width) % 2) {  // odd
    if (p1.X() == p2.X()) {
      // We're a vertical line.  Adjust our x.
      p1.SetX(p1.X() + 0.5f);
      p2.SetX(p2.X() + 0.5f);
    } else {
      // We're a horizontal line. Adjust our y.
      p1.SetY(p1.Y() + 0.5f);
      p2.SetY(p2.Y() + 0.5f);
    }
  }
}

sk_sp<SkColorFilter> GraphicsContext::WebCoreColorFilterToSkiaColorFilter(
    ColorFilter color_filter) {
  switch (color_filter) {
    case kColorFilterLuminanceToAlpha:
      return SkLumaColorFilter::Make();
    case kColorFilterLinearRGBToSRGB:
      return InterpolationSpaceUtilities::CreateInterpolationSpaceFilter(
          kInterpolationSpaceLinear, kInterpolationSpaceSRGB);
    case kColorFilterSRGBToLinearRGB:
      return InterpolationSpaceUtilities::CreateInterpolationSpaceFilter(
          kInterpolationSpaceSRGB, kInterpolationSpaceLinear);
    case kColorFilterNone:
      break;
    default:
      NOTREACHED();
      break;
  }

  return nullptr;
}

}  // namespace blink
