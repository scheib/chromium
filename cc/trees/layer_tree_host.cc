// Copyright 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cc/trees/layer_tree_host.h"

#include <stddef.h>
#include <stdint.h>

#include <algorithm>
#include <memory>
#include <stack>
#include <string>
#include <unordered_map>

#include "base/atomic_sequence_num.h"
#include "base/auto_reset.h"
#include "base/bind.h"
#include "base/command_line.h"
#include "base/location.h"
#include "base/memory/ptr_util.h"
#include "base/metrics/histogram_macros.h"
#include "base/numerics/safe_math.h"
#include "base/single_thread_task_runner.h"
#include "base/stl_util.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "base/threading/thread_task_runner_handle.h"
#include "base/timer/elapsed_timer.h"
#include "base/trace_event/trace_event.h"
#include "base/trace_event/trace_event_argument.h"
#include "cc/base/devtools_instrumentation.h"
#include "cc/base/histograms.h"
#include "cc/base/math_util.h"
#include "cc/debug/rendering_stats_instrumentation.h"
#include "cc/input/layer_selection_bound.h"
#include "cc/input/page_scale_animation.h"
#include "cc/layers/heads_up_display_layer.h"
#include "cc/layers/heads_up_display_layer_impl.h"
#include "cc/layers/layer.h"
#include "cc/layers/painted_scrollbar_layer.h"
#include "cc/resources/ui_resource_manager.h"
#include "cc/tiles/frame_viewer_instrumentation.h"
#include "cc/trees/draw_property_utils.h"
#include "cc/trees/effect_node.h"
#include "cc/trees/layer_tree_host_client.h"
#include "cc/trees/layer_tree_host_common.h"
#include "cc/trees/layer_tree_host_impl.h"
#include "cc/trees/mutator_host.h"
#include "cc/trees/property_tree_builder.h"
#include "cc/trees/proxy_main.h"
#include "cc/trees/single_thread_proxy.h"
#include "cc/trees/swap_promise_manager.h"
#include "cc/trees/transform_node.h"
#include "cc/trees/tree_synchronizer.h"
#include "ui/gfx/geometry/size_conversions.h"
#include "ui/gfx/geometry/vector2d_conversions.h"

namespace {
static base::StaticAtomicSequenceNumber s_layer_tree_host_sequence_number;
}

namespace cc {

LayerTreeHost::InitParams::InitParams() {}

LayerTreeHost::InitParams::~InitParams() {}

std::unique_ptr<LayerTreeHost> LayerTreeHost::CreateThreaded(
    scoped_refptr<base::SingleThreadTaskRunner> impl_task_runner,
    InitParams* params) {
  DCHECK(params->main_task_runner.get());
  DCHECK(impl_task_runner.get());
  DCHECK(params->settings);
  std::unique_ptr<LayerTreeHost> layer_tree_host(
      new LayerTreeHost(params, CompositorMode::THREADED));
  layer_tree_host->InitializeThreaded(params->main_task_runner,
                                      impl_task_runner);
  return layer_tree_host;
}

std::unique_ptr<LayerTreeHost>
LayerTreeHost::CreateSingleThreaded(
    LayerTreeHostSingleThreadClient* single_thread_client,
    InitParams* params) {
  DCHECK(params->settings);
  std::unique_ptr<LayerTreeHost> layer_tree_host(
      new LayerTreeHost(params, CompositorMode::SINGLE_THREADED));
  layer_tree_host->InitializeSingleThreaded(single_thread_client,
                                            params->main_task_runner);
  return layer_tree_host;
}

LayerTreeHost::LayerTreeHost(InitParams* params, CompositorMode mode)
    : micro_benchmark_controller_(this),
      image_worker_task_runner_(params->image_worker_task_runner),
      compositor_mode_(mode),
      ui_resource_manager_(base::MakeUnique<UIResourceManager>()),
      client_(params->client),
      rendering_stats_instrumentation_(RenderingStatsInstrumentation::Create()),
      settings_(*params->settings),
      debug_state_(settings_.initial_debug_state),
      id_(s_layer_tree_host_sequence_number.GetNext() + 1),
      task_graph_runner_(params->task_graph_runner),
      content_source_id_(0),
      event_listener_properties_(),
      mutator_host_(params->mutator_host) {
  DCHECK(task_graph_runner_);
  DCHECK(!settings_.enable_checker_imaging || image_worker_task_runner_);

  mutator_host_->SetMutatorHostClient(this);

  rendering_stats_instrumentation_->set_record_rendering_stats(
      debug_state_.RecordRenderingStats());
}

void LayerTreeHost::InitializeThreaded(
    scoped_refptr<base::SingleThreadTaskRunner> main_task_runner,
    scoped_refptr<base::SingleThreadTaskRunner> impl_task_runner) {
  task_runner_provider_ =
      TaskRunnerProvider::Create(main_task_runner, impl_task_runner);
  std::unique_ptr<ProxyMain> proxy_main =
      base::MakeUnique<ProxyMain>(this, task_runner_provider_.get());
  InitializeProxy(std::move(proxy_main));
}

void LayerTreeHost::InitializeSingleThreaded(
    LayerTreeHostSingleThreadClient* single_thread_client,
    scoped_refptr<base::SingleThreadTaskRunner> main_task_runner) {
  task_runner_provider_ = TaskRunnerProvider::Create(main_task_runner, nullptr);
  InitializeProxy(SingleThreadProxy::Create(this, single_thread_client,
                                            task_runner_provider_.get()));
}

void LayerTreeHost::InitializeForTesting(
    std::unique_ptr<TaskRunnerProvider> task_runner_provider,
    std::unique_ptr<Proxy> proxy_for_testing) {
  task_runner_provider_ = std::move(task_runner_provider);
  InitializeProxy(std::move(proxy_for_testing));
}

void LayerTreeHost::SetTaskRunnerProviderForTesting(
    std::unique_ptr<TaskRunnerProvider> task_runner_provider) {
  DCHECK(!task_runner_provider_);
  task_runner_provider_ = std::move(task_runner_provider);
}

void LayerTreeHost::SetUIResourceManagerForTesting(
    std::unique_ptr<UIResourceManager> ui_resource_manager) {
  ui_resource_manager_ = std::move(ui_resource_manager);
}

void LayerTreeHost::InitializeProxy(std::unique_ptr<Proxy> proxy) {
  TRACE_EVENT0("cc", "LayerTreeHostInProcess::InitializeForReal");
  DCHECK(task_runner_provider_);

  proxy_ = std::move(proxy);
  proxy_->Start();

  UpdateDeferCommitsInternal();

  mutator_host_->SetSupportsScrollAnimations(proxy_->SupportsImplScrolling());
}

LayerTreeHost::~LayerTreeHost() {
  // Track when we're inside a main frame to see if compositor is being
  // destroyed midway which causes a crash. crbug.com/654672
  DCHECK(!inside_main_frame_);
  TRACE_EVENT0("cc", "LayerTreeHostInProcess::~LayerTreeHostInProcess");

  // Clear any references into the LayerTreeHost.
  mutator_host_->SetMutatorHostClient(nullptr);

  // We must clear any pointers into the layer tree prior to destroying it.
  RegisterViewportLayers(ViewportLayers());

  if (root_layer_) {
    root_layer_->SetLayerTreeHost(nullptr);

    // The root layer must be destroyed before the layer tree. We've made a
    // contract with our animation controllers that the animation_host will
    // outlive them, and we must make good.
    root_layer_ = nullptr;
  }

  if (proxy_) {
    DCHECK(task_runner_provider_->IsMainThread());
    proxy_->Stop();

    // Proxy must be destroyed before the Task Runner Provider.
    proxy_ = nullptr;
  }
}

int LayerTreeHost::GetId() const {
  return id_;
}

int LayerTreeHost::SourceFrameNumber() const {
  return source_frame_number_;
}

UIResourceManager* LayerTreeHost::GetUIResourceManager() const {
  return ui_resource_manager_.get();
}

TaskRunnerProvider* LayerTreeHost::GetTaskRunnerProvider() const {
  return task_runner_provider_.get();
}

SwapPromiseManager* LayerTreeHost::GetSwapPromiseManager() {
  return &swap_promise_manager_;
}

const LayerTreeSettings& LayerTreeHost::GetSettings() const {
  return settings_;
}

void LayerTreeHost::SetFrameSinkId(const FrameSinkId& frame_sink_id) {
  surface_sequence_generator_.set_frame_sink_id(frame_sink_id);
}

void LayerTreeHost::QueueSwapPromise(
    std::unique_ptr<SwapPromise> swap_promise) {
  swap_promise_manager_.QueueSwapPromise(std::move(swap_promise));
}

SurfaceSequenceGenerator*
LayerTreeHost::GetSurfaceSequenceGenerator() {
  return &surface_sequence_generator_;
}

void LayerTreeHost::WillBeginMainFrame() {
  inside_main_frame_ = true;
  devtools_instrumentation::WillBeginMainThreadFrame(GetId(),
                                                     SourceFrameNumber());
  client_->WillBeginMainFrame();
}

void LayerTreeHost::DidBeginMainFrame() {
  inside_main_frame_ = false;
  client_->DidBeginMainFrame();
}

void LayerTreeHost::BeginMainFrameNotExpectedSoon() {
  client_->BeginMainFrameNotExpectedSoon();
}

void LayerTreeHost::BeginMainFrameNotExpectedUntil(base::TimeTicks time) {
  client_->BeginMainFrameNotExpectedUntil(time);
}

void LayerTreeHost::BeginMainFrame(const BeginFrameArgs& args) {
  client_->BeginMainFrame(args);
}

void LayerTreeHost::DidStopFlinging() {
  proxy_->MainThreadHasStoppedFlinging();
}

const LayerTreeDebugState& LayerTreeHost::GetDebugState() const {
  return debug_state_;
}

void LayerTreeHost::RequestMainFrameUpdate() {
  client_->UpdateLayerTreeHost();
}

// This function commits the LayerTreeHost to an impl tree. When modifying
// this function, keep in mind that the function *runs* on the impl thread! Any
// code that is logically a main thread operation, e.g. deletion of a Layer,
// should be delayed until the LayerTreeHostInProcess::CommitComplete, which
// will run after the commit, but on the main thread.
void LayerTreeHost::FinishCommitOnImplThread(
    LayerTreeHostImpl* host_impl) {
  DCHECK(task_runner_provider_->IsImplThread());

  bool is_new_trace;
  TRACE_EVENT_IS_NEW_TRACE(&is_new_trace);
  if (is_new_trace &&
      frame_viewer_instrumentation::IsTracingLayerTreeSnapshots() &&
      root_layer()) {
    // We'll be dumping layer trees as part of trace, so make sure
    // PushPropertiesTo() propagates layer debug info to the impl side --
    // otherwise this won't happen for the layers that remain unchanged since
    // tracing started.
    LayerTreeHostCommon::CallFunctionForEveryLayer(
        this, [](Layer* layer) { layer->SetNeedsPushProperties(); });
  }

  LayerTreeImpl* sync_tree = host_impl->sync_tree();
  sync_tree->lifecycle().AdvanceTo(LayerTreeLifecycle::kBeginningSync);

  if (next_commit_forces_redraw_) {
    sync_tree->ForceRedrawNextActivation();
    next_commit_forces_redraw_ = false;
  }
  if (next_commit_forces_recalculate_raster_scales_) {
    sync_tree->ForceRecalculateRasterScales();
    next_commit_forces_recalculate_raster_scales_ = false;
  }

  sync_tree->set_source_frame_number(SourceFrameNumber());

  if (needs_full_tree_sync_)
    TreeSynchronizer::SynchronizeTrees(root_layer(), sync_tree);

  // Track the navigation state before pushing properties since it overwrites
  // the |content_source_id_| on the sync tree.
  bool did_navigate = content_source_id_ != sync_tree->content_source_id();
  if (did_navigate)
    host_impl->ClearImageCacheOnNavigation();

  {
    TRACE_EVENT0("cc", "LayerTreeHostInProcess::PushProperties");

    PushPropertyTreesTo(sync_tree);
    sync_tree->lifecycle().AdvanceTo(LayerTreeLifecycle::kSyncedPropertyTrees);

    TreeSynchronizer::PushLayerProperties(this, sync_tree);
    sync_tree->lifecycle().AdvanceTo(
        LayerTreeLifecycle::kSyncedLayerProperties);

    PushLayerTreePropertiesTo(sync_tree);
    PushLayerTreeHostPropertiesTo(host_impl);

    sync_tree->PassSwapPromises(swap_promise_manager_.TakeSwapPromises());

    // TODO(pdr): Move this into PushPropertyTreesTo or introduce a lifecycle
    // state for it.
    sync_tree->SetDeviceScaleFactor(device_scale_factor_);

    sync_tree->set_ui_resource_request_queue(
        ui_resource_manager_->TakeUIResourcesRequests());

    // This must happen after synchronizing property trees and after pushing
    // properties, which updates the clobber_active_value flag.
    // TODO(pdr): Enforce this comment with DCHECKS and a lifecycle state.
    sync_tree->property_trees()->scroll_tree.PushScrollUpdatesFromMainThread(
        property_trees(), sync_tree);

    // This must happen after synchronizing property trees and after push
    // properties, which updates property tree indices, but before animation
    // host pushes properties as animation host push properties can change
    // Animation::InEffect and we want the old InEffect value for updating
    // property tree scrolling and animation.
    // TODO(pdr): Enforce this comment with DCHECKS and a lifecycle state.
    bool is_impl_side_update = false;
    sync_tree->UpdatePropertyTreeScrollingAndAnimationFromMainThread(
        is_impl_side_update);

    TRACE_EVENT0("cc", "LayerTreeHostInProcess::AnimationHost::PushProperties");
    DCHECK(host_impl->mutator_host());
    mutator_host_->PushPropertiesTo(host_impl->mutator_host());

    sync_tree->lifecycle().AdvanceTo(LayerTreeLifecycle::kNotSyncing);
  }

  // Transfer image decode requests to the impl thread.
  for (auto& request : queued_image_decodes_)
    host_impl->QueueImageDecode(std::move(request.first), request.second);
  queued_image_decodes_.clear();

  micro_benchmark_controller_.ScheduleImplBenchmarks(host_impl);
  property_trees_.ResetAllChangeTracking();
}

void LayerTreeHost::PushPropertyTreesTo(LayerTreeImpl* tree_impl) {
  bool property_trees_changed_on_active_tree =
      tree_impl->IsActiveTree() && tree_impl->property_trees()->changed;
  // Property trees may store damage status. We preserve the sync tree damage
  // status by pushing the damage status from sync tree property trees to main
  // thread property trees or by moving it onto the layers.
  if (root_layer_ && property_trees_changed_on_active_tree) {
    if (property_trees_.sequence_number ==
        tree_impl->property_trees()->sequence_number)
      tree_impl->property_trees()->PushChangeTrackingTo(&property_trees_);
    else
      tree_impl->MoveChangeTrackingToLayers();
  }

  tree_impl->SetPropertyTrees(&property_trees_);
}

void LayerTreeHost::WillCommit() {
  swap_promise_manager_.WillCommit();
  client_->WillCommit();
}


void LayerTreeHost::UpdateDeferCommitsInternal() {
  proxy_->SetDeferCommits(defer_commits_ ||
                          (settings_.enable_surface_synchronization &&
                           !local_surface_id_.is_valid()));
}

void LayerTreeHost::CommitComplete() {
  source_frame_number_++;
  client_->DidCommit();
  if (did_complete_scale_animation_) {
    client_->DidCompletePageScaleAnimation();
    did_complete_scale_animation_ = false;
  }
}

void LayerTreeHost::SetCompositorFrameSink(
    std::unique_ptr<CompositorFrameSink> surface) {
  TRACE_EVENT0("cc", "LayerTreeHostInProcess::SetCompositorFrameSink");
  DCHECK(surface);

  DCHECK(!new_compositor_frame_sink_);
  new_compositor_frame_sink_ = std::move(surface);
  proxy_->SetCompositorFrameSink(new_compositor_frame_sink_.get());
}

std::unique_ptr<CompositorFrameSink>
LayerTreeHost::ReleaseCompositorFrameSink() {
  DCHECK(!visible_);

  DidLoseCompositorFrameSink();
  proxy_->ReleaseCompositorFrameSink();
  return std::move(current_compositor_frame_sink_);
}

void LayerTreeHost::RequestNewCompositorFrameSink() {
  client_->RequestNewCompositorFrameSink();
}

void LayerTreeHost::DidInitializeCompositorFrameSink() {
  DCHECK(new_compositor_frame_sink_);
  current_compositor_frame_sink_ = std::move(new_compositor_frame_sink_);
  client_->DidInitializeCompositorFrameSink();
}

void LayerTreeHost::DidFailToInitializeCompositorFrameSink() {
  DCHECK(new_compositor_frame_sink_);
  // Note: It is safe to drop all output surface references here as
  // LayerTreeHostImpl will not keep a pointer to either the old or
  // new CompositorFrameSink after failing to initialize the new one.
  current_compositor_frame_sink_ = nullptr;
  new_compositor_frame_sink_ = nullptr;
  client_->DidFailToInitializeCompositorFrameSink();
}

std::unique_ptr<LayerTreeHostImpl>
LayerTreeHost::CreateLayerTreeHostImpl(
    LayerTreeHostImplClient* client) {
  DCHECK(task_runner_provider_->IsImplThread());

  const bool supports_impl_scrolling = task_runner_provider_->HasImplThread();
  std::unique_ptr<MutatorHost> mutator_host_impl =
      mutator_host_->CreateImplInstance(supports_impl_scrolling);

  std::unique_ptr<LayerTreeHostImpl> host_impl = LayerTreeHostImpl::Create(
      settings_, client, task_runner_provider_.get(),
      rendering_stats_instrumentation_.get(), task_graph_runner_,
      std::move(mutator_host_impl), id_, std::move(image_worker_task_runner_));
  host_impl->SetHasGpuRasterizationTrigger(has_gpu_rasterization_trigger_);
  host_impl->SetContentIsSuitableForGpuRasterization(
      content_is_suitable_for_gpu_rasterization_);
  task_graph_runner_ = NULL;
  input_handler_weak_ptr_ = host_impl->AsWeakPtr();
  return host_impl;
}

void LayerTreeHost::DidLoseCompositorFrameSink() {
  TRACE_EVENT0("cc", "LayerTreeHostInProcess::DidLoseCompositorFrameSink");
  DCHECK(task_runner_provider_->IsMainThread());
  SetNeedsCommit();
}

void LayerTreeHost::SetDeferCommits(bool defer_commits) {
  if (defer_commits_ == defer_commits)
    return;
  defer_commits_ = defer_commits;
  UpdateDeferCommitsInternal();
}

DISABLE_CFI_PERF
void LayerTreeHost::SetNeedsAnimate() {
  proxy_->SetNeedsAnimate();
  swap_promise_manager_.NotifySwapPromiseMonitorsOfSetNeedsCommit();
}

DISABLE_CFI_PERF
void LayerTreeHost::SetNeedsUpdateLayers() {
  proxy_->SetNeedsUpdateLayers();
  swap_promise_manager_.NotifySwapPromiseMonitorsOfSetNeedsCommit();
}

void LayerTreeHost::SetNeedsCommit() {
  proxy_->SetNeedsCommit();
  swap_promise_manager_.NotifySwapPromiseMonitorsOfSetNeedsCommit();
}

void LayerTreeHost::SetNeedsRecalculateRasterScales() {
  next_commit_forces_recalculate_raster_scales_ = true;
  proxy_->SetNeedsCommit();
}

void LayerTreeHost::SetNeedsRedrawRect(const gfx::Rect& damage_rect) {
  proxy_->SetNeedsRedraw(damage_rect);
}

bool LayerTreeHost::CommitRequested() const {
  return proxy_->CommitRequested();
}

void LayerTreeHost::SetNextCommitWaitsForActivation() {
  proxy_->SetNextCommitWaitsForActivation();
}

void LayerTreeHost::SetNeedsCommitWithForcedRedraw() {
  next_commit_forces_redraw_ = true;
  proxy_->SetNeedsCommit();
}

void LayerTreeHost::SetAnimationEvents(
    std::unique_ptr<MutatorEvents> events) {
  DCHECK(task_runner_provider_->IsMainThread());
  mutator_host_->SetAnimationEvents(std::move(events));
}

void LayerTreeHost::SetDebugState(
    const LayerTreeDebugState& debug_state) {
  LayerTreeDebugState new_debug_state =
      LayerTreeDebugState::Unite(settings_.initial_debug_state, debug_state);

  if (LayerTreeDebugState::Equal(debug_state_, new_debug_state))
    return;

  debug_state_ = new_debug_state;

  rendering_stats_instrumentation_->set_record_rendering_stats(
      debug_state_.RecordRenderingStats());

  SetNeedsCommit();
}

void LayerTreeHost::ResetGpuRasterizationTracking() {
  content_is_suitable_for_gpu_rasterization_ = true;
  gpu_rasterization_histogram_recorded_ = false;
}

void LayerTreeHost::SetHasGpuRasterizationTrigger(bool has_trigger) {
  if (has_trigger == has_gpu_rasterization_trigger_)
    return;

  has_gpu_rasterization_trigger_ = has_trigger;
  TRACE_EVENT_INSTANT1(
      "cc", "LayerTreeHostInProcess::SetHasGpuRasterizationTrigger",
      TRACE_EVENT_SCOPE_THREAD, "has_trigger", has_gpu_rasterization_trigger_);
}

void LayerTreeHost::ApplyPageScaleDeltaFromImplSide(
    float page_scale_delta) {
  DCHECK(CommitRequested());
  if (page_scale_delta == 1.f)
    return;
  float page_scale = page_scale_factor_ * page_scale_delta;
  SetPageScaleFromImplSide(page_scale);
}

void LayerTreeHost::SetVisible(bool visible) {
  if (visible_ == visible)
    return;
  visible_ = visible;
  proxy_->SetVisible(visible);
}

bool LayerTreeHost::IsVisible() const {
  return visible_;
}

void LayerTreeHost::NotifyInputThrottledUntilCommit() {
  proxy_->NotifyInputThrottledUntilCommit();
}

void LayerTreeHost::LayoutAndUpdateLayers() {
  DCHECK(IsSingleThreaded());
  // This function is only valid when not using the scheduler.
  DCHECK(!settings_.single_thread_proxy_scheduler);
  RequestMainFrameUpdate();
  UpdateLayers();
}

void LayerTreeHost::Composite(base::TimeTicks frame_begin_time) {
  DCHECK(IsSingleThreaded());
  // This function is only valid when not using the scheduler.
  DCHECK(!settings_.single_thread_proxy_scheduler);
  SingleThreadProxy* proxy = static_cast<SingleThreadProxy*>(proxy_.get());

  proxy->CompositeImmediately(frame_begin_time);
}

static int GetLayersUpdateTimeHistogramBucket(size_t numLayers) {
  // We uses the following exponential (ratio 2) bucketization:
  // [0, 10), [10, 30), [30, 70), [70, 150), [150, infinity)
  if (numLayers < 10)
    return 0;
  if (numLayers < 30)
    return 1;
  if (numLayers < 70)
    return 2;
  if (numLayers < 150)
    return 3;
  return 4;
}

bool LayerTreeHost::UpdateLayers() {
  if (!root_layer()) {
    property_trees_.clear();
    return false;
  }
  DCHECK(!root_layer()->parent());
  base::ElapsedTimer timer;

  bool result = DoUpdateLayers(root_layer());
  micro_benchmark_controller_.DidUpdateLayers();

  if (const char* client_name = GetClientNameForMetrics()) {
    std::string histogram_name =
        base::StringPrintf("Compositing.%s.LayersUpdateTime.%d", client_name,
                           GetLayersUpdateTimeHistogramBucket(NumLayers()));
    base::Histogram::FactoryGet(histogram_name, 0, 10000000, 50,
                                base::HistogramBase::kUmaTargetedHistogramFlag)
        ->Add(timer.Elapsed().InMicroseconds());
  }

  return result;
}

void LayerTreeHost::DidCompletePageScaleAnimation() {
  did_complete_scale_animation_ = true;
}

void LayerTreeHost::RecordGpuRasterizationHistogram(
    const LayerTreeHostImpl* host_impl) {
  // Gpu rasterization is only supported for Renderer compositors.
  // Checking for IsSingleThreaded() to exclude Browser compositors.
  if (gpu_rasterization_histogram_recorded_ || IsSingleThreaded())
    return;

  bool gpu_rasterization_enabled = false;
  if (host_impl->compositor_frame_sink()) {
    ContextProvider* compositor_context_provider =
        host_impl->compositor_frame_sink()->context_provider();
    if (compositor_context_provider) {
      gpu_rasterization_enabled =
          compositor_context_provider->ContextCapabilities().gpu_rasterization;
    }
  }

  // Record how widely gpu rasterization is enabled.
  // This number takes device/gpu whitelisting/backlisting into account.
  // Note that we do not consider the forced gpu rasterization mode, which is
  // mostly used for debugging purposes.
  UMA_HISTOGRAM_BOOLEAN("Renderer4.GpuRasterizationEnabled",
                        gpu_rasterization_enabled);
  if (gpu_rasterization_enabled) {
    UMA_HISTOGRAM_BOOLEAN("Renderer4.GpuRasterizationTriggered",
                          has_gpu_rasterization_trigger_);
    UMA_HISTOGRAM_BOOLEAN("Renderer4.GpuRasterizationSuitableContent",
                          content_is_suitable_for_gpu_rasterization_);
    // Record how many pages actually get gpu rasterization when enabled.
    UMA_HISTOGRAM_BOOLEAN("Renderer4.GpuRasterizationUsed",
                          (has_gpu_rasterization_trigger_ &&
                           content_is_suitable_for_gpu_rasterization_));
  }

  gpu_rasterization_histogram_recorded_ = true;
}

bool LayerTreeHost::DoUpdateLayers(Layer* root_layer) {
  TRACE_EVENT1("cc", "LayerTreeHostInProcess::DoUpdateLayers",
               "source_frame_number", SourceFrameNumber());

  UpdateHudLayer(debug_state_.ShowHudInfo());

  Layer* root_scroll =
      PropertyTreeBuilder::FindFirstScrollableLayer(root_layer);
  Layer* page_scale_layer = viewport_layers_.page_scale.get();
  if (!page_scale_layer && root_scroll)
    page_scale_layer = root_scroll->parent();

  if (hud_layer_) {
    hud_layer_->PrepareForCalculateDrawProperties(device_viewport_size_,
                                                  device_scale_factor_);
    // The HUD layer is managed outside the layer list sent to LayerTreeHost
    // and needs to have its property tree state set.
    if (settings_.use_layer_lists && root_layer_.get()) {
      hud_layer_->SetTransformTreeIndex(root_layer_->transform_tree_index());
      hud_layer_->SetEffectTreeIndex(root_layer_->effect_tree_index());
      hud_layer_->SetClipTreeIndex(root_layer_->clip_tree_index());
      hud_layer_->SetScrollTreeIndex(root_layer_->scroll_tree_index());
      hud_layer_->set_property_tree_sequence_number(
          root_layer_->property_tree_sequence_number());
    }
  }

  gfx::Transform identity_transform;
  LayerList update_layer_list;

  {
    base::AutoReset<bool> update_property_trees(&in_update_property_trees_,
                                                true);
    TRACE_EVENT0("cc",
                 "LayerTreeHostInProcess::UpdateLayers::BuildPropertyTrees");
    TRACE_EVENT0(
        TRACE_DISABLED_BY_DEFAULT("cc.debug.cdp-perf"),
        "LayerTreeHostInProcessCommon::ComputeVisibleRectsWithPropertyTrees");
    PropertyTrees* property_trees = &property_trees_;
    if (!settings_.use_layer_lists) {
      // If use_layer_lists is set, then the property trees should have been
      // built by the client already.
      PropertyTreeBuilder::BuildPropertyTrees(
          root_layer, page_scale_layer, inner_viewport_scroll_layer(),
          outer_viewport_scroll_layer(), overscroll_elasticity_layer(),
          elastic_overscroll_, page_scale_factor_, device_scale_factor_,
          gfx::Rect(device_viewport_size_), identity_transform, property_trees);
      TRACE_EVENT_INSTANT1(
          "cc", "LayerTreeHostInProcess::UpdateLayers_BuiltPropertyTrees",
          TRACE_EVENT_SCOPE_THREAD, "property_trees",
          property_trees->AsTracedValue());
    } else {
      TRACE_EVENT_INSTANT1(
          "cc", "LayerTreeHostInProcess::UpdateLayers_ReceivedPropertyTrees",
          TRACE_EVENT_SCOPE_THREAD, "property_trees",
          property_trees->AsTracedValue());
    }
    draw_property_utils::UpdatePropertyTrees(this, property_trees);
    draw_property_utils::FindLayersThatNeedUpdates(this, property_trees,
                                                   &update_layer_list);
  }

  bool content_is_suitable_for_gpu = true;
  bool did_paint_content =
      PaintContent(update_layer_list, &content_is_suitable_for_gpu);

  if (content_is_suitable_for_gpu) {
    ++num_consecutive_frames_suitable_for_gpu_;
    if (num_consecutive_frames_suitable_for_gpu_ >=
        kNumFramesToConsiderBeforeGpuRasterization) {
      content_is_suitable_for_gpu_rasterization_ = true;
    }
  } else {
    num_consecutive_frames_suitable_for_gpu_ = 0;
    content_is_suitable_for_gpu_rasterization_ = false;
  }
  return did_paint_content;
}

void LayerTreeHost::ApplyViewportDeltas(ScrollAndScaleSet* info) {
  gfx::Vector2dF inner_viewport_scroll_delta;
  if (info->inner_viewport_scroll.layer_id != Layer::INVALID_ID)
    inner_viewport_scroll_delta = info->inner_viewport_scroll.scroll_delta;

  if (inner_viewport_scroll_delta.IsZero() && info->page_scale_delta == 1.f &&
      info->elastic_overscroll_delta.IsZero() && !info->top_controls_delta)
    return;

  // Preemptively apply the scroll offset and scale delta here before sending
  // it to the client.  If the client comes back and sets it to the same
  // value, then the layer can early out without needing a full commit.
  if (viewport_layers_.inner_viewport_scroll) {
    viewport_layers_.inner_viewport_scroll->SetScrollOffsetFromImplSide(
        gfx::ScrollOffsetWithDelta(
            viewport_layers_.inner_viewport_scroll->scroll_offset(),
            inner_viewport_scroll_delta));
  }

  ApplyPageScaleDeltaFromImplSide(info->page_scale_delta);
  SetElasticOverscrollFromImplSide(elastic_overscroll_ +
                                   info->elastic_overscroll_delta);
  // TODO(ccameron): pass the elastic overscroll here so that input events
  // may be translated appropriately.
  client_->ApplyViewportDeltas(inner_viewport_scroll_delta, gfx::Vector2dF(),
                               info->elastic_overscroll_delta,
                               info->page_scale_delta,
                               info->top_controls_delta);
  SetNeedsUpdateLayers();
}

void LayerTreeHost::RecordWheelAndTouchScrollingCount(ScrollAndScaleSet* info) {
  bool has_scrolled_by_wheel = info->has_scrolled_by_wheel;
  bool has_scrolled_by_touch = info->has_scrolled_by_touch;

  if (has_scrolled_by_wheel || has_scrolled_by_touch) {
    client_->RecordWheelAndTouchScrollingCount(has_scrolled_by_wheel,
                                               has_scrolled_by_touch);
  }
}

void LayerTreeHost::ApplyScrollAndScale(ScrollAndScaleSet* info) {
  for (auto& swap_promise : info->swap_promises) {
    TRACE_EVENT_WITH_FLOW1("input,benchmark", "LatencyInfo.Flow",
                           TRACE_ID_DONT_MANGLE(swap_promise->TraceId()),
                           TRACE_EVENT_FLAG_FLOW_IN | TRACE_EVENT_FLAG_FLOW_OUT,
                           "step", "Main thread scroll update");
    swap_promise_manager_.QueueSwapPromise(std::move(swap_promise));
  }

  if (root_layer_) {
    for (size_t i = 0; i < info->scrolls.size(); ++i) {
      Layer* layer = LayerById(info->scrolls[i].layer_id);
      if (!layer)
        continue;
      layer->SetScrollOffsetFromImplSide(gfx::ScrollOffsetWithDelta(
          layer->scroll_offset(), info->scrolls[i].scroll_delta));
      SetNeedsUpdateLayers();
    }
    for (size_t i = 0; i < info->scrollbars.size(); ++i) {
      Layer* layer = LayerByElementId(info->scrollbars[i].element_id);
      if (!layer)
        continue;
      layer->SetScrollbarsHiddenFromImplSide(info->scrollbars[i].hidden);
    }
  }

  // This needs to happen after scroll deltas have been sent to prevent top
  // controls from clamping the layout viewport both on the compositor and
  // on the main thread.
  ApplyViewportDeltas(info);

  RecordWheelAndTouchScrollingCount(info);
}

const base::WeakPtr<InputHandler>& LayerTreeHost::GetInputHandler()
    const {
  return input_handler_weak_ptr_;
}

void LayerTreeHost::UpdateBrowserControlsState(
    BrowserControlsState constraints,
    BrowserControlsState current,
    bool animate) {
  // Browser controls are only used in threaded mode.
  DCHECK(IsThreaded());
  proxy_->UpdateBrowserControlsState(constraints, current, animate);
}

void LayerTreeHost::AnimateLayers(base::TimeTicks monotonic_time) {
  std::unique_ptr<MutatorEvents> events = mutator_host_->CreateEvents();

  if (mutator_host_->TickAnimations(monotonic_time))
    mutator_host_->UpdateAnimationState(true, events.get());

  if (!events->IsEmpty())
    property_trees_.needs_rebuild = true;
}

int LayerTreeHost::ScheduleMicroBenchmark(
    const std::string& benchmark_name,
    std::unique_ptr<base::Value> value,
    const MicroBenchmark::DoneCallback& callback) {
  return micro_benchmark_controller_.ScheduleRun(benchmark_name,
                                                 std::move(value), callback);
}

bool LayerTreeHost::SendMessageToMicroBenchmark(
    int id,
    std::unique_ptr<base::Value> value) {
  return micro_benchmark_controller_.SendMessage(id, std::move(value));
}

void LayerTreeHost::SetLayerTreeMutator(
    std::unique_ptr<LayerTreeMutator> mutator) {
  proxy_->SetMutator(std::move(mutator));
}

bool LayerTreeHost::IsSingleThreaded() const {
  DCHECK(compositor_mode_ != CompositorMode::SINGLE_THREADED ||
         !task_runner_provider_->HasImplThread());
  return compositor_mode_ == CompositorMode::SINGLE_THREADED;
}

bool LayerTreeHost::IsThreaded() const {
  DCHECK(compositor_mode_ != CompositorMode::THREADED ||
         task_runner_provider_->HasImplThread());
  return compositor_mode_ == CompositorMode::THREADED;
}

void LayerTreeHost::SetRootLayer(scoped_refptr<Layer> root_layer) {
  if (root_layer_.get() == root_layer.get())
    return;

  if (root_layer_.get())
    root_layer_->SetLayerTreeHost(nullptr);
  root_layer_ = root_layer;
  if (root_layer_.get()) {
    DCHECK(!root_layer_->parent());
    root_layer_->SetLayerTreeHost(this);
  }

  if (hud_layer_.get())
    hud_layer_->RemoveFromParent();

  // Reset gpu rasterization tracking.
  // This flag is sticky until a new tree comes along.
  ResetGpuRasterizationTracking();

  SetNeedsFullTreeSync();
}

LayerTreeHost::ViewportLayers::ViewportLayers() {}

LayerTreeHost::ViewportLayers::~ViewportLayers() {}

void LayerTreeHost::RegisterViewportLayers(const ViewportLayers& layers) {
  DCHECK(!layers.inner_viewport_scroll ||
         layers.inner_viewport_scroll != layers.outer_viewport_scroll);
  viewport_layers_.overscroll_elasticity = layers.overscroll_elasticity;
  viewport_layers_.page_scale = layers.page_scale;
  viewport_layers_.inner_viewport_container = layers.inner_viewport_container;
  viewport_layers_.outer_viewport_container = layers.outer_viewport_container;
  viewport_layers_.inner_viewport_scroll = layers.inner_viewport_scroll;
  viewport_layers_.outer_viewport_scroll = layers.outer_viewport_scroll;
}

void LayerTreeHost::RegisterSelection(const LayerSelection& selection) {
  if (selection_ == selection)
    return;

  selection_ = selection;
  SetNeedsCommit();
}

void LayerTreeHost::SetHaveScrollEventHandlers(bool have_event_handlers) {
  if (have_scroll_event_handlers_ == have_event_handlers)
    return;

  have_scroll_event_handlers_ = have_event_handlers;
  SetNeedsCommit();
}

void LayerTreeHost::SetEventListenerProperties(
    EventListenerClass event_class,
    EventListenerProperties properties) {
  const size_t index = static_cast<size_t>(event_class);
  if (event_listener_properties_[index] == properties)
    return;

  event_listener_properties_[index] = properties;
  SetNeedsCommit();
}

void LayerTreeHost::SetViewportSize(const gfx::Size& device_viewport_size) {
  if (device_viewport_size_ == device_viewport_size)
    return;

  device_viewport_size_ = device_viewport_size;

  SetPropertyTreesNeedRebuild();
  SetNeedsCommit();
}

void LayerTreeHost::SetBrowserControlsHeight(float height, bool shrink) {
  if (top_controls_height_ == height &&
      browser_controls_shrink_blink_size_ == shrink)
    return;

  top_controls_height_ = height;
  browser_controls_shrink_blink_size_ = shrink;
  SetNeedsCommit();
}

void LayerTreeHost::SetBrowserControlsShownRatio(float ratio) {
  if (top_controls_shown_ratio_ == ratio)
    return;

  top_controls_shown_ratio_ = ratio;
  SetNeedsCommit();
}

void LayerTreeHost::SetBottomControlsHeight(float height) {
  if (bottom_controls_height_ == height)
    return;

  bottom_controls_height_ = height;
  SetNeedsCommit();
}

void LayerTreeHost::SetPageScaleFactorAndLimits(float page_scale_factor,
                                                float min_page_scale_factor,
                                                float max_page_scale_factor) {
  if (page_scale_factor_ == page_scale_factor &&
      min_page_scale_factor_ == min_page_scale_factor &&
      max_page_scale_factor_ == max_page_scale_factor)
    return;

  page_scale_factor_ = page_scale_factor;
  min_page_scale_factor_ = min_page_scale_factor;
  max_page_scale_factor_ = max_page_scale_factor;
  SetPropertyTreesNeedRebuild();
  SetNeedsCommit();
}

void LayerTreeHost::StartPageScaleAnimation(const gfx::Vector2d& target_offset,
                                            bool use_anchor,
                                            float scale,
                                            base::TimeDelta duration) {
  pending_page_scale_animation_.reset(new PendingPageScaleAnimation(
      target_offset, use_anchor, scale, duration));

  SetNeedsCommit();
}

bool LayerTreeHost::HasPendingPageScaleAnimation() const {
  return !!pending_page_scale_animation_.get();
}

void LayerTreeHost::SetDeviceScaleFactor(float device_scale_factor) {
  if (device_scale_factor_ == device_scale_factor)
    return;
  device_scale_factor_ = device_scale_factor;

  property_trees_.needs_rebuild = true;
  SetNeedsCommit();
}

void LayerTreeHost::SetPaintedDeviceScaleFactor(
    float painted_device_scale_factor) {
  if (painted_device_scale_factor_ == painted_device_scale_factor)
    return;
  painted_device_scale_factor_ = painted_device_scale_factor;

  SetNeedsCommit();
}

void LayerTreeHost::SetRasterColorSpace(
    const gfx::ColorSpace& raster_color_space) {
  if (raster_color_space_ == raster_color_space)
    return;
  raster_color_space_ = raster_color_space;
  LayerTreeHostCommon::CallFunctionForEveryLayer(
      this, [](Layer* layer) { layer->SetNeedsDisplay(); });
}

void LayerTreeHost::SetContentSourceId(uint32_t id) {
  if (content_source_id_ == id)
    return;
  content_source_id_ = id;
  SetNeedsCommit();
}

void LayerTreeHost::SetLocalSurfaceId(const LocalSurfaceId& local_surface_id) {
  if (local_surface_id_ == local_surface_id)
    return;
  local_surface_id_ = local_surface_id;
  UpdateDeferCommitsInternal();
  SetNeedsCommit();
}

void LayerTreeHost::RegisterLayer(Layer* layer) {
  DCHECK(!LayerById(layer->id()));
  DCHECK(!in_paint_layer_contents_);
  layer_id_map_[layer->id()] = layer;
  if (layer->element_id()) {
    mutator_host_->RegisterElement(layer->element_id(),
                                   ElementListType::ACTIVE);
  }
}

void LayerTreeHost::UnregisterLayer(Layer* layer) {
  DCHECK(LayerById(layer->id()));
  DCHECK(!in_paint_layer_contents_);
  if (layer->element_id()) {
    mutator_host_->UnregisterElement(layer->element_id(),
                                     ElementListType::ACTIVE);
  }
  RemoveLayerShouldPushProperties(layer);
  layer_id_map_.erase(layer->id());
}

Layer* LayerTreeHost::LayerById(int id) const {
  auto iter = layer_id_map_.find(id);
  return iter != layer_id_map_.end() ? iter->second : nullptr;
}

size_t LayerTreeHost::NumLayers() const {
  return layer_id_map_.size();
}

bool LayerTreeHost::PaintContent(const LayerList& update_layer_list,
                                 bool* content_is_suitable_for_gpu) {
  base::AutoReset<bool> painting(&in_paint_layer_contents_, true);
  bool did_paint_content = false;
  for (const auto& layer : update_layer_list) {
    did_paint_content |= layer->Update();
    *content_is_suitable_for_gpu &= layer->IsSuitableForGpuRasterization();
  }
  return did_paint_content;
}

void LayerTreeHost::AddLayerShouldPushProperties(Layer* layer) {
  layers_that_should_push_properties_.insert(layer);
}

void LayerTreeHost::RemoveLayerShouldPushProperties(Layer* layer) {
  layers_that_should_push_properties_.erase(layer);
}

std::unordered_set<Layer*>& LayerTreeHost::LayersThatShouldPushProperties() {
  return layers_that_should_push_properties_;
}

bool LayerTreeHost::LayerNeedsPushPropertiesForTesting(Layer* layer) const {
  return layers_that_should_push_properties_.find(layer) !=
         layers_that_should_push_properties_.end();
}

void LayerTreeHost::SetPageScaleFromImplSide(float page_scale) {
  DCHECK(CommitRequested());
  page_scale_factor_ = page_scale;
  SetPropertyTreesNeedRebuild();
}

void LayerTreeHost::SetElasticOverscrollFromImplSide(
    gfx::Vector2dF elastic_overscroll) {
  DCHECK(CommitRequested());
  elastic_overscroll_ = elastic_overscroll;
}

void LayerTreeHost::UpdateHudLayer(bool show_hud_info) {
  if (show_hud_info) {
    if (!hud_layer_.get()) {
      hud_layer_ = HeadsUpDisplayLayer::Create();
    }

    if (root_layer_.get() && !hud_layer_->parent())
      root_layer_->AddChild(hud_layer_);
  } else if (hud_layer_.get()) {
    hud_layer_->RemoveFromParent();
    hud_layer_ = nullptr;
  }
}

void LayerTreeHost::SetNeedsFullTreeSync() {
  needs_full_tree_sync_ = true;
  property_trees_.needs_rebuild = true;
  SetNeedsCommit();
}

void LayerTreeHost::SetPropertyTreesNeedRebuild() {
  property_trees_.needs_rebuild = true;
  SetNeedsUpdateLayers();
}

void LayerTreeHost::PushLayerTreePropertiesTo(LayerTreeImpl* tree_impl) {
  tree_impl->set_needs_full_tree_sync(needs_full_tree_sync_);
  needs_full_tree_sync_ = false;

  if (hud_layer_.get()) {
    LayerImpl* hud_impl = tree_impl->LayerById(hud_layer_->id());
    tree_impl->set_hud_layer(static_cast<HeadsUpDisplayLayerImpl*>(hud_impl));
  } else {
    tree_impl->set_hud_layer(nullptr);
  }

  tree_impl->set_background_color(background_color_);
  tree_impl->set_has_transparent_background(has_transparent_background_);
  tree_impl->set_have_scroll_event_handlers(have_scroll_event_handlers_);
  tree_impl->set_event_listener_properties(
      EventListenerClass::kTouchStartOrMove,
      event_listener_properties(EventListenerClass::kTouchStartOrMove));
  tree_impl->set_event_listener_properties(
      EventListenerClass::kMouseWheel,
      event_listener_properties(EventListenerClass::kMouseWheel));
  tree_impl->set_event_listener_properties(
      EventListenerClass::kTouchEndOrCancel,
      event_listener_properties(EventListenerClass::kTouchEndOrCancel));

  if (viewport_layers_.page_scale && viewport_layers_.inner_viewport_scroll) {
    LayerTreeImpl::ViewportLayerIds ids;
    if (viewport_layers_.overscroll_elasticity)
      ids.overscroll_elasticity = viewport_layers_.overscroll_elasticity->id();
    ids.page_scale = viewport_layers_.page_scale->id();
    if (viewport_layers_.inner_viewport_container)
      ids.inner_viewport_container =
          viewport_layers_.inner_viewport_container->id();
    if (viewport_layers_.outer_viewport_container)
      ids.outer_viewport_container =
          viewport_layers_.outer_viewport_container->id();
    ids.inner_viewport_scroll = viewport_layers_.inner_viewport_scroll->id();
    if (viewport_layers_.outer_viewport_scroll)
      ids.outer_viewport_scroll = viewport_layers_.outer_viewport_scroll->id();
    tree_impl->SetViewportLayersFromIds(ids);
    DCHECK(viewport_layers_.inner_viewport_scroll
               ->IsContainerForFixedPositionLayers());
  } else {
    tree_impl->ClearViewportLayers();
  }

  tree_impl->RegisterSelection(selection_);

  tree_impl->PushPageScaleFromMainThread(
      page_scale_factor_, min_page_scale_factor_, max_page_scale_factor_);

  tree_impl->set_browser_controls_shrink_blink_size(
      browser_controls_shrink_blink_size_);
  tree_impl->set_top_controls_height(top_controls_height_);
  tree_impl->set_bottom_controls_height(bottom_controls_height_);
  tree_impl->PushBrowserControlsFromMainThread(top_controls_shown_ratio_);
  tree_impl->elastic_overscroll()->PushFromMainThread(elastic_overscroll_);
  if (tree_impl->IsActiveTree())
    tree_impl->elastic_overscroll()->PushPendingToActive();

  tree_impl->set_painted_device_scale_factor(painted_device_scale_factor_);

  tree_impl->SetRasterColorSpace(raster_color_space_);

  tree_impl->set_content_source_id(content_source_id_);

  tree_impl->set_local_surface_id(local_surface_id_);

  if (pending_page_scale_animation_) {
    tree_impl->SetPendingPageScaleAnimation(
        std::move(pending_page_scale_animation_));
  }

  DCHECK(!tree_impl->ViewportSizeInvalid());

  tree_impl->set_has_ever_been_drawn(false);
}

void LayerTreeHost::PushLayerTreeHostPropertiesTo(
    LayerTreeHostImpl* host_impl) {
  host_impl->SetHasGpuRasterizationTrigger(has_gpu_rasterization_trigger_);
  host_impl->SetContentIsSuitableForGpuRasterization(
      content_is_suitable_for_gpu_rasterization_);
  RecordGpuRasterizationHistogram(host_impl);

  host_impl->SetViewportSize(device_viewport_size_);
  host_impl->SetDebugState(debug_state_);
}

Layer* LayerTreeHost::LayerByElementId(ElementId element_id) const {
  auto iter = element_layers_map_.find(element_id);
  return iter != element_layers_map_.end() ? iter->second : nullptr;
}

void LayerTreeHost::RegisterElement(ElementId element_id,
                                    ElementListType list_type,
                                    Layer* layer) {
  if (layer->element_id()) {
    element_layers_map_[layer->element_id()] = layer;
  }

  mutator_host_->RegisterElement(element_id, list_type);
}

void LayerTreeHost::UnregisterElement(ElementId element_id,
                                      ElementListType list_type,
                                      Layer* layer) {
  mutator_host_->UnregisterElement(element_id, list_type);

  if (layer->element_id()) {
    element_layers_map_.erase(layer->element_id());
  }
}

static void SetElementIdForTesting(Layer* layer) {
  layer->SetElementId(LayerIdToElementIdForTesting(layer->id()));
}

void LayerTreeHost::SetElementIdsForTesting() {
  LayerTreeHostCommon::CallFunctionForEveryLayer(this, SetElementIdForTesting);
}

void LayerTreeHost::BuildPropertyTreesForTesting() {
  gfx::Transform identity_transform;
  PropertyTreeBuilder::BuildPropertyTrees(
      root_layer(), page_scale_layer(), inner_viewport_scroll_layer(),
      outer_viewport_scroll_layer(), overscroll_elasticity_layer(),
      elastic_overscroll(), page_scale_factor(), device_scale_factor(),
      gfx::Rect(device_viewport_size()), identity_transform, property_trees());
}

bool LayerTreeHost::IsElementInList(ElementId element_id,
                                    ElementListType list_type) const {
  return list_type == ElementListType::ACTIVE && LayerByElementId(element_id);
}

void LayerTreeHost::SetMutatorsNeedCommit() {
  SetNeedsCommit();
}

void LayerTreeHost::SetMutatorsNeedRebuildPropertyTrees() {
  property_trees_.needs_rebuild = true;
}

void LayerTreeHost::SetElementFilterMutated(ElementId element_id,
                                            ElementListType list_type,
                                            const FilterOperations& filters) {
  if (settings_.use_layer_lists) {
    // In SPv2 we always have property trees and can set the filter
    // directly on the effect node.
    property_trees_.effect_tree.OnFilterAnimated(element_id, filters);
    return;
  }

  Layer* layer = LayerByElementId(element_id);
  DCHECK(layer);
  layer->OnFilterAnimated(filters);
}

void LayerTreeHost::SetElementOpacityMutated(ElementId element_id,
                                             ElementListType list_type,
                                             float opacity) {
  DCHECK_GE(opacity, 0.f);
  DCHECK_LE(opacity, 1.f);

  if (settings_.use_layer_lists) {
    property_trees_.effect_tree.OnOpacityAnimated(element_id, opacity);
    return;
  }

  Layer* layer = LayerByElementId(element_id);
  DCHECK(layer);
  layer->OnOpacityAnimated(opacity);

  if (EffectNode* node =
          property_trees_.effect_tree.UpdateNodeFromOwningLayerId(
              layer->id())) {
    DCHECK_EQ(layer->effect_tree_index(), node->id);
    if (node->opacity == opacity)
      return;

    node->opacity = opacity;
    property_trees_.effect_tree.set_needs_update(true);
  }

  SetNeedsUpdateLayers();
}

void LayerTreeHost::SetElementTransformMutated(
    ElementId element_id,
    ElementListType list_type,
    const gfx::Transform& transform) {
  if (settings_.use_layer_lists) {
    property_trees_.transform_tree.OnTransformAnimated(element_id, transform);
    return;
  }

  Layer* layer = LayerByElementId(element_id);
  DCHECK(layer);
  layer->OnTransformAnimated(transform);

  if (TransformNode* node =
          property_trees_.transform_tree.UpdateNodeFromOwningLayerId(
              layer->id())) {
    DCHECK_EQ(layer->transform_tree_index(), node->id);
    if (node->local == transform)
      return;

    node->local = transform;
    node->needs_local_transform_update = true;
    node->has_potential_animation = true;
    property_trees_.transform_tree.set_needs_update(true);
  }

  SetNeedsUpdateLayers();
}

void LayerTreeHost::SetElementScrollOffsetMutated(
    ElementId element_id,
    ElementListType list_type,
    const gfx::ScrollOffset& scroll_offset) {
  Layer* layer = LayerByElementId(element_id);
  DCHECK(layer);
  layer->OnScrollOffsetAnimated(scroll_offset);
}

void LayerTreeHost::ElementIsAnimatingChanged(
    ElementId element_id,
    ElementListType list_type,
    const PropertyAnimationState& mask,
    const PropertyAnimationState& state) {
  // TODO(weiliangc): Most of the code is duplicated with LayerTeeHostImpl
  // version of function. Should try to share code.
  DCHECK_EQ(ElementListType::ACTIVE, list_type);

  for (int property = TargetProperty::FIRST_TARGET_PROPERTY;
       property <= TargetProperty::LAST_TARGET_PROPERTY; ++property) {
    if (!mask.currently_running[property] &&
        !mask.potentially_animating[property])
      continue;

    switch (property) {
      case TargetProperty::TRANSFORM:
        if (TransformNode* transform_node =
                property_trees()->transform_tree.FindNodeFromElementId(
                    element_id)) {
          if (mask.currently_running[property])
            transform_node->is_currently_animating =
                state.currently_running[property];
          if (mask.potentially_animating[property]) {
            transform_node->has_potential_animation =
                state.potentially_animating[property];
            transform_node->has_only_translation_animations =
                mutator_host()->HasOnlyTranslationTransforms(element_id,
                                                             list_type);
            property_trees()->transform_tree.set_needs_update(true);
          }
        } else {
          if (state.currently_running[property] ||
              state.potentially_animating[property])
            DCHECK(property_trees()->needs_rebuild)
                << "Attempting to animate non existent transform node";
        }
        break;
      case TargetProperty::OPACITY:
        if (EffectNode* effect_node =
                property_trees()->effect_tree.FindNodeFromElementId(
                    element_id)) {
          if (mask.currently_running[property])
            effect_node->is_currently_animating_opacity =
                state.currently_running[property];
          if (mask.potentially_animating[property]) {
            effect_node->has_potential_opacity_animation =
                state.potentially_animating[property];
            property_trees()->effect_tree.set_needs_update(true);
          }
        } else {
          if (state.currently_running[property] ||
              state.potentially_animating[property])
            DCHECK(property_trees()->needs_rebuild)
                << "Attempting to animate opacity on non existent effect node";
        }
        break;
      case TargetProperty::FILTER:
        if (EffectNode* effect_node =
                property_trees()->effect_tree.FindNodeFromElementId(
                    element_id)) {
          if (mask.currently_running[property])
            effect_node->is_currently_animating_filter =
                state.currently_running[property];
          if (mask.potentially_animating[property])
            effect_node->has_potential_filter_animation =
                state.potentially_animating[property];
        } else {
          if (state.currently_running[property] ||
              state.potentially_animating[property])
            DCHECK(property_trees()->needs_rebuild)
                << "Attempting to animate filter on non existent effect node";
        }
        break;
      default:
        break;
    }
  }
}

gfx::ScrollOffset LayerTreeHost::GetScrollOffsetForAnimation(
    ElementId element_id) const {
  Layer* layer = LayerByElementId(element_id);
  DCHECK(layer);
  return layer->ScrollOffsetForAnimation();
}

void LayerTreeHost::QueueImageDecode(
    sk_sp<const SkImage> image,
    const base::Callback<void(bool)>& callback) {
  TRACE_EVENT0("cc", "LayerTreeHost::QueueImageDecode");
  queued_image_decodes_.emplace_back(std::move(image), callback);
  SetNeedsCommit();
}

LayerListIterator<Layer> LayerTreeHost::begin() const {
  return LayerListIterator<Layer>(root_layer_.get());
}

LayerListIterator<Layer> LayerTreeHost::end() const {
  return LayerListIterator<Layer>(nullptr);
}

LayerListReverseIterator<Layer> LayerTreeHost::rbegin() {
  return LayerListReverseIterator<Layer>(root_layer_.get());
}

LayerListReverseIterator<Layer> LayerTreeHost::rend() {
  return LayerListReverseIterator<Layer>(nullptr);
}

void LayerTreeHost::SetNeedsDisplayOnAllLayers() {
  for (auto* layer : *this)
    layer->SetNeedsDisplay();
}

void LayerTreeHost::SetHasCopyRequest(bool has_copy_request) {
  has_copy_request_ = has_copy_request;
}

}  // namespace cc
