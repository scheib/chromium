/*
 * Copyright (C) 2009 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "public/web/WebAXObject.h"

#include "SkMatrix44.h"
#include "core/HTMLNames.h"
#include "core/css/CSSPrimitiveValueMappings.h"
#include "core/dom/Document.h"
#include "core/dom/Node.h"
#include "core/editing/markers/DocumentMarker.h"
#include "core/exported/WebViewBase.h"
#include "core/frame/FrameView.h"
#include "core/frame/VisualViewport.h"
#include "core/frame/WebLocalFrameBase.h"
#include "core/input/KeyboardEventManager.h"
#include "core/page/Page.h"
#include "core/style/ComputedStyle.h"
#include "modules/accessibility/AXObjectCacheImpl.h"
#include "modules/accessibility/AXObjectImpl.h"
#include "modules/accessibility/AXTable.h"
#include "modules/accessibility/AXTableCell.h"
#include "modules/accessibility/AXTableColumn.h"
#include "modules/accessibility/AXTableRow.h"
#include "platform/wtf/text/StringBuilder.h"
#include "public/platform/WebFloatRect.h"
#include "public/platform/WebPoint.h"
#include "public/platform/WebRect.h"
#include "public/platform/WebString.h"
#include "public/platform/WebURL.h"
#include "public/web/WebDocument.h"
#include "public/web/WebElement.h"
#include "public/web/WebNode.h"

namespace blink {

class WebAXSparseAttributeClientAdapter : public AXSparseAttributeClient {
 public:
  WebAXSparseAttributeClientAdapter(WebAXSparseAttributeClient& attribute_map)
      : attribute_map_(attribute_map) {}
  virtual ~WebAXSparseAttributeClientAdapter() {}

 private:
  WebAXSparseAttributeClient& attribute_map_;

  void AddBoolAttribute(AXBoolAttribute attribute, bool value) override {
    attribute_map_.AddBoolAttribute(static_cast<WebAXBoolAttribute>(attribute),
                                    value);
  }

  void AddStringAttribute(AXStringAttribute attribute,
                          const String& value) override {
    attribute_map_.AddStringAttribute(
        static_cast<WebAXStringAttribute>(attribute), value);
  }

  void AddObjectAttribute(AXObjectAttribute attribute,
                          AXObjectImpl& value) override {
    attribute_map_.AddObjectAttribute(
        static_cast<WebAXObjectAttribute>(attribute), WebAXObject(&value));
  }

  void AddObjectVectorAttribute(
      AXObjectVectorAttribute attribute,
      HeapVector<Member<AXObjectImpl>>& value) override {
    WebVector<WebAXObject> result(value.size());
    for (size_t i = 0; i < value.size(); i++)
      result[i] = WebAXObject(value[i]);
    attribute_map_.AddObjectVectorAttribute(
        static_cast<WebAXObjectVectorAttribute>(attribute), result);
  }
};

#if DCHECK_IS_ON()
// It's not safe to call some WebAXObject APIs if a layout is pending.
// Clients should call updateLayoutAndCheckValidity first.
static bool IsLayoutClean(Document* document) {
  if (!document || !document->View())
    return false;
  return document->Lifecycle().GetState() >= DocumentLifecycle::kLayoutClean ||
         ((document->Lifecycle().GetState() == DocumentLifecycle::kStyleClean ||
           document->Lifecycle().GetState() ==
               DocumentLifecycle::kLayoutSubtreeChangeClean) &&
          !document->View()->NeedsLayout());
}
#endif

WebScopedAXContext::WebScopedAXContext(WebDocument& root_document)
    : private_(ScopedAXObjectCache::Create(*root_document.Unwrap<Document>())) {
}

WebScopedAXContext::~WebScopedAXContext() {
  private_.reset(0);
}

WebAXObject WebScopedAXContext::Root() const {
  return WebAXObject(static_cast<AXObjectCacheImpl*>(private_->Get())->Root());
}

void WebAXObject::Reset() {
  private_.Reset();
}

void WebAXObject::Assign(const WebAXObject& other) {
  private_ = other.private_;
}

bool WebAXObject::Equals(const WebAXObject& n) const {
  return private_.Get() == n.private_.Get();
}

bool WebAXObject::IsDetached() const {
  if (private_.IsNull())
    return true;

  return private_->IsDetached();
}

int WebAXObject::AxID() const {
  if (IsDetached())
    return -1;

  return private_->AxObjectID();
}

int WebAXObject::GenerateAXID() const {
  if (IsDetached())
    return -1;

  return private_->AxObjectCache().GenerateAXID();
}

bool WebAXObject::UpdateLayoutAndCheckValidity() {
  if (!IsDetached()) {
    Document* document = private_->GetDocument();
    // TODO(szager): Investigate whether/why document->IsDetached() can be
    // true when this->IsDetached() is false.
    if (!document || !document->View() || document->IsDetached())
      return false;
    document->View()->UpdateLifecycleToCompositingCleanPlusScrolling();
  }

  // Doing a layout can cause this object to be invalid, so check again.
  return !IsDetached();
}

WebAXDefaultActionVerb WebAXObject::Action() const {
  if (IsDetached())
    return WebAXDefaultActionVerb::kNone;

  return static_cast<WebAXDefaultActionVerb>(private_->Action());
}

bool WebAXObject::CanDecrement() const {
  if (IsDetached())
    return false;

  return private_->IsSlider();
}

bool WebAXObject::CanIncrement() const {
  if (IsDetached())
    return false;

  return private_->IsSlider();
}

bool WebAXObject::CanPress() const {
  if (IsDetached())
    return false;

  return private_->ActionElement() || private_->IsButton() ||
         private_->IsMenuRelated();
}

bool WebAXObject::CanSetFocusAttribute() const {
  if (IsDetached())
    return false;

  return private_->CanSetFocusAttribute();
}

bool WebAXObject::CanSetValueAttribute() const {
  if (IsDetached())
    return false;

  return private_->CanSetValueAttribute();
}

unsigned WebAXObject::ChildCount() const {
  if (IsDetached())
    return 0;

  return private_->Children().size();
}

WebAXObject WebAXObject::ChildAt(unsigned index) const {
  if (IsDetached())
    return WebAXObject();

  if (private_->Children().size() <= index)
    return WebAXObject();

  return WebAXObject(private_->Children()[index]);
}

WebAXObject WebAXObject::ParentObject() const {
  if (IsDetached())
    return WebAXObject();

  return WebAXObject(private_->ParentObject());
}

void WebAXObject::GetSparseAXAttributes(
    WebAXSparseAttributeClient& client) const {
  if (IsDetached())
    return;

  WebAXSparseAttributeClientAdapter adapter(client);
  private_->GetSparseAXAttributes(adapter);
}

bool WebAXObject::CanSetSelectedAttribute() const {
  if (IsDetached())
    return false;

  return private_->CanSetSelectedAttribute();
}

bool WebAXObject::IsAnchor() const {
  if (IsDetached())
    return false;

  return private_->IsAnchor();
}

bool WebAXObject::IsAriaReadOnly() const {
  if (IsDetached())
    return false;

  return EqualIgnoringASCIICase(
      private_->GetAttribute(HTMLNames::aria_readonlyAttr), "true");
}

WebString WebAXObject::AriaAutoComplete() const {
  if (IsDetached())
    return WebString();

  return private_->AriaAutoComplete();
}

WebAXAriaCurrentState WebAXObject::AriaCurrentState() const {
  if (IsDetached())
    return kWebAXAriaCurrentStateUndefined;

  return static_cast<WebAXAriaCurrentState>(private_->GetAriaCurrentState());
}

bool WebAXObject::IsCheckable() const {
  if (IsDetached())
    return false;

  return private_->IsCheckable();
}

WebAXCheckedState WebAXObject::CheckedState() const {
  if (IsDetached())
    return WebAXCheckedFalse;

  return static_cast<WebAXCheckedState>(private_->CheckedState());
}

bool WebAXObject::IsClickable() const {
  if (IsDetached())
    return false;

  return private_->IsClickable();
}

bool WebAXObject::IsCollapsed() const {
  if (IsDetached())
    return false;

  return private_->IsCollapsed();
}

bool WebAXObject::IsControl() const {
  if (IsDetached())
    return false;

  return private_->IsControl();
}

bool WebAXObject::IsEnabled() const {
  if (IsDetached())
    return false;

  return private_->IsEnabled();
}

WebAXExpanded WebAXObject::IsExpanded() const {
  if (IsDetached())
    return kWebAXExpandedUndefined;

  return static_cast<WebAXExpanded>(private_->IsExpanded());
}

bool WebAXObject::IsFocused() const {
  if (IsDetached())
    return false;

  return private_->IsFocused();
}

bool WebAXObject::IsHovered() const {
  if (IsDetached())
    return false;

  return private_->IsHovered();
}

bool WebAXObject::IsLinked() const {
  if (IsDetached())
    return false;

  return private_->IsLinked();
}

bool WebAXObject::IsLoaded() const {
  if (IsDetached())
    return false;

  return private_->IsLoaded();
}

bool WebAXObject::IsModal() const {
  if (IsDetached())
    return false;

  return private_->IsModal();
}

bool WebAXObject::IsMultiSelectable() const {
  if (IsDetached())
    return false;

  return private_->IsMultiSelectable();
}

bool WebAXObject::IsOffScreen() const {
  if (IsDetached())
    return false;

  return private_->IsOffScreen();
}

bool WebAXObject::IsPasswordField() const {
  if (IsDetached())
    return false;

  return private_->IsPasswordField();
}

bool WebAXObject::IsPressed() const {
  if (IsDetached())
    return false;

  return private_->IsPressed();
}

bool WebAXObject::IsReadOnly() const {
  if (IsDetached())
    return false;

  return private_->IsReadOnly();
}

bool WebAXObject::IsRequired() const {
  if (IsDetached())
    return false;

  return private_->IsRequired();
}

bool WebAXObject::IsSelected() const {
  if (IsDetached())
    return false;

  return private_->IsSelected();
}

bool WebAXObject::IsSelectedOptionActive() const {
  if (IsDetached())
    return false;

  return private_->IsSelectedOptionActive();
}

bool WebAXObject::IsVisible() const {
  if (IsDetached())
    return false;

  return private_->IsVisible();
}

bool WebAXObject::IsVisited() const {
  if (IsDetached())
    return false;

  return private_->IsVisited();
}

WebString WebAXObject::AccessKey() const {
  if (IsDetached())
    return WebString();

  return WebString(private_->AccessKey());
}

unsigned WebAXObject::BackgroundColor() const {
  if (IsDetached())
    return 0;

  // RGBA32 is an alias for unsigned int.
  return private_->BackgroundColor();
}

unsigned WebAXObject::GetColor() const {
  if (IsDetached())
    return 0;

  // RGBA32 is an alias for unsigned int.
  return private_->GetColor();
}

// Deprecated.
void WebAXObject::ColorValue(int& r, int& g, int& b) const {
  if (IsDetached())
    return;

  unsigned color = private_->ColorValue();
  r = (color >> 16) & 0xFF;
  g = (color >> 8) & 0xFF;
  b = color & 0xFF;
}

unsigned WebAXObject::ColorValue() const {
  if (IsDetached())
    return 0;

  // RGBA32 is an alias for unsigned int.
  return private_->ColorValue();
}

WebAXObject WebAXObject::AriaActiveDescendant() const {
  if (IsDetached())
    return WebAXObject();

  return WebAXObject(private_->ActiveDescendant());
}

bool WebAXObject::AriaHasPopup() const {
  if (IsDetached())
    return false;

  return private_->AriaHasPopup();
}

bool WebAXObject::IsEditable() const {
  if (IsDetached())
    return false;

  return private_->IsEditable();
}

bool WebAXObject::IsMultiline() const {
  if (IsDetached())
    return false;

  return private_->IsMultiline();
}

bool WebAXObject::IsRichlyEditable() const {
  if (IsDetached())
    return false;

  return private_->IsRichlyEditable();
}

int WebAXObject::PosInSet() const {
  if (IsDetached())
    return 0;

  return private_->PosInSet();
}

int WebAXObject::SetSize() const {
  if (IsDetached())
    return 0;

  return private_->SetSize();
}

bool WebAXObject::IsInLiveRegion() const {
  if (IsDetached())
    return false;

  return 0 != private_->LiveRegionRoot();
}

bool WebAXObject::LiveRegionAtomic() const {
  if (IsDetached())
    return false;

  return private_->LiveRegionAtomic();
}

bool WebAXObject::LiveRegionBusy() const {
  if (IsDetached())
    return false;

  return private_->LiveRegionBusy();
}

WebString WebAXObject::LiveRegionRelevant() const {
  if (IsDetached())
    return WebString();

  return private_->LiveRegionRelevant();
}

WebString WebAXObject::LiveRegionStatus() const {
  if (IsDetached())
    return WebString();

  return private_->LiveRegionStatus();
}

WebAXObject WebAXObject::LiveRegionRoot() const {
  if (IsDetached())
    return WebAXObject();

  AXObjectImpl* live_region_root = private_->LiveRegionRoot();
  if (live_region_root)
    return WebAXObject(live_region_root);
  return WebAXObject();
}

bool WebAXObject::ContainerLiveRegionAtomic() const {
  if (IsDetached())
    return false;

  return private_->ContainerLiveRegionAtomic();
}

bool WebAXObject::ContainerLiveRegionBusy() const {
  if (IsDetached())
    return false;

  return private_->ContainerLiveRegionBusy();
}

WebString WebAXObject::ContainerLiveRegionRelevant() const {
  if (IsDetached())
    return WebString();

  return private_->ContainerLiveRegionRelevant();
}

WebString WebAXObject::ContainerLiveRegionStatus() const {
  if (IsDetached())
    return WebString();

  return private_->ContainerLiveRegionStatus();
}

bool WebAXObject::AriaOwns(WebVector<WebAXObject>& owns_elements) const {
  // aria-owns rearranges the accessibility tree rather than just
  // exposing an attribute.

  // FIXME(dmazzoni): remove this function after we stop calling it
  // from Chromium.  http://crbug.com/489590

  return false;
}

WebString WebAXObject::FontFamily() const {
  if (IsDetached())
    return WebString();

  return private_->FontFamily();
}

float WebAXObject::FontSize() const {
  if (IsDetached())
    return 0.0f;

  return private_->FontSize();
}

bool WebAXObject::CanvasHasFallbackContent() const {
  if (IsDetached())
    return false;

  return private_->CanvasHasFallbackContent();
}

WebString WebAXObject::ImageDataUrl(const WebSize& max_size) const {
  if (IsDetached())
    return WebString();

  return private_->ImageDataUrl(max_size);
}

WebAXInvalidState WebAXObject::InvalidState() const {
  if (IsDetached())
    return kWebAXInvalidStateUndefined;

  return static_cast<WebAXInvalidState>(private_->GetInvalidState());
}

// Only used when invalidState() returns WebAXInvalidStateOther.
WebString WebAXObject::AriaInvalidValue() const {
  if (IsDetached())
    return WebString();

  return private_->AriaInvalidValue();
}

double WebAXObject::EstimatedLoadingProgress() const {
  if (IsDetached())
    return 0.0;

  return private_->EstimatedLoadingProgress();
}

int WebAXObject::HeadingLevel() const {
  if (IsDetached())
    return 0;

  return private_->HeadingLevel();
}

int WebAXObject::HierarchicalLevel() const {
  if (IsDetached())
    return 0;

  return private_->HierarchicalLevel();
}

// FIXME: This method passes in a point that has page scale applied but assumes
// that (0, 0) is the top left of the visual viewport. In other words, the
// point has the VisualViewport scale applied, but not the VisualViewport
// offset. crbug.com/459591.
WebAXObject WebAXObject::HitTest(const WebPoint& point) const {
  if (IsDetached())
    return WebAXObject();

  IntPoint contents_point =
      private_->DocumentFrameView()->SoonToBeRemovedUnscaledViewportToContents(
          point);
  AXObjectImpl* hit = private_->AccessibilityHitTest(contents_point);

  if (hit)
    return WebAXObject(hit);

  if (private_->GetBoundsInFrameCoordinates().Contains(contents_point))
    return *this;

  return WebAXObject();
}

WebString WebAXObject::KeyboardShortcut() const {
  if (IsDetached())
    return WebString();

  String access_key = private_->AccessKey();
  if (access_key.IsNull())
    return WebString();

  DEFINE_STATIC_LOCAL(String, modifier_string, ());
  if (modifier_string.IsNull()) {
    unsigned modifiers = KeyboardEventManager::kAccessKeyModifiers;
    // Follow the same order as Mozilla MSAA implementation:
    // Ctrl+Alt+Shift+Meta+key. MSDN states that keyboard shortcut strings
    // should not be localized and defines the separator as "+".
    StringBuilder modifier_string_builder;
    if (modifiers & WebInputEvent::kControlKey)
      modifier_string_builder.Append("Ctrl+");
    if (modifiers & WebInputEvent::kAltKey)
      modifier_string_builder.Append("Alt+");
    if (modifiers & WebInputEvent::kShiftKey)
      modifier_string_builder.Append("Shift+");
    if (modifiers & WebInputEvent::kMetaKey)
      modifier_string_builder.Append("Win+");
    modifier_string = modifier_string_builder.ToString();
  }

  return String(modifier_string + access_key);
}

WebString WebAXObject::Language() const {
  if (IsDetached())
    return WebString();

  return private_->Language();
}

bool WebAXObject::PerformDefaultAction() const {
  if (IsDetached())
    return false;

  return private_->PerformDefaultAction();
}

bool WebAXObject::Increment() const {
  if (IsDetached())
    return false;

  if (CanIncrement()) {
    private_->Increment();
    return true;
  }
  return false;
}

bool WebAXObject::Decrement() const {
  if (IsDetached())
    return false;

  if (CanDecrement()) {
    private_->Decrement();
    return true;
  }
  return false;
}

WebAXObject WebAXObject::InPageLinkTarget() const {
  if (IsDetached())
    return WebAXObject();
  AXObjectImpl* target = private_->InPageLinkTarget();
  if (!target)
    return WebAXObject();
  return WebAXObject(target);
}

WebAXOrientation WebAXObject::Orientation() const {
  if (IsDetached())
    return kWebAXOrientationUndefined;

  return static_cast<WebAXOrientation>(private_->Orientation());
}

bool WebAXObject::Press() const {
  if (IsDetached())
    return false;

  return private_->Press();
}

WebVector<WebAXObject> WebAXObject::RadioButtonsInGroup() const {
  if (IsDetached())
    return WebVector<WebAXObject>();

  AXObjectImpl::AXObjectVector radio_buttons = private_->RadioButtonsInGroup();
  WebVector<WebAXObject> web_radio_buttons(radio_buttons.size());
  for (size_t i = 0; i < radio_buttons.size(); ++i)
    web_radio_buttons[i] = WebAXObject(radio_buttons[i]);
  return web_radio_buttons;
}

WebAXRole WebAXObject::Role() const {
  if (IsDetached())
    return kWebAXRoleUnknown;

  return static_cast<WebAXRole>(private_->RoleValue());
}

void WebAXObject::Selection(WebAXObject& anchor_object,
                            int& anchor_offset,
                            WebAXTextAffinity& anchor_affinity,
                            WebAXObject& focus_object,
                            int& focus_offset,
                            WebAXTextAffinity& focus_affinity) const {
  if (IsDetached()) {
    anchor_object = WebAXObject();
    anchor_offset = -1;
    anchor_affinity = kWebAXTextAffinityDownstream;
    focus_object = WebAXObject();
    focus_offset = -1;
    focus_affinity = kWebAXTextAffinityDownstream;
    return;
  }

  AXObjectImpl::AXRange ax_selection = private_->Selection();
  anchor_object = WebAXObject(ax_selection.anchor_object);
  anchor_offset = ax_selection.anchor_offset;
  anchor_affinity =
      static_cast<WebAXTextAffinity>(ax_selection.anchor_affinity);
  focus_object = WebAXObject(ax_selection.focus_object);
  focus_offset = ax_selection.focus_offset;
  focus_affinity = static_cast<WebAXTextAffinity>(ax_selection.focus_affinity);
  return;
}

void WebAXObject::SetSelection(const WebAXObject& anchor_object,
                               int anchor_offset,
                               const WebAXObject& focus_object,
                               int focus_offset) const {
  if (IsDetached())
    return;

  AXObjectImpl::AXRange ax_selection(anchor_object, anchor_offset,
                                     TextAffinity::kUpstream, focus_object,
                                     focus_offset, TextAffinity::kDownstream);
  private_->SetSelection(ax_selection);
  return;
}

unsigned WebAXObject::SelectionEnd() const {
  if (IsDetached())
    return 0;

  AXObjectImpl::AXRange ax_selection = private_->SelectionUnderObject();
  if (ax_selection.focus_offset < 0)
    return 0;

  return ax_selection.focus_offset;
}

unsigned WebAXObject::SelectionStart() const {
  if (IsDetached())
    return 0;

  AXObjectImpl::AXRange ax_selection = private_->SelectionUnderObject();
  if (ax_selection.anchor_offset < 0)
    return 0;

  return ax_selection.anchor_offset;
}

unsigned WebAXObject::SelectionEndLineNumber() const {
  if (IsDetached())
    return 0;

  VisiblePosition position = private_->VisiblePositionForIndex(SelectionEnd());
  int line_number = private_->LineForPosition(position);
  if (line_number < 0)
    return 0;

  return line_number;
}

unsigned WebAXObject::SelectionStartLineNumber() const {
  if (IsDetached())
    return 0;

  VisiblePosition position =
      private_->VisiblePositionForIndex(SelectionStart());
  int line_number = private_->LineForPosition(position);
  if (line_number < 0)
    return 0;

  return line_number;
}

void WebAXObject::SetFocused(bool on) const {
  if (!IsDetached())
    private_->SetFocused(on);
}

void WebAXObject::SetSelectedTextRange(int selection_start,
                                       int selection_end) const {
  if (IsDetached())
    return;

  private_->SetSelection(AXObjectImpl::AXRange(selection_start, selection_end));
}

void WebAXObject::SetSequentialFocusNavigationStartingPoint() const {
  if (IsDetached())
    return;

  private_->SetSequentialFocusNavigationStartingPoint();
}

void WebAXObject::SetValue(WebString value) const {
  if (IsDetached())
    return;

  private_->SetValue(value);
}

void WebAXObject::ShowContextMenu() const {
  if (IsDetached())
    return;

  Node* node = private_->GetNode();
  if (!node)
    return;

  Element* element = nullptr;
  if (node->IsElementNode()) {
    element = ToElement(node);
  } else if (node->IsDocumentNode()) {
    element = node->GetDocument().documentElement();
  } else {
    node->UpdateDistribution();
    ContainerNode* parent = FlatTreeTraversal::Parent(*node);
    if (!parent)
      return;
    SECURITY_DCHECK(parent->IsElementNode());
    element = ToElement(parent);
  }

  if (!element)
    return;

  LocalFrame* frame = element->GetDocument().GetFrame();
  if (!frame)
    return;

  WebViewBase* view = WebLocalFrameBase::FromFrame(frame)->ViewImpl();
  if (!view)
    return;

  view->ShowContextMenuForElement(WebElement(element));
}

WebString WebAXObject::StringValue() const {
  if (IsDetached())
    return WebString();

  return private_->StringValue();
}

WebAXTextDirection WebAXObject::GetTextDirection() const {
  if (IsDetached())
    return kWebAXTextDirectionLR;

  return static_cast<WebAXTextDirection>(private_->GetTextDirection());
}

WebAXTextStyle WebAXObject::TextStyle() const {
  if (IsDetached())
    return kWebAXTextStyleNone;

  return static_cast<WebAXTextStyle>(private_->GetTextStyle());
}

WebURL WebAXObject::Url() const {
  if (IsDetached())
    return WebURL();

  return private_->Url();
}

WebString WebAXObject::GetName(WebAXNameFrom& out_name_from,
                               WebVector<WebAXObject>& out_name_objects) const {
  if (IsDetached())
    return WebString();

  AXNameFrom name_from = kAXNameFromUninitialized;
  HeapVector<Member<AXObjectImpl>> name_objects;
  WebString result = private_->GetName(name_from, &name_objects);
  out_name_from = static_cast<WebAXNameFrom>(name_from);

  WebVector<WebAXObject> web_name_objects(name_objects.size());
  for (size_t i = 0; i < name_objects.size(); i++)
    web_name_objects[i] = WebAXObject(name_objects[i]);
  out_name_objects.Swap(web_name_objects);

  return result;
}

WebString WebAXObject::GetName() const {
  if (IsDetached())
    return WebString();

  AXNameFrom name_from;
  HeapVector<Member<AXObjectImpl>> name_objects;
  return private_->GetName(name_from, &name_objects);
}

WebString WebAXObject::Description(
    WebAXNameFrom name_from,
    WebAXDescriptionFrom& out_description_from,
    WebVector<WebAXObject>& out_description_objects) const {
  if (IsDetached())
    return WebString();

  AXDescriptionFrom description_from = kAXDescriptionFromUninitialized;
  HeapVector<Member<AXObjectImpl>> description_objects;
  String result = private_->Description(static_cast<AXNameFrom>(name_from),
                                        description_from, &description_objects);
  out_description_from = static_cast<WebAXDescriptionFrom>(description_from);

  WebVector<WebAXObject> web_description_objects(description_objects.size());
  for (size_t i = 0; i < description_objects.size(); i++)
    web_description_objects[i] = WebAXObject(description_objects[i]);
  out_description_objects.Swap(web_description_objects);

  return result;
}

WebString WebAXObject::Placeholder(WebAXNameFrom name_from) const {
  if (IsDetached())
    return WebString();

  return private_->Placeholder(static_cast<AXNameFrom>(name_from));
}

bool WebAXObject::SupportsRangeValue() const {
  if (IsDetached())
    return false;

  return private_->SupportsRangeValue();
}

WebString WebAXObject::ValueDescription() const {
  if (IsDetached())
    return WebString();

  return private_->ValueDescription();
}

float WebAXObject::ValueForRange() const {
  if (IsDetached())
    return 0.0;

  return private_->ValueForRange();
}

float WebAXObject::MaxValueForRange() const {
  if (IsDetached())
    return 0.0;

  return private_->MaxValueForRange();
}

float WebAXObject::MinValueForRange() const {
  if (IsDetached())
    return 0.0;

  return private_->MinValueForRange();
}

WebNode WebAXObject::GetNode() const {
  if (IsDetached())
    return WebNode();

  Node* node = private_->GetNode();
  if (!node)
    return WebNode();

  return WebNode(node);
}

WebDocument WebAXObject::GetDocument() const {
  if (IsDetached())
    return WebDocument();

  Document* document = private_->GetDocument();
  if (!document)
    return WebDocument();

  return WebDocument(document);
}

bool WebAXObject::HasComputedStyle() const {
  if (IsDetached())
    return false;

  Document* document = private_->GetDocument();
  if (document)
    document->UpdateStyleAndLayoutTree();

  Node* node = private_->GetNode();
  if (!node)
    return false;

  return node->EnsureComputedStyle();
}

WebString WebAXObject::ComputedStyleDisplay() const {
  if (IsDetached())
    return WebString();

  Document* document = private_->GetDocument();
  if (document)
    document->UpdateStyleAndLayoutTree();

  Node* node = private_->GetNode();
  if (!node)
    return WebString();

  const ComputedStyle* computed_style = node->EnsureComputedStyle();
  if (!computed_style)
    return WebString();

  return WebString(
      CSSIdentifierValue::Create(computed_style->Display())->CssText());
}

bool WebAXObject::AccessibilityIsIgnored() const {
  if (IsDetached())
    return false;

  return private_->AccessibilityIsIgnored();
}

bool WebAXObject::LineBreaks(WebVector<int>& result) const {
  if (IsDetached())
    return false;

  Vector<int> line_breaks_vector;
  private_->LineBreaks(line_breaks_vector);

  size_t vector_size = line_breaks_vector.size();
  WebVector<int> line_breaks_web_vector(vector_size);
  for (size_t i = 0; i < vector_size; i++)
    line_breaks_web_vector[i] = line_breaks_vector[i];
  result.Swap(line_breaks_web_vector);

  return true;
}

int WebAXObject::AriaColumnCount() const {
  if (IsDetached())
    return 0;

  if (!private_->IsAXTable())
    return 0;

  return ToAXTable(private_.Get())->AriaColumnCount();
}

unsigned WebAXObject::AriaColumnIndex() const {
  if (IsDetached())
    return 0;

  if (!private_->IsTableCell())
    return 0;

  return ToAXTableCell(private_.Get())->AriaColumnIndex();
}

int WebAXObject::AriaRowCount() const {
  if (IsDetached())
    return 0;

  if (!private_->IsAXTable())
    return 0;

  return ToAXTable(private_.Get())->AriaRowCount();
}

unsigned WebAXObject::AriaRowIndex() const {
  if (IsDetached())
    return 0;

  if (private_->IsTableCell())
    return ToAXTableCell(private_.Get())->AriaRowIndex();

  if (private_->IsTableRow())
    return ToAXTableRow(private_.Get())->AriaRowIndex();

  return 0;
}

unsigned WebAXObject::ColumnCount() const {
  if (IsDetached())
    return false;

  if (!private_->IsAXTable())
    return 0;

  return ToAXTable(private_.Get())->ColumnCount();
}

unsigned WebAXObject::RowCount() const {
  if (IsDetached())
    return 0;

  if (!private_->IsAXTable())
    return 0;

  return ToAXTable(private_.Get())->RowCount();
}

WebAXObject WebAXObject::CellForColumnAndRow(unsigned column,
                                             unsigned row) const {
  if (IsDetached())
    return WebAXObject();

  if (!private_->IsAXTable())
    return WebAXObject();

  AXTableCell* cell =
      ToAXTable(private_.Get())->CellForColumnAndRow(column, row);
  return WebAXObject(static_cast<AXObjectImpl*>(cell));
}

WebAXObject WebAXObject::HeaderContainerObject() const {
  if (IsDetached())
    return WebAXObject();

  if (!private_->IsAXTable())
    return WebAXObject();

  return WebAXObject(ToAXTable(private_.Get())->HeaderContainer());
}

WebAXObject WebAXObject::RowAtIndex(unsigned row_index) const {
  if (IsDetached())
    return WebAXObject();

  if (!private_->IsAXTable())
    return WebAXObject();

  const AXObjectImpl::AXObjectVector& rows = ToAXTable(private_.Get())->Rows();
  if (row_index < rows.size())
    return WebAXObject(rows[row_index]);

  return WebAXObject();
}

WebAXObject WebAXObject::ColumnAtIndex(unsigned column_index) const {
  if (IsDetached())
    return WebAXObject();

  if (!private_->IsAXTable())
    return WebAXObject();

  const AXObjectImpl::AXObjectVector& columns =
      ToAXTable(private_.Get())->Columns();
  if (column_index < columns.size())
    return WebAXObject(columns[column_index]);

  return WebAXObject();
}

unsigned WebAXObject::RowIndex() const {
  if (IsDetached())
    return 0;

  if (!private_->IsTableRow())
    return 0;

  return ToAXTableRow(private_.Get())->RowIndex();
}

WebAXObject WebAXObject::RowHeader() const {
  if (IsDetached())
    return WebAXObject();

  if (!private_->IsTableRow())
    return WebAXObject();

  return WebAXObject(ToAXTableRow(private_.Get())->HeaderObject());
}

void WebAXObject::RowHeaders(
    WebVector<WebAXObject>& row_header_elements) const {
  if (IsDetached())
    return;

  if (!private_->IsAXTable())
    return;

  AXObjectImpl::AXObjectVector headers;
  ToAXTable(private_.Get())->RowHeaders(headers);

  size_t header_count = headers.size();
  WebVector<WebAXObject> result(header_count);

  for (size_t i = 0; i < header_count; i++)
    result[i] = WebAXObject(headers[i]);

  row_header_elements.Swap(result);
}

unsigned WebAXObject::ColumnIndex() const {
  if (IsDetached())
    return 0;

  if (private_->RoleValue() != kColumnRole)
    return 0;

  return ToAXTableColumn(private_.Get())->ColumnIndex();
}

WebAXObject WebAXObject::ColumnHeader() const {
  if (IsDetached())
    return WebAXObject();

  if (private_->RoleValue() != kColumnRole)
    return WebAXObject();

  return WebAXObject(ToAXTableColumn(private_.Get())->HeaderObject());
}

void WebAXObject::ColumnHeaders(
    WebVector<WebAXObject>& column_header_elements) const {
  if (IsDetached())
    return;

  if (!private_->IsAXTable())
    return;

  AXObjectImpl::AXObjectVector headers;
  ToAXTable(private_.Get())->ColumnHeaders(headers);

  size_t header_count = headers.size();
  WebVector<WebAXObject> result(header_count);

  for (size_t i = 0; i < header_count; i++)
    result[i] = WebAXObject(headers[i]);

  column_header_elements.Swap(result);
}

unsigned WebAXObject::CellColumnIndex() const {
  if (IsDetached())
    return 0;

  if (!private_->IsTableCell())
    return 0;

  std::pair<unsigned, unsigned> column_range;
  ToAXTableCell(private_.Get())->ColumnIndexRange(column_range);
  return column_range.first;
}

unsigned WebAXObject::CellColumnSpan() const {
  if (IsDetached())
    return 0;

  if (!private_->IsTableCell())
    return 0;

  std::pair<unsigned, unsigned> column_range;
  ToAXTableCell(private_.Get())->ColumnIndexRange(column_range);
  return column_range.second;
}

unsigned WebAXObject::CellRowIndex() const {
  if (IsDetached())
    return 0;

  if (!private_->IsTableCell())
    return 0;

  std::pair<unsigned, unsigned> row_range;
  ToAXTableCell(private_.Get())->RowIndexRange(row_range);
  return row_range.first;
}

unsigned WebAXObject::CellRowSpan() const {
  if (IsDetached())
    return 0;

  if (!private_->IsTableCell())
    return 0;

  std::pair<unsigned, unsigned> row_range;
  ToAXTableCell(private_.Get())->RowIndexRange(row_range);
  return row_range.second;
}

WebAXSortDirection WebAXObject::SortDirection() const {
  if (IsDetached())
    return kWebAXSortDirectionUndefined;

  return static_cast<WebAXSortDirection>(private_->GetSortDirection());
}

void WebAXObject::LoadInlineTextBoxes() const {
  if (IsDetached())
    return;

  private_->LoadInlineTextBoxes();
}

WebAXObject WebAXObject::NextOnLine() const {
  if (IsDetached())
    return WebAXObject();

  return WebAXObject(private_.Get()->NextOnLine());
}

WebAXObject WebAXObject::PreviousOnLine() const {
  if (IsDetached())
    return WebAXObject();

  return WebAXObject(private_.Get()->PreviousOnLine());
}

void WebAXObject::Markers(WebVector<WebAXMarkerType>& types,
                          WebVector<int>& starts,
                          WebVector<int>& ends) const {
  if (IsDetached())
    return;

  Vector<DocumentMarker::MarkerType> marker_types;
  Vector<AXObjectImpl::AXRange> marker_ranges;
  private_->Markers(marker_types, marker_ranges);
  DCHECK_EQ(marker_types.size(), marker_ranges.size());

  WebVector<WebAXMarkerType> web_marker_types(marker_types.size());
  WebVector<int> start_offsets(marker_ranges.size());
  WebVector<int> end_offsets(marker_ranges.size());
  for (size_t i = 0; i < marker_types.size(); ++i) {
    web_marker_types[i] = static_cast<WebAXMarkerType>(marker_types[i]);
    DCHECK(marker_ranges[i].IsSimple());
    start_offsets[i] = marker_ranges[i].anchor_offset;
    end_offsets[i] = marker_ranges[i].focus_offset;
  }

  types.Swap(web_marker_types);
  starts.Swap(start_offsets);
  ends.Swap(end_offsets);
}

void WebAXObject::CharacterOffsets(WebVector<int>& offsets) const {
  if (IsDetached())
    return;

  Vector<int> offsets_vector;
  private_->TextCharacterOffsets(offsets_vector);

  size_t vector_size = offsets_vector.size();
  WebVector<int> offsets_web_vector(vector_size);
  for (size_t i = 0; i < vector_size; i++)
    offsets_web_vector[i] = offsets_vector[i];
  offsets.Swap(offsets_web_vector);
}

void WebAXObject::GetWordBoundaries(WebVector<int>& starts,
                                    WebVector<int>& ends) const {
  if (IsDetached())
    return;

  Vector<AXObjectImpl::AXRange> word_boundaries;
  private_->GetWordBoundaries(word_boundaries);

  WebVector<int> word_start_offsets(word_boundaries.size());
  WebVector<int> word_end_offsets(word_boundaries.size());
  for (size_t i = 0; i < word_boundaries.size(); ++i) {
    DCHECK(word_boundaries[i].IsSimple());
    word_start_offsets[i] = word_boundaries[i].anchor_offset;
    word_end_offsets[i] = word_boundaries[i].focus_offset;
  }

  starts.Swap(word_start_offsets);
  ends.Swap(word_end_offsets);
}

bool WebAXObject::IsScrollableContainer() const {
  if (IsDetached())
    return false;

  return private_->IsScrollableContainer();
}

WebPoint WebAXObject::GetScrollOffset() const {
  if (IsDetached())
    return WebPoint();

  return private_->GetScrollOffset();
}

WebPoint WebAXObject::MinimumScrollOffset() const {
  if (IsDetached())
    return WebPoint();

  return private_->MinimumScrollOffset();
}

WebPoint WebAXObject::MaximumScrollOffset() const {
  if (IsDetached())
    return WebPoint();

  return private_->MaximumScrollOffset();
}

void WebAXObject::SetScrollOffset(const WebPoint& offset) const {
  if (IsDetached())
    return;

  private_->SetScrollOffset(offset);
}

void WebAXObject::GetRelativeBounds(WebAXObject& offset_container,
                                    WebFloatRect& bounds_in_container,
                                    SkMatrix44& container_transform) const {
  if (IsDetached())
    return;

#if DCHECK_IS_ON()
  DCHECK(IsLayoutClean(private_->GetDocument()));
#endif

  AXObjectImpl* container = nullptr;
  FloatRect bounds;
  private_->GetRelativeBounds(&container, bounds, container_transform);
  offset_container = WebAXObject(container);
  bounds_in_container = WebFloatRect(bounds);
}

void WebAXObject::ScrollToMakeVisible() const {
  if (!IsDetached())
    private_->ScrollToMakeVisible();
}

void WebAXObject::ScrollToMakeVisibleWithSubFocus(
    const WebRect& subfocus) const {
  if (!IsDetached())
    private_->ScrollToMakeVisibleWithSubFocus(subfocus);
}

void WebAXObject::ScrollToGlobalPoint(const WebPoint& point) const {
  if (!IsDetached())
    private_->ScrollToGlobalPoint(point);
}

WebAXObject::WebAXObject(AXObjectImpl* object) : private_(object) {}

WebAXObject& WebAXObject::operator=(AXObjectImpl* object) {
  private_ = object;
  return *this;
}

WebAXObject::operator AXObjectImpl*() const {
  return private_.Get();
}

}  // namespace blink
