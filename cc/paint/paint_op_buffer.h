// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CC_PAINT_PAINT_OP_BUFFER_H_
#define CC_PAINT_PAINT_OP_BUFFER_H_

#include <stdint.h>

#include "base/logging.h"
#include "base/memory/aligned_memory.h"
#include "cc/base/math_util.h"
#include "cc/paint/paint_canvas.h"
#include "cc/paint/paint_export.h"
#include "cc/paint/paint_flags.h"
#include "third_party/skia/include/core/SkPicture.h"
#include "third_party/skia/include/core/SkRect.h"
#include "third_party/skia/include/core/SkTextBlob.h"

// PaintOpBuffer is a reimplementation of SkLiteDL.
// See: third_party/skia/src/core/SkLiteDL.h.

namespace cc {
class DisplayItemList;

class CC_PAINT_EXPORT ThreadsafeMatrix : public SkMatrix {
 public:
  explicit ThreadsafeMatrix(const SkMatrix& matrix) : SkMatrix(matrix) {
    (void)getType();
  }
};

class CC_PAINT_EXPORT ThreadsafePath : public SkPath {
 public:
  explicit ThreadsafePath(const SkPath& path) : SkPath(path) {
    updateBoundsCache();
  }
};

enum class PaintOpType : uint8_t {
  Annotate,
  ClipPath,
  ClipRect,
  ClipRRect,
  Concat,
  DrawArc,
  DrawCircle,
  DrawColor,
  DrawDisplayItemList,
  DrawDRRect,
  DrawImage,
  DrawImageRect,
  DrawIRect,
  DrawLine,
  DrawOval,
  DrawPath,
  DrawPosText,
  DrawRecord,
  DrawRect,
  DrawRRect,
  DrawText,
  DrawTextBlob,
  Noop,
  Restore,
  Rotate,
  Save,
  SaveLayer,
  SaveLayerAlpha,
  Scale,
  SetMatrix,
  Translate,
  LastPaintOpType = Translate,
};

struct CC_PAINT_EXPORT PaintOp {
  uint32_t type : 8;
  uint32_t skip : 24;

  PaintOpType GetType() const { return static_cast<PaintOpType>(type); }

  // Subclasses should provide a static Raster() method which is called from
  // here. The Raster method should take a const PaintOp* parameter. It is
  // static with a pointer to the base type so that we can use it as a function
  // pointer.
  void Raster(SkCanvas* canvas, const SkMatrix& original_ctm) const;
  bool IsDrawOp() const;

  // Only valid for draw ops.
  void RasterWithAlpha(SkCanvas* canvas, uint8_t alpha) const;

  int CountSlowPaths() const { return 0; }
  int CountSlowPathsFromFlags() const { return 0; }

  bool HasDiscardableImages() const { return false; }
  bool HasDiscardableImagesFromFlags() const { return false; }

  // Returns the number of bytes used by this op in referenced sub records
  // and display lists.  This doesn't count other objects like paths or blobs.
  size_t AdditionalBytesUsed() const { return 0; }

  static constexpr bool kIsDrawOp = false;
  static constexpr bool kHasPaintFlags = false;
  static SkRect kUnsetRect;
};

struct CC_PAINT_EXPORT PaintOpWithFlags : PaintOp {
  static constexpr bool kHasPaintFlags = true;

  explicit PaintOpWithFlags(const PaintFlags& flags) : flags(flags) {}

  int CountSlowPathsFromFlags() const { return flags.getPathEffect() ? 1 : 0; }
  bool HasDiscardableImagesFromFlags() const {
    if (!IsDrawOp())
      return false;

    SkShader* shader = flags.getShader();
    SkImage* image = shader ? shader->isAImage(nullptr, nullptr) : nullptr;
    return image && image->isLazyGenerated();
  }

  // Subclasses should provide a static RasterWithFlags() method which is called
  // from the Raster() method. The RasterWithFlags() should use the PaintFlags
  // passed to it, instead of the |flags| member directly, as some callers may
  // provide a modified PaintFlags. The RasterWithFlags() method is static with
  // a const PaintOpWithFlags* parameter so that it can be used as a function
  // pointer.
  PaintFlags flags;
};

struct CC_PAINT_EXPORT PaintOpWithData : PaintOpWithFlags {
  // Having data is just a helper for ops that have a varying amount of data and
  // want a way to store that inline.  This is for ops that pass in a
  // void* and a length.  The void* data is assumed to not have any alignment
  // requirements.
  PaintOpWithData(const PaintFlags& flags, size_t bytes)
      : PaintOpWithFlags(flags), bytes(bytes) {}

  // Get data out by calling paint_op_data.  This can't be part of the class
  // because it needs to know the size of the derived type.
  size_t bytes;

 protected:
  // For some derived object T, return the internally stored data.
  // This needs the fully derived type to know how much to offset
  // from the start of the top to the data.
  template <typename T>
  const void* GetDataForThis(const T* op) const {
    static_assert(std::is_convertible<T, PaintOpWithData>::value,
                  "T is not a PaintOpWithData");
    // Arbitrary data for a PaintOp is stored after the PaintOp itself
    // in the PaintOpBuffer.  Therefore, to access this data, it's
    // pointer math to increment past the size of T.  Accessing the
    // next op in the buffer is ((char*)op) + op->skip, with the data
    // fitting between.
    return op + 1;
  }

  template <typename T>
  void* GetDataForThis(T* op) {
    return const_cast<void*>(
        const_cast<const PaintOpWithData*>(this)->GetDataForThis(
            const_cast<const T*>(op)));
  }
};

struct CC_PAINT_EXPORT PaintOpWithArrayBase : PaintOpWithFlags {
  explicit PaintOpWithArrayBase(const PaintFlags& flags)
      : PaintOpWithFlags(flags) {}
};

template <typename M>
struct CC_PAINT_EXPORT PaintOpWithArray : PaintOpWithArrayBase {
  // Paint op that has a M[count] and a char[bytes].
  // Array data is stored first so that it can be aligned with T's alignment
  // with the arbitrary unaligned char data after it.
  // Memory layout here is: | op | M[count] | char[bytes] | padding | next op |
  // Next op is located at (char*)(op) + op->skip.
  PaintOpWithArray(const PaintFlags& flags, size_t bytes, size_t count)
      : PaintOpWithArrayBase(flags), bytes(bytes), count(count) {}

  size_t bytes;
  size_t count;

 protected:
  template <typename T>
  const void* GetDataForThis(const T* op) const {
    static_assert(std::is_convertible<T, PaintOpWithArrayBase>::value,
                  "T is not a PaintOpWithData");
    const char* start_array =
        reinterpret_cast<const char*>(GetArrayForThis(op));
    return start_array + sizeof(M) * count;
  }

  template <typename T>
  void* GetDataForThis(T* op) {
    return const_cast<void*>(
        const_cast<const PaintOpWithArray*>(this)->GetDataForThis(
            const_cast<T*>(op)));
  }

  template <typename T>
  const M* GetArrayForThis(const T* op) const {
    static_assert(std::is_convertible<T, PaintOpWithArrayBase>::value,
                  "T is not a PaintOpWithData");
    // As an optimization to not have to store an additional offset,
    // assert that T has the same or more alignment requirements than M.  Thus,
    // if T is aligned, and M's alignment needs are a multiple of T's size, then
    // M will also be aligned when placed immediately after T.
    static_assert(
        sizeof(T) % ALIGNOF(M) == 0,
        "T must be padded such that an array of M is aligned after it");
    static_assert(
        ALIGNOF(T) >= ALIGNOF(M),
        "T must have not have less alignment requirements than the array data");
    return reinterpret_cast<const M*>(op + 1);
  }

  template <typename T>
  M* GetArrayForThis(T* op) {
    return const_cast<M*>(
        const_cast<const PaintOpWithArray*>(this)->GetArrayForThis(
            const_cast<T*>(op)));
  }
};

struct CC_PAINT_EXPORT AnnotateOp final : PaintOp {
  enum class AnnotationType {
    URL,
    LinkToDestination,
    NamedDestination,
  };

  static constexpr PaintOpType kType = PaintOpType::Annotate;
  AnnotateOp(PaintCanvas::AnnotationType annotation_type,
             const SkRect& rect,
             sk_sp<SkData> data);
  ~AnnotateOp();
  static void Raster(const PaintOp* op,
                     SkCanvas* canvas,
                     const SkMatrix& original_ctm);

  PaintCanvas::AnnotationType annotation_type;
  SkRect rect;
  sk_sp<SkData> data;
};

struct CC_PAINT_EXPORT ClipPathOp final : PaintOp {
  static constexpr PaintOpType kType = PaintOpType::ClipPath;
  ClipPathOp(SkPath path, SkClipOp op, bool antialias)
      : path(path), op(op), antialias(antialias) {}
  static void Raster(const PaintOp* op,
                     SkCanvas* canvas,
                     const SkMatrix& original_ctm);
  int CountSlowPaths() const;

  ThreadsafePath path;
  SkClipOp op;
  bool antialias;
};

struct CC_PAINT_EXPORT ClipRectOp final : PaintOp {
  static constexpr PaintOpType kType = PaintOpType::ClipRect;
  ClipRectOp(const SkRect& rect, SkClipOp op, bool antialias)
      : rect(rect), op(op), antialias(antialias) {}
  static void Raster(const PaintOp* op,
                     SkCanvas* canvas,
                     const SkMatrix& original_ctm);

  SkRect rect;
  SkClipOp op;
  bool antialias;
};

struct CC_PAINT_EXPORT ClipRRectOp final : PaintOp {
  static constexpr PaintOpType kType = PaintOpType::ClipRRect;
  ClipRRectOp(const SkRRect& rrect, SkClipOp op, bool antialias)
      : rrect(rrect), op(op), antialias(antialias) {}
  static void Raster(const PaintOp* op,
                     SkCanvas* canvas,
                     const SkMatrix& original_ctm);

  SkRRect rrect;
  SkClipOp op;
  bool antialias;
};

struct CC_PAINT_EXPORT ConcatOp final : PaintOp {
  static constexpr PaintOpType kType = PaintOpType::Concat;
  explicit ConcatOp(const SkMatrix& matrix) : matrix(matrix) {}
  static void Raster(const PaintOp* op,
                     SkCanvas* canvas,
                     const SkMatrix& original_ctm);

  ThreadsafeMatrix matrix;
};

struct CC_PAINT_EXPORT DrawArcOp final : PaintOpWithFlags {
  static constexpr PaintOpType kType = PaintOpType::DrawArc;
  static constexpr bool kIsDrawOp = true;
  DrawArcOp(const SkRect& oval,
            SkScalar start_angle,
            SkScalar sweep_angle,
            bool use_center,
            const PaintFlags& flags)
      : PaintOpWithFlags(flags),
        oval(oval),
        start_angle(start_angle),
        sweep_angle(sweep_angle),
        use_center(use_center) {}
  static void Raster(const PaintOp* op,
                     SkCanvas* canvas,
                     const SkMatrix& original_ctm) {
    auto* flags_op = static_cast<const PaintOpWithFlags*>(op);
    RasterWithFlags(flags_op, &flags_op->flags, canvas, original_ctm);
  }
  static void RasterWithFlags(const PaintOpWithFlags* op,
                              const PaintFlags* flags,
                              SkCanvas* canvas,
                              const SkMatrix& original_ctm);

  SkRect oval;
  SkScalar start_angle;
  SkScalar sweep_angle;
  bool use_center;
};

struct CC_PAINT_EXPORT DrawCircleOp final : PaintOpWithFlags {
  static constexpr PaintOpType kType = PaintOpType::DrawCircle;
  static constexpr bool kIsDrawOp = true;
  DrawCircleOp(SkScalar cx,
               SkScalar cy,
               SkScalar radius,
               const PaintFlags& flags)
      : PaintOpWithFlags(flags), cx(cx), cy(cy), radius(radius) {}
  static void Raster(const PaintOp* op,
                     SkCanvas* canvas,
                     const SkMatrix& original_ctm) {
    auto* flags_op = static_cast<const PaintOpWithFlags*>(op);
    RasterWithFlags(flags_op, &flags_op->flags, canvas, original_ctm);
  }
  static void RasterWithFlags(const PaintOpWithFlags* op,
                              const PaintFlags* flags,
                              SkCanvas* canvas,
                              const SkMatrix& original_ctm);

  SkScalar cx;
  SkScalar cy;
  SkScalar radius;
};

struct CC_PAINT_EXPORT DrawColorOp final : PaintOp {
  static constexpr PaintOpType kType = PaintOpType::DrawColor;
  static constexpr bool kIsDrawOp = true;
  DrawColorOp(SkColor color, SkBlendMode mode) : color(color), mode(mode) {}
  static void Raster(const PaintOp* op,
                     SkCanvas* canvas,
                     const SkMatrix& original_ctm);

  SkColor color;
  SkBlendMode mode;
};

struct CC_PAINT_EXPORT DrawDisplayItemListOp final : PaintOp {
  static constexpr PaintOpType kType = PaintOpType::DrawDisplayItemList;
  static constexpr bool kIsDrawOp = true;
  explicit DrawDisplayItemListOp(scoped_refptr<DisplayItemList> list);
  // Windows wants to generate these when types are exported, so
  // provide them here explicitly so that DisplayItemList doesn't have
  // to be defined in this header.
  DrawDisplayItemListOp(const DrawDisplayItemListOp& op);
  DrawDisplayItemListOp& operator=(const DrawDisplayItemListOp& op);
  ~DrawDisplayItemListOp();
  static void Raster(const PaintOp* op,
                     SkCanvas* canvas,
                     const SkMatrix& original_ctm);
  size_t AdditionalBytesUsed() const;
  bool HasDiscardableImages() const;
  // TODO(enne): DisplayItemList should know number of slow paths.

  scoped_refptr<DisplayItemList> list;
};

struct CC_PAINT_EXPORT DrawDRRectOp final : PaintOpWithFlags {
  static constexpr PaintOpType kType = PaintOpType::DrawDRRect;
  static constexpr bool kIsDrawOp = true;
  DrawDRRectOp(const SkRRect& outer,
               const SkRRect& inner,
               const PaintFlags& flags)
      : PaintOpWithFlags(flags), outer(outer), inner(inner) {}
  static void Raster(const PaintOp* op,
                     SkCanvas* canvas,
                     const SkMatrix& original_ctm) {
    auto* flags_op = static_cast<const PaintOpWithFlags*>(op);
    RasterWithFlags(flags_op, &flags_op->flags, canvas, original_ctm);
  }
  static void RasterWithFlags(const PaintOpWithFlags* op,
                              const PaintFlags* flags,
                              SkCanvas* canvas,
                              const SkMatrix& original_ctm);

  SkRRect outer;
  SkRRect inner;
};

struct CC_PAINT_EXPORT DrawImageOp final : PaintOpWithFlags {
  static constexpr PaintOpType kType = PaintOpType::DrawImage;
  static constexpr bool kIsDrawOp = true;
  DrawImageOp(const PaintImage& image,
              SkScalar left,
              SkScalar top,
              const PaintFlags* flags);
  ~DrawImageOp();
  static void Raster(const PaintOp* op,
                     SkCanvas* canvas,
                     const SkMatrix& original_ctm) {
    auto* flags_op = static_cast<const PaintOpWithFlags*>(op);
    RasterWithFlags(flags_op, &flags_op->flags, canvas, original_ctm);
  }
  static void RasterWithFlags(const PaintOpWithFlags* op,
                              const PaintFlags* flags,
                              SkCanvas* canvas,
                              const SkMatrix& original_ctm);
  bool HasDiscardableImages() const;

  PaintImage image;
  SkScalar left;
  SkScalar top;
};

struct CC_PAINT_EXPORT DrawImageRectOp final : PaintOpWithFlags {
  static constexpr PaintOpType kType = PaintOpType::DrawImageRect;
  static constexpr bool kIsDrawOp = true;
  DrawImageRectOp(const PaintImage& image,
                  const SkRect& src,
                  const SkRect& dst,
                  const PaintFlags* flags,
                  PaintCanvas::SrcRectConstraint constraint);
  ~DrawImageRectOp();
  static void Raster(const PaintOp* op,
                     SkCanvas* canvas,
                     const SkMatrix& original_ctm) {
    auto* flags_op = static_cast<const PaintOpWithFlags*>(op);
    RasterWithFlags(flags_op, &flags_op->flags, canvas, original_ctm);
  }
  static void RasterWithFlags(const PaintOpWithFlags* op,
                              const PaintFlags* flags,
                              SkCanvas* canvas,
                              const SkMatrix& original_ctm);
  bool HasDiscardableImages() const;

  PaintImage image;
  SkRect src;
  SkRect dst;
  PaintCanvas::SrcRectConstraint constraint;
};

struct CC_PAINT_EXPORT DrawIRectOp final : PaintOpWithFlags {
  static constexpr PaintOpType kType = PaintOpType::DrawIRect;
  static constexpr bool kIsDrawOp = true;
  DrawIRectOp(const SkIRect& rect, const PaintFlags& flags)
      : PaintOpWithFlags(flags), rect(rect) {}
  static void Raster(const PaintOp* op,
                     SkCanvas* canvas,
                     const SkMatrix& original_ctm) {
    auto* flags_op = static_cast<const PaintOpWithFlags*>(op);
    RasterWithFlags(flags_op, &flags_op->flags, canvas, original_ctm);
  }
  static void RasterWithFlags(const PaintOpWithFlags* op,
                              const PaintFlags* flags,
                              SkCanvas* canvas,
                              const SkMatrix& original_ctm);

  SkIRect rect;
};

struct CC_PAINT_EXPORT DrawLineOp final : PaintOpWithFlags {
  static constexpr PaintOpType kType = PaintOpType::DrawLine;
  static constexpr bool kIsDrawOp = true;
  DrawLineOp(SkScalar x0,
             SkScalar y0,
             SkScalar x1,
             SkScalar y1,
             const PaintFlags& flags)
      : PaintOpWithFlags(flags), x0(x0), y0(y0), x1(x1), y1(y1) {}
  static void Raster(const PaintOp* op,
                     SkCanvas* canvas,
                     const SkMatrix& original_ctm) {
    auto* flags_op = static_cast<const PaintOpWithFlags*>(op);
    RasterWithFlags(flags_op, &flags_op->flags, canvas, original_ctm);
  }
  static void RasterWithFlags(const PaintOpWithFlags* op,
                              const PaintFlags* flags,
                              SkCanvas* canvas,
                              const SkMatrix& original_ctm);

  int CountSlowPaths() const;

  SkScalar x0;
  SkScalar y0;
  SkScalar x1;
  SkScalar y1;
};

struct CC_PAINT_EXPORT DrawOvalOp final : PaintOpWithFlags {
  static constexpr PaintOpType kType = PaintOpType::DrawOval;
  static constexpr bool kIsDrawOp = true;
  DrawOvalOp(const SkRect& oval, const PaintFlags& flags)
      : PaintOpWithFlags(flags), oval(oval) {}
  static void Raster(const PaintOp* op,
                     SkCanvas* canvas,
                     const SkMatrix& original_ctm) {
    auto* flags_op = static_cast<const PaintOpWithFlags*>(op);
    RasterWithFlags(flags_op, &flags_op->flags, canvas, original_ctm);
  }
  static void RasterWithFlags(const PaintOpWithFlags* op,
                              const PaintFlags* flags,
                              SkCanvas* canvas,
                              const SkMatrix& original_ctm);

  SkRect oval;
};

struct CC_PAINT_EXPORT DrawPathOp final : PaintOpWithFlags {
  static constexpr PaintOpType kType = PaintOpType::DrawPath;
  static constexpr bool kIsDrawOp = true;
  DrawPathOp(const SkPath& path, const PaintFlags& flags)
      : PaintOpWithFlags(flags), path(path) {}
  static void Raster(const PaintOp* op,
                     SkCanvas* canvas,
                     const SkMatrix& original_ctm) {
    auto* flags_op = static_cast<const PaintOpWithFlags*>(op);
    RasterWithFlags(flags_op, &flags_op->flags, canvas, original_ctm);
  }
  static void RasterWithFlags(const PaintOpWithFlags* op,
                              const PaintFlags* flags,
                              SkCanvas* canvas,
                              const SkMatrix& original_ctm);
  int CountSlowPaths() const;

  ThreadsafePath path;
};

struct CC_PAINT_EXPORT DrawPosTextOp final : PaintOpWithArray<SkPoint> {
  static constexpr PaintOpType kType = PaintOpType::DrawPosText;
  static constexpr bool kIsDrawOp = true;
  DrawPosTextOp(size_t bytes, size_t count, const PaintFlags& flags);
  ~DrawPosTextOp();
  static void Raster(const PaintOp* op,
                     SkCanvas* canvas,
                     const SkMatrix& original_ctm) {
    auto* flags_op = static_cast<const PaintOpWithFlags*>(op);
    RasterWithFlags(flags_op, &flags_op->flags, canvas, original_ctm);
  }
  static void RasterWithFlags(const PaintOpWithFlags* op,
                              const PaintFlags* flags,
                              SkCanvas* canvas,
                              const SkMatrix& original_ctm);

  const void* GetData() const { return GetDataForThis(this); }
  void* GetData() { return GetDataForThis(this); }
  const SkPoint* GetArray() const { return GetArrayForThis(this); }
  SkPoint* GetArray() { return GetArrayForThis(this); }
};

struct CC_PAINT_EXPORT DrawRecordOp final : PaintOp {
  static constexpr PaintOpType kType = PaintOpType::DrawRecord;
  static constexpr bool kIsDrawOp = true;
  explicit DrawRecordOp(sk_sp<const PaintRecord> record);
  ~DrawRecordOp();
  static void Raster(const PaintOp* op,
                     SkCanvas* canvas,
                     const SkMatrix& original_ctm);
  size_t AdditionalBytesUsed() const;
  bool HasDiscardableImages() const;
  int CountSlowPaths() const;

  sk_sp<const PaintRecord> record;
};

struct CC_PAINT_EXPORT DrawRectOp final : PaintOpWithFlags {
  static constexpr PaintOpType kType = PaintOpType::DrawRect;
  static constexpr bool kIsDrawOp = true;
  DrawRectOp(const SkRect& rect, const PaintFlags& flags)
      : PaintOpWithFlags(flags), rect(rect) {}
  static void Raster(const PaintOp* op,
                     SkCanvas* canvas,
                     const SkMatrix& original_ctm) {
    auto* flags_op = static_cast<const PaintOpWithFlags*>(op);
    RasterWithFlags(flags_op, &flags_op->flags, canvas, original_ctm);
  }
  static void RasterWithFlags(const PaintOpWithFlags* op,
                              const PaintFlags* flags,
                              SkCanvas* canvas,
                              const SkMatrix& original_ctm);

  SkRect rect;
};

struct CC_PAINT_EXPORT DrawRRectOp final : PaintOpWithFlags {
  static constexpr PaintOpType kType = PaintOpType::DrawRRect;
  static constexpr bool kIsDrawOp = true;
  DrawRRectOp(const SkRRect& rrect, const PaintFlags& flags)
      : PaintOpWithFlags(flags), rrect(rrect) {}
  static void Raster(const PaintOp* op,
                     SkCanvas* canvas,
                     const SkMatrix& original_ctm) {
    auto* flags_op = static_cast<const PaintOpWithFlags*>(op);
    RasterWithFlags(flags_op, &flags_op->flags, canvas, original_ctm);
  }
  static void RasterWithFlags(const PaintOpWithFlags* op,
                              const PaintFlags* flags,
                              SkCanvas* canvas,
                              const SkMatrix& original_ctm);

  SkRRect rrect;
};

struct CC_PAINT_EXPORT DrawTextOp final : PaintOpWithData {
  static constexpr PaintOpType kType = PaintOpType::DrawText;
  static constexpr bool kIsDrawOp = true;
  DrawTextOp(size_t bytes, SkScalar x, SkScalar y, const PaintFlags& flags)
      : PaintOpWithData(flags, bytes), x(x), y(y) {}
  static void Raster(const PaintOp* op,
                     SkCanvas* canvas,
                     const SkMatrix& original_ctm) {
    auto* flags_op = static_cast<const PaintOpWithFlags*>(op);
    RasterWithFlags(flags_op, &flags_op->flags, canvas, original_ctm);
  }
  static void RasterWithFlags(const PaintOpWithFlags* op,
                              const PaintFlags* flags,
                              SkCanvas* canvas,
                              const SkMatrix& original_ctm);

  void* GetData() { return GetDataForThis(this); }
  const void* GetData() const { return GetDataForThis(this); }

  SkScalar x;
  SkScalar y;
};

struct CC_PAINT_EXPORT DrawTextBlobOp final : PaintOpWithFlags {
  static constexpr PaintOpType kType = PaintOpType::DrawTextBlob;
  static constexpr bool kIsDrawOp = true;
  DrawTextBlobOp(sk_sp<SkTextBlob> blob,
                 SkScalar x,
                 SkScalar y,
                 const PaintFlags& flags);
  ~DrawTextBlobOp();
  static void Raster(const PaintOp* op,
                     SkCanvas* canvas,
                     const SkMatrix& original_ctm) {
    auto* flags_op = static_cast<const PaintOpWithFlags*>(op);
    RasterWithFlags(flags_op, &flags_op->flags, canvas, original_ctm);
  }
  static void RasterWithFlags(const PaintOpWithFlags* op,
                              const PaintFlags* flags,
                              SkCanvas* canvas,
                              const SkMatrix& original_ctm);

  sk_sp<SkTextBlob> blob;
  SkScalar x;
  SkScalar y;
};

struct CC_PAINT_EXPORT NoopOp final : PaintOp {
  static constexpr PaintOpType kType = PaintOpType::Noop;
  static void Raster(const PaintOp* op,
                     SkCanvas* canvas,
                     const SkMatrix& original_ctm) {}
};

struct CC_PAINT_EXPORT RestoreOp final : PaintOp {
  static constexpr PaintOpType kType = PaintOpType::Restore;
  static void Raster(const PaintOp* op,
                     SkCanvas* canvas,
                     const SkMatrix& original_ctm);
};

struct CC_PAINT_EXPORT RotateOp final : PaintOp {
  static constexpr PaintOpType kType = PaintOpType::Rotate;
  explicit RotateOp(SkScalar degrees) : degrees(degrees) {}
  static void Raster(const PaintOp* op,
                     SkCanvas* canvas,
                     const SkMatrix& original_ctm);

  SkScalar degrees;
};

struct CC_PAINT_EXPORT SaveOp final : PaintOp {
  static constexpr PaintOpType kType = PaintOpType::Save;
  static void Raster(const PaintOp* op,
                     SkCanvas* canvas,
                     const SkMatrix& original_ctm);
};

struct CC_PAINT_EXPORT SaveLayerOp final : PaintOpWithFlags {
  static constexpr PaintOpType kType = PaintOpType::SaveLayer;
  SaveLayerOp(const SkRect* bounds, const PaintFlags* flags)
      : PaintOpWithFlags(flags ? *flags : PaintFlags()),
        bounds(bounds ? *bounds : kUnsetRect) {}
  static void Raster(const PaintOp* op,
                     SkCanvas* canvas,
                     const SkMatrix& original_ctm) {
    auto* flags_op = static_cast<const PaintOpWithFlags*>(op);
    RasterWithFlags(flags_op, &flags_op->flags, canvas, original_ctm);
  }
  static void RasterWithFlags(const PaintOpWithFlags* op,
                              const PaintFlags* flags,
                              SkCanvas* canvas,
                              const SkMatrix& original_ctm);

  SkRect bounds;
};

struct CC_PAINT_EXPORT SaveLayerAlphaOp final : PaintOp {
  static constexpr PaintOpType kType = PaintOpType::SaveLayerAlpha;
  SaveLayerAlphaOp(const SkRect* bounds, uint8_t alpha)
      : bounds(bounds ? *bounds : kUnsetRect), alpha(alpha) {}
  static void Raster(const PaintOp* op,
                     SkCanvas* canvas,
                     const SkMatrix& original_ctm);

  SkRect bounds;
  uint8_t alpha;
};

struct CC_PAINT_EXPORT ScaleOp final : PaintOp {
  static constexpr PaintOpType kType = PaintOpType::Scale;
  ScaleOp(SkScalar sx, SkScalar sy) : sx(sx), sy(sy) {}
  static void Raster(const PaintOp* op,
                     SkCanvas* canvas,
                     const SkMatrix& original_ctm);

  SkScalar sx;
  SkScalar sy;
};

struct CC_PAINT_EXPORT SetMatrixOp final : PaintOp {
  static constexpr PaintOpType kType = PaintOpType::SetMatrix;
  explicit SetMatrixOp(const SkMatrix& matrix) : matrix(matrix) {}
  // This is the only op that needs the original ctm of the SkCanvas
  // used for raster (since SetMatrix is relative to the recording origin and
  // shouldn't clobber the SkCanvas raster origin).
  //
  // TODO(enne): Find some cleaner way to do this, possibly by making
  // all SetMatrix calls Concat??
  static void Raster(const PaintOp* op,
                     SkCanvas* canvas,
                     const SkMatrix& original_ctm);

  ThreadsafeMatrix matrix;
};

struct CC_PAINT_EXPORT TranslateOp final : PaintOp {
  static constexpr PaintOpType kType = PaintOpType::Translate;
  TranslateOp(SkScalar dx, SkScalar dy) : dx(dx), dy(dy) {}
  static void Raster(const PaintOp* op,
                     SkCanvas* canvas,
                     const SkMatrix& original_ctm);

  SkScalar dx;
  SkScalar dy;
};

using LargestPaintOp = DrawDRRectOp;

class CC_PAINT_EXPORT PaintOpBuffer : public SkRefCnt {
 public:
  enum { kInitialBufferSize = 4096 };
  // It's not necessarily the case that the op with the maximum alignment
  // requirements is also the biggest op, but for now that's true.
  static constexpr size_t PaintOpAlign = ALIGNOF(DrawDRRectOp);

  PaintOpBuffer();
  explicit PaintOpBuffer(const SkRect& cull_rect);
  ~PaintOpBuffer() override;

  void Reset();

  void playback(SkCanvas* canvas) const;
  void playback(SkCanvas* canvas, SkPicture::AbortCallback* callback) const;

  // Returns the size of the paint op buffer. That is, the number of ops
  // contained in it.
  size_t size() const { return op_count_; }
  // Returns the number of bytes used by the paint op buffer.
  size_t bytes_used() const {
    return sizeof(*this) + reserved_ + subrecord_bytes_used_;
  }
  int numSlowPaths() const { return num_slow_paths_; }
  bool HasDiscardableImages() const { return has_discardable_images_; }

  // Resize the PaintOpBuffer to exactly fit the current amount of used space.
  void ShrinkToFit();

  const SkRect& cullRect() const { return cull_rect_; }

  PaintOp* GetFirstOp() const {
    return const_cast<PaintOp*>(first_op_.data_as<PaintOp>());
  }

  template <typename T, typename... Args>
  void push(Args&&... args) {
    static_assert(std::is_convertible<T, PaintOp>::value, "T not a PaintOp.");
    static_assert(!std::is_convertible<T, PaintOpWithData>::value,
                  "Type needs to use push_with_data");
    push_internal<T>(0, std::forward<Args>(args)...);
  }

  template <typename T, typename... Args>
  void push_with_data(const void* data, size_t bytes, Args&&... args) {
    static_assert(std::is_convertible<T, PaintOpWithData>::value,
                  "T is not a PaintOpWithData");
    static_assert(!std::is_convertible<T, PaintOpWithArrayBase>::value,
                  "Type needs to use push_with_array");
    DCHECK_GE(bytes, 0u);
    T* op = push_internal<T>(bytes, bytes, std::forward<Args>(args)...);
    memcpy(op->GetData(), data, bytes);

#if DCHECK_IS_ON()
    // Double check the data fits between op and next op and doesn't clobber.
    char* op_start = reinterpret_cast<char*>(op);
    char* op_end = op_start + sizeof(T);
    char* next_op = op_start + op->skip;
    char* data_start = reinterpret_cast<char*>(op->GetData());
    char* data_end = data_start + bytes;
    DCHECK_GE(data_start, op_end);
    DCHECK_LT(data_start, next_op);
    DCHECK_LE(data_end, next_op);
#endif
  }

  template <typename T, typename M, typename... Args>
  void push_with_array(const void* data,
                       size_t bytes,
                       const M* array,
                       size_t count,
                       Args&&... args) {
    static_assert(std::is_convertible<T, PaintOpWithArray<M>>::value,
                  "T is not a PaintOpWithArray");
    DCHECK_GE(bytes, 0u);
    DCHECK_GE(count, 0u);
    size_t array_size = sizeof(M) * count;
    size_t total_size = bytes + array_size;
    T* op =
        push_internal<T>(total_size, bytes, count, std::forward<Args>(args)...);
    memcpy(op->GetData(), data, bytes);
    memcpy(op->GetArray(), array, array_size);

#if DCHECK_IS_ON()
    // Double check data and array don't clobber op, next op, or each other
    char* op_start = reinterpret_cast<char*>(op);
    char* op_end = op_start + sizeof(T);
    char* array_start = reinterpret_cast<char*>(op->GetArray());
    char* array_end = array_start + array_size;
    char* data_start = reinterpret_cast<char*>(op->GetData());
    char* data_end = data_start + bytes;
    char* next_op = op_start + op->skip;
    DCHECK_GE(array_start, op_end);
    DCHECK_LE(array_start, data_start);
    DCHECK_GE(data_start, array_end);
    DCHECK_LE(data_end, next_op);
#endif
  }

  class Iterator {
   public:
    explicit Iterator(const PaintOpBuffer* buffer)
        : buffer_(buffer), ptr_(buffer_->data_.get()) {}

    PaintOp* operator->() const {
      return op_idx_ ? reinterpret_cast<PaintOp*>(ptr_) : buffer_->GetFirstOp();
    }
    PaintOp* operator*() const { return operator->(); }
    Iterator begin() { return Iterator(buffer_, buffer_->data_.get(), 0); }
    Iterator end() {
      return Iterator(buffer_, buffer_->data_.get() + buffer_->used_,
                      buffer_->size());
    }
    bool operator!=(const Iterator& other) {
      // Not valid to compare iterators on different buffers.
      DCHECK_EQ(other.buffer_, buffer_);
      return other.op_idx_ != op_idx_;
    }
    Iterator& operator++() {
      if (!op_idx_++)
        return *this;
      PaintOp* op = **this;
      uint32_t type = op->type;
      CHECK_LE(type, static_cast<uint32_t>(PaintOpType::LastPaintOpType));
      ptr_ += op->skip;
      return *this;
    }
    operator bool() const { return op_idx_ < buffer_->size(); }

    size_t op_idx() const { return op_idx_; }

   private:
    Iterator(const PaintOpBuffer* buffer, char* ptr, size_t op_idx)
        : buffer_(buffer), ptr_(ptr), op_idx_(op_idx) {}

    const PaintOpBuffer* buffer_ = nullptr;
    char* ptr_ = nullptr;
    size_t op_idx_ = 0;
  };

 private:
  void ReallocBuffer(size_t new_size);
  // Returns the allocated op and the number of bytes to skip in |data_| to get
  // to the next op.
  std::pair<void*, size_t> AllocatePaintOp(size_t sizeof_op, size_t bytes);

  template <typename T, typename... Args>
  T* push_internal(size_t bytes, Args&&... args) {
    static_assert(ALIGNOF(T) <= PaintOpAlign, "");

    auto pair = AllocatePaintOp(sizeof(T), bytes);
    T* op = reinterpret_cast<T*>(pair.first);
    size_t skip = pair.second;

    new (op) T{std::forward<Args>(args)...};
    op->type = static_cast<uint32_t>(T::kType);
    op->skip = skip;
    AnalyzeAddedOp(op);
    return op;
  }

  template <typename T>
  void AnalyzeAddedOp(const T* op) {
    num_slow_paths_ += op->CountSlowPathsFromFlags();
    num_slow_paths_ += op->CountSlowPaths();

    has_discardable_images_ |= op->HasDiscardableImages();
    has_discardable_images_ |= op->HasDiscardableImagesFromFlags();

    subrecord_bytes_used_ += op->AdditionalBytesUsed();
  }

  // As a performance optimization because n=1 is an extremely common case just
  // store the first op in the PaintOpBuffer itself to avoid an extra alloc.
  base::AlignedMemory<sizeof(LargestPaintOp), PaintOpAlign> first_op_;
  std::unique_ptr<char, base::AlignedFreeDeleter> data_;
  size_t used_ = 0;
  size_t reserved_ = 0;
  size_t op_count_ = 0;

  // Record paths for veto-to-msaa for gpu raster.
  int num_slow_paths_ = 0;
  // Record additional bytes used by referenced sub-records and display lists.
  size_t subrecord_bytes_used_ = 0;
  bool has_discardable_images_ = false;
  SkRect cull_rect_;

  DISALLOW_COPY_AND_ASSIGN(PaintOpBuffer);
};

}  // namespace cc

#endif  // CC_PAINT_PAINT_OP_BUFFER_H_
