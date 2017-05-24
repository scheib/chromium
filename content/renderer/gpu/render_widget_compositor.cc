// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/renderer/gpu/render_widget_compositor.h"

#include <stddef.h>

#include <cmath>
#include <limits>
#include <string>
#include <utility>

#include "base/callback.h"
#include "base/command_line.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/memory/ptr_util.h"
#include "base/numerics/safe_conversions.h"
#include "base/single_thread_task_runner.h"
#include "base/strings/string_number_conversions.h"
#include "base/synchronization/lock.h"
#include "base/sys_info.h"
#include "base/task_scheduler/post_task.h"
#include "base/task_scheduler/task_scheduler.h"
#include "base/task_scheduler/task_traits.h"
#include "base/threading/thread_task_runner_handle.h"
#include "base/time/time.h"
#include "base/values.h"
#include "build/build_config.h"
#include "cc/animation/animation_host.h"
#include "cc/animation/animation_timeline.h"
#include "cc/base/region.h"
#include "cc/base/switches.h"
#include "cc/benchmarks/micro_benchmark.h"
#include "cc/blink/web_layer_impl.h"
#include "cc/debug/layer_tree_debug_state.h"
#include "cc/input/layer_selection_bound.h"
#include "cc/layers/layer.h"
#include "cc/output/begin_frame_args.h"
#include "cc/output/copy_output_request.h"
#include "cc/output/copy_output_result.h"
#include "cc/output/latency_info_swap_promise.h"
#include "cc/output/swap_promise.h"
#include "cc/resources/single_release_callback.h"
#include "cc/scheduler/begin_frame_source.h"
#include "cc/trees/latency_info_swap_promise_monitor.h"
#include "cc/trees/layer_tree_host.h"
#include "cc/trees/layer_tree_mutator.h"
#include "content/common/content_switches_internal.h"
#include "content/common/layer_tree_settings_factory.h"
#include "content/public/common/content_client.h"
#include "content/public/common/content_switches.h"
#include "content/public/common/screen_info.h"
#include "content/public/renderer/content_renderer_client.h"
#include "content/renderer/gpu/render_widget_compositor_delegate.h"
#include "content/renderer/input/input_handler_manager.h"
#include "gpu/command_buffer/client/gles2_interface.h"
#include "gpu/command_buffer/service/gpu_switches.h"
#include "services/ui/public/cpp/gpu/context_provider_command_buffer.h"
#include "third_party/WebKit/public/platform/WebCompositeAndReadbackAsyncCallback.h"
#include "third_party/WebKit/public/platform/WebCompositorMutatorClient.h"
#include "third_party/WebKit/public/platform/WebLayoutAndPaintAsyncCallback.h"
#include "third_party/WebKit/public/platform/WebSize.h"
#include "third_party/WebKit/public/platform/scheduler/renderer/renderer_scheduler.h"
#include "third_party/WebKit/public/web/WebKit.h"
#include "third_party/WebKit/public/web/WebRuntimeFeatures.h"
#include "third_party/WebKit/public/web/WebSelection.h"
#include "ui/gfx/switches.h"
#include "ui/gl/gl_switches.h"
#include "ui/native_theme/native_theme_features.h"
#include "ui/native_theme/overlay_scrollbar_constants_aura.h"

namespace base {
class Value;
}

namespace cc {
class Layer;
}

using blink::WebFloatPoint;
using blink::WebRect;
using blink::WebSelection;
using blink::WebSize;
using blink::WebBrowserControlsState;

namespace content {
namespace {

using ReportTimeCallback = base::Callback<void(bool, double)>;

double MonotonicallyIncreasingTime() {
  return static_cast<double>(base::TimeTicks::Now().ToInternalValue()) /
         base::Time::kMicrosecondsPerSecond;
}

class ReportTimeSwapPromise : public cc::SwapPromise {
 public:
  ReportTimeSwapPromise(
      ReportTimeCallback callback,
      const scoped_refptr<base::SingleThreadTaskRunner>& task_runner);
  ~ReportTimeSwapPromise() override;

  void DidActivate() override {}
  void WillSwap(cc::CompositorFrameMetadata* metadata) override {}
  void DidSwap() override;
  DidNotSwapAction DidNotSwap(DidNotSwapReason reason) override;

  int64_t TraceId() const override;

 private:
  ReportTimeCallback callback_;
  scoped_refptr<base::SingleThreadTaskRunner> task_runner_;

  DISALLOW_COPY_AND_ASSIGN(ReportTimeSwapPromise);
};

ReportTimeSwapPromise::ReportTimeSwapPromise(
    ReportTimeCallback callback,
    const scoped_refptr<base::SingleThreadTaskRunner>& task_runner)
    : callback_(callback), task_runner_(task_runner) {}

ReportTimeSwapPromise::~ReportTimeSwapPromise() {}

void ReportTimeSwapPromise::DidSwap() {
  task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(callback_, true, MonotonicallyIncreasingTime()));
}

cc::SwapPromise::DidNotSwapAction ReportTimeSwapPromise::DidNotSwap(
    cc::SwapPromise::DidNotSwapReason reason) {
  task_runner_->PostTask(FROM_HERE, base::BindOnce(callback_, false, 0));
  return cc::SwapPromise::DidNotSwapAction::BREAK_PROMISE;
}

int64_t ReportTimeSwapPromise::TraceId() const {
  return 0;
}

bool GetSwitchValueAsInt(const base::CommandLine& command_line,
                         const std::string& switch_string,
                         int min_value,
                         int max_value,
                         int* result) {
  std::string string_value = command_line.GetSwitchValueASCII(switch_string);
  int int_value;
  if (base::StringToInt(string_value, &int_value) && int_value >= min_value &&
      int_value <= max_value) {
    *result = int_value;
    return true;
  } else {
    LOG(WARNING) << "Failed to parse switch " << switch_string << ": "
                 << string_value;
    return false;
  }
}

cc::LayerSelectionBound ConvertWebSelectionBound(
    const WebSelection& web_selection,
    bool is_start) {
  cc::LayerSelectionBound cc_bound;
  if (web_selection.IsNone())
    return cc_bound;

  const blink::WebSelectionBound& web_bound =
      is_start ? web_selection.Start() : web_selection.end();
  DCHECK(web_bound.layer_id);
  cc_bound.type = gfx::SelectionBound::CENTER;
  if (web_selection.IsRange()) {
    if (is_start) {
      cc_bound.type = web_bound.is_text_direction_rtl
                          ? gfx::SelectionBound::RIGHT
                          : gfx::SelectionBound::LEFT;
    } else {
      cc_bound.type = web_bound.is_text_direction_rtl
                          ? gfx::SelectionBound::LEFT
                          : gfx::SelectionBound::RIGHT;
    }
  }
  cc_bound.layer_id = web_bound.layer_id;
  cc_bound.edge_top = gfx::Point(web_bound.edge_top_in_layer);
  cc_bound.edge_bottom = gfx::Point(web_bound.edge_bottom_in_layer);
  return cc_bound;
}

cc::LayerSelection ConvertWebSelection(const WebSelection& web_selection) {
  cc::LayerSelection cc_selection;
  cc_selection.start = ConvertWebSelectionBound(web_selection, true);
  cc_selection.end = ConvertWebSelectionBound(web_selection, false);
  return cc_selection;
}

gfx::Size CalculateDefaultTileSize(float initial_device_scale_factor,
                                   const ScreenInfo& screen_info) {
  int default_tile_size = 256;
#if defined(OS_ANDROID)
  const gfx::Size screen_size = gfx::ScaleToFlooredSize(
      screen_info.rect.size(), screen_info.device_scale_factor);
  int display_width = screen_size.width();
  int display_height = screen_size.height();
  int numTiles = (display_width * display_height) / (256 * 256);
  if (numTiles > 16)
    default_tile_size = 384;
  if (numTiles >= 40)
    default_tile_size = 512;

  // Adjust for some resolutions that barely straddle an extra
  // tile when in portrait mode. This helps worst case scroll/raster
  // by not needing a full extra tile for each row.
  constexpr int tolerance = 10;  // To avoid rounding errors.
  int portrait_width = std::min(display_width, display_height);
  if (default_tile_size == 256 && std::abs(portrait_width - 768) < tolerance)
    default_tile_size += 32;
  if (default_tile_size == 384 && std::abs(portrait_width - 1200) < tolerance)
    default_tile_size += 32;
#elif defined(OS_CHROMEOS) || defined(OS_MACOSX)
  // Use 512 for high DPI (dsf=2.0f) devices.
  if (initial_device_scale_factor >= 2.0f)
    default_tile_size = 512;
#endif

  return gfx::Size(default_tile_size, default_tile_size);
}

// Check cc::BrowserControlsState, and blink::WebBrowserControlsState
// are kept in sync.
static_assert(int(blink::kWebBrowserControlsBoth) == int(cc::BOTH),
              "mismatching enums: BOTH");
static_assert(int(blink::kWebBrowserControlsHidden) == int(cc::HIDDEN),
              "mismatching enums: HIDDEN");
static_assert(int(blink::kWebBrowserControlsShown) == int(cc::SHOWN),
              "mismatching enums: SHOWN");

static cc::BrowserControlsState ConvertBrowserControlsState(
    WebBrowserControlsState state) {
  return static_cast<cc::BrowserControlsState>(state);
}

}  // namespace

// static
std::unique_ptr<RenderWidgetCompositor> RenderWidgetCompositor::Create(
    RenderWidgetCompositorDelegate* delegate,
    CompositorDependencies* compositor_deps) {
  std::unique_ptr<RenderWidgetCompositor> compositor(
      new RenderWidgetCompositor(delegate, compositor_deps));
  return compositor;
}

RenderWidgetCompositor::RenderWidgetCompositor(
    RenderWidgetCompositorDelegate* delegate,
    CompositorDependencies* compositor_deps)
    : num_failed_recreate_attempts_(0),
      delegate_(delegate),
      compositor_deps_(compositor_deps),
      threaded_(!!compositor_deps_->GetCompositorImplThreadTaskRunner()),
      never_visible_(false),
      is_for_oopif_(false),
      layout_and_paint_async_callback_(nullptr),
      weak_factory_(this) {}

void RenderWidgetCompositor::Initialize(
    std::unique_ptr<cc::LayerTreeHost> layer_tree_host,
    std::unique_ptr<cc::AnimationHost> animation_host) {
  DCHECK(layer_tree_host);
  DCHECK(animation_host);
  animation_host_ = std::move(animation_host);
  layer_tree_host_ = std::move(layer_tree_host);
}

RenderWidgetCompositor::~RenderWidgetCompositor() = default;

// static
std::unique_ptr<cc::LayerTreeHost> RenderWidgetCompositor::CreateLayerTreeHost(
    LayerTreeHostClient* client,
    cc::LayerTreeHostSingleThreadClient* single_thread_client,
    cc::MutatorHost* mutator_host,
    CompositorDependencies* deps,
    float device_scale_factor,
    const ScreenInfo& screen_info) {
  base::CommandLine* cmd = base::CommandLine::ForCurrentProcess();
  cc::LayerTreeSettings settings = GenerateLayerTreeSettings(
      *cmd, deps, device_scale_factor, client->IsForSubframe(), screen_info);

  const bool is_threaded = !!deps->GetCompositorImplThreadTaskRunner();

  std::unique_ptr<cc::LayerTreeHost> layer_tree_host;

  cc::LayerTreeHost::InitParams params;
  params.client = client;
  params.settings = &settings;
  params.task_graph_runner = deps->GetTaskGraphRunner();
  params.main_task_runner = deps->GetCompositorMainThreadTaskRunner();
  params.mutator_host = mutator_host;
  if (base::TaskScheduler::GetInstance()) {
    // The image worker thread needs to allow waiting since it makes discardable
    // shared memory allocations which need to make synchronous calls to the
    // IO thread.
    params.image_worker_task_runner = base::CreateSequencedTaskRunnerWithTraits(
        {base::WithBaseSyncPrimitives(), base::TaskPriority::BACKGROUND,
         base::TaskShutdownBehavior::CONTINUE_ON_SHUTDOWN});
  }
  if (!is_threaded) {
    // Single-threaded layout tests.
    layer_tree_host =
        cc::LayerTreeHost::CreateSingleThreaded(single_thread_client, &params);
  } else {
    layer_tree_host = cc::LayerTreeHost::CreateThreaded(
        deps->GetCompositorImplThreadTaskRunner(), &params);
  }

  return layer_tree_host;
}

// static
cc::LayerTreeSettings RenderWidgetCompositor::GenerateLayerTreeSettings(
    const base::CommandLine& cmd,
    CompositorDependencies* compositor_deps,
    float device_scale_factor,
    bool is_for_subframe,
    const ScreenInfo& screen_info) {
  cc::LayerTreeSettings settings;

  settings.is_layer_tree_for_subframe = is_for_subframe;

  // For web contents, layer transforms should scale up the contents of layers
  // to keep content always crisp when possible.
  settings.layer_transforms_should_scale_layer_contents = true;

  settings.main_frame_before_activation_enabled =
      cmd.HasSwitch(cc::switches::kEnableMainFrameBeforeActivation);

  settings.enable_checker_imaging =
      cmd.HasSwitch(cc::switches::kEnableCheckerImaging);

  // TODO(danakj): This should not be a setting O_O; it should change when the
  // device scale factor on LayerTreeHost changes.
  settings.default_tile_size =
      CalculateDefaultTileSize(device_scale_factor, screen_info);
  if (cmd.HasSwitch(switches::kDefaultTileWidth)) {
    int tile_width = 0;
    GetSwitchValueAsInt(cmd, switches::kDefaultTileWidth, 1,
                        std::numeric_limits<int>::max(), &tile_width);
    settings.default_tile_size.set_width(tile_width);
  }
  if (cmd.HasSwitch(switches::kDefaultTileHeight)) {
    int tile_height = 0;
    GetSwitchValueAsInt(cmd, switches::kDefaultTileHeight, 1,
                        std::numeric_limits<int>::max(), &tile_height);
    settings.default_tile_size.set_height(tile_height);
  }

  int max_untiled_layer_width = settings.max_untiled_layer_size.width();
  if (cmd.HasSwitch(switches::kMaxUntiledLayerWidth)) {
    GetSwitchValueAsInt(cmd, switches::kMaxUntiledLayerWidth, 1,
                        std::numeric_limits<int>::max(),
                        &max_untiled_layer_width);
  }
  int max_untiled_layer_height = settings.max_untiled_layer_size.height();
  if (cmd.HasSwitch(switches::kMaxUntiledLayerHeight)) {
    GetSwitchValueAsInt(cmd, switches::kMaxUntiledLayerHeight, 1,
                        std::numeric_limits<int>::max(),
                        &max_untiled_layer_height);
  }

  settings.max_untiled_layer_size =
      gfx::Size(max_untiled_layer_width, max_untiled_layer_height);

  settings.gpu_rasterization_msaa_sample_count =
      compositor_deps->GetGpuRasterizationMSAASampleCount();
  settings.gpu_rasterization_forced =
      compositor_deps->IsGpuRasterizationForced();
  settings.async_worker_context_enabled =
      compositor_deps->IsAsyncWorkerContextEnabled();

  settings.can_use_lcd_text = compositor_deps->IsLcdTextEnabled();
  settings.use_distance_field_text =
      compositor_deps->IsDistanceFieldTextEnabled();
  settings.use_zero_copy = compositor_deps->IsZeroCopyEnabled();
  settings.use_partial_raster = compositor_deps->IsPartialRasterEnabled();
  settings.enable_elastic_overscroll =
      compositor_deps->IsElasticOverscrollEnabled();
  settings.renderer_settings.use_gpu_memory_buffer_resources =
      compositor_deps->IsGpuMemoryBufferCompositorResourcesEnabled();
  settings.enable_color_correct_rasterization =
      cmd.HasSwitch(switches::kEnableColorCorrectRendering);
  settings.renderer_settings.enable_color_correct_rendering =
      cmd.HasSwitch(switches::kEnableColorCorrectRendering);
  settings.renderer_settings.buffer_to_texture_target_map =
      compositor_deps->GetBufferToTextureTargetMap();

  // Build LayerTreeSettings from command line args.
  LayerTreeSettingsFactory::SetBrowserControlsSettings(settings, cmd);

  settings.use_layer_lists = cmd.HasSwitch(cc::switches::kEnableLayerLists);

  settings.renderer_settings.allow_antialiasing &=
      !cmd.HasSwitch(cc::switches::kDisableCompositedAntialiasing);
  // The means the renderer compositor has 2 possible modes:
  // - Threaded compositing with a scheduler.
  // - Single threaded compositing without a scheduler (for layout tests only).
  // Using the scheduler in layout tests introduces additional composite steps
  // that create flakiness.
  settings.single_thread_proxy_scheduler = false;

  // These flags should be mirrored by UI versions in ui/compositor/.
  if (cmd.HasSwitch(cc::switches::kShowCompositedLayerBorders))
    settings.initial_debug_state.show_debug_borders.set();
  settings.initial_debug_state.show_layer_animation_bounds_rects =
      cmd.HasSwitch(cc::switches::kShowLayerAnimationBounds);
  settings.initial_debug_state.show_paint_rects =
      cmd.HasSwitch(switches::kShowPaintRects);
  settings.initial_debug_state.show_property_changed_rects =
      cmd.HasSwitch(cc::switches::kShowPropertyChangedRects);
  settings.initial_debug_state.show_surface_damage_rects =
      cmd.HasSwitch(cc::switches::kShowSurfaceDamageRects);
  settings.initial_debug_state.show_screen_space_rects =
      cmd.HasSwitch(cc::switches::kShowScreenSpaceRects);

  settings.initial_debug_state.SetRecordRenderingStats(
      cmd.HasSwitch(cc::switches::kEnableGpuBenchmarking));
  settings.enable_surface_synchronization =
      cmd.HasSwitch(cc::switches::kEnableSurfaceSynchronization);

  if (cmd.HasSwitch(cc::switches::kSlowDownRasterScaleFactor)) {
    const int kMinSlowDownScaleFactor = 0;
    const int kMaxSlowDownScaleFactor = INT_MAX;
    GetSwitchValueAsInt(
        cmd, cc::switches::kSlowDownRasterScaleFactor, kMinSlowDownScaleFactor,
        kMaxSlowDownScaleFactor,
        &settings.initial_debug_state.slow_down_raster_scale_factor);
  }

  // This is default overlay scrollbar settings for Android and DevTools mobile
  // emulator. Aura Overlay Scrollbar will override below.
  settings.scrollbar_animator = cc::LayerTreeSettings::ANDROID_OVERLAY;
  settings.solid_color_scrollbar_color = SkColorSetARGB(128, 128, 128, 128);
  settings.scrollbar_fade_delay = base::TimeDelta::FromMilliseconds(300);
  settings.scrollbar_fade_duration = base::TimeDelta::FromMilliseconds(300);

#if defined(OS_ANDROID)
  bool using_synchronous_compositor =
      GetContentClient()->UsingSynchronousCompositing();

  settings.use_stream_video_draw_quad = true;
  settings.using_synchronous_renderer_compositor = using_synchronous_compositor;
  if (using_synchronous_compositor) {
    // Android WebView uses system scrollbars, so make ours invisible.
    // http://crbug.com/677348: This can't be done using hide_scrollbars
    // setting because supporting -webkit custom scrollbars is still desired
    // on sublayers.
    settings.scrollbar_animator = cc::LayerTreeSettings::NO_ANIMATOR;
    settings.solid_color_scrollbar_color = SK_ColorTRANSPARENT;
  }
  settings.renderer_settings.highp_threshold_min = 2048;
  // Android WebView handles root layer flings itself.
  settings.ignore_root_layer_flings = using_synchronous_compositor;
  // Memory policy on Android WebView does not depend on whether device is
  // low end, so always use default policy.
  bool use_low_memory_policy =
      base::SysInfo::IsLowEndDevice() && !using_synchronous_compositor;
  if (use_low_memory_policy) {
    // On low-end we want to be very carefull about killing other
    // apps. So initially we use 50% more memory to avoid flickering
    // or raster-on-demand.
    settings.max_memory_for_prepaint_percentage = 67;

    // RGBA_4444 textures are only enabled by default for low end devices
    // and are disabled for Android WebView as it doesn't support the format.
    if (!cmd.HasSwitch(switches::kDisableRGBA4444Textures) &&
        base::SysInfo::AmountOfPhysicalMemoryMB() <= 512)
      settings.renderer_settings.preferred_tile_format = cc::RGBA_4444;
  } else {
    // On other devices we have increased memory excessively to avoid
    // raster-on-demand already, so now we reserve 50% _only_ to avoid
    // raster-on-demand, and use 50% of the memory otherwise.
    settings.max_memory_for_prepaint_percentage = 50;
  }
  // Webview does not own the surface so should not clear it.
  settings.renderer_settings.should_clear_root_render_pass =
      !using_synchronous_compositor;

  if (base::SysInfo::IsLowEndDevice()) {
    // When running on a low end device, we limit cached bytes to 2MB.
    // This allows a typical page to fit its images in cache, but prevents
    // most long-term caching.
    settings.decoded_image_cache_budget_bytes = 2 * 1024 * 1024;
  }

  // TODO(danakj): Only do this on low end devices.
  settings.create_low_res_tiling = true;
#else  // defined(OS_ANDROID)
  if (ui::IsOverlayScrollbarEnabled()) {
    settings.scrollbar_animator = cc::LayerTreeSettings::AURA_OVERLAY;
    settings.scrollbar_fade_delay = ui::kOverlayScrollbarFadeDelay;
    settings.scrollbar_fade_duration = ui::kOverlayScrollbarFadeDuration;
    settings.scrollbar_thinning_duration =
        ui::kOverlayScrollbarThinningDuration;
  }

  // On desktop, if there's over 4GB of memory on the machine, increase the
  // image decode budget to 256MB for both gpu and software.
  const int kImageDecodeMemoryThresholdMB = 4 * 1024;
  if (base::SysInfo::AmountOfPhysicalMemoryMB() >=
      kImageDecodeMemoryThresholdMB) {
    settings.decoded_image_cache_budget_bytes = 256 * 1024 * 1024;
    settings.decoded_image_working_set_budget_bytes = 256 * 1024 * 1024;
  } else {
    // These are the defaults, but recorded here as well.
    settings.decoded_image_cache_budget_bytes = 128 * 1024 * 1024;
    settings.decoded_image_working_set_budget_bytes = 128 * 1024 * 1024;
  }

#endif  // defined(OS_ANDROID)

  if (cmd.HasSwitch(switches::kEnableLowResTiling))
    settings.create_low_res_tiling = true;
  if (cmd.HasSwitch(switches::kDisableLowResTiling))
    settings.create_low_res_tiling = false;

  if (cmd.HasSwitch(switches::kEnableRGBA4444Textures) &&
      !cmd.HasSwitch(switches::kDisableRGBA4444Textures)) {
    settings.renderer_settings.preferred_tile_format = cc::RGBA_4444;
  }

  if (cmd.HasSwitch(cc::switches::kEnableTileCompression)) {
    settings.renderer_settings.preferred_tile_format = cc::ETC1;
  }

  settings.max_staging_buffer_usage_in_bytes = 32 * 1024 * 1024;  // 32MB
  // Use 1/4th of staging buffers on low-end devices.
  if (base::SysInfo::IsLowEndDevice())
    settings.max_staging_buffer_usage_in_bytes /= 4;

  cc::ManagedMemoryPolicy defaults = settings.gpu_memory_policy;
  settings.gpu_memory_policy = GetGpuMemoryPolicy(defaults);
  settings.software_memory_policy.num_resources_limit =
      base::SharedMemory::GetHandleLimit() / 3;

  settings.disallow_non_exact_resource_reuse =
      cmd.HasSwitch(cc::switches::kDisallowNonExactResourceReuse);
  settings.renderer_settings.disallow_non_exact_resource_reuse =
      settings.disallow_non_exact_resource_reuse;

  return settings;
}

// static
cc::ManagedMemoryPolicy RenderWidgetCompositor::GetGpuMemoryPolicy(
    const cc::ManagedMemoryPolicy& default_policy) {
  cc::ManagedMemoryPolicy actual = default_policy;
  actual.bytes_limit_when_visible = 0;

  // If the value was overridden on the command line, use the specified value.
  static bool client_hard_limit_bytes_overridden =
      base::CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kForceGpuMemAvailableMb);
  if (client_hard_limit_bytes_overridden) {
    if (base::StringToSizeT(
            base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII(
                switches::kForceGpuMemAvailableMb),
            &actual.bytes_limit_when_visible))
      actual.bytes_limit_when_visible *= 1024 * 1024;
    return actual;
  }

#if defined(OS_ANDROID)
  // We can't query available GPU memory from the system on Android.
  // Physical memory is also mis-reported sometimes (eg. Nexus 10 reports
  // 1262MB when it actually has 2GB, while Razr M has 1GB but only reports
  // 128MB java heap size). First we estimate physical memory using both.
  size_t dalvik_mb = base::SysInfo::DalvikHeapSizeMB();
  size_t physical_mb = base::SysInfo::AmountOfPhysicalMemoryMB();
  size_t physical_memory_mb = 0;
  if (dalvik_mb >= 256)
    physical_memory_mb = dalvik_mb * 4;
  else
    physical_memory_mb = std::max(dalvik_mb * 4, (physical_mb * 4) / 3);

  // Now we take a default of 1/8th of memory on high-memory devices,
  // and gradually scale that back for low-memory devices (to be nicer
  // to other apps so they don't get killed). Examples:
  // Nexus 4/10(2GB)    256MB (normally 128MB)
  // Droid Razr M(1GB)  114MB (normally 57MB)
  // Galaxy Nexus(1GB)  100MB (normally 50MB)
  // Xoom(1GB)          100MB (normally 50MB)
  // Nexus S(low-end)   8MB (normally 8MB)
  // Note that the compositor now uses only some of this memory for
  // pre-painting and uses the rest only for 'emergencies'.
  if (actual.bytes_limit_when_visible == 0) {
    // NOTE: Non-low-end devices use only 50% of these limits,
    // except during 'emergencies' where 100% can be used.
    if (physical_memory_mb >= 1536)
      actual.bytes_limit_when_visible = physical_memory_mb / 8;  // >192MB
    else if (physical_memory_mb >= 1152)
      actual.bytes_limit_when_visible = physical_memory_mb / 8;  // >144MB
    else if (physical_memory_mb >= 768)
      actual.bytes_limit_when_visible = physical_memory_mb / 10;  // >76MB
    else if (physical_memory_mb >= 513)
      actual.bytes_limit_when_visible = physical_memory_mb / 12;  // <64MB
    else
      // Devices with this little RAM have very little headroom so we hardcode
      // the limit rather than relying on the heuristics above.  (They also use
      // 4444 textures so we can use a lower limit.)
      actual.bytes_limit_when_visible = 8;

    actual.bytes_limit_when_visible =
        actual.bytes_limit_when_visible * 1024 * 1024;
    // Clamp the observed value to a specific range on Android.
    actual.bytes_limit_when_visible = std::max(
        actual.bytes_limit_when_visible, static_cast<size_t>(8 * 1024 * 1024));
    actual.bytes_limit_when_visible =
        std::min(actual.bytes_limit_when_visible,
                 static_cast<size_t>(256 * 1024 * 1024));
  }
  actual.priority_cutoff_when_visible =
      gpu::MemoryAllocation::CUTOFF_ALLOW_EVERYTHING;
#else
  // Ignore what the system said and give all clients the same maximum
  // allocation on desktop platforms.
  actual.bytes_limit_when_visible = 512 * 1024 * 1024;
  actual.priority_cutoff_when_visible =
      gpu::MemoryAllocation::CUTOFF_ALLOW_NICE_TO_HAVE;
#endif
  return actual;
}

void RenderWidgetCompositor::SetNeverVisible() {
  DCHECK(!layer_tree_host_->IsVisible());
  never_visible_ = true;
}

const base::WeakPtr<cc::InputHandler>&
RenderWidgetCompositor::GetInputHandler() {
  return layer_tree_host_->GetInputHandler();
}

void RenderWidgetCompositor::SetNeedsDisplayOnAllLayers() {
  layer_tree_host_->SetNeedsDisplayOnAllLayers();
}

void RenderWidgetCompositor::SetRasterizeOnlyVisibleContent() {
  cc::LayerTreeDebugState current = layer_tree_host_->GetDebugState();
  current.rasterize_only_visible_content = true;
  layer_tree_host_->SetDebugState(current);
}

void RenderWidgetCompositor::SetNeedsRedrawRect(gfx::Rect damage_rect) {
  layer_tree_host_->SetNeedsRedrawRect(damage_rect);
}

void RenderWidgetCompositor::SetNeedsForcedRedraw() {
  layer_tree_host_->SetNeedsCommitWithForcedRedraw();
}

std::unique_ptr<cc::SwapPromiseMonitor>
RenderWidgetCompositor::CreateLatencyInfoSwapPromiseMonitor(
    ui::LatencyInfo* latency) {
  return base::MakeUnique<cc::LatencyInfoSwapPromiseMonitor>(
      latency, layer_tree_host_->GetSwapPromiseManager(), nullptr);
}

void RenderWidgetCompositor::QueueSwapPromise(
    std::unique_ptr<cc::SwapPromise> swap_promise) {
  layer_tree_host_->QueueSwapPromise(std::move(swap_promise));
}

int RenderWidgetCompositor::GetSourceFrameNumber() const {
  return layer_tree_host_->SourceFrameNumber();
}

void RenderWidgetCompositor::NotifyInputThrottledUntilCommit() {
  layer_tree_host_->NotifyInputThrottledUntilCommit();
}

const cc::Layer* RenderWidgetCompositor::GetRootLayer() const {
  return layer_tree_host_->root_layer();
}

int RenderWidgetCompositor::ScheduleMicroBenchmark(
    const std::string& name,
    std::unique_ptr<base::Value> value,
    const base::Callback<void(std::unique_ptr<base::Value>)>& callback) {
  return layer_tree_host_->ScheduleMicroBenchmark(name, std::move(value),
                                                  callback);
}

bool RenderWidgetCompositor::SendMessageToMicroBenchmark(
    int id,
    std::unique_ptr<base::Value> value) {
  return layer_tree_host_->SendMessageToMicroBenchmark(id, std::move(value));
}

cc::FrameSinkId RenderWidgetCompositor::GetFrameSinkId() {
  return frame_sink_id_;
}

void RenderWidgetCompositor::SetRootLayer(const blink::WebLayer& layer) {
  layer_tree_host_->SetRootLayer(
      static_cast<const cc_blink::WebLayerImpl*>(&layer)->layer());
}

void RenderWidgetCompositor::ClearRootLayer() {
  layer_tree_host_->SetRootLayer(scoped_refptr<cc::Layer>());
}

cc::AnimationHost* RenderWidgetCompositor::CompositorAnimationHost() {
  return animation_host_.get();
}

void RenderWidgetCompositor::SetViewportSize(
    const WebSize& device_viewport_size) {
  layer_tree_host_->SetViewportSize(device_viewport_size);
}

WebSize RenderWidgetCompositor::GetViewportSize() const {
  return layer_tree_host_->device_viewport_size();
}

WebFloatPoint RenderWidgetCompositor::adjustEventPointForPinchZoom(
    const WebFloatPoint& point) const {
  return point;
}

void RenderWidgetCompositor::SetDeviceScaleFactor(float device_scale) {
  layer_tree_host_->SetDeviceScaleFactor(device_scale);
}

void RenderWidgetCompositor::SetBackgroundColor(blink::WebColor color) {
  layer_tree_host_->set_background_color(color);
  layer_tree_host_->set_has_transparent_background(SkColorGetA(color) <
                                                   SK_AlphaOPAQUE);
}

void RenderWidgetCompositor::SetVisible(bool visible) {
  if (never_visible_)
    return;

  layer_tree_host_->SetVisible(visible);
}

void RenderWidgetCompositor::SetPageScaleFactorAndLimits(
    float page_scale_factor,
    float minimum,
    float maximum) {
  layer_tree_host_->SetPageScaleFactorAndLimits(page_scale_factor, minimum,
                                                maximum);
}

void RenderWidgetCompositor::StartPageScaleAnimation(
    const blink::WebPoint& destination,
    bool use_anchor,
    float new_page_scale,
    double duration_sec) {
  base::TimeDelta duration = base::TimeDelta::FromMicroseconds(
      duration_sec * base::Time::kMicrosecondsPerSecond);
  layer_tree_host_->StartPageScaleAnimation(
      gfx::Vector2d(destination.x, destination.y), use_anchor, new_page_scale,
      duration);
}

bool RenderWidgetCompositor::HasPendingPageScaleAnimation() const {
  return layer_tree_host_->HasPendingPageScaleAnimation();
}

void RenderWidgetCompositor::HeuristicsForGpuRasterizationUpdated(
    bool matches_heuristics) {
  layer_tree_host_->SetHasGpuRasterizationTrigger(matches_heuristics);
}

void RenderWidgetCompositor::SetNeedsBeginFrame() {
  layer_tree_host_->SetNeedsAnimate();
}

void RenderWidgetCompositor::DidStopFlinging() {
  layer_tree_host_->DidStopFlinging();
}

void RenderWidgetCompositor::RegisterViewportLayers(
    const blink::WebLayer* overscroll_elasticity_layer,
    const blink::WebLayer* page_scale_layer,
    const blink::WebLayer* inner_viewport_container_layer,
    const blink::WebLayer* outer_viewport_container_layer,
    const blink::WebLayer* inner_viewport_scroll_layer,
    const blink::WebLayer* outer_viewport_scroll_layer) {
  cc::LayerTreeHost::ViewportLayers viewport_layers;
  // TODO(bokan): This check can probably be removed now, but it looks
  // like overscroll elasticity may still be nullptr until VisualViewport
  // registers its layers.
  if (overscroll_elasticity_layer) {
    viewport_layers.overscroll_elasticity =
        static_cast<const cc_blink::WebLayerImpl*>(overscroll_elasticity_layer)
            ->layer();
  }
  viewport_layers.page_scale =
      static_cast<const cc_blink::WebLayerImpl*>(page_scale_layer)->layer();
  if (inner_viewport_container_layer) {
    viewport_layers.inner_viewport_container =
        static_cast<const cc_blink::WebLayerImpl*>(
            inner_viewport_container_layer)
            ->layer();
  }
  if (outer_viewport_container_layer) {
    viewport_layers.outer_viewport_container =
        static_cast<const cc_blink::WebLayerImpl*>(
            outer_viewport_container_layer)
            ->layer();
  }
  viewport_layers.inner_viewport_scroll =
      static_cast<const cc_blink::WebLayerImpl*>(inner_viewport_scroll_layer)
          ->layer();
  // TODO(bokan): This check can probably be removed now, but it looks
  // like overscroll elasticity may still be nullptr until VisualViewport
  // registers its layers.
  if (outer_viewport_scroll_layer) {
    viewport_layers.outer_viewport_scroll =
        static_cast<const cc_blink::WebLayerImpl*>(outer_viewport_scroll_layer)
            ->layer();
  }
  layer_tree_host_->RegisterViewportLayers(viewport_layers);
}

void RenderWidgetCompositor::ClearViewportLayers() {
  layer_tree_host_->RegisterViewportLayers(cc::LayerTreeHost::ViewportLayers());
}

void RenderWidgetCompositor::RegisterSelection(
    const blink::WebSelection& selection) {
  layer_tree_host_->RegisterSelection(ConvertWebSelection(selection));
}

void RenderWidgetCompositor::ClearSelection() {
  cc::LayerSelection empty_selection;
  layer_tree_host_->RegisterSelection(empty_selection);
}

void RenderWidgetCompositor::SetMutatorClient(
    std::unique_ptr<blink::WebCompositorMutatorClient> client) {
  TRACE_EVENT0("compositor-worker", "RenderWidgetCompositor::setMutatorClient");
  layer_tree_host_->SetLayerTreeMutator(std::move(client));
}

void RenderWidgetCompositor::ForceRecalculateRasterScales() {
  layer_tree_host_->SetNeedsRecalculateRasterScales();
}

static_assert(static_cast<cc::EventListenerClass>(
                  blink::WebEventListenerClass::kTouchStartOrMove) ==
                  cc::EventListenerClass::kTouchStartOrMove,
              "EventListenerClass and WebEventListenerClass enums must match");
static_assert(static_cast<cc::EventListenerClass>(
                  blink::WebEventListenerClass::kMouseWheel) ==
                  cc::EventListenerClass::kMouseWheel,
              "EventListenerClass and WebEventListenerClass enums must match");

static_assert(static_cast<cc::EventListenerProperties>(
                  blink::WebEventListenerProperties::kNothing) ==
                  cc::EventListenerProperties::kNone,
              "EventListener and WebEventListener enums must match");
static_assert(static_cast<cc::EventListenerProperties>(
                  blink::WebEventListenerProperties::kPassive) ==
                  cc::EventListenerProperties::kPassive,
              "EventListener and WebEventListener enums must match");
static_assert(static_cast<cc::EventListenerProperties>(
                  blink::WebEventListenerProperties::kBlocking) ==
                  cc::EventListenerProperties::kBlocking,
              "EventListener and WebEventListener enums must match");
static_assert(static_cast<cc::EventListenerProperties>(
                  blink::WebEventListenerProperties::kBlockingAndPassive) ==
                  cc::EventListenerProperties::kBlockingAndPassive,
              "EventListener and WebEventListener enums must match");

void RenderWidgetCompositor::SetEventListenerProperties(
    blink::WebEventListenerClass eventClass,
    blink::WebEventListenerProperties properties) {
  layer_tree_host_->SetEventListenerProperties(
      static_cast<cc::EventListenerClass>(eventClass),
      static_cast<cc::EventListenerProperties>(properties));
}

void RenderWidgetCompositor::UpdateEventRectsForSubframeIfNecessary() {
  if (!is_for_oopif_)
    return;

  // If this is an oopif sub-frame compositor, we won't be getting TouchRects
  // from ScrollingCoordinator, so to make sure touch events are handled
  // properly, mark the entire root layer as a TouchRect.
  // TODO(wjmaclean): remove this when ScrollingCoordinator is made per-frame,
  // as opposed to per-page.
  using blink::WebEventListenerProperties;
  using blink::WebEventListenerClass;

  WebEventListenerProperties touch_start_properties =
      EventListenerProperties(WebEventListenerClass::kTouchStartOrMove);
  WebEventListenerProperties touch_end_cancel_properties =
      EventListenerProperties(WebEventListenerClass::kTouchEndOrCancel);
  bool has_touch_handlers =
      touch_start_properties == WebEventListenerProperties::kBlocking ||
      touch_start_properties ==
          WebEventListenerProperties::kBlockingAndPassive ||
      touch_end_cancel_properties == WebEventListenerProperties::kBlocking ||
      touch_end_cancel_properties ==
          WebEventListenerProperties::kBlockingAndPassive;

  WebEventListenerProperties wheel_event_properties =
      EventListenerProperties(WebEventListenerClass::kMouseWheel);
  bool has_wheel_handlers =
      wheel_event_properties == WebEventListenerProperties::kBlocking ||
      wheel_event_properties == WebEventListenerProperties::kBlockingAndPassive;

  cc::Layer* root_layer = layer_tree_host_->root_layer();
  cc::Region touch_handler_region;
  if (has_touch_handlers)
    touch_handler_region = gfx::Rect(gfx::Point(), root_layer->bounds());
  root_layer->SetTouchEventHandlerRegion(touch_handler_region);

  cc::Region wheel_handler_region;
  if (has_wheel_handlers)
    wheel_handler_region = gfx::Rect(gfx::Point(), root_layer->bounds());
  root_layer->SetNonFastScrollableRegion(wheel_handler_region);
}

blink::WebEventListenerProperties
RenderWidgetCompositor::EventListenerProperties(
    blink::WebEventListenerClass event_class) const {
  return static_cast<blink::WebEventListenerProperties>(
      layer_tree_host_->event_listener_properties(
          static_cast<cc::EventListenerClass>(event_class)));
}

void RenderWidgetCompositor::SetHaveScrollEventHandlers(bool has_handlers) {
  layer_tree_host_->SetHaveScrollEventHandlers(has_handlers);
}

bool RenderWidgetCompositor::HaveScrollEventHandlers() const {
  return layer_tree_host_->have_scroll_event_handlers();
}

void CompositeAndReadbackAsyncCallback(
    blink::WebCompositeAndReadbackAsyncCallback* callback,
    std::unique_ptr<cc::CopyOutputResult> result) {
  if (result->HasBitmap()) {
    std::unique_ptr<SkBitmap> result_bitmap = result->TakeBitmap();
    callback->DidCompositeAndReadback(*result_bitmap);
  } else {
    callback->DidCompositeAndReadback(SkBitmap());
  }
}

bool RenderWidgetCompositor::CompositeIsSynchronous() const {
  if (!threaded_) {
    DCHECK(!layer_tree_host_->GetSettings().single_thread_proxy_scheduler);
    return true;
  }
  return false;
}

void RenderWidgetCompositor::LayoutAndPaintAsync(
    blink::WebLayoutAndPaintAsyncCallback* callback) {
  DCHECK(!layout_and_paint_async_callback_);
  layout_and_paint_async_callback_ = callback;

  if (CompositeIsSynchronous()) {
    base::ThreadTaskRunnerHandle::Get()->PostTask(
        FROM_HERE, base::Bind(&RenderWidgetCompositor::LayoutAndUpdateLayers,
                              weak_factory_.GetWeakPtr()));
  } else {
    layer_tree_host_->SetNeedsCommit();
  }
}

void RenderWidgetCompositor::SetCompositorFrameSink(
    std::unique_ptr<cc::CompositorFrameSink> compositor_frame_sink) {
  if (!compositor_frame_sink) {
    DidFailToInitializeCompositorFrameSink();
    return;
  }
  layer_tree_host_->SetCompositorFrameSink(std::move(compositor_frame_sink));
}

void RenderWidgetCompositor::LayoutAndUpdateLayers() {
  DCHECK(CompositeIsSynchronous());
  layer_tree_host_->LayoutAndUpdateLayers();
  InvokeLayoutAndPaintCallback();
}

void RenderWidgetCompositor::InvokeLayoutAndPaintCallback() {
  if (!layout_and_paint_async_callback_)
    return;
  layout_and_paint_async_callback_->DidLayoutAndPaint();
  layout_and_paint_async_callback_ = nullptr;
}

void RenderWidgetCompositor::CompositeAndReadbackAsync(
    blink::WebCompositeAndReadbackAsyncCallback* callback) {
  DCHECK(!layout_and_paint_async_callback_);
  scoped_refptr<base::SingleThreadTaskRunner> main_thread_task_runner =
      layer_tree_host_->GetTaskRunnerProvider()->MainThreadTaskRunner();
  std::unique_ptr<cc::CopyOutputRequest> request =
      cc::CopyOutputRequest::CreateBitmapRequest(base::Bind(
          [](blink::WebCompositeAndReadbackAsyncCallback* callback,
             scoped_refptr<base::SingleThreadTaskRunner> task_runner,
             std::unique_ptr<cc::CopyOutputResult> result) {
            task_runner->PostTask(FROM_HERE,
                                  base::Bind(&CompositeAndReadbackAsyncCallback,
                                             callback, base::Passed(&result)));
          },
          callback, base::Passed(&main_thread_task_runner)));
  // Force a redraw to ensure that the copy swap promise isn't cancelled due to
  // no damage.
  SetNeedsForcedRedraw();
  layer_tree_host_->QueueSwapPromise(
      delegate_->RequestCopyOfOutputForLayoutTest(std::move(request)));

  // Force a commit to happen. The temporary copy output request will
  // be installed after layout which will happen as a part of the commit, for
  // widgets that delay the creation of their output surface.
  if (CompositeIsSynchronous()) {
    base::ThreadTaskRunnerHandle::Get()->PostTask(
        FROM_HERE, base::Bind(&RenderWidgetCompositor::SynchronouslyComposite,
                              weak_factory_.GetWeakPtr()));
  } else {
    layer_tree_host_->SetNeedsCommit();
  }
}

void RenderWidgetCompositor::SynchronouslyComposite() {
  DCHECK(CompositeIsSynchronous());
  layer_tree_host_->Composite(base::TimeTicks::Now());
}

void RenderWidgetCompositor::SetDeferCommits(bool defer_commits) {
  layer_tree_host_->SetDeferCommits(defer_commits);
}

int RenderWidgetCompositor::LayerTreeId() const {
  return layer_tree_host_->GetId();
}

void RenderWidgetCompositor::SetShowFPSCounter(bool show) {
  cc::LayerTreeDebugState debug_state = layer_tree_host_->GetDebugState();
  debug_state.show_fps_counter = show;
  layer_tree_host_->SetDebugState(debug_state);
}

void RenderWidgetCompositor::SetShowPaintRects(bool show) {
  cc::LayerTreeDebugState debug_state = layer_tree_host_->GetDebugState();
  debug_state.show_paint_rects = show;
  layer_tree_host_->SetDebugState(debug_state);
}

void RenderWidgetCompositor::SetShowDebugBorders(bool show) {
  cc::LayerTreeDebugState debug_state = layer_tree_host_->GetDebugState();
  if (show)
    debug_state.show_debug_borders.set();
  else
    debug_state.show_debug_borders.reset();
  layer_tree_host_->SetDebugState(debug_state);
}

void RenderWidgetCompositor::SetShowScrollBottleneckRects(bool show) {
  cc::LayerTreeDebugState debug_state = layer_tree_host_->GetDebugState();
  debug_state.show_touch_event_handler_rects = show;
  debug_state.show_wheel_event_handler_rects = show;
  debug_state.show_non_fast_scrollable_rects = show;
  layer_tree_host_->SetDebugState(debug_state);
}

void RenderWidgetCompositor::UpdateBrowserControlsState(
    WebBrowserControlsState constraints,
    WebBrowserControlsState current,
    bool animate) {
  layer_tree_host_->UpdateBrowserControlsState(
      ConvertBrowserControlsState(constraints),
      ConvertBrowserControlsState(current), animate);
}

void RenderWidgetCompositor::SetBrowserControlsHeight(float height,
                                                      bool shrink) {
  layer_tree_host_->SetBrowserControlsHeight(height, shrink);
}

void RenderWidgetCompositor::SetBrowserControlsShownRatio(float ratio) {
  layer_tree_host_->SetBrowserControlsShownRatio(ratio);
}

void RenderWidgetCompositor::setBottomControlsHeight(float height) {
  layer_tree_host_->SetBottomControlsHeight(height);
}

void RenderWidgetCompositor::WillBeginMainFrame() {
  delegate_->WillBeginCompositorFrame();
}

void RenderWidgetCompositor::DidBeginMainFrame() {}

void RenderWidgetCompositor::BeginMainFrame(const cc::BeginFrameArgs& args) {
  compositor_deps_->GetRendererScheduler()->WillBeginFrame(args);
  double frame_time_sec = (args.frame_time - base::TimeTicks()).InSecondsF();
  delegate_->BeginMainFrame(frame_time_sec);
}

void RenderWidgetCompositor::BeginMainFrameNotExpectedSoon() {
  compositor_deps_->GetRendererScheduler()->BeginFrameNotExpectedSoon();
}

void RenderWidgetCompositor::BeginMainFrameNotExpectedUntil(
    base::TimeTicks time) {
  compositor_deps_->GetRendererScheduler()->BeginMainFrameNotExpectedUntil(
      time);
}

void RenderWidgetCompositor::UpdateLayerTreeHost() {
  delegate_->UpdateVisualState();
}

void RenderWidgetCompositor::ApplyViewportDeltas(
    const gfx::Vector2dF& inner_delta,
    const gfx::Vector2dF& outer_delta,
    const gfx::Vector2dF& elastic_overscroll_delta,
    float page_scale,
    float top_controls_delta) {
  delegate_->ApplyViewportDeltas(inner_delta, outer_delta,
                                 elastic_overscroll_delta, page_scale,
                                 top_controls_delta);
}

void RenderWidgetCompositor::RecordWheelAndTouchScrollingCount(
    bool has_scrolled_by_wheel,
    bool has_scrolled_by_touch) {
  delegate_->RecordWheelAndTouchScrollingCount(has_scrolled_by_wheel,
                                               has_scrolled_by_touch);
}

void RenderWidgetCompositor::RequestNewCompositorFrameSink() {
  // If the host is closing, then no more compositing is possible.  This
  // prevents shutdown races between handling the close message and
  // the CreateCompositorFrameSink task.
  if (delegate_->IsClosing())
    return;

  bool fallback = num_failed_recreate_attempts_ >=
                  COMPOSITOR_FRAME_SINK_RETRIES_BEFORE_FALLBACK;

#ifdef OS_ANDROID
  LOG_IF(FATAL, fallback) << "Android does not support fallback frame sinks.";
#endif

  delegate_->RequestNewCompositorFrameSink(
      fallback, base::Bind(&RenderWidgetCompositor::SetCompositorFrameSink,
                           weak_factory_.GetWeakPtr()));
}

void RenderWidgetCompositor::DidInitializeCompositorFrameSink() {
  num_failed_recreate_attempts_ = 0;
}

void RenderWidgetCompositor::DidFailToInitializeCompositorFrameSink() {
  ++num_failed_recreate_attempts_;
  // Tolerate a certain number of recreation failures to work around races
  // in the output-surface-lost machinery.
  LOG_IF(FATAL,
         (num_failed_recreate_attempts_ >= MAX_COMPOSITOR_FRAME_SINK_RETRIES))
      << "Failed to create a fallback CompositorFrameSink.";

  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE,
      base::Bind(&RenderWidgetCompositor::RequestNewCompositorFrameSink,
                 weak_factory_.GetWeakPtr()));
}

void RenderWidgetCompositor::WillCommit() {
  InvokeLayoutAndPaintCallback();
}

void RenderWidgetCompositor::DidCommit() {
  delegate_->DidCommitCompositorFrame();
  compositor_deps_->GetRendererScheduler()->DidCommitFrameToCompositor();
}

void RenderWidgetCompositor::DidCommitAndDrawFrame() {
  delegate_->DidCommitAndDrawCompositorFrame();
}

void RenderWidgetCompositor::DidReceiveCompositorFrameAck() {
  delegate_->DidReceiveCompositorFrameAck();
}

void RenderWidgetCompositor::DidCompletePageScaleAnimation() {
  delegate_->DidCompletePageScaleAnimation();
}

bool RenderWidgetCompositor::IsForSubframe() {
  return is_for_oopif_;
}

void RenderWidgetCompositor::RequestScheduleAnimation() {
  delegate_->RequestScheduleAnimation();
}

void RenderWidgetCompositor::DidSubmitCompositorFrame() {}

void RenderWidgetCompositor::DidLoseCompositorFrameSink() {}

void RenderWidgetCompositor::SetFrameSinkId(
    const cc::FrameSinkId& frame_sink_id) {
  frame_sink_id_ = frame_sink_id;
  layer_tree_host_->SetFrameSinkId(frame_sink_id);
}

void RenderWidgetCompositor::SetPaintedDeviceScaleFactor(float device_scale) {
  layer_tree_host_->SetPaintedDeviceScaleFactor(device_scale);
}

void RenderWidgetCompositor::SetRasterColorSpace(
    const gfx::ColorSpace& color_space) {
  layer_tree_host_->SetRasterColorSpace(color_space);
}

void RenderWidgetCompositor::SetIsForOopif(bool is_for_oopif) {
  is_for_oopif_ = is_for_oopif;
}

void RenderWidgetCompositor::SetContentSourceId(uint32_t id) {
  layer_tree_host_->SetContentSourceId(id);
}

void RenderWidgetCompositor::SetLocalSurfaceId(
    const cc::LocalSurfaceId& local_surface_id) {
  layer_tree_host_->SetLocalSurfaceId(local_surface_id);
}

void RenderWidgetCompositor::NotifySwapTime(ReportTimeCallback callback) {
  QueueSwapPromise(base::MakeUnique<ReportTimeSwapPromise>(
      std::move(callback), base::ThreadTaskRunnerHandle::Get()));
}

}  // namespace content
