// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_DATA_USE_MEASUREMENT_CHROME_DATA_USE_ASCRIBER_H_
#define CHROME_BROWSER_DATA_USE_MEASUREMENT_CHROME_DATA_USE_ASCRIBER_H_

#include <list>
#include <memory>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>

#include "base/containers/hash_tables.h"
#include "base/hash.h"
#include "base/macros.h"
#include "base/supports_user_data.h"
#include "chrome/browser/data_use_measurement/chrome_data_use_recorder.h"
#include "components/data_use_measurement/core/data_use_ascriber.h"
#include "content/public/browser/global_request_id.h"
#include "url/gurl.h"

namespace content {
class RenderFrameHost;
}

namespace data_use_measurement {

class URLRequestClassifier;

// Browser implementation of DataUseAscriber. Maintains a list of
// DataUseRecorder instances, one for each source of data, such as a page
// load.
//
// A page includes all resources loaded in response to a main page navigation.
// The scope of a page load ends either when the tab is closed or a new page
// load is initiated by clicking on a link, entering a new URL, window location
// change, etc.
//
// For URLRequests initiated outside the context of a page load, such as
// Service Workers, Chrome Services, etc, a new instance of DataUseRecorder
// will be created for each URLRequest.
//
// Each page load will be associated with an instance of DataUseRecorder.
// Each URLRequest initiated in the context of a page load will be mapped to
// the DataUseRecorder instance for page load.
//
// This class lives entirely on the IO thread. It maintains a copy of frame and
// navigation information on the IO thread.
class ChromeDataUseAscriber : public DataUseAscriber {
 public:
  ChromeDataUseAscriber();

  ~ChromeDataUseAscriber() override;

  // DataUseAscriber implementation:
  ChromeDataUseRecorder* GetOrCreateDataUseRecorder(
      net::URLRequest* request) override;
  ChromeDataUseRecorder* GetDataUseRecorder(
      const net::URLRequest& request) override;
  void OnUrlRequestCompleted(const net::URLRequest& request,
                             bool started) override;
  void OnUrlRequestDestroyed(net::URLRequest* request) override;
  std::unique_ptr<URLRequestClassifier> CreateURLRequestClassifier()
      const override;

  // Called when a render frame host is created. When the render frame is a main
  // frame, |main_render_process_id| and |main_render_frame_id| should be -1.
  void RenderFrameCreated(int render_process_id,
                          int render_frame_id,
                          int main_render_process_id,
                          int main_render_frame_id);

  // Called when a render frame host is deleted. When the render frame is a main
  // frame, |main_render_process_id| and |main_render_frame_id| should be -1.
  void RenderFrameDeleted(int render_process_id,
                          int render_frame_id,
                          int main_render_process_id,
                          int main_render_frame_id);

  // Called when a main frame navigation is started.
  void DidStartMainFrameNavigation(GURL gurl,
                                   int render_process_id,
                                   int render_frame_id,
                                   void* navigation_handle);

  // Called when a main frame navigation is ready to be committed in a
  // renderer.
  void ReadyToCommitMainFrameNavigation(
      GURL gurl,
      content::GlobalRequestID global_request_id,
      int render_process_id,
      int render_frame_id,
      bool is_same_page_navigation,
      void* navigation_handle);

  // Called every time the WebContents changes visibility.
  void WasShownOrHidden(int main_render_process_id,
                        int main_render_frame_id,
                        bool visible);

  // Called whenever one of the render frames of a WebContents is swapped.
  void RenderFrameHostChanged(int old_render_process_id,
                              int old_render_frame_id,
                              int new_render_process_id,
                              int new_render_frame_id);

  void DidFinishNavigation(int render_process_id,
                           int render_frame_id,
                           uint32_t page_transition);

 private:
  friend class ChromeDataUseAscriberTest;

  // Entry in the |data_use_recorders_| list which owns all instances of
  // DataUseRecorder.
  typedef std::list<ChromeDataUseRecorder> DataUseRecorderList;
  typedef DataUseRecorderList::iterator DataUseRecorderEntry;

  struct GlobalRequestIDHash {
   public:
    std::size_t operator()(const content::GlobalRequestID& x) const {
      return base::HashInts(x.child_id, x.request_id);
    }
  };

  class DataUseRecorderEntryAsUserData : public base::SupportsUserData::Data {
   public:
    explicit DataUseRecorderEntryAsUserData(DataUseRecorderEntry entry);

    ~DataUseRecorderEntryAsUserData() override;

    DataUseRecorderEntry recorder_entry() { return entry_; }

    static const void* kUserDataKey;

   private:
    DataUseRecorderEntry entry_;
  };

  DataUseRecorderEntry GetOrCreateDataUseRecorderEntry(
      net::URLRequest* request);

  void NotifyDataUseCompleted(DataUseRecorderEntry entry);

  DataUseRecorderEntry CreateNewDataUseRecorder(
      net::URLRequest* request,
      DataUse::TrafficType traffic_type);

  bool IsRecorderInPendingNavigationMap(net::URLRequest* request);

  bool IsRecorderInRenderFrameMap(net::URLRequest* request);

  // Owner for all instances of DataUseRecorder. An instance is kept in this
  // list if any entity (render frame hosts, URLRequests, pending navigations)
  // that ascribe data use to the instance exists, and deleted when all
  // ascribing entities go away.
  DataUseRecorderList data_use_recorders_;

  // Map from RenderFrameHost to the DataUseRecorderEntry in
  // |data_use_recorders_| that the main frame ascribes data use to.
  base::hash_map<RenderFrameHostID, DataUseRecorderEntry>
      main_render_frame_data_use_map_;

  // Maps subframe IDs to the mainframe ID, so the mainframe lifetime can have
  // ownership over the lifetime of entries in |data_use_recorders_|. Mainframes
  // are mapped to themselves.
  base::hash_map<RenderFrameHostID, RenderFrameHostID>
      subframe_to_mainframe_map_;

  // Map from pending navigations to the DataUseRecorderEntry in
  // |data_use_recorders_| that the navigation ascribes data use to.
  std::unordered_map<content::GlobalRequestID,
                     DataUseRecorderEntry,
                     GlobalRequestIDHash>
      pending_navigation_data_use_map_;

  // Contains the mainframe IDs that are currently visible.
  base::hash_set<RenderFrameHostID> visible_main_render_frames_;

  DISALLOW_COPY_AND_ASSIGN(ChromeDataUseAscriber);
};

}  // namespace data_use_measurement

#endif  // CHROME_BROWSER_DATA_USE_MEASUREMENT_CHROME_DATA_USE_ASCRIBER_H_
