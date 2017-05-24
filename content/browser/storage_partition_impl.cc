// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/storage_partition_impl.h"

#include <stddef.h>

#include <set>
#include <vector>

#include "base/bind.h"
#include "base/command_line.h"
#include "base/location.h"
#include "base/sequenced_task_runner.h"
#include "base/single_thread_task_runner.h"
#include "base/strings/utf_string_conversions.h"
#include "content/browser/browser_main_loop.h"
#include "content/browser/browsing_data/storage_partition_http_cache_data_remover.h"
#include "content/browser/fileapi/browser_file_system_helper.h"
#include "content/browser/gpu/shader_cache_factory.h"
#include "content/browser/host_zoom_map_impl.h"
#include "content/browser/notifications/platform_notification_context_impl.h"
#include "content/common/dom_storage/dom_storage_types.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/content_browser_client.h"
#include "content/public/browser/dom_storage_context.h"
#include "content/public/browser/indexed_db_context.h"
#include "content/public/browser/local_storage_usage_info.h"
#include "content/public/browser/session_storage_usage_info.h"
#include "content/public/common/content_client.h"
#include "content/public/common/content_switches.h"
#include "content/public/common/service_manager_connection.h"
#include "content/public/common/service_names.mojom.h"
#include "net/base/completion_callback.h"
#include "net/base/net_errors.h"
#include "net/cookies/canonical_cookie.h"
#include "net/cookies/cookie_monster.h"
#include "net/url_request/url_request_context.h"
#include "net/url_request/url_request_context_getter.h"
#include "ppapi/features/features.h"
#include "services/service_manager/public/cpp/connector.h"
#include "storage/browser/database/database_tracker.h"
#include "storage/browser/quota/quota_manager.h"

#if BUILDFLAG(ENABLE_PLUGINS)
#include "content/browser/plugin_private_storage_helper.h"
#endif  // BUILDFLAG(ENABLE_PLUGINS)

namespace content {

namespace {

bool DoesCookieMatchHost(const std::string& host,
                         const net::CanonicalCookie& cookie) {
  return cookie.IsHostCookie() && cookie.IsDomainMatch(host);
}

void OnClearedCookies(const base::Closure& callback, int num_deleted) {
  // The final callback needs to happen from UI thread.
  if (!BrowserThread::CurrentlyOn(BrowserThread::UI)) {
    BrowserThread::PostTask(
        BrowserThread::UI, FROM_HERE,
        base::Bind(&OnClearedCookies, callback, num_deleted));
    return;
  }

  callback.Run();
}

// Cookie matcher and storage_origin are never both populated.
void ClearCookiesOnIOThread(
    const scoped_refptr<net::URLRequestContextGetter>& rq_context,
    const base::Time begin,
    const base::Time end,
    const GURL& storage_origin,
    const StoragePartition::CookieMatcherFunction& cookie_matcher,
    const base::Closure& callback) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  DCHECK(cookie_matcher.is_null() || storage_origin.is_empty());
  net::CookieStore* cookie_store =
      rq_context->GetURLRequestContext()->cookie_store();
  if (!cookie_matcher.is_null()) {
    cookie_store->DeleteAllCreatedBetweenWithPredicateAsync(
        begin, end, cookie_matcher, base::Bind(&OnClearedCookies, callback));
    return;
  }
  if (!storage_origin.is_empty()) {
    // TODO(mkwst): It's not clear whether removing host cookies is the correct
    // behavior. We might want to remove all domain-matching cookies instead.
    // Also, this code path may be dead anyways.
    cookie_store->DeleteAllCreatedBetweenWithPredicateAsync(
        begin, end,
        StoragePartitionImpl::CreatePredicateForHostCookies(storage_origin),
        base::Bind(&OnClearedCookies, callback));
    return;
  }
  cookie_store->DeleteAllCreatedBetweenAsync(
      begin, end, base::Bind(&OnClearedCookies, callback));
}

void CheckQuotaManagedDataDeletionStatus(size_t* deletion_task_count,
                                         const base::Closure& callback) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  if (*deletion_task_count == 0) {
    delete deletion_task_count;
    callback.Run();
  }
}

void OnQuotaManagedOriginDeleted(const GURL& origin,
                                 storage::StorageType type,
                                 size_t* deletion_task_count,
                                 const base::Closure& callback,
                                 storage::QuotaStatusCode status) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  DCHECK_GT(*deletion_task_count, 0u);
  if (status != storage::kQuotaStatusOk) {
    DLOG(ERROR) << "Couldn't remove data of type " << type << " for origin "
                << origin << ". Status: " << status;
  }

  (*deletion_task_count)--;
  CheckQuotaManagedDataDeletionStatus(deletion_task_count, callback);
}

void ClearedShaderCache(const base::Closure& callback) {
  if (!BrowserThread::CurrentlyOn(BrowserThread::UI)) {
    BrowserThread::PostTask(
        BrowserThread::UI, FROM_HERE,
        base::Bind(&ClearedShaderCache, callback));
    return;
  }
  callback.Run();
}

void ClearShaderCacheOnIOThread(const base::FilePath& path,
                                const base::Time begin,
                                const base::Time end,
                                const base::Closure& callback) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  GetShaderCacheFactorySingleton()->ClearByPath(
      path, begin, end, base::Bind(&ClearedShaderCache, callback));
}

void OnLocalStorageUsageInfo(
    const scoped_refptr<DOMStorageContextWrapper>& dom_storage_context,
    const scoped_refptr<storage::SpecialStoragePolicy>& special_storage_policy,
    const StoragePartition::OriginMatcherFunction& origin_matcher,
    const base::Time delete_begin,
    const base::Time delete_end,
    const base::Closure& callback,
    const std::vector<LocalStorageUsageInfo>& infos) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  for (size_t i = 0; i < infos.size(); ++i) {
    if (!origin_matcher.is_null() &&
        !origin_matcher.Run(infos[i].origin, special_storage_policy.get())) {
      continue;
    }

    if (infos[i].last_modified >= delete_begin &&
        infos[i].last_modified <= delete_end) {
      dom_storage_context->DeleteLocalStorage(infos[i].origin);
    }
  }
  callback.Run();
}

void OnSessionStorageUsageInfo(
    const scoped_refptr<DOMStorageContextWrapper>& dom_storage_context,
    const scoped_refptr<storage::SpecialStoragePolicy>& special_storage_policy,
    const StoragePartition::OriginMatcherFunction& origin_matcher,
    const base::Closure& callback,
    const std::vector<SessionStorageUsageInfo>& infos) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  for (size_t i = 0; i < infos.size(); ++i) {
    if (!origin_matcher.is_null() &&
        !origin_matcher.Run(infos[i].origin, special_storage_policy.get())) {
      continue;
    }
    dom_storage_context->DeleteSessionStorage(infos[i]);
  }

  callback.Run();
}

void ClearLocalStorageOnUIThread(
    const scoped_refptr<DOMStorageContextWrapper>& dom_storage_context,
    const scoped_refptr<storage::SpecialStoragePolicy>& special_storage_policy,
    const StoragePartition::OriginMatcherFunction& origin_matcher,
    const GURL& storage_origin,
    const base::Time begin,
    const base::Time end,
    const base::Closure& callback) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  if (!storage_origin.is_empty()) {
    bool can_delete = origin_matcher.is_null() ||
                      origin_matcher.Run(storage_origin,
                                         special_storage_policy.get());
    if (can_delete)
      dom_storage_context->DeleteLocalStorageForPhysicalOrigin(storage_origin);

    callback.Run();
    return;
  }

  dom_storage_context->GetLocalStorageUsage(
      base::Bind(&OnLocalStorageUsageInfo,
                 dom_storage_context, special_storage_policy, origin_matcher,
                 begin, end, callback));
}

void ClearSessionStorageOnUIThread(
    const scoped_refptr<DOMStorageContextWrapper>& dom_storage_context,
    const scoped_refptr<storage::SpecialStoragePolicy>& special_storage_policy,
    const StoragePartition::OriginMatcherFunction& origin_matcher,
    const base::Closure& callback) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  dom_storage_context->GetSessionStorageUsage(
      base::Bind(&OnSessionStorageUsageInfo, dom_storage_context,
                 special_storage_policy, origin_matcher,
                 callback));
}

}  // namespace

// Static.
int StoragePartitionImpl::GenerateQuotaClientMask(uint32_t remove_mask) {
  int quota_client_mask = 0;

  if (remove_mask & StoragePartition::REMOVE_DATA_MASK_FILE_SYSTEMS)
    quota_client_mask |= storage::QuotaClient::kFileSystem;
  if (remove_mask & StoragePartition::REMOVE_DATA_MASK_WEBSQL)
    quota_client_mask |= storage::QuotaClient::kDatabase;
  if (remove_mask & StoragePartition::REMOVE_DATA_MASK_APPCACHE)
    quota_client_mask |= storage::QuotaClient::kAppcache;
  if (remove_mask & StoragePartition::REMOVE_DATA_MASK_INDEXEDDB)
    quota_client_mask |= storage::QuotaClient::kIndexedDatabase;
  if (remove_mask & StoragePartition::REMOVE_DATA_MASK_SERVICE_WORKERS)
    quota_client_mask |= storage::QuotaClient::kServiceWorker;
  if (remove_mask & StoragePartition::REMOVE_DATA_MASK_CACHE_STORAGE)
    quota_client_mask |= storage::QuotaClient::kServiceWorkerCache;

  return quota_client_mask;
}

// static
net::CookieStore::CookiePredicate
StoragePartitionImpl::CreatePredicateForHostCookies(const GURL& url) {
  return base::Bind(&DoesCookieMatchHost, url.host());
}

// Helper for deleting quota managed data from a partition.
//
// Most of the operations in this class are done on IO thread.
struct StoragePartitionImpl::QuotaManagedDataDeletionHelper {
  QuotaManagedDataDeletionHelper(uint32_t remove_mask,
                                 uint32_t quota_storage_remove_mask,
                                 const GURL& storage_origin,
                                 const base::Closure& callback)
      : remove_mask(remove_mask),
        quota_storage_remove_mask(quota_storage_remove_mask),
        storage_origin(storage_origin),
        callback(callback),
        task_count(0) {}

  void IncrementTaskCountOnIO();
  void DecrementTaskCountOnIO();

  void ClearDataOnIOThread(
      const scoped_refptr<storage::QuotaManager>& quota_manager,
      const base::Time begin,
      const scoped_refptr<storage::SpecialStoragePolicy>&
          special_storage_policy,
      const StoragePartition::OriginMatcherFunction& origin_matcher);

  void ClearOriginsOnIOThread(
      storage::QuotaManager* quota_manager,
      const scoped_refptr<storage::SpecialStoragePolicy>&
          special_storage_policy,
      const StoragePartition::OriginMatcherFunction& origin_matcher,
      const base::Closure& callback,
      const std::set<GURL>& origins,
      storage::StorageType quota_storage_type);

  // All of these data are accessed on IO thread.
  uint32_t remove_mask;
  uint32_t quota_storage_remove_mask;
  GURL storage_origin;
  const base::Closure callback;
  int task_count;
};

// Helper for deleting all sorts of data from a partition, keeps track of
// deletion status.
//
// StoragePartitionImpl creates an instance of this class to keep track of
// data deletion progress. Deletion requires deleting multiple bits of data
// (e.g. cookies, local storage, session storage etc.) and hopping between UI
// and IO thread. An instance of this class is created in the beginning of
// deletion process (StoragePartitionImpl::ClearDataImpl) and the instance is
// forwarded and updated on each (sub) deletion's callback. The instance is
// finally destroyed when deletion completes (and |callback| is invoked).
struct StoragePartitionImpl::DataDeletionHelper {
  DataDeletionHelper(uint32_t remove_mask,
                     uint32_t quota_storage_remove_mask,
                     const base::Closure& callback)
      : remove_mask(remove_mask),
        quota_storage_remove_mask(quota_storage_remove_mask),
        callback(callback),
        task_count(0) {}

  void IncrementTaskCountOnUI();
  void DecrementTaskCountOnUI();

  void ClearDataOnUIThread(
      const GURL& storage_origin,
      const OriginMatcherFunction& origin_matcher,
      const CookieMatcherFunction& cookie_matcher,
      const base::FilePath& path,
      net::URLRequestContextGetter* rq_context,
      DOMStorageContextWrapper* dom_storage_context,
      storage::QuotaManager* quota_manager,
      storage::SpecialStoragePolicy* special_storage_policy,
      storage::FileSystemContext* filesystem_context,
      const base::Time begin,
      const base::Time end);

  void ClearQuotaManagedDataOnIOThread(
      const scoped_refptr<storage::QuotaManager>& quota_manager,
      const base::Time begin,
      const GURL& storage_origin,
      const scoped_refptr<storage::SpecialStoragePolicy>&
          special_storage_policy,
      const StoragePartition::OriginMatcherFunction& origin_matcher,
      const base::Closure& callback);

  uint32_t remove_mask;
  uint32_t quota_storage_remove_mask;

  // Accessed on UI thread.
  const base::Closure callback;
  // Accessed on UI thread.
  int task_count;
};

void StoragePartitionImpl::DataDeletionHelper::ClearQuotaManagedDataOnIOThread(
    const scoped_refptr<storage::QuotaManager>& quota_manager,
    const base::Time begin,
    const GURL& storage_origin,
    const scoped_refptr<storage::SpecialStoragePolicy>& special_storage_policy,
    const StoragePartition::OriginMatcherFunction& origin_matcher,
    const base::Closure& callback) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);

  StoragePartitionImpl::QuotaManagedDataDeletionHelper* helper =
      new StoragePartitionImpl::QuotaManagedDataDeletionHelper(
          remove_mask,
          quota_storage_remove_mask,
          storage_origin,
          callback);
  helper->ClearDataOnIOThread(quota_manager, begin, special_storage_policy,
                              origin_matcher);
}

StoragePartitionImpl::StoragePartitionImpl(
    BrowserContext* browser_context,
    const base::FilePath& partition_path,
    storage::SpecialStoragePolicy* special_storage_policy)
    : partition_path_(partition_path),
      special_storage_policy_(special_storage_policy),
      browser_context_(browser_context),
      weak_factory_(this) {}

StoragePartitionImpl::~StoragePartitionImpl() {
  browser_context_ = nullptr;

  // These message loop checks are just to avoid leaks in unittests.
  if (GetDatabaseTracker() &&
      BrowserThread::IsMessageLoopValid(BrowserThread::FILE)) {
    BrowserThread::PostTask(
        BrowserThread::FILE,
        FROM_HERE,
        base::Bind(&storage::DatabaseTracker::Shutdown, GetDatabaseTracker()));
  }

  if (GetFileSystemContext())
    GetFileSystemContext()->Shutdown();

  if (GetDOMStorageContext())
    GetDOMStorageContext()->Shutdown();

  if (GetServiceWorkerContext())
    GetServiceWorkerContext()->Shutdown();

  if (GetCacheStorageContext())
    GetCacheStorageContext()->Shutdown();

  if (GetPlatformNotificationContext())
    GetPlatformNotificationContext()->Shutdown();

  if (GetBackgroundFetchContext())
    GetBackgroundFetchContext()->Shutdown();

  if (GetBackgroundSyncContext())
    GetBackgroundSyncContext()->Shutdown();

  if (GetPaymentAppContext())
    GetPaymentAppContext()->Shutdown();
}

// static
std::unique_ptr<StoragePartitionImpl> StoragePartitionImpl::Create(
    BrowserContext* context,
    bool in_memory,
    const base::FilePath& relative_partition_path) {
  // Ensure that these methods are called on the UI thread, except for
  // unittests where a UI thread might not have been created.
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI) ||
         !BrowserThread::IsMessageLoopValid(BrowserThread::UI));

  base::FilePath partition_path =
      context->GetPath().Append(relative_partition_path);

  std::unique_ptr<StoragePartitionImpl> partition =
      base::WrapUnique(new StoragePartitionImpl(
          context, partition_path, context->GetSpecialStoragePolicy()));

  // All of the clients have to be created and registered with the
  // QuotaManager prior to the QuotaManger being used. We do them
  // all together here prior to handing out a reference to anything
  // that utilizes the QuotaManager.
  partition->quota_manager_ = new storage::QuotaManager(
      in_memory, partition_path,
      BrowserThread::GetTaskRunnerForThread(BrowserThread::IO).get(),
      BrowserThread::GetTaskRunnerForThread(BrowserThread::DB).get(),
      context->GetSpecialStoragePolicy(),
      base::Bind(&StoragePartitionImpl::GetQuotaSettings,
                 partition->weak_factory_.GetWeakPtr()));
  scoped_refptr<storage::QuotaManagerProxy> quota_manager_proxy =
      partition->quota_manager_->proxy();

  // Each consumer is responsible for registering its QuotaClient during
  // its construction.
  partition->filesystem_context_ = CreateFileSystemContext(
      context, partition_path, in_memory, quota_manager_proxy.get());

  partition->database_tracker_ = new storage::DatabaseTracker(
      partition_path, in_memory, context->GetSpecialStoragePolicy(),
      quota_manager_proxy.get(),
      BrowserThread::GetTaskRunnerForThread(BrowserThread::FILE).get());

  partition->dom_storage_context_ = new DOMStorageContextWrapper(
      BrowserContext::GetConnectorFor(context),
      in_memory ? base::FilePath() : context->GetPath(),
      relative_partition_path, context->GetSpecialStoragePolicy());

  // BrowserMainLoop may not be initialized in unit tests. Tests will
  // need to inject their own task runner into the IndexedDBContext.
  base::SequencedTaskRunner* idb_task_runner =
      BrowserThread::CurrentlyOn(BrowserThread::UI) &&
              BrowserMainLoop::GetInstance()
          ? BrowserMainLoop::GetInstance()
                ->indexed_db_thread()
                ->task_runner()
                .get()
          : NULL;

  base::FilePath path = in_memory ? base::FilePath() : partition_path;
  partition->indexed_db_context_ =
      new IndexedDBContextImpl(path, context->GetSpecialStoragePolicy(),
                               quota_manager_proxy.get(), idb_task_runner);

  partition->cache_storage_context_ = new CacheStorageContextImpl(context);
  partition->cache_storage_context_->Init(path, quota_manager_proxy);

  partition->service_worker_context_ = new ServiceWorkerContextWrapper(context);
  partition->service_worker_context_->Init(path, quota_manager_proxy.get(),
                                           context->GetSpecialStoragePolicy());
  partition->service_worker_context_->set_storage_partition(partition.get());

  partition->appcache_service_ =
      new ChromeAppCacheService(quota_manager_proxy.get());

  partition->push_messaging_context_ =
      new PushMessagingContext(context, partition->service_worker_context_);

  partition->host_zoom_level_context_ = new HostZoomLevelContext(
      context->CreateZoomLevelDelegate(partition_path));

  partition->platform_notification_context_ =
      new PlatformNotificationContextImpl(path, context,
                                          partition->service_worker_context_);
  partition->platform_notification_context_->Initialize();

  partition->background_fetch_context_ = new BackgroundFetchContext(
      context, partition.get(), partition->service_worker_context_);

  partition->background_sync_context_ = new BackgroundSyncContext();
  partition->background_sync_context_->Init(partition->service_worker_context_);

  partition->payment_app_context_ = new PaymentAppContextImpl();
  partition->payment_app_context_->Init(partition->service_worker_context_);

  partition->broadcast_channel_provider_ = new BroadcastChannelProvider();

  partition->bluetooth_allowed_devices_map_ = new BluetoothAllowedDevicesMap();

  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kEnableNetworkService)) {
    mojom::NetworkServicePtr network_service;
    ServiceManagerConnection::GetForProcess()->GetConnector()->BindInterface(
        mojom::kNetworkServiceName, &network_service);
    mojom::NetworkContextParamsPtr context_params =
        mojom::NetworkContextParams::New();
    // TODO: fill this
    // context_params->cache_dir =
    // context_params->cookie_path =
    network_service->CreateNetworkContext(
        MakeRequest(&partition->network_context_), std::move(context_params));

    partition->url_loader_factory_getter_ = new URLLoaderFactoryGetter();
    partition->url_loader_factory_getter_->Initialize(partition.get());
  }

  return partition;
}

base::FilePath StoragePartitionImpl::GetPath() {
  return partition_path_;
}

net::URLRequestContextGetter* StoragePartitionImpl::GetURLRequestContext() {
  return url_request_context_.get();
}

net::URLRequestContextGetter*
StoragePartitionImpl::GetMediaURLRequestContext() {
  return media_url_request_context_.get();
}

storage::QuotaManager* StoragePartitionImpl::GetQuotaManager() {
  return quota_manager_.get();
}

ChromeAppCacheService* StoragePartitionImpl::GetAppCacheService() {
  return appcache_service_.get();
}

storage::FileSystemContext* StoragePartitionImpl::GetFileSystemContext() {
  return filesystem_context_.get();
}

storage::DatabaseTracker* StoragePartitionImpl::GetDatabaseTracker() {
  return database_tracker_.get();
}

DOMStorageContextWrapper* StoragePartitionImpl::GetDOMStorageContext() {
  return dom_storage_context_.get();
}

IndexedDBContextImpl* StoragePartitionImpl::GetIndexedDBContext() {
  return indexed_db_context_.get();
}

CacheStorageContextImpl* StoragePartitionImpl::GetCacheStorageContext() {
  return cache_storage_context_.get();
}

ServiceWorkerContextWrapper* StoragePartitionImpl::GetServiceWorkerContext() {
  return service_worker_context_.get();
}

HostZoomMap* StoragePartitionImpl::GetHostZoomMap() {
  DCHECK(host_zoom_level_context_.get());
  return host_zoom_level_context_->GetHostZoomMap();
}

HostZoomLevelContext* StoragePartitionImpl::GetHostZoomLevelContext() {
  return host_zoom_level_context_.get();
}

ZoomLevelDelegate* StoragePartitionImpl::GetZoomLevelDelegate() {
  DCHECK(host_zoom_level_context_.get());
  return host_zoom_level_context_->GetZoomLevelDelegate();
}

PlatformNotificationContextImpl*
StoragePartitionImpl::GetPlatformNotificationContext() {
  return platform_notification_context_.get();
}

BackgroundFetchContext* StoragePartitionImpl::GetBackgroundFetchContext() {
  return background_fetch_context_.get();
}

BackgroundSyncContext* StoragePartitionImpl::GetBackgroundSyncContext() {
  return background_sync_context_.get();
}

PaymentAppContextImpl* StoragePartitionImpl::GetPaymentAppContext() {
  return payment_app_context_.get();
}

BroadcastChannelProvider* StoragePartitionImpl::GetBroadcastChannelProvider() {
  return broadcast_channel_provider_.get();
}

BluetoothAllowedDevicesMap*
StoragePartitionImpl::GetBluetoothAllowedDevicesMap() {
  return bluetooth_allowed_devices_map_.get();
}

void StoragePartitionImpl::OpenLocalStorage(
    const url::Origin& origin,
    mojo::InterfaceRequest<mojom::LevelDBWrapper> request) {
  dom_storage_context_->OpenLocalStorage(origin, std::move(request));
}

void StoragePartitionImpl::ClearDataImpl(
    uint32_t remove_mask,
    uint32_t quota_storage_remove_mask,
    const GURL& storage_origin,
    const OriginMatcherFunction& origin_matcher,
    const CookieMatcherFunction& cookie_matcher,
    net::URLRequestContextGetter* rq_context,
    const base::Time begin,
    const base::Time end,
    const base::Closure& callback) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  DataDeletionHelper* helper = new DataDeletionHelper(remove_mask,
                                                      quota_storage_remove_mask,
                                                      callback);
  // |helper| deletes itself when done in
  // DataDeletionHelper::DecrementTaskCountOnUI().
  helper->ClearDataOnUIThread(
      storage_origin, origin_matcher, cookie_matcher, GetPath(), rq_context,
      dom_storage_context_.get(), quota_manager_.get(),
      special_storage_policy_.get(), filesystem_context_.get(), begin, end);
}

void StoragePartitionImpl::
    QuotaManagedDataDeletionHelper::IncrementTaskCountOnIO() {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  ++task_count;
}

void StoragePartitionImpl::
    QuotaManagedDataDeletionHelper::DecrementTaskCountOnIO() {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  DCHECK_GT(task_count, 0);
  --task_count;
  if (task_count)
    return;

  callback.Run();
  delete this;
}

void StoragePartitionImpl::QuotaManagedDataDeletionHelper::ClearDataOnIOThread(
    const scoped_refptr<storage::QuotaManager>& quota_manager,
    const base::Time begin,
    const scoped_refptr<storage::SpecialStoragePolicy>& special_storage_policy,
    const StoragePartition::OriginMatcherFunction& origin_matcher) {
  IncrementTaskCountOnIO();
  base::Closure decrement_callback = base::Bind(
      &QuotaManagedDataDeletionHelper::DecrementTaskCountOnIO,
      base::Unretained(this));

  if (quota_storage_remove_mask & QUOTA_MANAGED_STORAGE_MASK_PERSISTENT) {
    IncrementTaskCountOnIO();
    // Ask the QuotaManager for all origins with persistent quota modified
    // within the user-specified timeframe, and deal with the resulting set in
    // ClearQuotaManagedOriginsOnIOThread().
    quota_manager->GetOriginsModifiedSince(
        storage::kStorageTypePersistent, begin,
        base::Bind(&QuotaManagedDataDeletionHelper::ClearOriginsOnIOThread,
                   base::Unretained(this), base::RetainedRef(quota_manager),
                   special_storage_policy, origin_matcher, decrement_callback));
  }

  // Do the same for temporary quota.
  if (quota_storage_remove_mask & QUOTA_MANAGED_STORAGE_MASK_TEMPORARY) {
    IncrementTaskCountOnIO();
    quota_manager->GetOriginsModifiedSince(
        storage::kStorageTypeTemporary, begin,
        base::Bind(&QuotaManagedDataDeletionHelper::ClearOriginsOnIOThread,
                   base::Unretained(this), base::RetainedRef(quota_manager),
                   special_storage_policy, origin_matcher, decrement_callback));
  }

  // Do the same for syncable quota.
  if (quota_storage_remove_mask & QUOTA_MANAGED_STORAGE_MASK_SYNCABLE) {
    IncrementTaskCountOnIO();
    quota_manager->GetOriginsModifiedSince(
        storage::kStorageTypeSyncable, begin,
        base::Bind(&QuotaManagedDataDeletionHelper::ClearOriginsOnIOThread,
                   base::Unretained(this), base::RetainedRef(quota_manager),
                   special_storage_policy, origin_matcher, decrement_callback));
  }

  DecrementTaskCountOnIO();
}

void
StoragePartitionImpl::QuotaManagedDataDeletionHelper::ClearOriginsOnIOThread(
    storage::QuotaManager* quota_manager,
    const scoped_refptr<storage::SpecialStoragePolicy>& special_storage_policy,
    const StoragePartition::OriginMatcherFunction& origin_matcher,
    const base::Closure& callback,
    const std::set<GURL>& origins,
    storage::StorageType quota_storage_type) {
  // The QuotaManager manages all storage other than cookies, LocalStorage,
  // and SessionStorage. This loop wipes out most HTML5 storage for the given
  // origins.
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  if (origins.empty()) {
    callback.Run();
    return;
  }

  size_t* deletion_task_count = new size_t(0u);
  (*deletion_task_count)++;
  for (std::set<GURL>::const_iterator origin = origins.begin();
       origin != origins.end(); ++origin) {
    // TODO(mkwst): Clean this up, it's slow. http://crbug.com/130746
    if (!storage_origin.is_empty() && origin->GetOrigin() != storage_origin)
      continue;

    if (!origin_matcher.is_null() &&
        !origin_matcher.Run(*origin, special_storage_policy.get())) {
      continue;
    }

    (*deletion_task_count)++;
    quota_manager->DeleteOriginData(
        *origin, quota_storage_type,
        StoragePartitionImpl::GenerateQuotaClientMask(remove_mask),
        base::Bind(&OnQuotaManagedOriginDeleted,
                   origin->GetOrigin(), quota_storage_type,
                   deletion_task_count, callback));
  }
  (*deletion_task_count)--;

  CheckQuotaManagedDataDeletionStatus(deletion_task_count, callback);
}

void StoragePartitionImpl::DataDeletionHelper::IncrementTaskCountOnUI() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  ++task_count;
}

void StoragePartitionImpl::DataDeletionHelper::DecrementTaskCountOnUI() {
  if (!BrowserThread::CurrentlyOn(BrowserThread::UI)) {
    BrowserThread::PostTask(
        BrowserThread::UI, FROM_HERE,
        base::Bind(&DataDeletionHelper::DecrementTaskCountOnUI,
                   base::Unretained(this)));
    return;
  }
  DCHECK_GT(task_count, 0);
  --task_count;
  if (!task_count) {
    callback.Run();
    delete this;
  }
}

void StoragePartitionImpl::DataDeletionHelper::ClearDataOnUIThread(
    const GURL& storage_origin,
    const OriginMatcherFunction& origin_matcher,
    const CookieMatcherFunction& cookie_matcher,
    const base::FilePath& path,
    net::URLRequestContextGetter* rq_context,
    DOMStorageContextWrapper* dom_storage_context,
    storage::QuotaManager* quota_manager,
    storage::SpecialStoragePolicy* special_storage_policy,
    storage::FileSystemContext* filesystem_context,
    const base::Time begin,
    const base::Time end) {
  DCHECK_NE(remove_mask, 0u);
  DCHECK(!callback.is_null());

  IncrementTaskCountOnUI();
  base::Closure decrement_callback = base::Bind(
      &DataDeletionHelper::DecrementTaskCountOnUI, base::Unretained(this));

  if (remove_mask & REMOVE_DATA_MASK_COOKIES) {
    // Handle the cookies.
    IncrementTaskCountOnUI();
    BrowserThread::PostTask(
        BrowserThread::IO, FROM_HERE,
        base::Bind(&ClearCookiesOnIOThread, make_scoped_refptr(rq_context),
                   begin, end, storage_origin, cookie_matcher,
                   decrement_callback));
  }

  if (remove_mask & REMOVE_DATA_MASK_INDEXEDDB ||
      remove_mask & REMOVE_DATA_MASK_WEBSQL ||
      remove_mask & REMOVE_DATA_MASK_APPCACHE ||
      remove_mask & REMOVE_DATA_MASK_FILE_SYSTEMS ||
      remove_mask & REMOVE_DATA_MASK_SERVICE_WORKERS ||
      remove_mask & REMOVE_DATA_MASK_CACHE_STORAGE) {
    IncrementTaskCountOnUI();
    BrowserThread::PostTask(
        BrowserThread::IO, FROM_HERE,
        base::Bind(&DataDeletionHelper::ClearQuotaManagedDataOnIOThread,
                   base::Unretained(this),
                   make_scoped_refptr(quota_manager),
                   begin,
                   storage_origin,
                   make_scoped_refptr(special_storage_policy),
                   origin_matcher,
                   decrement_callback));
  }

  if (remove_mask & REMOVE_DATA_MASK_LOCAL_STORAGE) {
    IncrementTaskCountOnUI();
    ClearLocalStorageOnUIThread(
        make_scoped_refptr(dom_storage_context),
        make_scoped_refptr(special_storage_policy),
        origin_matcher,
        storage_origin, begin, end,
        decrement_callback);

    // ClearDataImpl cannot clear session storage data when a particular origin
    // is specified. Therefore we ignore clearing session storage in this case.
    // TODO(lazyboy): Fix.
    if (storage_origin.is_empty()) {
      IncrementTaskCountOnUI();
      ClearSessionStorageOnUIThread(
          make_scoped_refptr(dom_storage_context),
          make_scoped_refptr(special_storage_policy),
          origin_matcher,
          decrement_callback);
    }
  }

  if (remove_mask & REMOVE_DATA_MASK_SHADER_CACHE) {
    IncrementTaskCountOnUI();
    BrowserThread::PostTask(
        BrowserThread::IO, FROM_HERE,
        base::Bind(&ClearShaderCacheOnIOThread,
                   path, begin, end, decrement_callback));
  }

#if BUILDFLAG(ENABLE_PLUGINS)
  if (remove_mask & REMOVE_DATA_MASK_PLUGIN_PRIVATE_DATA) {
    IncrementTaskCountOnUI();
    filesystem_context->default_file_task_runner()->PostTask(
        FROM_HERE, base::Bind(&ClearPluginPrivateDataOnFileTaskRunner,
                              make_scoped_refptr(filesystem_context),
                              storage_origin, begin, end, decrement_callback));
  }
#endif  // BUILDFLAG(ENABLE_PLUGINS)

  DecrementTaskCountOnUI();
}

void StoragePartitionImpl::ClearDataForOrigin(
    uint32_t remove_mask,
    uint32_t quota_storage_remove_mask,
    const GURL& storage_origin,
    net::URLRequestContextGetter* request_context_getter,
    const base::Closure& callback) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  ClearDataImpl(remove_mask, quota_storage_remove_mask, storage_origin,
                OriginMatcherFunction(), CookieMatcherFunction(),
                request_context_getter, base::Time(), base::Time::Max(),
                callback);
}

void StoragePartitionImpl::ClearData(
    uint32_t remove_mask,
    uint32_t quota_storage_remove_mask,
    const GURL& storage_origin,
    const OriginMatcherFunction& origin_matcher,
    const base::Time begin,
    const base::Time end,
    const base::Closure& callback) {
  ClearDataImpl(remove_mask, quota_storage_remove_mask, storage_origin,
                origin_matcher, CookieMatcherFunction(), GetURLRequestContext(),
                begin, end, callback);
}

void StoragePartitionImpl::ClearData(
    uint32_t remove_mask,
    uint32_t quota_storage_remove_mask,
    const OriginMatcherFunction& origin_matcher,
    const CookieMatcherFunction& cookie_matcher,
    const base::Time begin,
    const base::Time end,
    const base::Closure& callback) {
  ClearDataImpl(remove_mask, quota_storage_remove_mask, GURL(), origin_matcher,
                cookie_matcher, GetURLRequestContext(), begin, end, callback);
}

void StoragePartitionImpl::ClearHttpAndMediaCaches(
    const base::Time begin,
    const base::Time end,
    const base::Callback<bool(const GURL&)>& url_matcher,
    const base::Closure& callback) {
  // StoragePartitionHttpCacheDataRemover deletes itself when it is done.
  if (url_matcher.is_null()) {
    StoragePartitionHttpCacheDataRemover::CreateForRange(this, begin, end)
        ->Remove(callback);
  } else {
    StoragePartitionHttpCacheDataRemover::CreateForURLsAndRange(
        this, url_matcher, begin, end)
        ->Remove(callback);
  }
}

void StoragePartitionImpl::Flush() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  if (GetDOMStorageContext())
    GetDOMStorageContext()->Flush();
}

void StoragePartitionImpl::ClearBluetoothAllowedDevicesMapForTesting() {
  bluetooth_allowed_devices_map_->Clear();
}

BrowserContext* StoragePartitionImpl::browser_context() const {
  return browser_context_;
}

void StoragePartitionImpl::Bind(
    mojo::InterfaceRequest<mojom::StoragePartitionService> request) {
  bindings_.AddBinding(this, std::move(request));
}

void StoragePartitionImpl::OverrideQuotaManagerForTesting(
    storage::QuotaManager* quota_manager) {
  quota_manager_ = quota_manager;
}

void StoragePartitionImpl::OverrideSpecialStoragePolicyForTesting(
    storage::SpecialStoragePolicy* special_storage_policy) {
  special_storage_policy_ = special_storage_policy;
}

void StoragePartitionImpl::SetURLRequestContext(
    net::URLRequestContextGetter* url_request_context) {
  url_request_context_ = url_request_context;
}

void StoragePartitionImpl::SetMediaURLRequestContext(
    net::URLRequestContextGetter* media_url_request_context) {
  media_url_request_context_ = media_url_request_context;
}

void StoragePartitionImpl::GetQuotaSettings(
    const storage::OptionalQuotaSettingsCallback& callback) {
  GetContentClient()->browser()->GetQuotaSettings(browser_context_, this,
                                                  callback);
}

}  // namespace content
