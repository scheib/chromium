// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromecast/browser/cast_content_browser_client.h"

#include <stddef.h>

#include <string>
#include <utility>

#include "base/base_switches.h"
#include "base/command_line.h"
#include "base/files/scoped_file.h"
#include "base/i18n/rtl.h"
#include "base/json/json_reader.h"
#include "base/macros.h"
#include "base/memory/ptr_util.h"
#include "base/path_service.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/threading/thread_task_runner_handle.h"
#include "build/build_config.h"
#include "chromecast/base/cast_constants.h"
#include "chromecast/base/cast_paths.h"
#include "chromecast/base/chromecast_switches.h"
#include "chromecast/browser/cast_browser_context.h"
#include "chromecast/browser/cast_browser_main_parts.h"
#include "chromecast/browser/cast_browser_process.h"
#include "chromecast/browser/cast_network_delegate.h"
#include "chromecast/browser/cast_quota_permission_context.h"
#include "chromecast/browser/cast_resource_dispatcher_host_delegate.h"
#include "chromecast/browser/devtools/cast_devtools_manager_delegate.h"
#include "chromecast/browser/grit/cast_browser_resources.h"
#include "chromecast/browser/media/media_caps_impl.h"
#include "chromecast/browser/service/cast_service_simple.h"
#include "chromecast/browser/url_request_context_factory.h"
#include "chromecast/common/global_descriptors.h"
#include "chromecast/media/audio/cast_audio_manager.h"
#include "chromecast/media/cma/backend/media_pipeline_backend_factory.h"
#include "chromecast/media/cma/backend/media_pipeline_backend_manager.h"
#include "chromecast/public/media/media_pipeline_backend.h"
#include "components/crash/content/app/breakpad_linux.h"
#include "components/crash/content/browser/crash_handler_host_linux.h"
#include "components/network_hints/browser/network_hints_message_filter.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/certificate_request_result_type.h"
#include "content/public/browser/client_certificate_delegate.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/resource_dispatcher_host.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/content_descriptors.h"
#include "content/public/common/content_switches.h"
#include "content/public/common/service_names.mojom.h"
#include "content/public/common/url_constants.h"
#include "content/public/common/web_preferences.h"
#include "media/audio/audio_thread_impl.h"
#include "media/mojo/features.h"
#include "net/ssl/ssl_cert_request_info.h"
#include "net/url_request/url_request_context_getter.h"
#include "services/service_manager/public/cpp/bind_source_info.h"
#include "services/service_manager/public/cpp/binder_registry.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/display/display.h"
#include "ui/display/screen.h"
#include "ui/gl/gl_switches.h"

#if BUILDFLAG(ENABLE_MOJO_MEDIA_IN_BROWSER_PROCESS)
#include "chromecast/media/service/cast_mojo_media_client.h"
#include "media/mojo/services/media_service.h"  // nogncheck
#endif  // ENABLE_MOJO_MEDIA_IN_BROWSER_PROCESS

#if defined(OS_ANDROID)
#include "components/cdm/browser/cdm_message_filter_android.h"
#include "components/crash/content/browser/crash_dump_manager_android.h"
#endif  // defined(OS_ANDROID)

#if BUILDFLAG(IS_CAST_USING_CMA_BACKEND)
#include "chromecast/media/cdm/cast_cdm_factory.h"
#endif  // BUILDFLAG(IS_CAST_USING_CMA_BACKEND)

namespace chromecast {
namespace shell {

namespace {
#if BUILDFLAG(ENABLE_MOJO_MEDIA_IN_BROWSER_PROCESS)
static std::unique_ptr<service_manager::Service> CreateMediaService(
    CastContentBrowserClient* browser_client) {
  std::unique_ptr<media::CastMojoMediaClient> mojo_media_client(
      new media::CastMojoMediaClient(
          browser_client->GetMediaPipelineBackendFactory(),
          base::Bind(&CastContentBrowserClient::CreateCdmFactory,
                     base::Unretained(browser_client)),
          browser_client->GetVideoModeSwitcher(),
          browser_client->GetVideoResolutionPolicy(),
          browser_client->media_resource_tracker()));
  return std::unique_ptr<service_manager::Service>(
      new ::media::MediaService(std::move(mojo_media_client)));
}
#endif  // BUILDFLAG(ENABLE_MOJO_MEDIA_IN_BROWSER_PROCESS)

}  // namespace

CastContentBrowserClient::CastContentBrowserClient()
    : cast_browser_main_parts_(nullptr),
      url_request_context_factory_(new URLRequestContextFactory()) {}

CastContentBrowserClient::~CastContentBrowserClient() {
  content::BrowserThread::DeleteSoon(
      content::BrowserThread::IO,
      FROM_HERE,
      url_request_context_factory_.release());
}

void CastContentBrowserClient::AppendExtraCommandLineSwitches(
    base::CommandLine* command_line) {
#if defined(USE_AURA)
  std::string process_type =
      command_line->GetSwitchValueNative(switches::kProcessType);
  if (process_type == switches::kGpuProcess) {
    gfx::Size res =
        display::Screen::GetScreen()->GetPrimaryDisplay().GetSizeInPixel();
    if (!command_line->HasSwitch(switches::kCastInitialScreenWidth)) {
      command_line->AppendSwitchASCII(switches::kCastInitialScreenWidth,
                                      base::IntToString(res.width()));
    }
    if (!command_line->HasSwitch(switches::kCastInitialScreenHeight)) {
      command_line->AppendSwitchASCII(switches::kCastInitialScreenHeight,
                                      base::IntToString(res.height()));
    }
    base::CommandLine* browser_command_line =
        base::CommandLine::ForCurrentProcess();
    for (auto* const switch_name : {switches::kUseDoubleBuffering}) {
      if (browser_command_line->HasSwitch(switch_name)) {
        command_line->AppendSwitchASCII(
            switch_name,
            browser_command_line->GetSwitchValueASCII(switch_name));
      }
    }
  }
#endif  // defined(USE_AURA)
}

void CastContentBrowserClient::PreCreateThreads() {
}

std::unique_ptr<CastService> CastContentBrowserClient::CreateCastService(
    content::BrowserContext* browser_context,
    PrefService* pref_service,
    net::URLRequestContextGetter* request_context_getter,
    DisableQuicClosure disable_quic_closure,
    media::VideoPlaneController* video_plane_controller,
    CastWindowManager* window_manager) {
  return base::MakeUnique<CastServiceSimple>(browser_context, pref_service,
                                             window_manager);
}

media::VideoModeSwitcher* CastContentBrowserClient::GetVideoModeSwitcher() {
  return nullptr;
}

#if BUILDFLAG(IS_CAST_USING_CMA_BACKEND)
media::VideoResolutionPolicy*
CastContentBrowserClient::GetVideoResolutionPolicy() {
  return nullptr;
}

scoped_refptr<base::SingleThreadTaskRunner>
CastContentBrowserClient::GetMediaTaskRunner() {
  DCHECK(cast_browser_main_parts_);
  return cast_browser_main_parts_->GetMediaTaskRunner();
}

media::MediaPipelineBackendFactory*
CastContentBrowserClient::GetMediaPipelineBackendFactory() {
  DCHECK(GetMediaTaskRunner()->BelongsToCurrentThread());
  if (!media_pipeline_backend_factory_) {
    media_pipeline_backend_factory_.reset(
        new media::MediaPipelineBackendFactory(
            media_pipeline_backend_manager()));
  }
  return media_pipeline_backend_factory_.get();
}

media::MediaResourceTracker*
CastContentBrowserClient::media_resource_tracker() {
  return cast_browser_main_parts_->media_resource_tracker();
}

media::MediaPipelineBackendManager*
CastContentBrowserClient::media_pipeline_backend_manager() {
  DCHECK(cast_browser_main_parts_);
  return cast_browser_main_parts_->media_pipeline_backend_manager();
}

std::unique_ptr<::media::AudioManager>
CastContentBrowserClient::CreateAudioManager(
    ::media::AudioLogFactory* audio_log_factory) {
  return base::MakeUnique<media::CastAudioManager>(
      base::MakeUnique<::media::AudioThreadImpl>(), audio_log_factory,
      media_pipeline_backend_manager());
}

std::unique_ptr<::media::CdmFactory>
CastContentBrowserClient::CreateCdmFactory() {
#if BUILDFLAG(ENABLE_MOJO_MEDIA_IN_BROWSER_PROCESS)
  return base::MakeUnique<media::CastCdmFactory>(GetMediaTaskRunner(),
                                                 media_resource_tracker());
#endif  // BUILDFLAG(ENABLE_MOJO_MEDIA_IN_BROWSER_PROCESS)
  return nullptr;
}
#endif  // BUILDFLAG(IS_CAST_USING_CMA_BACKEND)

media::MediaCapsImpl* CastContentBrowserClient::media_caps() {
  DCHECK(cast_browser_main_parts_);
  return cast_browser_main_parts_->media_caps();
}

void CastContentBrowserClient::SetMetricsClientId(
    const std::string& client_id) {
}

void CastContentBrowserClient::RegisterMetricsProviders(
    ::metrics::MetricsService* metrics_service) {
}

bool CastContentBrowserClient::EnableRemoteDebuggingImmediately() {
  return true;
}

content::BrowserMainParts* CastContentBrowserClient::CreateBrowserMainParts(
    const content::MainFunctionParams& parameters) {
  DCHECK(!cast_browser_main_parts_);
  cast_browser_main_parts_ =
      new CastBrowserMainParts(parameters, url_request_context_factory_.get());
  CastBrowserProcess::GetInstance()->SetCastContentBrowserClient(this);
  return cast_browser_main_parts_;
}

void CastContentBrowserClient::RenderProcessWillLaunch(
    content::RenderProcessHost* host) {
  // Forcibly trigger I/O-thread URLRequestContext initialization before
  // getting HostResolver.
  content::BrowserThread::PostTaskAndReplyWithResult(
      content::BrowserThread::IO, FROM_HERE,
      base::Bind(&net::URLRequestContextGetter::GetURLRequestContext,
                 base::Unretained(
                    url_request_context_factory_->GetSystemGetter())),
      base::Bind(&CastContentBrowserClient::AddNetworkHintsMessageFilter,
                 base::Unretained(this), host->GetID()));

#if defined(OS_ANDROID)
  host->AddFilter(new cdm::CdmMessageFilterAndroid());
#endif  // defined(OS_ANDROID)
}

void CastContentBrowserClient::AddNetworkHintsMessageFilter(
    int render_process_id, net::URLRequestContext* context) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

  content::RenderProcessHost* host =
      content::RenderProcessHost::FromID(render_process_id);
  if (!host)
    return;

  scoped_refptr<content::BrowserMessageFilter> network_hints_message_filter(
      new network_hints::NetworkHintsMessageFilter(
          url_request_context_factory_->host_resolver()));
  host->AddFilter(network_hints_message_filter.get());
}

bool CastContentBrowserClient::IsHandledURL(const GURL& url) {
  if (!url.is_valid())
    return false;

  static const char* const kProtocolList[] = {
    content::kChromeUIScheme,
    content::kChromeDevToolsScheme,
    kChromeResourceScheme,
    url::kBlobScheme,
    url::kDataScheme,
    url::kFileSystemScheme,
  };

  const std::string& scheme = url.scheme();
  for (size_t i = 0; i < arraysize(kProtocolList); ++i) {
    if (scheme == kProtocolList[i])
      return true;
  }

  if (scheme == url::kFileScheme) {
    return base::CommandLine::ForCurrentProcess()->HasSwitch(
        switches::kEnableLocalFileAccesses);
  }

  return false;
}

void CastContentBrowserClient::AppendExtraCommandLineSwitches(
    base::CommandLine* command_line,
    int child_process_id) {
  std::string process_type =
      command_line->GetSwitchValueNative(switches::kProcessType);
  base::CommandLine* browser_command_line =
      base::CommandLine::ForCurrentProcess();

  // IsCrashReporterEnabled() is set when InitCrashReporter() is called, and
  // controlled by GetBreakpadClient()->EnableBreakpadForProcess(), therefore
  // it's ok to add switch to every process here.
  if (breakpad::IsCrashReporterEnabled()) {
    command_line->AppendSwitch(switches::kEnableCrashReporter);
  }

  // Renderer process command-line
  if (process_type == switches::kRendererProcess) {
    // Any browser command-line switches that should be propagated to
    // the renderer go here.
    if (browser_command_line->HasSwitch(switches::kAllowHiddenMediaPlayback))
      command_line->AppendSwitch(switches::kAllowHiddenMediaPlayback);
  }

#if defined(OS_LINUX)
  // Necessary for accelerated 2d canvas.  By default on Linux, Chromium assumes
  // GLES2 contexts can be lost to a power-save mode, which breaks GPU canvas
  // apps.
  if (process_type == switches::kGpuProcess) {
    command_line->AppendSwitch(switches::kGpuNoContextLost);
  }
#endif

  AppendExtraCommandLineSwitches(command_line);
}

void CastContentBrowserClient::OverrideWebkitPrefs(
    content::RenderViewHost* render_view_host,
    content::WebPreferences* prefs) {
  prefs->allow_scripts_to_close_windows = true;
  // TODO(lcwu): http://crbug.com/391089. This pref is set to true by default
  // because some content providers such as YouTube use plain http requests
  // to retrieve media data chunks while running in a https page. This pref
  // should be disabled once all the content providers are no longer doing that.
  prefs->allow_running_insecure_content = true;

  // Enable 5% margins for WebVTT cues to keep within title-safe area
  prefs->text_track_margin_percentage = 5;

  prefs->hide_scrollbars = true;

#if defined(OS_ANDROID)
  // Enable the television style for viewport so that all cast apps have a
  // 1280px wide layout viewport by default.
  DCHECK(prefs->viewport_enabled);
  DCHECK(prefs->viewport_meta_enabled);
  prefs->viewport_style = content::ViewportStyle::TELEVISION;
#endif  // defined(OS_ANDROID)
}

void CastContentBrowserClient::ResourceDispatcherHostCreated() {
  resource_dispatcher_host_delegate_.reset(
      new CastResourceDispatcherHostDelegate);
  content::ResourceDispatcherHost::Get()->SetDelegate(
      resource_dispatcher_host_delegate_.get());
}

std::string CastContentBrowserClient::GetApplicationLocale() {
  const std::string locale(base::i18n::GetConfiguredLocale());
  return locale.empty() ? "en-US" : locale;
}

content::QuotaPermissionContext*
CastContentBrowserClient::CreateQuotaPermissionContext() {
  return new CastQuotaPermissionContext();
}

void CastContentBrowserClient::GetQuotaSettings(
    content::BrowserContext* context,
    content::StoragePartition* partition,
    const storage::OptionalQuotaSettingsCallback& callback) {
  content::BrowserThread::PostTaskAndReplyWithResult(
      content::BrowserThread::FILE, FROM_HERE,
      base::Bind(&storage::CalculateNominalDynamicSettings,
                 partition->GetPath(), context->IsOffTheRecord()),
      callback);
}
void CastContentBrowserClient::AllowCertificateError(
    content::WebContents* web_contents,
    int cert_error,
    const net::SSLInfo& ssl_info,
    const GURL& request_url,
    content::ResourceType resource_type,
    bool overridable,
    bool strict_enforcement,
    bool expired_previous_decision,
    const base::Callback<void(content::CertificateRequestResultType)>&
        callback) {
  // Allow developers to override certificate errors.
  // Otherwise, any fatal certificate errors will cause an abort.
  if (!callback.is_null()) {
    callback.Run(content::CERTIFICATE_REQUEST_RESULT_TYPE_CANCEL);
  }
  return;
}

void CastContentBrowserClient::SelectClientCertificate(
    content::WebContents* web_contents,
    net::SSLCertRequestInfo* cert_request_info,
    net::CertificateList client_certs,
    std::unique_ptr<content::ClientCertificateDelegate> delegate) {
  GURL requesting_url("https://" + cert_request_info->host_and_port.ToString());

  if (!requesting_url.is_valid()) {
    LOG(ERROR) << "Invalid URL string: "
               << requesting_url.possibly_invalid_spec();
    delegate->ContinueWithCertificate(nullptr);
    return;
  }

  // In our case there are no relevant certs in |client_certs|. The cert
  // we need to return (if permitted) is the Cast device cert, which we can
  // access directly through the ClientAuthSigner instance. However, we need to
  // be on the IO thread to determine whether the app is whitelisted to return
  // it, because CastNetworkDelegate is bound to the IO thread.
  // Subsequently, the callback must then itself be performed back here
  // on the UI thread.
  //
  // TODO(davidben): Stop using child ID to identify an app.
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  content::BrowserThread::PostTaskAndReplyWithResult(
      content::BrowserThread::IO, FROM_HERE,
      base::Bind(&CastContentBrowserClient::SelectClientCertificateOnIOThread,
                 base::Unretained(this), requesting_url,
                 web_contents->GetRenderProcessHost()->GetID()),
      base::Bind(&content::ClientCertificateDelegate::ContinueWithCertificate,
                 base::Owned(delegate.release())));
}

net::X509Certificate*
CastContentBrowserClient::SelectClientCertificateOnIOThread(
    GURL requesting_url,
    int render_process_id) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::IO);
  CastNetworkDelegate* network_delegate =
      url_request_context_factory_->app_network_delegate();
  if (network_delegate->IsWhitelisted(requesting_url,
                                      render_process_id, false)) {
    return CastNetworkDelegate::DeviceCert();
  } else {
    LOG(ERROR) << "Invalid host for client certificate request: "
               << requesting_url.host()
               << " with render_process_id: "
               << render_process_id;
    return NULL;
  }
}

bool CastContentBrowserClient::CanCreateWindow(
    content::RenderFrameHost* opener,
    const GURL& opener_url,
    const GURL& opener_top_level_frame_url,
    const GURL& source_origin,
    content::mojom::WindowContainerType container_type,
    const GURL& target_url,
    const content::Referrer& referrer,
    const std::string& frame_name,
    WindowOpenDisposition disposition,
    const blink::mojom::WindowFeatures& features,
    bool user_gesture,
    bool opener_suppressed,
    bool* no_javascript_access) {
  *no_javascript_access = true;
  return false;
}

void CastContentBrowserClient::ExposeInterfacesToRenderer(
    service_manager::BinderRegistry* registry,
    content::AssociatedInterfaceRegistry* associated_registry,
    content::RenderProcessHost* render_process_host) {
  registry->AddInterface(
      base::Bind(&media::MediaCapsImpl::AddBinding,
                 base::Unretained(cast_browser_main_parts_->media_caps())),
      base::ThreadTaskRunnerHandle::Get());
}

void CastContentBrowserClient::RegisterInProcessServices(
    StaticServiceMap* services) {
#if BUILDFLAG(ENABLE_MOJO_MEDIA_IN_BROWSER_PROCESS)
  content::ServiceInfo info;
  info.factory = base::Bind(&CreateMediaService, base::Unretained(this));
  info.task_runner = GetMediaTaskRunner();
  services->insert(std::make_pair("media", info));
#endif
}

std::unique_ptr<base::Value>
CastContentBrowserClient::GetServiceManifestOverlay(
    base::StringPiece service_name) {
  ResourceBundle& rb = ResourceBundle::GetSharedInstance();
  int id = -1;
  if (service_name == content::mojom::kBrowserServiceName)
    id = IDR_CAST_CONTENT_BROWSER_MANIFEST_OVERLAY;
  else if (service_name == content::mojom::kPackagedServicesServiceName)
    id = IDR_CAST_CONTENT_PACKAGED_SERVICES_MANIFEST_OVERLAY;
  else
    return nullptr;
  base::StringPiece manifest_contents =
      rb.GetRawDataResourceForScale(id, ui::ScaleFactor::SCALE_FACTOR_NONE);
  return base::JSONReader::Read(manifest_contents);
}

void CastContentBrowserClient::GetAdditionalMappedFilesForChildProcess(
    const base::CommandLine& command_line,
    int child_process_id,
    content::FileDescriptorInfo* mappings) {
#if defined(OS_ANDROID)
  mappings->ShareWithRegion(
      kAndroidPakDescriptor,
      base::GlobalDescriptors::GetInstance()->Get(kAndroidPakDescriptor),
      base::GlobalDescriptors::GetInstance()->GetRegion(kAndroidPakDescriptor));
  breakpad::CrashDumpObserver::GetInstance()->BrowserChildProcessStarted(
      child_process_id, mappings);
#else
    int crash_signal_fd = GetCrashSignalFD(command_line);
  if (crash_signal_fd >= 0) {
    mappings->Share(kCrashDumpSignal, crash_signal_fd);
  }
#endif  // defined(OS_ANDROID)
}

void CastContentBrowserClient::GetAdditionalWebUISchemes(
    std::vector<std::string>* additional_schemes) {
  additional_schemes->push_back(kChromeResourceScheme);
}

content::DevToolsManagerDelegate*
CastContentBrowserClient::GetDevToolsManagerDelegate() {
  return new CastDevToolsManagerDelegate();
}

#if !defined(OS_ANDROID)
int CastContentBrowserClient::GetCrashSignalFD(
    const base::CommandLine& command_line) {
  std::string process_type =
      command_line.GetSwitchValueASCII(switches::kProcessType);

  if (process_type == switches::kRendererProcess ||
      process_type == switches::kGpuProcess ||
      process_type == switches::kUtilityProcess) {
    breakpad::CrashHandlerHostLinux* crash_handler =
        crash_handlers_[process_type];
    if (!crash_handler) {
      crash_handler = CreateCrashHandlerHost(process_type);
      crash_handlers_[process_type] = crash_handler;
    }
    return crash_handler->GetDeathSignalSocket();
  }

  return -1;
}

breakpad::CrashHandlerHostLinux*
CastContentBrowserClient::CreateCrashHandlerHost(
    const std::string& process_type) {
  // Let cast shell dump to /tmp. Internal minidump generator code can move it
  // to /data/minidumps later, since /data/minidumps is file lock-controlled.
  base::FilePath dumps_path;
  PathService::Get(base::DIR_TEMP, &dumps_path);

  // Alway set "upload" to false to use our own uploader.
  breakpad::CrashHandlerHostLinux* crash_handler =
    new breakpad::CrashHandlerHostLinux(
        process_type, dumps_path, false /* upload */);
  // StartUploaderThread() even though upload is diferred.
  // Breakpad-related memory is freed in the uploader thread.
  crash_handler->StartUploaderThread();
  return crash_handler;
}
#endif  // !defined(OS_ANDROID)

}  // namespace shell
}  // namespace chromecast
