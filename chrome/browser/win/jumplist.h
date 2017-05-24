// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_WIN_JUMPLIST_H_
#define CHROME_BROWSER_WIN_JUMPLIST_H_

#include <stddef.h>

#include <list>
#include <memory>
#include <string>
#include <utility>

#include "base/containers/flat_map.h"
#include "base/files/file_path.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/string16.h"
#include "base/synchronization/lock.h"
#include "base/task/cancelable_task_tracker.h"
#include "base/timer/timer.h"
#include "chrome/browser/prefs/incognito_mode_prefs.h"
#include "chrome/browser/win/jumplist_updater.h"
#include "components/history/core/browser/history_service.h"
#include "components/history/core/browser/history_types.h"
#include "components/history/core/browser/top_sites_observer.h"
#include "components/keyed_service/core/refcounted_keyed_service.h"
#include "components/sessions/core/tab_restore_service.h"
#include "components/sessions/core/tab_restore_service_observer.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"

namespace base {
class SingleThreadTaskRunner;
class SequencedTaskRunner;
}

namespace chrome {
struct FaviconImageResult;
}

class JumpListFactory;
class PrefChangeRegistrar;
class Profile;

// A class which implements an application JumpList.
// This class encapsulates operations required for updating an application
// JumpList:
// * Retrieving "Most Visited" pages from HistoryService;
// * Retrieving strings from the application resource;
// * Adding COM objects to JumpList, etc.
//
// This class observes the tabs and policies of the given Profile and updates
// the JumpList whenever a change is detected.
//
// Updating a JumpList requires some file operations and it is not good to
// update it in a UI thread. To solve this problem, this class posts to a
// runnable method when it actually updates a JumpList.
//
// Note. base::CancelableTaskTracker is not thread safe, so we
// always delete JumpList on UI thread (the same thread it got constructed on).
class JumpList : public sessions::TabRestoreServiceObserver,
                 public history::TopSitesObserver,
                 public RefcountedKeyedService,
                 public base::NonThreadSafe {
 public:
  struct JumpListData {
    JumpListData();
    ~JumpListData();

    // Lock for most_visited_pages_, recently_closed_pages_, icon_urls_
    // as they may be used by up to 2 threads.
    base::Lock list_lock_;

    // A list of URLs we need to retrieve their favicons,
    // protected by the list_lock_.
    typedef std::pair<std::string, scoped_refptr<ShellLinkItem> > URLPair;
    std::list<URLPair> icon_urls_;

    // Items in the "Most Visited" category of the application JumpList,
    // protected by the list_lock_.
    ShellLinkItemList most_visited_pages_;

    // Items in the "Recently Closed" category of the application JumpList,
    // protected by the list_lock_.
    ShellLinkItemList recently_closed_pages_;

    // A boolean flag indicating if "Most Visited" category of the JumpList
    // has new updates therefore its icons need to be updated.
    // By default, this flag is set to false. If there's any change in
    // TabRestoreService, this flag will be set to true.
    bool most_visited_pages_have_updates_ = false;

    // A boolean flag indicating if "Recently Closed" category of the JumpList
    // has new updates therefore its icons need to be updated.
    // By default, this flag is set to false. If there's any change in TopSites
    // service, this flag will be set to true.
    bool recently_closed_pages_have_updates_ = false;
  };

  // Observer callback for TabRestoreService::Observer to notify when a tab is
  // added or removed.
  void TabRestoreServiceChanged(sessions::TabRestoreService* service) override;

  // Observer callback to notice when our associated TabRestoreService
  // is destroyed.
  void TabRestoreServiceDestroyed(
      sessions::TabRestoreService* service) override;

  // Cancel a pending jumplist update.
  void CancelPendingUpdate();

  // Terminate the jumplist: cancel any pending updates and stop observing
  // the Profile and its services. This must be called before the |profile_|
  // is destroyed.
  void Terminate();

  // RefcountedKeyedService:
  void ShutdownOnUIThread() override;

  // Returns true if the custom JumpList is enabled.
  static bool Enabled();

 private:
  friend JumpListFactory;
  explicit JumpList(Profile* profile);  // Use JumpListFactory instead
  ~JumpList() override;

  enum class JumpListCategory { kMostVisited, kRecentlyClosed };

  // Adds a new ShellLinkItem for |tab| to |data| provided that doing so will
  // not exceed |max_items|.
  bool AddTab(const sessions::TabRestoreService::Tab& tab,
              size_t max_items,
              JumpListData* data);

  // Adds a new ShellLinkItem for each tab in |window| to |data| provided that
  // doing so will not exceed |max_items|.
  void AddWindow(const sessions::TabRestoreService::Window& window,
                 size_t max_items,
                 JumpListData* data);

  // Starts loading a favicon for each URL in |icon_urls_|.
  // This function sends a query to HistoryService.
  // When finishing loading all favicons, this function posts a task that
  // decompresses collected favicons and updates a JumpList.
  void StartLoadingFavicon();

  // A callback function for HistoryService that notify when a requested favicon
  // is available.
  // To avoid file operations, this function just attaches the given data to
  // a ShellLinkItem object.
  void OnFaviconDataAvailable(
      const favicon_base::FaviconImageResult& image_result);

  // Callback for TopSites that notifies when the "Most Visited" list is
  // available. This function updates the ShellLinkItemList objects and
  // begins the process of fetching favicons for the URLs.
  void OnMostVisitedURLsAvailable(
      const history::MostVisitedURLList& data);

  // Callback for changes to the incognito mode availability pref.
  void OnIncognitoAvailabilityChanged();

  // Posts tasks to update the JumpList and delete any obsolete JumpList related
  // folders.
  void PostRunUpdate();

  // history::TopSitesObserver implementation.
  void TopSitesLoaded(history::TopSites* top_sites) override;
  void TopSitesChanged(history::TopSites* top_sites,
                       ChangeReason change_reason) override;

  // Called on a timer to update the most visited URLs after requests storms
  // have subsided.
  void DeferredTopSitesChanged();

  // Called on a timer to update the "Recently Closed" category of JumpList
  // after requests storms have subsided.
  void DeferredTabRestoreServiceChanged();

  // Deletes icon files of |category| in |icon_dir| which are not in the cache
  // anymore.
  void DeleteIconFiles(const base::FilePath& icon_dir,
                       JumpListCategory category);

  // Creates at most |max_items| icon files of |category| in |icon_dir| for the
  // asynchrounously loaded icons stored in |item_list|.
  void CreateIconFiles(const base::FilePath& icon_dir,
                       const ShellLinkItemList& item_list,
                       size_t max_items,
                       JumpListCategory category);

  // Updates icon files in |icon_dir|, which includes deleting old icons and
  // creating at most |slot_limit| new icons for |page_list|.
  void UpdateIconFiles(const base::FilePath& icon_dir,
                       const ShellLinkItemList& page_list,
                       size_t slot_limit,
                       JumpListCategory category);

  // Updates the jumplist, once all the data has been fetched. This method calls
  // UpdateJumpList() to do most of the work.
  void RunUpdateJumpList(
      IncognitoModePrefs::Availability incognito_availability,
      const base::string16& app_id,
      const base::FilePath& profile_dir,
      base::RefCountedData<JumpListData>* ref_counted_data);

  // Updates the application JumpList, which consists of 1) delete old icon
  // files; 2) create new icon files; 3) notify the OS. This method is called
  // from RunUpdateJumpList().
  // Note that any timeout error along the way results in the old jumplist being
  // left as-is, while any non-timeout error results in the old jumplist being
  // left as-is, but without icon files.
  bool UpdateJumpList(const base::string16& app_id,
                      const base::FilePath& profile_dir,
                      const ShellLinkItemList& most_visited_pages,
                      const ShellLinkItemList& recently_closed_pages,
                      bool most_visited_pages_have_updates,
                      bool recently_closed_pages_have_updates,
                      IncognitoModePrefs::Availability incognito_availability);

  // Tracks FaviconService tasks.
  base::CancelableTaskTracker cancelable_task_tracker_;

  // The Profile object is used to listen for events.
  Profile* profile_;

  // Lives on the UI thread.
  std::unique_ptr<PrefChangeRegistrar> pref_change_registrar_;

  // App id to associate with the jump list.
  base::string16 app_id_;

  // Timer for requesting delayed updates of the "Most Visited" category of
  // jumplist.
  base::OneShotTimer timer_most_visited_;

  // Timer for requesting delayed updates of the "Recently Closed" category of
  // jumplist.
  base::OneShotTimer timer_recently_closed_;

  // Number of updates to skip to alleviate the machine when a previous update
  // was too slow. Updates will be resumed when this reaches 0 again.
  int updates_to_skip_ = 0;

  // Holds data that can be accessed from multiple threads.
  scoped_refptr<base::RefCountedData<JumpListData>> jumplist_data_;

  // The icon file paths of the most visited links and the recently closed links
  // in the current jumplist, indexed by tab url, respectively.
  // They may only be accessed on update_jumplist_task_runner_.
  base::flat_map<std::string, base::FilePath> most_visited_icons_;
  base::flat_map<std::string, base::FilePath> recently_closed_icons_;

  // Id of last favicon task. It's used to cancel current task if a new one
  // comes in before it finishes.
  base::CancelableTaskTracker::TaskId task_id_;

  // A task runner running tasks to update the JumpList.
  scoped_refptr<base::SingleThreadTaskRunner> update_jumplist_task_runner_;

  // A task runner running tasks to delete JumpListIcons directory and
  // JumpListIconsOld directory.
  scoped_refptr<base::SequencedTaskRunner> delete_jumplisticons_task_runner_;

  // For callbacks may be run after destruction.
  base::WeakPtrFactory<JumpList> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(JumpList);
};

#endif  // CHROME_BROWSER_WIN_JUMPLIST_H_
