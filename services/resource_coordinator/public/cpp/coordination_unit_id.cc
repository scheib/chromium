// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/resource_coordinator/public/cpp/coordination_unit_id.h"

#include "base/logging.h"
#include "base/numerics/safe_conversions.h"
#include "third_party/smhasher/src/MurmurHash2.h"

namespace resource_coordinator {

// The seed to use when taking the murmur2 hash of the id.
const uint64_t kMurmur2HashSeed = 0;

CoordinationUnitID::CoordinationUnitID()
    : id(0), type(CoordinationUnitType::kInvalidType) {}

CoordinationUnitID::CoordinationUnitID(const CoordinationUnitType& type,
                                       const std::string& new_id)
    : type(type) {
  DCHECK(base::IsValueInRangeForNumericType<int>(new_id.size()));
  if (!new_id.empty()) {
    id = MurmurHash64A(&new_id.front(), static_cast<int>(new_id.size()),
                       kMurmur2HashSeed);
  } else {
    id = 0;
  }
}

CoordinationUnitID::CoordinationUnitID(const CoordinationUnitType& type,
                                       uint64_t new_id)
    : id(new_id), type(type) {}

}  // resource_coordinator
