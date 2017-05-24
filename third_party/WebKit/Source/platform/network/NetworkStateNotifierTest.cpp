/*
 * Copyright (c) 2014, Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "platform/network/NetworkStateNotifier.h"

#include "platform/scheduler/test/fake_web_task_runner.h"
#include "platform/testing/UnitTestHelpers.h"
#include "platform/wtf/Functional.h"
#include "platform/wtf/Optional.h"
#include "platform/wtf/Time.h"
#include "public/platform/Platform.h"
#include "public/platform/WebConnectionType.h"
#include "public/platform/WebThread.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace blink {

using scheduler::FakeWebTaskRunner;

namespace {
const double kNoneMaxBandwidthMbps = 0.0;
const double kBluetoothMaxBandwidthMbps = 1.0;
const double kEthernetMaxBandwidthMbps = 2.0;
const Optional<TimeDelta> kEthernetHttpRtt(TimeDelta::FromMilliseconds(50));
const Optional<TimeDelta> kEthernetTransportRtt(
    TimeDelta::FromMilliseconds(25));
const Optional<double> kEthernetThroughputMbps(75.0);
const Optional<TimeDelta> kUnknownRtt;
const Optional<double> kUnknownThroughputMbps;
}

class StateObserver : public NetworkStateNotifier::NetworkStateObserver {
 public:
  StateObserver()
      : observed_type_(kWebConnectionTypeNone),
        observed_max_bandwidth_mbps_(0.0),
        observed_http_rtt_(kUnknownRtt),
        observed_transport_rtt_(kUnknownRtt),
        observed_downlink_throughput_mbps_(kUnknownThroughputMbps),
        observed_on_line_state_(false),
        callback_count_(0) {}

  virtual void ConnectionChange(
      WebConnectionType type,
      double max_bandwidth_mbps,
      const Optional<TimeDelta>& http_rtt,
      const Optional<TimeDelta>& transport_rtt,
      const Optional<double>& downlink_throughput_mbps) {
    observed_type_ = type;
    observed_max_bandwidth_mbps_ = max_bandwidth_mbps;
    observed_http_rtt_ = http_rtt;
    observed_transport_rtt_ = transport_rtt;
    observed_downlink_throughput_mbps_ = downlink_throughput_mbps;
    callback_count_ += 1;

    if (closure_)
      (*closure_)();
  }

  virtual void OnLineStateChange(bool on_line) {
    observed_on_line_state_ = on_line;
    callback_count_ += 1;

    if (closure_)
      (*closure_)();
  }

  WebConnectionType ObservedType() const { return observed_type_; }
  double ObservedMaxBandwidth() const { return observed_max_bandwidth_mbps_; }
  Optional<TimeDelta> ObservedHttpRtt() const { return observed_http_rtt_; }
  Optional<TimeDelta> ObservedTransportRtt() const {
    return observed_transport_rtt_;
  }
  Optional<double> ObservedDownlinkThroughputMbps() const {
    return observed_downlink_throughput_mbps_;
  }
  bool ObservedOnLineState() const { return observed_on_line_state_; }
  int CallbackCount() const { return callback_count_; }

  void SetNotificationCallback(std::unique_ptr<WTF::Closure> closure) {
    closure_ = std::move(closure);
  }

 private:
  std::unique_ptr<WTF::Closure> closure_;
  WebConnectionType observed_type_;
  double observed_max_bandwidth_mbps_;
  Optional<TimeDelta> observed_http_rtt_;
  Optional<TimeDelta> observed_transport_rtt_;
  Optional<double> observed_downlink_throughput_mbps_;
  bool observed_on_line_state_;
  int callback_count_;
};

class NetworkStateNotifierTest : public ::testing::Test {
 public:
  NetworkStateNotifierTest()
      : task_runner_(AdoptRef(new FakeWebTaskRunner())),
        task_runner2_(AdoptRef(new FakeWebTaskRunner())) {
    // Initialize connection, so that future calls to setWebConnection issue
    // notifications.
    notifier_.SetWebConnection(kWebConnectionTypeUnknown, 0.0);
    notifier_.SetOnLine(false);
  }

  WebTaskRunner* GetTaskRunner() { return task_runner_.Get(); }
  WebTaskRunner* GetTaskRunner2() { return task_runner2_.Get(); }

  void TearDown() override {
    RunPendingTasks();
    task_runner_ = nullptr;
    task_runner2_ = nullptr;
  }

 protected:
  void RunPendingTasks() {
    task_runner_->RunUntilIdle();
    task_runner2_->RunUntilIdle();
  }

  void SetConnection(WebConnectionType type,
                     double max_bandwidth_mbps,
                     const Optional<TimeDelta>& http_rtt,
                     const Optional<TimeDelta>& transport_rtt,
                     const Optional<double>& downlink_throughput_mbps) {
    notifier_.SetWebConnection(type, max_bandwidth_mbps);
    notifier_.SetNetworkQuality(
        http_rtt.has_value() ? http_rtt.value()
                             : base::TimeDelta::FromMilliseconds(-1),
        transport_rtt.has_value() ? transport_rtt.value()
                                  : base::TimeDelta::FromMilliseconds(-1),
        downlink_throughput_mbps.has_value()
            ? downlink_throughput_mbps.value() * 1000
            : -1);
    RunPendingTasks();
  }
  void SetOnLine(bool on_line) {
    notifier_.SetOnLine(on_line);
    RunPendingTasks();
  }

  void AddObserverOnNotification(StateObserver* observer,
                                 StateObserver* observer_to_add) {
    observer->SetNotificationCallback(
        Bind(&NetworkStateNotifier::AddConnectionObserver,
             WTF::Unretained(&notifier_), WTF::Unretained(observer_to_add),
             WTF::Unretained(GetTaskRunner())));
  }

  void RemoveObserverOnNotification(StateObserver* observer,
                                    StateObserver* observer_to_remove) {
    observer->SetNotificationCallback(
        Bind(&NetworkStateNotifier::RemoveConnectionObserver,
             WTF::Unretained(&notifier_), WTF::Unretained(observer_to_remove),
             WTF::Unretained(GetTaskRunner())));
  }

  bool VerifyObservations(
      const StateObserver& observer,
      WebConnectionType type,
      double max_bandwidth_mbps,
      const Optional<TimeDelta>& http_rtt,
      const Optional<TimeDelta>& transport_rtt,
      const Optional<double>& downlink_throughput_mbps) const {
    EXPECT_EQ(type, observer.ObservedType());
    EXPECT_EQ(max_bandwidth_mbps, observer.ObservedMaxBandwidth());
    EXPECT_EQ(http_rtt, observer.ObservedHttpRtt());
    EXPECT_EQ(transport_rtt, observer.ObservedTransportRtt());
    EXPECT_EQ(downlink_throughput_mbps,
              observer.ObservedDownlinkThroughputMbps());

    return observer.ObservedType() == type &&
           observer.ObservedMaxBandwidth() == max_bandwidth_mbps &&
           observer.ObservedHttpRtt() == http_rtt &&
           observer.ObservedTransportRtt() == transport_rtt &&
           observer.ObservedDownlinkThroughputMbps() ==
               downlink_throughput_mbps;
  }

  RefPtr<FakeWebTaskRunner> task_runner_;
  RefPtr<FakeWebTaskRunner> task_runner2_;
  NetworkStateNotifier notifier_;
};

TEST_F(NetworkStateNotifierTest, AddObserver) {
  StateObserver observer;
  notifier_.AddConnectionObserver(&observer, GetTaskRunner());
  EXPECT_TRUE(VerifyObservations(observer, kWebConnectionTypeNone,
                                 kNoneMaxBandwidthMbps, kUnknownRtt,
                                 kUnknownRtt, kUnknownThroughputMbps));

  // Change max. bandwidth and the network quality estimates.
  SetConnection(kWebConnectionTypeBluetooth, kBluetoothMaxBandwidthMbps,
                kEthernetHttpRtt, kEthernetTransportRtt,
                kEthernetThroughputMbps);
  EXPECT_TRUE(VerifyObservations(
      observer, kWebConnectionTypeBluetooth, kBluetoothMaxBandwidthMbps,
      kEthernetHttpRtt, kEthernetTransportRtt, kEthernetThroughputMbps));
  EXPECT_EQ(observer.CallbackCount(), 2);

  // Only change the connection type.
  SetConnection(kWebConnectionTypeEthernet, kBluetoothMaxBandwidthMbps,
                kEthernetHttpRtt, kEthernetTransportRtt,
                kEthernetThroughputMbps);
  EXPECT_TRUE(VerifyObservations(
      observer, kWebConnectionTypeEthernet, kBluetoothMaxBandwidthMbps,
      kEthernetHttpRtt, kEthernetTransportRtt, kEthernetThroughputMbps));
  EXPECT_EQ(observer.CallbackCount(), 3);

  // Only change the max. bandwidth.
  SetConnection(kWebConnectionTypeEthernet, kEthernetMaxBandwidthMbps,
                kEthernetHttpRtt, kEthernetTransportRtt,
                kEthernetThroughputMbps);
  EXPECT_TRUE(VerifyObservations(
      observer, kWebConnectionTypeEthernet, kEthernetMaxBandwidthMbps,
      kEthernetHttpRtt, kEthernetTransportRtt, kEthernetThroughputMbps));
  EXPECT_EQ(observer.CallbackCount(), 4);

  // Only change the transport RTT.
  SetConnection(kWebConnectionTypeEthernet, kEthernetMaxBandwidthMbps,
                kEthernetHttpRtt, kEthernetTransportRtt.value() * 2,
                kEthernetThroughputMbps);
  EXPECT_TRUE(VerifyObservations(observer, kWebConnectionTypeEthernet,
                                 kEthernetMaxBandwidthMbps, kEthernetHttpRtt,
                                 kEthernetTransportRtt.value() * 2,
                                 kEthernetThroughputMbps));
  EXPECT_EQ(observer.CallbackCount(), 5);

  notifier_.RemoveConnectionObserver(&observer, GetTaskRunner());
}

TEST_F(NetworkStateNotifierTest, RemoveObserver) {
  StateObserver observer1, observer2;
  notifier_.AddConnectionObserver(&observer1, GetTaskRunner());
  notifier_.RemoveConnectionObserver(&observer1, GetTaskRunner());
  notifier_.AddConnectionObserver(&observer2, GetTaskRunner());

  SetConnection(kWebConnectionTypeBluetooth, kBluetoothMaxBandwidthMbps,
                kEthernetHttpRtt, kEthernetTransportRtt,
                kEthernetThroughputMbps);

  EXPECT_TRUE(VerifyObservations(observer1, kWebConnectionTypeNone,
                                 kNoneMaxBandwidthMbps, kUnknownRtt,
                                 kUnknownRtt, kUnknownThroughputMbps));
  EXPECT_TRUE(VerifyObservations(
      observer2, kWebConnectionTypeBluetooth, kBluetoothMaxBandwidthMbps,
      kEthernetHttpRtt, kEthernetTransportRtt, kEthernetThroughputMbps));
  notifier_.RemoveConnectionObserver(&observer2, GetTaskRunner());
}

TEST_F(NetworkStateNotifierTest, RemoveSoleObserver) {
  StateObserver observer1;
  notifier_.AddConnectionObserver(&observer1, GetTaskRunner());
  notifier_.RemoveConnectionObserver(&observer1, GetTaskRunner());

  SetConnection(kWebConnectionTypeBluetooth, kBluetoothMaxBandwidthMbps,
                kEthernetHttpRtt, kEthernetTransportRtt,
                kEthernetThroughputMbps);
  EXPECT_TRUE(VerifyObservations(observer1, kWebConnectionTypeNone,
                                 kNoneMaxBandwidthMbps, kUnknownRtt,
                                 kUnknownRtt, kUnknownThroughputMbps));
}

TEST_F(NetworkStateNotifierTest, AddObserverWhileNotifying) {
  StateObserver observer1, observer2;
  notifier_.AddConnectionObserver(&observer1, GetTaskRunner());
  AddObserverOnNotification(&observer1, &observer2);

  SetConnection(kWebConnectionTypeBluetooth, kBluetoothMaxBandwidthMbps,
                kUnknownRtt, kUnknownRtt, kUnknownThroughputMbps);
  EXPECT_TRUE(VerifyObservations(observer1, kWebConnectionTypeBluetooth,
                                 kBluetoothMaxBandwidthMbps, kUnknownRtt,
                                 kUnknownRtt, kUnknownThroughputMbps));
  EXPECT_TRUE(VerifyObservations(observer2, kWebConnectionTypeBluetooth,
                                 kBluetoothMaxBandwidthMbps, kUnknownRtt,
                                 kUnknownRtt, kUnknownThroughputMbps));
  notifier_.RemoveConnectionObserver(&observer1, GetTaskRunner());
  notifier_.RemoveConnectionObserver(&observer2, GetTaskRunner());
}

TEST_F(NetworkStateNotifierTest, RemoveSoleObserverWhileNotifying) {
  StateObserver observer1;
  notifier_.AddConnectionObserver(&observer1, GetTaskRunner());
  RemoveObserverOnNotification(&observer1, &observer1);

  SetConnection(kWebConnectionTypeBluetooth, kBluetoothMaxBandwidthMbps,
                kUnknownRtt, kUnknownRtt, kUnknownThroughputMbps);
  EXPECT_TRUE(VerifyObservations(observer1, kWebConnectionTypeBluetooth,
                                 kBluetoothMaxBandwidthMbps, kUnknownRtt,
                                 kUnknownRtt, kUnknownThroughputMbps));

  SetConnection(kWebConnectionTypeEthernet, kEthernetMaxBandwidthMbps,
                kUnknownRtt, kUnknownRtt, kUnknownThroughputMbps);
  EXPECT_TRUE(VerifyObservations(observer1, kWebConnectionTypeBluetooth,
                                 kBluetoothMaxBandwidthMbps, kUnknownRtt,
                                 kUnknownRtt, kUnknownThroughputMbps));
}

TEST_F(NetworkStateNotifierTest, RemoveCurrentObserverWhileNotifying) {
  StateObserver observer1, observer2;
  notifier_.AddConnectionObserver(&observer1, GetTaskRunner());
  notifier_.AddConnectionObserver(&observer2, GetTaskRunner());
  RemoveObserverOnNotification(&observer1, &observer1);

  SetConnection(kWebConnectionTypeBluetooth, kBluetoothMaxBandwidthMbps,
                kUnknownRtt, kUnknownRtt, kUnknownThroughputMbps);
  EXPECT_TRUE(VerifyObservations(observer1, kWebConnectionTypeBluetooth,
                                 kBluetoothMaxBandwidthMbps, kUnknownRtt,
                                 kUnknownRtt, kUnknownThroughputMbps));
  EXPECT_TRUE(VerifyObservations(observer2, kWebConnectionTypeBluetooth,
                                 kBluetoothMaxBandwidthMbps, kUnknownRtt,
                                 kUnknownRtt, kUnknownThroughputMbps));

  SetConnection(kWebConnectionTypeEthernet, kEthernetMaxBandwidthMbps,
                kUnknownRtt, kUnknownRtt, kUnknownThroughputMbps);
  EXPECT_TRUE(VerifyObservations(observer1, kWebConnectionTypeBluetooth,
                                 kBluetoothMaxBandwidthMbps, kUnknownRtt,
                                 kUnknownRtt, kUnknownThroughputMbps));
  EXPECT_TRUE(VerifyObservations(observer2, kWebConnectionTypeEthernet,
                                 kEthernetMaxBandwidthMbps, kUnknownRtt,
                                 kUnknownRtt, kUnknownThroughputMbps));

  notifier_.RemoveConnectionObserver(&observer1, GetTaskRunner());
  notifier_.RemoveConnectionObserver(&observer2, GetTaskRunner());
}

TEST_F(NetworkStateNotifierTest, RemovePastObserverWhileNotifying) {
  StateObserver observer1, observer2;
  notifier_.AddConnectionObserver(&observer1, GetTaskRunner());
  notifier_.AddConnectionObserver(&observer2, GetTaskRunner());
  RemoveObserverOnNotification(&observer2, &observer1);

  SetConnection(kWebConnectionTypeBluetooth, kBluetoothMaxBandwidthMbps,
                kUnknownRtt, kUnknownRtt, kUnknownThroughputMbps);
  EXPECT_EQ(observer1.ObservedType(), kWebConnectionTypeBluetooth);
  EXPECT_EQ(observer2.ObservedType(), kWebConnectionTypeBluetooth);

  SetConnection(kWebConnectionTypeEthernet, kEthernetMaxBandwidthMbps,
                kUnknownRtt, kUnknownRtt, kUnknownThroughputMbps);
  EXPECT_TRUE(VerifyObservations(observer1, kWebConnectionTypeBluetooth,
                                 kBluetoothMaxBandwidthMbps, kUnknownRtt,
                                 kUnknownRtt, kUnknownThroughputMbps));
  EXPECT_TRUE(VerifyObservations(observer2, kWebConnectionTypeEthernet,
                                 kEthernetMaxBandwidthMbps, kUnknownRtt,
                                 kUnknownRtt, kUnknownThroughputMbps));

  notifier_.RemoveConnectionObserver(&observer1, GetTaskRunner());
  notifier_.RemoveConnectionObserver(&observer2, GetTaskRunner());
}

TEST_F(NetworkStateNotifierTest, RemoveFutureObserverWhileNotifying) {
  StateObserver observer1, observer2, observer3;
  notifier_.AddConnectionObserver(&observer1, GetTaskRunner());
  notifier_.AddConnectionObserver(&observer2, GetTaskRunner());
  notifier_.AddConnectionObserver(&observer3, GetTaskRunner());
  RemoveObserverOnNotification(&observer1, &observer2);

  SetConnection(kWebConnectionTypeBluetooth, kBluetoothMaxBandwidthMbps,
                kUnknownRtt, kUnknownRtt, kUnknownThroughputMbps);
  EXPECT_TRUE(VerifyObservations(observer1, kWebConnectionTypeBluetooth,
                                 kBluetoothMaxBandwidthMbps, kUnknownRtt,
                                 kUnknownRtt, kUnknownThroughputMbps));
  EXPECT_TRUE(VerifyObservations(observer2, kWebConnectionTypeNone,
                                 kNoneMaxBandwidthMbps, kUnknownRtt,
                                 kUnknownRtt, kUnknownThroughputMbps));
  EXPECT_TRUE(VerifyObservations(observer3, kWebConnectionTypeBluetooth,
                                 kBluetoothMaxBandwidthMbps, kUnknownRtt,
                                 kUnknownRtt, kUnknownThroughputMbps));

  notifier_.RemoveConnectionObserver(&observer1, GetTaskRunner());
  notifier_.RemoveConnectionObserver(&observer2, GetTaskRunner());
  notifier_.RemoveConnectionObserver(&observer3, GetTaskRunner());
}

TEST_F(NetworkStateNotifierTest, MultipleContextsAddObserver) {
  StateObserver observer1, observer2;
  notifier_.AddConnectionObserver(&observer1, GetTaskRunner());
  notifier_.AddConnectionObserver(&observer2, GetTaskRunner2());

  SetConnection(kWebConnectionTypeBluetooth, kBluetoothMaxBandwidthMbps,
                kEthernetHttpRtt, kEthernetTransportRtt,
                kEthernetThroughputMbps);
  EXPECT_TRUE(VerifyObservations(
      observer1, kWebConnectionTypeBluetooth, kBluetoothMaxBandwidthMbps,
      kEthernetHttpRtt, kEthernetTransportRtt, kEthernetThroughputMbps));
  EXPECT_TRUE(VerifyObservations(
      observer2, kWebConnectionTypeBluetooth, kBluetoothMaxBandwidthMbps,
      kEthernetHttpRtt, kEthernetTransportRtt, kEthernetThroughputMbps));

  notifier_.RemoveConnectionObserver(&observer1, GetTaskRunner());
  notifier_.RemoveConnectionObserver(&observer2, GetTaskRunner2());
}

TEST_F(NetworkStateNotifierTest, RemoveContext) {
  StateObserver observer1, observer2;
  notifier_.AddConnectionObserver(&observer1, GetTaskRunner());
  notifier_.AddConnectionObserver(&observer2, GetTaskRunner2());
  notifier_.RemoveConnectionObserver(&observer2, GetTaskRunner2());

  SetConnection(kWebConnectionTypeBluetooth, kBluetoothMaxBandwidthMbps,
                kEthernetHttpRtt, kEthernetTransportRtt,
                kEthernetThroughputMbps);
  EXPECT_TRUE(VerifyObservations(
      observer1, kWebConnectionTypeBluetooth, kBluetoothMaxBandwidthMbps,
      kEthernetHttpRtt, kEthernetTransportRtt, kEthernetThroughputMbps));
  EXPECT_TRUE(VerifyObservations(observer2, kWebConnectionTypeNone,
                                 kNoneMaxBandwidthMbps, kUnknownRtt,
                                 kUnknownRtt, kUnknownThroughputMbps));

  notifier_.RemoveConnectionObserver(&observer1, GetTaskRunner());
}

TEST_F(NetworkStateNotifierTest, RemoveAllContexts) {
  StateObserver observer1, observer2;
  notifier_.AddConnectionObserver(&observer1, GetTaskRunner());
  notifier_.AddConnectionObserver(&observer2, GetTaskRunner2());
  notifier_.RemoveConnectionObserver(&observer1, GetTaskRunner());
  notifier_.RemoveConnectionObserver(&observer2, GetTaskRunner2());

  SetConnection(kWebConnectionTypeBluetooth, kBluetoothMaxBandwidthMbps,
                kEthernetHttpRtt, kEthernetTransportRtt,
                kEthernetThroughputMbps);
  EXPECT_TRUE(VerifyObservations(observer1, kWebConnectionTypeNone,
                                 kNoneMaxBandwidthMbps, kUnknownRtt,
                                 kUnknownRtt, kUnknownThroughputMbps));
  EXPECT_TRUE(VerifyObservations(observer2, kWebConnectionTypeNone,
                                 kNoneMaxBandwidthMbps, kUnknownRtt,
                                 kUnknownRtt, kUnknownThroughputMbps));
}

TEST_F(NetworkStateNotifierTest, SetOverride) {
  StateObserver observer;
  notifier_.AddConnectionObserver(&observer, GetTaskRunner());

  notifier_.SetOnLine(true);
  SetConnection(kWebConnectionTypeBluetooth, kBluetoothMaxBandwidthMbps,
                kUnknownRtt, kUnknownRtt, kUnknownThroughputMbps);
  EXPECT_TRUE(VerifyObservations(observer, kWebConnectionTypeBluetooth,
                                 kBluetoothMaxBandwidthMbps, kUnknownRtt,
                                 kUnknownRtt, kUnknownThroughputMbps));
  EXPECT_TRUE(notifier_.OnLine());
  EXPECT_EQ(kWebConnectionTypeBluetooth, notifier_.ConnectionType());
  EXPECT_EQ(kBluetoothMaxBandwidthMbps, notifier_.MaxBandwidth());

  notifier_.SetOverride(true, kWebConnectionTypeEthernet,
                        kEthernetMaxBandwidthMbps);
  RunPendingTasks();
  EXPECT_TRUE(VerifyObservations(observer, kWebConnectionTypeEthernet,
                                 kEthernetMaxBandwidthMbps, kUnknownRtt,
                                 kUnknownRtt, kUnknownThroughputMbps));
  EXPECT_TRUE(notifier_.OnLine());
  EXPECT_EQ(kWebConnectionTypeEthernet, notifier_.ConnectionType());
  EXPECT_EQ(kEthernetMaxBandwidthMbps, notifier_.MaxBandwidth());

  // When override is active, calls to setOnLine and setConnection are temporary
  // ignored.
  notifier_.SetOnLine(false);
  SetConnection(kWebConnectionTypeNone, kNoneMaxBandwidthMbps, kUnknownRtt,
                kUnknownRtt, kUnknownThroughputMbps);
  RunPendingTasks();
  EXPECT_TRUE(VerifyObservations(observer, kWebConnectionTypeEthernet,
                                 kEthernetMaxBandwidthMbps, kUnknownRtt,
                                 kUnknownRtt, kUnknownThroughputMbps));
  EXPECT_TRUE(notifier_.OnLine());
  EXPECT_EQ(kWebConnectionTypeEthernet, notifier_.ConnectionType());
  EXPECT_EQ(kEthernetMaxBandwidthMbps, notifier_.MaxBandwidth());

  notifier_.ClearOverride();
  RunPendingTasks();
  EXPECT_TRUE(VerifyObservations(observer, kWebConnectionTypeNone,
                                 kNoneMaxBandwidthMbps, kUnknownRtt,
                                 kUnknownRtt, kUnknownThroughputMbps));
  EXPECT_FALSE(notifier_.OnLine());
  EXPECT_EQ(kWebConnectionTypeNone, notifier_.ConnectionType());
  EXPECT_EQ(kNoneMaxBandwidthMbps, notifier_.MaxBandwidth());

  notifier_.RemoveConnectionObserver(&observer, GetTaskRunner());
}

TEST_F(NetworkStateNotifierTest, NoExtraNotifications) {
  StateObserver observer;
  notifier_.AddConnectionObserver(&observer, GetTaskRunner());

  SetConnection(kWebConnectionTypeBluetooth, kBluetoothMaxBandwidthMbps,
                kEthernetHttpRtt, kEthernetTransportRtt,
                kEthernetThroughputMbps);
  EXPECT_TRUE(VerifyObservations(
      observer, kWebConnectionTypeBluetooth, kBluetoothMaxBandwidthMbps,
      kEthernetHttpRtt, kEthernetTransportRtt, kEthernetThroughputMbps));
  EXPECT_EQ(observer.CallbackCount(), 2);

  SetConnection(kWebConnectionTypeBluetooth, kBluetoothMaxBandwidthMbps,
                kEthernetHttpRtt, kEthernetTransportRtt,
                kEthernetThroughputMbps);
  EXPECT_EQ(observer.CallbackCount(), 2);

  SetConnection(kWebConnectionTypeEthernet, kEthernetMaxBandwidthMbps,
                kEthernetHttpRtt.value() * 2, kEthernetTransportRtt.value() * 2,
                kEthernetThroughputMbps.value() * 2);
  EXPECT_TRUE(VerifyObservations(
      observer, kWebConnectionTypeEthernet, kEthernetMaxBandwidthMbps,
      kEthernetHttpRtt.value() * 2, kEthernetTransportRtt.value() * 2,
      kEthernetThroughputMbps.value() * 2));
  EXPECT_EQ(observer.CallbackCount(), 4);

  SetConnection(kWebConnectionTypeEthernet, kEthernetMaxBandwidthMbps,
                kEthernetHttpRtt.value() * 2, kEthernetTransportRtt.value() * 2,
                kEthernetThroughputMbps.value() * 2);
  EXPECT_EQ(observer.CallbackCount(), 4);

  SetConnection(kWebConnectionTypeBluetooth, kBluetoothMaxBandwidthMbps,
                kEthernetHttpRtt, kEthernetTransportRtt,
                kEthernetThroughputMbps);
  EXPECT_TRUE(VerifyObservations(
      observer, kWebConnectionTypeBluetooth, kBluetoothMaxBandwidthMbps,
      kEthernetHttpRtt, kEthernetTransportRtt, kEthernetThroughputMbps));
  EXPECT_EQ(observer.CallbackCount(), 6);

  notifier_.RemoveConnectionObserver(&observer, GetTaskRunner());
}

TEST_F(NetworkStateNotifierTest, NoNotificationOnInitialization) {
  NetworkStateNotifier notifier;
  StateObserver observer;

  notifier.AddConnectionObserver(&observer, GetTaskRunner());
  notifier.AddOnLineObserver(&observer, GetTaskRunner());
  RunPendingTasks();
  EXPECT_EQ(observer.CallbackCount(), 0);

  notifier.SetWebConnection(kWebConnectionTypeBluetooth,
                            kBluetoothMaxBandwidthMbps);
  notifier.SetOnLine(true);
  RunPendingTasks();
  EXPECT_EQ(observer.CallbackCount(), 0);

  notifier.SetOnLine(true);
  notifier.SetWebConnection(kWebConnectionTypeBluetooth,
                            kBluetoothMaxBandwidthMbps);
  RunPendingTasks();
  EXPECT_EQ(observer.CallbackCount(), 0);

  notifier.SetWebConnection(kWebConnectionTypeEthernet,
                            kEthernetMaxBandwidthMbps);
  RunPendingTasks();
  EXPECT_EQ(observer.CallbackCount(), 1);
  EXPECT_EQ(observer.ObservedType(), kWebConnectionTypeEthernet);
  EXPECT_EQ(observer.ObservedMaxBandwidth(), kEthernetMaxBandwidthMbps);

  notifier.SetOnLine(false);
  RunPendingTasks();
  EXPECT_EQ(observer.CallbackCount(), 2);
  EXPECT_FALSE(observer.ObservedOnLineState());

  notifier_.RemoveConnectionObserver(&observer, GetTaskRunner());
  notifier_.RemoveOnLineObserver(&observer, GetTaskRunner());
}

TEST_F(NetworkStateNotifierTest, OnLineNotification) {
  StateObserver observer;
  notifier_.AddOnLineObserver(&observer, GetTaskRunner());

  SetOnLine(true);
  RunPendingTasks();
  EXPECT_TRUE(observer.ObservedOnLineState());
  EXPECT_EQ(observer.CallbackCount(), 1);

  SetOnLine(false);
  RunPendingTasks();
  EXPECT_FALSE(observer.ObservedOnLineState());
  EXPECT_EQ(observer.CallbackCount(), 2);

  notifier_.RemoveConnectionObserver(&observer, GetTaskRunner());
}

TEST_F(NetworkStateNotifierTest, MultipleObservers) {
  StateObserver observer1;
  StateObserver observer2;

  // Observer1 observes online state, Observer2 observes both.
  notifier_.AddOnLineObserver(&observer1, GetTaskRunner());
  notifier_.AddConnectionObserver(&observer2, GetTaskRunner());
  notifier_.AddOnLineObserver(&observer2, GetTaskRunner());

  notifier_.SetOnLine(true);
  RunPendingTasks();
  EXPECT_TRUE(observer1.ObservedOnLineState());
  EXPECT_TRUE(observer2.ObservedOnLineState());
  EXPECT_EQ(observer1.CallbackCount(), 1);
  EXPECT_EQ(observer2.CallbackCount(), 1);

  notifier_.SetOnLine(false);
  RunPendingTasks();
  EXPECT_FALSE(observer1.ObservedOnLineState());
  EXPECT_FALSE(observer2.ObservedOnLineState());
  EXPECT_EQ(observer1.CallbackCount(), 2);
  EXPECT_EQ(observer2.CallbackCount(), 2);

  notifier_.SetOnLine(true);
  SetConnection(kWebConnectionTypeEthernet, kEthernetMaxBandwidthMbps,
                kEthernetHttpRtt, kEthernetTransportRtt,
                kEthernetThroughputMbps);

  EXPECT_TRUE(observer1.ObservedOnLineState());
  EXPECT_TRUE(observer2.ObservedOnLineState());
  EXPECT_TRUE(VerifyObservations(
      observer2, kWebConnectionTypeEthernet, kEthernetMaxBandwidthMbps,
      kEthernetHttpRtt, kEthernetTransportRtt, kEthernetThroughputMbps));
  EXPECT_EQ(observer1.CallbackCount(), 3);
  EXPECT_EQ(observer2.CallbackCount(), 5);

  notifier_.RemoveConnectionObserver(&observer1, GetTaskRunner());
  notifier_.RemoveConnectionObserver(&observer2, GetTaskRunner());
}

}  // namespace blink
