// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cc/test/test_layer_tree_host_base.h"

#include "base/memory/ptr_util.h"
#include "base/threading/thread_task_runner_handle.h"
#include "cc/test/fake_compositor_frame_sink.h"
#include "cc/test/fake_raster_source.h"
#include "cc/trees/layer_tree_impl.h"

namespace cc {

TestLayerTreeHostBase::TestLayerTreeHostBase()
    : task_runner_provider_(base::ThreadTaskRunnerHandle::Get()),
      pending_layer_(nullptr),
      active_layer_(nullptr),
      old_pending_layer_(nullptr),
      root_id_(6),
      id_(7) {}

TestLayerTreeHostBase::~TestLayerTreeHostBase() = default;

void TestLayerTreeHostBase::SetUp() {
  compositor_frame_sink_ = CreateCompositorFrameSink();
  task_graph_runner_ = CreateTaskGraphRunner();
  host_impl_ = CreateHostImpl(CreateSettings(), &task_runner_provider_,
                              task_graph_runner_.get());
  InitializeRenderer();
  SetInitialTreePriority();
}

LayerTreeSettings TestLayerTreeHostBase::CreateSettings() {
  LayerTreeSettings settings;
  return settings;
}

std::unique_ptr<CompositorFrameSink>
TestLayerTreeHostBase::CreateCompositorFrameSink() {
  return FakeCompositorFrameSink::Create3d();
}

std::unique_ptr<FakeLayerTreeHostImpl> TestLayerTreeHostBase::CreateHostImpl(
    const LayerTreeSettings& settings,
    TaskRunnerProvider* task_runner_provider,
    TaskGraphRunner* task_graph_runner) {
  return base::MakeUnique<FakeLayerTreeHostImpl>(settings, task_runner_provider,
                                                 task_graph_runner);
}

std::unique_ptr<TaskGraphRunner>
TestLayerTreeHostBase::CreateTaskGraphRunner() {
  return base::WrapUnique(new TestTaskGraphRunner);
}

void TestLayerTreeHostBase::InitializeRenderer() {
  host_impl_->SetVisible(true);
  host_impl_->InitializeRenderer(compositor_frame_sink_.get());
}

void TestLayerTreeHostBase::ResetCompositorFrameSink(
    std::unique_ptr<CompositorFrameSink> compositor_frame_sink) {
  host_impl()->DidLoseCompositorFrameSink();
  host_impl()->SetVisible(true);
  host_impl()->InitializeRenderer(compositor_frame_sink.get());
  compositor_frame_sink_ = std::move(compositor_frame_sink);
}

std::unique_ptr<FakeLayerTreeHostImpl> TestLayerTreeHostBase::TakeHostImpl() {
  return std::move(host_impl_);
}

void TestLayerTreeHostBase::SetupDefaultTrees(const gfx::Size& layer_bounds) {
  scoped_refptr<FakeRasterSource> pending_raster_source =
      FakeRasterSource::CreateFilled(layer_bounds);
  scoped_refptr<FakeRasterSource> active_raster_source =
      FakeRasterSource::CreateFilled(layer_bounds);

  SetupTrees(std::move(pending_raster_source), std::move(active_raster_source));
}

void TestLayerTreeHostBase::SetupTrees(
    scoped_refptr<RasterSource> pending_raster_source,
    scoped_refptr<RasterSource> active_raster_source) {
  SetupPendingTree(std::move(active_raster_source));
  ActivateTree();
  SetupPendingTree(std::move(pending_raster_source));
}

void TestLayerTreeHostBase::SetupPendingTree(
    scoped_refptr<RasterSource> raster_source) {
  SetupPendingTree(std::move(raster_source), gfx::Size(), Region());
}

void TestLayerTreeHostBase::SetupPendingTree(
    scoped_refptr<RasterSource> raster_source,
    const gfx::Size& tile_size,
    const Region& invalidation) {
  host_impl()->CreatePendingTree();
  host_impl()->pending_tree()->PushPageScaleFromMainThread(1.f, 0.00001f,
                                                           100000.f);
  LayerTreeImpl* pending_tree = host_impl()->pending_tree();
  pending_tree->SetDeviceScaleFactor(
      host_impl()->active_tree()->device_scale_factor());

  // Steal from the recycled tree if possible.
  LayerImpl* pending_root = pending_tree->root_layer_for_testing();
  std::unique_ptr<FakePictureLayerImpl> pending_layer;
  DCHECK(!pending_root || pending_root->id() == root_id_);
  if (!pending_root) {
    std::unique_ptr<LayerImpl> new_pending_root =
        LayerImpl::Create(pending_tree, root_id_);
    pending_layer = FakePictureLayerImpl::Create(pending_tree, id_);
    if (!tile_size.IsEmpty())
      pending_layer->set_fixed_tile_size(tile_size);
    pending_layer->SetDrawsContent(true);
    pending_layer->SetScrollClipLayer(new_pending_root->id());
    pending_root = new_pending_root.get();
    pending_tree->SetRootLayerForTesting(std::move(new_pending_root));
  } else {
    pending_layer.reset(static_cast<FakePictureLayerImpl*>(
        pending_root->test_properties()
            ->RemoveChild(pending_root->test_properties()->children[0])
            .release()));
    if (!tile_size.IsEmpty())
      pending_layer->set_fixed_tile_size(tile_size);
  }
  pending_root->test_properties()->force_render_surface = true;
  // The bounds() just mirror the raster source size.
  pending_layer->SetBounds(raster_source->GetSize());
  pending_layer->SetRasterSourceOnPending(raster_source, invalidation);

  pending_root->test_properties()->AddChild(std::move(pending_layer));
  LayerTreeImpl::ViewportLayerIds viewport_ids;
  viewport_ids.page_scale = pending_tree->root_layer_for_testing()->id();
  pending_tree->SetViewportLayersFromIds(viewport_ids);

  pending_layer_ = static_cast<FakePictureLayerImpl*>(
      host_impl()->pending_tree()->LayerById(id_));

  // Add tilings/tiles for the layer.
  bool update_lcd_text = false;
  RebuildPropertyTreesOnPendingTree();
  host_impl()->pending_tree()->UpdateDrawProperties(update_lcd_text);
}

void TestLayerTreeHostBase::ActivateTree() {
  RebuildPropertyTreesOnPendingTree();
  host_impl()->ActivateSyncTree();
  CHECK(!host_impl()->pending_tree());
  CHECK(host_impl()->recycle_tree());
  old_pending_layer_ = pending_layer_;
  pending_layer_ = nullptr;
  active_layer_ = static_cast<FakePictureLayerImpl*>(
      host_impl()->active_tree()->LayerById(id_));

  bool update_lcd_text = false;
  host_impl()->active_tree()->UpdateDrawProperties(update_lcd_text);
}

void TestLayerTreeHostBase::PerformImplSideInvalidation() {
  DCHECK(host_impl()->active_tree());
  DCHECK(!host_impl()->pending_tree());
  DCHECK(host_impl()->recycle_tree());

  host_impl()->CreatePendingTree();
  host_impl()->sync_tree()->InvalidateRegionForImages(
      host_impl()->tile_manager()->TakeImagesToInvalidateOnSyncTree());
  pending_layer_ = old_pending_layer_;
  old_pending_layer_ = nullptr;
}

void TestLayerTreeHostBase::RebuildPropertyTreesOnPendingTree() {
  host_impl()->pending_tree()->property_trees()->needs_rebuild = true;
  host_impl()->pending_tree()->BuildLayerListAndPropertyTreesForTesting();
}

void TestLayerTreeHostBase::SetInitialTreePriority() {
  GlobalStateThatImpactsTilePriority state;

  state.soft_memory_limit_in_bytes = 100 * 1000 * 1000;
  state.num_resources_limit = 10000;
  state.hard_memory_limit_in_bytes = state.soft_memory_limit_in_bytes * 2;
  state.memory_limit_policy = ALLOW_ANYTHING;
  state.tree_priority = SAME_PRIORITY_FOR_BOTH_TREES;

  host_impl_->resource_pool()->SetResourceUsageLimits(
      state.soft_memory_limit_in_bytes, state.num_resources_limit);
  host_impl_->tile_manager()->SetGlobalStateForTesting(state);
}

}  // namespace cc
