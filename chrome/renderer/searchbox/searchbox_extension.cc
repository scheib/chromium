// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/renderer/searchbox/searchbox_extension.h"

#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>

#include "base/i18n/rtl.h"
#include "base/json/string_escape.h"
#include "base/macros.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/common/search/instant_types.h"
#include "chrome/common/search/ntp_logging_events.h"
#include "chrome/common/url_constants.h"
#include "chrome/grit/renderer_resources.h"
#include "chrome/renderer/searchbox/searchbox.h"
#include "components/crx_file/id_util.h"
#include "components/ntp_tiles/tile_source.h"
#include "components/ntp_tiles/tile_visual_type.h"
#include "content/public/renderer/render_frame.h"
#include "content/public/renderer/render_view.h"
#include "third_party/WebKit/public/platform/WebURLRequest.h"
#include "third_party/WebKit/public/web/WebDocument.h"
#include "third_party/WebKit/public/web/WebLocalFrame.h"
#include "third_party/WebKit/public/web/WebScriptSource.h"
#include "third_party/WebKit/public/web/WebView.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/base/window_open_disposition.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "url/gurl.h"
#include "url/url_constants.h"
#include "v8/include/v8.h"

namespace {

const char kCSSBackgroundImageFormat[] = "-webkit-image-set("
    "url(chrome-search://theme/IDR_THEME_NTP_BACKGROUND?%s) 1x, "
    "url(chrome-search://theme/IDR_THEME_NTP_BACKGROUND@2x?%s) 2x)";

const char kCSSBackgroundColorFormat[] = "rgba(%d,%d,%d,%s)";

const char kCSSBackgroundPositionCenter[] = "center";
const char kCSSBackgroundPositionLeft[] = "left";
const char kCSSBackgroundPositionTop[] = "top";
const char kCSSBackgroundPositionRight[] = "right";
const char kCSSBackgroundPositionBottom[] = "bottom";

const char kCSSBackgroundRepeatNo[] = "no-repeat";
const char kCSSBackgroundRepeatX[] = "repeat-x";
const char kCSSBackgroundRepeatY[] = "repeat-y";
const char kCSSBackgroundRepeat[] = "repeat";

const char kThemeAttributionFormat[] = "-webkit-image-set("
    "url(chrome-search://theme/IDR_THEME_NTP_ATTRIBUTION?%s) 1x, "
    "url(chrome-search://theme/IDR_THEME_NTP_ATTRIBUTION@2x?%s) 2x)";

const char kLTRHtmlTextDirection[] = "ltr";
const char kRTLHtmlTextDirection[] = "rtl";

// Converts a V8 value to a string16.
base::string16 V8ValueToUTF16(v8::Local<v8::Value> v) {
  v8::String::Value s(v);
  return base::string16(reinterpret_cast<const base::char16*>(*s), s.length());
}

// Converts string16 to V8 String.
v8::Local<v8::String> UTF16ToV8String(v8::Isolate* isolate,
                                       const base::string16& s) {
  return v8::String::NewFromTwoByte(isolate,
                                    reinterpret_cast<const uint16_t*>(s.data()),
                                    v8::String::kNormalString,
                                    s.size());
}

// Converts std::string to V8 String.
v8::Local<v8::String> UTF8ToV8String(v8::Isolate* isolate,
                                      const std::string& s) {
  return v8::String::NewFromUtf8(
      isolate, s.data(), v8::String::kNormalString, s.size());
}

// Throws a TypeError on the current V8 context if the args are invalid.
void ThrowInvalidParameters(const v8::FunctionCallbackInfo<v8::Value>& args) {
  v8::Isolate* isolate = args.GetIsolate();
  isolate->ThrowException(v8::Exception::TypeError(
      v8::String::NewFromUtf8(isolate, "Invalid parameters")));
}

void Dispatch(blink::WebFrame* frame, const blink::WebString& script) {
  if (!frame) return;
  frame->ExecuteScript(blink::WebScriptSource(script));
}

v8::Local<v8::String> GenerateThumbnailURL(
    v8::Isolate* isolate,
    int render_view_id,
    InstantRestrictedID most_visited_item_id) {
  return UTF8ToV8String(
      isolate,
      base::StringPrintf(
          "chrome-search://thumb/%d/%d", render_view_id, most_visited_item_id));
}

v8::Local<v8::String> GenerateThumb2URL(v8::Isolate* isolate,
                                        const GURL& page_url,
                                        const GURL& fallback_thumb_url) {
  return UTF8ToV8String(isolate,
                        base::StringPrintf("chrome-search://thumb2/%s?fb=%s",
                                           page_url.spec().c_str(),
                                           fallback_thumb_url.spec().c_str()));
}

// Populates a Javascript MostVisitedItem object from |mv_item|.
// NOTE: Includes "url", "title" and "domain" which are private data, so should
// not be returned to the Instant page. These should be erased before returning
// the object. See GetMostVisitedItemsWrapper() in searchbox_api.js.
v8::Local<v8::Object> GenerateMostVisitedItem(
    v8::Isolate* isolate,
    int render_view_id,
    InstantRestrictedID restricted_id,
    const InstantMostVisitedItem& mv_item) {
  // We set the "dir" attribute of the title, so that in RTL locales, a LTR
  // title is rendered left-to-right and truncated from the right. For
  // example, the title of http://msdn.microsoft.com/en-us/default.aspx is
  // "MSDN: Microsoft developer network". In RTL locales, in the New Tab
  // page, if the "dir" of this title is not specified, it takes Chrome UI's
  // directionality. So the title will be truncated as "soft developer
  // network". Setting the "dir" attribute as "ltr" renders the truncated
  // title as "MSDN: Microsoft D...". As another example, the title of
  // http://yahoo.com is "Yahoo!". In RTL locales, in the New Tab page, the
  // title will be rendered as "!Yahoo" if its "dir" attribute is not set to
  // "ltr".
  const char* direction;
  if (base::i18n::StringContainsStrongRTLChars(mv_item.title))
    direction = kRTLHtmlTextDirection;
  else
    direction = kLTRHtmlTextDirection;

  base::string16 title = mv_item.title;
  if (title.empty())
    title = base::UTF8ToUTF16(mv_item.url.spec());

  v8::Local<v8::Object> obj = v8::Object::New(isolate);
  obj->Set(v8::String::NewFromUtf8(isolate, "renderViewId"),
           v8::Int32::New(isolate, render_view_id));
  obj->Set(v8::String::NewFromUtf8(isolate, "rid"),
           v8::Int32::New(isolate, restricted_id));

  // If the suggestion already has a suggested thumbnail, we create a thumbnail
  // URL with both the local thumbnail and the proposed one as a fallback.
  // Otherwise, we just pass on the generated one.
  obj->Set(v8::String::NewFromUtf8(isolate, "thumbnailUrl"),
           mv_item.thumbnail.is_valid()
               ? GenerateThumb2URL(isolate, mv_item.url, mv_item.thumbnail)
               : GenerateThumbnailURL(isolate, render_view_id, restricted_id));

  // If the suggestion already has a favicon, we populate the element with it.
  if (!mv_item.favicon.spec().empty()) {
    obj->Set(v8::String::NewFromUtf8(isolate, "faviconUrl"),
             UTF8ToV8String(isolate, mv_item.favicon.spec()));
  }

  obj->Set(v8::String::NewFromUtf8(isolate, "tileSource"),
           v8::Integer::New(isolate, static_cast<int>(mv_item.source)));
  obj->Set(v8::String::NewFromUtf8(isolate, "title"),
           UTF16ToV8String(isolate, title));
  obj->Set(v8::String::NewFromUtf8(isolate, "domain"),
           UTF8ToV8String(isolate, mv_item.url.host()));
  obj->Set(v8::String::NewFromUtf8(isolate, "direction"),
           v8::String::NewFromUtf8(isolate, direction));
  obj->Set(v8::String::NewFromUtf8(isolate, "url"),
           UTF8ToV8String(isolate, mv_item.url.spec()));
  return obj;
}

// Returns the main frame of the render frame for the current JS context if it
// matches |origin|, otherwise returns NULL. Used to restrict methods that
// access suggestions and most visited data to pages with origin
// chrome-search://most-visited and chrome-search://suggestions.
content::RenderFrame* GetRenderFrameWithCheckedOrigin(const GURL& origin) {
  blink::WebLocalFrame* webframe =
      blink::WebLocalFrame::FrameForCurrentContext();
  if (!webframe)
    return NULL;
  auto* main_frame = content::RenderFrame::FromWebFrame(webframe->LocalRoot());
  if (!main_frame || !main_frame->IsMainFrame())
    return NULL;

  GURL url(webframe->GetDocument().Url());
  if (url.GetOrigin() != origin.GetOrigin())
    return NULL;

  return main_frame;
}

}  // namespace

namespace internal {  // for testing.

// Returns an array with the RGBA color components.
v8::Local<v8::Value> RGBAColorToArray(v8::Isolate* isolate,
                                       const RGBAColor& color) {
  v8::Local<v8::Array> color_array = v8::Array::New(isolate, 4);
  color_array->Set(0, v8::Int32::New(isolate, color.r));
  color_array->Set(1, v8::Int32::New(isolate, color.g));
  color_array->Set(2, v8::Int32::New(isolate, color.b));
  color_array->Set(3, v8::Int32::New(isolate, color.a));
  return color_array;
}

// Resolves a possibly relative URL using the current URL.
GURL ResolveURL(const GURL& current_url,
                const base::string16& possibly_relative_url) {
  if (current_url.is_valid() && !possibly_relative_url.empty())
    return current_url.Resolve(possibly_relative_url);
  return GURL(possibly_relative_url);
}

}  // namespace internal

namespace extensions_v8 {

static const char kSearchBoxExtensionName[] = "v8/EmbeddedSearch";

static const char kDispatchChromeIdentityCheckResult[] =
    "if (window.chrome &&"
    "    window.chrome.embeddedSearch &&"
    "    window.chrome.embeddedSearch.newTabPage &&"
    "    window.chrome.embeddedSearch.newTabPage.onsignedincheckdone &&"
    "    typeof window.chrome.embeddedSearch.newTabPage"
    "        .onsignedincheckdone === 'function') {"
    "  window.chrome.embeddedSearch.newTabPage.onsignedincheckdone(%s, %s);"
    "  true;"
    "}";

static const char kDispatchFocusChangedScript[] =
    "if (window.chrome &&"
    "    window.chrome.embeddedSearch &&"
    "    window.chrome.embeddedSearch.searchBox &&"
    "    window.chrome.embeddedSearch.searchBox.onfocuschange &&"
    "    typeof window.chrome.embeddedSearch.searchBox.onfocuschange =="
    "         'function') {"
    "  window.chrome.embeddedSearch.searchBox.onfocuschange();"
    "  true;"
    "}";

static const char kDispatchHistorySyncCheckResult[] =
    "if (window.chrome &&"
    "    window.chrome.embeddedSearch &&"
    "    window.chrome.embeddedSearch.newTabPage &&"
    "    window.chrome.embeddedSearch.newTabPage.onhistorysynccheckdone &&"
    "    typeof window.chrome.embeddedSearch.newTabPage"
    "        .onhistorysynccheckdone === 'function') {"
    "  window.chrome.embeddedSearch.newTabPage.onhistorysynccheckdone(%s);"
    "  true;"
    "}";

static const char kDispatchInputCancelScript[] =
    "if (window.chrome &&"
    "    window.chrome.embeddedSearch &&"
    "    window.chrome.embeddedSearch.newTabPage &&"
    "    window.chrome.embeddedSearch.newTabPage.oninputcancel &&"
    "    typeof window.chrome.embeddedSearch.newTabPage.oninputcancel =="
    "         'function') {"
    "  window.chrome.embeddedSearch.newTabPage.oninputcancel();"
    "  true;"
    "}";

static const char kDispatchInputStartScript[] =
    "if (window.chrome &&"
    "    window.chrome.embeddedSearch &&"
    "    window.chrome.embeddedSearch.newTabPage &&"
    "    window.chrome.embeddedSearch.newTabPage.oninputstart &&"
    "    typeof window.chrome.embeddedSearch.newTabPage.oninputstart =="
    "         'function') {"
    "  window.chrome.embeddedSearch.newTabPage.oninputstart();"
    "  true;"
    "}";

static const char kDispatchKeyCaptureChangeScript[] =
    "if (window.chrome &&"
    "    window.chrome.embeddedSearch &&"
    "    window.chrome.embeddedSearch.searchBox &&"
    "    window.chrome.embeddedSearch.searchBox.onkeycapturechange &&"
    "    typeof window.chrome.embeddedSearch.searchBox.onkeycapturechange =="
    "        'function') {"
    "  window.chrome.embeddedSearch.searchBox.onkeycapturechange();"
    "  true;"
    "}";

static const char kDispatchMostVisitedChangedScript[] =
    "if (window.chrome &&"
    "    window.chrome.embeddedSearch &&"
    "    window.chrome.embeddedSearch.newTabPage &&"
    "    window.chrome.embeddedSearch.newTabPage.onmostvisitedchange &&"
    "    typeof window.chrome.embeddedSearch.newTabPage.onmostvisitedchange =="
    "         'function') {"
    "  window.chrome.embeddedSearch.newTabPage.onmostvisitedchange();"
    "  true;"
    "}";

static const char kDispatchSubmitEventScript[] =
    "if (window.chrome &&"
    "    window.chrome.embeddedSearch &&"
    "    window.chrome.embeddedSearch.searchBox &&"
    "    window.chrome.embeddedSearch.searchBox.onsubmit &&"
    "    typeof window.chrome.embeddedSearch.searchBox.onsubmit =="
    "        'function') {"
    "  window.chrome.embeddedSearch.searchBox.onsubmit();"
    "  true;"
    "}";

static const char kDispatchSuggestionChangeEventScript[] =
    "if (window.chrome &&"
    "    window.chrome.embeddedSearch &&"
    "    window.chrome.embeddedSearch.searchBox &&"
    "    window.chrome.embeddedSearch.searchBox.onsuggestionchange &&"
    "    typeof window.chrome.embeddedSearch.searchBox.onsuggestionchange =="
    "        'function') {"
    "  window.chrome.embeddedSearch.searchBox.onsuggestionchange();"
    "  true;"
    "}";

static const char kDispatchThemeChangeEventScript[] =
    "if (window.chrome &&"
    "    window.chrome.embeddedSearch &&"
    "    window.chrome.embeddedSearch.newTabPage &&"
    "    window.chrome.embeddedSearch.newTabPage.onthemechange &&"
    "    typeof window.chrome.embeddedSearch.newTabPage.onthemechange =="
    "        'function') {"
    "  window.chrome.embeddedSearch.newTabPage.onthemechange();"
    "  true;"
    "}";

// ----------------------------------------------------------------------------

class SearchBoxExtensionWrapper : public v8::Extension {
 public:
  explicit SearchBoxExtensionWrapper(const base::StringPiece& code);

  // Allows v8's javascript code to call the native functions defined
  // in this class for window.chrome.
  v8::Local<v8::FunctionTemplate> GetNativeFunctionTemplate(
      v8::Isolate*,
      v8::Local<v8::String> name) override;

  // Helper function to find the main RenderFrame. May return NULL.
  static content::RenderFrame* GetRenderFrame();

  // Sends a Chrome identity check to the browser.
  static void CheckIsUserSignedInToChromeAs(
      const v8::FunctionCallbackInfo<v8::Value>& args);

  // Checks whether the user syncs their history.
  static void CheckIsUserSyncingHistory(
      const v8::FunctionCallbackInfo<v8::Value>& args);

  // Deletes a Most Visited item.
  static void DeleteMostVisitedItem(
      const v8::FunctionCallbackInfo<v8::Value>& args);

  // Gets Most Visited Items.
  static void GetMostVisitedItems(
      const v8::FunctionCallbackInfo<v8::Value>& args);

  // Gets the raw data for a most visited item including its raw URL.
  // GetRenderFrameWithCheckedOrigin() enforces that only code in the origin
  // chrome-search://most-visited can call this function.
  static void GetMostVisitedItemData(
    const v8::FunctionCallbackInfo<v8::Value>& args);

  // Gets the Embedded Search request params. Used for logging purposes.
  static void GetSearchRequestParams(
      const v8::FunctionCallbackInfo<v8::Value>& args);

  // Gets the current top suggestion to prefetch search results.
  static void GetSuggestionToPrefetch(
      const v8::FunctionCallbackInfo<v8::Value>& args);

  // Gets the background info of the theme currently adopted by browser.
  // Call only when overlay is showing NTP page.
  static void GetThemeBackgroundInfo(
      const v8::FunctionCallbackInfo<v8::Value>& args);

  // Gets whether the omnibox has focus or not.
  static void IsFocused(const v8::FunctionCallbackInfo<v8::Value>& args);

  // Gets whether user input is in progress.
  static void IsInputInProgress(
      const v8::FunctionCallbackInfo<v8::Value>& args);

  // Gets whether the browser is capturing key strokes.
  static void IsKeyCaptureEnabled(
      const v8::FunctionCallbackInfo<v8::Value>& args);

  // Logs information from the iframes/titles on the NTP.
  static void LogEvent(const v8::FunctionCallbackInfo<v8::Value>& args);

  // Logs an impression on one of the Most Visited tile on the NTP.
  static void LogMostVisitedImpression(
      const v8::FunctionCallbackInfo<v8::Value>& args);

  // Logs a navigation on one of the Most Visited tile on the NTP.
  static void LogMostVisitedNavigation(
      const v8::FunctionCallbackInfo<v8::Value>& args);

  // Pastes provided value or clipboard's content into the omnibox.
  static void Paste(const v8::FunctionCallbackInfo<v8::Value>& args);

  // Start capturing user key strokes.
  static void StartCapturingKeyStrokes(
      const v8::FunctionCallbackInfo<v8::Value>& args);

  // Stop capturing user key strokes.
  static void StopCapturingKeyStrokes(
      const v8::FunctionCallbackInfo<v8::Value>& args);

  // Undoes the deletion of all Most Visited itens.
  static void UndoAllMostVisitedDeletions(
      const v8::FunctionCallbackInfo<v8::Value>& args);

  // Undoes the deletion of a Most Visited item.
  static void UndoMostVisitedDeletion(
      const v8::FunctionCallbackInfo<v8::Value>& args);

 private:
  DISALLOW_COPY_AND_ASSIGN(SearchBoxExtensionWrapper);
};

// static
v8::Extension* SearchBoxExtension::Get() {
  return new SearchBoxExtensionWrapper(ResourceBundle::GetSharedInstance().
      GetRawDataResource(IDR_SEARCHBOX_API));
}

// static
void SearchBoxExtension::DispatchChromeIdentityCheckResult(
    blink::WebFrame* frame,
    const base::string16& identity,
    bool identity_match) {
  std::string escaped_identity = base::GetQuotedJSONString(identity);
  blink::WebString script(blink::WebString::FromUTF8(base::StringPrintf(
      kDispatchChromeIdentityCheckResult, escaped_identity.c_str(),
      identity_match ? "true" : "false")));
  Dispatch(frame, script);
}

// static
void SearchBoxExtension::DispatchFocusChange(blink::WebFrame* frame) {
  Dispatch(frame, kDispatchFocusChangedScript);
}

// static
void SearchBoxExtension::DispatchHistorySyncCheckResult(
    blink::WebFrame* frame,
    bool sync_history) {
  blink::WebString script(blink::WebString::FromUTF8(base::StringPrintf(
      kDispatchHistorySyncCheckResult, sync_history ? "true" : "false")));
  Dispatch(frame, script);
}

// static
void SearchBoxExtension::DispatchInputCancel(blink::WebFrame* frame) {
  Dispatch(frame, kDispatchInputCancelScript);
}

// static
void SearchBoxExtension::DispatchInputStart(blink::WebFrame* frame) {
  Dispatch(frame, kDispatchInputStartScript);
}

// static
void SearchBoxExtension::DispatchKeyCaptureChange(blink::WebFrame* frame) {
  Dispatch(frame, kDispatchKeyCaptureChangeScript);
}

// static
void SearchBoxExtension::DispatchMostVisitedChanged(
    blink::WebFrame* frame) {
  Dispatch(frame, kDispatchMostVisitedChangedScript);
}

// static
void SearchBoxExtension::DispatchSubmit(blink::WebFrame* frame) {
  Dispatch(frame, kDispatchSubmitEventScript);
}

// static
void SearchBoxExtension::DispatchSuggestionChange(blink::WebFrame* frame) {
  Dispatch(frame, kDispatchSuggestionChangeEventScript);
}

// static
void SearchBoxExtension::DispatchThemeChange(blink::WebFrame* frame) {
  Dispatch(frame, kDispatchThemeChangeEventScript);
}

SearchBoxExtensionWrapper::SearchBoxExtensionWrapper(
    const base::StringPiece& code)
    : v8::Extension(kSearchBoxExtensionName, code.data(), 0, 0, code.size()) {
}

v8::Local<v8::FunctionTemplate>
SearchBoxExtensionWrapper::GetNativeFunctionTemplate(
    v8::Isolate* isolate,
    v8::Local<v8::String> name) {
  // Extract the name for easier comparison.
  std::string name_str;
  if (name->Length() > 0)
    name->WriteUtf8(base::WriteInto(&name_str, name->Length() + 1));

  if (name_str == "CheckIsUserSignedInToChromeAs")
    return v8::FunctionTemplate::New(isolate, CheckIsUserSignedInToChromeAs);
  if (name_str == "CheckIsUserSyncingHistory")
    return v8::FunctionTemplate::New(isolate, CheckIsUserSyncingHistory);
  if (name_str == "DeleteMostVisitedItem")
    return v8::FunctionTemplate::New(isolate, DeleteMostVisitedItem);
  if (name_str == "GetMostVisitedItems")
    return v8::FunctionTemplate::New(isolate, GetMostVisitedItems);
  if (name_str == "GetMostVisitedItemData")
    return v8::FunctionTemplate::New(isolate, GetMostVisitedItemData);
  if (name_str == "GetSearchRequestParams")
    return v8::FunctionTemplate::New(isolate, GetSearchRequestParams);
  if (name_str == "GetSuggestionToPrefetch")
    return v8::FunctionTemplate::New(isolate, GetSuggestionToPrefetch);
  if (name_str == "GetThemeBackgroundInfo")
    return v8::FunctionTemplate::New(isolate, GetThemeBackgroundInfo);
  if (name_str == "IsFocused")
    return v8::FunctionTemplate::New(isolate, IsFocused);
  if (name_str == "IsInputInProgress")
    return v8::FunctionTemplate::New(isolate, IsInputInProgress);
  if (name_str == "IsKeyCaptureEnabled")
    return v8::FunctionTemplate::New(isolate, IsKeyCaptureEnabled);
  if (name_str == "LogEvent")
    return v8::FunctionTemplate::New(isolate, LogEvent);
  if (name_str == "LogMostVisitedImpression")
    return v8::FunctionTemplate::New(isolate, LogMostVisitedImpression);
  if (name_str == "LogMostVisitedNavigation")
    return v8::FunctionTemplate::New(isolate, LogMostVisitedNavigation);
  if (name_str == "Paste")
    return v8::FunctionTemplate::New(isolate, Paste);
  if (name_str == "StartCapturingKeyStrokes")
    return v8::FunctionTemplate::New(isolate, StartCapturingKeyStrokes);
  if (name_str == "StopCapturingKeyStrokes")
    return v8::FunctionTemplate::New(isolate, StopCapturingKeyStrokes);
  if (name_str == "UndoAllMostVisitedDeletions")
    return v8::FunctionTemplate::New(isolate, UndoAllMostVisitedDeletions);
  if (name_str == "UndoMostVisitedDeletion")
    return v8::FunctionTemplate::New(isolate, UndoMostVisitedDeletion);

  return v8::Local<v8::FunctionTemplate>();
}

// static
content::RenderFrame* SearchBoxExtensionWrapper::GetRenderFrame() {
  blink::WebLocalFrame* webframe =
      blink::WebLocalFrame::FrameForCurrentContext();
  if (!webframe) return NULL;

  auto* main_frame = content::RenderFrame::FromWebFrame(webframe->LocalRoot());
  if (!main_frame || !main_frame->IsMainFrame())
    return NULL;

  return main_frame;
}

// static
void SearchBoxExtensionWrapper::CheckIsUserSignedInToChromeAs(
    const v8::FunctionCallbackInfo<v8::Value>& args) {
  content::RenderFrame* render_frame = GetRenderFrame();
  if (!render_frame)
    return;

  if (!args.Length() || args[0]->IsUndefined()) {
    ThrowInvalidParameters(args);
    return;
  }

  DVLOG(1) << render_frame << " CheckIsUserSignedInToChromeAs";

  SearchBox::Get(render_frame)
      ->CheckIsUserSignedInToChromeAs(V8ValueToUTF16(args[0]));
}

// static
void SearchBoxExtensionWrapper::CheckIsUserSyncingHistory(
    const v8::FunctionCallbackInfo<v8::Value>& args) {
  content::RenderFrame* render_frame = GetRenderFrame();
  if (!render_frame)
    return;

  DVLOG(1) << render_frame << " CheckIsUserSyncingHistory";
  SearchBox::Get(render_frame)->CheckIsUserSyncingHistory();
}

// static
void SearchBoxExtensionWrapper::DeleteMostVisitedItem(
    const v8::FunctionCallbackInfo<v8::Value>& args) {
  content::RenderFrame* render_frame = GetRenderFrame();
  if (!render_frame)
    return;

  if (!args.Length()) {
    ThrowInvalidParameters(args);
    return;
  }

  DVLOG(1) << render_frame
           << " DeleteMostVisitedItem: " << args[0]->ToInteger()->Value();
  SearchBox::Get(render_frame)
      ->DeleteMostVisitedItem(args[0]->ToInteger()->Value());
}

// static
void SearchBoxExtensionWrapper::GetMostVisitedItems(
    const v8::FunctionCallbackInfo<v8::Value>& args) {
  content::RenderFrame* render_frame = GetRenderFrame();
  if (!render_frame)
    return;
  DVLOG(1) << render_frame << " GetMostVisitedItems";

  const SearchBox* search_box = SearchBox::Get(render_frame);
  int render_view_id = render_frame->GetRenderView()->GetRoutingID();

  std::vector<InstantMostVisitedItemIDPair> instant_mv_items;
  search_box->GetMostVisitedItems(&instant_mv_items);
  v8::Isolate* isolate = args.GetIsolate();
  v8::Local<v8::Array> v8_mv_items =
      v8::Array::New(isolate, instant_mv_items.size());
  for (size_t i = 0; i < instant_mv_items.size(); ++i) {
    v8_mv_items->Set(i, GenerateMostVisitedItem(isolate, render_view_id,
                                                instant_mv_items[i].first,
                                                instant_mv_items[i].second));
  }
  args.GetReturnValue().Set(v8_mv_items);
}

// static
void SearchBoxExtensionWrapper::GetMostVisitedItemData(
    const v8::FunctionCallbackInfo<v8::Value>& args) {
  content::RenderFrame* render_frame = GetRenderFrameWithCheckedOrigin(
      GURL(chrome::kChromeSearchMostVisitedUrl));
  if (!render_frame)
    return;

  // Need an rid argument.
  if (!args.Length() || !args[0]->IsNumber()) {
    ThrowInvalidParameters(args);
    return;
  }

  DVLOG(1) << render_frame << " GetMostVisitedItem";
  InstantRestrictedID restricted_id = args[0]->IntegerValue();
  InstantMostVisitedItem mv_item;
  if (!SearchBox::Get(render_frame)
           ->GetMostVisitedItemWithID(restricted_id, &mv_item)) {
    return;
  }
  v8::Isolate* isolate = args.GetIsolate();
  args.GetReturnValue().Set(GenerateMostVisitedItem(
      isolate, render_frame->GetRenderView()->GetRoutingID(), restricted_id,
      mv_item));
}

// static
void SearchBoxExtensionWrapper::GetSearchRequestParams(
    const v8::FunctionCallbackInfo<v8::Value>& args) {
  content::RenderFrame* render_frame = GetRenderFrame();
  if (!render_frame)
    return;

  const EmbeddedSearchRequestParams& params =
      SearchBox::Get(render_frame)->GetEmbeddedSearchRequestParams();
  v8::Isolate* isolate = args.GetIsolate();
  v8::Local<v8::Object> data = v8::Object::New(isolate);
  if (!params.search_query.empty()) {
    data->Set(v8::String::NewFromUtf8(isolate, kSearchQueryKey),
              UTF16ToV8String(isolate, params.search_query));
  }
  if (!params.original_query.empty()) {
    data->Set(v8::String::NewFromUtf8(isolate, kOriginalQueryKey),
              UTF16ToV8String(isolate, params.original_query));
  }
  if (!params.rlz_parameter_value.empty()) {
    data->Set(v8::String::NewFromUtf8(isolate, kRLZParameterKey),
              UTF16ToV8String(isolate, params.rlz_parameter_value));
  }
  if (!params.input_encoding.empty()) {
    data->Set(v8::String::NewFromUtf8(isolate, kInputEncodingKey),
              UTF16ToV8String(isolate, params.input_encoding));
  }
  if (!params.assisted_query_stats.empty()) {
    data->Set(v8::String::NewFromUtf8(isolate, kAssistedQueryStatsKey),
              UTF16ToV8String(isolate, params.assisted_query_stats));
  }
  args.GetReturnValue().Set(data);
}

// static
void SearchBoxExtensionWrapper::GetSuggestionToPrefetch(
    const v8::FunctionCallbackInfo<v8::Value>& args) {
  content::RenderFrame* render_frame = GetRenderFrame();
  if (!render_frame)
    return;

  const InstantSuggestion& suggestion =
      SearchBox::Get(render_frame)->suggestion();
  v8::Isolate* isolate = args.GetIsolate();
  v8::Local<v8::Object> data = v8::Object::New(isolate);
  data->Set(v8::String::NewFromUtf8(isolate, "text"),
            UTF16ToV8String(isolate, suggestion.text));
  data->Set(v8::String::NewFromUtf8(isolate, "metadata"),
            UTF8ToV8String(isolate, suggestion.metadata));
  args.GetReturnValue().Set(data);
}

// static
void SearchBoxExtensionWrapper::GetThemeBackgroundInfo(
    const v8::FunctionCallbackInfo<v8::Value>& args) {
  content::RenderFrame* render_frame = GetRenderFrame();
  if (!render_frame)
    return;

  DVLOG(1) << render_frame << " GetThemeBackgroundInfo";
  const ThemeBackgroundInfo& theme_info =
      SearchBox::Get(render_frame)->GetThemeBackgroundInfo();
  v8::Isolate* isolate = args.GetIsolate();
  v8::Local<v8::Object> info = v8::Object::New(isolate);

  info->Set(v8::String::NewFromUtf8(isolate, "usingDefaultTheme"),
            v8::Boolean::New(isolate, theme_info.using_default_theme));

  // The theme background color is in RGBA format "rgba(R,G,B,A)" where R, G and
  // B are between 0 and 255 inclusive, and A is a double between 0 and 1
  // inclusive.
  // This is the CSS "background-color" format.
  // Value is always valid.
  // TODO(jfweitz): Remove this field after GWS is modified to use the new
  // backgroundColorRgba field.
  info->Set(
      v8::String::NewFromUtf8(isolate, "colorRgba"),
      UTF8ToV8String(
          isolate,
          // Convert the alpha using DoubleToString because StringPrintf will
          // use
          // locale specific formatters (e.g., use , instead of . in German).
          base::StringPrintf(
              kCSSBackgroundColorFormat,
              theme_info.background_color.r,
              theme_info.background_color.g,
              theme_info.background_color.b,
              base::DoubleToString(theme_info.background_color.a / 255.0)
                  .c_str())));

  // Theme color for background as an array with the RGBA components in order.
  // Value is always valid.
  info->Set(v8::String::NewFromUtf8(isolate, "backgroundColorRgba"),
            internal::RGBAColorToArray(isolate, theme_info.background_color));

  // Theme color for text as an array with the RGBA components in order.
  // Value is always valid.
  info->Set(v8::String::NewFromUtf8(isolate, "textColorRgba"),
            internal::RGBAColorToArray(isolate, theme_info.text_color));

  // Theme color for links as an array with the RGBA components in order.
  // Value is always valid.
  info->Set(v8::String::NewFromUtf8(isolate, "linkColorRgba"),
            internal::RGBAColorToArray(isolate, theme_info.link_color));

  // Theme color for light text as an array with the RGBA components in order.
  // Value is always valid.
  info->Set(v8::String::NewFromUtf8(isolate, "textColorLightRgba"),
            internal::RGBAColorToArray(isolate, theme_info.text_color_light));

  // Theme color for header as an array with the RGBA components in order.
  // Value is always valid.
  info->Set(v8::String::NewFromUtf8(isolate, "headerColorRgba"),
            internal::RGBAColorToArray(isolate, theme_info.header_color));

  // Theme color for section border as an array with the RGBA components in
  // order. Value is always valid.
  info->Set(
      v8::String::NewFromUtf8(isolate, "sectionBorderColorRgba"),
      internal::RGBAColorToArray(isolate, theme_info.section_border_color));

  // The theme alternate logo value indicates a white logo when TRUE and a
  // colorful one when FALSE.
  info->Set(v8::String::NewFromUtf8(isolate, "alternateLogo"),
            v8::Boolean::New(isolate, theme_info.logo_alternate));

  // The theme background image url is of format kCSSBackgroundImageFormat
  // where both instances of "%s" are replaced with the id that identifies the
  // theme.
  // This is the CSS "background-image" format.
  // Value is only valid if there's a custom theme background image.
  if (crx_file::id_util::IdIsValid(theme_info.theme_id)) {
    info->Set(v8::String::NewFromUtf8(isolate, "imageUrl"),
              UTF8ToV8String(isolate,
                             base::StringPrintf(kCSSBackgroundImageFormat,
                                                theme_info.theme_id.c_str(),
                                                theme_info.theme_id.c_str())));

    // The theme background image horizontal alignment is one of "left",
    // "right", "center".
    // This is the horizontal component of the CSS "background-position" format.
    // Value is only valid if |imageUrl| is not empty.
    std::string alignment = kCSSBackgroundPositionCenter;
    if (theme_info.image_horizontal_alignment ==
            THEME_BKGRND_IMAGE_ALIGN_LEFT) {
      alignment = kCSSBackgroundPositionLeft;
    } else if (theme_info.image_horizontal_alignment ==
                   THEME_BKGRND_IMAGE_ALIGN_RIGHT) {
      alignment = kCSSBackgroundPositionRight;
    }
    info->Set(v8::String::NewFromUtf8(isolate, "imageHorizontalAlignment"),
              UTF8ToV8String(isolate, alignment));

    // The theme background image vertical alignment is one of "top", "bottom",
    // "center".
    // This is the vertical component of the CSS "background-position" format.
    // Value is only valid if |image_url| is not empty.
    if (theme_info.image_vertical_alignment == THEME_BKGRND_IMAGE_ALIGN_TOP) {
      alignment = kCSSBackgroundPositionTop;
    } else if (theme_info.image_vertical_alignment ==
                   THEME_BKGRND_IMAGE_ALIGN_BOTTOM) {
      alignment = kCSSBackgroundPositionBottom;
    } else {
      alignment = kCSSBackgroundPositionCenter;
    }
    info->Set(v8::String::NewFromUtf8(isolate, "imageVerticalAlignment"),
              UTF8ToV8String(isolate, alignment));

    // The tiling of the theme background image is one of "no-repeat",
    // "repeat-x", "repeat-y", "repeat".
    // This is the CSS "background-repeat" format.
    // Value is only valid if |image_url| is not empty.
    std::string tiling = kCSSBackgroundRepeatNo;
    switch (theme_info.image_tiling) {
      case THEME_BKGRND_IMAGE_NO_REPEAT:
        tiling = kCSSBackgroundRepeatNo;
        break;
      case THEME_BKGRND_IMAGE_REPEAT_X:
        tiling = kCSSBackgroundRepeatX;
        break;
      case THEME_BKGRND_IMAGE_REPEAT_Y:
        tiling = kCSSBackgroundRepeatY;
        break;
      case THEME_BKGRND_IMAGE_REPEAT:
        tiling = kCSSBackgroundRepeat;
        break;
    }
    info->Set(v8::String::NewFromUtf8(isolate, "imageTiling"),
              UTF8ToV8String(isolate, tiling));

    // The theme background image height is only valid if |imageUrl| is valid.
    info->Set(v8::String::NewFromUtf8(isolate, "imageHeight"),
              v8::Int32::New(isolate, theme_info.image_height));

    // The attribution URL is only valid if the theme has attribution logo.
    if (theme_info.has_attribution) {
      info->Set(
          v8::String::NewFromUtf8(isolate, "attributionUrl"),
          UTF8ToV8String(isolate,
                         base::StringPrintf(kThemeAttributionFormat,
                                            theme_info.theme_id.c_str(),
                                            theme_info.theme_id.c_str())));
    }
  }

  args.GetReturnValue().Set(info);
}

// static
void SearchBoxExtensionWrapper::IsFocused(
    const v8::FunctionCallbackInfo<v8::Value>& args) {
  content::RenderFrame* render_frame = GetRenderFrame();
  if (!render_frame)
    return;

  bool is_focused = SearchBox::Get(render_frame)->is_focused();
  DVLOG(1) << render_frame << " IsFocused: " << is_focused;
  args.GetReturnValue().Set(is_focused);
}

// static
void SearchBoxExtensionWrapper::IsInputInProgress(
    const v8::FunctionCallbackInfo<v8::Value>& args) {
  content::RenderFrame* render_frame = GetRenderFrame();
  if (!render_frame)
    return;

  bool is_input_in_progress =
      SearchBox::Get(render_frame)->is_input_in_progress();
  DVLOG(1) << render_frame << " IsInputInProgress: " << is_input_in_progress;
  args.GetReturnValue().Set(is_input_in_progress);
}

// static
void SearchBoxExtensionWrapper::IsKeyCaptureEnabled(
    const v8::FunctionCallbackInfo<v8::Value>& args) {
  content::RenderFrame* render_frame = GetRenderFrame();
  if (!render_frame)
    return;

  args.GetReturnValue().Set(
      SearchBox::Get(render_frame)->is_key_capture_enabled());
}

// static
void SearchBoxExtensionWrapper::LogEvent(
    const v8::FunctionCallbackInfo<v8::Value>& args) {
  content::RenderFrame* render_frame = GetRenderFrameWithCheckedOrigin(
      GURL(chrome::kChromeSearchMostVisitedUrl));
  if (!render_frame)
    return;

  if (!args.Length() || !args[0]->IsNumber()) {
    ThrowInvalidParameters(args);
    return;
  }

  DVLOG(1) << render_frame << " LogEvent";

  if (args[0]->Uint32Value() <= NTP_EVENT_TYPE_LAST) {
    NTPLoggingEventType event =
        static_cast<NTPLoggingEventType>(args[0]->Uint32Value());
    SearchBox::Get(render_frame)->LogEvent(event);
  }
}

// static
void SearchBoxExtensionWrapper::LogMostVisitedImpression(
    const v8::FunctionCallbackInfo<v8::Value>& args) {
  content::RenderFrame* render_frame = GetRenderFrameWithCheckedOrigin(
      GURL(chrome::kChromeSearchMostVisitedUrl));
  if (!render_frame)
    return;

  if (args.Length() < 3 || !args[0]->IsNumber() || !args[1]->IsNumber() ||
      !args[2]->IsNumber()) {
    ThrowInvalidParameters(args);
    return;
  }

  DVLOG(1) << render_frame << " LogMostVisitedImpression";

  if (args[1]->Uint32Value() <= static_cast<int>(ntp_tiles::TileSource::LAST) &&
      args[2]->Uint32Value() <= ntp_tiles::TileVisualType::TILE_TYPE_MAX) {
    auto tile_source =
        static_cast<ntp_tiles::TileSource>(args[1]->Uint32Value());
    auto tile_type =
        static_cast<ntp_tiles::TileVisualType>(args[2]->Uint32Value());
    SearchBox::Get(render_frame)
        ->LogMostVisitedImpression(args[0]->IntegerValue(), tile_source,
                                   tile_type);
  }
}

// static
void SearchBoxExtensionWrapper::LogMostVisitedNavigation(
    const v8::FunctionCallbackInfo<v8::Value>& args) {
  content::RenderFrame* render_frame = GetRenderFrameWithCheckedOrigin(
      GURL(chrome::kChromeSearchMostVisitedUrl));
  if (!render_frame)
    return;

  if (args.Length() < 2 || !args[0]->IsNumber() || !args[1]->IsNumber()) {
    ThrowInvalidParameters(args);
    return;
  }

  DVLOG(1) << render_frame << " LogMostVisitedNavigation";

  if (args[1]->Uint32Value() <= static_cast<int>(ntp_tiles::TileSource::LAST) &&
      args[2]->Uint32Value() <= ntp_tiles::TileVisualType::TILE_TYPE_MAX) {
    auto tile_source =
        static_cast<ntp_tiles::TileSource>(args[1]->Uint32Value());
    auto tile_type =
        static_cast<ntp_tiles::TileVisualType>(args[2]->Uint32Value());
    SearchBox::Get(render_frame)
        ->LogMostVisitedNavigation(args[0]->IntegerValue(), tile_source,
                                   tile_type);
  }
}

// static
void SearchBoxExtensionWrapper::Paste(
    const v8::FunctionCallbackInfo<v8::Value>& args) {
  content::RenderFrame* render_frame = GetRenderFrame();
  if (!render_frame)
    return;

  base::string16 text;
  if (!args[0]->IsUndefined())
    text = V8ValueToUTF16(args[0]);

  DVLOG(1) << render_frame << " Paste: " << text;
  SearchBox::Get(render_frame)->Paste(text);
}

// static
void SearchBoxExtensionWrapper::StartCapturingKeyStrokes(
    const v8::FunctionCallbackInfo<v8::Value>& args) {
  content::RenderFrame* render_frame = GetRenderFrame();
  if (!render_frame)
    return;

  DVLOG(1) << render_frame << " StartCapturingKeyStrokes";
  SearchBox::Get(render_frame)->StartCapturingKeyStrokes();
}

// static
void SearchBoxExtensionWrapper::StopCapturingKeyStrokes(
    const v8::FunctionCallbackInfo<v8::Value>& args) {
  content::RenderFrame* render_frame = GetRenderFrame();
  if (!render_frame)
    return;

  DVLOG(1) << render_frame << " StopCapturingKeyStrokes";
  SearchBox::Get(render_frame)->StopCapturingKeyStrokes();
}

// static
void SearchBoxExtensionWrapper::UndoAllMostVisitedDeletions(
    const v8::FunctionCallbackInfo<v8::Value>& args) {
  content::RenderFrame* render_frame = GetRenderFrame();
  if (!render_frame)
    return;

  DVLOG(1) << render_frame << " UndoAllMostVisitedDeletions";
  SearchBox::Get(render_frame)->UndoAllMostVisitedDeletions();
}

// static
void SearchBoxExtensionWrapper::UndoMostVisitedDeletion(
    const v8::FunctionCallbackInfo<v8::Value>& args) {
  content::RenderFrame* render_frame = GetRenderFrame();
  if (!render_frame) {
    return;
  }
  if (!args.Length()) {
    ThrowInvalidParameters(args);
    return;
  }

  DVLOG(1) << render_frame << " UndoMostVisitedDeletion";
  SearchBox::Get(render_frame)
      ->UndoMostVisitedDeletion(args[0]->ToInteger()->Value());
}

}  // namespace extensions_v8
