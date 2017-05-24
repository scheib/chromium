// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/proximity_auth/bluetooth_low_energy_setup_connection_finder.h"

#include <string>

#include "base/memory/ptr_util.h"
#include "components/cryptauth/ble/bluetooth_low_energy_weave_client_connection.h"
#include "components/cryptauth/bluetooth_throttler.h"
#include "components/cryptauth/connection.h"
#include "components/proximity_auth/logging/logging.h"
#include "device/bluetooth/bluetooth_device.h"
#include "device/bluetooth/bluetooth_uuid.h"

using device::BluetoothDevice;

namespace proximity_auth {

BluetoothLowEnergySetupConnectionFinder::
    BluetoothLowEnergySetupConnectionFinder(
        const std::string& remote_service_uuid,
        cryptauth::BluetoothThrottler* bluetooth_throttler)
    : BluetoothLowEnergyConnectionFinder(
          cryptauth::RemoteDevice(),
          remote_service_uuid,
          base::MakeUnique<cryptauth::BackgroundEidGenerator>(),
          bluetooth_throttler),
      remote_service_uuid_(device::BluetoothUUID(remote_service_uuid)) {}

bool BluetoothLowEnergySetupConnectionFinder::IsRightDevice(
    BluetoothDevice* device) {
  if (!device)
    return false;

  BluetoothDevice::UUIDSet uuids = device->GetUUIDs();
  PA_LOG(INFO) << "Device " << device->GetAddress() << " has " << uuids.size()
               << " services.";
  return base::ContainsKey(uuids, remote_service_uuid_);
}

}  // namespace proximity_auth
