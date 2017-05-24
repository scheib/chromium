// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef Sensor_h
#define Sensor_h

#include "core/dom/ContextLifecycleObserver.h"
#include "core/dom/DOMHighResTimeStamp.h"
#include "core/dom/DOMTimeStamp.h"
#include "core/dom/SuspendableObject.h"
#include "core/frame/PlatformEventController.h"
#include "modules/EventTargetModules.h"
#include "modules/sensor/SensorOptions.h"
#include "modules/sensor/SensorProxy.h"
#include "platform/WebTaskRunner.h"
#include "platform/bindings/ActiveScriptWrappable.h"
#include "platform/bindings/ScriptWrappable.h"
#include "platform/heap/Handle.h"

namespace blink {

class DOMException;
class ExceptionState;
class ExecutionContext;
class SensorReading;

class Sensor : public EventTargetWithInlineData,
               public ActiveScriptWrappable<Sensor>,
               public ContextLifecycleObserver,
               public SensorProxy::Observer {
  USING_GARBAGE_COLLECTED_MIXIN(Sensor);
  DEFINE_WRAPPERTYPEINFO();

 public:
  enum class SensorState { kIdle, kActivating, kActivated };

  ~Sensor() override;

  void start();
  void stop();

  // EventTarget overrides.
  const AtomicString& InterfaceName() const override {
    return EventTargetNames::Sensor;
  }
  ExecutionContext* GetExecutionContext() const override {
    return ContextLifecycleObserver::GetExecutionContext();
  }

  // Getters
  bool activated() const;
  DOMHighResTimeStamp timestamp(ScriptState*, bool& is_null) const;

  DEFINE_ATTRIBUTE_EVENT_LISTENER(error);
  DEFINE_ATTRIBUTE_EVENT_LISTENER(change);
  DEFINE_ATTRIBUTE_EVENT_LISTENER(activate);

  // ActiveScriptWrappable overrides.
  bool HasPendingActivity() const override;

  DECLARE_VIRTUAL_TRACE();

 protected:
  Sensor(ExecutionContext*,
         const SensorOptions&,
         ExceptionState&,
         device::mojom::blink::SensorType);

  using SensorConfigurationPtr = device::mojom::blink::SensorConfigurationPtr;
  using SensorConfiguration = device::mojom::blink::SensorConfiguration;

  // The default implementation will init frequency configuration parameter,
  // concrete sensor implementations can override this method to handle other
  // parameters if needed.
  virtual SensorConfigurationPtr CreateSensorConfig();

  double ReadingValue(int index, bool& is_null) const;
  double ReadingValueUnchecked(int index) const;
  bool CanReturnReadings() const;
  bool IsActivated() const { return state_ == SensorState::kActivated; }

  // SensorProxy::Observer overrides.
  void OnSensorInitialized() override;
  void OnSensorReadingChanged() override;
  void OnSensorError(ExceptionCode,
                     const String& sanitized_message,
                     const String& unsanitized_message) override;

 private:
  void InitSensorProxyIfNeeded();

  // ContextLifecycleObserver overrides.
  void ContextDestroyed(ExecutionContext*) override;

  void OnAddConfigurationRequestCompleted(bool);

  void StartListening();
  void StopListening();

  void RequestAddConfiguration();

  void UpdateState(SensorState new_state);
  void HandleError(ExceptionCode = kUnknownError,
                   const String& sanitized_message = String(),
                   const String& unsanitized_message = String());

  void UpdateReading();
  void NotifyOnActivate();
  void NotifyError(DOMException* error);

 private:
  SensorOptions sensor_options_;
  device::mojom::blink::SensorType type_;
  SensorState state_;
  Member<SensorProxy> sensor_proxy_;
  device::SensorReading reading_;
  SensorConfigurationPtr configuration_;
  TaskHandle pending_reading_update_;
};

}  // namespace blink

#endif  // Sensor_h
