// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/data_use_measurement/chrome_data_use_ascriber.h"

#include <string>

#include "base/memory/ptr_util.h"
#include "chrome/browser/data_use_measurement/chrome_data_use_recorder.h"
#include "components/data_use_measurement/content/content_url_request_classifier.h"
#include "components/data_use_measurement/core/data_use_recorder.h"
#include "components/data_use_measurement/core/data_use_user_data.h"
#include "components/data_use_measurement/core/url_request_classifier.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/resource_request_info.h"
#include "content/public/common/browser_side_navigation_policy.h"
#include "ipc/ipc_message.h"
#include "net/url_request/url_request.h"

namespace data_use_measurement {

// static
const void* ChromeDataUseAscriber::DataUseRecorderEntryAsUserData::
    kUserDataKey = static_cast<void*>(
        &ChromeDataUseAscriber::DataUseRecorderEntryAsUserData::kUserDataKey);

ChromeDataUseAscriber::DataUseRecorderEntryAsUserData::
    DataUseRecorderEntryAsUserData(DataUseRecorderEntry entry)
    : entry_(entry) {}

ChromeDataUseAscriber::DataUseRecorderEntryAsUserData::
    ~DataUseRecorderEntryAsUserData() {}

ChromeDataUseAscriber::ChromeDataUseAscriber() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);
}

ChromeDataUseAscriber::~ChromeDataUseAscriber() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);
  DCHECK(subframe_to_mainframe_map_.empty());
  // |data_use_recorders_| can be non empty, when mainframe url requests are
  // created but no mainframe navigations take place.
  // TODO(rajendrant): Enable this check when fixed for unittests.
  // DCHECK(data_use_recorders_.empty());
}

ChromeDataUseRecorder* ChromeDataUseAscriber::GetOrCreateDataUseRecorder(
    net::URLRequest* request) {
  DataUseRecorderEntry entry = GetOrCreateDataUseRecorderEntry(request);
  return entry == data_use_recorders_.end() ? nullptr : &(*entry);
}

ChromeDataUseRecorder* ChromeDataUseAscriber::GetDataUseRecorder(
    const net::URLRequest& request) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);

  // TODO(ryansturm): Handle PlzNavigate (http://crbug/664233).
  if (content::IsBrowserSideNavigationEnabled())
    return nullptr;

  // If a DataUseRecorder has already been set as user data, then return that.
  auto* user_data = static_cast<DataUseRecorderEntryAsUserData*>(
      request.GetUserData(DataUseRecorderEntryAsUserData::kUserDataKey));
  return user_data ? &(*user_data->recorder_entry()) : nullptr;
}

ChromeDataUseAscriber::DataUseRecorderEntry
ChromeDataUseAscriber::GetOrCreateDataUseRecorderEntry(
    net::URLRequest* request) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);

  // TODO(ryansturm): Handle PlzNavigate (http://crbug/664233).
  if (content::IsBrowserSideNavigationEnabled())
    return data_use_recorders_.end();

  // If a DataUseRecorder has already been set as user data, then return that.
  auto* user_data = static_cast<DataUseRecorderEntryAsUserData*>(
      request->GetUserData(DataUseRecorderEntryAsUserData::kUserDataKey));
  if (user_data)
    return user_data->recorder_entry();

  // If request is associated with a ChromeService, create a new
  // DataUseRecorder for it. There is no reason to aggregate URLRequests
  // from ChromeServices into the same DataUseRecorder instance.
  DataUseUserData* service = static_cast<DataUseUserData*>(
      request->GetUserData(DataUseUserData::kUserDataKey));
  if (service) {
    DataUseRecorderEntry entry =
        CreateNewDataUseRecorder(request, DataUse::TrafficType::SERVICES);
    entry->data_use().set_description(
        DataUseUserData::GetServiceNameAsString(service->service_name()));
    return entry;
  }

  int render_process_id = -1;
  int render_frame_id = -1;
  bool has_valid_frame = content::ResourceRequestInfo::GetRenderFrameForRequest(
      request, &render_process_id, &render_frame_id);
  if (has_valid_frame &&
      render_frame_id != SpecialRoutingIDs::MSG_ROUTING_NONE) {
    DCHECK(render_process_id >= 0 || render_frame_id >= 0);

    // Browser tests may not set up DataUseWebContentsObservers in which case
    // this class never sees navigation and frame events so DataUseRecorders
    // will never be destroyed. To avoid this, we ignore requests whose
    // render frames don't have a record. However, this can also be caused by
    // URLRequests racing the frame create events.
    // TODO(kundaji): Add UMA.
    RenderFrameHostID frame_key(render_process_id, render_frame_id);
    auto main_frame_key_iter = subframe_to_mainframe_map_.find(frame_key);
    if (main_frame_key_iter == subframe_to_mainframe_map_.end()) {
      return data_use_recorders_.end();
    }
    auto frame_iter =
        main_render_frame_data_use_map_.find(main_frame_key_iter->second);
    if (frame_iter == main_render_frame_data_use_map_.end()) {
      return data_use_recorders_.end();
    }

    const content::ResourceRequestInfo* request_info =
        content::ResourceRequestInfo::ForRequest(request);
    content::ResourceType resource_type =
        request_info ? request_info->GetResourceType()
                     : content::RESOURCE_TYPE_LAST_TYPE;

    if (resource_type == content::RESOURCE_TYPE_MAIN_FRAME) {
      content::GlobalRequestID navigation_key =
          request_info->GetGlobalRequestID();

      DataUseRecorderEntry new_entry =
          CreateNewDataUseRecorder(request, DataUse::TrafficType::USER_TRAFFIC);
      new_entry->set_main_frame_request_id(navigation_key);
      pending_navigation_data_use_map_.insert(
          std::make_pair(navigation_key, new_entry));

      return new_entry;
    }

    DCHECK(frame_iter != main_render_frame_data_use_map_.end());
    auto entry = frame_iter->second;
    request->SetUserData(
        DataUseRecorderEntryAsUserData::kUserDataKey,
        base::MakeUnique<DataUseRecorderEntryAsUserData>(entry));
    entry->AddPendingURLRequest(request);
    return entry;
  }

  // Create a new DataUseRecorder for all other requests.
  DataUseRecorderEntry entry = CreateNewDataUseRecorder(
      request,
      content::ResourceRequestInfo::OriginatedFromServiceWorker(request)
          ? DataUse::TrafficType::SERVICE_WORKER
          : DataUse::TrafficType::UNKNOWN);
  DataUse& data_use = entry->data_use();
  data_use.set_url(request->url());
  return entry;
}

void ChromeDataUseAscriber::OnUrlRequestCompleted(
    const net::URLRequest& request,
    bool started) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);

  ChromeDataUseRecorder* recorder = GetDataUseRecorder(request);

  if (!recorder)
    return;

  const content::ResourceRequestInfo* request_info =
      content::ResourceRequestInfo::ForRequest(&request);
  if (!request_info ||
      request_info->GetResourceType() != content::RESOURCE_TYPE_MAIN_FRAME) {
    return;
  }

  // If mainframe request was not successful, then NavigationHandle in
  // DidFinishMainFrameNavigation will not have GlobalRequestID. So we erase the
  // DataUseRecorderEntry here.
  if (!request.status().is_success())
    pending_navigation_data_use_map_.erase(recorder->main_frame_request_id());
}

void ChromeDataUseAscriber::OnUrlRequestDestroyed(net::URLRequest* request) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);

  // TODO(rajendrant): GetDataUseRecorder is sufficient and
  // GetOrCreateDataUseRecorderEntry is not needed. The entry gets created in
  // DataUseAscriber::OnBeforeUrlRequest().
  DataUseRecorderEntry entry = GetOrCreateDataUseRecorderEntry(request);

  if (entry == data_use_recorders_.end())
    return;

  for (auto& observer : observers_)
    observer.OnPageResourceLoad(*request, &entry->data_use());

  RenderFrameHostID frame_key = entry->main_frame_id();
  auto frame_iter = main_render_frame_data_use_map_.find(frame_key);
  bool is_in_render_frame_map =
      frame_iter != main_render_frame_data_use_map_.end() &&
      frame_iter->second->HasPendingURLRequest(request);

  const content::ResourceRequestInfo* request_info =
      content::ResourceRequestInfo::ForRequest(request);
  bool is_in_pending_navigation_map =
      request_info &&
      request_info->GetResourceType() == content::RESOURCE_TYPE_MAIN_FRAME &&
      pending_navigation_data_use_map_.find(entry->main_frame_request_id()) !=
          pending_navigation_data_use_map_.end();

  DataUseAscriber::OnUrlRequestDestroyed(request);
  request->RemoveUserData(DataUseRecorderEntryAsUserData::kUserDataKey);

  if (entry->IsDataUseComplete() && !is_in_render_frame_map &&
      !is_in_pending_navigation_map) {
    NotifyDataUseCompleted(entry);
    data_use_recorders_.erase(entry);
  }
}

void ChromeDataUseAscriber::RenderFrameCreated(int render_process_id,
                                               int render_frame_id,
                                               int main_render_process_id,
                                               int main_render_frame_id) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);

  if (content::IsBrowserSideNavigationEnabled())
    return;

  if (main_render_process_id != -1 && main_render_frame_id != -1) {
    // Create an entry in |subframe_to_mainframe_map_| for this frame mapped to
    // it's parent frame.
    subframe_to_mainframe_map_.insert(std::make_pair(
        RenderFrameHostID(render_process_id, render_frame_id),
        RenderFrameHostID(main_render_process_id, main_render_frame_id)));
  } else {
    subframe_to_mainframe_map_.insert(
        std::make_pair(RenderFrameHostID(render_process_id, render_frame_id),
                       RenderFrameHostID(render_process_id, render_frame_id)));
    auto frame_iter = main_render_frame_data_use_map_.find(
        RenderFrameHostID(render_process_id, render_frame_id));
    DCHECK(frame_iter == main_render_frame_data_use_map_.end());
    DataUseRecorderEntry entry =
        CreateNewDataUseRecorder(nullptr, DataUse::TrafficType::UNKNOWN);
    entry->set_main_frame_id(
        RenderFrameHostID(render_process_id, render_frame_id));
    main_render_frame_data_use_map_.insert(std::make_pair(
        RenderFrameHostID(render_process_id, render_frame_id), entry));
  }
}

void ChromeDataUseAscriber::RenderFrameDeleted(int render_process_id,
                                               int render_frame_id,
                                               int main_render_process_id,
                                               int main_render_frame_id) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);

  if (content::IsBrowserSideNavigationEnabled())
    return;

  RenderFrameHostID key(render_process_id, render_frame_id);

  if (main_render_process_id == -1 && main_render_frame_id == -1) {
    auto frame_iter = main_render_frame_data_use_map_.find(key);
    DataUseRecorderEntry entry = frame_iter->second;
    if (entry->IsDataUseComplete()) {
      NotifyDataUseCompleted(entry);
      data_use_recorders_.erase(entry);
    }
    main_render_frame_data_use_map_.erase(frame_iter);
  }
  subframe_to_mainframe_map_.erase(key);
  visible_main_render_frames_.erase(key);
}

void ChromeDataUseAscriber::DidStartMainFrameNavigation(
    GURL gurl,
    int render_process_id,
    int render_frame_id,
    void* navigation_handle) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);
}

void ChromeDataUseAscriber::ReadyToCommitMainFrameNavigation(
    GURL gurl,
    content::GlobalRequestID global_request_id,
    int render_process_id,
    int render_frame_id,
    bool is_same_page_navigation,
    void* navigation_handle) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);

  // Find the DataUseRecorderEntry the frame is associated with
  auto frame_it = main_render_frame_data_use_map_.find(
      RenderFrameHostID(render_process_id, render_frame_id));

  // Find the pending navigation entry.
  auto navigation_iter =
      pending_navigation_data_use_map_.find(global_request_id);
  // We might not find a navigation entry since the pending navigation may not
  // have caused any HTTP or HTTPS URLRequests to be made.
  if (navigation_iter == pending_navigation_data_use_map_.end()) {
    // No pending navigation entry to worry about. However, the old frame entry
    // must be removed from frame map, and possibly marked complete and deleted.
    if (frame_it != main_render_frame_data_use_map_.end()) {
      DataUseRecorderEntry old_frame_entry = frame_it->second;
      DataUse::TrafficType old_traffic_type =
          old_frame_entry->data_use().traffic_type();
      main_render_frame_data_use_map_.erase(frame_it);
      if (old_frame_entry->IsDataUseComplete()) {
        NotifyDataUseCompleted(old_frame_entry);
        data_use_recorders_.erase(old_frame_entry);
      }

      // Add a new recorder to the render frame map to replace the deleted one.
      DataUseRecorderEntry entry = data_use_recorders_.emplace(
          data_use_recorders_.end(), old_traffic_type);
      std::pair<int, int> frame_key =
          RenderFrameHostID(render_process_id, render_frame_id);
      entry->set_main_frame_id(frame_key);
      main_render_frame_data_use_map_.insert(std::make_pair(frame_key, entry));
    }
    return;
  }

  DataUseRecorderEntry entry = navigation_iter->second;
  pending_navigation_data_use_map_.erase(navigation_iter);
  entry->set_main_frame_id(
      RenderFrameHostID(render_process_id, render_frame_id));

  // If the frame has already been deleted then mark this navigation as having
  // completed its data use.
  if (frame_it == main_render_frame_data_use_map_.end()) {
    if (entry->IsDataUseComplete()) {
      NotifyDataUseCompleted(entry);
      data_use_recorders_.erase(entry);
    }
    return;
  }
  DataUseRecorderEntry old_frame_entry = frame_it->second;
  if (is_same_page_navigation) {
    old_frame_entry->MergeFrom(&(*entry));

    for (auto* request : entry->pending_url_requests()) {
      request->RemoveUserData(DataUseRecorderEntryAsUserData::kUserDataKey);
      request->SetUserData(
          DataUseRecorderEntryAsUserData::kUserDataKey,
          base::MakeUnique<DataUseRecorderEntryAsUserData>(old_frame_entry));
      old_frame_entry->AddPendingURLRequest(request);
    }

    entry->RemoveAllPendingURLRequests();

    data_use_recorders_.erase(entry);
  } else {
    // Navigation is not same page, so remove old entry from
    // |main_render_frame_data_use_map_|, possibly marking it complete.
    main_render_frame_data_use_map_.erase(frame_it);
    if (old_frame_entry->IsDataUseComplete()) {
      NotifyDataUseCompleted(old_frame_entry);
      data_use_recorders_.erase(old_frame_entry);

      if (visible_main_render_frames_.find(
              RenderFrameHostID(render_process_id, render_frame_id)) !=
          visible_main_render_frames_.end()) {
        entry->set_is_visible(true);
      }
    }

    DataUse& data_use = entry->data_use();

    DCHECK(!data_use.url().is_valid() || data_use.url() == gurl)
        << "is valid: " << data_use.url().is_valid()
        << "; data_use.url(): " << data_use.url().spec()
        << "; gurl: " << gurl.spec();
    if (!data_use.url().is_valid()) {
      data_use.set_url(gurl);
    }

    main_render_frame_data_use_map_.insert(std::make_pair(
        RenderFrameHostID(render_process_id, render_frame_id), entry));
  }
}

void ChromeDataUseAscriber::DidFinishNavigation(int render_process_id,
                                                int render_frame_id,
                                                uint32_t page_transition) {
  auto frame_it = main_render_frame_data_use_map_.find(
      RenderFrameHostID(render_process_id, render_frame_id));
  if (frame_it != main_render_frame_data_use_map_.end()) {
    frame_it->second->set_page_transition(page_transition);

    for (auto& observer : observers_)
      observer.OnPageLoadCommit(&frame_it->second->data_use());
  }
}

void ChromeDataUseAscriber::NotifyDataUseCompleted(DataUseRecorderEntry entry) {
  for (auto& observer : observers_)
    observer.OnPageLoadComplete(&entry->data_use());
}

std::unique_ptr<URLRequestClassifier>
ChromeDataUseAscriber::CreateURLRequestClassifier() const {
  return base::MakeUnique<ContentURLRequestClassifier>();
}

ChromeDataUseAscriber::DataUseRecorderEntry
ChromeDataUseAscriber::CreateNewDataUseRecorder(
    net::URLRequest* request,
    DataUse::TrafficType traffic_type) {
  DataUseRecorderEntry entry =
      data_use_recorders_.emplace(data_use_recorders_.end(), traffic_type);
  if (request) {
    entry->AddPendingURLRequest(request);
    request->SetUserData(
        DataUseRecorderEntryAsUserData::kUserDataKey,
        base::MakeUnique<DataUseRecorderEntryAsUserData>(entry));
  }
  return entry;
}

void ChromeDataUseAscriber::WasShownOrHidden(int main_render_process_id,
                                             int main_render_frame_id,
                                             bool visible) {
  RenderFrameHostID main_render_frame_host_id(main_render_process_id,
                                              main_render_frame_id);

  auto frame_iter =
      main_render_frame_data_use_map_.find(main_render_frame_host_id);
  if (frame_iter != main_render_frame_data_use_map_.end())
    frame_iter->second->set_is_visible(visible);

  if (visible) {
    visible_main_render_frames_.insert(main_render_frame_host_id);
  } else {
    visible_main_render_frames_.erase(main_render_frame_host_id);
  }
}

void ChromeDataUseAscriber::RenderFrameHostChanged(int old_render_process_id,
                                                   int old_render_frame_id,
                                                   int new_render_process_id,
                                                   int new_render_frame_id) {
  if (visible_main_render_frames_.find(
          RenderFrameHostID(old_render_process_id, old_render_frame_id)) !=
      visible_main_render_frames_.end()) {
    WasShownOrHidden(new_render_process_id, new_render_frame_id, true);
  }
}

}  // namespace data_use_measurement
