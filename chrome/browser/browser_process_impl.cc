// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/browser_process_impl.h"

#include <stddef.h>

#include <algorithm>
#include <map>
#include <utility>
#include <vector>

#include "base/atomic_ref_count.h"
#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/command_line.h"
#include "base/debug/crash_logging.h"
#include "base/debug/leak_annotations.h"
#include "base/files/file_path.h"
#include "base/location.h"
#include "base/macros.h"
#include "base/memory/ptr_util.h"
#include "base/message_loop/message_loop.h"
#include "base/metrics/histogram_macros.h"
#include "base/path_service.h"
#include "base/run_loop.h"
#include "base/single_thread_task_runner.h"
#include "base/synchronization/waitable_event.h"
#include "base/task_scheduler/post_task.h"
#include "base/threading/thread.h"
#include "base/threading/thread_restrictions.h"
#include "base/threading/thread_task_runner_handle.h"
#include "base/time/default_clock.h"
#include "base/time/default_tick_clock.h"
#include "base/trace_event/trace_event.h"
#include "build/build_config.h"
#include "chrome/browser/chrome_browser_main.h"
#include "chrome/browser/chrome_child_process_watcher.h"
#include "chrome/browser/chrome_content_browser_client.h"
#include "chrome/browser/chrome_device_client.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/component_updater/chrome_component_updater_configurator.h"
#include "chrome/browser/component_updater/supervised_user_whitelist_installer.h"
#include "chrome/browser/defaults.h"
#include "chrome/browser/devtools/devtools_auto_opener.h"
#include "chrome/browser/devtools/remote_debugging_server.h"
#include "chrome/browser/download/download_request_limiter.h"
#include "chrome/browser/download/download_status_updater.h"
#include "chrome/browser/gpu/gpu_mode_manager.h"
#include "chrome/browser/gpu/gpu_profile_cache.h"
#include "chrome/browser/icon_manager.h"
#include "chrome/browser/intranet_redirect_detector.h"
#include "chrome/browser/io_thread.h"
#include "chrome/browser/lifetime/application_lifetime.h"
#include "chrome/browser/loader/chrome_resource_dispatcher_host_delegate.h"
#include "chrome/browser/metrics/chrome_metrics_service_accessor.h"
#include "chrome/browser/metrics/chrome_metrics_services_manager_client.h"
#include "chrome/browser/metrics/thread_watcher.h"
#include "chrome/browser/net/chrome_net_log_helper.h"
#include "chrome/browser/net/crl_set_fetcher.h"
#include "chrome/browser/notifications/notification_platform_bridge.h"
#include "chrome/browser/notifications/notification_ui_manager.h"
#include "chrome/browser/plugins/chrome_plugin_service_filter.h"
#include "chrome/browser/plugins/plugin_finder.h"
#include "chrome/browser/prefs/browser_prefs.h"
#include "chrome/browser/prefs/chrome_pref_service_factory.h"
#include "chrome/browser/printing/background_printing_manager.h"
#include "chrome/browser/printing/print_job_manager.h"
#include "chrome/browser/printing/print_preview_dialog_controller.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/safe_browsing/safe_browsing_service.h"
#include "chrome/browser/shell_integration.h"
#include "chrome/browser/status_icons/status_tray.h"
#include "chrome/browser/ui/browser_dialogs.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/update_client/chrome_update_query_params_delegate.h"
#include "chrome/common/channel_info.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/common/chrome_features.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/crash_keys.h"
#include "chrome/common/extensions/chrome_extensions_client.h"
#include "chrome/common/extensions/extension_process_policy.h"
#include "chrome/common/features.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/switch_utils.h"
#include "chrome/common/url_constants.h"
#include "chrome/installer/util/google_update_settings.h"
#include "components/component_updater/component_updater_service.h"
#include "components/gcm_driver/gcm_driver.h"
#include "components/metrics/metrics_pref_names.h"
#include "components/metrics/metrics_service.h"
#include "components/metrics_services_manager/metrics_services_manager.h"
#include "components/net_log/chrome_net_log.h"
#include "components/network_time/network_time_tracker.h"
#include "components/physical_web/data_source/physical_web_data_source.h"
#include "components/policy/core/browser/browser_policy_connector.h"
#include "components/policy/core/common/policy_service.h"
#include "components/prefs/json_pref_store.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"
#include "components/rappor/public/rappor_utils.h"
#include "components/rappor/rappor_service_impl.h"
#include "components/safe_json/safe_json_parser.h"
#include "components/signin/core/common/profile_management_switches.h"
#include "components/subresource_filter/content/browser/content_ruleset_service.h"
#include "components/subresource_filter/core/browser/ruleset_service.h"
#include "components/subresource_filter/core/browser/subresource_filter_constants.h"
#include "components/subresource_filter/core/browser/subresource_filter_features.h"
#include "components/translate/core/browser/translate_download_manager.h"
#include "components/update_client/update_query_params.h"
#include "components/web_resource/web_resource_pref_names.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/child_process_security_policy.h"
#include "content/public/browser/notification_details.h"
#include "content/public/browser/plugin_service.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/resource_dispatcher_host.h"
#include "content/public/browser/service_worker_context.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/common/content_switches.h"
#include "extensions/common/constants.h"
#include "extensions/features/features.h"
#include "media/media_features.h"
#include "net/socket/client_socket_pool_manager.h"
#include "net/url_request/url_request_context_getter.h"
#include "ppapi/features/features.h"
#include "printing/features/features.h"
#include "ui/base/idle/idle.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/message_center/message_center.h"

#if defined(OS_WIN)
#include "base/win/windows_version.h"
#include "ui/views/focus/view_storage.h"
#elif defined(OS_MACOSX)
#include "chrome/browser/chrome_browser_main_mac.h"
#endif

#if !defined(OS_ANDROID)
#include "chrome/browser/gcm/gcm_product_util.h"
#include "chrome/browser/lifetime/keep_alive_registry.h"
#include "chrome/browser/ui/user_manager.h"
#include "components/gcm_driver/gcm_client_factory.h"
#include "components/gcm_driver/gcm_desktop_utils.h"
#endif

#if BUILDFLAG(ENABLE_BACKGROUND)
#include "chrome/browser/background/background_mode_manager.h"
#endif

#if BUILDFLAG(ENABLE_EXTENSIONS)
#include "chrome/browser/extensions/chrome_extensions_browser_client.h"
#include "chrome/browser/extensions/event_router_forwarder.h"
#include "chrome/browser/media_galleries/media_file_system_registry.h"
#include "chrome/browser/ui/apps/chrome_app_window_client.h"
#include "components/storage_monitor/storage_monitor.h"
#include "extensions/common/extension_l10n_util.h"
#endif

#if !defined(DISABLE_NACL)
#include "chrome/browser/component_updater/pnacl_component_installer.h"
#endif

#if BUILDFLAG(ENABLE_PLUGINS)
#include "chrome/browser/plugins/plugins_resource_service.h"
#endif

#if BUILDFLAG(ENABLE_WEBRTC)
#include "chrome/browser/media/webrtc/webrtc_log_uploader.h"
#endif

#if defined(OS_WIN) || defined(OS_MACOSX) || defined(OS_LINUX)
#include "chrome/browser/memory/tab_manager.h"
#endif

#if !defined(OS_ANDROID) && !defined(OS_CHROMEOS)
#include "chrome/browser/first_run/upgrade_util.h"
#endif

#if defined(OS_ANDROID)
#include "chrome/browser/android/physical_web/physical_web_data_source_android.h"
#endif

#if (defined(OS_WIN) || defined(OS_LINUX)) && !defined(OS_CHROMEOS)
// How often to check if the persistent instance of Chrome needs to restart
// to install an update.
static const int kUpdateCheckIntervalHours = 6;
#endif

#if defined(USE_X11) || defined(OS_WIN) || defined(USE_OZONE)
// How long to wait for the File thread to complete during EndSession, on Linux
// and Windows. We have a timeout here because we're unable to run the UI
// messageloop and there's some deadlock risk. Our only option is to exit
// anyway.
static constexpr base::TimeDelta kEndSessionTimeout =
    base::TimeDelta::FromSeconds(10);
#endif

using content::BrowserThread;
using content::ChildProcessSecurityPolicy;
using content::PluginService;
using content::ResourceDispatcherHost;

rappor::RapporService* GetBrowserRapporService() {
  if (g_browser_process != nullptr)
    return g_browser_process->rappor_service();
  return nullptr;
}

BrowserProcessImpl::BrowserProcessImpl(
    base::SequencedTaskRunner* local_state_task_runner,
    const base::CommandLine& command_line)
    : created_watchdog_thread_(false),
      created_browser_policy_connector_(false),
      created_profile_manager_(false),
      created_local_state_(false),
      created_icon_manager_(false),
      created_notification_ui_manager_(false),
      created_notification_bridge_(false),
      created_safe_browsing_service_(false),
      created_subresource_filter_ruleset_service_(false),
      shutting_down_(false),
      tearing_down_(false),
      download_status_updater_(new DownloadStatusUpdater),
      local_state_task_runner_(local_state_task_runner),
      cached_default_web_client_state_(shell_integration::UNKNOWN_DEFAULT) {
  g_browser_process = this;
  rappor::SetDefaultServiceAccessor(&GetBrowserRapporService);
  platform_part_.reset(new BrowserProcessPlatformPart());

#if BUILDFLAG(ENABLE_PRINTING)
  // Must be created after the NotificationService.
  print_job_manager_.reset(new printing::PrintJobManager);
#endif

  base::FilePath net_log_path;
  if (command_line.HasSwitch(switches::kLogNetLog))
    net_log_path = command_line.GetSwitchValuePath(switches::kLogNetLog);
  net_log_.reset(new net_log::ChromeNetLog(
      net_log_path, GetNetCaptureModeFromCommandLine(command_line),
      command_line.GetCommandLineString(), chrome::GetChannelString()));

  ChildProcessSecurityPolicy::GetInstance()->RegisterWebSafeScheme(
      chrome::kChromeSearchScheme);

#if defined(OS_MACOSX)
  ui::InitIdleMonitor();
#endif

  device_client_.reset(new ChromeDeviceClient);

#if BUILDFLAG(ENABLE_EXTENSIONS)
  // Athena sets its own instance during Athena's init process.
  extensions::AppWindowClient::Set(ChromeAppWindowClient::GetInstance());

  extension_event_router_forwarder_ = new extensions::EventRouterForwarder;

  extensions::ExtensionsClient::Set(
      extensions::ChromeExtensionsClient::GetInstance());

  extensions_browser_client_.reset(
      new extensions::ChromeExtensionsBrowserClient);
  extensions::ExtensionsBrowserClient::Set(extensions_browser_client_.get());
#endif

  message_center::MessageCenter::Initialize();

  update_client::UpdateQueryParams::SetDelegate(
      ChromeUpdateQueryParamsDelegate::GetInstance());

#if !defined(OS_ANDROID)
  KeepAliveRegistry::GetInstance()->AddObserver(this);
#endif  // !defined(OS_ANDROID)
}

BrowserProcessImpl::~BrowserProcessImpl() {
#if BUILDFLAG(ENABLE_EXTENSIONS)
  extensions::ExtensionsBrowserClient::Set(nullptr);
#endif

#if !defined(OS_ANDROID)
  KeepAliveRegistry::GetInstance()->RemoveObserver(this);
#endif  // !defined(OS_ANDROID)

  tracked_objects::ThreadData::EnsureCleanupWasCalled(4);

  g_browser_process = NULL;
}

#if !defined(OS_ANDROID)
void BrowserProcessImpl::StartTearDown() {
  TRACE_EVENT0("shutdown", "BrowserProcessImpl::StartTearDown");
  // TODO(crbug.com/560486): Fix the tests that make the check of
  // |tearing_down_| necessary in IsShuttingDown().
  tearing_down_ = true;
  DCHECK(IsShuttingDown());
  // We need to destroy the MetricsServicesManager, IntranetRedirectDetector,
  // NetworkTimeTracker, and SafeBrowsing ClientSideDetectionService
  // (owned by the SafeBrowsingService) before the io_thread_ gets destroyed,
  // since their destructors can call the URLFetcher destructor, which does a
  // PostDelayedTask operation on the IO thread. (The IO thread will handle
  // that URLFetcher operation before going away.)
  metrics_services_manager_.reset();
  intranet_redirect_detector_.reset();
  if (safe_browsing_service_.get())
    safe_browsing_service()->ShutDown();
  network_time_tracker_.reset();
#if BUILDFLAG(ENABLE_PLUGINS)
  plugins_resource_service_.reset();
#endif

  // Need to clear the desktop notification balloons before the io_thread_ and
  // before the profiles, since if there are any still showing we will access
  // those things during teardown.
  notification_ui_manager_.reset();

  // The SupervisedUserWhitelistInstaller observes the ProfileAttributesStorage,
  // so it needs to be shut down before the ProfileManager.
  supervised_user_whitelist_installer_.reset();

  // Debugger must be cleaned up before ProfileManager.
  remote_debugging_server_.reset();
  devtools_auto_opener_.reset();

  // Need to clear profiles (download managers) before the io_thread_.
  {
    TRACE_EVENT0("shutdown",
                 "BrowserProcessImpl::StartTearDown:ProfileManager");
    // The desktop User Manager needs to be closed before the guest profile
    // can be destroyed.
    UserManager::Hide();
    profile_manager_.reset();
  }

  child_process_watcher_.reset();

#if BUILDFLAG(ENABLE_EXTENSIONS)
  media_file_system_registry_.reset();
  // Remove the global instance of the Storage Monitor now. Otherwise the
  // FILE thread would be gone when we try to release it in the dtor and
  // Valgrind would report a leak on almost every single browser_test.
  // TODO(gbillock): Make this unnecessary.
  storage_monitor::StorageMonitor::Destroy();
#endif

  message_center::MessageCenter::Shutdown();

  // The policy providers managed by |browser_policy_connector_| need to shut
  // down while the IO and FILE threads are still alive. The monitoring
  // framework owned by |browser_policy_connector_| relies on |gcm_driver_|, so
  // this must be shutdown before |gcm_driver_| below.
  if (browser_policy_connector_)
    browser_policy_connector_->Shutdown();

  // The |gcm_driver_| must shut down while the IO thread is still alive.
  if (gcm_driver_)
    gcm_driver_->Shutdown();

  // Stop the watchdog thread before stopping other threads.
  watchdog_thread_.reset();

  platform_part()->StartTearDown();

#if BUILDFLAG(ENABLE_WEBRTC)
  // Cancel any uploads to release the system url request context references.
  if (webrtc_log_uploader_)
    webrtc_log_uploader_->StartShutdown();
#endif

  if (local_state())
    local_state()->CommitPendingWrite();
}

void BrowserProcessImpl::PostDestroyThreads() {
  // With the file_thread_ flushed, we can release any icon resources.
  icon_manager_.reset();

#if BUILDFLAG(ENABLE_WEBRTC)
  // Must outlive the file thread.
  webrtc_log_uploader_.reset();
#endif

  // Reset associated state right after actual thread is stopped,
  // as io_thread_.global_ cleanup happens in CleanUp on the IO
  // thread, i.e. as the thread exits its message loop.
  //
  // This is important also because in various places, the
  // IOThread object being NULL is considered synonymous with the
  // IO thread having stopped.
  io_thread_.reset();
}
#endif  // !defined(OS_ANDROID)

namespace {

// Used at the end of session to block the UI thread for completion of sentinel
// tasks on the set of threads used to persist profile data and local state.
// This is done to ensure that the data has been persisted to disk before
// continuing.
class RundownTaskCounter :
    public base::RefCountedThreadSafe<RundownTaskCounter> {
 public:
  RundownTaskCounter();

  // Posts a rundown task to |task_runner|, can be invoked an arbitrary number
  // of times before calling TimedWait.
  void Post(base::SequencedTaskRunner* task_runner);

  // Waits until the count is zero or |end_time| is reached.
  // This can only be called once per instance. Returns true if a count of zero
  // is reached or false if the |end_time| is reached. It is valid to pass an
  // |end_time| in the past.
  bool TimedWaitUntil(const base::TimeTicks& end_time);

 private:
  friend class base::RefCountedThreadSafe<RundownTaskCounter>;
  ~RundownTaskCounter() {}

  // Decrements the counter and releases the waitable event on transition to
  // zero.
  void Decrement();

  // The count starts at one to defer the possibility of one->zero transitions
  // until TimedWait is called.
  base::AtomicRefCount count_;
  base::WaitableEvent waitable_event_;

  DISALLOW_COPY_AND_ASSIGN(RundownTaskCounter);
};

RundownTaskCounter::RundownTaskCounter()
    : count_(1),
      waitable_event_(base::WaitableEvent::ResetPolicy::MANUAL,
                      base::WaitableEvent::InitialState::NOT_SIGNALED) {}

void RundownTaskCounter::Post(base::SequencedTaskRunner* task_runner) {
  // As the count starts off at one, it should never get to zero unless
  // TimedWait has been called.
  DCHECK(!base::AtomicRefCountIsZero(&count_));

  base::AtomicRefCountInc(&count_);

  // The task must be non-nestable to guarantee that it runs after all tasks
  // currently scheduled on |task_runner| have completed.
  task_runner->PostNonNestableTask(
      FROM_HERE, base::BindOnce(&RundownTaskCounter::Decrement, this));
}

void RundownTaskCounter::Decrement() {
  if (!base::AtomicRefCountDec(&count_))
    waitable_event_.Signal();
}

bool RundownTaskCounter::TimedWaitUntil(const base::TimeTicks& end_time) {
  // Decrement the excess count from the constructor.
  Decrement();

  return waitable_event_.TimedWaitUntil(end_time);
}

}  // namespace

void BrowserProcessImpl::EndSession() {
  // Mark all the profiles as clean.
  ProfileManager* pm = profile_manager();
  std::vector<Profile*> profiles(pm->GetLoadedProfiles());
  scoped_refptr<RundownTaskCounter> rundown_counter(new RundownTaskCounter());
  const bool pref_service_enabled =
      base::FeatureList::IsEnabled(features::kPrefService);
  std::vector<scoped_refptr<base::SequencedTaskRunner>> profile_writer_runners;
  for (size_t i = 0; i < profiles.size(); ++i) {
    Profile* profile = profiles[i];
    profile->SetExitType(Profile::EXIT_SESSION_ENDED);
    if (profile->GetPrefs()) {
      profile->GetPrefs()->CommitPendingWrite();
      if (pref_service_enabled) {
        rundown_counter->Post(content::BrowserThread::GetTaskRunnerForThread(
                                  content::BrowserThread::IO)
                                  .get());
        profile_writer_runners.push_back(profile->GetIOTaskRunner());
      } else {
        rundown_counter->Post(profile->GetIOTaskRunner().get());
      }
    }
  }

  // Tell the metrics service it was cleanly shutdown.
  metrics::MetricsService* metrics = g_browser_process->metrics_service();
  if (metrics && local_state()) {
    metrics->RecordStartOfSessionEnd();
#if !defined(OS_CHROMEOS)
    // MetricsService lazily writes to prefs, force it to write now.
    // On ChromeOS, chrome gets killed when hangs, so no need to
    // commit metrics::prefs::kStabilitySessionEndCompleted change immediately.
    local_state()->CommitPendingWrite();

    rundown_counter->Post(local_state_task_runner_.get());
#endif
  }

  // http://crbug.com/125207
  base::ThreadRestrictions::ScopedAllowWait allow_wait;

  // We must write that the profile and metrics service shutdown cleanly,
  // otherwise on startup we'll think we crashed. So we block until done and
  // then proceed with normal shutdown.
  //
  // If you change the condition here, be sure to also change
  // ProfileBrowserTests to match.
#if defined(USE_X11) || defined(OS_WIN) || defined(USE_OZONE)
  // Do a best-effort wait on the successful countdown of rundown tasks. Note
  // that if we don't complete "quickly enough", Windows will terminate our
  // process.
  //
  // On Windows, we previously posted a message to FILE and then ran a nested
  // message loop, waiting for that message to be processed until quitting.
  // However, doing so means that other messages will also be processed. In
  // particular, if the GPU process host notices that the GPU has been killed
  // during shutdown, it races exiting the nested loop with the process host
  // blocking the message loop attempting to re-establish a connection to the
  // GPU process synchronously. Because the system may not be allowing
  // processes to launch, this can result in a hang. See
  // http://crbug.com/318527.
  const base::TimeTicks end_time = base::TimeTicks::Now() + kEndSessionTimeout;
  const bool timed_out = !rundown_counter->TimedWaitUntil(end_time);
  if (timed_out || !pref_service_enabled)
    return;

  scoped_refptr<RundownTaskCounter> profile_write_rundown_counter(
      new RundownTaskCounter());
  for (auto& profile_writer_runner : profile_writer_runners)
    profile_write_rundown_counter->Post(profile_writer_runner.get());
  profile_write_rundown_counter->TimedWaitUntil(end_time);
#else
  NOTIMPLEMENTED();
#endif
}

metrics_services_manager::MetricsServicesManager*
BrowserProcessImpl::GetMetricsServicesManager() {
  DCHECK(CalledOnValidThread());
  if (!metrics_services_manager_) {
    metrics_services_manager_.reset(
        new metrics_services_manager::MetricsServicesManager(
            base::MakeUnique<ChromeMetricsServicesManagerClient>(
                local_state())));
  }
  return metrics_services_manager_.get();
}

metrics::MetricsService* BrowserProcessImpl::metrics_service() {
  DCHECK(CalledOnValidThread());
  return GetMetricsServicesManager()->GetMetricsService();
}

rappor::RapporServiceImpl* BrowserProcessImpl::rappor_service() {
  DCHECK(CalledOnValidThread());
  return GetMetricsServicesManager()->GetRapporServiceImpl();
}

ukm::UkmService* BrowserProcessImpl::ukm_service() {
  DCHECK(CalledOnValidThread());
  return GetMetricsServicesManager()->GetUkmService();
}

IOThread* BrowserProcessImpl::io_thread() {
  DCHECK(CalledOnValidThread());
  DCHECK(io_thread_.get());
  return io_thread_.get();
}

WatchDogThread* BrowserProcessImpl::watchdog_thread() {
  DCHECK(CalledOnValidThread());
  if (!created_watchdog_thread_)
    CreateWatchdogThread();
  DCHECK(watchdog_thread_.get() != NULL);
  return watchdog_thread_.get();
}

ProfileManager* BrowserProcessImpl::profile_manager() {
  DCHECK(CalledOnValidThread());
  if (!created_profile_manager_)
    CreateProfileManager();
  return profile_manager_.get();
}

PrefService* BrowserProcessImpl::local_state() {
  DCHECK(CalledOnValidThread());
  if (!created_local_state_)
    CreateLocalState();
  return local_state_.get();
}

net::URLRequestContextGetter* BrowserProcessImpl::system_request_context() {
  DCHECK(CalledOnValidThread());
  return io_thread()->system_url_request_context_getter();
}

variations::VariationsService* BrowserProcessImpl::variations_service() {
  DCHECK(CalledOnValidThread());
  return GetMetricsServicesManager()->GetVariationsService();
}

BrowserProcessPlatformPart* BrowserProcessImpl::platform_part() {
  return platform_part_.get();
}

extensions::EventRouterForwarder*
BrowserProcessImpl::extension_event_router_forwarder() {
#if BUILDFLAG(ENABLE_EXTENSIONS)
  return extension_event_router_forwarder_.get();
#else
  return NULL;
#endif
}

NotificationUIManager* BrowserProcessImpl::notification_ui_manager() {
  DCHECK(CalledOnValidThread());
// TODO(miguelg) return NULL for MAC as well once native notifications
// are enabled by default.
#if defined(OS_ANDROID)
  return nullptr;
#else
  if (!created_notification_ui_manager_)
    CreateNotificationUIManager();
  return notification_ui_manager_.get();
#endif
}

NotificationPlatformBridge* BrowserProcessImpl::notification_platform_bridge() {
#if BUILDFLAG(ENABLE_NATIVE_NOTIFICATIONS)
  if (!created_notification_bridge_)
    CreateNotificationPlatformBridge();
  return notification_bridge_.get();
#else
  return nullptr;
#endif
}

message_center::MessageCenter* BrowserProcessImpl::message_center() {
  DCHECK(CalledOnValidThread());
  return message_center::MessageCenter::Get();
}

policy::BrowserPolicyConnector* BrowserProcessImpl::browser_policy_connector() {
  DCHECK(CalledOnValidThread());
  if (!created_browser_policy_connector_) {
    DCHECK(!browser_policy_connector_);
    browser_policy_connector_ = platform_part_->CreateBrowserPolicyConnector();
    created_browser_policy_connector_ = true;
  }
  return browser_policy_connector_.get();
}

policy::PolicyService* BrowserProcessImpl::policy_service() {
  return browser_policy_connector()->GetPolicyService();
}

IconManager* BrowserProcessImpl::icon_manager() {
  DCHECK(CalledOnValidThread());
  if (!created_icon_manager_)
    CreateIconManager();
  return icon_manager_.get();
}

GpuProfileCache* BrowserProcessImpl::gpu_profile_cache() {
  DCHECK(CalledOnValidThread());
  if (!gpu_profile_cache_.get())
    gpu_profile_cache_.reset(GpuProfileCache::Create());
  return gpu_profile_cache_.get();
}

GpuModeManager* BrowserProcessImpl::gpu_mode_manager() {
  DCHECK(CalledOnValidThread());
  if (!gpu_mode_manager_.get())
    gpu_mode_manager_.reset(new GpuModeManager());
  return gpu_mode_manager_.get();
}

void BrowserProcessImpl::CreateDevToolsHttpProtocolHandler(
    const std::string& ip,
    uint16_t port) {
  DCHECK(CalledOnValidThread());
#if !defined(OS_ANDROID)
  // StartupBrowserCreator::LaunchBrowser can be run multiple times when browser
  // is started with several profiles or existing browser process is reused.
  if (!remote_debugging_server_.get()) {
    remote_debugging_server_.reset(new RemoteDebuggingServer(ip, port));
  }
#endif
}

void BrowserProcessImpl::CreateDevToolsAutoOpener() {
  DCHECK(CalledOnValidThread());
#if !defined(OS_ANDROID)
  // StartupBrowserCreator::LaunchBrowser can be run multiple times when browser
  // is started with several profiles or existing browser process is reused.
  if (!devtools_auto_opener_.get())
    devtools_auto_opener_.reset(new DevToolsAutoOpener());
#endif
}

bool BrowserProcessImpl::IsShuttingDown() {
  DCHECK(CalledOnValidThread());
  // TODO(crbug.com/560486): Fix the tests that make the check of
  // |tearing_down_| necessary here.
  return shutting_down_ || tearing_down_;
}

printing::PrintJobManager* BrowserProcessImpl::print_job_manager() {
  DCHECK(CalledOnValidThread());
  return print_job_manager_.get();
}

printing::PrintPreviewDialogController*
    BrowserProcessImpl::print_preview_dialog_controller() {
#if BUILDFLAG(ENABLE_PRINT_PREVIEW)
  DCHECK(CalledOnValidThread());
  if (!print_preview_dialog_controller_.get())
    CreatePrintPreviewDialogController();
  return print_preview_dialog_controller_.get();
#else
  NOTIMPLEMENTED();
  return NULL;
#endif
}

printing::BackgroundPrintingManager*
    BrowserProcessImpl::background_printing_manager() {
#if BUILDFLAG(ENABLE_PRINT_PREVIEW)
  DCHECK(CalledOnValidThread());
  if (!background_printing_manager_.get())
    CreateBackgroundPrintingManager();
  return background_printing_manager_.get();
#else
  NOTIMPLEMENTED();
  return NULL;
#endif
}

IntranetRedirectDetector* BrowserProcessImpl::intranet_redirect_detector() {
  DCHECK(CalledOnValidThread());
  if (!intranet_redirect_detector_.get())
    CreateIntranetRedirectDetector();
  return intranet_redirect_detector_.get();
}

const std::string& BrowserProcessImpl::GetApplicationLocale() {
  DCHECK(!locale_.empty());
  return locale_;
}

void BrowserProcessImpl::SetApplicationLocale(const std::string& locale) {
  locale_ = locale;
#if BUILDFLAG(ENABLE_EXTENSIONS)
  extension_l10n_util::SetProcessLocale(locale);
#endif
  ChromeContentBrowserClient::SetApplicationLocale(locale);
  translate::TranslateDownloadManager::GetInstance()->set_application_locale(
      locale);
}

DownloadStatusUpdater* BrowserProcessImpl::download_status_updater() {
  return download_status_updater_.get();
}

MediaFileSystemRegistry* BrowserProcessImpl::media_file_system_registry() {
#if BUILDFLAG(ENABLE_EXTENSIONS)
  if (!media_file_system_registry_)
    media_file_system_registry_.reset(new MediaFileSystemRegistry());
  return media_file_system_registry_.get();
#else
  return NULL;
#endif
}

bool BrowserProcessImpl::created_local_state() const {
  return created_local_state_;
}

#if BUILDFLAG(ENABLE_WEBRTC)
WebRtcLogUploader* BrowserProcessImpl::webrtc_log_uploader() {
  if (!webrtc_log_uploader_.get())
    webrtc_log_uploader_.reset(new WebRtcLogUploader());
  return webrtc_log_uploader_.get();
}
#endif

network_time::NetworkTimeTracker* BrowserProcessImpl::network_time_tracker() {
  if (!network_time_tracker_) {
    network_time_tracker_.reset(new network_time::NetworkTimeTracker(
        base::WrapUnique(new base::DefaultClock()),
        base::WrapUnique(new base::DefaultTickClock()), local_state(),
        system_request_context()));
  }
  return network_time_tracker_.get();
}

gcm::GCMDriver* BrowserProcessImpl::gcm_driver() {
  DCHECK(CalledOnValidThread());
  if (!gcm_driver_)
    CreateGCMDriver();
  return gcm_driver_.get();
}

memory::TabManager* BrowserProcessImpl::GetTabManager() {
  DCHECK(CalledOnValidThread());
#if defined(OS_WIN) || defined(OS_MACOSX) || defined(OS_LINUX)
  if (!tab_manager_.get())
    tab_manager_.reset(new memory::TabManager());
  return tab_manager_.get();
#else
  return nullptr;
#endif
}

shell_integration::DefaultWebClientState
BrowserProcessImpl::CachedDefaultWebClientState() {
  return cached_default_web_client_state_;
}

physical_web::PhysicalWebDataSource*
BrowserProcessImpl::GetPhysicalWebDataSource() {
  DCHECK(CalledOnValidThread());
#if defined(OS_ANDROID)
  if (!physical_web_data_source_) {
    CreatePhysicalWebDataSource();
    DCHECK(physical_web_data_source_);
  }
  return physical_web_data_source_.get();
#else
  return nullptr;
#endif
}

// static
void BrowserProcessImpl::RegisterPrefs(PrefRegistrySimple* registry) {
  registry->RegisterBooleanPref(prefs::kDefaultBrowserSettingEnabled,
                                false);
  // This policy needs to be defined before the net subsystem is initialized,
  // so we do it here.
  registry->RegisterIntegerPref(prefs::kMaxConnectionsPerProxy,
                                net::kDefaultMaxSocketsPerProxyServer);

  registry->RegisterBooleanPref(prefs::kAllowCrossOriginAuthPrompt, false);

#if defined(OS_CHROMEOS) || defined(OS_ANDROID)
  registry->RegisterBooleanPref(prefs::kEulaAccepted, false);
#endif  // defined(OS_CHROMEOS) || defined(OS_ANDROID)

  // TODO(brettw,*): this comment about ResourceBundle was here since
  // initial commit.  This comment seems unrelated, bit-rotten and
  // a candidate for removal.
  // Initialize ResourceBundle which handles files loaded from external
  // sources. This has to be done before uninstall code path and before prefs
  // are registered.
  registry->RegisterStringPref(prefs::kApplicationLocale, std::string());
#if defined(OS_CHROMEOS)
  registry->RegisterStringPref(prefs::kOwnerLocale, std::string());
  registry->RegisterStringPref(prefs::kHardwareKeyboardLayout,
                               std::string());
#endif  // defined(OS_CHROMEOS)

  registry->RegisterBooleanPref(metrics::prefs::kMetricsReportingEnabled,
                                GoogleUpdateSettings::GetCollectStatsConsent());

#if defined(OS_ANDROID)
  registry->RegisterBooleanPref(
      prefs::kCrashReportingEnabled, false);
#endif  // defined(OS_ANDROID)
}

DownloadRequestLimiter* BrowserProcessImpl::download_request_limiter() {
  DCHECK(CalledOnValidThread());
  if (!download_request_limiter_.get())
    download_request_limiter_ = new DownloadRequestLimiter();
  return download_request_limiter_.get();
}

BackgroundModeManager* BrowserProcessImpl::background_mode_manager() {
  DCHECK(CalledOnValidThread());
#if BUILDFLAG(ENABLE_BACKGROUND)
  if (!background_mode_manager_.get())
    CreateBackgroundModeManager();
  return background_mode_manager_.get();
#else
  NOTIMPLEMENTED();
  return NULL;
#endif
}

void BrowserProcessImpl::set_background_mode_manager_for_test(
    std::unique_ptr<BackgroundModeManager> manager) {
#if BUILDFLAG(ENABLE_BACKGROUND)
  background_mode_manager_ = std::move(manager);
#endif
}

StatusTray* BrowserProcessImpl::status_tray() {
  DCHECK(CalledOnValidThread());
  if (!status_tray_.get())
    CreateStatusTray();
  return status_tray_.get();
}

safe_browsing::SafeBrowsingService*
BrowserProcessImpl::safe_browsing_service() {
  DCHECK(CalledOnValidThread());
  if (!created_safe_browsing_service_)
    CreateSafeBrowsingService();
  return safe_browsing_service_.get();
}

safe_browsing::ClientSideDetectionService*
    BrowserProcessImpl::safe_browsing_detection_service() {
  DCHECK(CalledOnValidThread());
  if (safe_browsing_service())
    return safe_browsing_service()->safe_browsing_detection_service();
  return NULL;
}

subresource_filter::ContentRulesetService*
BrowserProcessImpl::subresource_filter_ruleset_service() {
  DCHECK(CalledOnValidThread());
  if (!created_subresource_filter_ruleset_service_)
    CreateSubresourceFilterRulesetService();
  return subresource_filter_ruleset_service_.get();
}

#if (defined(OS_WIN) || defined(OS_LINUX)) && !defined(OS_CHROMEOS)
void BrowserProcessImpl::StartAutoupdateTimer() {
  autoupdate_timer_.Start(FROM_HERE,
      base::TimeDelta::FromHours(kUpdateCheckIntervalHours),
      this,
      &BrowserProcessImpl::OnAutoupdateTimer);
}
#endif

net_log::ChromeNetLog* BrowserProcessImpl::net_log() {
  return net_log_.get();
}

component_updater::ComponentUpdateService*
BrowserProcessImpl::component_updater() {
  if (component_updater_)
    return component_updater_.get();

  if (!BrowserThread::CurrentlyOn(BrowserThread::UI))
    return nullptr;

  component_updater_ = component_updater::ComponentUpdateServiceFactory(
      component_updater::MakeChromeComponentUpdaterConfigurator(
          base::CommandLine::ForCurrentProcess(),
          io_thread()->system_url_request_context_getter(),
          g_browser_process->local_state()));

  return component_updater_.get();
}

CRLSetFetcher* BrowserProcessImpl::crl_set_fetcher() {
  if (!crl_set_fetcher_)
    crl_set_fetcher_ = new CRLSetFetcher();
  return crl_set_fetcher_.get();
}

component_updater::PnaclComponentInstaller*
BrowserProcessImpl::pnacl_component_installer() {
#if !defined(DISABLE_NACL)
  if (!pnacl_component_installer_) {
    pnacl_component_installer_ =
        new component_updater::PnaclComponentInstaller();
  }
  return pnacl_component_installer_.get();
#else
  return nullptr;
#endif
}

component_updater::SupervisedUserWhitelistInstaller*
BrowserProcessImpl::supervised_user_whitelist_installer() {
  if (!supervised_user_whitelist_installer_) {
    supervised_user_whitelist_installer_ =
        component_updater::SupervisedUserWhitelistInstaller::Create(
            component_updater(),
            &profile_manager()->GetProfileAttributesStorage(),
            local_state());
  }
  return supervised_user_whitelist_installer_.get();
}

void BrowserProcessImpl::ResourceDispatcherHostCreated() {
  resource_dispatcher_host_delegate_.reset(
      new ChromeResourceDispatcherHostDelegate);
  ResourceDispatcherHost::Get()->SetDelegate(
      resource_dispatcher_host_delegate_.get());

  pref_change_registrar_.Add(
      prefs::kAllowCrossOriginAuthPrompt,
      base::Bind(&BrowserProcessImpl::ApplyAllowCrossOriginAuthPromptPolicy,
                 base::Unretained(this)));
  ApplyAllowCrossOriginAuthPromptPolicy();
}

void BrowserProcessImpl::OnKeepAliveStateChanged(bool is_keeping_alive) {
  if (is_keeping_alive)
    Pin();
  else
    Unpin();
}

void BrowserProcessImpl::OnKeepAliveRestartStateChanged(bool can_restart) {}

void BrowserProcessImpl::CreateWatchdogThread() {
  DCHECK(!created_watchdog_thread_ && !watchdog_thread_);
  created_watchdog_thread_ = true;

  std::unique_ptr<WatchDogThread> thread(new WatchDogThread());
  base::Thread::Options options;
  options.timer_slack = base::TIMER_SLACK_MAXIMUM;
  if (!thread->StartWithOptions(options))
    return;
  watchdog_thread_.swap(thread);
}

void BrowserProcessImpl::CreateProfileManager() {
  DCHECK(!created_profile_manager_ && !profile_manager_);
  created_profile_manager_ = true;

  base::FilePath user_data_dir;
  PathService::Get(chrome::DIR_USER_DATA, &user_data_dir);
  profile_manager_.reset(new ProfileManager(user_data_dir));
}

void BrowserProcessImpl::CreateLocalState() {
  DCHECK(!created_local_state_ && !local_state_);
  created_local_state_ = true;

  base::FilePath local_state_path;
  CHECK(PathService::Get(chrome::FILE_LOCAL_STATE, &local_state_path));
  scoped_refptr<PrefRegistrySimple> pref_registry = new PrefRegistrySimple;

  // Register local state preferences.
  chrome::RegisterLocalState(pref_registry.get());

  local_state_ = chrome_prefs::CreateLocalState(
      local_state_path, local_state_task_runner_.get(), policy_service(),
      pref_registry, false);

  pref_change_registrar_.Init(local_state_.get());

  // Initialize the notification for the default browser setting policy.
  pref_change_registrar_.Add(
      prefs::kDefaultBrowserSettingEnabled,
      base::Bind(&BrowserProcessImpl::ApplyDefaultBrowserPolicy,
                 base::Unretained(this)));

  // This preference must be kept in sync with external values; update them
  // whenever the preference or its controlling policy changes.
#if !defined(OS_ANDROID)
  pref_change_registrar_.Add(
      metrics::prefs::kMetricsReportingEnabled,
      base::Bind(&BrowserProcessImpl::ApplyMetricsReportingPolicy,
                 base::Unretained(this)));
#endif

  int max_per_proxy = local_state_->GetInteger(prefs::kMaxConnectionsPerProxy);
  net::ClientSocketPoolManager::set_max_sockets_per_proxy_server(
      net::HttpNetworkSession::NORMAL_SOCKET_POOL,
      std::max(std::min(max_per_proxy, 99),
               net::ClientSocketPoolManager::max_sockets_per_group(
                   net::HttpNetworkSession::NORMAL_SOCKET_POOL)));
}

void BrowserProcessImpl::PreCreateThreads() {
#if BUILDFLAG(ENABLE_EXTENSIONS)
  // chrome-extension:// URLs are safe to request anywhere, but may only
  // commit (including in iframes) in extension processes.
  ChildProcessSecurityPolicy::GetInstance()->RegisterWebSafeIsolatedScheme(
      extensions::kExtensionScheme, true);
#endif

  io_thread_.reset(
      new IOThread(local_state(), policy_service(), net_log_.get(),
                   extension_event_router_forwarder()));
}

void BrowserProcessImpl::PreMainMessageLoopRun() {
  TRACE_EVENT0("startup", "BrowserProcessImpl::PreMainMessageLoopRun");
  SCOPED_UMA_HISTOGRAM_TIMER(
      "Startup.BrowserProcessImpl_PreMainMessageLoopRunTime");

  // browser_policy_connector() is created very early because local_state()
  // needs policy to be initialized with the managed preference values.
  // However, policy fetches from the network and loading of disk caches
  // requires that threads are running; this Init() call lets the connector
  // resume its initialization now that the loops are spinning and the
  // system request context is available for the fetchers.
  browser_policy_connector()->Init(local_state(), system_request_context());

  if (local_state_->IsManagedPreference(prefs::kDefaultBrowserSettingEnabled))
    ApplyDefaultBrowserPolicy();

#if !defined(OS_ANDROID)
  ApplyMetricsReportingPolicy();
#endif

#if BUILDFLAG(ENABLE_PLUGINS)
  PluginService* plugin_service = PluginService::GetInstance();
  plugin_service->SetFilter(ChromePluginServiceFilter::GetInstance());

  // Triggers initialization of the singleton instance on UI thread.
  PluginFinder::GetInstance()->Init();

  DCHECK(!plugins_resource_service_);
  plugins_resource_service_ =
      base::MakeUnique<PluginsResourceService>(local_state());
  plugins_resource_service_->Init();
#endif  // BUILDFLAG(ENABLE_PLUGINS)

#if !defined(OS_ANDROID)
  storage_monitor::StorageMonitor::Create();
#endif

  child_process_watcher_.reset(new ChromeChildProcessWatcher());

  CacheDefaultWebClientState();

  platform_part_->PreMainMessageLoopRun();

  if (base::FeatureList::IsEnabled(network_time::kNetworkTimeServiceQuerying)) {
    network_time_tracker_.reset(new network_time::NetworkTimeTracker(
        base::WrapUnique(new base::DefaultClock()),
        base::WrapUnique(new base::DefaultTickClock()), local_state(),
        system_request_context()));
  }
}

void BrowserProcessImpl::CreateIconManager() {
  DCHECK(!created_icon_manager_ && !icon_manager_);
  created_icon_manager_ = true;
  icon_manager_.reset(new IconManager);
}

void BrowserProcessImpl::CreateIntranetRedirectDetector() {
  DCHECK(!intranet_redirect_detector_);
  std::unique_ptr<IntranetRedirectDetector> intranet_redirect_detector(
      new IntranetRedirectDetector);
  intranet_redirect_detector_.swap(intranet_redirect_detector);
}

void BrowserProcessImpl::CreateNotificationPlatformBridge() {
#if BUILDFLAG(ENABLE_NATIVE_NOTIFICATIONS)
  DCHECK(!notification_bridge_);
  notification_bridge_.reset(NotificationPlatformBridge::Create());
  created_notification_bridge_ = true;
#endif
}

void BrowserProcessImpl::CreateNotificationUIManager() {
// Android does not use the NotificationUIManager anymore
// All notification traffic is routed through NotificationPlatformBridge.
#if !defined(OS_ANDROID)
  DCHECK(!notification_ui_manager_);
  notification_ui_manager_.reset(NotificationUIManager::Create());
  created_notification_ui_manager_ = true;
#endif
}

void BrowserProcessImpl::CreateBackgroundModeManager() {
#if BUILDFLAG(ENABLE_BACKGROUND)
  DCHECK(!background_mode_manager_);
  background_mode_manager_.reset(
      new BackgroundModeManager(
              *base::CommandLine::ForCurrentProcess(),
              &profile_manager()->GetProfileAttributesStorage()));
#endif
}

void BrowserProcessImpl::CreateStatusTray() {
  DCHECK(!status_tray_);
  status_tray_.reset(StatusTray::Create());
}

void BrowserProcessImpl::CreatePrintPreviewDialogController() {
#if BUILDFLAG(ENABLE_PRINT_PREVIEW)
  DCHECK(!print_preview_dialog_controller_);
  print_preview_dialog_controller_ =
      new printing::PrintPreviewDialogController();
#else
  NOTIMPLEMENTED();
#endif
}

void BrowserProcessImpl::CreateBackgroundPrintingManager() {
#if BUILDFLAG(ENABLE_PRINT_PREVIEW)
  DCHECK(!background_printing_manager_);
  background_printing_manager_.reset(new printing::BackgroundPrintingManager());
#else
  NOTIMPLEMENTED();
#endif
}

void BrowserProcessImpl::CreateSafeBrowsingService() {
  DCHECK(!safe_browsing_service_);
  // Set this flag to true so that we don't retry indefinitely to
  // create the service class if there was an error.
  created_safe_browsing_service_ = true;
  safe_browsing_service_ =
      safe_browsing::SafeBrowsingService::CreateSafeBrowsingService();
  safe_browsing_service_->Initialize();
}

void BrowserProcessImpl::CreateSubresourceFilterRulesetService() {
  DCHECK(!subresource_filter_ruleset_service_);
  created_subresource_filter_ruleset_service_ = true;

  if (!base::FeatureList::IsEnabled(
          subresource_filter::kSafeBrowsingSubresourceFilter)) {
    return;
  }

  scoped_refptr<base::SequencedTaskRunner> blocking_task_runner(
      base::CreateSequencedTaskRunnerWithTraits(
          {base::MayBlock(), base::TaskPriority::BACKGROUND,
           base::TaskShutdownBehavior::SKIP_ON_SHUTDOWN}));

  base::FilePath user_data_dir;
  PathService::Get(chrome::DIR_USER_DATA, &user_data_dir);
  base::FilePath indexed_ruleset_base_dir =
      user_data_dir.Append(subresource_filter::kTopLevelDirectoryName)
          .Append(subresource_filter::kIndexedRulesetBaseDirectoryName);
  subresource_filter_ruleset_service_ =
      base::MakeUnique<subresource_filter::ContentRulesetService>(
          blocking_task_runner);
  subresource_filter_ruleset_service_->set_ruleset_service(
      base::MakeUnique<subresource_filter::RulesetService>(
          local_state(), blocking_task_runner,
          subresource_filter_ruleset_service_.get(), indexed_ruleset_base_dir));
}

void BrowserProcessImpl::CreateGCMDriver() {
  DCHECK(!gcm_driver_);

#if defined(OS_ANDROID)
  // Android's GCMDriver currently makes the assumption that it's a singleton.
  // Until this gets fixed, instantiating multiple Java GCMDrivers will throw
  // an exception, but because they're only initialized on demand these crashes
  // would be very difficult to triage. See http://crbug.com/437827.
  NOTREACHED();
#else
  base::FilePath store_path;
  CHECK(PathService::Get(chrome::DIR_GLOBAL_GCM_STORE, &store_path));
  scoped_refptr<base::SequencedTaskRunner> blocking_task_runner(
      base::CreateSequencedTaskRunnerWithTraits(
          {base::MayBlock(), base::TaskPriority::BACKGROUND,
           base::TaskShutdownBehavior::SKIP_ON_SHUTDOWN}));

  gcm_driver_ = gcm::CreateGCMDriverDesktop(
      base::WrapUnique(new gcm::GCMClientFactory), local_state(), store_path,
      system_request_context(), chrome::GetChannel(),
      gcm::GetProductCategoryForSubtypes(local_state()),
      content::BrowserThread::GetTaskRunnerForThread(
          content::BrowserThread::UI),
      content::BrowserThread::GetTaskRunnerForThread(
          content::BrowserThread::IO),
      blocking_task_runner);
#endif  // defined(OS_ANDROID)
}

void BrowserProcessImpl::CreatePhysicalWebDataSource() {
  DCHECK(!physical_web_data_source_);

#if defined(OS_ANDROID)
  physical_web_data_source_ = base::MakeUnique<PhysicalWebDataSourceAndroid>();
#else
  NOTIMPLEMENTED();
#endif
}

void BrowserProcessImpl::ApplyDefaultBrowserPolicy() {
  if (local_state()->GetBoolean(prefs::kDefaultBrowserSettingEnabled)) {
    // The worker pointer is reference counted. While it is running, the
    // message loops of the FILE and UI thread will hold references to it
    // and it will be automatically freed once all its tasks have finished.
    scoped_refptr<shell_integration::DefaultBrowserWorker> set_browser_worker =
        new shell_integration::DefaultBrowserWorker(
            shell_integration::DefaultWebClientWorkerCallback());
    // The user interaction must always be disabled when applying the default
    // browser policy since it is done at each browser startup and the result
    // of the interaction cannot be forced.
    set_browser_worker->set_interactive_permitted(false);
    set_browser_worker->StartSetAsDefault();
  }
}

void BrowserProcessImpl::ApplyAllowCrossOriginAuthPromptPolicy() {
  bool value = local_state()->GetBoolean(prefs::kAllowCrossOriginAuthPrompt);
  ResourceDispatcherHost::Get()->SetAllowCrossOriginAuthPrompt(value);
}

void BrowserProcessImpl::ApplyMetricsReportingPolicy() {
#if !defined(OS_ANDROID)
  CHECK(BrowserThread::PostTask(
      BrowserThread::FILE, FROM_HERE,
      base::BindOnce(
          base::IgnoreResult(&GoogleUpdateSettings::SetCollectStatsConsent),
          ChromeMetricsServiceAccessor::IsMetricsAndCrashReportingEnabled())));
#endif
}

void BrowserProcessImpl::CacheDefaultWebClientState() {
#if defined(OS_CHROMEOS)
  cached_default_web_client_state_ = shell_integration::IS_DEFAULT;
#elif !defined(OS_ANDROID)
  cached_default_web_client_state_ = shell_integration::GetDefaultBrowser();
#endif
}

void BrowserProcessImpl::Pin() {
  DCHECK(CalledOnValidThread());

  // CHECK(!IsShuttingDown());
  if (IsShuttingDown()) {
    // TODO(crbug.com/113031, crbug.com/625646): Temporary instrumentation.
    base::debug::SetCrashKeyToStackTrace(crash_keys::kBrowserUnpinTrace,
                                         release_last_reference_callstack_);
    CHECK(false);
  }
}

void BrowserProcessImpl::Unpin() {
  DCHECK(CalledOnValidThread());
  release_last_reference_callstack_ = base::debug::StackTrace();

  shutting_down_ = true;
#if BUILDFLAG(ENABLE_PRINTING)
  // Wait for the pending print jobs to finish. Don't do this later, since
  // this might cause a nested run loop to run, and we don't want pending
  // tasks to run once teardown has started.
  print_job_manager_->Shutdown();
#endif

#if defined(LEAK_SANITIZER)
  // Check for memory leaks now, before we start shutting down threads. Doing
  // this early means we won't report any shutdown-only leaks (as they have
  // not yet happened at this point).
  // If leaks are found, this will make the process exit immediately.
  __lsan_do_leak_check();
#endif

  CHECK(base::RunLoop::IsRunningOnCurrentThread());

#if defined(OS_MACOSX)
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE, base::Bind(ChromeBrowserMainPartsMac::DidEndMainMessageLoop));
#endif
  base::MessageLoop::current()->QuitWhenIdle();

#if !defined(OS_ANDROID)
  chrome::ShutdownIfNeeded();
#endif  // !defined(OS_ANDROID)
}

// Mac is currently not supported.
#if (defined(OS_WIN) || defined(OS_LINUX)) && !defined(OS_CHROMEOS)

bool BrowserProcessImpl::CanAutorestartForUpdate() const {
  // Check if browser is in the background and if it needs to be restarted to
  // apply a pending update.
  return chrome::GetTotalBrowserCount() == 0 &&
         KeepAliveRegistry::GetInstance()->IsKeepingAlive() &&
         upgrade_util::IsUpdatePendingRestart();
}

// Switches to add when auto-restarting Chrome.
const char* const kSwitchesToAddOnAutorestart[] = {
  switches::kNoStartupWindow
};

void BrowserProcessImpl::RestartBackgroundInstance() {
  base::CommandLine* old_cl = base::CommandLine::ForCurrentProcess();
  std::unique_ptr<base::CommandLine> new_cl(
      new base::CommandLine(old_cl->GetProgram()));

  std::map<std::string, base::CommandLine::StringType> switches =
      old_cl->GetSwitches();

  switches::RemoveSwitchesForAutostart(&switches);

  // Append the rest of the switches (along with their values, if any)
  // to the new command line
  for (std::map<std::string, base::CommandLine::StringType>::const_iterator i =
           switches.begin();
       i != switches.end(); ++i) {
    base::CommandLine::StringType switch_value = i->second;
      if (switch_value.length() > 0) {
        new_cl->AppendSwitchNative(i->first, i->second);
      } else {
        new_cl->AppendSwitch(i->first);
      }
  }

  // Ensure that our desired switches are set on the new process.
  for (size_t i = 0; i < arraysize(kSwitchesToAddOnAutorestart); ++i) {
    if (!new_cl->HasSwitch(kSwitchesToAddOnAutorestart[i]))
      new_cl->AppendSwitch(kSwitchesToAddOnAutorestart[i]);
  }

#if defined(OS_WIN)
  new_cl->AppendArg(switches::kPrefetchArgumentBrowserBackground);
#endif  // defined(OS_WIN)

  DLOG(WARNING) << "Shutting down current instance of the browser.";
  chrome::AttemptExit();

  // Transfer ownership to Upgrade.
  upgrade_util::SetNewCommandLine(new_cl.release());
}

void BrowserProcessImpl::OnAutoupdateTimer() {
  if (CanAutorestartForUpdate()) {
    DLOG(WARNING) << "Detected update.  Restarting browser.";
    RestartBackgroundInstance();
  }
}

#endif  // (defined(OS_WIN) || defined(OS_LINUX)) && !defined(OS_CHROMEOS)
