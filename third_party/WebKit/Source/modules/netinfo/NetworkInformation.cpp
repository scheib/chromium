// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "modules/netinfo/NetworkInformation.h"

#include <limits>

#include "core/dom/ExecutionContext.h"
#include "core/dom/TaskRunnerHelper.h"
#include "core/events/Event.h"
#include "modules/EventTargetModules.h"
#include "platform/RuntimeEnabledFeatures.h"
#include "platform/wtf/text/WTFString.h"

namespace blink {

namespace {

String ConnectionTypeToString(WebConnectionType type) {
  switch (type) {
    case kWebConnectionTypeCellular2G:
    case kWebConnectionTypeCellular3G:
    case kWebConnectionTypeCellular4G:
      return "cellular";
    case kWebConnectionTypeBluetooth:
      return "bluetooth";
    case kWebConnectionTypeEthernet:
      return "ethernet";
    case kWebConnectionTypeWifi:
      return "wifi";
    case kWebConnectionTypeWimax:
      return "wimax";
    case kWebConnectionTypeOther:
      return "other";
    case kWebConnectionTypeNone:
      return "none";
    case kWebConnectionTypeUnknown:
      return "unknown";
  }
  NOTREACHED();
  return "none";
}

// Rounds |rtt_msec| to the nearest 25 milliseconds as per the NetInfo spec.
unsigned long RoundRtt(const Optional<TimeDelta>& rtt) {
  if (!rtt.has_value()) {
    // RTT is unavailable. So, return the fastest value.
    return 0;
  }

  int rtt_msec = rtt.value().InMilliseconds();
  if (rtt.value().InMilliseconds() > std::numeric_limits<int>::max())
    rtt_msec = std::numeric_limits<int>::max();

  DCHECK_LE(0, rtt_msec);
  return std::round(static_cast<double>(rtt_msec) / 25) * 25;
}

// Rounds |downlink_mbps| to the nearest 25 kbps as per the NetInfo spec. The
// returned value is in Mbps.
double RoundMbps(const Optional<double>& downlink_mbps) {
  if (!downlink_mbps.has_value()) {
    // Throughput is unavailable. So, return the fastest value.
    return std::numeric_limits<double>::infinity();
  }

  DCHECK_LE(0, downlink_mbps.value());
  double downlink_kbps = downlink_mbps.value() * 1000;
  double downlink_kbps_rounded = std::round(downlink_kbps / 25) * 25;
  return downlink_kbps_rounded / 1000;
}

}  // namespace

NetworkInformation* NetworkInformation::Create(ExecutionContext* context) {
  return new NetworkInformation(context);
}

NetworkInformation::~NetworkInformation() {
  DCHECK(!observing_);
}

String NetworkInformation::type() const {
  // m_type is only updated when listening for events, so ask
  // networkStateNotifier if not listening (crbug.com/379841).
  if (!observing_)
    return ConnectionTypeToString(GetNetworkStateNotifier().ConnectionType());

  // If observing, return m_type which changes when the event fires, per spec.
  return ConnectionTypeToString(type_);
}

double NetworkInformation::downlinkMax() const {
  if (!observing_)
    return GetNetworkStateNotifier().MaxBandwidth();

  return downlink_max_mbps_;
}

unsigned long NetworkInformation::rtt() const {
  if (!observing_)
    return RoundRtt(GetNetworkStateNotifier().TransportRtt());

  return transport_rtt_msec_;
}

double NetworkInformation::downlink() const {
  if (!observing_)
    return RoundMbps(GetNetworkStateNotifier().DownlinkThroughputMbps());

  return downlink_mbps_;
}

void NetworkInformation::ConnectionChange(
    WebConnectionType type,
    double downlink_max_mbps,
    const Optional<TimeDelta>& http_rtt,
    const Optional<TimeDelta>& transport_rtt,
    const Optional<double>& downlink_mbps) {
  DCHECK(GetExecutionContext()->IsContextThread());

  transport_rtt_msec_ = RoundRtt(transport_rtt);
  downlink_mbps_ = RoundMbps(downlink_mbps);
  // TODO(tbansal): https://crbug.com/719108. Dispatch |change| event if the
  // expected network quality has changed.

  // This can happen if the observer removes and then adds itself again
  // during notification, or if HTTP RTT was the only metric that changed.
  if (type_ == type && downlink_max_mbps_ == downlink_max_mbps)
    return;

  type_ = type;
  downlink_max_mbps_ = downlink_max_mbps;
  DispatchEvent(Event::Create(EventTypeNames::typechange));

  if (RuntimeEnabledFeatures::netInfoDownlinkMaxEnabled())
    DispatchEvent(Event::Create(EventTypeNames::change));
}

const AtomicString& NetworkInformation::InterfaceName() const {
  return EventTargetNames::NetworkInformation;
}

ExecutionContext* NetworkInformation::GetExecutionContext() const {
  return ContextLifecycleObserver::GetExecutionContext();
}

void NetworkInformation::AddedEventListener(
    const AtomicString& event_type,
    RegisteredEventListener& registered_listener) {
  EventTargetWithInlineData::AddedEventListener(event_type,
                                                registered_listener);
  StartObserving();
}

void NetworkInformation::RemovedEventListener(
    const AtomicString& event_type,
    const RegisteredEventListener& registered_listener) {
  EventTargetWithInlineData::RemovedEventListener(event_type,
                                                  registered_listener);
  if (!HasEventListeners())
    StopObserving();
}

void NetworkInformation::RemoveAllEventListeners() {
  EventTargetWithInlineData::RemoveAllEventListeners();
  DCHECK(!HasEventListeners());
  StopObserving();
}

bool NetworkInformation::HasPendingActivity() const {
  DCHECK(context_stopped_ || observing_ == HasEventListeners());

  // Prevent collection of this object when there are active listeners.
  return observing_;
}

void NetworkInformation::ContextDestroyed(ExecutionContext*) {
  context_stopped_ = true;
  StopObserving();
}

void NetworkInformation::StartObserving() {
  if (!observing_ && !context_stopped_) {
    type_ = GetNetworkStateNotifier().ConnectionType();
    GetNetworkStateNotifier().AddConnectionObserver(
        this,
        TaskRunnerHelper::Get(TaskType::kNetworking, GetExecutionContext()));
    observing_ = true;
  }
}

void NetworkInformation::StopObserving() {
  if (observing_) {
    GetNetworkStateNotifier().RemoveConnectionObserver(
        this,
        TaskRunnerHelper::Get(TaskType::kNetworking, GetExecutionContext()));
    observing_ = false;
  }
}

NetworkInformation::NetworkInformation(ExecutionContext* context)
    : ContextLifecycleObserver(context),
      type_(GetNetworkStateNotifier().ConnectionType()),
      downlink_max_mbps_(GetNetworkStateNotifier().MaxBandwidth()),
      transport_rtt_msec_(RoundRtt(GetNetworkStateNotifier().TransportRtt())),
      downlink_mbps_(
          RoundMbps(GetNetworkStateNotifier().DownlinkThroughputMbps())),
      observing_(false),
      context_stopped_(false) {}

DEFINE_TRACE(NetworkInformation) {
  EventTargetWithInlineData::Trace(visitor);
  ContextLifecycleObserver::Trace(visitor);
}

}  // namespace blink
