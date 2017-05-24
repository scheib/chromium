// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/settings/chromeos/device_power_handler.h"

#include <memory>
#include <utility>

#include "ash/resources/grit/ash_resources.h"
#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/grit/generated_resources.h"
#include "content/public/browser/web_ui.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/l10n/time_format.h"
#include "ui/base/webui/web_ui_util.h"

namespace chromeos {
namespace settings {
namespace {

base::string16 GetBatteryTimeText(base::TimeDelta time_left) {
  int hour = 0;
  int min = 0;
  ash::PowerStatus::SplitTimeIntoHoursAndMinutes(time_left, &hour, &min);

  base::string16 time_text;
  if (hour == 0 || min == 0) {
    // Display only one unit ("2 hours" or "10 minutes").
    return ui::TimeFormat::Simple(ui::TimeFormat::FORMAT_DURATION,
                                  ui::TimeFormat::LENGTH_LONG, time_left);
  }

  return ui::TimeFormat::Detailed(ui::TimeFormat::FORMAT_DURATION,
                                  ui::TimeFormat::LENGTH_LONG,
                                  -1,  // force hour and minute output
                                  time_left);
}

}  // namespace

PowerHandler::PowerHandler()
    : power_observer_(this) {
  power_status_ = ash::PowerStatus::Get();
}

PowerHandler::~PowerHandler() {}

void PowerHandler::RegisterMessages() {
  web_ui()->RegisterMessageCallback(
      "updatePowerStatus", base::Bind(&PowerHandler::HandleUpdatePowerStatus,
                                      base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "setPowerSource",
      base::Bind(&PowerHandler::HandleSetPowerSource, base::Unretained(this)));
}

void PowerHandler::OnJavascriptAllowed() {
  power_observer_.Add(power_status_);
}

void PowerHandler::OnJavascriptDisallowed() {
  power_observer_.RemoveAll();
}

void PowerHandler::OnPowerStatusChanged() {
  SendBatteryStatus();
  SendPowerSources();
}

void PowerHandler::HandleUpdatePowerStatus(const base::ListValue* args) {
  AllowJavascript();
  power_status_->RequestStatusUpdate();
}

void PowerHandler::HandleSetPowerSource(const base::ListValue* args) {
  AllowJavascript();

  std::string id;
  CHECK(args->GetString(0, &id));
  power_status_->SetPowerSource(id);
}

void PowerHandler::SendBatteryStatus() {
  bool charging = power_status_->IsBatteryCharging();
  bool calculating = power_status_->IsBatteryTimeBeingCalculated();
  int percent = power_status_->GetRoundedBatteryPercent();
  base::TimeDelta time_left;
  bool show_time = false;

  if (!calculating) {
    time_left = charging ? power_status_->GetBatteryTimeToFull()
                         : power_status_->GetBatteryTimeToEmpty();
    show_time = ash::PowerStatus::ShouldDisplayBatteryTime(time_left);
  }

  base::string16 status_text;
  if (show_time) {
    status_text = l10n_util::GetStringFUTF16(
        charging ? IDS_OPTIONS_BATTERY_STATUS_CHARGING
                 : IDS_OPTIONS_BATTERY_STATUS,
        base::IntToString16(percent), GetBatteryTimeText(time_left));
  } else {
    status_text = l10n_util::GetStringFUTF16(IDS_OPTIONS_BATTERY_STATUS_SHORT,
                                             base::IntToString16(percent));
  }

  base::DictionaryValue battery_dict;
  battery_dict.SetBoolean("charging", charging);
  battery_dict.SetBoolean("calculating", calculating);
  battery_dict.SetInteger("percent", percent);
  battery_dict.SetString("statusText", status_text);

  FireWebUIListener("battery-status-changed", battery_dict);
}

void PowerHandler::SendPowerSources() {
  base::ListValue sources_list;
  for (const auto& source : power_status_->GetPowerSources()) {
    std::unique_ptr<base::DictionaryValue> dict(new base::DictionaryValue());
    dict->SetString("id", source.id);
    dict->SetInteger("type", source.type);
    dict->SetString("description",
                    l10n_util::GetStringUTF16(source.description_id));
    sources_list.Append(std::move(dict));
  }

  FireWebUIListener("power-sources-changed", sources_list,
                    base::Value(power_status_->GetCurrentPowerSourceID()),
                    base::Value(power_status_->IsUsbChargerConnected()));
}

}  // namespace settings
}  // namespace chromeos
