// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ios/chrome/browser/browser_state/chrome_browser_state_impl.h"

#include <utility>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/logging.h"
#include "base/sequenced_task_runner.h"
#include "base/synchronization/waitable_event.h"
#include "components/bookmarks/browser/bookmark_model.h"
#include "components/keyed_service/ios/browser_state_dependency_manager.h"
#include "components/pref_registry/pref_registry_syncable.h"
#include "components/prefs/json_pref_store.h"
#include "components/prefs/pref_service.h"
#include "components/proxy_config/ios/proxy_service_factory.h"
#include "components/proxy_config/pref_proxy_config_tracker.h"
#include "components/sync_preferences/pref_service_syncable.h"
#include "components/user_prefs/user_prefs.h"
#include "ios/chrome/browser/application_context.h"
#include "ios/chrome/browser/bookmarks/bookmark_model_factory.h"
#include "ios/chrome/browser/browser_state/bookmark_model_loaded_observer.h"
#include "ios/chrome/browser/browser_state/off_the_record_chrome_browser_state_impl.h"
#include "ios/chrome/browser/chrome_constants.h"
#include "ios/chrome/browser/chrome_paths_internal.h"
#include "ios/chrome/browser/file_metadata_util.h"
#include "ios/chrome/browser/net/ios_chrome_url_request_context_getter.h"
#include "ios/chrome/browser/pref_names.h"
#include "ios/chrome/browser/prefs/browser_prefs.h"
#include "ios/chrome/browser/prefs/ios_chrome_pref_service_factory.h"
#include "ios/web/public/web_thread.h"

namespace {

// Returns a bool indicating whether the necessary directories were able to be
// created (or already existed).
bool EnsureBrowserStateDirectoriesCreated(const base::FilePath& path,
                                          const base::FilePath& otr_path) {
  if (!base::PathExists(path) && !base::CreateDirectory(path))
    return false;
  // Create the directory for the OTR stash state now, even though it won't
  // necessarily be needed: the OTR browser state itself is created
  // synchronously on an as-needed basis on the UI thread, so creation of its
  // stash state directory cannot easily be done at that point.
  if (base::PathExists(otr_path))
    return true;
  if (!base::CreateDirectory(otr_path))
    return false;
  SetSkipSystemBackupAttributeToItem(otr_path, true);
  return true;
}

// Task that creates the directory and signal the FILE thread to resume
// execution.
void CreateDirectoryAndSignal(const base::FilePath& path,
                              base::WaitableEvent* done_creating) {
  bool success = base::CreateDirectory(path);
  DCHECK(success);
  done_creating->Signal();
}

// Task that blocks the FILE thread until CreateDirectoryAndSignal() finishes on
// blocking I/O pool.
void BlockFileThreadOnDirectoryCreate(base::WaitableEvent* done_creating) {
  done_creating->Wait();
}

// Initiates creation of browser state directory on |sequenced_task_runner| and
// ensures that FILE thread is blocked until that operation finishes.
void CreateBrowserStateDirectory(
    base::SequencedTaskRunner* sequenced_task_runner,
    const base::FilePath& path) {
  base::WaitableEvent* done_creating =
      new base::WaitableEvent(base::WaitableEvent::ResetPolicy::AUTOMATIC,
                              base::WaitableEvent::InitialState::NOT_SIGNALED);
  sequenced_task_runner->PostTask(
      FROM_HERE, base::Bind(&CreateDirectoryAndSignal, path, done_creating));
  // Block the FILE thread until directory is created on I/O pool to make sure
  // that we don't attempt any operation until that part completes.
  web::WebThread::PostTask(web::WebThread::FILE, FROM_HERE,
                           base::Bind(&BlockFileThreadOnDirectoryCreate,
                                      base::Owned(done_creating)));
}

base::FilePath GetCachePath(const base::FilePath& base) {
  return base.Append(kIOSChromeCacheDirname);
}

}  // namespace

ChromeBrowserStateImpl::ChromeBrowserStateImpl(const base::FilePath& path)
    : state_path_(path),
      pref_registry_(new user_prefs::PrefRegistrySyncable),
      io_data_(new ChromeBrowserStateImplIOData::Handle(this)) {
  otr_state_path_ = state_path_.Append(FILE_PATH_LITERAL("OTR"));

  bool directories_created =
      EnsureBrowserStateDirectoriesCreated(state_path_, otr_state_path_);
  DCHECK(directories_created);

  RegisterBrowserStatePrefs(pref_registry_.get());
  BrowserStateDependencyManager::GetInstance()
      ->RegisterBrowserStatePrefsForServices(this, pref_registry_.get());

  scoped_refptr<base::SequencedTaskRunner> sequenced_task_runner =
      JsonPrefStore::GetTaskRunnerForFile(state_path_,
                                          web::WebThread::GetBlockingPool());
  prefs_ = CreateBrowserStatePrefs(state_path_, sequenced_task_runner.get(),
                                   pref_registry_);
  // Register on BrowserState.
  user_prefs::UserPrefs::Set(this, prefs_.get());

  // Migrate obsolete prefs.
  PrefService* local_state = GetApplicationContext()->GetLocalState();
  MigrateObsoleteLocalStatePrefs(local_state);
  MigrateObsoleteBrowserStatePrefs(prefs_.get());

  BrowserStateDependencyManager::GetInstance()->CreateBrowserStateServices(
      this);

  // It would be nice to use PathService for fetching this directory, but
  // the cache directory depends on the browser state stash directory, which
  // isn't available to PathService.
  base::FilePath base_cache_path;
  ios::GetUserCacheDirectory(state_path_, &base_cache_path);
  // Always create the cache directory asynchronously.
  scoped_refptr<base::SequencedTaskRunner> cache_sequenced_task_runner =
      JsonPrefStore::GetTaskRunnerForFile(base_cache_path,
                                          web::WebThread::GetBlockingPool());
  CreateBrowserStateDirectory(cache_sequenced_task_runner.get(),
                              base_cache_path);

  ssl_config_service_manager_.reset(
      ssl_config::SSLConfigServiceManager::CreateDefaultManager(
          local_state,
          web::WebThread::GetTaskRunnerForThread(web::WebThread::IO)));

  base::FilePath cookie_path = state_path_.Append(kIOSChromeCookieFilename);
  base::FilePath channel_id_path =
      state_path_.Append(kIOSChromeChannelIDFilename);
  base::FilePath cache_path = GetCachePath(base_cache_path);
  int cache_max_size = 0;

  // Make sure we initialize the io_data_ after everything else has been
  // initialized that we might be reading from the IO thread.
  io_data_->Init(cookie_path, channel_id_path, cache_path, cache_max_size,
                 state_path_);

  // Listen for bookmark model load, to bootstrap the sync service.
  bookmarks::BookmarkModel* model =
      ios::BookmarkModelFactory::GetForBrowserState(this);
  model->AddObserver(new BookmarkModelLoadedObserver(this));
}

ChromeBrowserStateImpl::~ChromeBrowserStateImpl() {
  BrowserStateDependencyManager::GetInstance()->DestroyBrowserStateServices(
      this);
  if (pref_proxy_config_tracker_)
    pref_proxy_config_tracker_->DetachFromPrefService();
  DestroyOffTheRecordChromeBrowserState();
}

ios::ChromeBrowserState*
ChromeBrowserStateImpl::GetOriginalChromeBrowserState() {
  return this;
}

ios::ChromeBrowserState*
ChromeBrowserStateImpl::GetOffTheRecordChromeBrowserState() {
  if (!otr_state_) {
    otr_state_.reset(
        new OffTheRecordChromeBrowserStateImpl(this, otr_state_path_));
  }

  return otr_state_.get();
}

bool ChromeBrowserStateImpl::HasOffTheRecordChromeBrowserState() const {
  return !!otr_state_;
}

void ChromeBrowserStateImpl::DestroyOffTheRecordChromeBrowserState() {
  // Tear down both the OTR ChromeBrowserState and the OTR Profile with which
  // it is associated.
  otr_state_.reset();
}

PrefService* ChromeBrowserStateImpl::GetPrefs() {
  DCHECK(prefs_);  // Should explicitly be initialized.
  return prefs_.get();
}

bool ChromeBrowserStateImpl::IsOffTheRecord() const {
  return false;
}

base::FilePath ChromeBrowserStateImpl::GetStatePath() const {
  return state_path_;
}

void ChromeBrowserStateImpl::SetOffTheRecordChromeBrowserState(
    std::unique_ptr<ios::ChromeBrowserState> otr_state) {
  DCHECK(!otr_state_);
  otr_state_ = std::move(otr_state);
}

PrefService* ChromeBrowserStateImpl::GetOffTheRecordPrefs() {
  DCHECK(prefs_);
  if (!otr_prefs_) {
    otr_prefs_ = CreateIncognitoBrowserStatePrefs(prefs_.get());
  }
  return otr_prefs_.get();
}

ChromeBrowserStateIOData* ChromeBrowserStateImpl::GetIOData() {
  return io_data_->io_data();
}

net::URLRequestContextGetter* ChromeBrowserStateImpl::CreateRequestContext(
    ProtocolHandlerMap* protocol_handlers) {
  ApplicationContext* application_context = GetApplicationContext();
  return io_data_
      ->CreateMainRequestContextGetter(
          protocol_handlers, application_context->GetLocalState(),
          application_context->GetIOSChromeIOThread())
      .get();
}

net::URLRequestContextGetter*
ChromeBrowserStateImpl::CreateIsolatedRequestContext(
    const base::FilePath& partition_path) {
  return io_data_->CreateIsolatedAppRequestContextGetter(partition_path).get();
}

void ChromeBrowserStateImpl::ClearNetworkingHistorySince(
    base::Time time,
    const base::Closure& completion) {
  io_data_->ClearNetworkingHistorySince(time, completion);
}

PrefProxyConfigTracker* ChromeBrowserStateImpl::GetProxyConfigTracker() {
  if (!pref_proxy_config_tracker_) {
    pref_proxy_config_tracker_ =
        ProxyServiceFactory::CreatePrefProxyConfigTrackerOfProfile(
            GetPrefs(), GetApplicationContext()->GetLocalState());
  }
  return pref_proxy_config_tracker_.get();
}

net::SSLConfigService* ChromeBrowserStateImpl::GetSSLConfigService() {
  // If ssl_config_service_manager_ is null, this typically means that some
  // KeyedService is trying to create a RequestContext at startup,
  // but SSLConfigServiceManager is not initialized until DoFinalInit() which is
  // invoked after all KeyedServices have been initialized (see
  // http://crbug.com/171406).
  DCHECK(ssl_config_service_manager_)
      << "SSLConfigServiceManager is not initialized yet";
  return ssl_config_service_manager_->Get();
}
