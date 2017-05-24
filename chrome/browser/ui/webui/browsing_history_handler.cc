// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/browsing_history_handler.h"

#include <stddef.h>

#include <set>
#include <utility>

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/i18n/rtl.h"
#include "base/i18n/time_formatting.h"
#include "base/logging.h"
#include "base/strings/string16.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/default_clock.h"
#include "base/time/time.h"
#include "base/values.h"
#include "chrome/browser/bookmarks/bookmark_model_factory.h"
#include "chrome/browser/favicon/large_icon_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sync/profile_sync_service_factory.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/chrome_pages.h"
#include "chrome/browser/ui/webui/favicon_source.h"
#include "chrome/browser/ui/webui/large_icon_source.h"
#include "chrome/common/features.h"
#include "components/bookmarks/browser/bookmark_model.h"
#include "components/bookmarks/browser/bookmark_utils.h"
#include "components/browser_sync/profile_sync_service.h"
#include "components/favicon/core/fallback_url_util.h"
#include "components/favicon/core/large_icon_service.h"
#include "components/query_parser/snippet.h"
#include "components/strings/grit/components_strings.h"
#include "components/sync/device_info/device_info.h"
#include "components/sync/device_info/device_info_tracker.h"
#include "components/url_formatter/url_formatter.h"
#include "content/public/browser/url_data_source.h"
#include "content/public/browser/web_ui.h"
#include "extensions/features/features.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/l10n/time_format.h"

#if BUILDFLAG(ENABLE_SUPERVISED_USERS)
#include "chrome/browser/supervised_user/supervised_user_navigation_observer.h"
#include "chrome/browser/supervised_user/supervised_user_service.h"
#include "chrome/browser/supervised_user/supervised_user_service_factory.h"
#include "chrome/browser/supervised_user/supervised_user_url_filter.h"
#endif

#if defined(OS_ANDROID)
#include "chrome/browser/android/preferences/preferences_launcher.h"
#else
#include "chrome/common/chrome_features.h"
#endif

using bookmarks::BookmarkModel;

namespace {

// Identifiers for the type of device from which a history entry originated.
static const char kDeviceTypeLaptop[] = "laptop";
static const char kDeviceTypePhone[] = "phone";
static const char kDeviceTypeTablet[] = "tablet";

// Returns a localized version of |visit_time| including a relative
// indicator (e.g. today, yesterday).
base::string16 GetRelativeDateLocalized(base::Clock* clock,
                                        const base::Time& visit_time) {
  base::Time midnight = clock->Now().LocalMidnight();
  base::string16 date_str = ui::TimeFormat::RelativeDate(visit_time, &midnight);
  if (date_str.empty()) {
    date_str = base::TimeFormatFriendlyDate(visit_time);
  } else {
    date_str = l10n_util::GetStringFUTF16(
        IDS_HISTORY_DATE_WITH_RELATIVE_TIME,
        date_str,
        base::TimeFormatFriendlyDate(visit_time));
  }
  return date_str;
}

// Sets the correct year when substracting months from a date.
void NormalizeMonths(base::Time::Exploded* exploded) {
  // Decrease a year at a time until we have a proper date.
  while (exploded->month < 1) {
    exploded->month += 12;
    exploded->year--;
  }
}

// Gets the name and type of a device for the given sync client ID.
// |name| and |type| are out parameters.
void GetDeviceNameAndType(const browser_sync::ProfileSyncService* sync_service,
                          const std::string& client_id,
                          std::string* name,
                          std::string* type) {
  // DeviceInfoTracker must be syncing in order for remote history entries to
  // be available.
  DCHECK(sync_service);
  DCHECK(sync_service->GetDeviceInfoTracker());
  DCHECK(sync_service->GetDeviceInfoTracker()->IsSyncing());

  std::unique_ptr<syncer::DeviceInfo> device_info =
      sync_service->GetDeviceInfoTracker()->GetDeviceInfo(client_id);
  if (device_info.get()) {
    *name = device_info->client_name();
    switch (device_info->device_type()) {
      case sync_pb::SyncEnums::TYPE_PHONE:
        *type = kDeviceTypePhone;
        break;
      case sync_pb::SyncEnums::TYPE_TABLET:
        *type = kDeviceTypeTablet;
        break;
      default:
        *type = kDeviceTypeLaptop;
    }
    return;
  }

  *name = l10n_util::GetStringUTF8(IDS_HISTORY_UNKNOWN_DEVICE);
  *type = kDeviceTypeLaptop;
}

// Formats |entry|'s URL and title and adds them to |result|.
void SetHistoryEntryUrlAndTitle(BrowsingHistoryService::HistoryEntry* entry,
                                base::DictionaryValue* result) {
  result->SetString("url", entry->url.spec());

  bool using_url_as_the_title = false;
  base::string16 title_to_set(entry->title);
  if (entry->title.empty()) {
    using_url_as_the_title = true;
    title_to_set = base::UTF8ToUTF16(entry->url.spec());
  }

  // Since the title can contain BiDi text, we need to mark the text as either
  // RTL or LTR, depending on the characters in the string. If we use the URL
  // as the title, we mark the title as LTR since URLs are always treated as
  // left to right strings.
  if (base::i18n::IsRTL()) {
    if (using_url_as_the_title)
      base::i18n::WrapStringWithLTRFormatting(&title_to_set);
    else
      base::i18n::AdjustStringForLocaleDirection(&title_to_set);
  }

#if !defined(OS_ANDROID)
  // Number of chars to truncate titles when making them "short".
  static const size_t kShortTitleLength = 300;
  if (title_to_set.size() > kShortTitleLength)
    title_to_set.resize(kShortTitleLength);
#endif

  result->SetString("title", title_to_set);
}

// Converts |entry| to a DictionaryValue to be owned by the caller.
std::unique_ptr<base::DictionaryValue> HistoryEntryToValue(
    BrowsingHistoryService::HistoryEntry* entry,
    BookmarkModel* bookmark_model,
    SupervisedUserService* supervised_user_service,
    const browser_sync::ProfileSyncService* sync_service) {
  std::unique_ptr<base::DictionaryValue> result(new base::DictionaryValue());
  SetHistoryEntryUrlAndTitle(entry, result.get());

  base::string16 domain = url_formatter::IDNToUnicode(entry->url.host());
  // When the domain is empty, use the scheme instead. This allows for a
  // sensible treatment of e.g. file: URLs when group by domain is on.
  if (domain.empty())
    domain = base::UTF8ToUTF16(entry->url.scheme() + ":");

  // The items which are to be written into result are also described in
  // chrome/browser/resources/history/history.js in @typedef for
  // HistoryEntry. Please update it whenever you add or remove
  // any keys in result.
  result->SetString("domain", domain);

  result->SetString(
      "fallbackFaviconText",
      base::UTF16ToASCII(favicon::GetFallbackIconText(entry->url)));

  result->SetDouble("time", entry->time.ToJsTime());

  // Pass the timestamps in a list.
  std::unique_ptr<base::ListValue> timestamps(new base::ListValue);
  for (std::set<int64_t>::const_iterator it = entry->all_timestamps.begin();
       it != entry->all_timestamps.end(); ++it) {
    timestamps->AppendDouble(base::Time::FromInternalValue(*it).ToJsTime());
  }
  result->Set("allTimestamps", std::move(timestamps));

  // Always pass the short date since it is needed both in the search and in
  // the monthly view.
  result->SetString("dateShort", base::TimeFormatShortDate(entry->time));

  base::string16 snippet_string;
  base::string16 date_relative_day;
  base::string16 date_time_of_day;
  bool is_blocked_visit = false;
  int host_filtering_behavior = -1;

  // Only pass in the strings we need (search results need a shortdate
  // and snippet, browse results need day and time information). Makes sure that
  // values of result are never undefined
  if (entry->is_search_result) {
    snippet_string = entry->snippet;
  } else {
    base::Time midnight = entry->clock->Now().LocalMidnight();
    base::string16 date_str = ui::TimeFormat::RelativeDate(entry->time,
                                                           &midnight);
    if (date_str.empty()) {
      date_str = base::TimeFormatFriendlyDate(entry->time);
    } else {
      date_str = l10n_util::GetStringFUTF16(
          IDS_HISTORY_DATE_WITH_RELATIVE_TIME,
          date_str,
          base::TimeFormatFriendlyDate(entry->time));
    }
    date_relative_day = date_str;
    date_time_of_day = base::TimeFormatTimeOfDay(entry->time);
  }

  std::string device_name;
  std::string device_type;
  if (!entry->client_id.empty())
    GetDeviceNameAndType(sync_service, entry->client_id, &device_name,
                         &device_type);
  result->SetString("deviceName", device_name);
  result->SetString("deviceType", device_type);

#if BUILDFLAG(ENABLE_SUPERVISED_USERS)
  if (supervised_user_service) {
    const SupervisedUserURLFilter* url_filter =
        supervised_user_service->GetURLFilter();
    int filtering_behavior =
        url_filter->GetFilteringBehaviorForURL(entry->url.GetWithEmptyPath());
    is_blocked_visit = entry->blocked_visit;
    host_filtering_behavior = filtering_behavior;
  }
#endif

  result->SetString("dateTimeOfDay", date_time_of_day);
  result->SetString("dateRelativeDay", date_relative_day);
  result->SetString("snippet", snippet_string);
  result->SetBoolean("starred", bookmark_model->IsBookmarked(entry->url));
  result->SetInteger("hostFilteringBehavior", host_filtering_behavior);
  result->SetBoolean("blockedVisit", is_blocked_visit);

  return result;
}

}  // namespace

BrowsingHistoryHandler::BrowsingHistoryHandler()
    : clock_(new base::DefaultClock()),
      browsing_history_service_(nullptr) {}

BrowsingHistoryHandler::~BrowsingHistoryHandler() {
}

void BrowsingHistoryHandler::RegisterMessages() {
  browsing_history_service_ = base::MakeUnique<BrowsingHistoryService>(
      Profile::FromWebUI(web_ui()), this);

  // Create our favicon data source.
  Profile* profile = Profile::FromWebUI(web_ui());

#if defined(OS_ANDROID)
  favicon::LargeIconService* large_icon_service =
      LargeIconServiceFactory::GetForBrowserContext(profile);
  content::URLDataSource::Add(profile, new LargeIconSource(large_icon_service));
#else
  content::URLDataSource::Add(profile, new FaviconSource(profile));
#endif

  web_ui()->RegisterMessageCallback("queryHistory",
      base::Bind(&BrowsingHistoryHandler::HandleQueryHistory,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback("removeVisits",
      base::Bind(&BrowsingHistoryHandler::HandleRemoveVisits,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback("clearBrowsingData",
      base::Bind(&BrowsingHistoryHandler::HandleClearBrowsingData,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback("removeBookmark",
      base::Bind(&BrowsingHistoryHandler::HandleRemoveBookmark,
                 base::Unretained(this)));
}

bool BrowsingHistoryHandler::ExtractIntegerValueAtIndex(
    const base::ListValue* value,
    int index,
    int* out_int) {
  double double_value;
  if (value->GetDouble(index, &double_value)) {
    *out_int = static_cast<int>(double_value);
    return true;
  }
  NOTREACHED();
  return false;
}

void BrowsingHistoryHandler::HandleQueryHistory(const base::ListValue* args) {
  history::QueryOptions options;

  // Parse the arguments from JavaScript. There are five required arguments:
  // - the text to search for (may be empty)
  // - the offset from which the search should start (in multiples of week or
  //   month, set by the next argument).
  // - the range (BrowsingHistoryHandler::Range) Enum value that sets the range
  //   of the query.
  // - the end time for the query. Only results older than this time will be
  //   returned.
  // - the maximum number of results to return (may be 0, meaning that there
  //   is no maximum).
  base::string16 search_text = ExtractStringValue(args);
  int offset;
  if (!args->GetInteger(1, &offset)) {
    NOTREACHED() << "Failed to convert argument 1. ";
    return;
  }
  int range;
  if (!args->GetInteger(2, &range)) {
    NOTREACHED() << "Failed to convert argument 2. ";
    return;
  }

  if (range == BrowsingHistoryHandler::MONTH)
    SetQueryTimeInMonths(offset, &options);
  else if (range == BrowsingHistoryHandler::WEEK)
    SetQueryTimeInWeeks(offset, &options);

  double end_time;
  if (!args->GetDouble(3, &end_time)) {
    NOTREACHED() << "Failed to convert argument 3. ";
    return;
  }
  if (end_time)
    options.end_time = base::Time::FromJsTime(end_time);

  if (!ExtractIntegerValueAtIndex(args, 4, &options.max_count)) {
    NOTREACHED() << "Failed to convert argument 4.";
    return;
  }

  options.duplicate_policy = history::QueryOptions::REMOVE_DUPLICATES_PER_DAY;
  browsing_history_service_->QueryHistory(search_text, options);
}

void BrowsingHistoryHandler::HandleRemoveVisits(const base::ListValue* args) {
  std::vector<std::unique_ptr<BrowsingHistoryService::HistoryEntry>>
      items_to_remove;
  items_to_remove.reserve(args->GetSize());
  for (base::ListValue::const_iterator it = args->begin();
       it != args->end(); ++it) {
    const base::DictionaryValue* deletion = NULL;
    base::string16 url;
    const base::ListValue* timestamps = NULL;

    // Each argument is a dictionary with properties "url" and "timestamps".
    if (!(it->GetAsDictionary(&deletion) && deletion->GetString("url", &url) &&
          deletion->GetList("timestamps", &timestamps))) {
      NOTREACHED() << "Unable to extract arguments";
      return;
    }
    DCHECK(timestamps->GetSize() > 0);
    std::unique_ptr<BrowsingHistoryService::HistoryEntry> entry(
            new BrowsingHistoryService::HistoryEntry());

    entry->url = GURL(url);

    double timestamp;
    for (base::ListValue::const_iterator ts_iterator = timestamps->begin();
         ts_iterator != timestamps->end(); ++ts_iterator) {
      if (!ts_iterator->GetAsDouble(&timestamp)) {
        NOTREACHED() << "Unable to extract visit timestamp.";
        continue;
      }

      base::Time visit_time = base::Time::FromJsTime(timestamp);
      entry->all_timestamps.insert(visit_time.ToInternalValue());
    }

    items_to_remove.push_back(std::move(entry));
  }

  browsing_history_service_->RemoveVisits(&items_to_remove);
  items_to_remove.clear();
}

void BrowsingHistoryHandler::HandleClearBrowsingData(
    const base::ListValue* args) {
#if defined(OS_ANDROID)
  chrome::android::PreferencesLauncher::OpenClearBrowsingData(
      web_ui()->GetWebContents());
#else
  // TODO(beng): This is an improper direct dependency on Browser. Route this
  // through some sort of delegate.
  Browser* browser = chrome::FindBrowserWithWebContents(
      web_ui()->GetWebContents());
  chrome::ShowClearBrowsingDataDialog(browser);
#endif
}

void BrowsingHistoryHandler::HandleRemoveBookmark(const base::ListValue* args) {
  base::string16 url = ExtractStringValue(args);
  Profile* profile = Profile::FromWebUI(web_ui());
  BookmarkModel* model = BookmarkModelFactory::GetForBrowserContext(profile);
  bookmarks::RemoveAllBookmarks(model, GURL(url));
}

void BrowsingHistoryHandler::SetQueryTimeInWeeks(
    int offset, history::QueryOptions* options) {
  // LocalMidnight returns the beginning of the current day so get the
  // beginning of the next one.
  base::Time midnight =
      clock_->Now().LocalMidnight() + base::TimeDelta::FromDays(1);
  options->end_time = midnight -
      base::TimeDelta::FromDays(7 * offset);
  options->begin_time = midnight -
      base::TimeDelta::FromDays(7 * (offset + 1));
}

void BrowsingHistoryHandler::SetQueryTimeInMonths(
    int offset, history::QueryOptions* options) {
  // Configure the begin point of the search to the start of the
  // current month.
  base::Time::Exploded exploded;
  clock_->Now().LocalMidnight().LocalExplode(&exploded);
  exploded.day_of_month = 1;

  if (offset == 0) {
    if (!base::Time::FromLocalExploded(exploded, &options->begin_time)) {
      // TODO(maksims): implement errors handling here.
      NOTIMPLEMENTED();
    }

    // Set the end time of this first search to null (which will
    // show results from the future, should the user's clock have
    // been set incorrectly).
    options->end_time = base::Time();
  } else {
    // Go back |offset| months in the past. The end time is not inclusive, so
    // use the first day of the |offset| - 1 and |offset| months (e.g. for
    // the last month, |offset| = 1, use the first days of the last month and
    // the current month.
    exploded.month -= offset - 1;
    // Set the correct year.
    NormalizeMonths(&exploded);
    if (!base::Time::FromLocalExploded(exploded, &options->end_time)) {
      // TODO(maksims): implement errors handling here.
      NOTIMPLEMENTED();
    }

    exploded.month -= 1;
    // Set the correct year
    NormalizeMonths(&exploded);
    if (!base::Time::FromLocalExploded(exploded, &options->begin_time)) {
      // TODO(maksims): implement errors handling here.
      NOTIMPLEMENTED();
    }
  }
}

void BrowsingHistoryHandler::OnQueryComplete(
    std::vector<BrowsingHistoryService::HistoryEntry>* results,
    BrowsingHistoryService::QueryResultsInfo* query_results_info) {
  Profile* profile = Profile::FromWebUI(web_ui());
  BookmarkModel* bookmark_model =
      BookmarkModelFactory::GetForBrowserContext(profile);
  SupervisedUserService* supervised_user_service = NULL;
#if defined(ENABLE_SUPERVISED_USERS)
  if (profile->IsSupervised())
    supervised_user_service =
        SupervisedUserServiceFactory::GetForProfile(profile);
#endif
  browser_sync::ProfileSyncService* sync_service =
      ProfileSyncServiceFactory::GetInstance()->GetForProfile(profile);

  // Convert the result vector into a ListValue.
  base::ListValue results_value;
  for (std::vector<BrowsingHistoryService::HistoryEntry>::iterator it =
           results->begin(); it != results->end(); ++it) {
    std::unique_ptr<base::Value> value(HistoryEntryToValue(
        &(*it), bookmark_model, supervised_user_service, sync_service));
    results_value.Append(std::move(value));
  }

  base::DictionaryValue results_info;
  // The items which are to be written into results_info_value_ are also
  // described in chrome/browser/resources/history/history.js in @typedef for
  // HistoryQuery. Please update it whenever you add or remove any keys in
  // results_info_value_.
  results_info.SetString("term", query_results_info->search_text);
  results_info.SetBoolean("finished", query_results_info->reached_beginning);
  results_info.SetBoolean("hasSyncedResults",
                          query_results_info->has_synced_results);

  // Add the specific dates that were searched to display them.
  // TODO(sergiu): Put today if the start is in the future.
  results_info.SetString(
      "queryStartTime",
      GetRelativeDateLocalized(clock_.get(), query_results_info->start_time));
  results_info.SetString(
      "queryEndTime",
      GetRelativeDateLocalized(clock_.get(), query_results_info->end_time));

// Not used in mobile UI, and cause ~16kb of code bloat (crbug/683386).
#ifndef OS_ANDROID
  // TODO(calamity): Clean up grouped-specific fields once grouped history is
  // removed.
  results_info.SetString(
      "queryStartMonth",
      base::TimeFormatMonthAndYear(query_results_info->start_time));
  results_info.SetString(
      "queryInterval",
      base::DateIntervalFormat(query_results_info->start_time,
                               query_results_info->end_time,
                               base::DATE_FORMAT_MONTH_WEEKDAY_DAY));
#endif

  web_ui()->CallJavascriptFunctionUnsafe("historyResult", results_info,
                                         results_value);
}

void BrowsingHistoryHandler::OnRemoveVisitsComplete() {
  web_ui()->CallJavascriptFunctionUnsafe("deleteComplete");
}

void BrowsingHistoryHandler::OnRemoveVisitsFailed() {
  web_ui()->CallJavascriptFunctionUnsafe("deleteFailed");
}

void BrowsingHistoryHandler::HistoryDeleted() {
  web_ui()->CallJavascriptFunctionUnsafe("historyDeleted");
}

void BrowsingHistoryHandler::HasOtherFormsOfBrowsingHistory(
    bool has_other_forms,
    bool has_synced_results) {
  web_ui()->CallJavascriptFunctionUnsafe("showNotification",
                                         base::Value(has_synced_results),
                                         base::Value(has_other_forms));
}
