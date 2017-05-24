// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/win/jumplist.h"

#include "base/base_paths.h"
#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/command_line.h"
#include "base/containers/flat_set.h"
#include "base/files/file_util.h"
#include "base/metrics/histogram_macros.h"
#include "base/path_service.h"
#include "base/sequenced_task_runner.h"
#include "base/single_thread_task_runner.h"
#include "base/strings/string_piece.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task_scheduler/post_task.h"
#include "base/task_scheduler/task_traits.h"
#include "base/threading/thread.h"
#include "base/timer/elapsed_timer.h"
#include "base/trace_event/trace_event.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/favicon/favicon_service_factory.h"
#include "chrome/browser/history/top_sites_factory.h"
#include "chrome/browser/metrics/jumplist_metrics_win.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sessions/tab_restore_service_factory.h"
#include "chrome/browser/shell_integration_win.h"
#include "chrome/browser/win/jumplist_file_util.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/common/chrome_icon_resources_win.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/pref_names.h"
#include "chrome/grit/generated_resources.h"
#include "chrome/install_static/install_util.h"
#include "components/favicon/core/favicon_service.h"
#include "components/history/core/browser/top_sites.h"
#include "components/prefs/pref_change_registrar.h"
#include "components/sessions/core/session_types.h"
#include "components/strings/grit/components_strings.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/gfx/codec/png_codec.h"
#include "ui/gfx/favicon_size.h"
#include "ui/gfx/icon_util.h"
#include "ui/gfx/image/image.h"
#include "ui/gfx/image/image_family.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/image/image_skia_rep.h"
#include "url/gurl.h"

using content::BrowserThread;
using JumpListData = JumpList::JumpListData;

namespace {

// The number of updates to skip to alleviate the machine when a previous update
// was too slow.
constexpr int kUpdatesToSkipUnderHeavyLoad = 10;

// The delay before updating the JumpList to prevent update storms.
constexpr base::TimeDelta kDelayForJumplistUpdate =
    base::TimeDelta::FromMilliseconds(3500);

// The maximum allowed time for JumpListUpdater::BeginUpdate. Updates taking
// longer than this are discarded to prevent bogging down slow machines.
constexpr base::TimeDelta kTimeOutForJumplistBeginUpdate =
    base::TimeDelta::FromMilliseconds(500);

// The maximum allowed time for updating both "most visited" and "recently
// closed" categories via JumpListUpdater::AddCustomCategory.
constexpr base::TimeDelta kTimeOutForAddCustomCategory =
    base::TimeDelta::FromMilliseconds(500);

// The maximum allowed time for JumpListUpdater::CommitUpdate.
constexpr base::TimeDelta kTimeOutForJumplistCommitUpdate =
    base::TimeDelta::FromMilliseconds(1000);

// Appends the common switches to each shell link.
void AppendCommonSwitches(ShellLinkItem* shell_link) {
  const char* kSwitchNames[] = { switches::kUserDataDir };
  const base::CommandLine& command_line =
      *base::CommandLine::ForCurrentProcess();
  shell_link->GetCommandLine()->CopySwitchesFrom(command_line,
                                                 kSwitchNames,
                                                 arraysize(kSwitchNames));
}

// Creates a ShellLinkItem preloaded with common switches.
scoped_refptr<ShellLinkItem> CreateShellLink() {
  scoped_refptr<ShellLinkItem> link(new ShellLinkItem);
  AppendCommonSwitches(link.get());
  return link;
}

// Creates a temporary icon file to be shown in JumpList.
bool CreateIconFile(const gfx::ImageSkia& image_skia,
                    const base::FilePath& icon_dir,
                    base::FilePath* icon_path) {
  // Retrieve the path to a temporary file.
  // We don't have to care about the extension of this temporary file because
  // JumpList does not care about it.
  base::FilePath path;
  if (!base::CreateTemporaryFileInDir(icon_dir, &path))
    return false;

  // Create an icon file from the favicon attached to the given |page|, and
  // save it as the temporary file.
  gfx::ImageFamily image_family;
  if (!image_skia.isNull()) {
    std::vector<float> supported_scales = image_skia.GetSupportedScales();
    for (auto& scale : supported_scales) {
      gfx::ImageSkiaRep image_skia_rep = image_skia.GetRepresentation(scale);
      if (!image_skia_rep.is_null())
        image_family.Add(
            gfx::Image::CreateFrom1xBitmap(image_skia_rep.sk_bitmap()));
    }
  }

  if (!IconUtil::CreateIconFileFromImageFamily(image_family, path,
                                               IconUtil::NORMAL_WRITE)) {
    // Delete the file created by CreateTemporaryFileInDir as it won't be used.
    base::DeleteFile(path, false);
    return false;
  }

  // Add this icon file to the list and return its absolute path.
  // The IShellLink::SetIcon() function needs the absolute path to an icon.
  *icon_path = path;
  return true;
}

// Updates the "Tasks" category of the JumpList.
bool UpdateTaskCategory(
    JumpListUpdater* jumplist_updater,
    IncognitoModePrefs::Availability incognito_availability) {
  base::FilePath chrome_path;
  if (!PathService::Get(base::FILE_EXE, &chrome_path))
    return false;

  int icon_index = install_static::GetIconResourceIndex();

  ShellLinkItemList items;

  // Create an IShellLink object which launches Chrome, and add it to the
  // collection. We use our application icon as the icon for this item.
  // We remove '&' characters from this string so we can share it with our
  // system menu.
  if (incognito_availability != IncognitoModePrefs::FORCED) {
    scoped_refptr<ShellLinkItem> chrome = CreateShellLink();
    base::string16 chrome_title = l10n_util::GetStringUTF16(IDS_NEW_WINDOW);
    base::ReplaceSubstringsAfterOffset(
        &chrome_title, 0, L"&", base::StringPiece16());
    chrome->set_title(chrome_title);
    chrome->set_icon(chrome_path.value(), icon_index);
    items.push_back(chrome);
  }

  // Create an IShellLink object which launches Chrome in incognito mode, and
  // add it to the collection.
  if (incognito_availability != IncognitoModePrefs::DISABLED) {
    scoped_refptr<ShellLinkItem> incognito = CreateShellLink();
    incognito->GetCommandLine()->AppendSwitch(switches::kIncognito);
    base::string16 incognito_title =
        l10n_util::GetStringUTF16(IDS_NEW_INCOGNITO_WINDOW);
    base::ReplaceSubstringsAfterOffset(
        &incognito_title, 0, L"&", base::StringPiece16());
    incognito->set_title(incognito_title);
    incognito->set_icon(chrome_path.value(), icon_resources::kIncognitoIndex);
    items.push_back(incognito);
  }

  return jumplist_updater->AddTasks(items);
}

// Returns the full path of the JumpListIcons[|suffix|] directory in
// |profile_dir|.
base::FilePath GenerateJumplistIconDirName(
    const base::FilePath& profile_dir,
    const base::FilePath::StringPieceType& suffix) {
  base::FilePath::StringType dir_name(chrome::kJumpListIconDirname);
  suffix.AppendToString(&dir_name);
  return profile_dir.Append(dir_name);
}

}  // namespace

JumpList::JumpListData::JumpListData() {}

JumpList::JumpListData::~JumpListData() {}

JumpList::JumpList(Profile* profile)
    : RefcountedKeyedService(content::BrowserThread::GetTaskRunnerForThread(
          content::BrowserThread::UI)),
      profile_(profile),
      jumplist_data_(new base::RefCountedData<JumpListData>),
      task_id_(base::CancelableTaskTracker::kBadTaskId),
      update_jumplist_task_runner_(base::CreateCOMSTATaskRunnerWithTraits(
          {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
           base::TaskShutdownBehavior::CONTINUE_ON_SHUTDOWN})),
      delete_jumplisticons_task_runner_(
          base::CreateSequencedTaskRunnerWithTraits(
              {base::MayBlock(), base::TaskPriority::BACKGROUND,
               base::TaskShutdownBehavior::CONTINUE_ON_SHUTDOWN})),
      weak_ptr_factory_(this) {
  DCHECK(Enabled());
  // To update JumpList when a tab is added or removed, we add this object to
  // the observer list of the TabRestoreService class.
  // When we add this object to the observer list, we save the pointer to this
  // TabRestoreService object. This pointer is used when we remove this object
  // from the observer list.
  sessions::TabRestoreService* tab_restore_service =
      TabRestoreServiceFactory::GetForProfile(profile_);
  if (!tab_restore_service)
    return;

  app_id_ =
      shell_integration::win::GetChromiumModelIdForProfile(profile_->GetPath());

  scoped_refptr<history::TopSites> top_sites =
      TopSitesFactory::GetForProfile(profile_);
  if (top_sites) {
    // TopSites updates itself after a delay. This is especially noticable when
    // your profile is empty. Ask TopSites to update itself when jumplist is
    // initialized.
    top_sites->SyncWithHistory();
    // Register as TopSitesObserver so that we can update ourselves when the
    // TopSites changes.
    top_sites->AddObserver(this);
  }
  tab_restore_service->AddObserver(this);
  pref_change_registrar_.reset(new PrefChangeRegistrar);
  pref_change_registrar_->Init(profile_->GetPrefs());
  pref_change_registrar_->Add(
      prefs::kIncognitoModeAvailability,
      base::Bind(&JumpList::OnIncognitoAvailabilityChanged, this));
}

JumpList::~JumpList() {
  DCHECK(CalledOnValidThread());
  Terminate();
}

// static
bool JumpList::Enabled() {
  return JumpListUpdater::IsEnabled();
}

void JumpList::CancelPendingUpdate() {
  DCHECK(CalledOnValidThread());
  if (task_id_ != base::CancelableTaskTracker::kBadTaskId) {
    cancelable_task_tracker_.TryCancel(task_id_);
    task_id_ = base::CancelableTaskTracker::kBadTaskId;
  }
}

void JumpList::Terminate() {
  DCHECK(CalledOnValidThread());
  timer_most_visited_.Stop();
  timer_recently_closed_.Stop();
  CancelPendingUpdate();
  if (profile_) {
    sessions::TabRestoreService* tab_restore_service =
        TabRestoreServiceFactory::GetForProfile(profile_);
    if (tab_restore_service)
      tab_restore_service->RemoveObserver(this);
    scoped_refptr<history::TopSites> top_sites =
        TopSitesFactory::GetForProfile(profile_);
    if (top_sites)
      top_sites->RemoveObserver(this);
    pref_change_registrar_.reset();
  }
  profile_ = NULL;
}

void JumpList::ShutdownOnUIThread() {
  DCHECK(CalledOnValidThread());
  Terminate();
}

void JumpList::OnMostVisitedURLsAvailable(
    const history::MostVisitedURLList& urls) {
  DCHECK(CalledOnValidThread());

  // At most 9 JumpList items can be displayed for the "Most Visited"
  // category.
  const int kMostVistedCount = 9;
  {
    JumpListData* data = &jumplist_data_->data;
    base::AutoLock auto_lock(data->list_lock_);
    data->most_visited_pages_.clear();

    for (size_t i = 0; i < urls.size() && i < kMostVistedCount; i++) {
      const history::MostVisitedURL& url = urls[i];
      scoped_refptr<ShellLinkItem> link = CreateShellLink();
      std::string url_string = url.url.spec();
      base::string16 url_string_wide = base::UTF8ToUTF16(url_string);
      link->GetCommandLine()->AppendArgNative(url_string_wide);
      link->GetCommandLine()->AppendSwitchASCII(
          switches::kWinJumplistAction, jumplist::kMostVisitedCategory);
      link->set_title(!url.title.empty() ? url.title : url_string_wide);
      link->set_url(url_string);
      data->most_visited_pages_.push_back(link);
      data->icon_urls_.push_back(std::make_pair(url_string, link));
    }
    data->most_visited_pages_have_updates_ = true;
  }

  // Send a query that retrieves the first favicon.
  StartLoadingFavicon();
}

void JumpList::TabRestoreServiceChanged(sessions::TabRestoreService* service) {
  DCHECK(CalledOnValidThread());

  // if we have a pending favicon request, cancel it here (it is out of date).
  CancelPendingUpdate();

  // Initialize the one-shot timer to update the the "Recently Closed" category
  // in a while. If there is already a request queued then cancel it and post
  // the new request. This ensures that JumpList update of the "Recently Closed"
  // category won't happen until there has been a brief quiet period, thus
  // avoiding update storms.
  if (timer_recently_closed_.IsRunning()) {
    timer_recently_closed_.Reset();
  } else {
    timer_recently_closed_.Start(
        FROM_HERE, kDelayForJumplistUpdate,
        base::Bind(&JumpList::DeferredTabRestoreServiceChanged,
                   base::Unretained(this)));
  }
}

void JumpList::TabRestoreServiceDestroyed(
    sessions::TabRestoreService* service) {}

bool JumpList::AddTab(const sessions::TabRestoreService::Tab& tab,
                      size_t max_items,
                      JumpListData* data) {
  DCHECK(CalledOnValidThread());
  data->list_lock_.AssertAcquired();

  // This code adds the URL and the title strings of the given tab to |data|.
  if (data->recently_closed_pages_.size() >= max_items)
    return false;

  scoped_refptr<ShellLinkItem> link = CreateShellLink();
  const sessions::SerializedNavigationEntry& current_navigation =
      tab.navigations.at(tab.current_navigation_index);
  std::string url = current_navigation.virtual_url().spec();
  link->GetCommandLine()->AppendArgNative(base::UTF8ToUTF16(url));
  link->GetCommandLine()->AppendSwitchASCII(switches::kWinJumplistAction,
                                            jumplist::kRecentlyClosedCategory);
  link->set_title(current_navigation.title());
  link->set_url(url);
  data->recently_closed_pages_.push_back(link);
  data->icon_urls_.push_back(std::make_pair(std::move(url), std::move(link)));

  return true;
}

void JumpList::AddWindow(const sessions::TabRestoreService::Window& window,
                         size_t max_items,
                         JumpListData* data) {
  DCHECK(CalledOnValidThread());
  data->list_lock_.AssertAcquired();

  // This code enumerates all the tabs in the given window object and add their
  // URLs and titles to |data|.
  DCHECK(!window.tabs.empty());

  for (const auto& tab : window.tabs) {
    if (!AddTab(*tab, max_items, data))
      return;
  }
}

void JumpList::StartLoadingFavicon() {
  DCHECK(CalledOnValidThread());

  base::ElapsedTimer timer;

  GURL url;
  bool waiting_for_icons = true;
  {
    JumpListData* data = &jumplist_data_->data;
    base::AutoLock auto_lock(data->list_lock_);
    waiting_for_icons = !data->icon_urls_.empty();
    if (waiting_for_icons) {
      // Ask FaviconService if it has a favicon of a URL.
      // When FaviconService has one, it will call OnFaviconDataAvailable().
      url = GURL(data->icon_urls_.front().first);
    }
  }

  if (!waiting_for_icons) {
    // No more favicons are needed by the application JumpList. Schedule a
    // RunUpdateJumpList call.
    PostRunUpdate();
    return;
  }

  favicon::FaviconService* favicon_service =
      FaviconServiceFactory::GetForProfile(profile_,
                                           ServiceAccessType::EXPLICIT_ACCESS);
  task_id_ = favicon_service->GetFaviconImageForPageURL(
      url,
      base::Bind(&JumpList::OnFaviconDataAvailable, base::Unretained(this)),
      &cancelable_task_tracker_);

  // TODO(chengx): Remove the UMA histogram after fixing http://crbug.com/717236
  UMA_HISTOGRAM_TIMES("WinJumplist.StartLoadingFaviconDuration",
                      timer.Elapsed());
}

void JumpList::OnFaviconDataAvailable(
    const favicon_base::FaviconImageResult& image_result) {
  DCHECK(CalledOnValidThread());

  base::ElapsedTimer timer;

  // If there is currently a favicon request in progress, it is now outdated,
  // as we have received another, so nullify the handle from the old request.
  task_id_ = base::CancelableTaskTracker::kBadTaskId;
  // Lock the list to set icon data and pop the url.
  {
    JumpListData* data = &jumplist_data_->data;
    base::AutoLock auto_lock(data->list_lock_);
    // Attach the received data to the ShellLinkItem object.
    // This data will be decoded by the RunUpdateJumpList
    // method.
    if (!image_result.image.IsEmpty() && !data->icon_urls_.empty() &&
        data->icon_urls_.front().second.get()) {
      gfx::ImageSkia image_skia = image_result.image.AsImageSkia();
      image_skia.EnsureRepsForSupportedScales();
      std::unique_ptr<gfx::ImageSkia> deep_copy(image_skia.DeepCopy());
      data->icon_urls_.front().second->set_icon_image(*deep_copy);
    }

    if (!data->icon_urls_.empty())
      data->icon_urls_.pop_front();
  }

  // TODO(chengx): Remove the UMA histogram after fixing http://crbug.com/717236
  UMA_HISTOGRAM_TIMES("WinJumplist.OnFaviconDataAvailableDuration",
                      timer.Elapsed());

  // Check whether we need to load more favicons.
  StartLoadingFavicon();
}

void JumpList::OnIncognitoAvailabilityChanged() {
  DCHECK(CalledOnValidThread());

  bool waiting_for_icons = true;
  {
    JumpListData* data = &jumplist_data_->data;
    base::AutoLock auto_lock(data->list_lock_);
    waiting_for_icons = !data->icon_urls_.empty();
  }

  // Since neither the "Most Visited" category nor the "Recently Closed"
  // category changes, mark the flags so that icon files for those categories
  // won't be updated later on.
  if (!waiting_for_icons)
    PostRunUpdate();
}

void JumpList::PostRunUpdate() {
  DCHECK(CalledOnValidThread());

  TRACE_EVENT0("browser", "JumpList::PostRunUpdate");
  if (!profile_)
    return;

  base::FilePath profile_dir = profile_->GetPath();

  // Check if incognito windows (or normal windows) are disabled by policy.
  IncognitoModePrefs::Availability incognito_availability =
      IncognitoModePrefs::GetAvailability(profile_->GetPrefs());

  // Post a task to update the JumpList, which consists of 1) delete old icons,
  // 2) create new icons, 3) notify the OS.
  update_jumplist_task_runner_->PostTask(
      FROM_HERE,
      base::Bind(&JumpList::RunUpdateJumpList, this, incognito_availability,
                 app_id_, profile_dir, base::RetainedRef(jumplist_data_)));

  // Post a task to delete JumpListIcons folder as it's no longer needed.
  // Now we have JumpListIconsMostVisited folder and JumpListIconsRecentClosed
  // folder instead.
  base::FilePath icon_dir =
      GenerateJumplistIconDirName(profile_dir, FILE_PATH_LITERAL(""));

  delete_jumplisticons_task_runner_->PostTask(
      FROM_HERE,
      base::Bind(&DeleteDirectory, std::move(icon_dir), kFileDeleteLimit));

  // Post a task to delete JumpListIconsOld folder as it's no longer needed.
  base::FilePath icon_dir_old =
      GenerateJumplistIconDirName(profile_dir, FILE_PATH_LITERAL("Old"));

  delete_jumplisticons_task_runner_->PostTask(
      FROM_HERE,
      base::Bind(&DeleteDirectory, std::move(icon_dir_old), kFileDeleteLimit));
}

void JumpList::TopSitesLoaded(history::TopSites* top_sites) {
}

void JumpList::TopSitesChanged(history::TopSites* top_sites,
                               ChangeReason change_reason) {
  // If we have a pending favicon request, cancel it here (it is out of date).
  CancelPendingUpdate();

  // Initialize the one-shot timer to update the the "Most visited" category in
  // a while. If there is already a request queued then cancel it and post the
  // new request. This ensures that JumpList update of the "Most visited"
  // category won't happen until there has been a brief quiet period, thus
  // avoiding update storms.
  if (timer_most_visited_.IsRunning()) {
    timer_most_visited_.Reset();
  } else {
    timer_most_visited_.Start(
        FROM_HERE, kDelayForJumplistUpdate,
        base::Bind(&JumpList::DeferredTopSitesChanged, base::Unretained(this)));
  }
}

void JumpList::DeferredTopSitesChanged() {
  if (updates_to_skip_ > 0) {
    --updates_to_skip_;
    return;
  }

  scoped_refptr<history::TopSites> top_sites =
      TopSitesFactory::GetForProfile(profile_);
  if (top_sites) {
    top_sites->GetMostVisitedURLs(
        base::Bind(&JumpList::OnMostVisitedURLsAvailable,
                   weak_ptr_factory_.GetWeakPtr()),
        false);
  }
}

void JumpList::DeferredTabRestoreServiceChanged() {
  if (updates_to_skip_ > 0) {
    --updates_to_skip_;
    return;
  }

  // Create a list of ShellLinkItems from the "Recently Closed" pages.
  // As noted above, we create a ShellLinkItem objects with the following
  // parameters.
  // * arguments
  //   The last URL of the tab object.
  // * title
  //   The title of the last URL.
  // * icon
  //   An empty string. This value is to be updated in OnFaviconDataAvailable().
  const int kRecentlyClosedCount = 3;
  sessions::TabRestoreService* tab_restore_service =
      TabRestoreServiceFactory::GetForProfile(profile_);

  {
    JumpListData* data = &jumplist_data_->data;
    base::AutoLock auto_lock(data->list_lock_);
    data->recently_closed_pages_.clear();

    for (const auto& entry : tab_restore_service->entries()) {
      if (data->recently_closed_pages_.size() >= kRecentlyClosedCount)
        break;
      switch (entry->type) {
        case sessions::TabRestoreService::TAB:
          AddTab(static_cast<const sessions::TabRestoreService::Tab&>(*entry),
                 kRecentlyClosedCount, data);
          break;
        case sessions::TabRestoreService::WINDOW:
          AddWindow(
              static_cast<const sessions::TabRestoreService::Window&>(*entry),
              kRecentlyClosedCount, data);
          break;
      }
    }

    data->recently_closed_pages_have_updates_ = true;
  }

  // Send a query that retrieves the first favicon.
  StartLoadingFavicon();
}

void JumpList::DeleteIconFiles(const base::FilePath& icon_dir,
                               JumpListCategory category) {
  base::flat_map<std::string, base::FilePath>* source_map = nullptr;
  switch (category) {
    case JumpListCategory::kMostVisited:
      source_map = &most_visited_icons_;
      break;
    case JumpListCategory::kRecentlyClosed:
      source_map = &recently_closed_icons_;
      break;
  }

  // Put all cached icon file paths into a set.
  base::flat_set<base::FilePath> cached_files;
  cached_files.reserve(source_map->size());

  for (const auto& url_path_pair : *source_map)
    cached_files.insert(url_path_pair.second);

  DeleteNonCachedFiles(icon_dir, cached_files);
}

void JumpList::CreateIconFiles(const base::FilePath& icon_dir,
                               const ShellLinkItemList& item_list,
                               size_t max_items,
                               JumpListCategory category) {
  // TODO(chengx): Remove the UMA histogram after fixing http://crbug.com/40407.
  SCOPED_UMA_HISTOGRAM_TIMER("WinJumplist.CreateIconFilesDuration");

  // Reuse icons for urls that were already present in the jumplist for this
  // category.

  base::flat_map<std::string, base::FilePath>* source_map = nullptr;
  switch (category) {
    case JumpListCategory::kMostVisited:
      source_map = &most_visited_icons_;
      break;
    case JumpListCategory::kRecentlyClosed:
      source_map = &recently_closed_icons_;
      break;
  }

  base::flat_map<std::string, base::FilePath> updated_map;

  for (ShellLinkItemList::const_iterator iter = item_list.begin();
       iter != item_list.end() && max_items > 0; ++iter, --max_items) {
    ShellLinkItem* item = iter->get();
    auto cache_iter = source_map->find(item->url());
    if (cache_iter != source_map->end()) {
      item->set_icon(cache_iter->second.value(), 0);
      updated_map[item->url()] = cache_iter->second;
    } else {
      base::FilePath icon_path;
      if (CreateIconFile(item->icon_image(), icon_dir, &icon_path)) {
        item->set_icon(icon_path.value(), 0);
        updated_map[item->url()] = icon_path;
      }
    }
  }
  source_map->swap(updated_map);
}

void JumpList::UpdateIconFiles(const base::FilePath& icon_dir,
                               const ShellLinkItemList& page_list,
                               size_t slot_limit,
                               JumpListCategory category) {
  // Maximum number of icon files that each JumpList icon folder may hold.
  size_t icon_limit = (category == JumpListCategory::kMostVisited) ? 10 : 6;

  // Clear the JumpList icon folder at |icon_dir| and the cache when
  // 1) "Most visited" category updates for the 1st time after Chrome is
  //     launched. This actually happens right after Chrome is launched.
  // 2) "Recently closed" category updates for the 1st time after Chrome is
  //     launched.
  // 3) The number of icons in |icon_dir| has exceeded the limit.
  if ((category == JumpListCategory::kMostVisited &&
       most_visited_icons_.empty()) ||
      (category == JumpListCategory::kRecentlyClosed &&
       recently_closed_icons_.empty()) ||
      FilesExceedLimitInDir(icon_dir, icon_limit)) {
    DeleteDirectoryContentAndLogRuntime(icon_dir, kFileDeleteLimit);
    most_visited_icons_.clear();
    recently_closed_icons_.clear();
    // Create new icons only when the directory exists and is empty.
    if (base::CreateDirectory(icon_dir) && base::IsDirectoryEmpty(icon_dir))
      CreateIconFiles(icon_dir, page_list, slot_limit, category);
  } else if (base::CreateDirectory(icon_dir)) {
    CreateIconFiles(icon_dir, page_list, slot_limit, category);
    DeleteIconFiles(icon_dir, category);
  }
}

bool JumpList::UpdateJumpList(
    const base::string16& app_id,
    const base::FilePath& profile_dir,
    const ShellLinkItemList& most_visited_pages,
    const ShellLinkItemList& recently_closed_pages,
    bool most_visited_pages_have_updates,
    bool recently_closed_pages_have_updates,
    IncognitoModePrefs::Availability incognito_availability) {
  if (!JumpListUpdater::IsEnabled())
    return true;

  JumpListUpdater jumplist_updater(app_id);

  base::ElapsedTimer begin_update_timer;

  if (!jumplist_updater.BeginUpdate())
    return false;

  // Discard this JumpList update if JumpListUpdater::BeginUpdate takes longer
  // than the maximum allowed time, as it's very likely the following update
  // steps will also take a long time. As we've not updated the icons on the
  // disk, discarding this update wont't affect the current JumpList used by OS.
  if (begin_update_timer.Elapsed() >= kTimeOutForJumplistBeginUpdate) {
    updates_to_skip_ = kUpdatesToSkipUnderHeavyLoad;
    return false;
  }

  // The default maximum number of items to display in JumpList is 10.
  // https://msdn.microsoft.com/library/windows/desktop/dd378398.aspx
  // The "Most visited" category title always takes 1 of the JumpList slots if
  // |most_visited_pages| isn't empty.
  // The "Recently closed" category title will also take 1 if
  // |recently_closed_pages| isn't empty.
  // For the remaining slots, we allocate 5/8 (i.e., 5 slots if both categories
  // present) to "most-visited" items and 3/8 (i.e., 3 slots if both categories
  // present) to "recently-closed" items, respectively.
  // Nevertheless, if there are not so many items in |recently_closed_pages|,
  // we give the remaining slots to "most-visited" items.

  const int kMostVisited = 50;
  const int kRecentlyClosed = 30;
  const int kTotal = kMostVisited + kRecentlyClosed;

  // Adjust the available jumplist slots to account for the category titles.
  size_t user_max_items_adjusted = jumplist_updater.user_max_items();
  if (!most_visited_pages.empty())
    --user_max_items_adjusted;
  if (!recently_closed_pages.empty())
    --user_max_items_adjusted;

  size_t most_visited_items =
      MulDiv(user_max_items_adjusted, kMostVisited, kTotal);
  size_t recently_closed_items = user_max_items_adjusted - most_visited_items;
  if (recently_closed_pages.size() < recently_closed_items) {
    most_visited_items += recently_closed_items - recently_closed_pages.size();
    recently_closed_items = recently_closed_pages.size();
  }

  // Record the desired number of icons to create in this JumpList update.
  int icons_to_create = 0;

  // Update the icons for "Most Visisted" category of the JumpList if needed.
  if (most_visited_pages_have_updates) {
    base::FilePath icon_dir_most_visited = GenerateJumplistIconDirName(
        profile_dir, FILE_PATH_LITERAL("MostVisited"));

    UpdateIconFiles(icon_dir_most_visited, most_visited_pages,
                    most_visited_items, JumpListCategory::kMostVisited);

    icons_to_create += std::min(most_visited_pages.size(), most_visited_items);
  }

  // Update the icons for "Recently Closed" category of the JumpList if needed.
  if (recently_closed_pages_have_updates) {
    base::FilePath icon_dir_recent_closed = GenerateJumplistIconDirName(
        profile_dir, FILE_PATH_LITERAL("RecentClosed"));

    UpdateIconFiles(icon_dir_recent_closed, recently_closed_pages,
                    recently_closed_items, JumpListCategory::kRecentlyClosed);

    icons_to_create +=
        std::min(recently_closed_pages.size(), recently_closed_items);
  }

  // TODO(chengx): Remove the UMA histogram after fixing http://crbug.com/40407.
  UMA_HISTOGRAM_COUNTS_100("WinJumplist.CreateIconFilesCount", icons_to_create);

  // TODO(chengx): Remove the UMA histogram after fixing http://crbug.com/40407.
  SCOPED_UMA_HISTOGRAM_TIMER("WinJumplist.UpdateJumpListDuration");

  base::ElapsedTimer add_custom_category_timer;

  // Update the "Most Visited" category of the JumpList if it exists.
  // This update request is applied into the JumpList when we commit this
  // transaction.
  if (!jumplist_updater.AddCustomCategory(
          l10n_util::GetStringUTF16(IDS_NEW_TAB_MOST_VISITED),
          most_visited_pages, most_visited_items)) {
    return false;
  }

  // Update the "Recently Closed" category of the JumpList.
  if (!jumplist_updater.AddCustomCategory(
          l10n_util::GetStringUTF16(IDS_RECENTLY_CLOSED), recently_closed_pages,
          recently_closed_items)) {
    return false;
  }

  // If JumpListUpdater::AddCustomCategory or JumpListUpdater::CommitUpdate
  // takes longer than the maximum allowed time, skip the next
  // |kUpdatesToSkipUnderHeavyLoad| updates. This update should be finished
  // because we've already updated the icons on the disk. If discarding this
  // update from here, some items in the current JumpList may not have icons
  // as they've been delete from the disk. In this case, the background color of
  // the JumpList panel is used instead, which doesn't look nice.

  if (add_custom_category_timer.Elapsed() >= kTimeOutForAddCustomCategory)
    updates_to_skip_ = kUpdatesToSkipUnderHeavyLoad;

  // Update the "Tasks" category of the JumpList.
  if (!UpdateTaskCategory(&jumplist_updater, incognito_availability))
    return false;

  base::ElapsedTimer commit_update_timer;

  // Commit this transaction and send the updated JumpList to Windows.
  bool commit_result = jumplist_updater.CommitUpdate();

  if (commit_update_timer.Elapsed() >= kTimeOutForJumplistCommitUpdate)
    updates_to_skip_ = kUpdatesToSkipUnderHeavyLoad;

  return commit_result;
}

void JumpList::RunUpdateJumpList(
    IncognitoModePrefs::Availability incognito_availability,
    const base::string16& app_id,
    const base::FilePath& profile_dir,
    base::RefCountedData<JumpListData>* ref_counted_data) {
  JumpListData* data = &ref_counted_data->data;
  ShellLinkItemList local_most_visited_pages;
  ShellLinkItemList local_recently_closed_pages;
  bool most_visited_pages_have_updates;
  bool recently_closed_pages_have_updates;

  {
    base::AutoLock auto_lock(data->list_lock_);
    // Make sure we are not out of date: if icon_urls_ is not empty, then
    // another notification has been received since we processed this one
    if (!data->icon_urls_.empty())
      return;

    // Make local copies of lists and flags so we can release the lock.
    local_most_visited_pages = data->most_visited_pages_;
    local_recently_closed_pages = data->recently_closed_pages_;

    most_visited_pages_have_updates = data->most_visited_pages_have_updates_;
    recently_closed_pages_have_updates =
        data->recently_closed_pages_have_updates_;

    // Clear the flags to reflect that we'll take actions on these updates.
    data->most_visited_pages_have_updates_ = false;
    data->recently_closed_pages_have_updates_ = false;
  }

  if (!most_visited_pages_have_updates && !recently_closed_pages_have_updates)
    return;

  // Update the application JumpList. If it fails, reset the flags to true if
  // they were so that the corresponding JumpList categories will be tried to
  // update again in the next run.
  if (!UpdateJumpList(
          app_id, profile_dir, local_most_visited_pages,
          local_recently_closed_pages, most_visited_pages_have_updates,
          recently_closed_pages_have_updates, incognito_availability)) {
    base::AutoLock auto_lock(data->list_lock_);
    if (most_visited_pages_have_updates)
      data->most_visited_pages_have_updates_ = true;
    if (recently_closed_pages_have_updates)
      data->recently_closed_pages_have_updates_ = true;
  }
}
