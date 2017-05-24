// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "platform/graphics/CompositorMutableState.h"

#include "base/message_loop/message_loop.h"
#include "cc/test/fake_compositor_frame_sink.h"
#include "cc/test/fake_impl_task_runner_provider.h"
#include "cc/test/fake_layer_tree_host_impl.h"
#include "cc/test/test_shared_bitmap_manager.h"
#include "cc/test/test_task_graph_runner.h"
#include "cc/trees/layer_tree_host_impl.h"
#include "cc/trees/layer_tree_impl.h"
#include "platform/graphics/CompositorElementId.h"
#include "platform/graphics/CompositorMutableProperties.h"
#include "platform/graphics/CompositorMutableStateProvider.h"
#include "platform/graphics/CompositorMutation.h"
#include "testing/gtest/include/gtest/gtest.h"
#include <memory>

namespace blink {

using cc::FakeImplTaskRunnerProvider;
using cc::FakeLayerTreeHostImpl;
using cc::FakeCompositorFrameSink;
using cc::LayerImpl;
using cc::LayerTreeSettings;
using cc::TestTaskGraphRunner;
using cc::TestSharedBitmapManager;

class CompositorMutableStateTest : public testing::Test {
 public:
  CompositorMutableStateTest()
      : compositor_frame_sink_(FakeCompositorFrameSink::Create3d()) {
    LayerTreeSettings settings;
    settings.layer_transforms_should_scale_layer_contents = true;
    host_impl_.reset(new FakeLayerTreeHostImpl(settings, &task_runner_provider_,
                                               &task_graph_runner_));
    host_impl_->SetVisible(true);
    EXPECT_TRUE(host_impl_->InitializeRenderer(compositor_frame_sink_.get()));
  }

  void SetLayerPropertiesForTesting(LayerImpl* layer) {
    layer->test_properties()->transform = gfx::Transform();
    layer->SetPosition(gfx::PointF());
    layer->SetBounds(gfx::Size(100, 100));
    layer->SetDrawsContent(true);
  }

  FakeLayerTreeHostImpl& HostImpl() { return *host_impl_; }

  LayerImpl* RootLayer() {
    return host_impl_->active_tree()->root_layer_for_testing();
  }

 private:
  // The cc testing machinery has fairly deep dependency on having a main
  // message loop (one example is the task runner provider). We construct one
  // here so that it's installed in TLA and can be found by other cc classes.
  base::MessageLoop message_loop_;
  TestTaskGraphRunner task_graph_runner_;
  FakeImplTaskRunnerProvider task_runner_provider_;
  std::unique_ptr<FakeCompositorFrameSink> compositor_frame_sink_;
  std::unique_ptr<FakeLayerTreeHostImpl> host_impl_;
};

TEST_F(CompositorMutableStateTest, NoMutableState) {
  // In this test, there are no layers with either an element id or mutable
  // properties. We should not be able to get any mutable state.
  std::unique_ptr<LayerImpl> root =
      LayerImpl::Create(HostImpl().active_tree(), 42);
  SetLayerPropertiesForTesting(root.get());

  HostImpl().SetViewportSize(root->bounds());
  HostImpl().active_tree()->SetRootLayerForTesting(std::move(root));
  HostImpl().UpdateNumChildrenAndDrawPropertiesForActiveTree();

  CompositorMutations mutations;
  CompositorMutableStateProvider provider(HostImpl().active_tree(), &mutations);
  std::unique_ptr<CompositorMutableState> state(
      provider.GetMutableStateFor(42));
  EXPECT_FALSE(state);
}

TEST_F(CompositorMutableStateTest, MutableStateMutableProperties) {
  // In this test, there is a layer with an element id and mutable properties.
  // In this case, we should get a valid mutable state for this element id that
  // has a real effect on the corresponding layer.
  std::unique_ptr<LayerImpl> root =
      LayerImpl::Create(HostImpl().active_tree(), 42);

  std::unique_ptr<LayerImpl> scoped_layer =
      LayerImpl::Create(HostImpl().active_tree(), 11);
  LayerImpl* layer = scoped_layer.get();
  layer->SetScrollClipLayer(root->id());

  root->test_properties()->AddChild(std::move(scoped_layer));

  SetLayerPropertiesForTesting(layer);

  uint64_t primary_id = 12;
  root->SetElementId(CompositorElementIdFromDOMNodeId(
      primary_id, CompositorElementIdNamespace::kPrimaryCompositorProxy));
  layer->SetElementId(CompositorElementIdFromDOMNodeId(
      primary_id, CompositorElementIdNamespace::kScrollCompositorProxy));

  root->SetMutableProperties(CompositorMutableProperty::kOpacity |
                             CompositorMutableProperty::kTransform);
  layer->SetMutableProperties(CompositorMutableProperty::kScrollLeft |
                              CompositorMutableProperty::kScrollTop);

  HostImpl().SetViewportSize(layer->bounds());
  HostImpl().active_tree()->SetRootLayerForTesting(std::move(root));
  HostImpl().UpdateNumChildrenAndDrawPropertiesForActiveTree();

  CompositorMutations mutations;
  CompositorMutableStateProvider provider(HostImpl().active_tree(), &mutations);

  std::unique_ptr<CompositorMutableState> state(
      provider.GetMutableStateFor(primary_id));
  EXPECT_TRUE(state.get());

  EXPECT_EQ(1.0, RootLayer()->Opacity());
  EXPECT_EQ(gfx::Transform().ToString(), RootLayer()->Transform().ToString());
  EXPECT_EQ(0.0, layer->CurrentScrollOffset().x());
  EXPECT_EQ(0.0, layer->CurrentScrollOffset().y());

  gfx::Transform zero(0, 0, 0, 0, 0, 0);
  state->SetOpacity(0.5);
  state->SetTransform(zero.matrix());
  state->SetScrollLeft(1.0);
  state->SetScrollTop(1.0);

  EXPECT_EQ(0.5, RootLayer()->Opacity());
  EXPECT_EQ(zero.ToString(), RootLayer()->Transform().ToString());
  EXPECT_EQ(1.0, layer->CurrentScrollOffset().x());
  EXPECT_EQ(1.0, layer->CurrentScrollOffset().y());

  // The corresponding mutation should reflect the changed values.
  EXPECT_EQ(1ul, mutations.map.size());

  const CompositorMutation& mutation = *mutations.map.find(primary_id)->value;
  EXPECT_TRUE(mutation.IsOpacityMutated());
  EXPECT_TRUE(mutation.IsTransformMutated());
  EXPECT_TRUE(mutation.IsScrollLeftMutated());
  EXPECT_TRUE(mutation.IsScrollTopMutated());

  EXPECT_EQ(0.5, mutation.Opacity());
  EXPECT_EQ(zero.ToString(), gfx::Transform(mutation.Transform()).ToString());
  EXPECT_EQ(1.0, mutation.ScrollLeft());
  EXPECT_EQ(1.0, mutation.ScrollTop());
}

}  // namespace blink
