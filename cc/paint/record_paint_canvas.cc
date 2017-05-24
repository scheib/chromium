// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cc/paint/record_paint_canvas.h"

#include "base/memory/ptr_util.h"
#include "cc/paint/display_item_list.h"
#include "cc/paint/paint_op_buffer.h"
#include "cc/paint/paint_record.h"
#include "cc/paint/paint_recorder.h"
#include "third_party/skia/include/core/SkAnnotation.h"
#include "third_party/skia/include/core/SkMetaData.h"
#include "third_party/skia/include/utils/SkNWayCanvas.h"

namespace cc {

RecordPaintCanvas::RecordPaintCanvas(PaintOpBuffer* buffer) : buffer_(buffer) {
  DCHECK(buffer_);
}

RecordPaintCanvas::~RecordPaintCanvas() = default;

SkMetaData& RecordPaintCanvas::getMetaData() {
  // This could just be SkMetaData owned by RecordPaintCanvas, but since
  // SkCanvas already has one, we might as well use it directly.
  return GetCanvas()->getMetaData();
}

SkImageInfo RecordPaintCanvas::imageInfo() const {
  return GetCanvas()->imageInfo();
}

void RecordPaintCanvas::flush() {
  // This is a noop when recording.
}

int RecordPaintCanvas::save() {
  buffer_->push<SaveOp>();
  return GetCanvas()->save();
}

int RecordPaintCanvas::saveLayer(const SkRect* bounds,
                                 const PaintFlags* flags) {
  if (flags) {
    if (flags->IsSimpleOpacity()) {
      // TODO(enne): maybe more callers should know this and call
      // saveLayerAlpha instead of needing to check here.
      uint8_t alpha = SkColorGetA(flags->getColor());
      return saveLayerAlpha(bounds, alpha);
    }

    // TODO(enne): it appears that image filters affect matrices and color
    // matrices affect transparent flags on SkCanvas layers, but it's not clear
    // whether those are actually needed and we could just skip ToSkPaint here.
    buffer_->push<SaveLayerOp>(bounds, flags);
    const SkPaint& paint = ToSkPaint(*flags);
    return GetCanvas()->saveLayer(bounds, &paint);
  }
  buffer_->push<SaveLayerOp>(bounds, flags);
  return GetCanvas()->saveLayer(bounds, nullptr);
}

int RecordPaintCanvas::saveLayerAlpha(const SkRect* bounds, uint8_t alpha) {
  buffer_->push<SaveLayerAlphaOp>(bounds, alpha);
  return GetCanvas()->saveLayerAlpha(bounds, alpha);
}

void RecordPaintCanvas::restore() {
  buffer_->push<RestoreOp>();
  GetCanvas()->restore();
}

int RecordPaintCanvas::getSaveCount() const {
  return GetCanvas()->getSaveCount();
}

void RecordPaintCanvas::restoreToCount(int save_count) {
  if (!canvas_) {
    DCHECK_EQ(save_count, 1);
    return;
  }

  DCHECK_GE(save_count, 1);
  int diff = GetCanvas()->getSaveCount() - save_count;
  DCHECK_GE(diff, 0);
  for (int i = 0; i < diff; ++i)
    restore();
}

void RecordPaintCanvas::translate(SkScalar dx, SkScalar dy) {
  buffer_->push<TranslateOp>(dx, dy);
  GetCanvas()->translate(dx, dy);
}

void RecordPaintCanvas::scale(SkScalar sx, SkScalar sy) {
  buffer_->push<ScaleOp>(sx, sy);
  GetCanvas()->scale(sx, sy);
}

void RecordPaintCanvas::rotate(SkScalar degrees) {
  buffer_->push<RotateOp>(degrees);
  GetCanvas()->rotate(degrees);
}

void RecordPaintCanvas::concat(const SkMatrix& matrix) {
  buffer_->push<ConcatOp>(matrix);
  GetCanvas()->concat(matrix);
}

void RecordPaintCanvas::setMatrix(const SkMatrix& matrix) {
  buffer_->push<SetMatrixOp>(matrix);
  GetCanvas()->setMatrix(matrix);
}

void RecordPaintCanvas::clipRect(const SkRect& rect,
                                 SkClipOp op,
                                 bool antialias) {
  buffer_->push<ClipRectOp>(rect, op, antialias);
  GetCanvas()->clipRect(rect, op, antialias);
}

void RecordPaintCanvas::clipRRect(const SkRRect& rrect,
                                  SkClipOp op,
                                  bool antialias) {
  // TODO(enne): does this happen? Should the caller know this?
  if (rrect.isRect()) {
    clipRect(rrect.getBounds(), op, antialias);
    return;
  }
  buffer_->push<ClipRRectOp>(rrect, op, antialias);
  GetCanvas()->clipRRect(rrect, op, antialias);
}

void RecordPaintCanvas::clipPath(const SkPath& path,
                                 SkClipOp op,
                                 bool antialias) {
  if (!path.isInverseFillType() &&
      GetCanvas()->getTotalMatrix().rectStaysRect()) {
    // TODO(enne): do these cases happen? should the caller know that this isn't
    // a path?
    SkRect rect;
    if (path.isRect(&rect)) {
      clipRect(rect, op, antialias);
      return;
    }
    SkRRect rrect;
    if (path.isOval(&rect)) {
      rrect.setOval(rect);
      clipRRect(rrect, op, antialias);
      return;
    }
    if (path.isRRect(&rrect)) {
      clipRRect(rrect, op, antialias);
      return;
    }
  }

  buffer_->push<ClipPathOp>(path, op, antialias);
  GetCanvas()->clipPath(path, op, antialias);
  return;
}

bool RecordPaintCanvas::quickReject(const SkRect& rect) const {
  return GetCanvas()->quickReject(rect);
}

bool RecordPaintCanvas::quickReject(const SkPath& path) const {
  return GetCanvas()->quickReject(path);
}

SkRect RecordPaintCanvas::getLocalClipBounds() const {
  return GetCanvas()->getLocalClipBounds();
}

bool RecordPaintCanvas::getLocalClipBounds(SkRect* bounds) const {
  return GetCanvas()->getLocalClipBounds(bounds);
}

SkIRect RecordPaintCanvas::getDeviceClipBounds() const {
  return GetCanvas()->getDeviceClipBounds();
}

bool RecordPaintCanvas::getDeviceClipBounds(SkIRect* bounds) const {
  return GetCanvas()->getDeviceClipBounds(bounds);
}

void RecordPaintCanvas::drawColor(SkColor color, SkBlendMode mode) {
  buffer_->push<DrawColorOp>(color, mode);
}

void RecordPaintCanvas::clear(SkColor color) {
  buffer_->push<DrawColorOp>(color, SkBlendMode::kSrc);
}

void RecordPaintCanvas::drawLine(SkScalar x0,
                                 SkScalar y0,
                                 SkScalar x1,
                                 SkScalar y1,
                                 const PaintFlags& flags) {
  buffer_->push<DrawLineOp>(x0, y0, x1, y1, flags);
}

void RecordPaintCanvas::drawRect(const SkRect& rect, const PaintFlags& flags) {
  buffer_->push<DrawRectOp>(rect, flags);
}

void RecordPaintCanvas::drawIRect(const SkIRect& rect,
                                  const PaintFlags& flags) {
  buffer_->push<DrawIRectOp>(rect, flags);
}

void RecordPaintCanvas::drawOval(const SkRect& oval, const PaintFlags& flags) {
  buffer_->push<DrawOvalOp>(oval, flags);
}

void RecordPaintCanvas::drawRRect(const SkRRect& rrect,
                                  const PaintFlags& flags) {
  buffer_->push<DrawRRectOp>(rrect, flags);
}

void RecordPaintCanvas::drawDRRect(const SkRRect& outer,
                                   const SkRRect& inner,
                                   const PaintFlags& flags) {
  if (outer.isEmpty())
    return;
  if (inner.isEmpty()) {
    drawRRect(outer, flags);
    return;
  }
  buffer_->push<DrawDRRectOp>(outer, inner, flags);
}

void RecordPaintCanvas::drawCircle(SkScalar cx,
                                   SkScalar cy,
                                   SkScalar radius,
                                   const PaintFlags& flags) {
  buffer_->push<DrawCircleOp>(cx, cy, radius, flags);
}

void RecordPaintCanvas::drawArc(const SkRect& oval,
                                SkScalar start_angle,
                                SkScalar sweep_angle,
                                bool use_center,
                                const PaintFlags& flags) {
  buffer_->push<DrawArcOp>(oval, start_angle, sweep_angle, use_center, flags);
}

void RecordPaintCanvas::drawRoundRect(const SkRect& rect,
                                      SkScalar rx,
                                      SkScalar ry,
                                      const PaintFlags& flags) {
  // TODO(enne): move this into base class?
  if (rx > 0 && ry > 0) {
    SkRRect rrect;
    rrect.setRectXY(rect, rx, ry);
    drawRRect(rrect, flags);
  } else {
    drawRect(rect, flags);
  }
}

void RecordPaintCanvas::drawPath(const SkPath& path, const PaintFlags& flags) {
  buffer_->push<DrawPathOp>(path, flags);
}

void RecordPaintCanvas::drawImage(const PaintImage& image,
                                  SkScalar left,
                                  SkScalar top,
                                  const PaintFlags* flags) {
  buffer_->push<DrawImageOp>(image, left, top, flags);
}

void RecordPaintCanvas::drawImageRect(const PaintImage& image,
                                      const SkRect& src,
                                      const SkRect& dst,
                                      const PaintFlags* flags,
                                      SrcRectConstraint constraint) {
  buffer_->push<DrawImageRectOp>(image, src, dst, flags, constraint);
}

void RecordPaintCanvas::drawBitmap(const SkBitmap& bitmap,
                                   SkScalar left,
                                   SkScalar top,
                                   const PaintFlags* flags) {
  // TODO(enne): Move into base class?
  if (bitmap.drawsNothing())
    return;
  drawImage(
      PaintImage(PaintImage::kNonLazyStableId, SkImage::MakeFromBitmap(bitmap),
                 PaintImage::AnimationType::UNKNOWN,
                 PaintImage::CompletionState::UNKNOWN),
      left, top, flags);
}

void RecordPaintCanvas::drawText(const void* text,
                                 size_t byte_length,
                                 SkScalar x,
                                 SkScalar y,
                                 const PaintFlags& flags) {
  buffer_->push_with_data<DrawTextOp>(text, byte_length, x, y, flags);
}

void RecordPaintCanvas::drawPosText(const void* text,
                                    size_t byte_length,
                                    const SkPoint pos[],
                                    const PaintFlags& flags) {
  size_t count = ToSkPaint(flags).countText(text, byte_length);
  buffer_->push_with_array<DrawPosTextOp>(text, byte_length, pos, count, flags);
}

void RecordPaintCanvas::drawTextBlob(sk_sp<SkTextBlob> blob,
                                     SkScalar x,
                                     SkScalar y,
                                     const PaintFlags& flags) {
  buffer_->push<DrawTextBlobOp>(blob, x, y, flags);
}

void RecordPaintCanvas::drawDisplayItemList(
    scoped_refptr<DisplayItemList> list) {
  buffer_->push<DrawDisplayItemListOp>(list);
}

void RecordPaintCanvas::drawPicture(sk_sp<const PaintRecord> record) {
  // TODO(enne): If this is small, maybe flatten it?
  buffer_->push<DrawRecordOp>(record);
}

bool RecordPaintCanvas::isClipEmpty() const {
  return GetCanvas()->isClipEmpty();
}

bool RecordPaintCanvas::isClipRect() const {
  return GetCanvas()->isClipRect();
}

const SkMatrix& RecordPaintCanvas::getTotalMatrix() const {
  return GetCanvas()->getTotalMatrix();
}

void RecordPaintCanvas::Annotate(AnnotationType type,
                                 const SkRect& rect,
                                 sk_sp<SkData> data) {
  buffer_->push<AnnotateOp>(type, rect, data);
}

const SkNoDrawCanvas* RecordPaintCanvas::GetCanvas() const {
  return const_cast<RecordPaintCanvas*>(this)->GetCanvas();
}

SkNoDrawCanvas* RecordPaintCanvas::GetCanvas() {
  if (canvas_)
    return &*canvas_;

  SkRect recording_bounds = buffer_->cullRect();

  // Size the canvas to be large enough to contain the |recording_bounds|, which
  // may not be positioned at th origin.
  SkIRect enclosing_rect = recording_bounds.roundOut();
  canvas_.emplace(enclosing_rect.right(), enclosing_rect.bottom());

  // This is part of the "recording canvases have a size, but why" dance.
  // By creating a canvas of size (right x bottom) and then clipping it,
  // It makes getDeviceClipBounds return the original cull rect, which code
  // in GraphicsContextCanvas on Mac expects.  (Just creating an SkNoDrawCanvas
  // with the cull_rect makes a canvas of size (width x height) instead
  // which is incorrect.  SkRecorder cheats with private resetForNextCanvas.
  canvas_->clipRect(recording_bounds, SkClipOp::kIntersect, false);
  return &*canvas_;
}

}  // namespace cc
