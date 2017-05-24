// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ios/chrome/browser/ios_chrome_io_thread.h"

#include <stddef.h>

#include <utility>
#include <vector>

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/command_line.h"
#include "base/compiler_specific.h"
#include "base/debug/leak_tracker.h"
#include "base/environment.h"
#include "base/logging.h"
#include "base/macros.h"
#include "base/memory/ptr_util.h"
#include "base/metrics/field_trial.h"
#include "base/single_thread_task_runner.h"
#include "base/stl_util.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/threading/sequenced_worker_pool.h"
#include "base/threading/thread.h"
#include "base/time/time.h"
#include "base/trace_event/trace_event.h"
#include "components/net_log/chrome_net_log.h"
#include "components/network_session_configurator/network_session_configurator.h"
#include "components/prefs/pref_service.h"
#include "components/proxy_config/ios/proxy_service_factory.h"
#include "components/proxy_config/pref_proxy_config_tracker.h"
#include "components/variations/variations_associated_data.h"
#include "components/version_info/version_info.h"
#include "ios/chrome/browser/chrome_switches.h"
#include "ios/chrome/browser/net/cookie_util.h"
#include "ios/chrome/browser/net/ios_chrome_network_delegate.h"
#include "ios/chrome/common/channel_info.h"
#include "ios/web/public/user_agent.h"
#include "ios/web/public/web_client.h"
#include "ios/web/public/web_thread.h"
#include "net/base/sdch_manager.h"
#include "net/cert/cert_verifier.h"
#include "net/cert/ct_known_logs.h"
#include "net/cert/ct_log_verifier.h"
#include "net/cert/ct_policy_enforcer.h"
#include "net/cert/ct_verifier.h"
#include "net/cert/multi_log_ct_verifier.h"
#include "net/cert/multi_threaded_cert_verifier.h"
#include "net/cookies/cookie_monster.h"
#include "net/cookies/cookie_store.h"
#include "net/dns/host_cache.h"
#include "net/dns/host_resolver.h"
#include "net/dns/mapped_host_resolver.h"
#include "net/http/http_auth_filter.h"
#include "net/http/http_auth_handler_factory.h"
#include "net/http/http_auth_preferences.h"
#include "net/http/http_network_layer.h"
#include "net/http/http_server_properties_impl.h"
#include "net/log/net_log_event_type.h"
#include "net/nqe/external_estimate_provider.h"
#include "net/nqe/network_quality_estimator.h"
#include "net/proxy/proxy_config_service.h"
#include "net/proxy/proxy_script_fetcher_impl.h"
#include "net/proxy/proxy_service.h"
#include "net/socket/tcp_client_socket.h"
#include "net/spdy/chromium/spdy_session.h"
#include "net/ssl/channel_id_service.h"
#include "net/ssl/default_channel_id_store.h"
#include "net/url_request/data_protocol_handler.h"
#include "net/url_request/file_protocol_handler.h"
#include "net/url_request/static_http_user_agent_settings.h"
#include "net/url_request/url_request_context.h"
#include "net/url_request/url_request_context_builder.h"
#include "net/url_request/url_request_context_getter.h"
#include "net/url_request/url_request_job_factory_impl.h"
#include "url/url_constants.h"

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

// The IOSChromeIOThread object must outlive any tasks posted to the IO thread
// before the Quit task, so base::Bind() calls are not refcounted.

namespace {

const char kSupportedAuthSchemes[] = "basic,digest,ntlm";

// Field trial for network quality estimator. Seeds RTT and downstream
// throughput observations with values that correspond to the connection type
// determined by the operating system.
const char kNetworkQualityEstimatorFieldTrialName[] = "NetworkQualityEstimator";

// Used for the "system" URLRequestContext.
class SystemURLRequestContext : public net::URLRequestContext {
 public:
  SystemURLRequestContext() {
  }

 private:
  ~SystemURLRequestContext() override {
    AssertNoURLRequests();
  }
};

std::unique_ptr<net::HostResolver> CreateGlobalHostResolver(
    net::NetLog* net_log) {
  TRACE_EVENT0("startup", "IOSChromeIOThread::CreateGlobalHostResolver");
  const base::CommandLine& command_line =
      *base::CommandLine::ForCurrentProcess();

  std::unique_ptr<net::HostResolver> global_host_resolver =
      net::HostResolver::CreateSystemResolver(net::HostResolver::Options(),
                                              net_log);

  // If hostname remappings were specified on the command-line, layer these
  // rules on top of the real host resolver. This allows forwarding all requests
  // through a designated test server.
  if (!command_line.HasSwitch(switches::kIOSHostResolverRules))
    return global_host_resolver;

  std::unique_ptr<net::MappedHostResolver> remapped_resolver(
      new net::MappedHostResolver(std::move(global_host_resolver)));
  remapped_resolver->SetRulesFromString(
      command_line.GetSwitchValueASCII(switches::kIOSHostResolverRules));
  // TODO(crbug.com/703565): remove std::move() once Xcode 9.0+ is required.
  return std::move(remapped_resolver);
}

int GetSwitchValueAsInt(const base::CommandLine& command_line,
                        const std::string& switch_name) {
  int value;
  if (!base::StringToInt(command_line.GetSwitchValueASCII(switch_name),
                         &value)) {
    return 0;
  }
  return value;
}

}  // namespace

class IOSChromeIOThread::LoggingNetworkChangeObserver
    : public net::NetworkChangeNotifier::IPAddressObserver,
      public net::NetworkChangeNotifier::ConnectionTypeObserver,
      public net::NetworkChangeNotifier::NetworkChangeObserver {
 public:
  // |net_log| must remain valid throughout our lifetime.
  explicit LoggingNetworkChangeObserver(net::NetLog* net_log)
      : net_log_(net_log) {
    net::NetworkChangeNotifier::AddIPAddressObserver(this);
    net::NetworkChangeNotifier::AddConnectionTypeObserver(this);
    net::NetworkChangeNotifier::AddNetworkChangeObserver(this);
  }

  ~LoggingNetworkChangeObserver() override {
    net::NetworkChangeNotifier::RemoveIPAddressObserver(this);
    net::NetworkChangeNotifier::RemoveConnectionTypeObserver(this);
    net::NetworkChangeNotifier::RemoveNetworkChangeObserver(this);
  }

  // NetworkChangeNotifier::IPAddressObserver implementation.
  void OnIPAddressChanged() override {
    VLOG(1) << "Observed a change to the network IP addresses";

    net_log_->AddGlobalEntry(
        net::NetLogEventType::NETWORK_IP_ADDRESSES_CHANGED);
  }

  // NetworkChangeNotifier::ConnectionTypeObserver implementation.
  void OnConnectionTypeChanged(
      net::NetworkChangeNotifier::ConnectionType type) override {
    std::string type_as_string =
        net::NetworkChangeNotifier::ConnectionTypeToString(type);

    VLOG(1) << "Observed a change to network connectivity state "
            << type_as_string;

    net_log_->AddGlobalEntry(
        net::NetLogEventType::NETWORK_CONNECTIVITY_CHANGED,
        net::NetLog::StringCallback("new_connection_type", &type_as_string));
  }

  // NetworkChangeNotifier::NetworkChangeObserver implementation.
  void OnNetworkChanged(
      net::NetworkChangeNotifier::ConnectionType type) override {
    std::string type_as_string =
        net::NetworkChangeNotifier::ConnectionTypeToString(type);

    VLOG(1) << "Observed a network change to state " << type_as_string;

    net_log_->AddGlobalEntry(
        net::NetLogEventType::NETWORK_CHANGED,
        net::NetLog::StringCallback("new_connection_type", &type_as_string));
  }

 private:
  net::NetLog* net_log_;
  DISALLOW_COPY_AND_ASSIGN(LoggingNetworkChangeObserver);
};

class SystemURLRequestContextGetter : public net::URLRequestContextGetter {
 public:
  explicit SystemURLRequestContextGetter(IOSChromeIOThread* io_thread);

  // Implementation for net::UrlRequestContextGetter.
  net::URLRequestContext* GetURLRequestContext() override;
  scoped_refptr<base::SingleThreadTaskRunner> GetNetworkTaskRunner()
      const override;

  // Tells the getter that the URLRequestContext is about to be shut down.
  void Shutdown();

 protected:
  ~SystemURLRequestContextGetter() override;

 private:
  IOSChromeIOThread* io_thread_;  // Weak pointer, owned by ApplicationContext.
  scoped_refptr<base::SingleThreadTaskRunner> network_task_runner_;

  base::debug::LeakTracker<SystemURLRequestContextGetter> leak_tracker_;
};

SystemURLRequestContextGetter::SystemURLRequestContextGetter(
    IOSChromeIOThread* io_thread)
    : io_thread_(io_thread),
      network_task_runner_(
          web::WebThread::GetTaskRunnerForThread(web::WebThread::IO)) {}

SystemURLRequestContextGetter::~SystemURLRequestContextGetter() {}

net::URLRequestContext* SystemURLRequestContextGetter::GetURLRequestContext() {
  DCHECK_CURRENTLY_ON(web::WebThread::IO);
  if (!io_thread_)
    return nullptr;
  DCHECK(io_thread_->globals()->system_request_context.get());

  return io_thread_->globals()->system_request_context.get();
}

scoped_refptr<base::SingleThreadTaskRunner>
SystemURLRequestContextGetter::GetNetworkTaskRunner() const {
  return network_task_runner_;
}

void SystemURLRequestContextGetter::Shutdown() {
  DCHECK_CURRENTLY_ON(web::WebThread::IO);
  io_thread_ = nullptr;
  NotifyContextShuttingDown();
}

IOSChromeIOThread::Globals::SystemRequestContextLeakChecker::
    SystemRequestContextLeakChecker(Globals* globals)
    : globals_(globals) {
  DCHECK(globals_);
}

IOSChromeIOThread::Globals::SystemRequestContextLeakChecker::
    ~SystemRequestContextLeakChecker() {
  if (globals_->system_request_context.get())
    globals_->system_request_context->AssertNoURLRequests();
}

IOSChromeIOThread::Globals::Globals()
    : system_request_context_leak_checker(this) {}

IOSChromeIOThread::Globals::~Globals() {}

// |local_state| is passed in explicitly in order to (1) reduce implicit
// dependencies and (2) make IOSChromeIOThread more flexible for testing.
IOSChromeIOThread::IOSChromeIOThread(PrefService* local_state,
                                     net_log::ChromeNetLog* net_log)
    : net_log_(net_log),
      globals_(nullptr),
      creation_time_(base::TimeTicks::Now()),
      weak_factory_(this) {
  pref_proxy_config_tracker_ =
      ProxyServiceFactory::CreatePrefProxyConfigTrackerOfLocalState(
          local_state);
  IOSChromeNetworkDelegate::InitializePrefsOnUIThread(nullptr, local_state);
  ssl_config_service_manager_.reset(
      ssl_config::SSLConfigServiceManager::CreateDefaultManager(
          local_state,
          web::WebThread::GetTaskRunnerForThread(web::WebThread::IO)));

  web::WebThread::SetDelegate(web::WebThread::IO, this);
}

IOSChromeIOThread::~IOSChromeIOThread() {
  // This isn't needed for production code, but in tests, IOSChromeIOThread may
  // be multiply constructed.
  web::WebThread::SetDelegate(web::WebThread::IO, nullptr);

  pref_proxy_config_tracker_->DetachFromPrefService();
  DCHECK(!globals_);
}

IOSChromeIOThread::Globals* IOSChromeIOThread::globals() {
  DCHECK_CURRENTLY_ON(web::WebThread::IO);
  return globals_;
}

void IOSChromeIOThread::SetGlobalsForTesting(Globals* globals) {
  DCHECK_CURRENTLY_ON(web::WebThread::IO);
  DCHECK(!globals || !globals_);
  globals_ = globals;
}

net_log::ChromeNetLog* IOSChromeIOThread::net_log() {
  return net_log_;
}

void IOSChromeIOThread::ChangedToOnTheRecord() {
  DCHECK_CURRENTLY_ON(web::WebThread::UI);
  web::WebThread::PostTask(
      web::WebThread::IO, FROM_HERE,
      base::Bind(&IOSChromeIOThread::ChangedToOnTheRecordOnIOThread,
                 base::Unretained(this)));
}

net::URLRequestContextGetter*
IOSChromeIOThread::system_url_request_context_getter() {
  DCHECK_CURRENTLY_ON(web::WebThread::UI);
  if (!system_url_request_context_getter_.get()) {
    InitSystemRequestContext();
  }
  return system_url_request_context_getter_.get();
}

void IOSChromeIOThread::Init() {
  TRACE_EVENT0("startup", "IOSChromeIOThread::Init");
  DCHECK_CURRENTLY_ON(web::WebThread::IO);

  const base::CommandLine& command_line =
      *base::CommandLine::ForCurrentProcess();

  DCHECK(!globals_);
  globals_ = new Globals;

  // Add an observer that will emit network change events to the ChromeNetLog.
  // Assuming NetworkChangeNotifier dispatches in FIFO order, we should be
  // logging the network change before other IO thread consumers respond to it.
  network_change_observer_.reset(new LoggingNetworkChangeObserver(net_log_));

  // Setup the HistogramWatcher to run on the IO thread.
  net::NetworkChangeNotifier::InitHistogramWatcher();

  std::unique_ptr<IOSChromeNetworkDelegate> chrome_network_delegate(
      new IOSChromeNetworkDelegate());

  globals_->system_network_delegate = std::move(chrome_network_delegate);
  globals_->host_resolver = CreateGlobalHostResolver(net_log_);

  std::map<std::string, std::string> network_quality_estimator_params;
  variations::GetVariationParams(kNetworkQualityEstimatorFieldTrialName,
                                 &network_quality_estimator_params);

  std::unique_ptr<net::ExternalEstimateProvider> external_estimate_provider;
  // Pass ownership.
  globals_->network_quality_estimator.reset(new net::NetworkQualityEstimator(
      std::move(external_estimate_provider), network_quality_estimator_params,
      net_log_));

  globals_->cert_verifier = net::CertVerifier::CreateDefault();

  globals_->transport_security_state.reset(new net::TransportSecurityState());

  std::vector<scoped_refptr<const net::CTLogVerifier>> ct_logs(
      net::ct::CreateLogVerifiersForKnownLogs());

  net::MultiLogCTVerifier* ct_verifier = new net::MultiLogCTVerifier();
  globals_->cert_transparency_verifier.reset(ct_verifier);
  // Add built-in logs
  ct_verifier->AddLogs(ct_logs);

  globals_->ct_policy_enforcer.reset(new net::CTPolicyEnforcer());

  globals_->ssl_config_service = GetSSLConfigService();

  CreateDefaultAuthHandlerFactory();
  globals_->http_server_properties.reset(new net::HttpServerPropertiesImpl());
  // In-memory cookie store.
  globals_->system_cookie_store.reset(new net::CookieMonster(nullptr, nullptr));
  // In-memory channel ID store.
  globals_->system_channel_id_service.reset(
      new net::ChannelIDService(new net::DefaultChannelIDStore(nullptr)));
  globals_->system_cookie_store->SetChannelIDServiceID(
      globals_->system_channel_id_service->GetUniqueID());
  globals_->http_user_agent_settings.reset(new net::StaticHttpUserAgentSettings(
      std::string(),
      web::GetWebClient()->GetUserAgent(web::UserAgentType::MOBILE)));
  if (command_line.HasSwitch(switches::kIOSTestingFixedHttpPort)) {
    params_.testing_fixed_http_port =
        GetSwitchValueAsInt(command_line, switches::kIOSTestingFixedHttpPort);
  }
  if (command_line.HasSwitch(switches::kIOSTestingFixedHttpsPort)) {
    params_.testing_fixed_https_port =
        GetSwitchValueAsInt(command_line, switches::kIOSTestingFixedHttpsPort);
  }

  params_.ignore_certificate_errors = false;
  params_.enable_user_alternate_protocol_ports = false;

  std::string quic_user_agent_id = ::GetChannelString();
  if (!quic_user_agent_id.empty())
    quic_user_agent_id.push_back(' ');
  quic_user_agent_id.append(
      version_info::GetProductNameAndVersionForUserAgent());
  quic_user_agent_id.push_back(' ');
  quic_user_agent_id.append(web::BuildOSCpuInfo());

  network_session_configurator::ParseFieldTrials(
      /*is_quic_force_disabled=*/false,
      /*is_quic_force_enabled=*/false, quic_user_agent_id, &params_);

  // InitSystemRequestContext turns right around and posts a task back
  // to the IO thread, so we can't let it run until we know the IO
  // thread has started.
  //
  // Note that since we are at WebThread::Init time, the UI thread
  // is blocked waiting for the thread to start.  Therefore, posting
  // this task to the main thread's message loop here is guaranteed to
  // get it onto the message loop while the IOSChromeIOThread object still
  // exists.  However, the message might not be processed on the UI
  // thread until after IOSChromeIOThread is gone, so use a weak pointer.
  web::WebThread::PostTask(
      web::WebThread::UI, FROM_HERE,
      base::Bind(&IOSChromeIOThread::InitSystemRequestContext,
                 weak_factory_.GetWeakPtr()));
}

void IOSChromeIOThread::CleanUp() {
  system_url_request_context_getter_->Shutdown();
  system_url_request_context_getter_ = nullptr;

  // Release objects that the net::URLRequestContext could have been pointing
  // to.

  // Shutdown the HistogramWatcher on the IO thread.
  net::NetworkChangeNotifier::ShutdownHistogramWatcher();

  // This must be reset before the ChromeNetLog is destroyed.
  network_change_observer_.reset();

  system_proxy_config_service_.reset();

  delete globals_;
  globals_ = nullptr;

  base::debug::LeakTracker<SystemURLRequestContextGetter>::CheckForLeaks();
}

void IOSChromeIOThread::CreateDefaultAuthHandlerFactory() {
  std::vector<std::string> supported_schemes =
      base::SplitString(kSupportedAuthSchemes, ",", base::TRIM_WHITESPACE,
                        base::SPLIT_WANT_NONEMPTY);
  globals_->http_auth_preferences.reset(
      new net::HttpAuthPreferences(supported_schemes, std::string()));
  globals_->http_auth_handler_factory =
      net::HttpAuthHandlerRegistryFactory::Create(
          globals_->http_auth_preferences.get(), globals_->host_resolver.get());
}

void IOSChromeIOThread::ClearHostCache() {
  DCHECK_CURRENTLY_ON(web::WebThread::IO);

  net::HostCache* host_cache = globals_->host_resolver->GetHostCache();
  if (host_cache)
    host_cache->clear();
}

const net::HttpNetworkSession::Params& IOSChromeIOThread::NetworkSessionParams()
    const {
  return params_;
}

base::TimeTicks IOSChromeIOThread::creation_time() const {
  return creation_time_;
}

net::SSLConfigService* IOSChromeIOThread::GetSSLConfigService() {
  return ssl_config_service_manager_->Get();
}

void IOSChromeIOThread::ChangedToOnTheRecordOnIOThread() {
  DCHECK_CURRENTLY_ON(web::WebThread::IO);

  // Clear the host cache to avoid showing entries from the OTR session
  // in about:net-internals.
  ClearHostCache();
}

void IOSChromeIOThread::InitSystemRequestContext() {
  if (system_url_request_context_getter_.get())
    return;
  // If we're in unit_tests, IOSChromeIOThread may not be run.
  if (!web::WebThread::IsMessageLoopValid(web::WebThread::IO))
    return;
  system_proxy_config_service_ = ProxyServiceFactory::CreateProxyConfigService(
      pref_proxy_config_tracker_.get());

  system_url_request_context_getter_ = new SystemURLRequestContextGetter(this);
  // Safe to post an unretained this pointer, since IOSChromeIOThread is
  // guaranteed to outlive the IO WebThread.
  web::WebThread::PostTask(
      web::WebThread::IO, FROM_HERE,
      base::Bind(&IOSChromeIOThread::InitSystemRequestContextOnIOThread,
                 base::Unretained(this)));
}

void IOSChromeIOThread::InitSystemRequestContextOnIOThread() {
  DCHECK_CURRENTLY_ON(web::WebThread::IO);
  DCHECK(!globals_->system_proxy_service.get());
  DCHECK(system_proxy_config_service_.get());

  globals_->system_proxy_service = ProxyServiceFactory::CreateProxyService(
      net_log_, nullptr, globals_->system_network_delegate.get(),
      std::move(system_proxy_config_service_), true /* quick_check_enabled */);

  globals_->system_request_context.reset(
      ConstructSystemRequestContext(globals_, params_, net_log_));
}

net::URLRequestContext* IOSChromeIOThread::ConstructSystemRequestContext(
    IOSChromeIOThread::Globals* globals,
    const net::HttpNetworkSession::Params& params,
    net::NetLog* net_log) {
  net::URLRequestContext* context = new SystemURLRequestContext;
  context->set_net_log(net_log);
  context->set_host_resolver(globals->host_resolver.get());
  context->set_cert_verifier(globals->cert_verifier.get());
  context->set_transport_security_state(
      globals->transport_security_state.get());
  context->set_cert_transparency_verifier(
      globals->cert_transparency_verifier.get());
  context->set_ssl_config_service(globals->ssl_config_service.get());
  context->set_http_auth_handler_factory(
      globals->http_auth_handler_factory.get());
  context->set_proxy_service(globals->system_proxy_service.get());
  context->set_ct_policy_enforcer(globals->ct_policy_enforcer.get());

  net::URLRequestJobFactoryImpl* system_job_factory =
      new net::URLRequestJobFactoryImpl();
  // Data URLs are always loaded through the system request context on iOS
  // (due to UIWebView limitations).
  bool set_protocol = system_job_factory->SetProtocolHandler(
      url::kDataScheme, base::MakeUnique<net::DataProtocolHandler>());
  DCHECK(set_protocol);
  globals->system_url_request_job_factory.reset(system_job_factory);
  context->set_job_factory(globals->system_url_request_job_factory.get());

  context->set_cookie_store(globals->system_cookie_store.get());
  context->set_channel_id_service(globals->system_channel_id_service.get());
  context->set_network_delegate(globals->system_network_delegate.get());
  context->set_http_user_agent_settings(
      globals->http_user_agent_settings.get());
  context->set_network_quality_estimator(
      globals->network_quality_estimator.get());

  context->set_http_server_properties(globals->http_server_properties.get());

  net::HttpNetworkSession::Params system_params(params);
  net::URLRequestContextBuilder::SetHttpNetworkSessionComponents(
      context, &system_params);

  globals->system_http_network_session.reset(
      new net::HttpNetworkSession(system_params));
  globals->system_http_transaction_factory.reset(
      new net::HttpNetworkLayer(globals->system_http_network_session.get()));
  context->set_http_transaction_factory(
      globals->system_http_transaction_factory.get());

  return context;
}
