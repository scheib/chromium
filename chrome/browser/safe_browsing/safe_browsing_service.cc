// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/safe_browsing/safe_browsing_service.h"

#include <stddef.h>

#include <vector>

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/callback.h"
#include "base/command_line.h"
#include "base/lazy_instance.h"
#include "base/macros.h"
#include "base/memory/ptr_util.h"
#include "base/metrics/histogram_macros.h"
#include "base/path_service.h"
#include "base/single_thread_task_runner.h"
#include "base/strings/string_util.h"
#include "base/task_scheduler/post_task.h"
#include "base/threading/thread.h"
#include "base/threading/thread_restrictions.h"
#include "base/trace_event/trace_event.h"
#include "build/build_config.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/safe_browsing/chrome_password_protection_service.h"
#include "chrome/browser/safe_browsing/ping_manager.h"
#include "chrome/browser/safe_browsing/safe_browsing_navigation_observer_manager.h"
#include "chrome/browser/safe_browsing/ui_manager.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/safe_browsing/file_type_policies.h"
#include "components/prefs/pref_change_registrar.h"
#include "components/prefs/pref_service.h"
#include "components/safe_browsing/common/safebrowsing_constants.h"
#include "components/safe_browsing/common/safebrowsing_switches.h"
#include "components/safe_browsing/triggers/trigger_manager.h"
#include "components/safe_browsing_db/database_manager.h"
#include "components/safe_browsing_db/v4_feature_list.h"
#include "components/safe_browsing_db/v4_get_hash_protocol_manager.h"
#include "components/safe_browsing_db/v4_local_database_manager.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/cookie_store_factory.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/resource_request_info.h"
#include "google_apis/google_api_keys.h"
#include "net/cookies/cookie_store.h"
#include "net/extras/sqlite/cookie_crypto_delegate.h"
#include "net/extras/sqlite/sqlite_channel_id_store.h"
#include "net/http/http_network_layer.h"
#include "net/http/http_transaction_factory.h"
#include "net/ssl/channel_id_service.h"
#include "net/ssl/default_channel_id_store.h"
#include "net/url_request/url_request_context.h"
#include "net/url_request/url_request_context_getter.h"
#include "services/preferences/public/interfaces/tracked_preference_validation_delegate.mojom.h"

#if defined(OS_WIN)
#include "chrome/installer/util/browser_distribution.h"
#endif

#if defined(SAFE_BROWSING_DB_LOCAL)
#include "chrome/browser/safe_browsing/local_database_manager.h"
#elif defined(SAFE_BROWSING_DB_REMOTE)
#include "components/safe_browsing_db/remote_database_manager.h"
#endif

#if defined(FULL_SAFE_BROWSING)
#include "chrome/browser/safe_browsing/client_side_detection_service.h"
#include "chrome/browser/safe_browsing/download_protection_service.h"
#include "chrome/browser/safe_browsing/incident_reporting/binary_integrity_analyzer.h"
#include "chrome/browser/safe_browsing/incident_reporting/blacklist_load_analyzer.h"
#include "chrome/browser/safe_browsing/incident_reporting/incident_reporting_service.h"
#include "chrome/browser/safe_browsing/incident_reporting/module_load_analyzer.h"
#include "chrome/browser/safe_browsing/incident_reporting/resource_request_detector.h"
#include "chrome/browser/safe_browsing/incident_reporting/variations_seed_signature_analyzer.h"
#include "chrome/browser/safe_browsing/protocol_manager.h"
#endif

using content::BrowserThread;

namespace safe_browsing {

namespace {

// Filename suffix for the cookie database.
const base::FilePath::CharType kCookiesFile[] = FILE_PATH_LITERAL(" Cookies");
const base::FilePath::CharType kChannelIDFile[] =
    FILE_PATH_LITERAL(" Channel IDs");

// The default URL prefix where browser fetches chunk updates, hashes,
// and reports safe browsing hits and malware details.
const char kSbDefaultURLPrefix[] =
    "https://safebrowsing.google.com/safebrowsing";

// The backup URL prefix used when there are issues establishing a connection
// with the server at the primary URL.
const char kSbBackupConnectErrorURLPrefix[] =
    "https://alt1-safebrowsing.google.com/safebrowsing";

// The backup URL prefix used when there are HTTP-specific issues with the
// server at the primary URL.
const char kSbBackupHttpErrorURLPrefix[] =
    "https://alt2-safebrowsing.google.com/safebrowsing";

// The backup URL prefix used when there are local network specific issues.
const char kSbBackupNetworkErrorURLPrefix[] =
    "https://alt3-safebrowsing.google.com/safebrowsing";

base::FilePath CookieFilePath() {
  return base::FilePath(
      SafeBrowsingService::GetBaseFilename().value() + kCookiesFile);
}

base::FilePath ChannelIDFilePath() {
  return base::FilePath(SafeBrowsingService::GetBaseFilename().value() +
                        kChannelIDFile);
}

}  // namespace

class SafeBrowsingURLRequestContextGetter
    : public net::URLRequestContextGetter {
 public:
  explicit SafeBrowsingURLRequestContextGetter(
      scoped_refptr<net::URLRequestContextGetter> system_context_getter);

  // Implementation for net::UrlRequestContextGetter.
  net::URLRequestContext* GetURLRequestContext() override;
  scoped_refptr<base::SingleThreadTaskRunner> GetNetworkTaskRunner()
      const override;

  // Shuts down any pending requests using the getter, and sets |shut_down_| to
  // true.
  void ServiceShuttingDown();

  // Disables QUIC. This should not be necessary anymore when
  // http://crbug.com/678653 is implemented.
  void DisableQuicOnIOThread();

 protected:
  ~SafeBrowsingURLRequestContextGetter() override;

 private:
  bool shut_down_;

  scoped_refptr<net::URLRequestContextGetter> system_context_getter_;

  std::unique_ptr<net::CookieStore> safe_browsing_cookie_store_;

  std::unique_ptr<net::URLRequestContext> safe_browsing_request_context_;

  scoped_refptr<base::SingleThreadTaskRunner> network_task_runner_;

  std::unique_ptr<net::ChannelIDService> channel_id_service_;
  std::unique_ptr<net::HttpNetworkSession> http_network_session_;
  std::unique_ptr<net::HttpTransactionFactory> http_transaction_factory_;
};

SafeBrowsingURLRequestContextGetter::SafeBrowsingURLRequestContextGetter(
    scoped_refptr<net::URLRequestContextGetter> system_context_getter)
    : shut_down_(false),
      system_context_getter_(system_context_getter),
      network_task_runner_(
          BrowserThread::GetTaskRunnerForThread(BrowserThread::IO)) {}

net::URLRequestContext*
SafeBrowsingURLRequestContextGetter::GetURLRequestContext() {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);

  // Check if the service has been shut down.
  if (shut_down_)
    return nullptr;

  if (!safe_browsing_request_context_) {
    safe_browsing_request_context_.reset(new net::URLRequestContext());
    // May be NULL in unit tests.
    if (system_context_getter_) {
      safe_browsing_request_context_->CopyFrom(
          system_context_getter_->GetURLRequestContext());
    }
    scoped_refptr<base::SequencedTaskRunner> background_task_runner =
        base::CreateSequencedTaskRunnerWithTraits(
            {base::MayBlock(), base::TaskPriority::BACKGROUND,
             base::TaskShutdownBehavior::BLOCK_SHUTDOWN});
    // Set up the ChannelIDService
    scoped_refptr<net::SQLiteChannelIDStore> channel_id_db =
        new net::SQLiteChannelIDStore(ChannelIDFilePath(),
                                      background_task_runner);
    channel_id_service_.reset(new net::ChannelIDService(
        new net::DefaultChannelIDStore(channel_id_db.get())));

    // Set up the CookieStore
    content::CookieStoreConfig cookie_config(
        CookieFilePath(), content::CookieStoreConfig::EPHEMERAL_SESSION_COOKIES,
        nullptr, nullptr);
    cookie_config.channel_id_service = channel_id_service_.get();
    cookie_config.background_task_runner = background_task_runner;
    safe_browsing_cookie_store_ = content::CreateCookieStore(cookie_config);
    safe_browsing_request_context_->set_cookie_store(
        safe_browsing_cookie_store_.get());

    safe_browsing_request_context_->set_channel_id_service(
        channel_id_service_.get());
    safe_browsing_cookie_store_->SetChannelIDServiceID(
        channel_id_service_->GetUniqueID());

    // Rebuild the HttpNetworkSession and the HttpTransactionFactory to use the
    // new ChannelIDService.
    if (safe_browsing_request_context_->http_transaction_factory() &&
        safe_browsing_request_context_->http_transaction_factory()
            ->GetSession()) {
      net::HttpNetworkSession::Params safe_browsing_params =
          safe_browsing_request_context_->http_transaction_factory()
              ->GetSession()
              ->params();
      safe_browsing_params.channel_id_service = channel_id_service_.get();
      http_network_session_.reset(
          new net::HttpNetworkSession(safe_browsing_params));
      http_transaction_factory_.reset(
          new net::HttpNetworkLayer(http_network_session_.get()));
      safe_browsing_request_context_->set_http_transaction_factory(
          http_transaction_factory_.get());
    }
    safe_browsing_request_context_->set_name("safe_browsing");
  }

  return safe_browsing_request_context_.get();
}

scoped_refptr<base::SingleThreadTaskRunner>
SafeBrowsingURLRequestContextGetter::GetNetworkTaskRunner() const {
  return network_task_runner_;
}

void SafeBrowsingURLRequestContextGetter::ServiceShuttingDown() {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);

  shut_down_ = true;
  URLRequestContextGetter::NotifyContextShuttingDown();
  safe_browsing_request_context_.reset();
}

void SafeBrowsingURLRequestContextGetter::DisableQuicOnIOThread() {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);

  if (http_network_session_)
    http_network_session_->DisableQuic();
}

SafeBrowsingURLRequestContextGetter::~SafeBrowsingURLRequestContextGetter() {}

// static
SafeBrowsingServiceFactory* SafeBrowsingService::factory_ = NULL;

// The default SafeBrowsingServiceFactory.  Global, made a singleton so we
// don't leak it.
class SafeBrowsingServiceFactoryImpl : public SafeBrowsingServiceFactory {
 public:
  SafeBrowsingService* CreateSafeBrowsingService() override {
    return new SafeBrowsingService(V4FeatureList::GetV4UsageStatus());
  }

 private:
  friend struct base::LazyInstanceTraitsBase<SafeBrowsingServiceFactoryImpl>;

  SafeBrowsingServiceFactoryImpl() { }

  DISALLOW_COPY_AND_ASSIGN(SafeBrowsingServiceFactoryImpl);
};

static base::LazyInstance<SafeBrowsingServiceFactoryImpl>::Leaky
    g_safe_browsing_service_factory_impl = LAZY_INSTANCE_INITIALIZER;

// static
base::FilePath SafeBrowsingService::GetCookieFilePathForTesting() {
  return CookieFilePath();
}

// static
base::FilePath SafeBrowsingService::GetBaseFilename() {
  base::FilePath path;
  bool result = PathService::Get(chrome::DIR_USER_DATA, &path);
  DCHECK(result);
  return path.Append(safe_browsing::kSafeBrowsingBaseFilename);
}


// static
SafeBrowsingService* SafeBrowsingService::CreateSafeBrowsingService() {
  if (!factory_)
    factory_ = g_safe_browsing_service_factory_impl.Pointer();
  return factory_->CreateSafeBrowsingService();
}

SafeBrowsingService::SafeBrowsingService(
    V4FeatureList::V4UsageStatus v4_usage_status)
    : services_delegate_(ServicesDelegate::Create(this)),
      estimated_extended_reporting_by_prefs_(SBER_LEVEL_OFF),
      enabled_(false),
      enabled_by_prefs_(false),
      use_v4_only_(v4_usage_status == V4FeatureList::V4UsageStatus::V4_ONLY),
      v4_enabled_(v4_usage_status ==
                      V4FeatureList::V4UsageStatus::V4_INSTANTIATED ||
                  v4_usage_status == V4FeatureList::V4UsageStatus::V4_ONLY) {}

SafeBrowsingService::~SafeBrowsingService() {
  // We should have already been shut down. If we're still enabled, then the
  // database isn't going to be closed properly, which could lead to corruption.
  DCHECK(!enabled_);
}

void SafeBrowsingService::Initialize() {
  // Ensure FileTypePolicies's Singleton is instantiated during startup.
  // This guarantees we'll log UMA metrics about its state.
  FileTypePolicies::GetInstance();

  url_request_context_getter_ = new SafeBrowsingURLRequestContextGetter(
      g_browser_process->system_request_context());

  ui_manager_ = CreateUIManager();

  if (!use_v4_only_) {
    database_manager_ = CreateDatabaseManager();
  }

  navigation_observer_manager_ = new SafeBrowsingNavigationObserverManager();

  services_delegate_->Initialize(v4_enabled_);
  services_delegate_->InitializeCsdService(url_request_context_getter_.get());

  // Needs to happen after |ui_manager_| is created.
  CreateTriggerManager();

  // Track profile creation and destruction.
  profiles_registrar_.Add(this, chrome::NOTIFICATION_PROFILE_CREATED,
                          content::NotificationService::AllSources());
  profiles_registrar_.Add(this, chrome::NOTIFICATION_PROFILE_DESTROYED,
                          content::NotificationService::AllSources());

  // Register all the delayed analysis to the incident reporting service.
  RegisterAllDelayedAnalysis();
}

void SafeBrowsingService::ShutDown() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  shutdown_callback_list_.Notify();

  // Remove Profile creation/destruction observers.
  profiles_registrar_.RemoveAll();

  // Delete the PrefChangeRegistrars, whose dtors also unregister |this| as an
  // observer of the preferences.
  prefs_map_.clear();

  // Delete the ChromePasswordProtectionService instances.
  password_protection_service_map_.clear();

  Stop(true);

  services_delegate_->ShutdownServices();

  // Since URLRequestContextGetters are refcounted, can't count on clearing
  // |url_request_context_getter_| to delete it, so need to shut it down first,
  // which will cancel any requests that are currently using it, and prevent
  // new requests from using it as well.
  BrowserThread::PostNonNestableTask(
      BrowserThread::IO, FROM_HERE,
      base::BindOnce(&SafeBrowsingURLRequestContextGetter::ServiceShuttingDown,
                     url_request_context_getter_));

  // Release the URLRequestContextGetter after passing it to the IOThread.  It
  // has to be released now rather than in the destructor because it can only
  // be deleted on the IOThread, and the SafeBrowsingService outlives the IO
  // thread.
  url_request_context_getter_ = nullptr;
}

bool SafeBrowsingService::DownloadBinHashNeeded() const {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

#if defined(FULL_SAFE_BROWSING)
  return database_manager()->IsDownloadProtectionEnabled() ||
         (download_protection_service() &&
          download_protection_service()->enabled());
#else
  return false;
#endif
}

scoped_refptr<net::URLRequestContextGetter>
SafeBrowsingService::url_request_context() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  return url_request_context_getter_;
}

void SafeBrowsingService::DisableQuicOnIOThread() {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);

  url_request_context_getter_->DisableQuicOnIOThread();
}

const scoped_refptr<SafeBrowsingUIManager>&
SafeBrowsingService::ui_manager() const {
  return ui_manager_;
}

const scoped_refptr<SafeBrowsingDatabaseManager>&
SafeBrowsingService::database_manager() const {
  return use_v4_only_ ? v4_local_database_manager() : database_manager_;
}

scoped_refptr<SafeBrowsingNavigationObserverManager>
SafeBrowsingService::navigation_observer_manager() {
  return navigation_observer_manager_;
}

SafeBrowsingProtocolManager* SafeBrowsingService::protocol_manager() const {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
#if defined(SAFE_BROWSING_DB_LOCAL)
  DCHECK(!use_v4_only_);
  return protocol_manager_.get();
#else
  return nullptr;
#endif
}

SafeBrowsingPingManager* SafeBrowsingService::ping_manager() const {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  return ping_manager_.get();
}

const scoped_refptr<SafeBrowsingDatabaseManager>&
SafeBrowsingService::v4_local_database_manager() const {
  return services_delegate_->v4_local_database_manager();
}

TriggerManager* SafeBrowsingService::trigger_manager() const {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  return trigger_manager_.get();
}

PasswordProtectionService* SafeBrowsingService::GetPasswordProtectionService(
    Profile* profile) const {
  DCHECK(profile);
  auto it = password_protection_service_map_.find(profile);
  DCHECK(it != password_protection_service_map_.end());
  return it->second.get();
}

std::unique_ptr<prefs::mojom::TrackedPreferenceValidationDelegate>
SafeBrowsingService::CreatePreferenceValidationDelegate(
    Profile* profile) const {
  return services_delegate_->CreatePreferenceValidationDelegate(profile);
}

void SafeBrowsingService::RegisterDelayedAnalysisCallback(
    const DelayedAnalysisCallback& callback) {
  services_delegate_->RegisterDelayedAnalysisCallback(callback);
}

void SafeBrowsingService::AddDownloadManager(
    content::DownloadManager* download_manager) {
  services_delegate_->AddDownloadManager(download_manager);
}

void SafeBrowsingService::OnResourceRequest(const net::URLRequest* request) {
#if defined(FULL_SAFE_BROWSING)
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  TRACE_EVENT1("loader", "SafeBrowsingService::OnResourceRequest", "url",
               request->url().spec());

  ResourceRequestInfo info = ResourceRequestDetector::GetRequestInfo(request);
  BrowserThread::PostTask(
      BrowserThread::UI, FROM_HERE,
      base::BindOnce(&SafeBrowsingService::ProcessResourceRequest, this, info));
#endif
}

SafeBrowsingUIManager* SafeBrowsingService::CreateUIManager() {
  return new SafeBrowsingUIManager(this);
}

SafeBrowsingDatabaseManager* SafeBrowsingService::CreateDatabaseManager() {
#if defined(SAFE_BROWSING_DB_LOCAL)
  return new LocalSafeBrowsingDatabaseManager(this);
#elif defined(SAFE_BROWSING_DB_REMOTE)
  return new RemoteSafeBrowsingDatabaseManager();
#else
  return NULL;
#endif
}

void SafeBrowsingService::RegisterAllDelayedAnalysis() {
#if defined(FULL_SAFE_BROWSING)
  RegisterBinaryIntegrityAnalysis();
  RegisterBlacklistLoadAnalysis();
  RegisterModuleLoadAnalysis(database_manager());
  RegisterVariationsSeedSignatureAnalysis();
#endif
}

SafeBrowsingProtocolConfig SafeBrowsingService::GetProtocolConfig() const {
  SafeBrowsingProtocolConfig config;
  config.client_name = GetProtocolConfigClientName();

  base::CommandLine* cmdline = base::CommandLine::ForCurrentProcess();
  config.disable_auto_update =
      cmdline->HasSwitch(safe_browsing::switches::kSbDisableAutoUpdate) ||
      cmdline->HasSwitch(::switches::kDisableBackgroundNetworking);
  config.url_prefix = kSbDefaultURLPrefix;
  config.backup_connect_error_url_prefix = kSbBackupConnectErrorURLPrefix;
  config.backup_http_error_url_prefix = kSbBackupHttpErrorURLPrefix;
  config.backup_network_error_url_prefix = kSbBackupNetworkErrorURLPrefix;

  return config;
}

V4ProtocolConfig
SafeBrowsingService::GetV4ProtocolConfig() const {
  base::CommandLine* cmdline = base::CommandLine::ForCurrentProcess();
  return V4ProtocolConfig(
      GetProtocolConfigClientName(),
      cmdline->HasSwitch(::switches::kDisableBackgroundNetworking),
      google_apis::GetAPIKey(), ProtocolManagerHelper::Version());
}

std::string SafeBrowsingService::GetProtocolConfigClientName() const {
  std::string client_name;
  // On Windows, get the safe browsing client name from the browser
  // distribution classes in installer util. These classes don't yet have
  // an analog on non-Windows builds so just keep the name specified here.
#if defined(OS_WIN)
  BrowserDistribution* dist = BrowserDistribution::GetDistribution();
  client_name = dist->GetSafeBrowsingName();
#else
#if defined(GOOGLE_CHROME_BUILD)
  client_name = "googlechrome";
#else
  client_name = "chromium";
#endif

  // Mark client string to allow server to differentiate mobile.
#if defined(OS_ANDROID)
  client_name.append("-a");
#endif

#endif  // defined(OS_WIN)

  return client_name;
}

// Any tests that create a DatabaseManager that isn't derived from
// LocalSafeBrowsingDatabaseManager should override this to return NULL.
SafeBrowsingProtocolManagerDelegate*
SafeBrowsingService::GetProtocolManagerDelegate() {
#if defined(SAFE_BROWSING_DB_LOCAL)
  DCHECK(!use_v4_only_);
  return static_cast<LocalSafeBrowsingDatabaseManager*>(
      database_manager_.get());
#else
  NOTREACHED();
  return NULL;
#endif
}

void SafeBrowsingService::StartOnIOThread(
    net::URLRequestContextGetter* url_request_context_getter) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  if (enabled_)
    return;
  enabled_ = true;

  SafeBrowsingProtocolConfig config = GetProtocolConfig();
  V4ProtocolConfig v4_config = GetV4ProtocolConfig();

  services_delegate_->StartOnIOThread(url_request_context_getter, v4_config);

#if defined(SAFE_BROWSING_DB_LOCAL) || defined(SAFE_BROWSING_DB_REMOTE)
  if (!use_v4_only_) {
    DCHECK(database_manager_.get());
    database_manager_->StartOnIOThread(url_request_context_getter, v4_config);
  }
#endif

#if defined(SAFE_BROWSING_DB_LOCAL)
  if (!use_v4_only_) {
    SafeBrowsingProtocolManagerDelegate* protocol_manager_delegate =
        GetProtocolManagerDelegate();
    if (protocol_manager_delegate) {
      protocol_manager_ = SafeBrowsingProtocolManager::Create(
          protocol_manager_delegate, url_request_context_getter, config);
      protocol_manager_->Initialize();
    }
  }
#endif

  DCHECK(!ping_manager_);
  ping_manager_ = SafeBrowsingPingManager::Create(
      url_request_context_getter, config);
}

void SafeBrowsingService::StopOnIOThread(bool shutdown) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);

#if defined(SAFE_BROWSING_DB_LOCAL) || defined(SAFE_BROWSING_DB_REMOTE)
  if (!use_v4_only_) {
    database_manager_->StopOnIOThread(shutdown);
  }
#endif
  ui_manager_->StopOnIOThread(shutdown);

  services_delegate_->StopOnIOThread(shutdown);

  if (enabled_) {
    enabled_ = false;

#if defined(SAFE_BROWSING_DB_LOCAL)
    // This cancels all in-flight GetHash requests. Note that
    // |database_manager_| relies on |protocol_manager_| so if the latter is
    // destroyed, the former must be stopped.
    protocol_manager_.reset();
#endif
    ping_manager_.reset();
  }
}

void SafeBrowsingService::Start() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  BrowserThread::PostTask(
      BrowserThread::IO, FROM_HERE,
      base::BindOnce(&SafeBrowsingService::StartOnIOThread, this,
                     base::RetainedRef(url_request_context_getter_)));
}

void SafeBrowsingService::Stop(bool shutdown) {
  BrowserThread::PostTask(
      BrowserThread::IO, FROM_HERE,
      base::BindOnce(&SafeBrowsingService::StopOnIOThread, this, shutdown));
}

void SafeBrowsingService::Observe(int type,
                                  const content::NotificationSource& source,
                                  const content::NotificationDetails& details) {
  switch (type) {
    case chrome::NOTIFICATION_PROFILE_CREATED: {
      DCHECK_CURRENTLY_ON(BrowserThread::UI);
      Profile* profile = content::Source<Profile>(source).ptr();
      CreatePasswordProtectionService(profile);
      if (!profile->IsOffTheRecord())
        AddPrefService(profile->GetPrefs());
      break;
    }
    case chrome::NOTIFICATION_PROFILE_DESTROYED: {
      DCHECK_CURRENTLY_ON(BrowserThread::UI);
      Profile* profile = content::Source<Profile>(source).ptr();
      RemovePasswordProtectionService(profile);
      if (!profile->IsOffTheRecord())
        RemovePrefService(profile->GetPrefs());
      break;
    }
    default:
      NOTREACHED();
  }
}

void SafeBrowsingService::AddPrefService(PrefService* pref_service) {
  DCHECK(prefs_map_.find(pref_service) == prefs_map_.end());
  std::unique_ptr<PrefChangeRegistrar> registrar =
      base::MakeUnique<PrefChangeRegistrar>();
  registrar->Init(pref_service);
  registrar->Add(
      prefs::kSafeBrowsingEnabled,
      base::Bind(&SafeBrowsingService::RefreshState, base::Unretained(this)));
  // ClientSideDetectionService will need to be refresh the models
  // renderers have if extended-reporting changes.
  registrar->Add(
      prefs::kSafeBrowsingExtendedReportingEnabled,
      base::Bind(&SafeBrowsingService::RefreshState, base::Unretained(this)));
  registrar->Add(
      prefs::kSafeBrowsingScoutReportingEnabled,
      base::Bind(&SafeBrowsingService::RefreshState, base::Unretained(this)));
  prefs_map_[pref_service] = std::move(registrar);
  RefreshState();

  // Initialize SafeBrowsing prefs on startup.
  InitializeSafeBrowsingPrefs(pref_service);

  // Record the current pref state.
  UMA_HISTOGRAM_BOOLEAN("SafeBrowsing.Pref.General",
                        pref_service->GetBoolean(prefs::kSafeBrowsingEnabled));
  // Extended Reporting metrics are handled together elsewhere.
  RecordExtendedReportingMetrics(*pref_service);
}

void SafeBrowsingService::RemovePrefService(PrefService* pref_service) {
  if (prefs_map_.find(pref_service) != prefs_map_.end()) {
    prefs_map_.erase(pref_service);
    RefreshState();
  } else {
    NOTREACHED();
  }
}

std::unique_ptr<SafeBrowsingService::StateSubscription>
SafeBrowsingService::RegisterStateCallback(
    const base::Callback<void(void)>& callback) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  return state_callback_list_.Add(callback);
}

std::unique_ptr<SafeBrowsingService::ShutdownSubscription>
SafeBrowsingService::RegisterShutdownCallback(
    const base::Callback<void(void)>& callback) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  return shutdown_callback_list_.Add(callback);
}

void SafeBrowsingService::RefreshState() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  // Check if any profile requires the service to be active.
  enabled_by_prefs_ = false;
  estimated_extended_reporting_by_prefs_ = SBER_LEVEL_OFF;
  for (const auto& pref : prefs_map_) {
    if (pref.first->GetBoolean(prefs::kSafeBrowsingEnabled)) {
      enabled_by_prefs_ = true;
      ExtendedReportingLevel erl =
          safe_browsing::GetExtendedReportingLevel(*pref.first);
      if (erl != SBER_LEVEL_OFF) {
        estimated_extended_reporting_by_prefs_ = erl;
        break;
      }
    }
  }

  if (enabled_by_prefs_)
    Start();
  else
    Stop(false);

  state_callback_list_.Notify();

  services_delegate_->RefreshState(enabled_by_prefs_);
}

void SafeBrowsingService::SendSerializedDownloadReport(
    const std::string& report) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  BrowserThread::PostTask(
      BrowserThread::IO, FROM_HERE,
      base::BindOnce(&SafeBrowsingService::OnSendSerializedDownloadReport, this,
                     report));
}

void SafeBrowsingService::OnSendSerializedDownloadReport(
    const std::string& report) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  if (ping_manager())
    ping_manager()->ReportThreatDetails(report);
}

void SafeBrowsingService::ProcessResourceRequest(
    const ResourceRequestInfo& request) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  services_delegate_->ProcessResourceRequest(&request);
}

void SafeBrowsingService::CreatePasswordProtectionService(Profile* profile) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  DCHECK(profile);
  auto it = password_protection_service_map_.find(profile);
  DCHECK(it == password_protection_service_map_.end());
  std::unique_ptr<ChromePasswordProtectionService> service =
      base::MakeUnique<ChromePasswordProtectionService>(this, profile);
  password_protection_service_map_[profile] = std::move(service);
}

void SafeBrowsingService::RemovePasswordProtectionService(Profile* profile) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  DCHECK(profile);
  auto it = password_protection_service_map_.find(profile);
  if (it != password_protection_service_map_.end())
    password_protection_service_map_.erase(it);
}

void SafeBrowsingService::CreateTriggerManager() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  trigger_manager_ = base::MakeUnique<TriggerManager>(ui_manager_.get());
}
}  // namespace safe_browsing
