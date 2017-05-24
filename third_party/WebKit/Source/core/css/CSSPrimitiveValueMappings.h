/*
 * Copyright (C) 2007 Alexey Proskuryakov <ap@nypop.com>.
 * Copyright (C) 2008, 2009, 2010, 2011 Apple Inc. All rights reserved.
 * Copyright (C) 2009 Torch Mobile Inc. All rights reserved.
 * (http://www.torchmobile.com/)
 * Copyright (C) 2009 Jeff Schiller <codedread@gmail.com>
 * Copyright (C) Research In Motion Limited 2010. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef CSSPrimitiveValueMappings_h
#define CSSPrimitiveValueMappings_h

#include "core/CSSValueKeywords.h"
#include "core/css/CSSCalculationValue.h"
#include "core/css/CSSIdentifierValue.h"
#include "core/css/CSSPrimitiveValue.h"
#include "core/css/CSSReflectionDirection.h"
#include "core/css/CSSToLengthConversionData.h"
#include "core/style/ComputedStyleConstants.h"
#include "core/style/LineClampValue.h"
#include "core/style/SVGComputedStyleDefs.h"
#include "platform/Length.h"
#include "platform/ThemeTypes.h"
#include "platform/fonts/FontDescription.h"
#include "platform/fonts/FontSmoothingMode.h"
#include "platform/fonts/TextRenderingMode.h"
#include "platform/graphics/GraphicsTypes.h"
#include "platform/graphics/TouchAction.h"
#include "platform/scroll/ScrollableArea.h"
#include "platform/text/TextRun.h"
#include "platform/text/WritingMode.h"
#include "platform/wtf/MathExtras.h"
#include "public/platform/WebBlendMode.h"

namespace blink {

// TODO(sashab): Move these to CSSPrimitiveValue.h.
template <>
inline short CSSPrimitiveValue::ConvertTo() const {
  DCHECK(IsNumber());
  return clampTo<short>(GetDoubleValue());
}

template <>
inline unsigned short CSSPrimitiveValue::ConvertTo() const {
  DCHECK(IsNumber());
  return clampTo<unsigned short>(GetDoubleValue());
}

template <>
inline int CSSPrimitiveValue::ConvertTo() const {
  DCHECK(IsNumber());
  return clampTo<int>(GetDoubleValue());
}

template <>
inline unsigned CSSPrimitiveValue::ConvertTo() const {
  DCHECK(IsNumber());
  return clampTo<unsigned>(GetDoubleValue());
}

template <>
inline float CSSPrimitiveValue::ConvertTo() const {
  DCHECK(IsNumber());
  return clampTo<float>(GetDoubleValue());
}

template <>
inline CSSPrimitiveValue::CSSPrimitiveValue(LineClampValue i)
    : CSSValue(kPrimitiveClass) {
  Init(i.IsPercentage() ? UnitType::kPercentage : UnitType::kInteger);
  value_.num = static_cast<double>(i.Value());
}

template <>
inline LineClampValue CSSPrimitiveValue::ConvertTo() const {
  if (GetType() == UnitType::kInteger)
    return LineClampValue(clampTo<int>(value_.num), kLineClampLineCount);

  if (GetType() == UnitType::kPercentage)
    return LineClampValue(clampTo<int>(value_.num), kLineClampPercentage);

  NOTREACHED();
  return LineClampValue();
}

// TODO(sashab): Move these to CSSIdentifierValueMappings.h, and update to use
// the CSSValuePool.
template <>
inline CSSIdentifierValue::CSSIdentifierValue(CSSReflectionDirection e)
    : CSSValue(kIdentifierClass) {
  switch (e) {
    case kReflectionAbove:
      value_id_ = CSSValueAbove;
      break;
    case kReflectionBelow:
      value_id_ = CSSValueBelow;
      break;
    case kReflectionLeft:
      value_id_ = CSSValueLeft;
      break;
    case kReflectionRight:
      value_id_ = CSSValueRight;
  }
}

template <>
inline CSSReflectionDirection CSSIdentifierValue::ConvertTo() const {
  switch (value_id_) {
    case CSSValueAbove:
      return kReflectionAbove;
    case CSSValueBelow:
      return kReflectionBelow;
    case CSSValueLeft:
      return kReflectionLeft;
    case CSSValueRight:
      return kReflectionRight;
    default:
      break;
  }

  NOTREACHED();
  return kReflectionBelow;
}

template <>
inline CSSIdentifierValue::CSSIdentifierValue(ColumnFill column_fill)
    : CSSValue(kIdentifierClass) {
  switch (column_fill) {
    case kColumnFillAuto:
      value_id_ = CSSValueAuto;
      break;
    case kColumnFillBalance:
      value_id_ = CSSValueBalance;
      break;
  }
}

template <>
inline ColumnFill CSSIdentifierValue::ConvertTo() const {
  if (value_id_ == CSSValueBalance)
    return kColumnFillBalance;
  if (value_id_ == CSSValueAuto)
    return kColumnFillAuto;
  NOTREACHED();
  return kColumnFillBalance;
}

template <>
inline CSSIdentifierValue::CSSIdentifierValue(ColumnSpan column_span)
    : CSSValue(kIdentifierClass) {
  switch (column_span) {
    case kColumnSpanAll:
      value_id_ = CSSValueAll;
      break;
    case kColumnSpanNone:
      value_id_ = CSSValueNone;
      break;
  }
}

template <>
inline ColumnSpan CSSIdentifierValue::ConvertTo() const {
  switch (value_id_) {
    case CSSValueAll:
      return kColumnSpanAll;
    default:
      NOTREACHED();
    // fall-through
    case CSSValueNone:
      return kColumnSpanNone;
  }
}

template <>
inline EBorderStyle CSSIdentifierValue::ConvertTo() const {
  if (value_id_ == CSSValueAuto)  // Valid for CSS outline-style
    return EBorderStyle::kDotted;
  return detail::cssValueIDToPlatformEnumGenerated<EBorderStyle>(value_id_);
}

template <>
inline OutlineIsAuto CSSIdentifierValue::ConvertTo() const {
  if (value_id_ == CSSValueAuto)
    return kOutlineIsAutoOn;
  return kOutlineIsAutoOff;
}

template <>
inline CSSIdentifierValue::CSSIdentifierValue(CompositeOperator e)
    : CSSValue(kIdentifierClass) {
  switch (e) {
    case kCompositeClear:
      value_id_ = CSSValueClear;
      break;
    case kCompositeCopy:
      value_id_ = CSSValueCopy;
      break;
    case kCompositeSourceOver:
      value_id_ = CSSValueSourceOver;
      break;
    case kCompositeSourceIn:
      value_id_ = CSSValueSourceIn;
      break;
    case kCompositeSourceOut:
      value_id_ = CSSValueSourceOut;
      break;
    case kCompositeSourceAtop:
      value_id_ = CSSValueSourceAtop;
      break;
    case kCompositeDestinationOver:
      value_id_ = CSSValueDestinationOver;
      break;
    case kCompositeDestinationIn:
      value_id_ = CSSValueDestinationIn;
      break;
    case kCompositeDestinationOut:
      value_id_ = CSSValueDestinationOut;
      break;
    case kCompositeDestinationAtop:
      value_id_ = CSSValueDestinationAtop;
      break;
    case kCompositeXOR:
      value_id_ = CSSValueXor;
      break;
    case kCompositePlusLighter:
      value_id_ = CSSValuePlusLighter;
      break;
    default:
      NOTREACHED();
      break;
  }
}

template <>
inline CompositeOperator CSSIdentifierValue::ConvertTo() const {
  switch (value_id_) {
    case CSSValueClear:
      return kCompositeClear;
    case CSSValueCopy:
      return kCompositeCopy;
    case CSSValueSourceOver:
      return kCompositeSourceOver;
    case CSSValueSourceIn:
      return kCompositeSourceIn;
    case CSSValueSourceOut:
      return kCompositeSourceOut;
    case CSSValueSourceAtop:
      return kCompositeSourceAtop;
    case CSSValueDestinationOver:
      return kCompositeDestinationOver;
    case CSSValueDestinationIn:
      return kCompositeDestinationIn;
    case CSSValueDestinationOut:
      return kCompositeDestinationOut;
    case CSSValueDestinationAtop:
      return kCompositeDestinationAtop;
    case CSSValueXor:
      return kCompositeXOR;
    case CSSValuePlusLighter:
      return kCompositePlusLighter;
    default:
      break;
  }

  NOTREACHED();
  return kCompositeClear;
}

template <>
inline CSSIdentifierValue::CSSIdentifierValue(ControlPart e)
    : CSSValue(kIdentifierClass) {
  switch (e) {
    case kNoControlPart:
      value_id_ = CSSValueNone;
      break;
    case kCheckboxPart:
      value_id_ = CSSValueCheckbox;
      break;
    case kRadioPart:
      value_id_ = CSSValueRadio;
      break;
    case kPushButtonPart:
      value_id_ = CSSValuePushButton;
      break;
    case kSquareButtonPart:
      value_id_ = CSSValueSquareButton;
      break;
    case kButtonPart:
      value_id_ = CSSValueButton;
      break;
    case kButtonBevelPart:
      value_id_ = CSSValueButtonBevel;
      break;
    case kInnerSpinButtonPart:
      value_id_ = CSSValueInnerSpinButton;
      break;
    case kListboxPart:
      value_id_ = CSSValueListbox;
      break;
    case kListItemPart:
      value_id_ = CSSValueListitem;
      break;
    case kMediaEnterFullscreenButtonPart:
      value_id_ = CSSValueMediaEnterFullscreenButton;
      break;
    case kMediaExitFullscreenButtonPart:
      value_id_ = CSSValueMediaExitFullscreenButton;
      break;
    case kMediaPlayButtonPart:
      value_id_ = CSSValueMediaPlayButton;
      break;
    case kMediaOverlayPlayButtonPart:
      value_id_ = CSSValueMediaOverlayPlayButton;
      break;
    case kMediaMuteButtonPart:
      value_id_ = CSSValueMediaMuteButton;
      break;
    case kMediaToggleClosedCaptionsButtonPart:
      value_id_ = CSSValueMediaToggleClosedCaptionsButton;
      break;
    case kMediaCastOffButtonPart:
      value_id_ = CSSValueInternalMediaCastOffButton;
      break;
    case kMediaOverlayCastOffButtonPart:
      value_id_ = CSSValueInternalMediaOverlayCastOffButton;
      break;
    case kMediaSliderPart:
      value_id_ = CSSValueMediaSlider;
      break;
    case kMediaSliderThumbPart:
      value_id_ = CSSValueMediaSliderthumb;
      break;
    case kMediaVolumeSliderContainerPart:
      value_id_ = CSSValueMediaVolumeSliderContainer;
      break;
    case kMediaVolumeSliderPart:
      value_id_ = CSSValueMediaVolumeSlider;
      break;
    case kMediaVolumeSliderThumbPart:
      value_id_ = CSSValueMediaVolumeSliderthumb;
      break;
    case kMediaControlsBackgroundPart:
      value_id_ = CSSValueMediaControlsBackground;
      break;
    case kMediaControlsFullscreenBackgroundPart:
      value_id_ = CSSValueMediaControlsFullscreenBackground;
      break;
    case kMediaCurrentTimePart:
      value_id_ = CSSValueMediaCurrentTimeDisplay;
      break;
    case kMediaTimeRemainingPart:
      value_id_ = CSSValueMediaTimeRemainingDisplay;
      break;
    case kMediaTrackSelectionCheckmarkPart:
      value_id_ = CSSValueInternalMediaTrackSelectionCheckmark;
      break;
    case kMediaClosedCaptionsIconPart:
      value_id_ = CSSValueInternalMediaClosedCaptionsIcon;
      break;
    case kMediaSubtitlesIconPart:
      value_id_ = CSSValueInternalMediaSubtitlesIcon;
      break;
    case kMediaOverflowMenuButtonPart:
      value_id_ = CSSValueInternalMediaOverflowButton;
      break;
    case kMediaDownloadIconPart:
      value_id_ = CSSValueInternalMediaDownloadButton;
      break;
    case kMenulistPart:
      value_id_ = CSSValueMenulist;
      break;
    case kMenulistButtonPart:
      value_id_ = CSSValueMenulistButton;
      break;
    case kMenulistTextPart:
      value_id_ = CSSValueMenulistText;
      break;
    case kMenulistTextFieldPart:
      value_id_ = CSSValueMenulistTextfield;
      break;
    case kMeterPart:
      value_id_ = CSSValueMeter;
      break;
    case kProgressBarPart:
      value_id_ = CSSValueProgressBar;
      break;
    case kProgressBarValuePart:
      value_id_ = CSSValueProgressBarValue;
      break;
    case kSliderHorizontalPart:
      value_id_ = CSSValueSliderHorizontal;
      break;
    case kSliderVerticalPart:
      value_id_ = CSSValueSliderVertical;
      break;
    case kSliderThumbHorizontalPart:
      value_id_ = CSSValueSliderthumbHorizontal;
      break;
    case kSliderThumbVerticalPart:
      value_id_ = CSSValueSliderthumbVertical;
      break;
    case kCaretPart:
      value_id_ = CSSValueCaret;
      break;
    case kSearchFieldPart:
      value_id_ = CSSValueSearchfield;
      break;
    case kSearchFieldCancelButtonPart:
      value_id_ = CSSValueSearchfieldCancelButton;
      break;
    case kTextFieldPart:
      value_id_ = CSSValueTextfield;
      break;
    case kTextAreaPart:
      value_id_ = CSSValueTextarea;
      break;
    case kCapsLockIndicatorPart:
      value_id_ = CSSValueCapsLockIndicator;
      break;
    case kMediaRemotingCastIconPart:
      value_id_ = CSSValueInternalMediaRemotingCastIcon;
      break;
  }
}

template <>
inline ControlPart CSSIdentifierValue::ConvertTo() const {
  if (value_id_ == CSSValueNone)
    return kNoControlPart;
  return ControlPart(value_id_ - CSSValueCheckbox + 1);
}

template <>
inline CSSIdentifierValue::CSSIdentifierValue(EBackfaceVisibility e)
    : CSSValue(kIdentifierClass) {
  switch (e) {
    case kBackfaceVisibilityVisible:
      value_id_ = CSSValueVisible;
      break;
    case kBackfaceVisibilityHidden:
      value_id_ = CSSValueHidden;
      break;
  }
}

template <>
inline EBackfaceVisibility CSSIdentifierValue::ConvertTo() const {
  switch (value_id_) {
    case CSSValueVisible:
      return kBackfaceVisibilityVisible;
    case CSSValueHidden:
      return kBackfaceVisibilityHidden;
    default:
      break;
  }

  NOTREACHED();
  return kBackfaceVisibilityHidden;
}

template <>
inline CSSIdentifierValue::CSSIdentifierValue(EFillAttachment e)
    : CSSValue(kIdentifierClass) {
  switch (e) {
    case kScrollBackgroundAttachment:
      value_id_ = CSSValueScroll;
      break;
    case kLocalBackgroundAttachment:
      value_id_ = CSSValueLocal;
      break;
    case kFixedBackgroundAttachment:
      value_id_ = CSSValueFixed;
      break;
  }
}

template <>
inline EFillAttachment CSSIdentifierValue::ConvertTo() const {
  switch (value_id_) {
    case CSSValueScroll:
      return kScrollBackgroundAttachment;
    case CSSValueLocal:
      return kLocalBackgroundAttachment;
    case CSSValueFixed:
      return kFixedBackgroundAttachment;
    default:
      break;
  }

  NOTREACHED();
  return kScrollBackgroundAttachment;
}

template <>
inline CSSIdentifierValue::CSSIdentifierValue(EFillBox e)
    : CSSValue(kIdentifierClass) {
  switch (e) {
    case kBorderFillBox:
      value_id_ = CSSValueBorderBox;
      break;
    case kPaddingFillBox:
      value_id_ = CSSValuePaddingBox;
      break;
    case kContentFillBox:
      value_id_ = CSSValueContentBox;
      break;
    case kTextFillBox:
      value_id_ = CSSValueText;
      break;
  }
}

template <>
inline EFillBox CSSIdentifierValue::ConvertTo() const {
  switch (value_id_) {
    case CSSValueBorder:
    case CSSValueBorderBox:
      return kBorderFillBox;
    case CSSValuePadding:
    case CSSValuePaddingBox:
      return kPaddingFillBox;
    case CSSValueContent:
    case CSSValueContentBox:
      return kContentFillBox;
    case CSSValueText:
      return kTextFillBox;
    default:
      break;
  }

  NOTREACHED();
  return kBorderFillBox;
}

template <>
inline CSSIdentifierValue::CSSIdentifierValue(EFillRepeat e)
    : CSSValue(kIdentifierClass) {
  switch (e) {
    case kRepeatFill:
      value_id_ = CSSValueRepeat;
      break;
    case kNoRepeatFill:
      value_id_ = CSSValueNoRepeat;
      break;
    case kRoundFill:
      value_id_ = CSSValueRound;
      break;
    case kSpaceFill:
      value_id_ = CSSValueSpace;
      break;
  }
}

template <>
inline EFillRepeat CSSIdentifierValue::ConvertTo() const {
  switch (value_id_) {
    case CSSValueRepeat:
      return kRepeatFill;
    case CSSValueNoRepeat:
      return kNoRepeatFill;
    case CSSValueRound:
      return kRoundFill;
    case CSSValueSpace:
      return kSpaceFill;
    default:
      break;
  }

  NOTREACHED();
  return kRepeatFill;
}

template <>
inline CSSIdentifierValue::CSSIdentifierValue(EBoxPack e)
    : CSSValue(kIdentifierClass) {
  switch (e) {
    case kBoxPackStart:
      value_id_ = CSSValueStart;
      break;
    case kBoxPackCenter:
      value_id_ = CSSValueCenter;
      break;
    case kBoxPackEnd:
      value_id_ = CSSValueEnd;
      break;
    case kBoxPackJustify:
      value_id_ = CSSValueJustify;
      break;
  }
}

template <>
inline EBoxPack CSSIdentifierValue::ConvertTo() const {
  switch (value_id_) {
    case CSSValueStart:
      return kBoxPackStart;
    case CSSValueEnd:
      return kBoxPackEnd;
    case CSSValueCenter:
      return kBoxPackCenter;
    case CSSValueJustify:
      return kBoxPackJustify;
    default:
      break;
  }

  NOTREACHED();
  return kBoxPackJustify;
}

template <>
inline CSSIdentifierValue::CSSIdentifierValue(EBoxAlignment e)
    : CSSValue(kIdentifierClass) {
  switch (e) {
    case BSTRETCH:
      value_id_ = CSSValueStretch;
      break;
    case BSTART:
      value_id_ = CSSValueStart;
      break;
    case BCENTER:
      value_id_ = CSSValueCenter;
      break;
    case BEND:
      value_id_ = CSSValueEnd;
      break;
    case BBASELINE:
      value_id_ = CSSValueBaseline;
      break;
  }
}

template <>
inline EBoxAlignment CSSIdentifierValue::ConvertTo() const {
  switch (value_id_) {
    case CSSValueStretch:
      return BSTRETCH;
    case CSSValueStart:
      return BSTART;
    case CSSValueEnd:
      return BEND;
    case CSSValueCenter:
      return BCENTER;
    case CSSValueBaseline:
      return BBASELINE;
    default:
      break;
  }

  NOTREACHED();
  return BSTRETCH;
}

template <>
inline CSSIdentifierValue::CSSIdentifierValue(BackgroundEdgeOrigin e)
    : CSSValue(kIdentifierClass) {
  switch (e) {
    case kTopEdge:
      value_id_ = CSSValueTop;
      break;
    case kRightEdge:
      value_id_ = CSSValueRight;
      break;
    case kBottomEdge:
      value_id_ = CSSValueBottom;
      break;
    case kLeftEdge:
      value_id_ = CSSValueLeft;
      break;
  }
}

template <>
inline BackgroundEdgeOrigin CSSIdentifierValue::ConvertTo() const {
  switch (value_id_) {
    case CSSValueTop:
      return kTopEdge;
    case CSSValueRight:
      return kRightEdge;
    case CSSValueBottom:
      return kBottomEdge;
    case CSSValueLeft:
      return kLeftEdge;
    default:
      break;
  }

  NOTREACHED();
  return kTopEdge;
}

template <>
inline CSSIdentifierValue::CSSIdentifierValue(EBoxLines e)
    : CSSValue(kIdentifierClass) {
  switch (e) {
    case SINGLE:
      value_id_ = CSSValueSingle;
      break;
    case MULTIPLE:
      value_id_ = CSSValueMultiple;
      break;
  }
}

template <>
inline EBoxLines CSSIdentifierValue::ConvertTo() const {
  switch (value_id_) {
    case CSSValueSingle:
      return SINGLE;
    case CSSValueMultiple:
      return MULTIPLE;
    default:
      break;
  }

  NOTREACHED();
  return SINGLE;
}

template <>
inline CSSIdentifierValue::CSSIdentifierValue(EBoxOrient e)
    : CSSValue(kIdentifierClass) {
  switch (e) {
    case HORIZONTAL:
      value_id_ = CSSValueHorizontal;
      break;
    case VERTICAL:
      value_id_ = CSSValueVertical;
      break;
  }
}

template <>
inline EBoxOrient CSSIdentifierValue::ConvertTo() const {
  switch (value_id_) {
    case CSSValueHorizontal:
    case CSSValueInlineAxis:
      return HORIZONTAL;
    case CSSValueVertical:
    case CSSValueBlockAxis:
      return VERTICAL;
    default:
      break;
  }

  NOTREACHED();
  return HORIZONTAL;
}

template <>
inline CSSIdentifierValue::CSSIdentifierValue(EFlexDirection e)
    : CSSValue(kIdentifierClass) {
  switch (e) {
    case kFlowRow:
      value_id_ = CSSValueRow;
      break;
    case kFlowRowReverse:
      value_id_ = CSSValueRowReverse;
      break;
    case kFlowColumn:
      value_id_ = CSSValueColumn;
      break;
    case kFlowColumnReverse:
      value_id_ = CSSValueColumnReverse;
      break;
  }
}

template <>
inline EFlexDirection CSSIdentifierValue::ConvertTo() const {
  switch (value_id_) {
    case CSSValueRow:
      return kFlowRow;
    case CSSValueRowReverse:
      return kFlowRowReverse;
    case CSSValueColumn:
      return kFlowColumn;
    case CSSValueColumnReverse:
      return kFlowColumnReverse;
    default:
      break;
  }

  NOTREACHED();
  return kFlowRow;
}

template <>
inline CSSIdentifierValue::CSSIdentifierValue(EFlexWrap e)
    : CSSValue(kIdentifierClass) {
  switch (e) {
    case kFlexNoWrap:
      value_id_ = CSSValueNowrap;
      break;
    case kFlexWrap:
      value_id_ = CSSValueWrap;
      break;
    case kFlexWrapReverse:
      value_id_ = CSSValueWrapReverse;
      break;
  }
}

template <>
inline EFlexWrap CSSIdentifierValue::ConvertTo() const {
  switch (value_id_) {
    case CSSValueNowrap:
      return kFlexNoWrap;
    case CSSValueWrap:
      return kFlexWrap;
    case CSSValueWrapReverse:
      return kFlexWrapReverse;
    default:
      break;
  }

  NOTREACHED();
  return kFlexNoWrap;
}

template <>
inline CSSIdentifierValue::CSSIdentifierValue(EFloat e)
    : CSSValue(kIdentifierClass) {
  switch (e) {
    case EFloat::kNone:
      value_id_ = CSSValueNone;
      break;
    case EFloat::kLeft:
      value_id_ = CSSValueLeft;
      break;
    case EFloat::kRight:
      value_id_ = CSSValueRight;
      break;
  }
}

template <>
inline EFloat CSSIdentifierValue::ConvertTo() const {
  switch (value_id_) {
    case CSSValueLeft:
      return EFloat::kLeft;
    case CSSValueRight:
      return EFloat::kRight;
    case CSSValueNone:
      return EFloat::kNone;
    default:
      break;
  }

  NOTREACHED();
  return EFloat::kNone;
}

template <>
inline CSSIdentifierValue::CSSIdentifierValue(Hyphens e)
    : CSSValue(kIdentifierClass) {
  switch (e) {
    case Hyphens::kAuto:
      value_id_ = CSSValueAuto;
      break;
    case Hyphens::kManual:
      value_id_ = CSSValueManual;
      break;
    case Hyphens::kNone:
      value_id_ = CSSValueNone;
      break;
  }
}

template <>
inline Hyphens CSSIdentifierValue::ConvertTo() const {
  switch (value_id_) {
    case CSSValueAuto:
      return Hyphens::kAuto;
    case CSSValueManual:
      return Hyphens::kManual;
    case CSSValueNone:
      return Hyphens::kNone;
    default:
      break;
  }

  NOTREACHED();
  return Hyphens::kManual;
}

template <>
inline CSSIdentifierValue::CSSIdentifierValue(LineBreak e)
    : CSSValue(kIdentifierClass) {
  switch (e) {
    case LineBreak::kAuto:
      value_id_ = CSSValueAuto;
      break;
    case LineBreak::kLoose:
      value_id_ = CSSValueLoose;
      break;
    case LineBreak::kNormal:
      value_id_ = CSSValueNormal;
      break;
    case LineBreak::kStrict:
      value_id_ = CSSValueStrict;
      break;
    case LineBreak::kAfterWhiteSpace:
      value_id_ = CSSValueAfterWhiteSpace;
      break;
  }
}

template <>
inline LineBreak CSSIdentifierValue::ConvertTo() const {
  switch (value_id_) {
    case CSSValueAuto:
      return LineBreak::kAuto;
    case CSSValueLoose:
      return LineBreak::kLoose;
    case CSSValueNormal:
      return LineBreak::kNormal;
    case CSSValueStrict:
      return LineBreak::kStrict;
    case CSSValueAfterWhiteSpace:
      return LineBreak::kAfterWhiteSpace;
    default:
      break;
  }

  NOTREACHED();
  return LineBreak::kAuto;
}

template <>
inline CSSIdentifierValue::CSSIdentifierValue(EMarginCollapse e)
    : CSSValue(kIdentifierClass) {
  switch (e) {
    case kMarginCollapseCollapse:
      value_id_ = CSSValueCollapse;
      break;
    case kMarginCollapseSeparate:
      value_id_ = CSSValueSeparate;
      break;
    case kMarginCollapseDiscard:
      value_id_ = CSSValueDiscard;
      break;
  }
}

template <>
inline EMarginCollapse CSSIdentifierValue::ConvertTo() const {
  switch (value_id_) {
    case CSSValueCollapse:
      return kMarginCollapseCollapse;
    case CSSValueSeparate:
      return kMarginCollapseSeparate;
    case CSSValueDiscard:
      return kMarginCollapseDiscard;
    default:
      break;
  }

  NOTREACHED();
  return kMarginCollapseCollapse;
}

template <>
inline CSSIdentifierValue::CSSIdentifierValue(EPosition e)
    : CSSValue(kIdentifierClass) {
  switch (e) {
    case EPosition::kStatic:
      value_id_ = CSSValueStatic;
      break;
    case EPosition::kRelative:
      value_id_ = CSSValueRelative;
      break;
    case EPosition::kAbsolute:
      value_id_ = CSSValueAbsolute;
      break;
    case EPosition::kFixed:
      value_id_ = CSSValueFixed;
      break;
    case EPosition::kSticky:
      value_id_ = CSSValueSticky;
      break;
  }
}

template <>
inline EPosition CSSIdentifierValue::ConvertTo() const {
  switch (value_id_) {
    case CSSValueStatic:
      return EPosition::kStatic;
    case CSSValueRelative:
      return EPosition::kRelative;
    case CSSValueAbsolute:
      return EPosition::kAbsolute;
    case CSSValueFixed:
      return EPosition::kFixed;
    case CSSValueSticky:
      return EPosition::kSticky;
    default:
      break;
  }

  NOTREACHED();
  return EPosition::kStatic;
}

template <>
inline CSSIdentifierValue::CSSIdentifierValue(EResize e)
    : CSSValue(kIdentifierClass) {
  switch (e) {
    case RESIZE_BOTH:
      value_id_ = CSSValueBoth;
      break;
    case RESIZE_HORIZONTAL:
      value_id_ = CSSValueHorizontal;
      break;
    case RESIZE_VERTICAL:
      value_id_ = CSSValueVertical;
      break;
    case RESIZE_NONE:
      value_id_ = CSSValueNone;
      break;
  }
}

template <>
inline EResize CSSIdentifierValue::ConvertTo() const {
  switch (value_id_) {
    case CSSValueBoth:
      return RESIZE_BOTH;
    case CSSValueHorizontal:
      return RESIZE_HORIZONTAL;
    case CSSValueVertical:
      return RESIZE_VERTICAL;
    case CSSValueAuto:
      // Depends on settings, thus should be handled by the caller.
      NOTREACHED();
      return RESIZE_NONE;
    case CSSValueNone:
      return RESIZE_NONE;
    default:
      break;
  }

  NOTREACHED();
  return RESIZE_NONE;
}

template <>
inline CSSIdentifierValue::CSSIdentifierValue(ETableLayout e)
    : CSSValue(kIdentifierClass) {
  switch (e) {
    case ETableLayout::kAuto:
      value_id_ = CSSValueAuto;
      break;
    case ETableLayout::kFixed:
      value_id_ = CSSValueFixed;
      break;
  }
}

template <>
inline ETableLayout CSSIdentifierValue::ConvertTo() const {
  switch (value_id_) {
    case CSSValueFixed:
      return ETableLayout::kFixed;
    case CSSValueAuto:
      return ETableLayout::kAuto;
    default:
      break;
  }

  NOTREACHED();
  return ETableLayout::kAuto;
}

template <>
inline CSSIdentifierValue::CSSIdentifierValue(TextAlignLast e)
    : CSSValue(kIdentifierClass) {
  switch (e) {
    case kTextAlignLastStart:
      value_id_ = CSSValueStart;
      break;
    case kTextAlignLastEnd:
      value_id_ = CSSValueEnd;
      break;
    case kTextAlignLastLeft:
      value_id_ = CSSValueLeft;
      break;
    case kTextAlignLastRight:
      value_id_ = CSSValueRight;
      break;
    case kTextAlignLastCenter:
      value_id_ = CSSValueCenter;
      break;
    case kTextAlignLastJustify:
      value_id_ = CSSValueJustify;
      break;
    case kTextAlignLastAuto:
      value_id_ = CSSValueAuto;
      break;
  }
}

template <>
inline TextAlignLast CSSIdentifierValue::ConvertTo() const {
  switch (value_id_) {
    case CSSValueAuto:
      return kTextAlignLastAuto;
    case CSSValueStart:
      return kTextAlignLastStart;
    case CSSValueEnd:
      return kTextAlignLastEnd;
    case CSSValueLeft:
      return kTextAlignLastLeft;
    case CSSValueRight:
      return kTextAlignLastRight;
    case CSSValueCenter:
      return kTextAlignLastCenter;
    case CSSValueJustify:
      return kTextAlignLastJustify;
    default:
      break;
  }

  NOTREACHED();
  return kTextAlignLastAuto;
}

template <>
inline CSSIdentifierValue::CSSIdentifierValue(TextJustify e)
    : CSSValue(kIdentifierClass) {
  switch (e) {
    case kTextJustifyAuto:
      value_id_ = CSSValueAuto;
      break;
    case kTextJustifyNone:
      value_id_ = CSSValueNone;
      break;
    case kTextJustifyInterWord:
      value_id_ = CSSValueInterWord;
      break;
    case kTextJustifyDistribute:
      value_id_ = CSSValueDistribute;
      break;
  }
}

template <>
inline TextJustify CSSIdentifierValue::ConvertTo() const {
  switch (value_id_) {
    case CSSValueAuto:
      return kTextJustifyAuto;
    case CSSValueNone:
      return kTextJustifyNone;
    case CSSValueInterWord:
      return kTextJustifyInterWord;
    case CSSValueDistribute:
      return kTextJustifyDistribute;
    default:
      break;
  }

  NOTREACHED();
  return kTextJustifyAuto;
}

template <>
inline TextDecoration CSSIdentifierValue::ConvertTo() const {
  switch (value_id_) {
    case CSSValueNone:
      return TextDecoration::kNone;
    case CSSValueUnderline:
      return TextDecoration::kUnderline;
    case CSSValueOverline:
      return TextDecoration::kOverline;
    case CSSValueLineThrough:
      return TextDecoration::kLineThrough;
    case CSSValueBlink:
      return TextDecoration::kBlink;
    default:
      break;
  }

  NOTREACHED();
  return TextDecoration::kNone;
}

template <>
inline TextDecorationStyle CSSIdentifierValue::ConvertTo() const {
  switch (value_id_) {
    case CSSValueSolid:
      return kTextDecorationStyleSolid;
    case CSSValueDouble:
      return kTextDecorationStyleDouble;
    case CSSValueDotted:
      return kTextDecorationStyleDotted;
    case CSSValueDashed:
      return kTextDecorationStyleDashed;
    case CSSValueWavy:
      return kTextDecorationStyleWavy;
    default:
      break;
  }

  NOTREACHED();
  return kTextDecorationStyleSolid;
}

template <>
inline CSSIdentifierValue::CSSIdentifierValue(TextUnderlinePosition e)
    : CSSValue(kIdentifierClass) {
  switch (e) {
    case kTextUnderlinePositionAuto:
      value_id_ = CSSValueAuto;
      break;
    case kTextUnderlinePositionUnder:
      value_id_ = CSSValueUnder;
      break;
  }

  // FIXME: Implement support for 'under left' and 'under right' values.
}

template <>
inline TextUnderlinePosition CSSIdentifierValue::ConvertTo() const {
  switch (value_id_) {
    case CSSValueAuto:
      return kTextUnderlinePositionAuto;
    case CSSValueUnder:
      return kTextUnderlinePositionUnder;
    default:
      break;
  }

  // FIXME: Implement support for 'under left' and 'under right' values.

  NOTREACHED();
  return kTextUnderlinePositionAuto;
}

template <>
inline TextDecorationSkip CSSIdentifierValue::ConvertTo() const {
  switch (value_id_) {
    case CSSValueObjects:
      return kTextDecorationSkipObjects;
    case CSSValueInk:
      return kTextDecorationSkipInk;
    default:
      break;
  }

  NOTREACHED();
  return kTextDecorationSkipObjects;
}

template <>
inline CSSIdentifierValue::CSSIdentifierValue(ETextSecurity e)
    : CSSValue(kIdentifierClass) {
  switch (e) {
    case ETextSecurity::kNone:
      value_id_ = CSSValueNone;
      break;
    case ETextSecurity::kDisc:
      value_id_ = CSSValueDisc;
      break;
    case ETextSecurity::kCircle:
      value_id_ = CSSValueCircle;
      break;
    case ETextSecurity::kSquare:
      value_id_ = CSSValueSquare;
      break;
  }
}

template <>
inline ETextSecurity CSSIdentifierValue::ConvertTo() const {
  switch (value_id_) {
    case CSSValueNone:
      return ETextSecurity::kNone;
    case CSSValueDisc:
      return ETextSecurity::kDisc;
    case CSSValueCircle:
      return ETextSecurity::kCircle;
    case CSSValueSquare:
      return ETextSecurity::kSquare;
    default:
      break;
  }

  NOTREACHED();
  return ETextSecurity::kNone;
}

template <>
inline CSSIdentifierValue::CSSIdentifierValue(EUserDrag e)
    : CSSValue(kIdentifierClass) {
  switch (e) {
    case DRAG_AUTO:
      value_id_ = CSSValueAuto;
      break;
    case DRAG_NONE:
      value_id_ = CSSValueNone;
      break;
    case DRAG_ELEMENT:
      value_id_ = CSSValueElement;
      break;
    default:
      break;
  }
}

template <>
inline EUserDrag CSSIdentifierValue::ConvertTo() const {
  switch (value_id_) {
    case CSSValueAuto:
      return DRAG_AUTO;
    case CSSValueNone:
      return DRAG_NONE;
    case CSSValueElement:
      return DRAG_ELEMENT;
    default:
      break;
  }

  NOTREACHED();
  return DRAG_AUTO;
}

template <>
inline CSSIdentifierValue::CSSIdentifierValue(EUserModify e)
    : CSSValue(kIdentifierClass) {
  switch (e) {
    case EUserModify::kReadOnly:
      value_id_ = CSSValueReadOnly;
      break;
    case EUserModify::kReadWrite:
      value_id_ = CSSValueReadWrite;
      break;
    case EUserModify::kReadWritePlaintextOnly:
      value_id_ = CSSValueReadWritePlaintextOnly;
      break;
  }
}

template <>
inline EUserModify CSSIdentifierValue::ConvertTo() const {
  switch (value_id_) {
    case CSSValueReadOnly:
      return EUserModify::kReadOnly;
    case CSSValueReadWrite:
      return EUserModify::kReadWrite;
    case CSSValueReadWritePlaintextOnly:
      return EUserModify::kReadWritePlaintextOnly;
    default:
      break;
  }

  NOTREACHED();
  return EUserModify::kReadOnly;
}

template <>
inline CSSIdentifierValue::CSSIdentifierValue(EUserSelect e)
    : CSSValue(kIdentifierClass) {
  switch (e) {
    case EUserSelect::kNone:
      value_id_ = CSSValueNone;
      break;
    case EUserSelect::kText:
      value_id_ = CSSValueText;
      break;
    case EUserSelect::kAll:
      value_id_ = CSSValueAll;
      break;
  }
}

template <>
inline EUserSelect CSSIdentifierValue::ConvertTo() const {
  switch (value_id_) {
    case CSSValueAuto:
      return EUserSelect::kText;
    case CSSValueNone:
      return EUserSelect::kNone;
    case CSSValueText:
      return EUserSelect::kText;
    case CSSValueAll:
      return EUserSelect::kAll;
    default:
      break;
  }

  NOTREACHED();
  return EUserSelect::kText;
}

template <>
inline CSSIdentifierValue::CSSIdentifierValue(EVerticalAlign a)
    : CSSValue(kIdentifierClass) {
  switch (a) {
    case EVerticalAlign::kTop:
      value_id_ = CSSValueTop;
      break;
    case EVerticalAlign::kBottom:
      value_id_ = CSSValueBottom;
      break;
    case EVerticalAlign::kMiddle:
      value_id_ = CSSValueMiddle;
      break;
    case EVerticalAlign::kBaseline:
      value_id_ = CSSValueBaseline;
      break;
    case EVerticalAlign::kTextBottom:
      value_id_ = CSSValueTextBottom;
      break;
    case EVerticalAlign::kTextTop:
      value_id_ = CSSValueTextTop;
      break;
    case EVerticalAlign::kSub:
      value_id_ = CSSValueSub;
      break;
    case EVerticalAlign::kSuper:
      value_id_ = CSSValueSuper;
      break;
    case EVerticalAlign::kBaselineMiddle:
      value_id_ = CSSValueWebkitBaselineMiddle;
      break;
    case EVerticalAlign::kLength:
      value_id_ = CSSValueInvalid;
  }
}

template <>
inline EVerticalAlign CSSIdentifierValue::ConvertTo() const {
  switch (value_id_) {
    case CSSValueTop:
      return EVerticalAlign::kTop;
    case CSSValueBottom:
      return EVerticalAlign::kBottom;
    case CSSValueMiddle:
      return EVerticalAlign::kMiddle;
    case CSSValueBaseline:
      return EVerticalAlign::kBaseline;
    case CSSValueTextBottom:
      return EVerticalAlign::kTextBottom;
    case CSSValueTextTop:
      return EVerticalAlign::kTextTop;
    case CSSValueSub:
      return EVerticalAlign::kSub;
    case CSSValueSuper:
      return EVerticalAlign::kSuper;
    case CSSValueWebkitBaselineMiddle:
      return EVerticalAlign::kBaselineMiddle;
    default:
      break;
  }

  NOTREACHED();
  return EVerticalAlign::kTop;
}

template <>
inline CSSIdentifierValue::CSSIdentifierValue(EWordBreak e)
    : CSSValue(kIdentifierClass) {
  switch (e) {
    case EWordBreak::kNormal:
      value_id_ = CSSValueNormal;
      break;
    case EWordBreak::kBreakAll:
      value_id_ = CSSValueBreakAll;
      break;
    case EWordBreak::kBreakWord:
      value_id_ = CSSValueBreakWord;
      break;
    case EWordBreak::kKeepAll:
      value_id_ = CSSValueKeepAll;
      break;
  }
}

template <>
inline EWordBreak CSSIdentifierValue::ConvertTo() const {
  switch (value_id_) {
    case CSSValueBreakAll:
      return EWordBreak::kBreakAll;
    case CSSValueBreakWord:
      return EWordBreak::kBreakWord;
    case CSSValueNormal:
      return EWordBreak::kNormal;
    case CSSValueKeepAll:
      return EWordBreak::kKeepAll;
    default:
      break;
  }

  NOTREACHED();
  return EWordBreak::kNormal;
}

template <>
inline CSSIdentifierValue::CSSIdentifierValue(EOverflowWrap e)
    : CSSValue(kIdentifierClass) {
  switch (e) {
    case EOverflowWrap::kNormal:
      value_id_ = CSSValueNormal;
      break;
    case EOverflowWrap::kBreakWord:
      value_id_ = CSSValueBreakWord;
      break;
  }
}

template <>
inline EOverflowWrap CSSIdentifierValue::ConvertTo() const {
  switch (value_id_) {
    case CSSValueBreakWord:
      return EOverflowWrap::kBreakWord;
    case CSSValueNormal:
      return EOverflowWrap::kNormal;
    default:
      break;
  }

  NOTREACHED();
  return EOverflowWrap::kNormal;
}

template <>
inline CSSIdentifierValue::CSSIdentifierValue(TextCombine e)
    : CSSValue(kIdentifierClass) {
  switch (e) {
    case kTextCombineNone:
      value_id_ = CSSValueNone;
      break;
    case kTextCombineAll:
      value_id_ = CSSValueAll;
      break;
  }
}

template <>
inline TextCombine CSSIdentifierValue::ConvertTo() const {
  switch (value_id_) {
    case CSSValueNone:
      return kTextCombineNone;
    case CSSValueAll:
    case CSSValueHorizontal:  // -webkit-text-combine
      return kTextCombineAll;
    default:
      break;
  }

  NOTREACHED();
  return kTextCombineNone;
}

template <>
inline CSSIdentifierValue::CSSIdentifierValue(RubyPosition position)
    : CSSValue(kIdentifierClass) {
  switch (position) {
    case kRubyPositionBefore:
      value_id_ = CSSValueBefore;
      break;
    case kRubyPositionAfter:
      value_id_ = CSSValueAfter;
      break;
  }
}

template <>
inline RubyPosition CSSIdentifierValue::ConvertTo() const {
  switch (value_id_) {
    case CSSValueBefore:
      return kRubyPositionBefore;
    case CSSValueAfter:
      return kRubyPositionAfter;
    default:
      break;
  }

  NOTREACHED();
  return kRubyPositionBefore;
}

template <>
inline CSSIdentifierValue::CSSIdentifierValue(TextEmphasisPosition position)
    : CSSValue(kIdentifierClass) {
  switch (position) {
    case kTextEmphasisPositionOver:
      value_id_ = CSSValueOver;
      break;
    case kTextEmphasisPositionUnder:
      value_id_ = CSSValueUnder;
      break;
  }
}

template <>
inline TextEmphasisPosition CSSIdentifierValue::ConvertTo() const {
  switch (value_id_) {
    case CSSValueOver:
      return kTextEmphasisPositionOver;
    case CSSValueUnder:
      return kTextEmphasisPositionUnder;
    default:
      break;
  }

  NOTREACHED();
  return kTextEmphasisPositionOver;
}

template <>
inline CSSIdentifierValue::CSSIdentifierValue(TextOverflow overflow)
    : CSSValue(kIdentifierClass) {
  switch (overflow) {
    case kTextOverflowClip:
      value_id_ = CSSValueClip;
      break;
    case kTextOverflowEllipsis:
      value_id_ = CSSValueEllipsis;
      break;
  }
}

template <>
inline TextOverflow CSSIdentifierValue::ConvertTo() const {
  switch (value_id_) {
    case CSSValueClip:
      return kTextOverflowClip;
    case CSSValueEllipsis:
      return kTextOverflowEllipsis;
    default:
      break;
  }

  NOTREACHED();
  return kTextOverflowClip;
}

template <>
inline CSSIdentifierValue::CSSIdentifierValue(TextEmphasisFill fill)
    : CSSValue(kIdentifierClass) {
  switch (fill) {
    case kTextEmphasisFillFilled:
      value_id_ = CSSValueFilled;
      break;
    case kTextEmphasisFillOpen:
      value_id_ = CSSValueOpen;
      break;
  }
}

template <>
inline TextEmphasisFill CSSIdentifierValue::ConvertTo() const {
  switch (value_id_) {
    case CSSValueFilled:
      return kTextEmphasisFillFilled;
    case CSSValueOpen:
      return kTextEmphasisFillOpen;
    default:
      break;
  }

  NOTREACHED();
  return kTextEmphasisFillFilled;
}

template <>
inline CSSIdentifierValue::CSSIdentifierValue(TextEmphasisMark mark)
    : CSSValue(kIdentifierClass) {
  switch (mark) {
    case kTextEmphasisMarkDot:
      value_id_ = CSSValueDot;
      break;
    case kTextEmphasisMarkCircle:
      value_id_ = CSSValueCircle;
      break;
    case kTextEmphasisMarkDoubleCircle:
      value_id_ = CSSValueDoubleCircle;
      break;
    case kTextEmphasisMarkTriangle:
      value_id_ = CSSValueTriangle;
      break;
    case kTextEmphasisMarkSesame:
      value_id_ = CSSValueSesame;
      break;
    case kTextEmphasisMarkNone:
    case kTextEmphasisMarkAuto:
    case kTextEmphasisMarkCustom:
      NOTREACHED();
      value_id_ = CSSValueNone;
      break;
  }
}

template <>
inline TextEmphasisMark CSSIdentifierValue::ConvertTo() const {
  switch (value_id_) {
    case CSSValueNone:
      return kTextEmphasisMarkNone;
    case CSSValueDot:
      return kTextEmphasisMarkDot;
    case CSSValueCircle:
      return kTextEmphasisMarkCircle;
    case CSSValueDoubleCircle:
      return kTextEmphasisMarkDoubleCircle;
    case CSSValueTriangle:
      return kTextEmphasisMarkTriangle;
    case CSSValueSesame:
      return kTextEmphasisMarkSesame;
    default:
      break;
  }

  NOTREACHED();
  return kTextEmphasisMarkNone;
}

template <>
inline CSSIdentifierValue::CSSIdentifierValue(TextOrientation e)
    : CSSValue(kIdentifierClass) {
  switch (e) {
    case kTextOrientationSideways:
      value_id_ = CSSValueSideways;
      break;
    case kTextOrientationMixed:
      value_id_ = CSSValueMixed;
      break;
    case kTextOrientationUpright:
      value_id_ = CSSValueUpright;
      break;
  }
}

template <>
inline TextOrientation CSSIdentifierValue::ConvertTo() const {
  switch (value_id_) {
    case CSSValueSideways:
    case CSSValueSidewaysRight:
      return kTextOrientationSideways;
    case CSSValueMixed:
    case CSSValueVerticalRight:  // -webkit-text-orientation
      return kTextOrientationMixed;
    case CSSValueUpright:
      return kTextOrientationUpright;
    default:
      break;
  }

  NOTREACHED();
  return kTextOrientationMixed;
}

template <>
inline CSSIdentifierValue::CSSIdentifierValue(FontDescription::Kerning kerning)
    : CSSValue(kIdentifierClass) {
  switch (kerning) {
    case FontDescription::kAutoKerning:
      value_id_ = CSSValueAuto;
      return;
    case FontDescription::kNormalKerning:
      value_id_ = CSSValueNormal;
      return;
    case FontDescription::kNoneKerning:
      value_id_ = CSSValueNone;
      return;
  }

  NOTREACHED();
  value_id_ = CSSValueAuto;
}

template <>
inline FontDescription::Kerning CSSIdentifierValue::ConvertTo() const {
  switch (value_id_) {
    case CSSValueAuto:
      return FontDescription::kAutoKerning;
    case CSSValueNormal:
      return FontDescription::kNormalKerning;
    case CSSValueNone:
      return FontDescription::kNoneKerning;
    default:
      break;
  }

  NOTREACHED();
  return FontDescription::kAutoKerning;
}

template <>
inline CSSIdentifierValue::CSSIdentifierValue(ObjectFit fit)
    : CSSValue(kIdentifierClass) {
  switch (fit) {
    case kObjectFitFill:
      value_id_ = CSSValueFill;
      break;
    case kObjectFitContain:
      value_id_ = CSSValueContain;
      break;
    case kObjectFitCover:
      value_id_ = CSSValueCover;
      break;
    case kObjectFitNone:
      value_id_ = CSSValueNone;
      break;
    case kObjectFitScaleDown:
      value_id_ = CSSValueScaleDown;
      break;
  }
}

template <>
inline ObjectFit CSSIdentifierValue::ConvertTo() const {
  switch (value_id_) {
    case CSSValueFill:
      return kObjectFitFill;
    case CSSValueContain:
      return kObjectFitContain;
    case CSSValueCover:
      return kObjectFitCover;
    case CSSValueNone:
      return kObjectFitNone;
    case CSSValueScaleDown:
      return kObjectFitScaleDown;
    default:
      NOTREACHED();
      return kObjectFitFill;
  }
}

template <>
inline CSSIdentifierValue::CSSIdentifierValue(EFillSizeType fill_size)
    : CSSValue(kIdentifierClass) {
  switch (fill_size) {
    case kContain:
      value_id_ = CSSValueContain;
      break;
    case kCover:
      value_id_ = CSSValueCover;
      break;
    case kSizeNone:
      value_id_ = CSSValueNone;
      break;
    case kSizeLength:
    default:
      NOTREACHED();
  }
}

template <>
inline CSSIdentifierValue::CSSIdentifierValue(FontSmoothingMode smoothing)
    : CSSValue(kIdentifierClass) {
  switch (smoothing) {
    case kAutoSmoothing:
      value_id_ = CSSValueAuto;
      return;
    case kNoSmoothing:
      value_id_ = CSSValueNone;
      return;
    case kAntialiased:
      value_id_ = CSSValueAntialiased;
      return;
    case kSubpixelAntialiased:
      value_id_ = CSSValueSubpixelAntialiased;
      return;
  }

  NOTREACHED();
  value_id_ = CSSValueAuto;
}

template <>
inline FontSmoothingMode CSSIdentifierValue::ConvertTo() const {
  switch (value_id_) {
    case CSSValueAuto:
      return kAutoSmoothing;
    case CSSValueNone:
      return kNoSmoothing;
    case CSSValueAntialiased:
      return kAntialiased;
    case CSSValueSubpixelAntialiased:
      return kSubpixelAntialiased;
    default:
      break;
  }

  NOTREACHED();
  return kAutoSmoothing;
}

template <>
inline CSSIdentifierValue::CSSIdentifierValue(FontWeight weight)
    : CSSValue(kIdentifierClass) {
  switch (weight) {
    case kFontWeight900:
      value_id_ = CSSValue900;
      return;
    case kFontWeight800:
      value_id_ = CSSValue800;
      return;
    case kFontWeight700:
      value_id_ = CSSValueBold;
      return;
    case kFontWeight600:
      value_id_ = CSSValue600;
      return;
    case kFontWeight500:
      value_id_ = CSSValue500;
      return;
    case kFontWeight400:
      value_id_ = CSSValueNormal;
      return;
    case kFontWeight300:
      value_id_ = CSSValue300;
      return;
    case kFontWeight200:
      value_id_ = CSSValue200;
      return;
    case kFontWeight100:
      value_id_ = CSSValue100;
      return;
  }

  NOTREACHED();
  value_id_ = CSSValueNormal;
}

template <>
inline FontWeight CSSIdentifierValue::ConvertTo() const {
  switch (value_id_) {
    case CSSValueBold:
      return kFontWeightBold;
    case CSSValueNormal:
      return kFontWeightNormal;
    case CSSValue900:
      return kFontWeight900;
    case CSSValue800:
      return kFontWeight800;
    case CSSValue700:
      return kFontWeight700;
    case CSSValue600:
      return kFontWeight600;
    case CSSValue500:
      return kFontWeight500;
    case CSSValue400:
      return kFontWeight400;
    case CSSValue300:
      return kFontWeight300;
    case CSSValue200:
      return kFontWeight200;
    case CSSValue100:
      return kFontWeight100;
    default:
      break;
  }

  NOTREACHED();
  return kFontWeightNormal;
}

template <>
inline CSSIdentifierValue::CSSIdentifierValue(FontStyle italic)
    : CSSValue(kIdentifierClass) {
  switch (italic) {
    case kFontStyleNormal:
      value_id_ = CSSValueNormal;
      return;
    case kFontStyleOblique:
      value_id_ = CSSValueOblique;
      return;
    case kFontStyleItalic:
      value_id_ = CSSValueItalic;
      return;
  }

  NOTREACHED();
  value_id_ = CSSValueNormal;
}

template <>
inline FontStyle CSSIdentifierValue::ConvertTo() const {
  switch (value_id_) {
    case CSSValueOblique:
      return kFontStyleOblique;
    case CSSValueItalic:
      return kFontStyleItalic;
    case CSSValueNormal:
      return kFontStyleNormal;
    default:
      break;
  }
  NOTREACHED();
  return kFontStyleNormal;
}

template <>
inline CSSIdentifierValue::CSSIdentifierValue(FontStretch stretch)
    : CSSValue(kIdentifierClass) {
  switch (stretch) {
    case kFontStretchUltraCondensed:
      value_id_ = CSSValueUltraCondensed;
      return;
    case kFontStretchExtraCondensed:
      value_id_ = CSSValueExtraCondensed;
      return;
    case kFontStretchCondensed:
      value_id_ = CSSValueCondensed;
      return;
    case kFontStretchSemiCondensed:
      value_id_ = CSSValueSemiCondensed;
      return;
    case kFontStretchNormal:
      value_id_ = CSSValueNormal;
      return;
    case kFontStretchSemiExpanded:
      value_id_ = CSSValueSemiExpanded;
      return;
    case kFontStretchExpanded:
      value_id_ = CSSValueExpanded;
      return;
    case kFontStretchExtraExpanded:
      value_id_ = CSSValueExtraExpanded;
      return;
    case kFontStretchUltraExpanded:
      value_id_ = CSSValueUltraExpanded;
      return;
  }

  NOTREACHED();
  value_id_ = CSSValueNormal;
}

template <>
inline FontStretch CSSIdentifierValue::ConvertTo() const {
  switch (value_id_) {
    case CSSValueUltraCondensed:
      return kFontStretchUltraCondensed;
    case CSSValueExtraCondensed:
      return kFontStretchExtraCondensed;
    case CSSValueCondensed:
      return kFontStretchCondensed;
    case CSSValueSemiCondensed:
      return kFontStretchSemiCondensed;
    case CSSValueNormal:
      return kFontStretchNormal;
    case CSSValueSemiExpanded:
      return kFontStretchSemiExpanded;
    case CSSValueExpanded:
      return kFontStretchExpanded;
    case CSSValueExtraExpanded:
      return kFontStretchExtraExpanded;
    case CSSValueUltraExpanded:
      return kFontStretchUltraExpanded;
    default:
      break;
  }

  NOTREACHED();
  return kFontStretchNormal;
}

template <>
inline CSSIdentifierValue::CSSIdentifierValue(TextRenderingMode e)
    : CSSValue(kIdentifierClass) {
  switch (e) {
    case kAutoTextRendering:
      value_id_ = CSSValueAuto;
      break;
    case kOptimizeSpeed:
      value_id_ = CSSValueOptimizeSpeed;
      break;
    case kOptimizeLegibility:
      value_id_ = CSSValueOptimizeLegibility;
      break;
    case kGeometricPrecision:
      value_id_ = CSSValueGeometricPrecision;
      break;
  }
}

template <>
inline TextRenderingMode CSSIdentifierValue::ConvertTo() const {
  switch (value_id_) {
    case CSSValueAuto:
      return kAutoTextRendering;
    case CSSValueOptimizeSpeed:
      return kOptimizeSpeed;
    case CSSValueOptimizeLegibility:
      return kOptimizeLegibility;
    case CSSValueGeometricPrecision:
      return kGeometricPrecision;
    default:
      break;
  }

  NOTREACHED();
  return kAutoTextRendering;
}

template <>
inline CSSIdentifierValue::CSSIdentifierValue(ESpeak e)
    : CSSValue(kIdentifierClass) {
  switch (e) {
    case ESpeak::kNone:
      value_id_ = CSSValueNone;
      break;
    case ESpeak::kNormal:
      value_id_ = CSSValueNormal;
      break;
    case ESpeak::kSpellOut:
      value_id_ = CSSValueSpellOut;
      break;
    case ESpeak::kDigits:
      value_id_ = CSSValueDigits;
      break;
    case ESpeak::kLiteralPunctuation:
      value_id_ = CSSValueLiteralPunctuation;
      break;
    case ESpeak::kNoPunctuation:
      value_id_ = CSSValueNoPunctuation;
      break;
  }
}

template <>
inline EOrder CSSIdentifierValue::ConvertTo() const {
  switch (value_id_) {
    case CSSValueLogical:
      return EOrder::kLogical;
    case CSSValueVisual:
      return EOrder::kVisual;
    default:
      break;
  }

  NOTREACHED();
  return EOrder::kLogical;
}

template <>
inline CSSIdentifierValue::CSSIdentifierValue(EOrder e)
    : CSSValue(kIdentifierClass) {
  switch (e) {
    case EOrder::kLogical:
      value_id_ = CSSValueLogical;
      break;
    case EOrder::kVisual:
      value_id_ = CSSValueVisual;
      break;
  }
}

template <>
inline ESpeak CSSIdentifierValue::ConvertTo() const {
  switch (value_id_) {
    case CSSValueNone:
      return ESpeak::kNone;
    case CSSValueNormal:
      return ESpeak::kNormal;
    case CSSValueSpellOut:
      return ESpeak::kSpellOut;
    case CSSValueDigits:
      return ESpeak::kDigits;
    case CSSValueLiteralPunctuation:
      return ESpeak::kLiteralPunctuation;
    case CSSValueNoPunctuation:
      return ESpeak::kNoPunctuation;
    default:
      break;
  }

  NOTREACHED();
  return ESpeak::kNormal;
}

template <>
inline CSSIdentifierValue::CSSIdentifierValue(WebBlendMode blend_mode)
    : CSSValue(kIdentifierClass) {
  switch (blend_mode) {
    case kWebBlendModeNormal:
      value_id_ = CSSValueNormal;
      break;
    case kWebBlendModeMultiply:
      value_id_ = CSSValueMultiply;
      break;
    case kWebBlendModeScreen:
      value_id_ = CSSValueScreen;
      break;
    case kWebBlendModeOverlay:
      value_id_ = CSSValueOverlay;
      break;
    case kWebBlendModeDarken:
      value_id_ = CSSValueDarken;
      break;
    case kWebBlendModeLighten:
      value_id_ = CSSValueLighten;
      break;
    case kWebBlendModeColorDodge:
      value_id_ = CSSValueColorDodge;
      break;
    case kWebBlendModeColorBurn:
      value_id_ = CSSValueColorBurn;
      break;
    case kWebBlendModeHardLight:
      value_id_ = CSSValueHardLight;
      break;
    case kWebBlendModeSoftLight:
      value_id_ = CSSValueSoftLight;
      break;
    case kWebBlendModeDifference:
      value_id_ = CSSValueDifference;
      break;
    case kWebBlendModeExclusion:
      value_id_ = CSSValueExclusion;
      break;
    case kWebBlendModeHue:
      value_id_ = CSSValueHue;
      break;
    case kWebBlendModeSaturation:
      value_id_ = CSSValueSaturation;
      break;
    case kWebBlendModeColor:
      value_id_ = CSSValueColor;
      break;
    case kWebBlendModeLuminosity:
      value_id_ = CSSValueLuminosity;
      break;
  }
}

template <>
inline WebBlendMode CSSIdentifierValue::ConvertTo() const {
  switch (value_id_) {
    case CSSValueNormal:
      return kWebBlendModeNormal;
    case CSSValueMultiply:
      return kWebBlendModeMultiply;
    case CSSValueScreen:
      return kWebBlendModeScreen;
    case CSSValueOverlay:
      return kWebBlendModeOverlay;
    case CSSValueDarken:
      return kWebBlendModeDarken;
    case CSSValueLighten:
      return kWebBlendModeLighten;
    case CSSValueColorDodge:
      return kWebBlendModeColorDodge;
    case CSSValueColorBurn:
      return kWebBlendModeColorBurn;
    case CSSValueHardLight:
      return kWebBlendModeHardLight;
    case CSSValueSoftLight:
      return kWebBlendModeSoftLight;
    case CSSValueDifference:
      return kWebBlendModeDifference;
    case CSSValueExclusion:
      return kWebBlendModeExclusion;
    case CSSValueHue:
      return kWebBlendModeHue;
    case CSSValueSaturation:
      return kWebBlendModeSaturation;
    case CSSValueColor:
      return kWebBlendModeColor;
    case CSSValueLuminosity:
      return kWebBlendModeLuminosity;
    default:
      break;
  }

  NOTREACHED();
  return kWebBlendModeNormal;
}

template <>
inline CSSIdentifierValue::CSSIdentifierValue(LineCap e)
    : CSSValue(kIdentifierClass) {
  switch (e) {
    case kButtCap:
      value_id_ = CSSValueButt;
      break;
    case kRoundCap:
      value_id_ = CSSValueRound;
      break;
    case kSquareCap:
      value_id_ = CSSValueSquare;
      break;
  }
}

template <>
inline LineCap CSSIdentifierValue::ConvertTo() const {
  switch (value_id_) {
    case CSSValueButt:
      return kButtCap;
    case CSSValueRound:
      return kRoundCap;
    case CSSValueSquare:
      return kSquareCap;
    default:
      break;
  }

  NOTREACHED();
  return kButtCap;
}

template <>
inline CSSIdentifierValue::CSSIdentifierValue(LineJoin e)
    : CSSValue(kIdentifierClass) {
  switch (e) {
    case kMiterJoin:
      value_id_ = CSSValueMiter;
      break;
    case kRoundJoin:
      value_id_ = CSSValueRound;
      break;
    case kBevelJoin:
      value_id_ = CSSValueBevel;
      break;
  }
}

template <>
inline LineJoin CSSIdentifierValue::ConvertTo() const {
  switch (value_id_) {
    case CSSValueMiter:
      return kMiterJoin;
    case CSSValueRound:
      return kRoundJoin;
    case CSSValueBevel:
      return kBevelJoin;
    default:
      break;
  }

  NOTREACHED();
  return kMiterJoin;
}

template <>
inline CSSIdentifierValue::CSSIdentifierValue(WindRule e)
    : CSSValue(kIdentifierClass) {
  switch (e) {
    case RULE_NONZERO:
      value_id_ = CSSValueNonzero;
      break;
    case RULE_EVENODD:
      value_id_ = CSSValueEvenodd;
      break;
  }
}

template <>
inline WindRule CSSIdentifierValue::ConvertTo() const {
  switch (value_id_) {
    case CSSValueNonzero:
      return RULE_NONZERO;
    case CSSValueEvenodd:
      return RULE_EVENODD;
    default:
      break;
  }

  NOTREACHED();
  return RULE_NONZERO;
}

template <>
inline CSSIdentifierValue::CSSIdentifierValue(EAlignmentBaseline e)
    : CSSValue(kIdentifierClass) {
  switch (e) {
    case AB_AUTO:
      value_id_ = CSSValueAuto;
      break;
    case AB_BASELINE:
      value_id_ = CSSValueBaseline;
      break;
    case AB_BEFORE_EDGE:
      value_id_ = CSSValueBeforeEdge;
      break;
    case AB_TEXT_BEFORE_EDGE:
      value_id_ = CSSValueTextBeforeEdge;
      break;
    case AB_MIDDLE:
      value_id_ = CSSValueMiddle;
      break;
    case AB_CENTRAL:
      value_id_ = CSSValueCentral;
      break;
    case AB_AFTER_EDGE:
      value_id_ = CSSValueAfterEdge;
      break;
    case AB_TEXT_AFTER_EDGE:
      value_id_ = CSSValueTextAfterEdge;
      break;
    case AB_IDEOGRAPHIC:
      value_id_ = CSSValueIdeographic;
      break;
    case AB_ALPHABETIC:
      value_id_ = CSSValueAlphabetic;
      break;
    case AB_HANGING:
      value_id_ = CSSValueHanging;
      break;
    case AB_MATHEMATICAL:
      value_id_ = CSSValueMathematical;
      break;
  }
}

template <>
inline EAlignmentBaseline CSSIdentifierValue::ConvertTo() const {
  switch (value_id_) {
    case CSSValueAuto:
      return AB_AUTO;
    case CSSValueBaseline:
      return AB_BASELINE;
    case CSSValueBeforeEdge:
      return AB_BEFORE_EDGE;
    case CSSValueTextBeforeEdge:
      return AB_TEXT_BEFORE_EDGE;
    case CSSValueMiddle:
      return AB_MIDDLE;
    case CSSValueCentral:
      return AB_CENTRAL;
    case CSSValueAfterEdge:
      return AB_AFTER_EDGE;
    case CSSValueTextAfterEdge:
      return AB_TEXT_AFTER_EDGE;
    case CSSValueIdeographic:
      return AB_IDEOGRAPHIC;
    case CSSValueAlphabetic:
      return AB_ALPHABETIC;
    case CSSValueHanging:
      return AB_HANGING;
    case CSSValueMathematical:
      return AB_MATHEMATICAL;
    default:
      break;
  }

  NOTREACHED();
  return AB_AUTO;
}

template <>
inline CSSIdentifierValue::CSSIdentifierValue(EImageRendering e)
    : CSSValue(kIdentifierClass) {
  switch (e) {
    case kImageRenderingAuto:
      value_id_ = CSSValueAuto;
      break;
    case kImageRenderingOptimizeSpeed:
      value_id_ = CSSValueOptimizeSpeed;
      break;
    case kImageRenderingOptimizeQuality:
      value_id_ = CSSValueOptimizeQuality;
      break;
    case kImageRenderingPixelated:
      value_id_ = CSSValuePixelated;
      break;
    case kImageRenderingOptimizeContrast:
      value_id_ = CSSValueWebkitOptimizeContrast;
      break;
  }
}

template <>
inline EImageRendering CSSIdentifierValue::ConvertTo() const {
  switch (value_id_) {
    case CSSValueAuto:
      return kImageRenderingAuto;
    case CSSValueOptimizeSpeed:
      return kImageRenderingOptimizeSpeed;
    case CSSValueOptimizeQuality:
      return kImageRenderingOptimizeQuality;
    case CSSValuePixelated:
      return kImageRenderingPixelated;
    case CSSValueWebkitOptimizeContrast:
      return kImageRenderingOptimizeContrast;
    default:
      break;
  }

  NOTREACHED();
  return kImageRenderingAuto;
}

template <>
inline CSSIdentifierValue::CSSIdentifierValue(ETransformStyle3D e)
    : CSSValue(kIdentifierClass) {
  switch (e) {
    case kTransformStyle3DFlat:
      value_id_ = CSSValueFlat;
      break;
    case kTransformStyle3DPreserve3D:
      value_id_ = CSSValuePreserve3d;
      break;
  }
}

template <>
inline ETransformStyle3D CSSIdentifierValue::ConvertTo() const {
  switch (value_id_) {
    case CSSValueFlat:
      return kTransformStyle3DFlat;
    case CSSValuePreserve3d:
      return kTransformStyle3DPreserve3D;
    default:
      break;
  }

  NOTREACHED();
  return kTransformStyle3DFlat;
}

template <>
inline CSSIdentifierValue::CSSIdentifierValue(EBufferedRendering e)
    : CSSValue(kIdentifierClass) {
  switch (e) {
    case BR_AUTO:
      value_id_ = CSSValueAuto;
      break;
    case BR_DYNAMIC:
      value_id_ = CSSValueDynamic;
      break;
    case BR_STATIC:
      value_id_ = CSSValueStatic;
      break;
  }
}

template <>
inline EBufferedRendering CSSIdentifierValue::ConvertTo() const {
  switch (value_id_) {
    case CSSValueAuto:
      return BR_AUTO;
    case CSSValueDynamic:
      return BR_DYNAMIC;
    case CSSValueStatic:
      return BR_STATIC;
    default:
      break;
  }

  NOTREACHED();
  return BR_AUTO;
}

template <>
inline CSSIdentifierValue::CSSIdentifierValue(EColorInterpolation e)
    : CSSValue(kIdentifierClass) {
  switch (e) {
    case CI_AUTO:
      value_id_ = CSSValueAuto;
      break;
    case CI_SRGB:
      value_id_ = CSSValueSRGB;
      break;
    case CI_LINEARRGB:
      value_id_ = CSSValueLinearRGB;
      break;
  }
}

template <>
inline EColorInterpolation CSSIdentifierValue::ConvertTo() const {
  switch (value_id_) {
    case CSSValueSRGB:
      return CI_SRGB;
    case CSSValueLinearRGB:
      return CI_LINEARRGB;
    case CSSValueAuto:
      return CI_AUTO;
    default:
      break;
  }

  NOTREACHED();
  return CI_AUTO;
}

template <>
inline CSSIdentifierValue::CSSIdentifierValue(EColorRendering e)
    : CSSValue(kIdentifierClass) {
  switch (e) {
    case CR_AUTO:
      value_id_ = CSSValueAuto;
      break;
    case CR_OPTIMIZESPEED:
      value_id_ = CSSValueOptimizeSpeed;
      break;
    case CR_OPTIMIZEQUALITY:
      value_id_ = CSSValueOptimizeQuality;
      break;
  }
}

template <>
inline EColorRendering CSSIdentifierValue::ConvertTo() const {
  switch (value_id_) {
    case CSSValueOptimizeSpeed:
      return CR_OPTIMIZESPEED;
    case CSSValueOptimizeQuality:
      return CR_OPTIMIZEQUALITY;
    case CSSValueAuto:
      return CR_AUTO;
    default:
      break;
  }

  NOTREACHED();
  return CR_AUTO;
}

template <>
inline CSSIdentifierValue::CSSIdentifierValue(EDominantBaseline e)
    : CSSValue(kIdentifierClass) {
  switch (e) {
    case DB_AUTO:
      value_id_ = CSSValueAuto;
      break;
    case DB_USE_SCRIPT:
      value_id_ = CSSValueUseScript;
      break;
    case DB_NO_CHANGE:
      value_id_ = CSSValueNoChange;
      break;
    case DB_RESET_SIZE:
      value_id_ = CSSValueResetSize;
      break;
    case DB_CENTRAL:
      value_id_ = CSSValueCentral;
      break;
    case DB_MIDDLE:
      value_id_ = CSSValueMiddle;
      break;
    case DB_TEXT_BEFORE_EDGE:
      value_id_ = CSSValueTextBeforeEdge;
      break;
    case DB_TEXT_AFTER_EDGE:
      value_id_ = CSSValueTextAfterEdge;
      break;
    case DB_IDEOGRAPHIC:
      value_id_ = CSSValueIdeographic;
      break;
    case DB_ALPHABETIC:
      value_id_ = CSSValueAlphabetic;
      break;
    case DB_HANGING:
      value_id_ = CSSValueHanging;
      break;
    case DB_MATHEMATICAL:
      value_id_ = CSSValueMathematical;
      break;
  }
}

template <>
inline EDominantBaseline CSSIdentifierValue::ConvertTo() const {
  switch (value_id_) {
    case CSSValueAuto:
      return DB_AUTO;
    case CSSValueUseScript:
      return DB_USE_SCRIPT;
    case CSSValueNoChange:
      return DB_NO_CHANGE;
    case CSSValueResetSize:
      return DB_RESET_SIZE;
    case CSSValueIdeographic:
      return DB_IDEOGRAPHIC;
    case CSSValueAlphabetic:
      return DB_ALPHABETIC;
    case CSSValueHanging:
      return DB_HANGING;
    case CSSValueMathematical:
      return DB_MATHEMATICAL;
    case CSSValueCentral:
      return DB_CENTRAL;
    case CSSValueMiddle:
      return DB_MIDDLE;
    case CSSValueTextAfterEdge:
      return DB_TEXT_AFTER_EDGE;
    case CSSValueTextBeforeEdge:
      return DB_TEXT_BEFORE_EDGE;
    default:
      break;
  }

  NOTREACHED();
  return DB_AUTO;
}

template <>
inline CSSIdentifierValue::CSSIdentifierValue(EShapeRendering e)
    : CSSValue(kIdentifierClass) {
  switch (e) {
    case SR_AUTO:
      value_id_ = CSSValueAuto;
      break;
    case SR_OPTIMIZESPEED:
      value_id_ = CSSValueOptimizeSpeed;
      break;
    case SR_CRISPEDGES:
      value_id_ = CSSValueCrispEdges;
      break;
    case SR_GEOMETRICPRECISION:
      value_id_ = CSSValueGeometricPrecision;
      break;
  }
}

template <>
inline EShapeRendering CSSIdentifierValue::ConvertTo() const {
  switch (value_id_) {
    case CSSValueAuto:
      return SR_AUTO;
    case CSSValueOptimizeSpeed:
      return SR_OPTIMIZESPEED;
    case CSSValueCrispEdges:
      return SR_CRISPEDGES;
    case CSSValueGeometricPrecision:
      return SR_GEOMETRICPRECISION;
    default:
      break;
  }

  NOTREACHED();
  return SR_AUTO;
}

template <>
inline CSSIdentifierValue::CSSIdentifierValue(ETextAnchor e)
    : CSSValue(kIdentifierClass) {
  switch (e) {
    case TA_START:
      value_id_ = CSSValueStart;
      break;
    case TA_MIDDLE:
      value_id_ = CSSValueMiddle;
      break;
    case TA_END:
      value_id_ = CSSValueEnd;
      break;
  }
}

template <>
inline ETextAnchor CSSIdentifierValue::ConvertTo() const {
  switch (value_id_) {
    case CSSValueStart:
      return TA_START;
    case CSSValueMiddle:
      return TA_MIDDLE;
    case CSSValueEnd:
      return TA_END;
    default:
      break;
  }

  NOTREACHED();
  return TA_START;
}

template <>
inline CSSIdentifierValue::CSSIdentifierValue(EVectorEffect e)
    : CSSValue(kIdentifierClass) {
  switch (e) {
    case VE_NONE:
      value_id_ = CSSValueNone;
      break;
    case VE_NON_SCALING_STROKE:
      value_id_ = CSSValueNonScalingStroke;
      break;
  }
}

template <>
inline EVectorEffect CSSIdentifierValue::ConvertTo() const {
  switch (value_id_) {
    case CSSValueNone:
      return VE_NONE;
    case CSSValueNonScalingStroke:
      return VE_NON_SCALING_STROKE;
    default:
      break;
  }

  NOTREACHED();
  return VE_NONE;
}

template <>
inline CSSIdentifierValue::CSSIdentifierValue(EPaintOrderType e)
    : CSSValue(kIdentifierClass) {
  switch (e) {
    case PT_FILL:
      value_id_ = CSSValueFill;
      break;
    case PT_STROKE:
      value_id_ = CSSValueStroke;
      break;
    case PT_MARKERS:
      value_id_ = CSSValueMarkers;
      break;
    default:
      NOTREACHED();
      value_id_ = CSSValueFill;
      break;
  }
}

template <>
inline EPaintOrderType CSSIdentifierValue::ConvertTo() const {
  switch (value_id_) {
    case CSSValueFill:
      return PT_FILL;
    case CSSValueStroke:
      return PT_STROKE;
    case CSSValueMarkers:
      return PT_MARKERS;
    default:
      break;
  }

  NOTREACHED();
  return PT_NONE;
}

template <>
inline CSSIdentifierValue::CSSIdentifierValue(EMaskType e)
    : CSSValue(kIdentifierClass) {
  switch (e) {
    case MT_LUMINANCE:
      value_id_ = CSSValueLuminance;
      break;
    case MT_ALPHA:
      value_id_ = CSSValueAlpha;
      break;
  }
}

template <>
inline EMaskType CSSIdentifierValue::ConvertTo() const {
  switch (value_id_) {
    case CSSValueLuminance:
      return MT_LUMINANCE;
    case CSSValueAlpha:
      return MT_ALPHA;
    default:
      break;
  }

  NOTREACHED();
  return MT_LUMINANCE;
}

template <>
inline TouchAction CSSIdentifierValue::ConvertTo() const {
  switch (value_id_) {
    case CSSValueNone:
      return TouchAction::kTouchActionNone;
    case CSSValueAuto:
      return TouchAction::kTouchActionAuto;
    case CSSValuePanLeft:
      return TouchAction::kTouchActionPanLeft;
    case CSSValuePanRight:
      return TouchAction::kTouchActionPanRight;
    case CSSValuePanX:
      return TouchAction::kTouchActionPanX;
    case CSSValuePanUp:
      return TouchAction::kTouchActionPanUp;
    case CSSValuePanDown:
      return TouchAction::kTouchActionPanDown;
    case CSSValuePanY:
      return TouchAction::kTouchActionPanY;
    case CSSValueManipulation:
      return TouchAction::kTouchActionManipulation;
    case CSSValuePinchZoom:
      return TouchAction::kTouchActionPinchZoom;
    default:
      break;
  }

  NOTREACHED();
  return TouchAction::kTouchActionNone;
}

template <>
inline CSSIdentifierValue::CSSIdentifierValue(EIsolation i)
    : CSSValue(kIdentifierClass) {
  switch (i) {
    case kIsolationAuto:
      value_id_ = CSSValueAuto;
      break;
    case kIsolationIsolate:
      value_id_ = CSSValueIsolate;
      break;
  }
}

template <>
inline EIsolation CSSIdentifierValue::ConvertTo() const {
  switch (value_id_) {
    case CSSValueAuto:
      return kIsolationAuto;
    case CSSValueIsolate:
      return kIsolationIsolate;
    default:
      break;
  }

  NOTREACHED();
  return kIsolationAuto;
}

template <>
inline CSSIdentifierValue::CSSIdentifierValue(CSSBoxType css_box)
    : CSSValue(kIdentifierClass) {
  switch (css_box) {
    case kMarginBox:
      value_id_ = CSSValueMarginBox;
      break;
    case kBorderBox:
      value_id_ = CSSValueBorderBox;
      break;
    case kPaddingBox:
      value_id_ = CSSValuePaddingBox;
      break;
    case kContentBox:
      value_id_ = CSSValueContentBox;
      break;
    case kBoxMissing:
      // The missing box should convert to a null primitive value.
      NOTREACHED();
  }
}

template <>
inline CSSBoxType CSSIdentifierValue::ConvertTo() const {
  switch (GetValueID()) {
    case CSSValueMarginBox:
      return kMarginBox;
    case CSSValueBorderBox:
      return kBorderBox;
    case CSSValuePaddingBox:
      return kPaddingBox;
    case CSSValueContentBox:
      return kContentBox;
    default:
      break;
  }
  NOTREACHED();
  return kContentBox;
}

template <>
inline CSSIdentifierValue::CSSIdentifierValue(ItemPosition item_position)
    : CSSValue(kIdentifierClass) {
  switch (item_position) {
    case kItemPositionAuto:
      // The 'auto' values might have been already resolved.
      NOTREACHED();
      value_id_ = CSSValueNormal;
      break;
    case kItemPositionNormal:
      value_id_ = CSSValueNormal;
      break;
    case kItemPositionStretch:
      value_id_ = CSSValueStretch;
      break;
    case kItemPositionBaseline:
      value_id_ = CSSValueBaseline;
      break;
    case kItemPositionLastBaseline:
      value_id_ = CSSValueLastBaseline;
      break;
    case kItemPositionCenter:
      value_id_ = CSSValueCenter;
      break;
    case kItemPositionStart:
      value_id_ = CSSValueStart;
      break;
    case kItemPositionEnd:
      value_id_ = CSSValueEnd;
      break;
    case kItemPositionSelfStart:
      value_id_ = CSSValueSelfStart;
      break;
    case kItemPositionSelfEnd:
      value_id_ = CSSValueSelfEnd;
      break;
    case kItemPositionFlexStart:
      value_id_ = CSSValueFlexStart;
      break;
    case kItemPositionFlexEnd:
      value_id_ = CSSValueFlexEnd;
      break;
    case kItemPositionLeft:
      value_id_ = CSSValueLeft;
      break;
    case kItemPositionRight:
      value_id_ = CSSValueRight;
      break;
  }
}

template <>
inline ItemPosition CSSIdentifierValue::ConvertTo() const {
  switch (value_id_) {
    case CSSValueAuto:
      return kItemPositionAuto;
    case CSSValueNormal:
      return kItemPositionNormal;
    case CSSValueStretch:
      return kItemPositionStretch;
    case CSSValueBaseline:
      return kItemPositionBaseline;
    case CSSValueFirstBaseline:
      return kItemPositionBaseline;
    case CSSValueLastBaseline:
      return kItemPositionLastBaseline;
    case CSSValueCenter:
      return kItemPositionCenter;
    case CSSValueStart:
      return kItemPositionStart;
    case CSSValueEnd:
      return kItemPositionEnd;
    case CSSValueSelfStart:
      return kItemPositionSelfStart;
    case CSSValueSelfEnd:
      return kItemPositionSelfEnd;
    case CSSValueFlexStart:
      return kItemPositionFlexStart;
    case CSSValueFlexEnd:
      return kItemPositionFlexEnd;
    case CSSValueLeft:
      return kItemPositionLeft;
    case CSSValueRight:
      return kItemPositionRight;
    default:
      break;
  }
  NOTREACHED();
  return kItemPositionAuto;
}

template <>
inline CSSIdentifierValue::CSSIdentifierValue(ContentPosition content_position)
    : CSSValue(kIdentifierClass) {
  switch (content_position) {
    case kContentPositionNormal:
      value_id_ = CSSValueNormal;
      break;
    case kContentPositionBaseline:
      value_id_ = CSSValueBaseline;
      break;
    case kContentPositionLastBaseline:
      value_id_ = CSSValueLastBaseline;
      break;
    case kContentPositionCenter:
      value_id_ = CSSValueCenter;
      break;
    case kContentPositionStart:
      value_id_ = CSSValueStart;
      break;
    case kContentPositionEnd:
      value_id_ = CSSValueEnd;
      break;
    case kContentPositionFlexStart:
      value_id_ = CSSValueFlexStart;
      break;
    case kContentPositionFlexEnd:
      value_id_ = CSSValueFlexEnd;
      break;
    case kContentPositionLeft:
      value_id_ = CSSValueLeft;
      break;
    case kContentPositionRight:
      value_id_ = CSSValueRight;
      break;
  }
}

template <>
inline ContentPosition CSSIdentifierValue::ConvertTo() const {
  switch (value_id_) {
    case CSSValueNormal:
      return kContentPositionNormal;
    case CSSValueBaseline:
      return kContentPositionBaseline;
    case CSSValueFirstBaseline:
      return kContentPositionBaseline;
    case CSSValueLastBaseline:
      return kContentPositionLastBaseline;
    case CSSValueCenter:
      return kContentPositionCenter;
    case CSSValueStart:
      return kContentPositionStart;
    case CSSValueEnd:
      return kContentPositionEnd;
    case CSSValueFlexStart:
      return kContentPositionFlexStart;
    case CSSValueFlexEnd:
      return kContentPositionFlexEnd;
    case CSSValueLeft:
      return kContentPositionLeft;
    case CSSValueRight:
      return kContentPositionRight;
    default:
      break;
  }
  NOTREACHED();
  return kContentPositionNormal;
}

template <>
inline CSSIdentifierValue::CSSIdentifierValue(
    ContentDistributionType content_distribution)
    : CSSValue(kIdentifierClass) {
  switch (content_distribution) {
    case kContentDistributionDefault:
      value_id_ = CSSValueDefault;
      break;
    case kContentDistributionSpaceBetween:
      value_id_ = CSSValueSpaceBetween;
      break;
    case kContentDistributionSpaceAround:
      value_id_ = CSSValueSpaceAround;
      break;
    case kContentDistributionSpaceEvenly:
      value_id_ = CSSValueSpaceEvenly;
      break;
    case kContentDistributionStretch:
      value_id_ = CSSValueStretch;
      break;
  }
}

template <>
inline ContentDistributionType CSSIdentifierValue::ConvertTo() const {
  switch (value_id_) {
    case CSSValueSpaceBetween:
      return kContentDistributionSpaceBetween;
    case CSSValueSpaceAround:
      return kContentDistributionSpaceAround;
    case CSSValueSpaceEvenly:
      return kContentDistributionSpaceEvenly;
    case CSSValueStretch:
      return kContentDistributionStretch;
    default:
      break;
  }
  NOTREACHED();
  return kContentDistributionStretch;
}

template <>
inline CSSIdentifierValue::CSSIdentifierValue(
    OverflowAlignment overflow_alignment)
    : CSSValue(kIdentifierClass) {
  switch (overflow_alignment) {
    case kOverflowAlignmentDefault:
      value_id_ = CSSValueDefault;
      break;
    case kOverflowAlignmentUnsafe:
      value_id_ = CSSValueUnsafe;
      break;
    case kOverflowAlignmentSafe:
      value_id_ = CSSValueSafe;
      break;
  }
}

template <>
inline OverflowAlignment CSSIdentifierValue::ConvertTo() const {
  switch (value_id_) {
    case CSSValueUnsafe:
      return kOverflowAlignmentUnsafe;
    case CSSValueSafe:
      return kOverflowAlignmentSafe;
    default:
      break;
  }
  NOTREACHED();
  return kOverflowAlignmentUnsafe;
}

template <>
inline CSSIdentifierValue::CSSIdentifierValue(ScrollBehavior behavior)
    : CSSValue(kIdentifierClass) {
  switch (behavior) {
    case kScrollBehaviorAuto:
      value_id_ = CSSValueAuto;
      break;
    case kScrollBehaviorSmooth:
      value_id_ = CSSValueSmooth;
      break;
    case kScrollBehaviorInstant:
      // Behavior 'instant' is only allowed in ScrollOptions arguments passed to
      // CSSOM scroll APIs.
      NOTREACHED();
  }
}

template <>
inline ScrollBehavior CSSIdentifierValue::ConvertTo() const {
  switch (GetValueID()) {
    case CSSValueAuto:
      return kScrollBehaviorAuto;
    case CSSValueSmooth:
      return kScrollBehaviorSmooth;
    default:
      break;
  }
  NOTREACHED();
  return kScrollBehaviorAuto;
}

template <>
inline CSSIdentifierValue::CSSIdentifierValue(ScrollSnapType snap_type)
    : CSSValue(kIdentifierClass) {
  switch (snap_type) {
    case kScrollSnapTypeNone:
      value_id_ = CSSValueNone;
      break;
    case kScrollSnapTypeMandatory:
      value_id_ = CSSValueMandatory;
      break;
    case kScrollSnapTypeProximity:
      value_id_ = CSSValueProximity;
      break;
  }
}

template <>
inline ScrollSnapType CSSIdentifierValue::ConvertTo() const {
  switch (GetValueID()) {
    case CSSValueNone:
      return kScrollSnapTypeNone;
    case CSSValueMandatory:
      return kScrollSnapTypeMandatory;
    case CSSValueProximity:
      return kScrollSnapTypeProximity;
    default:
      break;
  }
  NOTREACHED();
  return kScrollSnapTypeNone;
}

template <>
inline CSSIdentifierValue::CSSIdentifierValue(Containment snap_type)
    : CSSValue(kIdentifierClass) {
  switch (snap_type) {
    case kContainsNone:
      value_id_ = CSSValueNone;
      break;
    case kContainsStrict:
      value_id_ = CSSValueStrict;
      break;
    case kContainsContent:
      value_id_ = CSSValueContent;
      break;
    case kContainsPaint:
      value_id_ = CSSValuePaint;
      break;
    case kContainsStyle:
      value_id_ = CSSValueStyle;
      break;
    case kContainsLayout:
      value_id_ = CSSValueLayout;
      break;
    case kContainsSize:
      value_id_ = CSSValueSize;
      break;
  }
}

template <>
inline Containment CSSIdentifierValue::ConvertTo() const {
  switch (GetValueID()) {
    case CSSValueNone:
      return kContainsNone;
    case CSSValueStrict:
      return kContainsStrict;
    case CSSValueContent:
      return kContainsContent;
    case CSSValuePaint:
      return kContainsPaint;
    case CSSValueStyle:
      return kContainsStyle;
    case CSSValueLayout:
      return kContainsLayout;
    case CSSValueSize:
      return kContainsSize;
    default:
      break;
  }
  NOTREACHED();
  return kContainsNone;
}

}  // namespace blink

#endif
