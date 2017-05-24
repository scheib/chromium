// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/chrome/browser/ui/payments/region_data_loader.h"

#include "base/strings/sys_string_conversions.h"
#include "components/autofill/core/browser/region_data_loader.h"
#include "ui/base/models/combobox_model.h"

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

namespace {
const int64_t kTimeoutInMilliseconds = 5000;
}  // namespace

RegionDataLoader::RegionDataLoader(id<RegionDataLoaderConsumer> consumer)
    : consumer_(consumer) {
  region_model_.AddObserver(this);
}

RegionDataLoader::~RegionDataLoader() {
  region_model_.RemoveObserver(this);
}

void RegionDataLoader::LoadRegionData(
    const std::string& country_code,
    autofill::RegionDataLoader* autofill_region_data_loader) {
  region_model_.LoadRegionData(country_code, autofill_region_data_loader,
                               kTimeoutInMilliseconds);
}

void RegionDataLoader::OnComboboxModelChanged(ui::ComboboxModel* model) {
  autofill::RegionComboboxModel* region_model =
      static_cast<autofill::RegionComboboxModel*>(model);
  if (region_model->IsPendingRegionDataLoad())
    return;

  NSMutableArray<NSString*>* regions = [[NSMutableArray alloc] init];
  if (!region_model->failed_to_load_data()) {
    for (int i = 0; i < region_model->GetItemCount(); ++i) {
      if (!region_model->IsItemSeparatorAt(i))
        [regions
            addObject:base::SysUTF16ToNSString(region_model->GetItemAt(i))];
    }
  }
  [consumer_ regionDataLoaderDidSucceedWithRegions:regions];
}
