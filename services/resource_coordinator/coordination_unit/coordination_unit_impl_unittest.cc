// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <utility>
#include <vector>

#include "base/bind.h"
#include "base/message_loop/message_loop.h"
#include "base/run_loop.h"
#include "services/resource_coordinator/coordination_unit/coordination_unit_impl_unittest_util.h"
#include "services/resource_coordinator/coordination_unit/coordination_unit_provider_impl.h"
#include "services/service_manager/public/cpp/service_context_ref.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace resource_coordinator {

namespace {

class CoordinationUnitImplTest : public CoordinationUnitImplTestBase {};

class TestCoordinationUnit : public mojom::CoordinationPolicyCallback {
 public:
  TestCoordinationUnit(CoordinationUnitProviderImpl* provider,
                       const CoordinationUnitType& type,
                       const std::string& id)
      : binding_(this) {
    CHECK(provider);

    CoordinationUnitID new_cu_id(type, id);
    mojom::CoordinationUnitPtr coordination_unit;
    provider->CreateCoordinationUnit(mojo::MakeRequest(&coordination_unit_),
                                     new_cu_id);

    base::RunLoop callback;
    SetGetIDClosure(callback.QuitClosure());
    coordination_unit_->SetCoordinationPolicyCallback(GetPolicyCallback());
    // Forces us to wait for the creation of the CUID to finish.
    coordination_unit_->GetID(base::Bind(&TestCoordinationUnit::GetIDCallback,
                                         base::Unretained(this)));

    callback.Run();
  }

  void GetIDCallback(const CoordinationUnitID& cu_id) {
    id_ = cu_id;
    get_id_closure_.Run();
  }

  void SetGetIDClosure(const base::Closure& get_id_closure) {
    get_id_closure_ = get_id_closure;
  }

  void SetPolicyClosure(const base::Closure& policy_closure) {
    policy_update_closure_ = policy_closure;
  }

  mojom::CoordinationPolicyCallbackPtr GetPolicyCallback() {
    return binding_.CreateInterfacePtrAndBind();
  }

  // The CU will always send policy updates on events (including parent events)
  void ForcePolicyUpdates() {
    base::RunLoop callback;
    SetPolicyClosure(callback.QuitClosure());
    mojom::EventPtr event = mojom::Event::New();
    event->type = mojom::EventType::kTestEvent;
    coordination_unit_->SendEvent(std::move(event));
    callback.Run();
  }

  const mojom::CoordinationUnitPtr& interface() const {
    return coordination_unit_;
  }

  const CoordinationUnitID& id() const { return id_; }

  // mojom::CoordinationPolicyCallback:
  void SetCoordinationPolicy(
      resource_coordinator::mojom::CoordinationPolicyPtr policy) override {
    if (policy_update_closure_) {
      policy_update_closure_.Run();
    }
  }

 private:
  base::Closure policy_update_closure_;
  base::Closure get_id_closure_;

  mojo::Binding<mojom::CoordinationPolicyCallback> binding_;
  mojom::CoordinationUnitPtr coordination_unit_;
  CoordinationUnitID id_;
};

}  // namespace

TEST_F(CoordinationUnitImplTest, BasicPolicyCallback) {
  TestCoordinationUnit test_coordination_unit(
      provider(), CoordinationUnitType::kWebContents, "test_id");
  test_coordination_unit.ForcePolicyUpdates();
}

TEST_F(CoordinationUnitImplTest, AddChild) {
  TestCoordinationUnit parent_unit(
      provider(), CoordinationUnitType::kWebContents, "parent_unit");

  TestCoordinationUnit child_unit(
      provider(), CoordinationUnitType::kWebContents, "child_unit");

  child_unit.ForcePolicyUpdates();
  parent_unit.ForcePolicyUpdates();

  {
    base::RunLoop callback;
    child_unit.SetPolicyClosure(callback.QuitClosure());
    parent_unit.interface()->AddChild(child_unit.id());
    callback.Run();
  }

  {
    base::RunLoop parent_callback;
    base::RunLoop child_callback;
    parent_unit.SetPolicyClosure(parent_callback.QuitClosure());
    child_unit.SetPolicyClosure(child_callback.QuitClosure());

    // This event should force the policy to recalculated for all children.
    mojom::EventPtr event = mojom::Event::New();
    event->type = mojom::EventType::kTestEvent;
    parent_unit.interface()->SendEvent(std::move(event));

    parent_callback.Run();
    child_callback.Run();
  }
}

TEST_F(CoordinationUnitImplTest, CyclicGraphUnits) {
  TestCoordinationUnit parent_unit(
      provider(), CoordinationUnitType::kWebContents, std::string());

  TestCoordinationUnit child_unit(
      provider(), CoordinationUnitType::kWebContents, std::string());

  child_unit.ForcePolicyUpdates();
  parent_unit.ForcePolicyUpdates();

  {
    base::RunLoop callback;
    child_unit.SetPolicyClosure(callback.QuitClosure());
    parent_unit.interface()->AddChild(child_unit.id());
    callback.Run();
  }

  // This should fail, due to the existing child-parent relationship.
  // Otherwise we end up with infinite recursion and crash when recalculating
  // policies below.
  child_unit.interface()->AddChild(parent_unit.id());

  {
    base::RunLoop parent_callback;
    base::RunLoop child_callback;
    parent_unit.SetPolicyClosure(parent_callback.QuitClosure());
    child_unit.SetPolicyClosure(child_callback.QuitClosure());

    // This event should force the policy to recalculated for all children.
    mojom::EventPtr event = mojom::Event::New();
    event->type = mojom::EventType::kTestEvent;
    parent_unit.interface()->SendEvent(std::move(event));

    parent_callback.Run();
    child_callback.Run();
  }
}

}  // namespace resource_coordinator
