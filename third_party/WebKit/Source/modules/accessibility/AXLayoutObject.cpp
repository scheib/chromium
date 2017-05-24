/*
* Copyright (C) 2008 Apple Inc. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions
* are met:
*
* 1.  Redistributions of source code must retain the above copyright
*     notice, this list of conditions and the following disclaimer.
* 2.  Redistributions in binary form must reproduce the above copyright
*     notice, this list of conditions and the following disclaimer in the
*     documentation and/or other materials provided with the distribution.
* 3.  Neither the name of Apple Computer, Inc. ("Apple") nor the names of
*     its contributors may be used to endorse or promote products derived
*     from this software without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND ANY
* EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
* DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR ANY
* DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
* (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
* LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
* ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
* (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
* THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "modules/accessibility/AXLayoutObject.h"

#include "bindings/core/v8/ExceptionState.h"
#include "core/CSSPropertyNames.h"
#include "core/InputTypeNames.h"
#include "core/dom/AccessibleNode.h"
#include "core/dom/ElementTraversal.h"
#include "core/dom/Range.h"
#include "core/dom/shadow/ShadowRoot.h"
#include "core/editing/EditingUtilities.h"
#include "core/editing/FrameSelection.h"
#include "core/editing/RenderedPosition.h"
#include "core/editing/TextAffinity.h"
#include "core/editing/VisibleUnits.h"
#include "core/editing/iterators/CharacterIterator.h"
#include "core/editing/iterators/TextIterator.h"
#include "core/frame/FrameOwner.h"
#include "core/frame/FrameView.h"
#include "core/frame/ImageBitmap.h"
#include "core/frame/LocalFrame.h"
#include "core/frame/Settings.h"
#include "core/html/HTMLCanvasElement.h"
#include "core/html/HTMLFrameOwnerElement.h"
#include "core/html/HTMLImageElement.h"
#include "core/html/HTMLInputElement.h"
#include "core/html/HTMLLabelElement.h"
#include "core/html/HTMLOptionElement.h"
#include "core/html/HTMLSelectElement.h"
#include "core/html/HTMLTextAreaElement.h"
#include "core/html/HTMLVideoElement.h"
#include "core/html/ImageData.h"
#include "core/html/LabelsNodeList.h"
#include "core/html/shadow/ShadowElementNames.h"
#include "core/imagebitmap/ImageBitmapOptions.h"
#include "core/layout/HitTestResult.h"
#include "core/layout/LayoutFileUploadControl.h"
#include "core/layout/LayoutHTMLCanvas.h"
#include "core/layout/LayoutImage.h"
#include "core/layout/LayoutInline.h"
#include "core/layout/LayoutListMarker.h"
#include "core/layout/LayoutMenuList.h"
#include "core/layout/LayoutTextControl.h"
#include "core/layout/LayoutTextFragment.h"
#include "core/layout/LayoutView.h"
#include "core/layout/api/LayoutAPIShim.h"
#include "core/layout/api/LayoutViewItem.h"
#include "core/layout/api/LineLayoutAPIShim.h"
#include "core/loader/ProgressTracker.h"
#include "core/page/Page.h"
#include "core/paint/PaintLayer.h"
#include "core/style/ComputedStyleConstants.h"
#include "core/svg/SVGDocumentExtensions.h"
#include "core/svg/SVGSVGElement.h"
#include "core/svg/graphics/SVGImage.h"
#include "modules/accessibility/AXImageMapLink.h"
#include "modules/accessibility/AXInlineTextBox.h"
#include "modules/accessibility/AXObjectCacheImpl.h"
#include "modules/accessibility/AXSVGRoot.h"
#include "modules/accessibility/AXSpinButton.h"
#include "modules/accessibility/AXTable.h"
#include "platform/fonts/FontTraits.h"
#include "platform/geometry/TransformState.h"
#include "platform/text/PlatformLocale.h"
#include "platform/text/TextDirection.h"
#include "platform/wtf/StdLibExtras.h"

using blink::WebLocalizedString;

namespace blink {

using namespace HTMLNames;

static inline LayoutObject* FirstChildInContinuation(
    const LayoutInline& layout_object) {
  LayoutBoxModelObject* r = layout_object.Continuation();

  while (r) {
    if (r->IsLayoutBlock())
      return r;
    if (LayoutObject* child = r->SlowFirstChild())
      return child;
    r = ToLayoutInline(r)->Continuation();
  }

  return 0;
}

static inline bool IsInlineWithContinuation(LayoutObject* object) {
  if (!object->IsBoxModelObject())
    return false;

  LayoutBoxModelObject* layout_object = ToLayoutBoxModelObject(object);
  if (!layout_object->IsLayoutInline())
    return false;

  return ToLayoutInline(layout_object)->Continuation();
}

static inline LayoutObject* FirstChildConsideringContinuation(
    LayoutObject* layout_object) {
  LayoutObject* first_child = layout_object->SlowFirstChild();

  // CSS first-letter pseudo element is handled as continuation. Returning it
  // will result in duplicated elements.
  if (first_child && first_child->IsText() &&
      ToLayoutText(first_child)->IsTextFragment() &&
      ToLayoutTextFragment(first_child)->GetFirstLetterPseudoElement())
    return nullptr;

  if (!first_child && IsInlineWithContinuation(layout_object))
    first_child = FirstChildInContinuation(ToLayoutInline(*layout_object));

  return first_child;
}

static inline LayoutInline* StartOfContinuations(LayoutObject* r) {
  if (r->IsInlineElementContinuation()) {
    return ToLayoutInline(r->GetNode()->GetLayoutObject());
  }

  // Blocks with a previous continuation always have a next continuation
  if (r->IsLayoutBlockFlow() &&
      ToLayoutBlockFlow(r)->InlineElementContinuation())
    return ToLayoutInline(ToLayoutBlockFlow(r)
                              ->InlineElementContinuation()
                              ->GetNode()
                              ->GetLayoutObject());

  return 0;
}

static inline LayoutObject* EndOfContinuations(LayoutObject* layout_object) {
  LayoutObject* prev = layout_object;
  LayoutObject* cur = layout_object;

  if (!cur->IsLayoutInline() && !cur->IsLayoutBlockFlow())
    return layout_object;

  while (cur) {
    prev = cur;
    if (cur->IsLayoutInline()) {
      cur = ToLayoutInline(cur)->InlineElementContinuation();
      DCHECK(cur || !ToLayoutInline(prev)->Continuation());
    } else {
      cur = ToLayoutBlockFlow(cur)->InlineElementContinuation();
    }
  }

  return prev;
}

static inline bool LastChildHasContinuation(LayoutObject* layout_object) {
  LayoutObject* last_child = layout_object->SlowLastChild();
  return last_child && IsInlineWithContinuation(last_child);
}

static LayoutBoxModelObject* NextContinuation(LayoutObject* layout_object) {
  DCHECK(layout_object);
  if (layout_object->IsLayoutInline() && !layout_object->IsAtomicInlineLevel())
    return ToLayoutInline(layout_object)->Continuation();
  if (layout_object->IsLayoutBlockFlow())
    return ToLayoutBlockFlow(layout_object)->InlineElementContinuation();
  return 0;
}

AXLayoutObject::AXLayoutObject(LayoutObject* layout_object,
                               AXObjectCacheImpl& ax_object_cache)
    : AXNodeObject(layout_object->GetNode(), ax_object_cache),
      layout_object_(layout_object) {
#if DCHECK_IS_ON()
  layout_object_->SetHasAXObject(true);
#endif
}

AXLayoutObject* AXLayoutObject::Create(LayoutObject* layout_object,
                                       AXObjectCacheImpl& ax_object_cache) {
  return new AXLayoutObject(layout_object, ax_object_cache);
}

AXLayoutObject::~AXLayoutObject() {
  DCHECK(IsDetached());
}

LayoutBoxModelObject* AXLayoutObject::GetLayoutBoxModelObject() const {
  if (!layout_object_ || !layout_object_->IsBoxModelObject())
    return 0;
  return ToLayoutBoxModelObject(layout_object_);
}

ScrollableArea* AXLayoutObject::GetScrollableAreaIfScrollable() const {
  if (IsWebArea())
    return DocumentFrameView()->LayoutViewportScrollableArea();

  if (!layout_object_ || !layout_object_->IsBox())
    return 0;

  LayoutBox* box = ToLayoutBox(layout_object_);
  if (!box->CanBeScrolledAndHasScrollableArea())
    return 0;

  return box->GetScrollableArea();
}

static bool IsImageOrAltText(LayoutBoxModelObject* box, Node* node) {
  if (box && box->IsImage())
    return true;
  if (isHTMLImageElement(node))
    return true;
  if (isHTMLInputElement(node) &&
      toHTMLInputElement(node)->HasFallbackContent())
    return true;
  return false;
}

AccessibilityRole AXLayoutObject::NativeAccessibilityRoleIgnoringAria() const {
  Node* node = layout_object_->GetNode();
  LayoutBoxModelObject* css_box = GetLayoutBoxModelObject();

  if ((css_box && css_box->IsListItem()) || isHTMLLIElement(node))
    return kListItemRole;
  if (layout_object_->IsListMarker())
    return kListMarkerRole;
  if (layout_object_->IsBR())
    return kLineBreakRole;
  if (layout_object_->IsText())
    return kStaticTextRole;
  if (css_box && IsImageOrAltText(css_box, node)) {
    if (node && node->IsLink())
      return kImageMapRole;
    if (isHTMLInputElement(node))
      return AriaHasPopup() ? kPopUpButtonRole : kButtonRole;
    if (IsSVGImage())
      return kSVGRootRole;
    return kImageRole;
  }
  // Note: if JavaScript is disabled, the layoutObject won't be a
  // LayoutHTMLCanvas.
  if (isHTMLCanvasElement(node) && layout_object_->IsCanvas())
    return kCanvasRole;

  if (css_box && css_box->IsLayoutView())
    return kWebAreaRole;

  if (layout_object_->IsSVGImage())
    return kImageRole;
  if (layout_object_->IsSVGRoot())
    return kSVGRootRole;

  // Table sections should be ignored.
  if (layout_object_->IsTableSection())
    return kIgnoredRole;

  if (layout_object_->IsHR())
    return kSplitterRole;

  return AXNodeObject::NativeAccessibilityRoleIgnoringAria();
}

AccessibilityRole AXLayoutObject::DetermineAccessibilityRole() {
  if (!layout_object_)
    return kUnknownRole;

  if ((aria_role_ = DetermineAriaRoleAttribute()) != kUnknownRole)
    return aria_role_;

  AccessibilityRole role = NativeAccessibilityRoleIgnoringAria();
  if (role != kUnknownRole)
    return role;

  // These are layout containers added by blink
  if (layout_object_->IsLayoutBlockFlow())
    return kGenericContainerRole;

  // If the element does not have role, but it has ARIA attributes or is an
  // in-page link target, accessibility should fallback to exposing it as a
  // generic container.
  if (IsInPageLinkTarget() || SupportsARIAAttributes())
    return kGenericContainerRole;

  return kUnknownRole;
}

void AXLayoutObject::Init() {
  AXNodeObject::Init();
}

void AXLayoutObject::Detach() {
  AXNodeObject::Detach();

  DetachRemoteSVGRoot();

#if DCHECK_IS_ON()
  if (layout_object_)
    layout_object_->SetHasAXObject(false);
#endif
  layout_object_ = 0;
}

//
// Check object role or purpose.
//

static bool IsLinkable(const AXObjectImpl& object) {
  if (!object.GetLayoutObject())
    return false;

  // See https://wiki.mozilla.org/Accessibility/AT-Windows-API for the elements
  // Mozilla considers linkable.
  return object.IsLink() || object.IsImage() ||
         object.GetLayoutObject()->IsText();
}

// Requires layoutObject to be present because it relies on style
// user-modify. Don't move this logic to AXNodeObject.
bool AXLayoutObject::IsEditable() const {
  if (GetLayoutObject() && GetLayoutObject()->IsTextControl())
    return true;

  if (GetNode() && HasEditableStyle(*GetNode()))
    return true;

  if (IsWebArea()) {
    Document& document = GetLayoutObject()->GetDocument();
    HTMLElement* body = document.body();
    if (body && HasEditableStyle(*body)) {
      AXObjectImpl* ax_body = AxObjectCache().GetOrCreate(body);
      return ax_body && ax_body != ax_body->AriaHiddenRoot();
    }

    return HasEditableStyle(document);
  }

  return AXNodeObject::IsEditable();
}

// Requires layoutObject to be present because it relies on style
// user-modify. Don't move this logic to AXNodeObject.
bool AXLayoutObject::IsRichlyEditable() const {
  if (GetNode() && HasRichlyEditableStyle(*GetNode()))
    return true;

  if (IsWebArea()) {
    Document& document = layout_object_->GetDocument();
    HTMLElement* body = document.body();
    if (body && HasRichlyEditableStyle(*body)) {
      AXObjectImpl* ax_body = AxObjectCache().GetOrCreate(body);
      return ax_body && ax_body != ax_body->AriaHiddenRoot();
    }

    return HasRichlyEditableStyle(document);
  }

  return AXNodeObject::IsRichlyEditable();
}

bool AXLayoutObject::IsLinked() const {
  if (!IsLinkable(*this))
    return false;

  Element* anchor = AnchorElement();
  if (!isHTMLAnchorElement(anchor))
    return false;

  return !toHTMLAnchorElement(*anchor).Href().IsEmpty();
}

bool AXLayoutObject::IsLoaded() const {
  return !layout_object_->GetDocument().Parser();
}

bool AXLayoutObject::IsOffScreen() const {
  DCHECK(layout_object_);
  IntRect content_rect =
      PixelSnappedIntRect(layout_object_->AbsoluteVisualRect());
  FrameView* view = layout_object_->GetFrame()->View();
  IntRect view_rect = view->VisibleContentRect();
  view_rect.Intersect(content_rect);
  return view_rect.IsEmpty();
}

bool AXLayoutObject::IsReadOnly() const {
  DCHECK(layout_object_);

  if (IsWebArea()) {
    Document& document = layout_object_->GetDocument();
    HTMLElement* body = document.body();
    if (body && HasEditableStyle(*body)) {
      AXObjectImpl* ax_body = AxObjectCache().GetOrCreate(body);
      return !ax_body || ax_body == ax_body->AriaHiddenRoot();
    }

    return !HasEditableStyle(document);
  }

  return AXNodeObject::IsReadOnly();
}

bool AXLayoutObject::IsVisited() const {
  // FIXME: Is it a privacy violation to expose visited information to
  // accessibility APIs?
  return layout_object_->Style()->IsLink() &&
         layout_object_->Style()->InsideLink() ==
             EInsideLink::kInsideVisitedLink;
}

//
// Check object state.
//

bool AXLayoutObject::IsFocused() const {
  if (!GetDocument())
    return false;

  Element* focused_element = GetDocument()->FocusedElement();
  if (!focused_element)
    return false;
  AXObjectImpl* focused_object = AxObjectCache().GetOrCreate(focused_element);
  if (!focused_object || !focused_object->IsAXLayoutObject())
    return false;

  // A web area is represented by the Document node in the DOM tree, which isn't
  // focusable.  Check instead if the frame's selection controller is focused
  if (focused_object == this ||
      (RoleValue() == kWebAreaRole &&
       GetDocument()->GetFrame()->Selection().FrameIsFocusedAndActive()))
    return true;

  return false;
}

bool AXLayoutObject::IsSelected() const {
  if (!GetLayoutObject() || !GetNode())
    return false;

  if (AOMPropertyOrARIAAttributeIsTrue(AOMBooleanProperty::kSelected))
    return true;

  AXObjectImpl* focused_object = AxObjectCache().FocusedObject();
  if (AriaRoleAttribute() == kListBoxOptionRole && focused_object &&
      focused_object->ActiveDescendant() == this) {
    return true;
  }

  if (IsTabItem() && IsTabItemSelected())
    return true;

  return false;
}

//
// Whether objects are ignored, i.e. not included in the tree.
//

AXObjectInclusion AXLayoutObject::DefaultObjectInclusion(
    IgnoredReasons* ignored_reasons) const {
  // The following cases can apply to any element that's a subclass of
  // AXLayoutObject.

  if (!layout_object_) {
    if (ignored_reasons)
      ignored_reasons->push_back(IgnoredReason(kAXNotRendered));
    return kIgnoreObject;
  }

  if (layout_object_->Style()->Visibility() != EVisibility::kVisible) {
    // aria-hidden is meant to override visibility as the determinant in AX
    // hierarchy inclusion.
    if (AOMPropertyOrARIAAttributeIsFalse(AOMBooleanProperty::kHidden))
      return kDefaultBehavior;

    if (ignored_reasons)
      ignored_reasons->push_back(IgnoredReason(kAXNotVisible));
    return kIgnoreObject;
  }

  return AXObjectImpl::DefaultObjectInclusion(ignored_reasons);
}

bool AXLayoutObject::ComputeAccessibilityIsIgnored(
    IgnoredReasons* ignored_reasons) const {
#if DCHECK_IS_ON()
  DCHECK(initialized_);
#endif

  if (!layout_object_)
    return true;

  // Check first if any of the common reasons cause this element to be ignored.
  // Then process other use cases that need to be applied to all the various
  // roles that AXLayoutObjects take on.
  AXObjectInclusion decision = DefaultObjectInclusion(ignored_reasons);
  if (decision == kIncludeObject)
    return false;
  if (decision == kIgnoreObject)
    return true;

  if (layout_object_->IsAnonymousBlock())
    return true;

  // If this element is within a parent that cannot have children, it should not
  // be exposed.
  if (IsDescendantOfLeafNode()) {
    if (ignored_reasons)
      ignored_reasons->push_back(
          IgnoredReason(kAXAncestorIsLeafNode, LeafNodeAncestor()));
    return true;
  }

  if (RoleValue() == kIgnoredRole) {
    if (ignored_reasons)
      ignored_reasons->push_back(IgnoredReason(kAXUninteresting));
    return true;
  }

  if (HasInheritedPresentationalRole()) {
    if (ignored_reasons) {
      const AXObjectImpl* inherits_from = InheritsPresentationalRoleFrom();
      if (inherits_from == this)
        ignored_reasons->push_back(IgnoredReason(kAXPresentationalRole));
      else
        ignored_reasons->push_back(
            IgnoredReason(kAXInheritsPresentation, inherits_from));
    }
    return true;
  }

  // An ARIA tree can only have tree items and static text as children.
  if (AXObjectImpl* tree_ancestor = TreeAncestorDisallowingChild()) {
    if (ignored_reasons)
      ignored_reasons->push_back(
          IgnoredReason(kAXAncestorDisallowsChild, tree_ancestor));
    return true;
  }

  // A LayoutPart is an iframe element or embedded object element or something
  // like that. We don't want to ignore those.
  if (layout_object_->IsLayoutPart())
    return false;

  // Make sure renderers with layers stay in the tree.
  if (GetLayoutObject() && GetLayoutObject()->HasLayer() && GetNode() &&
      GetNode()->hasChildren())
    return false;

  // Find out if this element is inside of a label element.  If so, it may be
  // ignored because it's the label for a checkbox or radio button.
  AXObjectImpl* control_object = CorrespondingControlForLabelElement();
  if (control_object && control_object->IsCheckboxOrRadio() &&
      control_object->NameFromLabelElement()) {
    if (ignored_reasons) {
      HTMLLabelElement* label = LabelElementContainer();
      if (label && label != GetNode()) {
        AXObjectImpl* label_ax_object = AxObjectCache().GetOrCreate(label);
        ignored_reasons->push_back(
            IgnoredReason(kAXLabelContainer, label_ax_object));
      }

      ignored_reasons->push_back(IgnoredReason(kAXLabelFor, control_object));
    }
    return true;
  }

  if (layout_object_->IsBR())
    return false;

  if (IsLink())
    return false;

  if (IsInPageLinkTarget())
    return false;

  if (layout_object_->IsText()) {
    // Static text beneath MenuItems and MenuButtons are just reported along
    // with the menu item, so it's ignored on an individual level.
    AXObjectImpl* parent = ParentObjectUnignored();
    if (parent && (parent->AriaRoleAttribute() == kMenuItemRole ||
                   parent->AriaRoleAttribute() == kMenuButtonRole)) {
      if (ignored_reasons)
        ignored_reasons->push_back(
            IgnoredReason(kAXStaticTextUsedAsNameFor, parent));
      return true;
    }
    LayoutText* layout_text = ToLayoutText(layout_object_);
    if (!layout_text->HasTextBoxes()) {
      if (ignored_reasons)
        ignored_reasons->push_back(IgnoredReason(kAXEmptyText));
      return true;
    }

    // Don't ignore static text in editable text controls.
    for (AXObjectImpl* parent = ParentObject(); parent;
         parent = parent->ParentObject()) {
      if (parent->RoleValue() == kTextFieldRole)
        return false;
    }

    // Text elements that are just empty whitespace should not be returned.
    // FIXME(dmazzoni): we probably shouldn't ignore this if the style is 'pre',
    // or similar...
    if (layout_text->GetText().Impl()->ContainsOnlyWhitespace()) {
      if (ignored_reasons)
        ignored_reasons->push_back(IgnoredReason(kAXEmptyText));
      return true;
    }
    return false;
  }

  if (IsHeading())
    return false;

  if (IsLandmarkRelated())
    return false;

  // Header and footer tags may also be exposed as landmark roles but not
  // always.
  if (GetNode() &&
      (GetNode()->HasTagName(headerTag) || GetNode()->HasTagName(footerTag)))
    return false;

  // all controls are accessible
  if (IsControl())
    return false;

  if (AriaRoleAttribute() != kUnknownRole)
    return false;

  // don't ignore labels, because they serve as TitleUIElements
  Node* node = layout_object_->GetNode();
  if (isHTMLLabelElement(node))
    return false;

  // Anything that is content editable should not be ignored.
  // However, one cannot just call node->hasEditableStyle() since that will ask
  // if its parents are also editable. Only the top level content editable
  // region should be exposed.
  if (HasContentEditableAttributeSet())
    return false;

  if (RoleValue() == kAbbrRole)
    return false;

  // List items play an important role in defining the structure of lists. They
  // should not be ignored.
  if (RoleValue() == kListItemRole)
    return false;

  if (RoleValue() == kBlockquoteRole)
    return false;

  if (RoleValue() == kDialogRole)
    return false;

  if (RoleValue() == kFigcaptionRole)
    return false;

  if (RoleValue() == kFigureRole)
    return false;

  if (RoleValue() == kDetailsRole)
    return false;

  if (RoleValue() == kMarkRole)
    return false;

  if (RoleValue() == kMathRole)
    return false;

  if (RoleValue() == kMeterRole)
    return false;

  if (RoleValue() == kRubyRole)
    return false;

  if (RoleValue() == kSplitterRole)
    return false;

  if (RoleValue() == kTimeRole)
    return false;

  // if this element has aria attributes on it, it should not be ignored.
  if (SupportsARIAAttributes())
    return false;

  // <span> tags are inline tags and not meant to convey information if they
  // have no other aria information on them. If we don't ignore them, they may
  // emit signals expected to come from their parent. In addition, because
  // included spans are GroupRole objects, and GroupRole objects are often
  // containers with meaningful information, the inclusion of a span can have
  // the side effect of causing the immediate parent accessible to be ignored.
  // This is especially problematic for platforms which have distinct roles for
  // textual block elements.
  if (isHTMLSpanElement(node)) {
    if (ignored_reasons)
      ignored_reasons->push_back(IgnoredReason(kAXUninteresting));
    return true;
  }

  if (IsImage())
    return false;

  if (IsCanvas()) {
    if (CanvasHasFallbackContent())
      return false;
    LayoutHTMLCanvas* canvas = ToLayoutHTMLCanvas(layout_object_);
    if (canvas->Size().Height() <= 1 || canvas->Size().Width() <= 1) {
      if (ignored_reasons)
        ignored_reasons->push_back(IgnoredReason(kAXProbablyPresentational));
      return true;
    }
    // Otherwise fall through; use presence of help text, title, or description
    // to decide.
  }

  if (IsWebArea() || layout_object_->IsListMarker())
    return false;

  // Using the help text, title or accessibility description (so we
  // check if there's some kind of accessible name for the element)
  // to decide an element's visibility is not as definitive as
  // previous checks, so this should remain as one of the last.
  //
  // These checks are simplified in the interest of execution speed;
  // for example, any element having an alt attribute will make it
  // not ignored, rather than just images.
  if (!GetAttribute(aria_helpAttr).IsEmpty() ||
      !GetAttribute(aria_describedbyAttr).IsEmpty() ||
      !GetAttribute(altAttr).IsEmpty() || !GetAttribute(titleAttr).IsEmpty())
    return false;

  // Don't ignore generic focusable elements like <div tabindex=0>
  // unless they're completely empty, with no children.
  if (IsGenericFocusableElement() && node->hasChildren())
    return false;

  if (IsScrollableContainer())
    return false;

  // Ignore layout objects that are block flows with inline children. These
  // are usually dummy layout objects that pad out the tree, but there are
  // some exceptions below.
  if (layout_object_->IsLayoutBlockFlow() && layout_object_->ChildrenInline() &&
      !CanSetFocusAttribute()) {
    // If the layout object has any plain text in it, that text will be
    // inside a LineBox, so the layout object will have a first LineBox.
    bool has_any_text = !!ToLayoutBlockFlow(layout_object_)->FirstLineBox();

    // Always include interesting-looking objects.
    if (has_any_text || MouseButtonListener())
      return false;

    if (ignored_reasons)
      ignored_reasons->push_back(IgnoredReason(kAXUninteresting));
    return true;
  }

  // By default, objects should be ignored so that the AX hierarchy is not
  // filled with unnecessary items.
  if (ignored_reasons)
    ignored_reasons->push_back(IgnoredReason(kAXUninteresting));
  return true;
}

//
// Properties of static elements.
//

const AtomicString& AXLayoutObject::AccessKey() const {
  Node* node = layout_object_->GetNode();
  if (!node)
    return g_null_atom;
  if (!node->IsElementNode())
    return g_null_atom;
  return ToElement(node)->getAttribute(accesskeyAttr);
}

RGBA32 AXLayoutObject::ComputeBackgroundColor() const {
  if (!GetLayoutObject())
    return AXNodeObject::BackgroundColor();

  Color blended_color = Color::kTransparent;
  // Color::blend should be called like this: background.blend(foreground).
  for (LayoutObject* layout_object = GetLayoutObject(); layout_object;
       layout_object = layout_object->Parent()) {
    const AXObjectImpl* ax_parent = AxObjectCache().GetOrCreate(layout_object);
    if (ax_parent && ax_parent != this) {
      Color parent_color = ax_parent->BackgroundColor();
      blended_color = parent_color.Blend(blended_color);
      return blended_color.Rgb();
    }

    const ComputedStyle* style = layout_object->Style();
    if (!style || !style->HasBackground())
      continue;

    Color current_color =
        style->VisitedDependentColor(CSSPropertyBackgroundColor);
    blended_color = current_color.Blend(blended_color);
    // Continue blending until we get no transparency.
    if (!blended_color.HasAlpha())
      break;
  }

  // If we still have some transparency, blend in the document base color.
  if (blended_color.HasAlpha()) {
    FrameView* view = DocumentFrameView();
    if (view) {
      Color document_base_color = view->BaseBackgroundColor();
      blended_color = document_base_color.Blend(blended_color);
    } else {
      // Default to a white background.
      blended_color.BlendWithWhite();
    }
  }

  return blended_color.Rgb();
}

RGBA32 AXLayoutObject::GetColor() const {
  if (!GetLayoutObject() || IsColorWell())
    return AXNodeObject::GetColor();

  const ComputedStyle* style = GetLayoutObject()->Style();
  if (!style)
    return AXNodeObject::GetColor();

  Color color = style->VisitedDependentColor(CSSPropertyColor);
  return color.Rgb();
}

String AXLayoutObject::FontFamily() const {
  if (!GetLayoutObject())
    return AXNodeObject::FontFamily();

  const ComputedStyle* style = GetLayoutObject()->Style();
  if (!style)
    return AXNodeObject::FontFamily();

  FontDescription& font_description =
      const_cast<FontDescription&>(style->GetFontDescription());
  return font_description.FirstFamily().Family();
}

// Font size is in pixels.
float AXLayoutObject::FontSize() const {
  if (!GetLayoutObject())
    return AXNodeObject::FontSize();

  const ComputedStyle* style = GetLayoutObject()->Style();
  if (!style)
    return AXNodeObject::FontSize();

  return style->ComputedFontSize();
}

String AXLayoutObject::ImageDataUrl(const IntSize& max_size) const {
  Node* node = GetNode();
  if (!node)
    return String();

  ImageBitmapOptions options;
  ImageBitmap* image_bitmap = nullptr;
  Document* document = &node->GetDocument();
  if (isHTMLImageElement(node)) {
    image_bitmap = ImageBitmap::Create(toHTMLImageElement(node),
                                       Optional<IntRect>(), document, options);
  } else if (isHTMLCanvasElement(node)) {
    image_bitmap = ImageBitmap::Create(toHTMLCanvasElement(node),
                                       Optional<IntRect>(), options);
  } else if (isHTMLVideoElement(node)) {
    image_bitmap = ImageBitmap::Create(toHTMLVideoElement(node),
                                       Optional<IntRect>(), document, options);
  }
  if (!image_bitmap)
    return String();

  RefPtr<StaticBitmapImage> bitmap_image = image_bitmap->BitmapImage();
  if (!bitmap_image)
    return String();

  sk_sp<SkImage> image = bitmap_image->ImageForCurrentFrame();
  if (!image || image->width() <= 0 || image->height() <= 0)
    return String();

  // Determine the width and height of the output image, using a proportional
  // scale factor such that it's no larger than |maxSize|, if |maxSize| is not
  // empty. It only resizes the image to be smaller (if necessary), not
  // larger.
  float x_scale =
      max_size.Width() ? max_size.Width() * 1.0 / image->width() : 1.0;
  float y_scale =
      max_size.Height() ? max_size.Height() * 1.0 / image->height() : 1.0;
  float scale = std::min(x_scale, y_scale);
  if (scale >= 1.0)
    scale = 1.0;
  int width = std::round(image->width() * scale);
  int height = std::round(image->height() * scale);

  // Draw the scaled image into a bitmap in native format.
  SkBitmap bitmap;
  bitmap.allocPixels(SkImageInfo::MakeN32(width, height, kPremul_SkAlphaType));
  SkCanvas canvas(bitmap);
  canvas.clear(SK_ColorTRANSPARENT);
  canvas.drawImageRect(image, SkRect::MakeIWH(width, height), nullptr);

  // Copy the bits into a buffer in RGBA_8888 unpremultiplied format
  // for encoding.
  SkImageInfo info = SkImageInfo::Make(width, height, kRGBA_8888_SkColorType,
                                       kUnpremul_SkAlphaType);
  size_t row_bytes = info.minRowBytes();
  Vector<char> pixel_storage(info.getSafeSize(row_bytes));
  SkPixmap pixmap(info, pixel_storage.data(), row_bytes);
  if (!SkImage::MakeFromBitmap(bitmap)->readPixels(pixmap, 0, 0))
    return String();

  // Encode as a PNG and return as a data url.
  String data_url =
      ImageDataBuffer(
          IntSize(width, height),
          reinterpret_cast<const unsigned char*>(pixel_storage.data()))
          .ToDataURL("image/png", 1.0);
  return data_url;
}

String AXLayoutObject::GetText() const {
  if (IsPasswordFieldAndShouldHideValue()) {
    if (!GetLayoutObject())
      return String();

    const ComputedStyle* style = GetLayoutObject()->Style();
    if (!style)
      return String();

    unsigned unmasked_text_length = AXNodeObject::GetText().length();
    if (!unmasked_text_length)
      return String();

    UChar mask_character = 0;
    switch (style->TextSecurity()) {
      case ETextSecurity::kNone:
        break;  // Fall through to the non-password branch.
      case ETextSecurity::kDisc:
        mask_character = kBulletCharacter;
        break;
      case ETextSecurity::kCircle:
        mask_character = kWhiteBulletCharacter;
        break;
      case ETextSecurity::kSquare:
        mask_character = kBlackSquareCharacter;
        break;
    }
    if (mask_character) {
      StringBuilder masked_text;
      masked_text.ReserveCapacity(unmasked_text_length);
      for (unsigned i = 0; i < unmasked_text_length; ++i)
        masked_text.Append(mask_character);
      return masked_text.ToString();
    }
  }

  return AXNodeObject::GetText();
}

AccessibilityTextDirection AXLayoutObject::GetTextDirection() const {
  if (!GetLayoutObject())
    return AXNodeObject::GetTextDirection();

  const ComputedStyle* style = GetLayoutObject()->Style();
  if (!style)
    return AXNodeObject::GetTextDirection();

  if (style->IsHorizontalWritingMode()) {
    switch (style->Direction()) {
      case TextDirection::kLtr:
        return kAccessibilityTextDirectionLTR;
      case TextDirection::kRtl:
        return kAccessibilityTextDirectionRTL;
    }
  } else {
    switch (style->Direction()) {
      case TextDirection::kLtr:
        return kAccessibilityTextDirectionTTB;
      case TextDirection::kRtl:
        return kAccessibilityTextDirectionBTT;
    }
  }

  return AXNodeObject::GetTextDirection();
}

int AXLayoutObject::TextLength() const {
  if (!IsTextControl())
    return -1;

  return GetText().length();
}

TextStyle AXLayoutObject::GetTextStyle() const {
  if (!GetLayoutObject())
    return AXNodeObject::GetTextStyle();

  const ComputedStyle* style = GetLayoutObject()->Style();
  if (!style)
    return AXNodeObject::GetTextStyle();

  unsigned text_style = kTextStyleNone;
  if (style->GetFontWeight() == kFontWeightBold)
    text_style |= kTextStyleBold;
  if (style->GetFontDescription().Style() == kFontStyleItalic)
    text_style |= kTextStyleItalic;
  if (style->GetTextDecoration() == TextDecoration::kUnderline)
    text_style |= kTextStyleUnderline;
  if (style->GetTextDecoration() == TextDecoration::kLineThrough)
    text_style |= kTextStyleLineThrough;

  return static_cast<TextStyle>(text_style);
}

KURL AXLayoutObject::Url() const {
  if (IsAnchor() && isHTMLAnchorElement(layout_object_->GetNode())) {
    if (HTMLAnchorElement* anchor = toHTMLAnchorElement(AnchorElement()))
      return anchor->Href();
  }

  if (IsWebArea())
    return layout_object_->GetDocument().Url();

  if (IsImage() && isHTMLImageElement(layout_object_->GetNode()))
    return toHTMLImageElement(*layout_object_->GetNode()).Src();

  if (IsInputImage())
    return toHTMLInputElement(layout_object_->GetNode())->Src();

  return KURL();
}

//
// Inline text boxes.
//

void AXLayoutObject::LoadInlineTextBoxes() {
  if (!GetLayoutObject())
    return;

  if (GetLayoutObject()->IsText()) {
    ClearChildren();
    AddInlineTextBoxChildren(true);
    return;
  }

  for (const auto& child : children_) {
    child->LoadInlineTextBoxes();
  }
}

AXObjectImpl* AXLayoutObject::NextOnLine() const {
  if (!GetLayoutObject())
    return nullptr;

  AXObjectImpl* result = nullptr;
  if (GetLayoutObject()->IsListMarker()) {
    AXObjectImpl* next_sibling = RawNextSibling();
    if (!next_sibling || !next_sibling->Children().size())
      return nullptr;
    result = next_sibling->Children()[0].Get();
  } else {
    InlineBox* inline_box = nullptr;
    if (GetLayoutObject()->IsLayoutInline())
      inline_box = ToLayoutInline(GetLayoutObject())->LastLineBox();
    else if (GetLayoutObject()->IsText())
      inline_box = ToLayoutText(GetLayoutObject())->LastTextBox();

    if (!inline_box)
      return nullptr;

    for (InlineBox* next = inline_box->NextOnLine(); next;
         next = next->NextOnLine()) {
      LayoutObject* layout_object =
          LineLayoutAPIShim::LayoutObjectFrom(next->GetLineLayoutItem());
      result = AxObjectCache().GetOrCreate(layout_object);
      if (result)
        break;
    }
  }

  // A static text node might span multiple lines. Try to return the first
  // inline text box within that static text if possible.
  if (result && result->RoleValue() == kStaticTextRole &&
      result->Children().size())
    result = result->Children()[0].Get();

  return result;
}

AXObjectImpl* AXLayoutObject::PreviousOnLine() const {
  if (!GetLayoutObject())
    return nullptr;

  InlineBox* inline_box = nullptr;
  if (GetLayoutObject()->IsLayoutInline())
    inline_box = ToLayoutInline(GetLayoutObject())->FirstLineBox();
  else if (GetLayoutObject()->IsText())
    inline_box = ToLayoutText(GetLayoutObject())->FirstTextBox();

  if (!inline_box)
    return nullptr;

  AXObjectImpl* result = nullptr;
  for (InlineBox* prev = inline_box->PrevOnLine(); prev;
       prev = prev->PrevOnLine()) {
    LayoutObject* layout_object =
        LineLayoutAPIShim::LayoutObjectFrom(prev->GetLineLayoutItem());
    result = AxObjectCache().GetOrCreate(layout_object);
    if (result)
      break;
  }

  // A static text node might span multiple lines. Try to return the last inline
  // text box within that static text if possible.
  if (result && result->RoleValue() == kStaticTextRole &&
      result->Children().size())
    result = result->Children()[result->Children().size() - 1].Get();

  return result;
}

//
// Properties of interactive elements.
//

String AXLayoutObject::StringValue() const {
  if (!layout_object_)
    return String();

  LayoutBoxModelObject* css_box = GetLayoutBoxModelObject();

  if (css_box && css_box->IsMenuList()) {
    // LayoutMenuList will go straight to the text() of its selected item.
    // This has to be overridden in the case where the selected item has an ARIA
    // label.
    HTMLSelectElement* select_element =
        toHTMLSelectElement(layout_object_->GetNode());
    int selected_index = select_element->selectedIndex();
    const HeapVector<Member<HTMLElement>>& list_items =
        select_element->GetListItems();
    if (selected_index >= 0 &&
        static_cast<size_t>(selected_index) < list_items.size()) {
      const AtomicString& overridden_description =
          list_items[selected_index]->FastGetAttribute(aria_labelAttr);
      if (!overridden_description.IsNull())
        return overridden_description;
    }
    return ToLayoutMenuList(layout_object_)->GetText();
  }

  if (IsWebArea()) {
    // FIXME: Why would a layoutObject exist when the Document isn't attached to
    // a frame?
    if (layout_object_->GetFrame())
      return String();

    NOTREACHED();
  }

  if (IsTextControl())
    return GetText();

  if (layout_object_->IsFileUploadControl())
    return ToLayoutFileUploadControl(layout_object_)->FileTextValue();

  // Handle other HTML input elements that aren't text controls, like date and
  // time controls, by returning the string value, with the exception of
  // checkboxes and radio buttons (which would return "on").
  if (GetNode() && isHTMLInputElement(GetNode())) {
    HTMLInputElement* input = toHTMLInputElement(GetNode());
    if (input->type() != InputTypeNames::checkbox &&
        input->type() != InputTypeNames::radio)
      return input->value();
  }

  // FIXME: We might need to implement a value here for more types
  // FIXME: It would be better not to advertise a value at all for the types for
  // which we don't implement one; this would require subclassing or making
  // accessibilityAttributeNames do something other than return a single static
  // array.
  return String();
}

String AXLayoutObject::TextAlternative(bool recursive,
                                       bool in_aria_labelled_by_traversal,
                                       AXObjectSet& visited,
                                       AXNameFrom& name_from,
                                       AXRelatedObjectVector* related_objects,
                                       NameSources* name_sources) const {
  if (layout_object_) {
    String text_alternative;
    bool found_text_alternative = false;

    if (layout_object_->IsBR()) {
      text_alternative = String("\n");
      found_text_alternative = true;
    } else if (layout_object_->IsText() &&
               (!recursive || !layout_object_->IsCounter())) {
      LayoutText* layout_text = ToLayoutText(layout_object_);
      String result = layout_text->PlainText();
      if (!result.IsEmpty() || layout_text->IsAllCollapsibleWhitespace())
        text_alternative = result;
      else
        text_alternative = layout_text->GetText();
      found_text_alternative = true;
    } else if (layout_object_->IsListMarker() && !recursive) {
      text_alternative = ToLayoutListMarker(layout_object_)->GetText();
      found_text_alternative = true;
    }

    if (found_text_alternative) {
      name_from = kAXNameFromContents;
      if (name_sources) {
        name_sources->push_back(NameSource(false));
        name_sources->back().type = name_from;
        name_sources->back().text = text_alternative;
      }
      return text_alternative;
    }
  }

  return AXNodeObject::TextAlternative(recursive, in_aria_labelled_by_traversal,
                                       visited, name_from, related_objects,
                                       name_sources);
}

//
// ARIA attributes.
//

void AXLayoutObject::AriaOwnsElements(AXObjectVector& owns) const {
  AccessibilityChildrenFromAttribute(aria_ownsAttr, owns);
}

void AXLayoutObject::AriaDescribedbyElements(
    AXObjectVector& describedby) const {
  AccessibilityChildrenFromAttribute(aria_describedbyAttr, describedby);
}

void AXLayoutObject::AriaLabelledbyElements(AXObjectVector& labelledby) const {
  AccessibilityChildrenFromAttribute(aria_labelledbyAttr, labelledby);
}

bool AXLayoutObject::AriaHasPopup() const {
  return ElementAttributeValue(aria_haspopupAttr);
}

bool AXLayoutObject::AriaRoleHasPresentationalChildren() const {
  switch (aria_role_) {
    case kButtonRole:
    case kSliderRole:
    case kImageRole:
    case kProgressIndicatorRole:
    case kSpinButtonRole:
      // case SeparatorRole:
      return true;
    default:
      return false;
  }
}

AXObjectImpl* AXLayoutObject::AncestorForWhichThisIsAPresentationalChild()
    const {
  // Walk the parent chain looking for a parent that has presentational children
  AXObjectImpl* parent = ParentObjectIfExists();
  while (parent) {
    if (parent->AriaRoleHasPresentationalChildren())
      break;

    // The descendants of a AXMenuList that are AXLayoutObjects are all
    // presentational. (The real descendants are an AXMenuListPopup and
    // AXMenuListOptions, which are not AXLayoutObjects.)
    if (parent->IsMenuList())
      break;

    parent = parent->ParentObjectIfExists();
  }

  return parent;
}

bool AXLayoutObject::SupportsARIADragging() const {
  const AtomicString& grabbed = GetAttribute(aria_grabbedAttr);
  return EqualIgnoringASCIICase(grabbed, "true") ||
         EqualIgnoringASCIICase(grabbed, "false");
}

bool AXLayoutObject::SupportsARIADropping() const {
  const AtomicString& drop_effect = GetAttribute(aria_dropeffectAttr);
  return !drop_effect.IsEmpty();
}

bool AXLayoutObject::SupportsARIAFlowTo() const {
  return !GetAttribute(aria_flowtoAttr).IsEmpty();
}

bool AXLayoutObject::SupportsARIAOwns() const {
  if (!layout_object_)
    return false;
  const AtomicString& aria_owns = GetAttribute(aria_ownsAttr);

  return !aria_owns.IsEmpty();
}

//
// ARIA live-region features.
//

const AtomicString& AXLayoutObject::LiveRegionStatus() const {
  DEFINE_STATIC_LOCAL(const AtomicString, live_region_status_assertive,
                      ("assertive"));
  DEFINE_STATIC_LOCAL(const AtomicString, live_region_status_polite,
                      ("polite"));
  DEFINE_STATIC_LOCAL(const AtomicString, live_region_status_off, ("off"));

  const AtomicString& live_region_status =
      GetAOMPropertyOrARIAAttribute(AOMStringProperty::kLive);
  // These roles have implicit live region status.
  if (live_region_status.IsEmpty()) {
    switch (RoleValue()) {
      case kAlertDialogRole:
      case kAlertRole:
        return live_region_status_assertive;
      case kLogRole:
      case kStatusRole:
        return live_region_status_polite;
      case kTimerRole:
      case kMarqueeRole:
        return live_region_status_off;
      default:
        break;
    }
  }

  return live_region_status;
}

const AtomicString& AXLayoutObject::LiveRegionRelevant() const {
  DEFINE_STATIC_LOCAL(const AtomicString, default_live_region_relevant,
                      ("additions text"));
  const AtomicString& relevant =
      GetAOMPropertyOrARIAAttribute(AOMStringProperty::kRelevant);

  // Default aria-relevant = "additions text".
  if (relevant.IsEmpty())
    return default_live_region_relevant;

  return relevant;
}

bool AXLayoutObject::LiveRegionAtomic() const {
  bool atomic = false;
  if (HasAOMPropertyOrARIAAttribute(AOMBooleanProperty::kAtomic, atomic))
    return atomic;

  // ARIA roles "alert" and "status" should have an implicit aria-atomic value
  // of true.
  return RoleValue() == kAlertRole || RoleValue() == kStatusRole;
}

bool AXLayoutObject::LiveRegionBusy() const {
  return AOMPropertyOrARIAAttributeIsTrue(AOMBooleanProperty::kBusy);
}

//
// Hit testing.
//

AXObjectImpl* AXLayoutObject::AccessibilityHitTest(
    const IntPoint& point) const {
  if (!layout_object_ || !layout_object_->HasLayer())
    return nullptr;

  PaintLayer* layer = ToLayoutBox(layout_object_)->Layer();

  HitTestRequest request(HitTestRequest::kReadOnly | HitTestRequest::kActive);
  HitTestResult hit_test_result = HitTestResult(request, point);
  layer->HitTest(hit_test_result);

  Node* node = hit_test_result.InnerNode();
  if (!node)
    return nullptr;

  if (isHTMLAreaElement(node))
    return AccessibilityImageMapHitTest(toHTMLAreaElement(node), point);

  if (isHTMLOptionElement(node)) {
    node = toHTMLOptionElement(*node).OwnerSelectElement();
    if (!node)
      return nullptr;
  }

  LayoutObject* obj = node->GetLayoutObject();
  if (!obj)
    return nullptr;

  AXObjectImpl* result = AxObjectCache().GetOrCreate(obj);
  result->UpdateChildrenIfNecessary();

  // Allow the element to perform any hit-testing it might need to do to reach
  // non-layout children.
  result = result->ElementAccessibilityHitTest(point);
  if (result && result->AccessibilityIsIgnored()) {
    // If this element is the label of a control, a hit test should return the
    // control.
    if (result->IsAXLayoutObject()) {
      AXObjectImpl* control_object =
          ToAXLayoutObject(result)->CorrespondingControlForLabelElement();
      if (control_object && control_object->NameFromLabelElement())
        return control_object;
    }

    result = result->ParentObjectUnignored();
  }

  return result;
}

AXObjectImpl* AXLayoutObject::ElementAccessibilityHitTest(
    const IntPoint& point) const {
  if (IsSVGImage())
    return RemoteSVGElementHitTest(point);

  return AXObjectImpl::ElementAccessibilityHitTest(point);
}

//
// High-level accessibility tree access.
//

AXObjectImpl* AXLayoutObject::ComputeParent() const {
  DCHECK(!IsDetached());
  if (!layout_object_)
    return 0;

  if (AriaRoleAttribute() == kMenuBarRole)
    return AxObjectCache().GetOrCreate(layout_object_->Parent());

  // menuButton and its corresponding menu are DOM siblings, but Accessibility
  // needs them to be parent/child.
  if (AriaRoleAttribute() == kMenuRole) {
    AXObjectImpl* parent = MenuButtonForMenu();
    if (parent)
      return parent;
  }

  LayoutObject* parent_obj = LayoutParentObject();
  if (parent_obj)
    return AxObjectCache().GetOrCreate(parent_obj);

  // A WebArea's parent should be the page popup owner, if any, otherwise null.
  if (IsWebArea()) {
    LocalFrame* frame = layout_object_->GetFrame();
    return AxObjectCache().GetOrCreate(frame->PagePopupOwner());
  }

  return 0;
}

AXObjectImpl* AXLayoutObject::ComputeParentIfExists() const {
  if (!layout_object_)
    return 0;

  if (AriaRoleAttribute() == kMenuBarRole)
    return AxObjectCache().Get(layout_object_->Parent());

  // menuButton and its corresponding menu are DOM siblings, but Accessibility
  // needs them to be parent/child.
  if (AriaRoleAttribute() == kMenuRole) {
    AXObjectImpl* parent = MenuButtonForMenu();
    if (parent)
      return parent;
  }

  LayoutObject* parent_obj = LayoutParentObject();
  if (parent_obj)
    return AxObjectCache().Get(parent_obj);

  // A WebArea's parent should be the page popup owner, if any, otherwise null.
  if (IsWebArea()) {
    LocalFrame* frame = layout_object_->GetFrame();
    return AxObjectCache().Get(frame->PagePopupOwner());
  }

  return 0;
}

//
// Low-level accessibility tree exploration, only for use within the
// accessibility module.
//

AXObjectImpl* AXLayoutObject::RawFirstChild() const {
  if (!layout_object_)
    return 0;

  LayoutObject* first_child = FirstChildConsideringContinuation(layout_object_);

  if (!first_child)
    return 0;

  return AxObjectCache().GetOrCreate(first_child);
}

AXObjectImpl* AXLayoutObject::RawNextSibling() const {
  if (!layout_object_)
    return 0;

  LayoutObject* next_sibling = 0;

  LayoutInline* inline_continuation =
      layout_object_->IsLayoutBlockFlow()
          ? ToLayoutBlockFlow(layout_object_)->InlineElementContinuation()
          : nullptr;
  if (inline_continuation) {
    // Case 1: node is a block and has an inline continuation. Next sibling is
    // the inline continuation's first child.
    next_sibling = FirstChildConsideringContinuation(inline_continuation);
  } else if (layout_object_->IsAnonymousBlock() &&
             LastChildHasContinuation(layout_object_)) {
    // Case 2: Anonymous block parent of the start of a continuation - skip all
    // the way to after the parent of the end, since everything in between will
    // be linked up via the continuation.
    LayoutObject* last_parent =
        EndOfContinuations(ToLayoutBlock(layout_object_)->LastChild())
            ->Parent();
    while (LastChildHasContinuation(last_parent))
      last_parent = EndOfContinuations(last_parent->SlowLastChild())->Parent();
    next_sibling = last_parent->NextSibling();
  } else if (LayoutObject* ns = layout_object_->NextSibling()) {
    // Case 3: node has an actual next sibling
    next_sibling = ns;
  } else if (IsInlineWithContinuation(layout_object_)) {
    // Case 4: node is an inline with a continuation. Next sibling is the next
    // sibling of the end of the continuation chain.
    next_sibling = EndOfContinuations(layout_object_)->NextSibling();
  } else if (layout_object_->Parent() &&
             IsInlineWithContinuation(layout_object_->Parent())) {
    // Case 5: node has no next sibling, and its parent is an inline with a
    // continuation.
    LayoutObject* continuation =
        ToLayoutInline(layout_object_->Parent())->Continuation();

    if (continuation->IsLayoutBlock()) {
      // Case 5a: continuation is a block - in this case the block itself is the
      // next sibling.
      next_sibling = continuation;
    } else {
      // Case 5b: continuation is an inline - in this case the inline's first
      // child is the next sibling.
      next_sibling = FirstChildConsideringContinuation(continuation);
    }
  }

  if (!next_sibling)
    return 0;

  return AxObjectCache().GetOrCreate(next_sibling);
}

void AXLayoutObject::AddChildren() {
  DCHECK(!IsDetached());
  // If the need to add more children in addition to existing children arises,
  // childrenChanged should have been called, leaving the object with no
  // children.
  DCHECK(!have_children_);

  have_children_ = true;

  if (!CanHaveChildren())
    return;

  HeapVector<Member<AXObjectImpl>> owned_children;
  ComputeAriaOwnsChildren(owned_children);

  for (AXObjectImpl* obj = RawFirstChild(); obj; obj = obj->RawNextSibling()) {
    if (!AxObjectCache().IsAriaOwned(obj)) {
      obj->SetParent(this);
      AddChild(obj);
    }
  }

  AddHiddenChildren();
  AddPopupChildren();
  AddImageMapChildren();
  AddTextFieldChildren();
  AddCanvasChildren();
  AddRemoteSVGChildren();
  AddInlineTextBoxChildren(false);

  for (const auto& child : children_) {
    if (!child->CachedParentObject())
      child->SetParent(this);
  }

  for (const auto& owned_child : owned_children)
    AddChild(owned_child);
}

bool AXLayoutObject::CanHaveChildren() const {
  if (!layout_object_)
    return false;

  return AXNodeObject::CanHaveChildren();
}

void AXLayoutObject::UpdateChildrenIfNecessary() {
  if (NeedsToUpdateChildren())
    ClearChildren();

  AXObjectImpl::UpdateChildrenIfNecessary();
}

void AXLayoutObject::ClearChildren() {
  AXObjectImpl::ClearChildren();
  children_dirty_ = false;
}

//
// Properties of the object's owning document or page.
//

double AXLayoutObject::EstimatedLoadingProgress() const {
  if (!layout_object_)
    return 0;

  if (IsLoaded())
    return 1.0;

  if (LocalFrame* frame = layout_object_->GetDocument().GetFrame())
    return frame->Loader().Progress().EstimatedProgress();
  return 0;
}

//
// DOM and layout tree access.
//

Node* AXLayoutObject::GetNode() const {
  return GetLayoutObject() ? GetLayoutObject()->GetNode() : nullptr;
}

Document* AXLayoutObject::GetDocument() const {
  if (!GetLayoutObject())
    return nullptr;
  return &GetLayoutObject()->GetDocument();
}

FrameView* AXLayoutObject::DocumentFrameView() const {
  if (!GetLayoutObject())
    return nullptr;

  // this is the LayoutObject's Document's LocalFrame's FrameView
  return GetLayoutObject()->GetDocument().View();
}

Element* AXLayoutObject::AnchorElement() const {
  if (!layout_object_)
    return 0;

  AXObjectCacheImpl& cache = AxObjectCache();
  LayoutObject* curr_layout_object;

  // Search up the layout tree for a LayoutObject with a DOM node. Defer to an
  // earlier continuation, though.
  for (curr_layout_object = layout_object_;
       curr_layout_object && !curr_layout_object->GetNode();
       curr_layout_object = curr_layout_object->Parent()) {
    if (curr_layout_object->IsAnonymousBlock() &&
        curr_layout_object->IsLayoutBlockFlow()) {
      LayoutObject* continuation =
          ToLayoutBlockFlow(curr_layout_object)->Continuation();
      if (continuation)
        return cache.GetOrCreate(continuation)->AnchorElement();
    }
  }

  // bail if none found
  if (!curr_layout_object)
    return 0;

  // Search up the DOM tree for an anchor element.
  // NOTE: this assumes that any non-image with an anchor is an
  // HTMLAnchorElement
  Node* node = curr_layout_object->GetNode();
  if (!node)
    return nullptr;
  for (Node& runner : NodeTraversal::InclusiveAncestorsOf(*node)) {
    if (isHTMLAnchorElement(runner) ||
        (runner.GetLayoutObject() &&
         cache.GetOrCreate(runner.GetLayoutObject())->IsAnchor()))
      return ToElement(&runner);
  }

  return 0;
}

//
// Functions that retrieve the current selection.
//

AXObjectImpl::AXRange AXLayoutObject::Selection() const {
  AXRange text_selection = TextControlSelection();
  if (text_selection.IsValid())
    return text_selection;

  if (!GetLayoutObject() || !GetLayoutObject()->GetFrame())
    return AXRange();

  VisibleSelection selection =
      GetLayoutObject()
          ->GetFrame()
          ->Selection()
          .ComputeVisibleSelectionInDOMTreeDeprecated();
  if (selection.IsNone())
    return AXRange();

  VisiblePosition visible_start = selection.VisibleStart();
  Position start = visible_start.ToParentAnchoredPosition();
  TextAffinity start_affinity = visible_start.Affinity();
  VisiblePosition visible_end = selection.VisibleEnd();
  Position end = visible_end.ToParentAnchoredPosition();
  TextAffinity end_affinity = visible_end.Affinity();

  Node* anchor_node = start.AnchorNode();
  DCHECK(anchor_node);

  AXLayoutObject* anchor_object = nullptr;
  // Find the closest node that has a corresponding AXObjectImpl.
  // This is because some nodes may be aria hidden or might not even have
  // a layout object if they are part of the shadow DOM.
  while (anchor_node) {
    anchor_object = GetUnignoredObjectFromNode(*anchor_node);
    if (anchor_object)
      break;

    if (anchor_node->nextSibling())
      anchor_node = anchor_node->nextSibling();
    else
      anchor_node = anchor_node->parentNode();
  }

  Node* focus_node = end.AnchorNode();
  DCHECK(focus_node);

  AXLayoutObject* focus_object = nullptr;
  while (focus_node) {
    focus_object = GetUnignoredObjectFromNode(*focus_node);
    if (focus_object)
      break;

    if (focus_node->previousSibling())
      focus_node = focus_node->previousSibling();
    else
      focus_node = focus_node->parentNode();
  }

  if (!anchor_object || !focus_object)
    return AXRange();

  int anchor_offset = anchor_object->IndexForVisiblePosition(visible_start);
  DCHECK_GE(anchor_offset, 0);
  int focus_offset = focus_object->IndexForVisiblePosition(visible_end);
  DCHECK_GE(focus_offset, 0);
  return AXRange(anchor_object, anchor_offset, start_affinity, focus_object,
                 focus_offset, end_affinity);
}

// Gets only the start and end offsets of the selection computed using the
// current object as the starting point. Returns a null selection if there is
// no selection in the subtree rooted at this object.
AXObjectImpl::AXRange AXLayoutObject::SelectionUnderObject() const {
  AXRange text_selection = TextControlSelection();
  if (text_selection.IsValid())
    return text_selection;

  if (!GetNode() || !GetLayoutObject()->GetFrame())
    return AXRange();

  VisibleSelection selection =
      GetLayoutObject()
          ->GetFrame()
          ->Selection()
          .ComputeVisibleSelectionInDOMTreeDeprecated();
  Range* selection_range = CreateRange(FirstEphemeralRangeOf(selection));
  ContainerNode* parent_node = GetNode()->parentNode();
  int node_index = GetNode()->NodeIndex();
  if (!selection_range
      // Selection is contained in node.
      || !(parent_node &&
           selection_range->comparePoint(parent_node, node_index,
                                         IGNORE_EXCEPTION_FOR_TESTING) < 0 &&
           selection_range->comparePoint(parent_node, node_index + 1,
                                         IGNORE_EXCEPTION_FOR_TESTING) > 0)) {
    return AXRange();
  }

  int start = IndexForVisiblePosition(selection.VisibleStart());
  DCHECK_GE(start, 0);
  int end = IndexForVisiblePosition(selection.VisibleEnd());
  DCHECK_GE(end, 0);

  return AXRange(start, end);
}

AXObjectImpl::AXRange AXLayoutObject::TextControlSelection() const {
  if (!GetLayoutObject())
    return AXRange();

  LayoutObject* layout = nullptr;
  if (GetLayoutObject()->IsTextControl()) {
    layout = GetLayoutObject();
  } else {
    Element* focused_element = GetDocument()->FocusedElement();
    if (focused_element && focused_element->GetLayoutObject() &&
        focused_element->GetLayoutObject()->IsTextControl())
      layout = focused_element->GetLayoutObject();
  }

  if (!layout)
    return AXRange();

  AXObjectImpl* ax_object = AxObjectCache().GetOrCreate(layout);
  if (!ax_object || !ax_object->IsAXLayoutObject())
    return AXRange();

  VisibleSelection selection =
      layout->GetFrame()
          ->Selection()
          .ComputeVisibleSelectionInDOMTreeDeprecated();
  TextControlElement* text_control =
      ToLayoutTextControl(layout)->GetTextControlElement();
  DCHECK(text_control);
  int start = text_control->selectionStart();
  int end = text_control->selectionEnd();

  return AXRange(ax_object, start, selection.VisibleStart().Affinity(),
                 ax_object, end, selection.VisibleEnd().Affinity());
}

int AXLayoutObject::IndexForVisiblePosition(
    const VisiblePosition& position) const {
  if (GetLayoutObject() && GetLayoutObject()->IsTextControl()) {
    TextControlElement* text_control =
        ToLayoutTextControl(GetLayoutObject())->GetTextControlElement();
    return text_control->IndexForVisiblePosition(position);
  }

  if (!GetNode())
    return 0;

  Position index_position = position.DeepEquivalent();
  if (index_position.IsNull())
    return 0;

  Range* range = Range::Create(*GetDocument());
  range->setStart(GetNode(), 0, IGNORE_EXCEPTION_FOR_TESTING);
  range->setEnd(index_position, IGNORE_EXCEPTION_FOR_TESTING);

  return TextIterator::RangeLength(range->StartPosition(),
                                   range->EndPosition());
}

AXLayoutObject* AXLayoutObject::GetUnignoredObjectFromNode(Node& node) const {
  if (IsDetached())
    return nullptr;

  AXObjectImpl* ax_object = AxObjectCache().GetOrCreate(&node);
  if (!ax_object)
    return nullptr;

  if (ax_object->IsAXLayoutObject() && !ax_object->AccessibilityIsIgnored())
    return ToAXLayoutObject(ax_object);

  return nullptr;
}

//
// Modify or take an action on an object.
//

// Convert from an accessible object and offset to a VisiblePosition.
static VisiblePosition ToVisiblePosition(AXObjectImpl* obj, int offset) {
  if (!obj->GetNode())
    return VisiblePosition();

  Node* node = obj->GetNode();
  if (!node->IsTextNode()) {
    int child_count = obj->Children().size();

    // Place position immediately before the container node, if there was no
    // children.
    if (child_count == 0) {
      if (!obj->ParentObject())
        return VisiblePosition();
      return ToVisiblePosition(obj->ParentObject(), obj->IndexInParent());
    }

    // The offsets are child offsets over the AX tree. Note that we allow
    // for the offset to equal the number of children as |Range| does.
    if (offset < 0 || offset > child_count)
      return VisiblePosition();

    // Clamp to between 0 and child count - 1.
    int clamped_offset =
        static_cast<unsigned>(offset) > (obj->Children().size() - 1)
            ? offset - 1
            : offset;
    AXObjectImpl* child_obj = obj->Children()[clamped_offset];
    Node* child_node = child_obj->GetNode();
    if (!child_node || !child_node->parentNode())
      return VisiblePosition();

    // The index in parent.
    int adjusted_offset = child_node->NodeIndex();

    // If we had to clamp the offset above, the client wants to select the
    // end of the node.
    if (clamped_offset != offset)
      adjusted_offset++;

    return CreateVisiblePosition(
        Position::EditingPositionOf(child_node->parentNode(), adjusted_offset));
  }

  // If it is a text node, we need to call some utility functions that use a
  // TextIterator to walk the characters of the node and figure out the position
  // corresponding to the visible character at position |offset|.
  ContainerNode* parent = node->parentNode();
  if (!parent)
    return VisiblePosition();

  VisiblePosition node_position = blink::VisiblePositionBeforeNode(*node);
  int node_index = blink::IndexForVisiblePosition(node_position, parent);
  return blink::VisiblePositionForIndex(node_index + offset, parent);
}

void AXLayoutObject::SetSelection(const AXRange& selection) {
  if (!GetLayoutObject() || !selection.IsValid())
    return;

  AXObjectImpl* anchor_object =
      selection.anchor_object ? selection.anchor_object.Get() : this;
  AXObjectImpl* focus_object =
      selection.focus_object ? selection.focus_object.Get() : this;

  if (!IsValidSelectionBound(anchor_object) ||
      !IsValidSelectionBound(focus_object)) {
    return;
  }

  // The selection offsets are offsets into the accessible value.
  if (anchor_object == focus_object &&
      anchor_object->GetLayoutObject()->IsTextControl()) {
    TextControlElement* text_control =
        ToLayoutTextControl(anchor_object->GetLayoutObject())
            ->GetTextControlElement();
    if (selection.anchor_offset <= selection.focus_offset) {
      text_control->SetSelectionRange(selection.anchor_offset,
                                      selection.focus_offset,
                                      kSelectionHasForwardDirection);
    } else {
      text_control->SetSelectionRange(selection.focus_offset,
                                      selection.anchor_offset,
                                      kSelectionHasBackwardDirection);
    }
    return;
  }

  LocalFrame* frame = GetLayoutObject()->GetFrame();
  if (!frame)
    return;

  // TODO(editing-dev): Use of updateStyleAndLayoutIgnorePendingStylesheets
  // needs to be audited.  see http://crbug.com/590369 for more details.
  // This callsite should probably move up the stack.
  frame->GetDocument()->UpdateStyleAndLayoutIgnorePendingStylesheets();

  // Set the selection based on visible positions, because the offsets in
  // accessibility nodes are based on visible indexes, which often skips
  // redundant whitespace, for example.
  VisiblePosition anchor_visible_position =
      ToVisiblePosition(anchor_object, selection.anchor_offset);
  VisiblePosition focus_visible_position =
      ToVisiblePosition(focus_object, selection.focus_offset);
  if (anchor_visible_position.IsNull() || focus_visible_position.IsNull())
    return;

  frame->Selection().SetSelection(
      SelectionInDOMTree::Builder()
          .Collapse(anchor_visible_position.ToPositionWithAffinity())
          .Extend(focus_visible_position.DeepEquivalent())
          .Build());
}

bool AXLayoutObject::IsValidSelectionBound(
    const AXObjectImpl* bound_object) const {
  return GetLayoutObject() && bound_object && !bound_object->IsDetached() &&
         bound_object->IsAXLayoutObject() && bound_object->GetLayoutObject() &&
         bound_object->GetLayoutObject()->GetFrame() ==
             GetLayoutObject()->GetFrame() &&
         &bound_object->AxObjectCache() == &AxObjectCache();
}

void AXLayoutObject::SetValue(const String& string) {
  if (!GetNode() || !GetNode()->IsElementNode())
    return;
  if (!layout_object_ || !layout_object_->IsBoxModelObject())
    return;

  LayoutBoxModelObject* layout_object = ToLayoutBoxModelObject(layout_object_);
  if (layout_object->IsTextField() && isHTMLInputElement(*GetNode()))
    toHTMLInputElement(*GetNode())
        .setValue(string, kDispatchInputAndChangeEvent);
  else if (layout_object->IsTextArea() && isHTMLTextAreaElement(*GetNode()))
    toHTMLTextAreaElement(*GetNode())
        .setValue(string, kDispatchInputAndChangeEvent);
}

//
// Notifications that this object may have changed.
//

void AXLayoutObject::HandleActiveDescendantChanged() {
  if (!GetLayoutObject())
    return;

  AXObjectImpl* focused_object = AxObjectCache().FocusedObject();
  if (focused_object == this && SupportsActiveDescendant()) {
    AxObjectCache().PostNotification(
        GetLayoutObject(), AXObjectCacheImpl::kAXActiveDescendantChanged);
  }
}

void AXLayoutObject::HandleAriaExpandedChanged() {
  // Find if a parent of this object should handle aria-expanded changes.
  AXObjectImpl* container_parent = this->ParentObject();
  while (container_parent) {
    bool found_parent = false;

    switch (container_parent->RoleValue()) {
      case kTreeRole:
      case kTreeGridRole:
      case kGridRole:
      case kTableRole:
        found_parent = true;
        break;
      default:
        break;
    }

    if (found_parent)
      break;

    container_parent = container_parent->ParentObject();
  }

  // Post that the row count changed.
  if (container_parent)
    AxObjectCache().PostNotification(container_parent,
                                     AXObjectCacheImpl::kAXRowCountChanged);

  // Post that the specific row either collapsed or expanded.
  AccessibilityExpanded expanded = IsExpanded();
  if (!expanded)
    return;

  if (RoleValue() == kRowRole || RoleValue() == kTreeItemRole) {
    AXObjectCacheImpl::AXNotification notification =
        AXObjectCacheImpl::kAXRowExpanded;
    if (expanded == kExpandedCollapsed)
      notification = AXObjectCacheImpl::kAXRowCollapsed;

    AxObjectCache().PostNotification(this, notification);
  } else {
    AxObjectCache().PostNotification(this,
                                     AXObjectCacheImpl::kAXExpandedChanged);
  }
}

void AXLayoutObject::TextChanged() {
  if (!layout_object_)
    return;

  Settings* settings = GetDocument()->GetSettings();
  if (settings && settings->GetInlineTextBoxAccessibilityEnabled() &&
      RoleValue() == kStaticTextRole)
    ChildrenChanged();

  // Do this last - AXNodeObject::textChanged posts live region announcements,
  // and we should update the inline text boxes first.
  AXNodeObject::TextChanged();
}

//
// Text metrics. Most of these should be deprecated, needs major cleanup.
//

// NOTE: Consider providing this utility method as AX API
int AXLayoutObject::Index(const VisiblePosition& position) const {
  if (position.IsNull() || !IsTextControl())
    return -1;

  if (LayoutObjectContainsPosition(layout_object_, position.DeepEquivalent()))
    return IndexForVisiblePosition(position);

  return -1;
}

VisiblePosition AXLayoutObject::VisiblePositionForIndex(int index) const {
  if (!layout_object_)
    return VisiblePosition();

  if (layout_object_->IsTextControl())
    return ToLayoutTextControl(layout_object_)
        ->GetTextControlElement()
        ->VisiblePositionForIndex(index);

  Node* node = layout_object_->GetNode();
  if (!node)
    return VisiblePosition();

  if (index <= 0)
    return CreateVisiblePosition(FirstPositionInOrBeforeNode(node));

  Position start, end;
  bool selected = Range::selectNodeContents(node, start, end);
  if (!selected)
    return VisiblePosition();

  CharacterIterator it(start, end);
  it.Advance(index - 1);
  return CreateVisiblePosition(Position(it.CurrentContainer(), it.EndOffset()),
                               TextAffinity::kUpstream);
}

void AXLayoutObject::AddInlineTextBoxChildren(bool force) {
  Settings* settings = GetDocument()->GetSettings();
  if (!force &&
      (!settings || !settings->GetInlineTextBoxAccessibilityEnabled()))
    return;

  if (!GetLayoutObject() || !GetLayoutObject()->IsText())
    return;

  if (GetLayoutObject()->NeedsLayout()) {
    // If a LayoutText needs layout, its inline text boxes are either
    // nonexistent or invalid, so defer until the layout happens and
    // the layoutObject calls AXObjectCacheImpl::inlineTextBoxesUpdated.
    return;
  }

  LayoutText* layout_text = ToLayoutText(GetLayoutObject());
  for (RefPtr<AbstractInlineTextBox> box =
           layout_text->FirstAbstractInlineTextBox();
       box.Get(); box = box->NextInlineTextBox()) {
    AXObjectImpl* ax_object = AxObjectCache().GetOrCreate(box.Get());
    if (!ax_object->AccessibilityIsIgnored())
      children_.push_back(ax_object);
  }
}

void AXLayoutObject::LineBreaks(Vector<int>& line_breaks) const {
  if (!IsTextControl())
    return;

  VisiblePosition visible_pos = VisiblePositionForIndex(0);
  VisiblePosition prev_visible_pos = visible_pos;
  visible_pos = NextLinePosition(visible_pos, LayoutUnit(), kHasEditableAXRole);
  // nextLinePosition moves to the end of the current line when there are
  // no more lines.
  while (visible_pos.IsNotNull() &&
         !InSameLine(prev_visible_pos, visible_pos)) {
    line_breaks.push_back(IndexForVisiblePosition(visible_pos));
    prev_visible_pos = visible_pos;
    visible_pos =
        NextLinePosition(visible_pos, LayoutUnit(), kHasEditableAXRole);

    // Make sure we always make forward progress.
    if (visible_pos.DeepEquivalent().CompareTo(
            prev_visible_pos.DeepEquivalent()) < 0)
      break;
  }
}

//
// Private.
//

AXObjectImpl* AXLayoutObject::TreeAncestorDisallowingChild() const {
  // Determine if this is in a tree. If so, we apply special behavior to make it
  // work like an AXOutline.
  AXObjectImpl* ax_obj = ParentObject();
  AXObjectImpl* tree_ancestor = 0;
  while (ax_obj) {
    if (ax_obj->IsTree()) {
      tree_ancestor = ax_obj;
      break;
    }
    ax_obj = ax_obj->ParentObject();
  }

  // If the object is in a tree, only tree items should be exposed (and the
  // children of tree items).
  if (tree_ancestor) {
    AccessibilityRole role = RoleValue();
    if (role != kTreeItemRole && role != kStaticTextRole)
      return tree_ancestor;
  }
  return 0;
}

bool AXLayoutObject::IsTabItemSelected() const {
  if (!IsTabItem() || !GetLayoutObject())
    return false;

  Node* node = GetNode();
  if (!node || !node->IsElementNode())
    return false;

  // The ARIA spec says a tab item can also be selected if it is aria-labeled by
  // a tabpanel that has keyboard focus inside of it, or if a tabpanel in its
  // aria-controls list has KB focus inside of it.
  AXObjectImpl* focused_element = AxObjectCache().FocusedObject();
  if (!focused_element)
    return false;

  HeapVector<Member<Element>> elements;
  ElementsFromAttribute(elements, aria_controlsAttr);

  for (const auto& element : elements) {
    AXObjectImpl* tab_panel = AxObjectCache().GetOrCreate(element);

    // A tab item should only control tab panels.
    if (!tab_panel || tab_panel->RoleValue() != kTabPanelRole)
      continue;

    AXObjectImpl* check_focus_element = focused_element;
    // Check if the focused element is a descendant of the element controlled by
    // the tab item.
    while (check_focus_element) {
      if (tab_panel == check_focus_element)
        return true;
      check_focus_element = check_focus_element->ParentObject();
    }
  }

  return false;
}

AXObjectImpl* AXLayoutObject::AccessibilityImageMapHitTest(
    HTMLAreaElement* area,
    const IntPoint& point) const {
  if (!area)
    return 0;

  AXObjectImpl* parent = AxObjectCache().GetOrCreate(area->ImageElement());
  if (!parent)
    return 0;

  for (const auto& child : parent->Children()) {
    if (child->GetBoundsInFrameCoordinates().Contains(point))
      return child.Get();
  }

  return 0;
}

LayoutObject* AXLayoutObject::LayoutParentObject() const {
  if (!layout_object_)
    return 0;

  LayoutObject* start_of_conts = layout_object_->IsLayoutBlockFlow()
                                     ? StartOfContinuations(layout_object_)
                                     : nullptr;
  if (start_of_conts) {
    // Case 1: node is a block and is an inline's continuation. Parent
    // is the start of the continuation chain.
    return start_of_conts;
  }

  LayoutObject* parent = layout_object_->Parent();
  start_of_conts =
      parent && parent->IsLayoutInline() ? StartOfContinuations(parent) : 0;
  if (start_of_conts) {
    // Case 2: node's parent is an inline which is some node's continuation;
    // parent is the earliest node in the continuation chain.
    return start_of_conts;
  }

  LayoutObject* first_child = parent ? parent->SlowFirstChild() : 0;
  if (first_child && first_child->GetNode()) {
    // Case 3: The first sibling is the beginning of a continuation chain. Find
    // the origin of that continuation.  Get the node's layoutObject and follow
    // that continuation chain until the first child is found.
    for (LayoutObject* node_layout_first_child =
             first_child->GetNode()->GetLayoutObject();
         node_layout_first_child != first_child;
         node_layout_first_child = first_child->GetNode()->GetLayoutObject()) {
      for (LayoutObject* conts_test = node_layout_first_child; conts_test;
           conts_test = NextContinuation(conts_test)) {
        if (conts_test == first_child) {
          parent = node_layout_first_child->Parent();
          break;
        }
      }
      LayoutObject* new_first_child = parent->SlowFirstChild();
      if (first_child == new_first_child)
        break;
      first_child = new_first_child;
      if (!first_child->GetNode())
        break;
    }
  }

  return parent;
}

bool AXLayoutObject::IsSVGImage() const {
  return RemoteSVGRootElement();
}

void AXLayoutObject::DetachRemoteSVGRoot() {
  if (AXSVGRoot* root = RemoteSVGRootElement())
    root->SetParent(0);
}

AXSVGRoot* AXLayoutObject::RemoteSVGRootElement() const {
  // FIXME(dmazzoni): none of this code properly handled multiple references to
  // the same remote SVG document. I'm disabling this support until it can be
  // fixed properly.
  return 0;
}

AXObjectImpl* AXLayoutObject::RemoteSVGElementHitTest(
    const IntPoint& point) const {
  AXObjectImpl* remote = RemoteSVGRootElement();
  if (!remote)
    return 0;

  IntSize offset =
      point - RoundedIntPoint(GetBoundsInFrameCoordinates().Location());
  return remote->AccessibilityHitTest(IntPoint(offset));
}

// The boundingBox for elements within the remote SVG element needs to be offset
// by its position within the parent page, otherwise they are in relative
// coordinates only.
void AXLayoutObject::OffsetBoundingBoxForRemoteSVGElement(
    LayoutRect& rect) const {
  for (AXObjectImpl* parent = ParentObject(); parent;
       parent = parent->ParentObject()) {
    if (parent->IsAXSVGRoot()) {
      rect.MoveBy(
          parent->ParentObject()->GetBoundsInFrameCoordinates().Location());
      break;
    }
  }
}

// Hidden children are those that are not laid out or visible, but are
// specifically marked as aria-hidden=false,
// meaning that they should be exposed to the AX hierarchy.
void AXLayoutObject::AddHiddenChildren() {
  Node* node = this->GetNode();
  if (!node)
    return;

  // First do a quick run through to determine if we have any hidden nodes (most
  // often we will not).  If we do have hidden nodes, we need to determine where
  // to insert them so they match DOM order as close as possible.
  bool should_insert_hidden_nodes = false;
  for (Node& child : NodeTraversal::ChildrenOf(*node)) {
    if (!child.GetLayoutObject() && IsNodeAriaVisible(&child)) {
      should_insert_hidden_nodes = true;
      break;
    }
  }

  if (!should_insert_hidden_nodes)
    return;

  // Iterate through all of the children, including those that may have already
  // been added, and try to insert hidden nodes in the correct place in the DOM
  // order.
  unsigned insertion_index = 0;
  for (Node& child : NodeTraversal::ChildrenOf(*node)) {
    if (child.GetLayoutObject()) {
      // Find out where the last layout sibling is located within m_children.
      if (AXObjectImpl* child_object =
              AxObjectCache().Get(child.GetLayoutObject())) {
        if (child_object->AccessibilityIsIgnored()) {
          const auto& children = child_object->Children();
          child_object = children.size() ? children.back().Get() : 0;
        }
        if (child_object)
          insertion_index = children_.Find(child_object) + 1;
        continue;
      }
    }

    if (!IsNodeAriaVisible(&child))
      continue;

    unsigned previous_size = children_.size();
    if (insertion_index > previous_size)
      insertion_index = previous_size;

    InsertChild(AxObjectCache().GetOrCreate(&child), insertion_index);
    insertion_index += (children_.size() - previous_size);
  }
}

void AXLayoutObject::AddTextFieldChildren() {
  Node* node = this->GetNode();
  if (!isHTMLInputElement(node))
    return;

  HTMLInputElement& input = toHTMLInputElement(*node);
  Element* spin_button_element = input.UserAgentShadowRoot()->getElementById(
      ShadowElementNames::SpinButton());
  if (!spin_button_element || !spin_button_element->IsSpinButtonElement())
    return;

  AXSpinButton* ax_spin_button =
      ToAXSpinButton(AxObjectCache().GetOrCreate(kSpinButtonRole));
  ax_spin_button->SetSpinButtonElement(
      ToSpinButtonElement(spin_button_element));
  ax_spin_button->SetParent(this);
  children_.push_back(ax_spin_button);
}

void AXLayoutObject::AddImageMapChildren() {
  LayoutBoxModelObject* css_box = GetLayoutBoxModelObject();
  if (!css_box || !css_box->IsLayoutImage())
    return;

  HTMLMapElement* map = ToLayoutImage(css_box)->ImageMap();
  if (!map)
    return;

  for (HTMLAreaElement& area :
       Traversal<HTMLAreaElement>::DescendantsOf(*map)) {
    // add an <area> element for this child if it has a link
    AXObjectImpl* obj = AxObjectCache().GetOrCreate(&area);
    if (obj) {
      AXImageMapLink* area_object = ToAXImageMapLink(obj);
      area_object->SetParent(this);
      DCHECK(area_object->AxObjectID() != 0);
      if (!area_object->AccessibilityIsIgnored())
        children_.push_back(area_object);
      else
        AxObjectCache().Remove(area_object->AxObjectID());
    }
  }
}

void AXLayoutObject::AddCanvasChildren() {
  if (!isHTMLCanvasElement(GetNode()))
    return;

  // If it's a canvas, it won't have laid out children, but it might have
  // accessible fallback content.  Clear m_haveChildren because
  // AXNodeObject::addChildren will expect it to be false.
  DCHECK(!children_.size());
  have_children_ = false;
  AXNodeObject::AddChildren();
}

void AXLayoutObject::AddPopupChildren() {
  if (!isHTMLInputElement(GetNode()))
    return;
  if (AXObjectImpl* ax_popup =
          ToAXObjectImpl(toHTMLInputElement(GetNode())->PopupRootAXObject()))
    children_.push_back(ax_popup);
}

void AXLayoutObject::AddRemoteSVGChildren() {
  AXSVGRoot* root = RemoteSVGRootElement();
  if (!root)
    return;

  root->SetParent(this);

  if (root->AccessibilityIsIgnored()) {
    for (const auto& child : root->Children())
      children_.push_back(child);
  } else {
    children_.push_back(root);
  }
}

bool AXLayoutObject::ElementAttributeValue(
    const QualifiedName& attribute_name) const {
  if (!layout_object_)
    return false;

  return EqualIgnoringASCIICase(GetAttribute(attribute_name), "true");
}

}  // namespace blink
