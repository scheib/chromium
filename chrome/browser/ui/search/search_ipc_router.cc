// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/search/search_ipc_router.h"

#include <utility>

#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/search/instant_service.h"
#include "chrome/browser/search/instant_service_factory.h"
#include "chrome/browser/search/search.h"
#include "chrome/common/render_messages.h"
#include "components/search/search.h"
#include "content/public/browser/navigation_details.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/associated_interface_provider.h"
#include "content/public/common/child_process_host.h"

namespace {

bool IsInInstantProcess(content::RenderFrameHost* render_frame) {
  content::RenderProcessHost* process_host = render_frame->GetProcess();
  const InstantService* instant_service = InstantServiceFactory::GetForProfile(
      Profile::FromBrowserContext(process_host->GetBrowserContext()));
  if (!instant_service)
    return false;

  return instant_service->IsInstantProcess(process_host->GetID());
}

class SearchBoxClientFactoryImpl
    : public SearchIPCRouter::SearchBoxClientFactory,
      public chrome::mojom::EmbeddedSearchConnector {
 public:
  // |web_contents| and |binding| must outlive this object.
  SearchBoxClientFactoryImpl(
      content::WebContents* web_contents,
      mojo::AssociatedBinding<chrome::mojom::Instant>* binding)
      : client_binding_(binding), factory_bindings_(web_contents, this) {
    DCHECK(web_contents);
    DCHECK(binding);
    // Before we are connected to a frame we throw away all messages.
    mojo::MakeIsolatedRequest(&search_box_);
  }

  chrome::mojom::SearchBox* GetSearchBox() override {
    return search_box_.get();
  }

 private:
  void Connect(chrome::mojom::InstantAssociatedRequest request,
               chrome::mojom::SearchBoxAssociatedPtrInfo client) override;

  // An interface used to push updates to the frame that connected to us. Before
  // we've been connected to a frame, messages sent on this interface go into
  // the void.
  chrome::mojom::SearchBoxAssociatedPtr search_box_;

  // Used to bind incoming interface requests to the implementation, which lives
  // in SearchIPCRouter.
  mojo::AssociatedBinding<chrome::mojom::Instant>* client_binding_;

  // Binding used to listen to connection requests.
  content::WebContentsFrameBindingSet<chrome::mojom::EmbeddedSearchConnector>
      factory_bindings_;

  DISALLOW_COPY_AND_ASSIGN(SearchBoxClientFactoryImpl);
};

void SearchBoxClientFactoryImpl::Connect(
    chrome::mojom::InstantAssociatedRequest request,
    chrome::mojom::SearchBoxAssociatedPtrInfo client) {
  content::RenderFrameHost* frame = factory_bindings_.GetCurrentTargetFrame();
  const bool is_main_frame = frame->GetParent() == nullptr;
  if (!IsInInstantProcess(frame) || !is_main_frame) {
    return;
  }
  client_binding_->Bind(std::move(request));
  search_box_.Bind(std::move(client));
}

}  // namespace

SearchIPCRouter::SearchIPCRouter(content::WebContents* web_contents,
                                 Delegate* delegate,
                                 std::unique_ptr<Policy> policy)
    : WebContentsObserver(web_contents),
      delegate_(delegate),
      policy_(std::move(policy)),
      commit_counter_(0),
      is_active_tab_(false),
      binding_(this),
      search_box_client_factory_(
          new SearchBoxClientFactoryImpl(web_contents, &binding_)) {
  DCHECK(web_contents);
  DCHECK(delegate);
  DCHECK(policy_.get());
}

SearchIPCRouter::~SearchIPCRouter() = default;

void SearchIPCRouter::OnNavigationEntryCommitted() {
  ++commit_counter_;
  search_box()->SetPageSequenceNumber(commit_counter_);
}

void SearchIPCRouter::SendChromeIdentityCheckResult(
    const base::string16& identity,
    bool identity_match) {
  if (!policy_->ShouldProcessChromeIdentityCheck())
    return;

  search_box()->ChromeIdentityCheckResult(identity, identity_match);
}

void SearchIPCRouter::SendHistorySyncCheckResult(bool sync_history) {
  if (!policy_->ShouldProcessHistorySyncCheck())
    return;

  search_box()->HistorySyncCheckResult(sync_history);
}

void SearchIPCRouter::SetSuggestionToPrefetch(
    const InstantSuggestion& suggestion) {
  if (!policy_->ShouldSendSetSuggestionToPrefetch())
    return;

  search_box()->SetSuggestionToPrefetch(suggestion);
}

void SearchIPCRouter::SetInputInProgress(bool input_in_progress) {
  if (!policy_->ShouldSendSetInputInProgress(is_active_tab_))
    return;

  search_box()->SetInputInProgress(input_in_progress);
}

void SearchIPCRouter::OmniboxFocusChanged(OmniboxFocusState state,
                                          OmniboxFocusChangeReason reason) {
  if (!policy_->ShouldSendOmniboxFocusChanged())
    return;

  search_box()->FocusChanged(state, reason);
}

void SearchIPCRouter::SendMostVisitedItems(
    const std::vector<InstantMostVisitedItem>& items) {
  if (!policy_->ShouldSendMostVisitedItems())
    return;

  search_box()->MostVisitedChanged(items);
}

void SearchIPCRouter::SendThemeBackgroundInfo(
    const ThemeBackgroundInfo& theme_info) {
  if (!policy_->ShouldSendThemeBackgroundInfo())
    return;

  search_box()->ThemeChanged(theme_info);
}

void SearchIPCRouter::Submit(const EmbeddedSearchRequestParams& params) {
  if (!policy_->ShouldSubmitQuery())
    return;

  search_box()->Submit(params);
}

void SearchIPCRouter::OnTabActivated() {
  is_active_tab_ = true;
}

void SearchIPCRouter::OnTabDeactivated() {
  is_active_tab_ = false;
}

void SearchIPCRouter::FocusOmnibox(int page_seq_no, OmniboxFocusState state) {
  if (page_seq_no != commit_counter_)
    return;

  if (!policy_->ShouldProcessFocusOmnibox(is_active_tab_))
    return;

  delegate_->FocusOmnibox(state);
}

void SearchIPCRouter::DeleteMostVisitedItem(int page_seq_no, const GURL& url) {
  if (page_seq_no != commit_counter_)
    return;

  if (!policy_->ShouldProcessDeleteMostVisitedItem())
    return;

  delegate_->OnDeleteMostVisitedItem(url);
}

void SearchIPCRouter::UndoMostVisitedDeletion(int page_seq_no,
                                              const GURL& url) {
  if (page_seq_no != commit_counter_)
    return;

  if (!policy_->ShouldProcessUndoMostVisitedDeletion())
    return;

  delegate_->OnUndoMostVisitedDeletion(url);
}

void SearchIPCRouter::UndoAllMostVisitedDeletions(int page_seq_no) {
  if (page_seq_no != commit_counter_)
    return;

  if (!policy_->ShouldProcessUndoAllMostVisitedDeletions())
    return;

  delegate_->OnUndoAllMostVisitedDeletions();
}

void SearchIPCRouter::LogEvent(int page_seq_no,
                               NTPLoggingEventType event,
                               base::TimeDelta time) {
  if (page_seq_no != commit_counter_)
    return;

  if (!policy_->ShouldProcessLogEvent())
    return;

  delegate_->OnLogEvent(event, time);
}

void SearchIPCRouter::LogMostVisitedImpression(
    int page_seq_no,
    int position,
    ntp_tiles::TileSource tile_source,
    ntp_tiles::TileVisualType tile_type) {
  if (page_seq_no != commit_counter_)
    return;

  // Logging impressions is controlled by the same policy as logging events.
  if (!policy_->ShouldProcessLogEvent())
    return;

  delegate_->OnLogMostVisitedImpression(position, tile_source, tile_type);
}

void SearchIPCRouter::LogMostVisitedNavigation(
    int page_seq_no,
    int position,
    ntp_tiles::TileSource tile_source,
    ntp_tiles::TileVisualType tile_type) {
  if (page_seq_no != commit_counter_)
    return;

  // Logging navigations is controlled by the same policy as logging events.
  if (!policy_->ShouldProcessLogEvent())
    return;

  delegate_->OnLogMostVisitedNavigation(position, tile_source, tile_type);
}

void SearchIPCRouter::PasteAndOpenDropdown(int page_seq_no,
                                           const base::string16& text) {
  if (page_seq_no != commit_counter_)
    return;

  if (!policy_->ShouldProcessPasteIntoOmnibox(is_active_tab_))
    return;

  delegate_->PasteIntoOmnibox(text);
}

void SearchIPCRouter::ChromeIdentityCheck(int page_seq_no,
                                          const base::string16& identity) {
  if (page_seq_no != commit_counter_)
    return;

  if (!policy_->ShouldProcessChromeIdentityCheck())
    return;

  delegate_->OnChromeIdentityCheck(identity);
}

void SearchIPCRouter::HistorySyncCheck(int page_seq_no) {
  if (page_seq_no != commit_counter_)
    return;

  if (!policy_->ShouldProcessHistorySyncCheck())
    return;

  delegate_->OnHistorySyncCheck();
}

void SearchIPCRouter::set_delegate_for_testing(Delegate* delegate) {
  DCHECK(delegate);
  delegate_ = delegate;
}

void SearchIPCRouter::set_policy_for_testing(std::unique_ptr<Policy> policy) {
  DCHECK(policy);
  policy_ = std::move(policy);
}
