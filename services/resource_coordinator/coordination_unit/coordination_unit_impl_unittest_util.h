// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_RESOURCE_COORDINATOR_COORDINATION_UNIT_COORDINATION_UNIT_IMPL_UNITTEST_UTIL_H_
#define SERVICES_RESOURCE_COORDINATOR_COORDINATION_UNIT_COORDINATION_UNIT_IMPL_UNITTEST_UTIL_H_

#include "base/message_loop/message_loop.h"
#include "services/resource_coordinator/coordination_unit/coordination_unit_provider_impl.h"
#include "services/service_manager/public/cpp/service_context_ref.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace resource_coordinator {

class CoordinationUnitImplTestBase : public testing::Test {
 public:
  CoordinationUnitImplTestBase();
  ~CoordinationUnitImplTestBase() override;

  // testing::Test:
  void TearDown() override;

 protected:
  service_manager::ServiceContextRefFactory* service_context_ref_factory() {
    return &service_ref_factory_;
  }
  CoordinationUnitProviderImpl* provider() { return &provider_; }

 private:
  base::MessageLoop message_loop_;
  service_manager::ServiceContextRefFactory service_ref_factory_;
  CoordinationUnitProviderImpl provider_;
};

}  // namespace resource_coordinator

#endif  // SERVICES_RESOURCE_COORDINATOR_COORDINATION_UNIT_COORDINATION_UNIT_IMPL_UNITTEST_UTIL_H_
