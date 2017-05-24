/*
 * Copyright (C) 2009, 2012 Google Inc. All rights reserved.
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

#include "web/ContextMenuClientImpl.h"

#include "bindings/core/v8/ExceptionState.h"
#include "core/CSSPropertyNames.h"
#include "core/HTMLNames.h"
#include "core/InputTypeNames.h"
#include "core/css/CSSStyleDeclaration.h"
#include "core/dom/Document.h"
#include "core/dom/ElementTraversal.h"
#include "core/editing/Editor.h"
#include "core/editing/markers/DocumentMarkerController.h"
#include "core/editing/spellcheck/SpellChecker.h"
#include "core/exported/WebDataSourceImpl.h"
#include "core/exported/WebPluginContainerBase.h"
#include "core/exported/WebViewBase.h"
#include "core/frame/FrameView.h"
#include "core/frame/Settings.h"
#include "core/frame/VisualViewport.h"
#include "core/frame/WebLocalFrameBase.h"
#include "core/html/HTMLAnchorElement.h"
#include "core/html/HTMLFormElement.h"
#include "core/html/HTMLFrameElementBase.h"
#include "core/html/HTMLImageElement.h"
#include "core/html/HTMLInputElement.h"
#include "core/html/HTMLMediaElement.h"
#include "core/html/HTMLPlugInElement.h"
#include "core/input/EventHandler.h"
#include "core/layout/HitTestResult.h"
#include "core/layout/LayoutPart.h"
#include "core/loader/DocumentLoader.h"
#include "core/loader/FrameLoader.h"
#include "core/loader/HistoryItem.h"
#include "core/page/ContextMenuController.h"
#include "core/page/Page.h"
#include "platform/ContextMenu.h"
#include "platform/exported/WrappedResourceResponse.h"
#include "platform/text/TextBreakIterator.h"
#include "platform/weborigin/KURL.h"
#include "platform/wtf/text/WTFString.h"
#include "public/platform/WebPoint.h"
#include "public/platform/WebString.h"
#include "public/platform/WebURL.h"
#include "public/platform/WebURLResponse.h"
#include "public/platform/WebVector.h"
#include "public/web/WebContextMenuData.h"
#include "public/web/WebFormElement.h"
#include "public/web/WebFrameClient.h"
#include "public/web/WebMenuItemInfo.h"
#include "public/web/WebPlugin.h"
#include "public/web/WebSearchableFormData.h"
#include "public/web/WebTextCheckClient.h"
#include "public/web/WebViewClient.h"
#include "web/ContextMenuAllowedScope.h"

namespace blink {

// Figure out the URL of a page or subframe. Returns |page_type| as the type,
// which indicates page or subframe, or ContextNodeType::kNone if the URL could
// not be determined for some reason.
static WebURL UrlFromFrame(LocalFrame* frame) {
  if (frame) {
    DocumentLoader* dl = frame->Loader().GetDocumentLoader();
    if (dl) {
      WebDataSource* ds = WebDataSourceImpl::FromDocumentLoader(dl);
      if (ds) {
        return ds->HasUnreachableURL() ? ds->UnreachableURL()
                                       : ds->GetRequest().Url();
      }
    }
  }
  return WebURL();
}

static bool IsWhiteSpaceOrPunctuation(UChar c) {
  return IsSpaceOrNewline(c) || WTF::Unicode::IsPunct(c);
}

static String SelectMisspellingAsync(LocalFrame* selected_frame,
                                     String& description) {
  VisibleSelection selection =
      selected_frame->Selection().ComputeVisibleSelectionInDOMTree();
  if (selection.IsNone())
    return String();

  // Caret and range selections always return valid normalized ranges.
  const EphemeralRange& selection_range =
      selection.ToNormalizedEphemeralRange();

  Node* const selection_start_container =
      selection_range.StartPosition().ComputeContainerNode();
  Node* const selection_end_container =
      selection_range.EndPosition().ComputeContainerNode();

  // We don't currently support the case where a misspelling spans multiple
  // nodes
  if (selection_start_container != selection_end_container)
    return String();

  const unsigned selection_start_offset =
      selection_range.StartPosition().ComputeOffsetInContainerNode();
  const unsigned selection_end_offset =
      selection_range.EndPosition().ComputeOffsetInContainerNode();

  const DocumentMarkerVector& markers_in_node =
      selected_frame->GetDocument()->Markers().MarkersFor(
          selection_start_container, DocumentMarker::MisspellingMarkers());

  const auto marker_it =
      std::find_if(markers_in_node.begin(), markers_in_node.end(),
                   [=](const DocumentMarker* marker) {
                     return marker->StartOffset() < selection_end_offset &&
                            marker->EndOffset() > selection_start_offset;
                   });
  if (marker_it == markers_in_node.end())
    return String();

  const DocumentMarker* const found_marker = *marker_it;
  description = found_marker->Description();

  Range* const marker_range =
      Range::Create(*selected_frame->GetDocument(), selection_start_container,
                    found_marker->StartOffset(), selection_start_container,
                    found_marker->EndOffset());

  if (marker_range->GetText().StripWhiteSpace(&IsWhiteSpaceOrPunctuation) !=
      CreateRange(selection_range)
          ->GetText()
          .StripWhiteSpace(&IsWhiteSpaceOrPunctuation))
    return String();

  return marker_range->GetText();
}

// static
int ContextMenuClientImpl::ComputeEditFlags(Document& selected_document,
                                            Editor& editor) {
  int edit_flags = WebContextMenuData::kCanDoNone;
  if (!selected_document.IsHTMLDocument() &&
      !selected_document.IsXHTMLDocument())
    return edit_flags;

  edit_flags |= WebContextMenuData::kCanTranslate;
  if (editor.CanUndo())
    edit_flags |= WebContextMenuData::kCanUndo;
  if (editor.CanRedo())
    edit_flags |= WebContextMenuData::kCanRedo;
  if (editor.CanCut())
    edit_flags |= WebContextMenuData::kCanCut;
  if (editor.CanCopy())
    edit_flags |= WebContextMenuData::kCanCopy;
  if (editor.CanPaste())
    edit_flags |= WebContextMenuData::kCanPaste;
  if (editor.CanDelete())
    edit_flags |= WebContextMenuData::kCanDelete;
  if (editor.CanEditRichly())
    edit_flags |= WebContextMenuData::kCanEditRichly;
  if (selected_document.queryCommandEnabled("selectAll", ASSERT_NO_EXCEPTION))
    edit_flags |= WebContextMenuData::kCanSelectAll;
  return edit_flags;
}

bool ContextMenuClientImpl::ShouldShowContextMenuFromTouch(
    const WebContextMenuData& data) {
  return web_view_->GetPage()
             ->GetSettings()
             .GetAlwaysShowContextMenuOnTouch() ||
         !data.link_url.IsEmpty() ||
         data.media_type == WebContextMenuData::kMediaTypeImage ||
         data.media_type == WebContextMenuData::kMediaTypeVideo ||
         data.is_editable;
}

static HTMLFormElement* AssociatedFormElement(HTMLElement& element) {
  if (isHTMLFormElement(element))
    return &toHTMLFormElement(element);
  return element.formOwner();
}

// Scans logically forward from "start", including any child frames.
static HTMLFormElement* ScanForForm(const Node* start) {
  if (!start)
    return nullptr;

  for (HTMLElement& element : Traversal<HTMLElement>::StartsAt(
           start->IsHTMLElement() ? ToHTMLElement(start)
                                  : Traversal<HTMLElement>::Next(*start))) {
    if (HTMLFormElement* form = AssociatedFormElement(element))
      return form;

    if (IsHTMLFrameElementBase(element)) {
      Node* child_document = ToHTMLFrameElementBase(element).contentDocument();
      if (HTMLFormElement* frame_result = ScanForForm(child_document))
        return frame_result;
    }
  }
  return nullptr;
}

// We look for either the form containing the current focus, or for one
// immediately after it
static HTMLFormElement* CurrentForm(const FrameSelection& current_selection) {
  // Start looking either at the active (first responder) node, or where the
  // selection is.
  const Node* start = current_selection.GetDocument().FocusedElement();
  if (!start) {
    start = current_selection.ComputeVisibleSelectionInDOMTree()
                .Start()
                .AnchorNode();
  }
  if (!start)
    return nullptr;

  // Try walking up the node tree to find a form element.
  for (Node& node : NodeTraversal::InclusiveAncestorsOf(*start)) {
    if (!node.IsHTMLElement())
      break;
    HTMLElement& element = ToHTMLElement(node);
    if (HTMLFormElement* form = AssociatedFormElement(element))
      return form;
  }

  // Try walking forward in the node tree to find a form element.
  return ScanForForm(start);
}

bool ContextMenuClientImpl::ShowContextMenu(const ContextMenu* default_menu,
                                            bool from_touch) {
  // Displaying the context menu in this function is a big hack as we don't
  // have context, i.e. whether this is being invoked via a script or in
  // response to user input (Mouse event WM_RBUTTONDOWN,
  // Keyboard events KeyVK_APPS, Shift+F10). Check if this is being invoked
  // in response to the above input events before popping up the context menu.
  if (!ContextMenuAllowedScope::IsContextMenuAllowed())
    return false;

  HitTestResult r =
      web_view_->GetPage()->GetContextMenuController().GetHitTestResult();

  r.SetToShadowHostIfInRestrictedShadowRoot();

  LocalFrame* selected_frame = r.InnerNodeFrame();
  WebLocalFrameBase* selected_web_frame =
      WebLocalFrameBase::FromFrame(selected_frame);

  WebContextMenuData data;
  data.mouse_position = selected_frame->View()->ContentsToViewport(
      r.RoundedPointInInnerNodeFrame());

  data.edit_flags = ComputeEditFlags(
      *selected_frame->GetDocument(),
      ToLocalFrame(web_view_->FocusedCoreFrame())->GetEditor());

  // Links, Images, Media tags, and Image/Media-Links take preference over
  // all else.
  data.link_url = r.AbsoluteLinkURL();

  if (r.InnerNode()->IsHTMLElement()) {
    HTMLElement* html_element = ToHTMLElement(r.InnerNode());
    if (!html_element->title().IsEmpty()) {
      data.title_text = html_element->title();
    } else {
      data.title_text = html_element->AltText();
    }
  }

  if (isHTMLCanvasElement(r.InnerNode())) {
    data.media_type = WebContextMenuData::kMediaTypeCanvas;
    data.has_image_contents = true;
  } else if (!r.AbsoluteImageURL().IsEmpty()) {
    data.src_url = r.AbsoluteImageURL();
    data.media_type = WebContextMenuData::kMediaTypeImage;
    data.media_flags |= WebContextMenuData::kMediaCanPrint;

    // An image can be null for many reasons, like being blocked, no image
    // data received from server yet.
    data.has_image_contents = r.GetImage() && !r.GetImage()->IsNull();
    if (data.has_image_contents &&
        isHTMLImageElement(r.InnerNodeOrImageMapImage())) {
      HTMLImageElement* image_element =
          toHTMLImageElement(r.InnerNodeOrImageMapImage());
      if (image_element && image_element->CachedImage())
        data.image_response = WrappedResourceResponse(
            image_element->CachedImage()->GetResponse());
    }
  } else if (!r.AbsoluteMediaURL().IsEmpty()) {
    data.src_url = r.AbsoluteMediaURL();

    // We know that if absoluteMediaURL() is not empty, then this
    // is a media element.
    HTMLMediaElement* media_element = ToHTMLMediaElement(r.InnerNode());
    if (isHTMLVideoElement(*media_element))
      data.media_type = WebContextMenuData::kMediaTypeVideo;
    else if (isHTMLAudioElement(*media_element))
      data.media_type = WebContextMenuData::kMediaTypeAudio;

    if (media_element->error())
      data.media_flags |= WebContextMenuData::kMediaInError;
    if (media_element->paused())
      data.media_flags |= WebContextMenuData::kMediaPaused;
    if (media_element->muted())
      data.media_flags |= WebContextMenuData::kMediaMuted;
    if (media_element->Loop())
      data.media_flags |= WebContextMenuData::kMediaLoop;
    if (media_element->SupportsSave())
      data.media_flags |= WebContextMenuData::kMediaCanSave;
    if (media_element->HasAudio())
      data.media_flags |= WebContextMenuData::kMediaHasAudio;
    // Media controls can be toggled only for video player. If we toggle
    // controls for audio then the player disappears, and there is no way to
    // return it back. Don't set this bit for fullscreen video, since
    // toggling is ignored in that case.
    if (media_element->IsHTMLVideoElement() && media_element->HasVideo() &&
        !media_element->IsFullscreen())
      data.media_flags |= WebContextMenuData::kMediaCanToggleControls;
    if (media_element->ShouldShowControls())
      data.media_flags |= WebContextMenuData::kMediaControls;
  } else if (isHTMLObjectElement(*r.InnerNode()) ||
             isHTMLEmbedElement(*r.InnerNode())) {
    LayoutObject* object = r.InnerNode()->GetLayoutObject();
    if (object && object->IsLayoutPart()) {
      PluginView* plugin_view = ToLayoutPart(object)->Plugin();
      if (plugin_view && plugin_view->IsPluginContainer()) {
        data.media_type = WebContextMenuData::kMediaTypePlugin;
        WebPluginContainerBase* plugin = ToWebPluginContainerBase(plugin_view);
        WebString text = plugin->Plugin()->SelectionAsText();
        if (!text.IsEmpty()) {
          data.selected_text = text;
          data.edit_flags |= WebContextMenuData::kCanCopy;
        }
        data.edit_flags &= ~WebContextMenuData::kCanTranslate;
        data.link_url = plugin->Plugin()->LinkAtPosition(data.mouse_position);
        if (plugin->Plugin()->SupportsPaginatedPrint())
          data.media_flags |= WebContextMenuData::kMediaCanPrint;

        HTMLPlugInElement* plugin_element = ToHTMLPlugInElement(r.InnerNode());
        data.src_url =
            plugin_element->GetDocument().CompleteURL(plugin_element->Url());
        data.media_flags |= WebContextMenuData::kMediaCanSave;

        // Add context menu commands that are supported by the plugin.
        if (plugin->Plugin()->CanRotateView())
          data.media_flags |= WebContextMenuData::kMediaCanRotate;
      }
    }
  }

  // If it's not a link, an image, a media element, or an image/media link,
  // show a selection menu or a more generic page menu.
  if (selected_frame->GetDocument()->Loader())
    data.frame_encoding = selected_frame->GetDocument()->EncodingName();

  // Send the frame and page URLs in any case.
  if (!web_view_->GetPage()->MainFrame()->IsLocalFrame()) {
    // TODO(kenrb): This works around the problem of URLs not being
    // available for top-level frames that are in a different process.
    // It mostly works to convert the security origin to a URL, but
    // extensions accessing that property will not get the correct value
    // in that case. See https://crbug.com/534561
    WebSecurityOrigin origin = web_view_->MainFrame()->GetSecurityOrigin();
    if (!origin.IsNull())
      data.page_url = KURL(kParsedURLString, origin.ToString());
  } else {
    data.page_url =
        UrlFromFrame(ToLocalFrame(web_view_->GetPage()->MainFrame()));
  }

  if (selected_frame != web_view_->GetPage()->MainFrame()) {
    data.frame_url = UrlFromFrame(selected_frame);
    HistoryItem* history_item =
        selected_frame->Loader().GetDocumentLoader()->GetHistoryItem();
    if (history_item)
      data.frame_history_item = WebHistoryItem(history_item);
  }

  // HitTestResult::isSelected() ensures clean layout by performing a hit test.
  if (r.IsSelected()) {
    if (!isHTMLInputElement(*r.InnerNode()) ||
        toHTMLInputElement(r.InnerNode())->type() != InputTypeNames::password) {
      data.selected_text = selected_frame->SelectedText();
    }
  }

  if (r.IsContentEditable()) {
    data.is_editable = true;

    // Spellchecker adds spelling markers to misspelled words and attaches
    // suggestions to these markers in the background. Therefore, when a
    // user right-clicks a mouse on a word, Chrome just needs to find a
    // spelling marker on the word instead of spellchecking it.
    String description;
    data.misspelled_word = SelectMisspellingAsync(selected_frame, description);
    if (description.length()) {
      Vector<String> suggestions;
      description.Split('\n', suggestions);
      data.dictionary_suggestions = suggestions;
    } else if (selected_web_frame->TextCheckClient()) {
      int misspelled_offset, misspelled_length;
      selected_web_frame->TextCheckClient()->CheckSpelling(
          data.misspelled_word, misspelled_offset, misspelled_length,
          &data.dictionary_suggestions);
    }

    HTMLFormElement* form = CurrentForm(selected_frame->Selection());
    if (form && isHTMLInputElement(*r.InnerNode())) {
      HTMLInputElement& selected_element = toHTMLInputElement(*r.InnerNode());
      WebSearchableFormData ws = WebSearchableFormData(
          WebFormElement(form), WebInputElement(&selected_element));
      if (ws.Url().IsValid())
        data.keyword_url = ws.Url();
    }
  }

  if (selected_frame->GetEditor().SelectionHasStyle(CSSPropertyDirection,
                                                    "ltr") != kFalseTriState)
    data.writing_direction_left_to_right |=
        WebContextMenuData::kCheckableMenuItemChecked;
  if (selected_frame->GetEditor().SelectionHasStyle(CSSPropertyDirection,
                                                    "rtl") != kFalseTriState)
    data.writing_direction_right_to_left |=
        WebContextMenuData::kCheckableMenuItemChecked;

  data.referrer_policy = static_cast<WebReferrerPolicy>(
      selected_frame->GetDocument()->GetReferrerPolicy());

  // Filter out custom menu elements and add them into the data.
  PopulateCustomMenuItems(default_menu, &data);

  if (isHTMLAnchorElement(r.URLElement())) {
    HTMLAnchorElement* anchor = toHTMLAnchorElement(r.URLElement());

    // Extract suggested filename for saving file.
    data.suggested_filename = anchor->FastGetAttribute(HTMLNames::downloadAttr);

    // If the anchor wants to suppress the referrer, update the referrerPolicy
    // accordingly.
    if (anchor->HasRel(kRelationNoReferrer))
      data.referrer_policy = kWebReferrerPolicyNever;

    data.link_text = anchor->innerText();
  }

  // Find the input field type.
  if (isHTMLInputElement(r.InnerNode())) {
    HTMLInputElement* element = toHTMLInputElement(r.InnerNode());
    if (element->type() == InputTypeNames::password)
      data.input_field_type = WebContextMenuData::kInputFieldTypePassword;
    else if (element->IsTextField())
      data.input_field_type = WebContextMenuData::kInputFieldTypePlainText;
    else
      data.input_field_type = WebContextMenuData::kInputFieldTypeOther;
  } else {
    data.input_field_type = WebContextMenuData::kInputFieldTypeNone;
  }

  WebRect focus_webrect;
  WebRect anchor_webrect;
  web_view_->SelectionBounds(focus_webrect, anchor_webrect);

  int left = std::min(focus_webrect.x, anchor_webrect.x);
  int top = std::min(focus_webrect.y, anchor_webrect.y);
  int right = std::max(focus_webrect.x + focus_webrect.width,
                       anchor_webrect.x + anchor_webrect.width);
  int bottom = std::max(focus_webrect.y + focus_webrect.height,
                        anchor_webrect.y + anchor_webrect.height);

  data.selection_rect = WebRect(left, top, right - left, bottom - top);

  if (from_touch && !ShouldShowContextMenuFromTouch(data))
    return false;

  selected_web_frame->SetContextMenuNode(r.InnerNodeOrImageMapImage());
  if (!selected_web_frame->Client())
    return false;

  selected_web_frame->Client()->ShowContextMenu(data);
  return true;
}

void ContextMenuClientImpl::ClearContextMenu() {
  HitTestResult r =
      web_view_->GetPage()->GetContextMenuController().GetHitTestResult();
  LocalFrame* selected_frame = r.InnerNodeFrame();
  if (!selected_frame)
    return;

  WebLocalFrameBase* selected_web_frame =
      WebLocalFrameBase::FromFrame(selected_frame);
  selected_web_frame->ClearContextMenuNode();
}

static void PopulateSubMenuItems(const Vector<ContextMenuItem>& input_menu,
                                 WebVector<WebMenuItemInfo>& sub_menu_items) {
  Vector<WebMenuItemInfo> sub_items;
  for (size_t i = 0; i < input_menu.size(); ++i) {
    const ContextMenuItem* input_item = &input_menu.at(i);
    if (input_item->Action() < kContextMenuItemBaseCustomTag ||
        input_item->Action() > kContextMenuItemLastCustomTag)
      continue;

    WebMenuItemInfo output_item;
    output_item.label = input_item->Title();
    output_item.icon = input_item->Icon();
    output_item.enabled = input_item->Enabled();
    output_item.checked = input_item->Checked();
    output_item.action = static_cast<unsigned>(input_item->Action() -
                                               kContextMenuItemBaseCustomTag);
    switch (input_item->GetType()) {
      case kActionType:
        output_item.type = WebMenuItemInfo::kOption;
        break;
      case kCheckableActionType:
        output_item.type = WebMenuItemInfo::kCheckableOption;
        break;
      case kSeparatorType:
        output_item.type = WebMenuItemInfo::kSeparator;
        break;
      case kSubmenuType:
        output_item.type = WebMenuItemInfo::kSubMenu;
        PopulateSubMenuItems(input_item->SubMenuItems(),
                             output_item.sub_menu_items);
        break;
    }
    sub_items.push_back(output_item);
  }

  WebVector<WebMenuItemInfo> output_items(sub_items.size());
  for (size_t i = 0; i < sub_items.size(); ++i)
    output_items[i] = sub_items[i];
  sub_menu_items.Swap(output_items);
}

void ContextMenuClientImpl::PopulateCustomMenuItems(
    const ContextMenu* default_menu,
    WebContextMenuData* data) {
  PopulateSubMenuItems(default_menu->Items(), data->custom_items);
}

}  // namespace blink
