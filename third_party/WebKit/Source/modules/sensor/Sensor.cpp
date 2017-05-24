// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "modules/sensor/Sensor.h"

#include "core/dom/Document.h"
#include "core/dom/ExceptionCode.h"
#include "core/dom/TaskRunnerHelper.h"
#include "core/inspector/ConsoleMessage.h"
#include "core/timing/DOMWindowPerformance.h"
#include "core/timing/Performance.h"
#include "device/generic_sensor/public/interfaces/sensor.mojom-blink.h"
#include "modules/sensor/SensorErrorEvent.h"
#include "modules/sensor/SensorProviderProxy.h"

using namespace device::mojom::blink;

namespace blink {

namespace {

constexpr double kMinWaitingInterval =
    1 / device::mojom::blink::SensorConfiguration::kMaxAllowedFrequency;

}  // namespace

Sensor::Sensor(ExecutionContext* execution_context,
               const SensorOptions& sensor_options,
               ExceptionState& exception_state,
               SensorType type)
    : ContextLifecycleObserver(execution_context),
      sensor_options_(sensor_options),
      type_(type),
      state_(SensorState::kIdle) {
  // Check secure context.
  String error_message;
  if (!execution_context->IsSecureContext(error_message)) {
    exception_state.ThrowDOMException(kSecurityError, error_message);
    return;
  }

  // Check top-level browsing context.
  if (!ToDocument(execution_context)->domWindow()->GetFrame() ||
      !ToDocument(execution_context)->GetFrame()->IsMainFrame()) {
    exception_state.ThrowSecurityError(
        "Must be in a top-level browsing context");
    return;
  }

  // Check the given frequency value.
  if (sensor_options_.hasFrequency()) {
    double frequency = sensor_options_.frequency();
    if (frequency > SensorConfiguration::kMaxAllowedFrequency) {
      sensor_options_.setFrequency(SensorConfiguration::kMaxAllowedFrequency);
      ConsoleMessage* console_message =
          ConsoleMessage::Create(kJSMessageSource, kInfoMessageLevel,
                                 "Frequency is limited to 60 Hz.");
      execution_context->AddConsoleMessage(console_message);
    }
  }
}

Sensor::~Sensor() = default;

void Sensor::start() {
  StartListening();
}

void Sensor::stop() {
  StopListening();
}

// Getters
bool Sensor::activated() const {
  return state_ == SensorState::kActivated;
}

DOMHighResTimeStamp Sensor::timestamp(ScriptState* script_state,
                                      bool& is_null) const {
  if (!CanReturnReadings()) {
    is_null = true;
    return 0.0;
  }

  LocalDOMWindow* window = LocalDOMWindow::From(script_state);
  if (!window) {
    is_null = true;
    return 0.0;
  }

  Performance* performance = DOMWindowPerformance::performance(*window);
  DCHECK(performance);
  DCHECK(sensor_proxy_);
  is_null = false;

  return performance->MonotonicTimeToDOMHighResTimeStamp(
      sensor_proxy_->reading().timestamp);
}

DEFINE_TRACE(Sensor) {
  visitor->Trace(sensor_proxy_);
  ActiveScriptWrappable::Trace(visitor);
  ContextLifecycleObserver::Trace(visitor);
  EventTargetWithInlineData::Trace(visitor);
}

bool Sensor::HasPendingActivity() const {
  if (state_ == Sensor::SensorState::kIdle)
    return false;
  return GetExecutionContext() && HasEventListeners();
}

auto Sensor::CreateSensorConfig() -> SensorConfigurationPtr {
  auto result = SensorConfiguration::New();

  double default_frequency = sensor_proxy_->DefaultConfig()->frequency;
  double minimum_frequency = sensor_proxy_->FrequencyLimits().first;
  double maximum_frequency = sensor_proxy_->FrequencyLimits().second;

  double frequency = sensor_options_.hasFrequency()
                         ? sensor_options_.frequency()
                         : default_frequency;

  if (frequency > maximum_frequency)
    frequency = maximum_frequency;
  if (frequency < minimum_frequency)
    frequency = minimum_frequency;

  result->frequency = frequency;
  return result;
}

double Sensor::ReadingValue(int index, bool& is_null) const {
  is_null = !CanReturnReadings();
  return is_null ? 0.0 : ReadingValueUnchecked(index);
}

double Sensor::ReadingValueUnchecked(int index) const {
  DCHECK(sensor_proxy_);
  DCHECK(index >= 0 && index < device::SensorReading::kValuesCount);
  return sensor_proxy_->reading().values[index];
}

void Sensor::InitSensorProxyIfNeeded() {
  if (sensor_proxy_)
    return;

  Document* document = ToDocument(GetExecutionContext());
  if (!document || !document->GetFrame())
    return;

  auto provider = SensorProviderProxy::From(document->GetFrame());
  sensor_proxy_ = provider->GetSensorProxy(type_);

  if (!sensor_proxy_)
    sensor_proxy_ = provider->CreateSensorProxy(type_, document->GetPage());
}

void Sensor::ContextDestroyed(ExecutionContext*) {
  StopListening();
}

void Sensor::OnSensorInitialized() {
  if (state_ != Sensor::SensorState::kActivating)
    return;

  RequestAddConfiguration();
}

void Sensor::OnSensorReadingChanged() {
  if (state_ != Sensor::SensorState::kActivated)
    return;

  // Return if reading update is already scheduled or the cached
  // reading is up-to-date.
  if (pending_reading_update_.IsActive())
    return;

  double elapsedTime = sensor_proxy_->reading().timestamp - reading_.timestamp;
  DCHECK_GT(elapsedTime, 0.0);

  DCHECK_GT(configuration_->frequency, 0.0);
  double waitingTime = 1 / configuration_->frequency - elapsedTime;

  // Negative or zero 'waitingTime' means that polling period has elapsed.
  // We also avoid scheduling if the elapsed time is slightly behind the
  // polling period.
  auto sensor_reading_changed =
      WTF::Bind(&Sensor::UpdateReading, WrapWeakPersistent(this));
  if (waitingTime < kMinWaitingInterval) {
    // Invoke JS callbacks in a different callchain to obviate
    // possible modifications of SensorProxy::observers_ container
    // while it is being iterated through.
    pending_reading_update_ =
        TaskRunnerHelper::Get(TaskType::kSensor, GetExecutionContext())
            ->PostCancellableTask(BLINK_FROM_HERE,
                                  std::move(sensor_reading_changed));
  } else {
    pending_reading_update_ =
        TaskRunnerHelper::Get(TaskType::kSensor, GetExecutionContext())
            ->PostDelayedCancellableTask(
                BLINK_FROM_HERE, std::move(sensor_reading_changed),
                WTF::TimeDelta::FromSecondsD(waitingTime));
  }
}

void Sensor::OnSensorError(ExceptionCode code,
                           const String& sanitized_message,
                           const String& unsanitized_message) {
  HandleError(code, sanitized_message, unsanitized_message);
}

void Sensor::OnAddConfigurationRequestCompleted(bool result) {
  if (state_ != SensorState::kActivating)
    return;

  if (!result) {
    HandleError(kNotReadableError, "start() call has failed.");
    return;
  }

  UpdateState(Sensor::SensorState::kActivated);

  if (GetExecutionContext()) {
    TaskRunnerHelper::Get(TaskType::kSensor, GetExecutionContext())
        ->PostTask(BLINK_FROM_HERE, WTF::Bind(&Sensor::NotifyOnActivate,
                                              WrapWeakPersistent(this)));
  }
}

void Sensor::StartListening() {
  if (state_ != SensorState::kIdle)
    return;

  InitSensorProxyIfNeeded();
  if (!sensor_proxy_) {
    HandleError(kInvalidStateError,
                "The Sensor is no longer associated to a frame.");
    return;
  }

  if (sensor_proxy_->IsInitialized())
    RequestAddConfiguration();
  else
    sensor_proxy_->Initialize();

  sensor_proxy_->AddObserver(this);
  UpdateState(SensorState::kActivating);
}

void Sensor::StopListening() {
  if (state_ == SensorState::kIdle)
    return;

  pending_reading_update_.Cancel();

  DCHECK(sensor_proxy_);
  if (sensor_proxy_->IsInitialized()) {
    DCHECK(configuration_);
    sensor_proxy_->RemoveConfiguration(configuration_->Clone());
  }

  sensor_proxy_->RemoveObserver(this);
  UpdateState(Sensor::SensorState::kIdle);
}

void Sensor::RequestAddConfiguration() {
  if (!configuration_) {
    configuration_ = CreateSensorConfig();
    DCHECK(configuration_);
    DCHECK(configuration_->frequency > 0 &&
           configuration_->frequency <=
               SensorConfiguration::kMaxAllowedFrequency);
  }

  DCHECK(sensor_proxy_);
  sensor_proxy_->AddConfiguration(
      configuration_->Clone(),
      WTF::Bind(&Sensor::OnAddConfigurationRequestCompleted,
                WrapWeakPersistent(this)));
}

void Sensor::UpdateState(Sensor::SensorState new_state) {
  state_ = new_state;
}

void Sensor::HandleError(ExceptionCode code,
                         const String& sanitized_message,
                         const String& unsanitized_message) {
  StopListening();

  if (GetExecutionContext()) {
    auto error =
        DOMException::Create(code, sanitized_message, unsanitized_message);
    TaskRunnerHelper::Get(TaskType::kSensor, GetExecutionContext())
        ->PostTask(BLINK_FROM_HERE,
                   WTF::Bind(&Sensor::NotifyError, WrapWeakPersistent(this),
                             WrapPersistent(error)));
  }
}

void Sensor::UpdateReading() {
  reading_ = sensor_proxy_->reading();
  DispatchEvent(Event::Create(EventTypeNames::change));
}

void Sensor::NotifyOnActivate() {
  DispatchEvent(Event::Create(EventTypeNames::activate));
}

void Sensor::NotifyError(DOMException* error) {
  DispatchEvent(
      SensorErrorEvent::Create(EventTypeNames::error, std::move(error)));
}

bool Sensor::CanReturnReadings() const {
  if (!IsActivated())
    return false;
  DCHECK(sensor_proxy_);
  return sensor_proxy_->reading().timestamp != 0.0;
}

}  // namespace blink
