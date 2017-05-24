// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/search/instant_search_prerenderer.h"

#include <utility>

#include "chrome/browser/prerender/prerender_handle.h"
#include "chrome/browser/prerender/prerender_manager.h"
#include "chrome/browser/prerender/prerender_manager_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/search/instant_service.h"
#include "chrome/browser/search/instant_service_factory.h"
#include "chrome/browser/search/search.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "chrome/browser/ui/browser_navigator_params.h"
#include "chrome/browser/ui/search/search_tab_helper.h"
#include "components/omnibox/browser/autocomplete_match.h"
#include "components/search/search.h"
#include "components/search_engines/template_url_service.h"

namespace {

// Returns true if |match| is associated with the default search provider.
bool MatchIsFromDefaultSearchProvider(const AutocompleteMatch& match,
                                      Profile* profile) {
  DCHECK(profile);
  TemplateURLService* template_url_service =
      TemplateURLServiceFactory::GetForProfile(profile);
  return match.GetTemplateURL(template_url_service, false) ==
      template_url_service->GetDefaultSearchProvider();
}

}  // namespace

InstantSearchPrerenderer::InstantSearchPrerenderer(Profile* profile,
                                                   const GURL& prerender_url)
    : profile_(profile), prerender_url_(prerender_url) {}

InstantSearchPrerenderer::~InstantSearchPrerenderer() {
  if (prerender_handle_)
    prerender_handle_->OnCancel();
}

// static
InstantSearchPrerenderer* InstantSearchPrerenderer::GetForProfile(
    Profile* profile) {
  DCHECK(profile);
  InstantService* instant_service =
      InstantServiceFactory::GetForProfile(profile);
  return instant_service ? instant_service->GetInstantSearchPrerenderer()
                         : nullptr;
}

void InstantSearchPrerenderer::Init(
    content::SessionStorageNamespace* session_storage_namespace,
    const gfx::Size& size) {
  // TODO(kmadhusu): Enable Instant for Incognito profile.
  if (profile_->IsOffTheRecord())
    return;

  // Only cancel the old prerender after starting the new one, so if the URLs
  // are the same, the underlying prerender will be reused.
  std::unique_ptr<prerender::PrerenderHandle> old_prerender_handle =
      std::move(prerender_handle_);
  prerender::PrerenderManager* prerender_manager =
      prerender::PrerenderManagerFactory::GetForBrowserContext(profile_);
  if (prerender_manager) {
    prerender_handle_ = prerender_manager->AddPrerenderForInstant(
        prerender_url_, session_storage_namespace, size);
  }
  if (old_prerender_handle)
    old_prerender_handle->OnCancel();
}

void InstantSearchPrerenderer::Cancel() {
  if (!prerender_handle_)
    return;

  last_instant_suggestion_ = InstantSuggestion();
  prerender_handle_->OnCancel();
  prerender_handle_.reset();
}

void InstantSearchPrerenderer::Prerender(const InstantSuggestion& suggestion) {
  if (!prerender_handle_)
    return;

  if (last_instant_suggestion_.text == suggestion.text)
    return;

  if (last_instant_suggestion_.text.empty() &&
      !prerender_handle_->IsFinishedLoading())
    return;

  if (!prerender_contents())
    return;

  last_instant_suggestion_ = suggestion;
  SearchTabHelper::FromWebContents(prerender_contents())->
      SetSuggestionToPrefetch(suggestion);
}

void InstantSearchPrerenderer::Commit(
    const EmbeddedSearchRequestParams& params) {
  DCHECK(prerender_handle_);
  DCHECK(prerender_contents());
  SearchTabHelper::FromWebContents(prerender_contents())->Submit(params);
}

bool InstantSearchPrerenderer::CanCommitQuery(
    content::WebContents* source,
    const base::string16& query) const {
  if (!source || query.empty() || !prerender_handle_ ||
      !prerender_handle_->IsFinishedLoading() || !prerender_contents()) {
    return false;
  }

  return true;
}

bool InstantSearchPrerenderer::UsePrerenderedPage(
    const GURL& url,
    chrome::NavigateParams* params) {
  base::string16 search_terms =
      search::ExtractSearchTermsFromURL(profile_, url);
  prerender::PrerenderManager* prerender_manager =
      prerender::PrerenderManagerFactory::GetForBrowserContext(profile_);
  if (search_terms.empty() || !params->target_contents ||
      !prerender_contents() || !prerender_manager ||
      params->disposition != WindowOpenDisposition::CURRENT_TAB) {
    Cancel();
    return false;
  }

  // Do not use prerendered page for renderer initiated search requests.
  if (params->is_renderer_initiated &&
      ui::PageTransitionCoreTypeIs(params->transition,
                                   ui::PAGE_TRANSITION_LINK)) {
    Cancel();
    return false;
  }

  bool success = prerender_manager->MaybeUsePrerenderedPage(
      prerender_contents()->GetURL(), params);
  prerender_handle_.reset();
  return success;
}

bool InstantSearchPrerenderer::IsAllowed(const AutocompleteMatch& match,
                                         content::WebContents* source) const {
  // We block prerendering for anything but search-type matches associated with
  // the default search provider.
  //
  // This is more restrictive than necessary.  All that's really needed to be
  // able to successfully prerender is that the |destination_url| of |match| be
  // from the same origin and path as the default search engine, and the params
  // to be sent to the server be a subset of the params we can pass to the
  // prerenderer.  So for example, if we normally prerender search URLs like
  // https://google.com/search?q=foo&x=bar, then any match with a URL like that,
  // potentially with the q and/or x params omitted, is prerenderable.
  //
  // However, if the URL has other params _not_ normally in the prerendered URL,
  // there's no way to pass them to the prerendered page, and worse, if the URL
  // does something like specifying params in both the query and ref sections of
  // the URL (as Google URLs often do), it can quickly become impossible to
  // figure out how to correctly tease out the right param names and values to
  // send.  Rather than try and write parsing code to deal with all these kinds
  // of cases, for various different search engines, including accommodating
  // changing behavior over time, we do the simple restriction described above.
  // This handles the by-far-the-most-common cases while still being simple and
  // maintainable.
  return source && AutocompleteMatch::IsSearchType(match.type) &&
      MatchIsFromDefaultSearchProvider(match, profile_);
}

content::WebContents* InstantSearchPrerenderer::prerender_contents() const {
  return (prerender_handle_ && prerender_handle_->contents()) ?
      prerender_handle_->contents()->prerender_contents() : NULL;
}
