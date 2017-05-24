// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "android_webview/renderer/aw_content_renderer_client.h"

#include <vector>

#include "android_webview/common/aw_switches.h"
#include "android_webview/common/render_view_messages.h"
#include "android_webview/common/url_constants.h"
#include "android_webview/grit/aw_resources.h"
#include "android_webview/grit/aw_strings.h"
#include "android_webview/renderer/aw_content_settings_client.h"
#include "android_webview/renderer/aw_key_systems.h"
#include "android_webview/renderer/aw_print_web_view_helper_delegate.h"
#include "android_webview/renderer/aw_render_frame_ext.h"
#include "android_webview/renderer/aw_render_view_ext.h"
#include "android_webview/renderer/print_render_frame_observer.h"
#include "base/command_line.h"
#include "base/i18n/rtl.h"
#include "base/memory/ptr_util.h"
#include "base/message_loop/message_loop.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "components/autofill/content/renderer/autofill_agent.h"
#include "components/autofill/content/renderer/password_autofill_agent.h"
#include "components/printing/renderer/print_web_view_helper.h"
#include "components/spellcheck/spellcheck_build_features.h"
#include "components/supervised_user_error_page/gin_wrapper.h"
#include "components/supervised_user_error_page/supervised_user_error_page_android.h"
#include "components/visitedlink/renderer/visitedlink_slave.h"
#include "components/web_restrictions/interfaces/web_restrictions.mojom.h"
#include "content/public/child/child_thread.h"
#include "content/public/common/service_manager_connection.h"
#include "content/public/common/simple_connection_filter.h"
#include "content/public/common/url_constants.h"
#include "content/public/renderer/document_state.h"
#include "content/public/renderer/navigation_state.h"
#include "content/public/renderer/render_frame.h"
#include "content/public/renderer/render_thread.h"
#include "content/public/renderer/render_view.h"
#include "net/base/escape.h"
#include "net/base/net_errors.h"
#include "services/service_manager/public/cpp/binder_registry.h"
#include "services/service_manager/public/cpp/interface_provider.h"
#include "third_party/WebKit/public/platform/WebString.h"
#include "third_party/WebKit/public/platform/WebURL.h"
#include "third_party/WebKit/public/platform/WebURLError.h"
#include "third_party/WebKit/public/platform/WebURLRequest.h"
#include "third_party/WebKit/public/web/WebFrame.h"
#include "third_party/WebKit/public/web/WebNavigationType.h"
#include "third_party/WebKit/public/web/WebSecurityPolicy.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/resource/resource_bundle.h"
#include "url/gurl.h"
#include "url/url_constants.h"

#if BUILDFLAG(ENABLE_SPELLCHECK)
#include "components/spellcheck/renderer/spellcheck.h"
#include "components/spellcheck/renderer/spellcheck_provider.h"
#endif

using content::RenderThread;

namespace android_webview {

AwContentRendererClient::AwContentRendererClient() {}

AwContentRendererClient::~AwContentRendererClient() {}

void AwContentRendererClient::RenderThreadStarted() {
  RenderThread* thread = RenderThread::Get();
  aw_render_thread_observer_.reset(new AwRenderThreadObserver);
  thread->AddObserver(aw_render_thread_observer_.get());

  visited_link_slave_.reset(new visitedlink::VisitedLinkSlave);

  auto registry = base::MakeUnique<service_manager::BinderRegistry>();
  registry->AddInterface(visited_link_slave_->GetBindCallback(),
                         base::ThreadTaskRunnerHandle::Get());
  content::ChildThread::Get()
      ->GetServiceManagerConnection()
      ->AddConnectionFilter(base::MakeUnique<content::SimpleConnectionFilter>(
          std::move(registry)));

#if BUILDFLAG(ENABLE_SPELLCHECK)
  if (!spellcheck_) {
    spellcheck_ = base::MakeUnique<SpellCheck>();
    thread->AddObserver(spellcheck_.get());
  }
#endif
}

bool AwContentRendererClient::HandleNavigation(
    content::RenderFrame* render_frame,
    bool is_content_initiated,
    bool render_view_was_created_by_renderer,
    blink::WebFrame* frame,
    const blink::WebURLRequest& request,
    blink::WebNavigationType type,
    blink::WebNavigationPolicy default_policy,
    bool is_redirect) {
  // Only GETs can be overridden.
  if (!request.HttpMethod().Equals("GET"))
    return false;

  // Any navigation from loadUrl, and goBack/Forward are considered application-
  // initiated and hence will not yield a shouldOverrideUrlLoading() callback.
  // Webview classic does not consider reload application-initiated so we
  // continue the same behavior.
  // TODO(sgurun) is_content_initiated is normally false for cross-origin
  // navigations but since android_webview does not swap out renderers, this
  // works fine. This will stop working if android_webview starts swapping out
  // renderers on navigation.
  bool application_initiated =
      !is_content_initiated || type == blink::kWebNavigationTypeBackForward;

  // Don't offer application-initiated navigations unless it's a redirect.
  if (application_initiated && !is_redirect)
    return false;

  bool is_main_frame = !frame->Parent();
  const GURL& gurl = request.Url();
  // For HTTP schemes, only top-level navigations can be overridden. Similarly,
  // WebView Classic lets app override only top level about:blank navigations.
  // So we filter out non-top about:blank navigations here.
  if (!is_main_frame &&
      (gurl.SchemeIs(url::kHttpScheme) || gurl.SchemeIs(url::kHttpsScheme) ||
       gurl.SchemeIs(url::kAboutScheme)))
    return false;

  // use NavigationInterception throttle to handle the call as that can
  // be deferred until after the java side has been constructed.
  //
  // TODO(nick): |render_view_was_created_by_renderer| was plumbed in to
  // preserve the existing code behavior, but it doesn't appear to be correct.
  // In particular, this value will be true for the initial navigation of a
  // RenderView created via window.open(), but it will also be true for all
  // subsequent navigations in that RenderView, no matter how they are
  // initiated.
  if (render_view_was_created_by_renderer) {
    return false;
  }

  bool ignore_navigation = false;
  base::string16 url = request.Url().GetString().Utf16();
  bool has_user_gesture = request.HasUserGesture();

  int render_frame_id = render_frame->GetRoutingID();
  RenderThread::Get()->Send(new AwViewHostMsg_ShouldOverrideUrlLoading(
      render_frame_id, url, has_user_gesture, is_redirect, is_main_frame,
      &ignore_navigation));
  return ignore_navigation;
}

void AwContentRendererClient::RenderFrameCreated(
    content::RenderFrame* render_frame) {
  new AwContentSettingsClient(render_frame);
  new PrintRenderFrameObserver(render_frame);
  new printing::PrintWebViewHelper(
      render_frame, base::MakeUnique<AwPrintWebViewHelperDelegate>());
  new AwRenderFrameExt(render_frame);

  // TODO(jam): when the frame tree moves into content and parent() works at
  // RenderFrame construction, simplify this by just checking parent().
  content::RenderFrame* parent_frame =
      render_frame->GetRenderView()->GetMainRenderFrame();
  if (parent_frame && parent_frame != render_frame) {
    // Avoid any race conditions from having the browser's UI thread tell the IO
    // thread that a subframe was created.
    RenderThread::Get()->Send(new AwViewHostMsg_SubFrameCreated(
        parent_frame->GetRoutingID(), render_frame->GetRoutingID()));
  }

  // TODO(sgurun) do not create a password autofill agent (change
  // autofill agent to store a weakptr).
  autofill::PasswordAutofillAgent* password_autofill_agent =
      new autofill::PasswordAutofillAgent(render_frame);
  new autofill::AutofillAgent(render_frame, password_autofill_agent, NULL);

#if BUILDFLAG(ENABLE_SPELLCHECK)
  new SpellCheckProvider(render_frame, spellcheck_.get());
#endif
}

void AwContentRendererClient::RenderViewCreated(
    content::RenderView* render_view) {
  AwRenderViewExt::RenderViewCreated(render_view);

#if BUILDFLAG(ENABLE_SPELLCHECK)
  // This is a workaround keeping the behavior that, the Blink side spellcheck
  // enabled state is initialized on RenderView creation.
  // TODO(xiaochengh): Design better way to sync between Chrome-side and
  // Blink-side spellcheck enabled states.  See crbug.com/710097.
  if (SpellCheckProvider* provider =
          SpellCheckProvider::Get(render_view->GetMainRenderFrame()))
    provider->EnableSpellcheck(spellcheck_->IsSpellcheckEnabled());
#endif
}

bool AwContentRendererClient::HasErrorPage(int http_status_code,
                                           std::string* error_domain) {
  return http_status_code >= 400;
}

void AwContentRendererClient::GetNavigationErrorStrings(
    content::RenderFrame* render_frame,
    const blink::WebURLRequest& failed_request,
    const blink::WebURLError& error,
    std::string* error_html,
    base::string16* error_description) {
  if (error_description) {
    if (error.localized_description.IsEmpty())
      *error_description = base::ASCIIToUTF16(net::ErrorToString(error.reason));
    else
      *error_description = error.localized_description.Utf16();
  }

  if (!error_html)
    return;

  // Create the error page based on the error reason.
  GURL gurl(failed_request.Url());
  std::string url_string = gurl.possibly_invalid_spec();
  int reason_id = IDS_AW_WEBPAGE_CAN_NOT_BE_LOADED;

  if (error.reason == net::ERR_BLOCKED_BY_ADMINISTRATOR) {
    // This creates a different error page giving considerably more
    // detail, and possibly allowing the user to request access.
    // Get the details this needs from the browser.
    render_frame->GetRemoteInterfaces()->GetInterface(
        &web_restrictions_service_);
    web_restrictions::mojom::ClientResultPtr result;
    if (web_restrictions_service_->GetResult(url_string, &result)) {
      std::string detailed_error_html =
          supervised_user_error_page::BuildHtmlFromWebRestrictionsResult(
              result, RenderThread::Get()->GetLocale());
      if (!detailed_error_html.empty()) {
        *error_html = detailed_error_html;
        supervised_user_error_page::GinWrapper::InstallWhenFrameReady(
            render_frame, url_string, web_restrictions_service_);
        return;
      }
      // If the error page isn't available (it is only available in
      // Monochrome) but the user is a child then we want to give a simple
      // custom message.
      if (result->intParams["Is child account"])
        reason_id = IDS_AW_WEBPAGE_PARENTAL_PERMISSION_NEEDED;
    }
  }

  std::string err = error.localized_description.Utf8(
      blink::WebString::UTF8ConversionMode::kStrictReplacingErrorsWithFFFD);

  if (err.empty())
    reason_id = IDS_AW_WEBPAGE_TEMPORARILY_DOWN;

  std::string escaped_url = net::EscapeForHTML(url_string);
  std::vector<std::string> replacements;
  replacements.push_back(
      l10n_util::GetStringUTF8(IDS_AW_WEBPAGE_NOT_AVAILABLE));
  replacements.push_back(
      l10n_util::GetStringFUTF8(reason_id, base::UTF8ToUTF16(escaped_url)));

  // Having chosen the base reason, chose what extra information to add.
  if (reason_id == IDS_AW_WEBPAGE_PARENTAL_PERMISSION_NEEDED) {
    replacements.push_back("");
  } else if (reason_id == IDS_AW_WEBPAGE_TEMPORARILY_DOWN) {
    replacements.push_back(
        l10n_util::GetStringUTF8(IDS_AW_WEBPAGE_TEMPORARILY_DOWN_SUGGESTIONS));
  } else {
    replacements.push_back(err);
  }
  if (base::i18n::IsRTL())
    replacements.push_back("direction: rtl;");
  else
    replacements.push_back("");
  *error_html = base::ReplaceStringPlaceholders(
      ResourceBundle::GetSharedInstance().GetRawDataResource(
          IDR_AW_LOAD_ERROR_HTML),
      replacements, nullptr);
}

unsigned long long AwContentRendererClient::VisitedLinkHash(
    const char* canonical_url,
    size_t length) {
  return visited_link_slave_->ComputeURLFingerprint(canonical_url, length);
}

bool AwContentRendererClient::IsLinkVisited(unsigned long long link_hash) {
  return visited_link_slave_->IsVisited(link_hash);
}

void AwContentRendererClient::AddSupportedKeySystems(
    std::vector<std::unique_ptr<::media::KeySystemProperties>>* key_systems) {
  AwAddKeySystems(key_systems);
}

bool AwContentRendererClient::ShouldUseMediaPlayerForURL(const GURL& url) {
  // Android WebView needs to support codecs that Chrome does not, for these
  // cases we must force the usage of Android MediaPlayer instead of Chrome's
  // internal player.
  //
  // Note: Despite these extensions being forwarded for playback to MediaPlayer,
  // HTMLMediaElement.canPlayType() will return empty for these containers.
  // TODO(boliu): If this is important, extend media::MimeUtil for WebView.
  //
  // Format list mirrors:
  // http://developer.android.com/guide/appendix/media-formats.html
  static const char* kMediaPlayerExtensions[] = {
      ".3gp",  ".ts",    ".flac", ".mid", ".xmf",
      ".mxmf", ".rtttl", ".rtx",  ".ota", ".imy"};
  for (auto* extension : kMediaPlayerExtensions) {
    if (base::EndsWith(url.path(), extension,
                       base::CompareCase::INSENSITIVE_ASCII)) {
      return true;
    }
  }
  return false;
}

}  // namespace android_webview
