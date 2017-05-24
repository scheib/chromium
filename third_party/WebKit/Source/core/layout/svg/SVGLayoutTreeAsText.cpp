/*
 * Copyright (C) 2004, 2005, 2007, 2009 Apple Inc. All rights reserved.
 *           (C) 2005 Rob Buis <buis@kde.org>
 *           (C) 2006 Alexander Kellett <lypanov@kde.org>
 * Copyright (C) Research In Motion Limited 2010. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY APPLE COMPUTER, INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE COMPUTER, INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "core/layout/svg/SVGLayoutTreeAsText.h"

#include "core/layout/LayoutTreeAsText.h"
#include "core/layout/api/LineLayoutSVGInlineText.h"
#include "core/layout/line/InlineTextBox.h"
#include "core/layout/svg/LayoutSVGGradientStop.h"
#include "core/layout/svg/LayoutSVGImage.h"
#include "core/layout/svg/LayoutSVGInline.h"
#include "core/layout/svg/LayoutSVGResourceClipper.h"
#include "core/layout/svg/LayoutSVGResourceFilter.h"
#include "core/layout/svg/LayoutSVGResourceLinearGradient.h"
#include "core/layout/svg/LayoutSVGResourceMarker.h"
#include "core/layout/svg/LayoutSVGResourceMasker.h"
#include "core/layout/svg/LayoutSVGResourcePattern.h"
#include "core/layout/svg/LayoutSVGResourceRadialGradient.h"
#include "core/layout/svg/LayoutSVGRoot.h"
#include "core/layout/svg/LayoutSVGShape.h"
#include "core/layout/svg/LayoutSVGText.h"
#include "core/layout/svg/SVGLayoutSupport.h"
#include "core/layout/svg/SVGResources.h"
#include "core/layout/svg/SVGResourcesCache.h"
#include "core/layout/svg/line/SVGInlineTextBox.h"
#include "core/layout/svg/line/SVGRootInlineBox.h"
#include "core/paint/PaintLayer.h"
#include "core/svg/LinearGradientAttributes.h"
#include "core/svg/PatternAttributes.h"
#include "core/svg/RadialGradientAttributes.h"
#include "core/svg/SVGCircleElement.h"
#include "core/svg/SVGEllipseElement.h"
#include "core/svg/SVGFilterElement.h"
#include "core/svg/SVGLineElement.h"
#include "core/svg/SVGLinearGradientElement.h"
#include "core/svg/SVGPathElement.h"
#include "core/svg/SVGPathUtilities.h"
#include "core/svg/SVGPatternElement.h"
#include "core/svg/SVGPointList.h"
#include "core/svg/SVGPolyElement.h"
#include "core/svg/SVGRadialGradientElement.h"
#include "core/svg/SVGRectElement.h"
#include "core/svg/SVGStopElement.h"
#include "core/svg/graphics/filters/SVGFilterBuilder.h"
#include "platform/graphics/DashArray.h"
#include "platform/graphics/GraphicsTypes.h"
#include "platform/graphics/filters/Filter.h"
#include "platform/graphics/filters/SourceGraphic.h"

#include <math.h>
#include <memory>

namespace blink {

/** class + iomanip to help streaming list separators, i.e. ", " in string "a,
 * b, c, d"
 * Can be used in cases where you don't know which item in the list is the first
 * one to be printed, but still want to avoid strings like ", b, c".
 */
class TextStreamSeparator {
 public:
  TextStreamSeparator(const String& s)
      : separator_(s), need_to_separate_(false) {}

 private:
  friend TextStream& operator<<(TextStream&, TextStreamSeparator&);

  String separator_;
  bool need_to_separate_;
};

TextStream& operator<<(TextStream& ts, TextStreamSeparator& sep) {
  if (sep.need_to_separate_)
    ts << sep.separator_;
  else
    sep.need_to_separate_ = true;
  return ts;
}

template <typename ValueType>
static void WriteNameValuePair(TextStream& ts,
                               const char* name,
                               ValueType value) {
  ts << " [" << name << "=" << value << "]";
}

template <typename ValueType>
static void WriteNameAndQuotedValue(TextStream& ts,
                                    const char* name,
                                    ValueType value) {
  ts << " [" << name << "=\"" << value << "\"]";
}

static void WriteIfNotEmpty(TextStream& ts,
                            const char* name,
                            const String& value) {
  if (!value.IsEmpty())
    WriteNameValuePair(ts, name, value);
}

template <typename ValueType>
static void WriteIfNotDefault(TextStream& ts,
                              const char* name,
                              ValueType value,
                              ValueType default_value) {
  if (value != default_value)
    WriteNameValuePair(ts, name, value);
}

TextStream& operator<<(TextStream& ts, const AffineTransform& transform) {
  if (transform.IsIdentity()) {
    ts << "identity";
  } else {
    ts << "{m=((" << transform.A() << "," << transform.B() << ")("
       << transform.C() << "," << transform.D() << ")) t=(" << transform.E()
       << "," << transform.F() << ")}";
  }

  return ts;
}

static TextStream& operator<<(TextStream& ts, const WindRule rule) {
  switch (rule) {
    case RULE_NONZERO:
      ts << "NON-ZERO";
      break;
    case RULE_EVENODD:
      ts << "EVEN-ODD";
      break;
  }

  return ts;
}

namespace {

template <typename Enum>
String SVGEnumerationToString(Enum value) {
  const SVGEnumerationStringEntries& entries = GetStaticStringEntries<Enum>();

  SVGEnumerationStringEntries::const_iterator it = entries.begin();
  SVGEnumerationStringEntries::const_iterator it_end = entries.end();
  for (; it != it_end; ++it) {
    if (value == it->first)
      return it->second;
  }

  NOTREACHED();
  return String();
}

}  // namespace

static TextStream& operator<<(TextStream& ts,
                              const SVGUnitTypes::SVGUnitType& unit_type) {
  ts << SVGEnumerationToString<SVGUnitTypes::SVGUnitType>(unit_type);
  return ts;
}

static TextStream& operator<<(TextStream& ts,
                              const SVGMarkerUnitsType& marker_unit) {
  ts << SVGEnumerationToString<SVGMarkerUnitsType>(marker_unit);
  return ts;
}

static TextStream& operator<<(TextStream& ts,
                              const SVGMarkerOrientType& orient_type) {
  ts << SVGEnumerationToString<SVGMarkerOrientType>(orient_type);
  return ts;
}

// FIXME: Maybe this should be in DashArray.cpp
static TextStream& operator<<(TextStream& ts, const DashArray& a) {
  ts << "{";
  DashArray::const_iterator end = a.end();
  for (DashArray::const_iterator it = a.begin(); it != end; ++it) {
    if (it != a.begin())
      ts << ", ";
    ts << *it;
  }
  ts << "}";
  return ts;
}

// FIXME: Maybe this should be in GraphicsTypes.cpp
static TextStream& operator<<(TextStream& ts, LineCap style) {
  switch (style) {
    case kButtCap:
      ts << "BUTT";
      break;
    case kRoundCap:
      ts << "ROUND";
      break;
    case kSquareCap:
      ts << "SQUARE";
      break;
  }
  return ts;
}

// FIXME: Maybe this should be in GraphicsTypes.cpp
static TextStream& operator<<(TextStream& ts, LineJoin style) {
  switch (style) {
    case kMiterJoin:
      ts << "MITER";
      break;
    case kRoundJoin:
      ts << "ROUND";
      break;
    case kBevelJoin:
      ts << "BEVEL";
      break;
  }
  return ts;
}

static TextStream& operator<<(TextStream& ts, const SVGSpreadMethodType& type) {
  ts << SVGEnumerationToString<SVGSpreadMethodType>(type).UpperASCII();
  return ts;
}

static void WriteSVGPaintingResource(
    TextStream& ts,
    const SVGPaintDescription& paint_description) {
  DCHECK(paint_description.is_valid);
  if (!paint_description.resource) {
    ts << "[type=SOLID] [color=" << paint_description.color << "]";
    return;
  }

  LayoutSVGResourcePaintServer* paint_server_container =
      paint_description.resource;
  SVGElement* element = paint_server_container->GetElement();
  DCHECK(element);

  if (paint_server_container->ResourceType() == kPatternResourceType)
    ts << "[type=PATTERN]";
  else if (paint_server_container->ResourceType() ==
           kLinearGradientResourceType)
    ts << "[type=LINEAR-GRADIENT]";
  else if (paint_server_container->ResourceType() ==
           kRadialGradientResourceType)
    ts << "[type=RADIAL-GRADIENT]";

  ts << " [id=\"" << element->GetIdAttribute() << "\"]";
}

static void WriteStyle(TextStream& ts, const LayoutObject& object) {
  const ComputedStyle& style = object.StyleRef();
  const SVGComputedStyle& svg_style = style.SvgStyle();

  if (!object.LocalSVGTransform().IsIdentity())
    WriteNameValuePair(ts, "transform", object.LocalSVGTransform());
  WriteIfNotDefault(ts, "image rendering", style.ImageRendering(),
                    ComputedStyle::InitialImageRendering());
  WriteIfNotDefault(ts, "opacity", style.Opacity(),
                    ComputedStyle::InitialOpacity());
  if (object.IsSVGShape()) {
    const LayoutSVGShape& shape = static_cast<const LayoutSVGShape&>(object);
    DCHECK(shape.GetElement());

    SVGPaintDescription stroke_paint_description =
        LayoutSVGResourcePaintServer::RequestPaintDescription(
            shape, shape.StyleRef(), kApplyToStrokeMode);
    if (stroke_paint_description.is_valid) {
      TextStreamSeparator s(" ");
      ts << " [stroke={" << s;
      WriteSVGPaintingResource(ts, stroke_paint_description);

      SVGLengthContext length_context(shape.GetElement());
      double dash_offset =
          length_context.ValueForLength(svg_style.StrokeDashOffset(), style);
      double stroke_width =
          length_context.ValueForLength(svg_style.StrokeWidth());
      DashArray dash_array = SVGLayoutSupport::ResolveSVGDashArray(
          *svg_style.StrokeDashArray(), style, length_context);

      WriteIfNotDefault(ts, "opacity", svg_style.StrokeOpacity(), 1.0f);
      WriteIfNotDefault(ts, "stroke width", stroke_width, 1.0);
      WriteIfNotDefault(ts, "miter limit", svg_style.StrokeMiterLimit(), 4.0f);
      WriteIfNotDefault(ts, "line cap", svg_style.CapStyle(), kButtCap);
      WriteIfNotDefault(ts, "line join", svg_style.JoinStyle(), kMiterJoin);
      WriteIfNotDefault(ts, "dash offset", dash_offset, 0.0);
      if (!dash_array.IsEmpty())
        WriteNameValuePair(ts, "dash array", dash_array);

      ts << "}]";
    }

    SVGPaintDescription fill_paint_description =
        LayoutSVGResourcePaintServer::RequestPaintDescription(
            shape, shape.StyleRef(), kApplyToFillMode);
    if (fill_paint_description.is_valid) {
      TextStreamSeparator s(" ");
      ts << " [fill={" << s;
      WriteSVGPaintingResource(ts, fill_paint_description);

      WriteIfNotDefault(ts, "opacity", svg_style.FillOpacity(), 1.0f);
      WriteIfNotDefault(ts, "fill rule", svg_style.FillRule(), RULE_NONZERO);
      ts << "}]";
    }
    WriteIfNotDefault(ts, "clip rule", svg_style.ClipRule(), RULE_NONZERO);
  }

  WriteIfNotEmpty(ts, "start marker", svg_style.MarkerStartResource());
  WriteIfNotEmpty(ts, "middle marker", svg_style.MarkerMidResource());
  WriteIfNotEmpty(ts, "end marker", svg_style.MarkerEndResource());
}

static TextStream& WritePositionAndStyle(TextStream& ts,
                                         const LayoutObject& object) {
  ts << " " << object.ObjectBoundingBox();
  WriteStyle(ts, object);
  return ts;
}

static TextStream& operator<<(TextStream& ts, const LayoutSVGShape& shape) {
  WritePositionAndStyle(ts, shape);

  SVGElement* svg_element = shape.GetElement();
  DCHECK(svg_element);
  SVGLengthContext length_context(svg_element);
  const ComputedStyle& style = shape.StyleRef();
  const SVGComputedStyle& svg_style = style.SvgStyle();

  if (isSVGRectElement(*svg_element)) {
    WriteNameValuePair(ts, "x",
                       length_context.ValueForLength(svg_style.X(), style,
                                                     SVGLengthMode::kWidth));
    WriteNameValuePair(ts, "y",
                       length_context.ValueForLength(svg_style.Y(), style,
                                                     SVGLengthMode::kHeight));
    WriteNameValuePair(ts, "width",
                       length_context.ValueForLength(style.Width(), style,
                                                     SVGLengthMode::kWidth));
    WriteNameValuePair(ts, "height",
                       length_context.ValueForLength(style.Height(), style,
                                                     SVGLengthMode::kHeight));
  } else if (isSVGLineElement(*svg_element)) {
    SVGLineElement& element = toSVGLineElement(*svg_element);
    WriteNameValuePair(ts, "x1",
                       element.x1()->CurrentValue()->Value(length_context));
    WriteNameValuePair(ts, "y1",
                       element.y1()->CurrentValue()->Value(length_context));
    WriteNameValuePair(ts, "x2",
                       element.x2()->CurrentValue()->Value(length_context));
    WriteNameValuePair(ts, "y2",
                       element.y2()->CurrentValue()->Value(length_context));
  } else if (isSVGEllipseElement(*svg_element)) {
    WriteNameValuePair(ts, "cx",
                       length_context.ValueForLength(svg_style.Cx(), style,
                                                     SVGLengthMode::kWidth));
    WriteNameValuePair(ts, "cy",
                       length_context.ValueForLength(svg_style.Cy(), style,
                                                     SVGLengthMode::kHeight));
    WriteNameValuePair(ts, "rx",
                       length_context.ValueForLength(svg_style.Rx(), style,
                                                     SVGLengthMode::kWidth));
    WriteNameValuePair(ts, "ry",
                       length_context.ValueForLength(svg_style.Ry(), style,
                                                     SVGLengthMode::kHeight));
  } else if (isSVGCircleElement(*svg_element)) {
    WriteNameValuePair(ts, "cx",
                       length_context.ValueForLength(svg_style.Cx(), style,
                                                     SVGLengthMode::kWidth));
    WriteNameValuePair(ts, "cy",
                       length_context.ValueForLength(svg_style.Cy(), style,
                                                     SVGLengthMode::kHeight));
    WriteNameValuePair(ts, "r",
                       length_context.ValueForLength(svg_style.R(), style,
                                                     SVGLengthMode::kOther));
  } else if (IsSVGPolyElement(*svg_element)) {
    WriteNameAndQuotedValue(ts, "points",
                            ToSVGPolyElement(*svg_element)
                                .Points()
                                ->CurrentValue()
                                ->ValueAsString());
  } else if (isSVGPathElement(*svg_element)) {
    const StylePath& path =
        svg_style.D() ? *svg_style.D() : *StylePath::EmptyPath();
    WriteNameAndQuotedValue(ts, "data",
                            BuildStringFromByteStream(path.ByteStream()));
  } else {
    NOTREACHED();
  }
  return ts;
}

static TextStream& operator<<(TextStream& ts, const LayoutSVGRoot& root) {
  ts << " " << root.FrameRect();
  WriteStyle(ts, root);
  return ts;
}

static void WriteLayoutSVGTextBox(TextStream& ts, const LayoutSVGText& text) {
  SVGRootInlineBox* box = ToSVGRootInlineBox(text.FirstRootBox());
  if (!box)
    return;

  // FIXME: Remove this hack, once the new text layout engine is completly
  // landed. We want to preserve the old layout test results for now.
  ts << " contains 1 chunk(s)";

  if (text.Parent() && (text.Parent()->ResolveColor(CSSPropertyColor) !=
                        text.ResolveColor(CSSPropertyColor))) {
    WriteNameValuePair(
        ts, "color",
        text.ResolveColor(CSSPropertyColor).NameForLayoutTreeAsText());
  }
}

static inline void WriteSVGInlineTextBox(TextStream& ts,
                                         SVGInlineTextBox* text_box,
                                         int indent) {
  Vector<SVGTextFragment>& fragments = text_box->TextFragments();
  if (fragments.IsEmpty())
    return;

  LineLayoutSVGInlineText text_line_layout =
      LineLayoutSVGInlineText(text_box->GetLineLayoutItem());

  const SVGComputedStyle& svg_style = text_line_layout.Style()->SvgStyle();
  String text = text_box->GetLineLayoutItem().GetText();

  unsigned fragments_size = fragments.size();
  for (unsigned i = 0; i < fragments_size; ++i) {
    SVGTextFragment& fragment = fragments.at(i);
    WriteIndent(ts, indent + 1);

    unsigned start_offset = fragment.character_offset;
    unsigned end_offset = fragment.character_offset + fragment.length;

    // FIXME: Remove this hack, once the new text layout engine is completly
    // landed. We want to preserve the old layout test results for now.
    ts << "chunk 1 ";
    ETextAnchor anchor = svg_style.TextAnchor();
    bool is_vertical_text =
        !text_line_layout.Style()->IsHorizontalWritingMode();
    if (anchor == TA_MIDDLE) {
      ts << "(middle anchor";
      if (is_vertical_text)
        ts << ", vertical";
      ts << ") ";
    } else if (anchor == TA_END) {
      ts << "(end anchor";
      if (is_vertical_text)
        ts << ", vertical";
      ts << ") ";
    } else if (is_vertical_text) {
      ts << "(vertical) ";
    }
    start_offset -= text_box->Start();
    end_offset -= text_box->Start();
    // </hack>

    ts << "text run " << i + 1 << " at (" << fragment.x << "," << fragment.y
       << ")";
    ts << " startOffset " << start_offset << " endOffset " << end_offset;
    if (is_vertical_text)
      ts << " height " << fragment.height;
    else
      ts << " width " << fragment.width;

    if (!text_box->IsLeftToRightDirection() || text_box->DirOverride()) {
      ts << (text_box->IsLeftToRightDirection() ? " LTR" : " RTL");
      if (text_box->DirOverride())
        ts << " override";
    }

    ts << ": "
       << QuoteAndEscapeNonPrintables(
              text.Substring(fragment.character_offset, fragment.length))
       << "\n";
  }
}

static inline void WriteSVGInlineTextBoxes(TextStream& ts,
                                           const LayoutText& text,
                                           int indent) {
  for (InlineTextBox* box = text.FirstTextBox(); box;
       box = box->NextTextBox()) {
    if (!box->IsSVGInlineTextBox())
      continue;

    WriteSVGInlineTextBox(ts, ToSVGInlineTextBox(box), indent);
  }
}

static void WriteStandardPrefix(TextStream& ts,
                                const LayoutObject& object,
                                int indent) {
  WriteIndent(ts, indent);
  ts << object.DecoratedName();

  if (object.GetNode())
    ts << " {" << object.GetNode()->nodeName() << "}";
}

static void WriteChildren(TextStream& ts,
                          const LayoutObject& object,
                          int indent) {
  for (LayoutObject* child = object.SlowFirstChild(); child;
       child = child->NextSibling())
    Write(ts, *child, indent + 1);
}

static inline void WriteCommonGradientProperties(
    TextStream& ts,
    SVGSpreadMethodType spread_method,
    const AffineTransform& gradient_transform,
    SVGUnitTypes::SVGUnitType gradient_units) {
  WriteNameValuePair(ts, "gradientUnits", gradient_units);

  if (spread_method != kSVGSpreadMethodPad)
    ts << " [spreadMethod=" << spread_method << "]";

  if (!gradient_transform.IsIdentity())
    ts << " [gradientTransform=" << gradient_transform << "]";
}

void WriteSVGResourceContainer(TextStream& ts,
                               const LayoutObject& object,
                               int indent) {
  WriteStandardPrefix(ts, object, indent);

  Element* element = ToElement(object.GetNode());
  const AtomicString& id = element->GetIdAttribute();
  WriteNameAndQuotedValue(ts, "id", id);

  LayoutSVGResourceContainer* resource =
      ToLayoutSVGResourceContainer(const_cast<LayoutObject*>(&object));
  DCHECK(resource);

  if (resource->ResourceType() == kMaskerResourceType) {
    LayoutSVGResourceMasker* masker = ToLayoutSVGResourceMasker(resource);
    WriteNameValuePair(ts, "maskUnits", masker->MaskUnits());
    WriteNameValuePair(ts, "maskContentUnits", masker->MaskContentUnits());
    ts << "\n";
  } else if (resource->ResourceType() == kFilterResourceType) {
    LayoutSVGResourceFilter* filter = ToLayoutSVGResourceFilter(resource);
    WriteNameValuePair(ts, "filterUnits", filter->FilterUnits());
    WriteNameValuePair(ts, "primitiveUnits", filter->PrimitiveUnits());
    ts << "\n";
    // Creating a placeholder filter which is passed to the builder.
    FloatRect dummy_rect;
    Filter* dummy_filter =
        Filter::Create(dummy_rect, dummy_rect, 1, Filter::kBoundingBox);
    SVGFilterBuilder builder(dummy_filter->GetSourceGraphic());
    builder.BuildGraph(dummy_filter, toSVGFilterElement(*filter->GetElement()),
                       dummy_rect);
    if (FilterEffect* last_effect = builder.LastEffect())
      last_effect->ExternalRepresentation(ts, indent + 1);
  } else if (resource->ResourceType() == kClipperResourceType) {
    WriteNameValuePair(ts, "clipPathUnits",
                       ToLayoutSVGResourceClipper(resource)->ClipPathUnits());
    ts << "\n";
  } else if (resource->ResourceType() == kMarkerResourceType) {
    LayoutSVGResourceMarker* marker = ToLayoutSVGResourceMarker(resource);
    WriteNameValuePair(ts, "markerUnits", marker->MarkerUnits());
    ts << " [ref at " << marker->ReferencePoint() << "]";
    ts << " [angle=";
    if (marker->OrientType() != kSVGMarkerOrientAngle)
      ts << marker->OrientType() << "]\n";
    else
      ts << marker->Angle() << "]\n";
  } else if (resource->ResourceType() == kPatternResourceType) {
    LayoutSVGResourcePattern* pattern =
        static_cast<LayoutSVGResourcePattern*>(resource);

    // Dump final results that are used for layout. No use in asking
    // SVGPatternElement for its patternUnits(), as it may link to other
    // patterns using xlink:href, we need to build the full inheritance chain,
    // aka. collectPatternProperties()
    PatternAttributes attributes;
    toSVGPatternElement(pattern->GetElement())
        ->CollectPatternAttributes(attributes);

    WriteNameValuePair(ts, "patternUnits", attributes.PatternUnits());
    WriteNameValuePair(ts, "patternContentUnits",
                       attributes.PatternContentUnits());

    AffineTransform transform = attributes.PatternTransform();
    if (!transform.IsIdentity())
      ts << " [patternTransform=" << transform << "]";
    ts << "\n";
  } else if (resource->ResourceType() == kLinearGradientResourceType) {
    LayoutSVGResourceLinearGradient* gradient =
        static_cast<LayoutSVGResourceLinearGradient*>(resource);

    // Dump final results that are used for layout. No use in asking
    // SVGGradientElement for its gradientUnits(), as it may link to other
    // gradients using xlink:href, we need to build the full inheritance chain,
    // aka. collectGradientProperties()
    LinearGradientAttributes attributes;
    toSVGLinearGradientElement(gradient->GetElement())
        ->CollectGradientAttributes(attributes);
    WriteCommonGradientProperties(ts, attributes.SpreadMethod(),
                                  attributes.GradientTransform(),
                                  attributes.GradientUnits());

    ts << " [start=" << gradient->StartPoint(attributes)
       << "] [end=" << gradient->EndPoint(attributes) << "]\n";
  } else if (resource->ResourceType() == kRadialGradientResourceType) {
    LayoutSVGResourceRadialGradient* gradient =
        ToLayoutSVGResourceRadialGradient(resource);

    // Dump final results that are used for layout. No use in asking
    // SVGGradientElement for its gradientUnits(), as it may link to other
    // gradients using xlink:href, we need to build the full inheritance chain,
    // aka. collectGradientProperties()
    RadialGradientAttributes attributes;
    toSVGRadialGradientElement(gradient->GetElement())
        ->CollectGradientAttributes(attributes);
    WriteCommonGradientProperties(ts, attributes.SpreadMethod(),
                                  attributes.GradientTransform(),
                                  attributes.GradientUnits());

    FloatPoint focal_point = gradient->FocalPoint(attributes);
    FloatPoint center_point = gradient->CenterPoint(attributes);
    float radius = gradient->Radius(attributes);
    float focal_radius = gradient->FocalRadius(attributes);

    ts << " [center=" << center_point << "] [focal=" << focal_point
       << "] [radius=" << radius << "] [focalRadius=" << focal_radius << "]\n";
  } else {
    ts << "\n";
  }
  WriteChildren(ts, object, indent);
}

void WriteSVGContainer(TextStream& ts,
                       const LayoutObject& container,
                       int indent) {
  // Currently LayoutSVGResourceFilterPrimitive has no meaningful output.
  if (container.IsSVGResourceFilterPrimitive())
    return;
  WriteStandardPrefix(ts, container, indent);
  WritePositionAndStyle(ts, container);
  ts << "\n";
  WriteResources(ts, container, indent);
  WriteChildren(ts, container, indent);
}

void Write(TextStream& ts, const LayoutSVGRoot& root, int indent) {
  WriteStandardPrefix(ts, root, indent);
  ts << root << "\n";
  WriteChildren(ts, root, indent);
}

void WriteSVGText(TextStream& ts, const LayoutSVGText& text, int indent) {
  WriteStandardPrefix(ts, text, indent);
  WritePositionAndStyle(ts, text);
  WriteLayoutSVGTextBox(ts, text);
  ts << "\n";
  WriteResources(ts, text, indent);
  WriteChildren(ts, text, indent);
}

void WriteSVGInline(TextStream& ts, const LayoutSVGInline& text, int indent) {
  WriteStandardPrefix(ts, text, indent);
  WritePositionAndStyle(ts, text);
  ts << "\n";
  WriteResources(ts, text, indent);
  WriteChildren(ts, text, indent);
}

void WriteSVGInlineText(TextStream& ts,
                        const LayoutSVGInlineText& text,
                        int indent) {
  WriteStandardPrefix(ts, text, indent);
  WritePositionAndStyle(ts, text);
  ts << "\n";
  WriteResources(ts, text, indent);
  WriteSVGInlineTextBoxes(ts, text, indent);
}

void WriteSVGImage(TextStream& ts, const LayoutSVGImage& image, int indent) {
  WriteStandardPrefix(ts, image, indent);
  WritePositionAndStyle(ts, image);
  ts << "\n";
  WriteResources(ts, image, indent);
}

void Write(TextStream& ts, const LayoutSVGShape& shape, int indent) {
  WriteStandardPrefix(ts, shape, indent);
  ts << shape << "\n";
  WriteResources(ts, shape, indent);
}

void WriteSVGGradientStop(TextStream& ts,
                          const LayoutSVGGradientStop& stop,
                          int indent) {
  WriteStandardPrefix(ts, stop, indent);

  SVGStopElement* stop_element = toSVGStopElement(stop.GetNode());
  DCHECK(stop_element);
  DCHECK(stop.Style());

  ts << " [offset=" << stop_element->offset()->CurrentValue()->Value()
     << "] [color=" << stop_element->StopColorIncludingOpacity() << "]\n";
}

void WriteResources(TextStream& ts, const LayoutObject& object, int indent) {
  SVGResources* resources =
      SVGResourcesCache::CachedResourcesForLayoutObject(&object);
  if (!resources)
    return;
  const ComputedStyle& style = object.StyleRef();
  TreeScope& tree_scope = object.GetDocument();
  if (LayoutSVGResourceMasker* masker = resources->Masker()) {
    WriteIndent(ts, indent);
    ts << " ";
    WriteNameAndQuotedValue(ts, "masker", style.SvgStyle().MaskerResource());
    ts << " ";
    WriteStandardPrefix(ts, *masker, 0);
    ts << " " << masker->ResourceBoundingBox(&object) << "\n";
  }
  if (LayoutSVGResourceClipper* clipper = resources->Clipper()) {
    DCHECK(style.ClipPath());
    DCHECK_EQ(style.ClipPath()->GetType(), ClipPathOperation::REFERENCE);
    const ReferenceClipPathOperation& clip_path_reference =
        ToReferenceClipPathOperation(*style.ClipPath());
    AtomicString id = SVGURIReference::FragmentIdentifierFromIRIString(
        clip_path_reference.Url(), tree_scope);
    WriteIndent(ts, indent);
    ts << " ";
    WriteNameAndQuotedValue(ts, "clipPath", id);
    ts << " ";
    WriteStandardPrefix(ts, *clipper, 0);
    ts << " " << clipper->ResourceBoundingBox(object.ObjectBoundingBox())
       << "\n";
  }
  if (LayoutSVGResourceFilter* filter = resources->Filter()) {
    DCHECK(style.HasFilter());
    DCHECK_EQ(style.Filter().size(), 1u);
    const FilterOperation& filter_operation = *style.Filter().at(0);
    DCHECK_EQ(filter_operation.GetType(), FilterOperation::REFERENCE);
    const auto& reference_filter_operation =
        ToReferenceFilterOperation(filter_operation);
    AtomicString id = SVGURIReference::FragmentIdentifierFromIRIString(
        reference_filter_operation.Url(), tree_scope);
    WriteIndent(ts, indent);
    ts << " ";
    WriteNameAndQuotedValue(ts, "filter", id);
    ts << " ";
    WriteStandardPrefix(ts, *filter, 0);
    ts << " " << filter->ResourceBoundingBox(&object) << "\n";
  }
}

}  // namespace blink
