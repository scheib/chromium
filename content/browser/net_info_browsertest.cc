// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <map>
#include <string>

#include "base/command_line.h"
#include "base/run_loop.h"
#include "base/strings/string_number_conversions.h"
#include "base/test/histogram_tester.h"
#include "build/build_config.h"
#include "content/browser/net/network_quality_observer_impl.h"
#include "content/public/common/content_switches.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/content_browser_test.h"
#include "content/public/test/content_browser_test_utils.h"
#include "content/shell/browser/shell.h"
#include "net/base/network_change_notifier.h"
#include "net/base/network_change_notifier_factory.h"
#include "net/log/test_net_log.h"
#include "net/nqe/network_quality_estimator_test_util.h"

namespace content {

class NetInfoBrowserTest : public content::ContentBrowserTest {
 protected:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    // TODO(jkarlin): Once NetInfo is enabled on all platforms remove this
    // switch.
    command_line->AppendSwitch(switches::kEnableNetworkInformation);

    // TODO(jkarlin): Remove this once downlinkMax is no longer
    // experimental.
    command_line->AppendSwitch(
        switches::kEnableExperimentalWebPlatformFeatures);
  }

  void SetUp() override {
    net::NetworkChangeNotifier::SetTestNotificationsOnly(true);

#if defined(OS_CHROMEOS)
    // ChromeOS's NetworkChangeNotifier isn't known to content and therefore
    // doesn't get created in content_browsertests. Insert a mock
    // NetworkChangeNotifier.
    net::NetworkChangeNotifier::CreateMock();
#endif

    content::ContentBrowserTest::SetUp();
  }

  void SetUpOnMainThread() override {
    base::RunLoop().RunUntilIdle();
  }

  static void SetConnectionType(
      net::NetworkChangeNotifier::ConnectionType type,
      net::NetworkChangeNotifier::ConnectionSubtype subtype) {
    net::NetworkChangeNotifier::NotifyObserversOfMaxBandwidthChangeForTests(
        net::NetworkChangeNotifier::GetMaxBandwidthForConnectionSubtype(
            subtype),
        type);
    base::RunLoop().RunUntilIdle();
  }

  std::string RunScriptExtractString(const std::string& script) {
    std::string data;
    EXPECT_TRUE(ExecuteScriptAndExtractString(shell(), script, &data));
    return data;
  }

  bool RunScriptExtractBool(const std::string& script) {
    bool data;
    EXPECT_TRUE(ExecuteScriptAndExtractBool(shell(), script, &data));
    return data;
  }

  double RunScriptExtractDouble(const std::string& script) {
    double data = 0.0;
    EXPECT_TRUE(base::StringToDouble(RunScriptExtractString(script), &data));
    return data;
  }
};

// Make sure the type is correct when the page is first opened.
IN_PROC_BROWSER_TEST_F(NetInfoBrowserTest, VerifyNetworkStateInitialized) {
  SetConnectionType(net::NetworkChangeNotifier::CONNECTION_ETHERNET,
                    net::NetworkChangeNotifier::SUBTYPE_GIGABIT_ETHERNET);
  NavigateToURL(shell(), content::GetTestUrl("", "net_info.html"));
  EXPECT_TRUE(RunScriptExtractBool("getOnLine()"));
  EXPECT_EQ("ethernet", RunScriptExtractString("getType()"));
  EXPECT_EQ(net::NetworkChangeNotifier::GetMaxBandwidthForConnectionSubtype(
                net::NetworkChangeNotifier::SUBTYPE_GIGABIT_ETHERNET),
            RunScriptExtractDouble("getDownlinkMax()"));
}

// Make sure that type changes in the browser make their way to
// navigator.connection.type.
IN_PROC_BROWSER_TEST_F(NetInfoBrowserTest, NetworkChangePlumbsToNavigator) {
  NavigateToURL(shell(), content::GetTestUrl("", "net_info.html"));
  SetConnectionType(net::NetworkChangeNotifier::CONNECTION_WIFI,
                    net::NetworkChangeNotifier::SUBTYPE_WIFI_N);
  EXPECT_EQ("wifi", RunScriptExtractString("getType()"));
  EXPECT_EQ(net::NetworkChangeNotifier::GetMaxBandwidthForConnectionSubtype(
                net::NetworkChangeNotifier::SUBTYPE_WIFI_N),
            RunScriptExtractDouble("getDownlinkMax()"));

  SetConnectionType(net::NetworkChangeNotifier::CONNECTION_ETHERNET,
                    net::NetworkChangeNotifier::SUBTYPE_GIGABIT_ETHERNET);
  EXPECT_EQ("ethernet", RunScriptExtractString("getType()"));
  EXPECT_EQ(net::NetworkChangeNotifier::GetMaxBandwidthForConnectionSubtype(
                net::NetworkChangeNotifier::SUBTYPE_GIGABIT_ETHERNET),
            RunScriptExtractDouble("getDownlinkMax()"));
}

// Make sure that type changes in the browser make their way to
// navigator.isOnline.
IN_PROC_BROWSER_TEST_F(NetInfoBrowserTest, IsOnline) {
  NavigateToURL(shell(), content::GetTestUrl("", "net_info.html"));
  SetConnectionType(net::NetworkChangeNotifier::CONNECTION_ETHERNET,
                    net::NetworkChangeNotifier::SUBTYPE_GIGABIT_ETHERNET);
  EXPECT_TRUE(RunScriptExtractBool("getOnLine()"));
  SetConnectionType(net::NetworkChangeNotifier::CONNECTION_NONE,
                    net::NetworkChangeNotifier::SUBTYPE_NONE);
  EXPECT_FALSE(RunScriptExtractBool("getOnLine()"));
  SetConnectionType(net::NetworkChangeNotifier::CONNECTION_WIFI,
                    net::NetworkChangeNotifier::SUBTYPE_WIFI_N);
  EXPECT_TRUE(RunScriptExtractBool("getOnLine()"));
}

// Creating a new render view shouldn't reinitialize Blink's
// NetworkStateNotifier. See https://crbug.com/535081.
IN_PROC_BROWSER_TEST_F(NetInfoBrowserTest, TwoRenderViewsInOneProcess) {
  SetConnectionType(net::NetworkChangeNotifier::CONNECTION_ETHERNET,
                    net::NetworkChangeNotifier::SUBTYPE_GIGABIT_ETHERNET);
  NavigateToURL(shell(), content::GetTestUrl("", "net_info.html"));
  EXPECT_TRUE(RunScriptExtractBool("getOnLine()"));

  SetConnectionType(net::NetworkChangeNotifier::CONNECTION_NONE,
                    net::NetworkChangeNotifier::SUBTYPE_NONE);
  EXPECT_FALSE(RunScriptExtractBool("getOnLine()"));

  // Open the same page in a new window on the same process.
  EXPECT_TRUE(ExecuteScript(shell(), "window.open(\"net_info.html\")"));

  // The network state should not have reinitialized to what it was when opening
  // the first window (online).
  EXPECT_FALSE(RunScriptExtractBool("getOnLine()"));
}

// Verify that when the network quality notifications are not sent, the
// Javascript API returns invalid estimate.
IN_PROC_BROWSER_TEST_F(NetInfoBrowserTest,
                       NetworkQualityEstimatorNotInitialized) {
  base::HistogramTester histogram_tester;
  net::TestNetworkQualityEstimator estimator(
      nullptr, std::map<std::string, std::string>(), false, false, true, true,
      base::MakeUnique<net::BoundTestNetLog>());
  NetworkQualityObserverImpl impl(&estimator);

  EXPECT_TRUE(embedded_test_server()->Start());
  EXPECT_TRUE(
      NavigateToURL(shell(), embedded_test_server()->GetURL("/net_info.html")));

  EXPECT_EQ(0, RunScriptExtractDouble("getRtt()"));
  EXPECT_EQ(std::numeric_limits<double>::infinity(),
            RunScriptExtractDouble("getDownlink()"));
}

// Make sure the changes in the network quality are notified to the render
// thread, and the changed network quality is accessible via Javascript API.
IN_PROC_BROWSER_TEST_F(NetInfoBrowserTest, NetworkQualityChangeNotified) {
  base::HistogramTester histogram_tester;
  net::TestNetworkQualityEstimator estimator(
      nullptr, std::map<std::string, std::string>(), false, false, true, true,
      base::MakeUnique<net::BoundTestNetLog>());
  NetworkQualityObserverImpl impl(&estimator);

  net::nqe::internal::NetworkQuality network_quality_1(
      base::TimeDelta::FromSeconds(1), base::TimeDelta::FromSeconds(2), 300);
  estimator.NotifyObserversOfRTTOrThroughputEstimatesComputed(
      network_quality_1);

  EXPECT_TRUE(embedded_test_server()->Start());
  EXPECT_TRUE(
      NavigateToURL(shell(), embedded_test_server()->GetURL("/net_info.html")));

  FetchHistogramsFromChildProcesses();
  EXPECT_FALSE(
      histogram_tester.GetAllSamples("NQE.RenderThreadNotified").empty());

  EXPECT_EQ(network_quality_1.transport_rtt().InMilliseconds(),
            RunScriptExtractDouble("getRtt()"));
  EXPECT_EQ(
      static_cast<double>(network_quality_1.downstream_throughput_kbps()) /
          1000,
      RunScriptExtractDouble("getDownlink()"));

  // Verify that the network quality change is accessible via Javascript API.
  net::nqe::internal::NetworkQuality network_quality_2(
      base::TimeDelta::FromSeconds(10), base::TimeDelta::FromSeconds(20), 3000);
  estimator.NotifyObserversOfRTTOrThroughputEstimatesComputed(
      network_quality_2);
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(network_quality_2.transport_rtt().InMilliseconds(),
            RunScriptExtractDouble("getRtt()"));
  EXPECT_EQ(
      static_cast<double>(network_quality_2.downstream_throughput_kbps()) /
          1000,
      RunScriptExtractDouble("getDownlink()"));
}

// Make sure the changes in the network quality are rounded to the nearest
// 25 milliseconds or 25 kbps.
IN_PROC_BROWSER_TEST_F(NetInfoBrowserTest, NetworkQualityChangeRounded) {
  base::HistogramTester histogram_tester;
  net::TestNetworkQualityEstimator estimator(
      std::unique_ptr<net::ExternalEstimateProvider>(),
      std::map<std::string, std::string>(), false, false, true, true,
      base::MakeUnique<net::BoundTestNetLog>());
  NetworkQualityObserverImpl impl(&estimator);

  // Verify that the network quality is rounded properly.
  net::nqe::internal::NetworkQuality network_quality_1(
      base::TimeDelta::FromMilliseconds(123),
      base::TimeDelta::FromMilliseconds(212), 303);
  estimator.NotifyObserversOfRTTOrThroughputEstimatesComputed(
      network_quality_1);

  EXPECT_TRUE(embedded_test_server()->Start());
  EXPECT_TRUE(
      NavigateToURL(shell(), embedded_test_server()->GetURL("/net_info.html")));
  EXPECT_EQ(200, RunScriptExtractDouble("getRtt()"));
  EXPECT_EQ(0.300, RunScriptExtractDouble("getDownlink()"));

  net::nqe::internal::NetworkQuality network_quality_2(
      base::TimeDelta::FromMilliseconds(123),
      base::TimeDelta::FromMilliseconds(1217), 1317);
  estimator.NotifyObserversOfRTTOrThroughputEstimatesComputed(
      network_quality_2);
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(1225, RunScriptExtractDouble("getRtt()"));
  EXPECT_EQ(1.325, RunScriptExtractDouble("getDownlink()"));

  net::nqe::internal::NetworkQuality network_quality_3(
      base::TimeDelta::FromMilliseconds(12),
      base::TimeDelta::FromMilliseconds(12), 12);
  estimator.NotifyObserversOfRTTOrThroughputEstimatesComputed(
      network_quality_3);
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(0, RunScriptExtractDouble("getRtt()"));
  EXPECT_EQ(0, RunScriptExtractDouble("getDownlink()"));
}

// Make sure the minor changes (<10%) in the network quality are not notified.
IN_PROC_BROWSER_TEST_F(NetInfoBrowserTest, NetworkQualityChangeNotNotified) {
  base::HistogramTester histogram_tester;
  net::TestNetworkQualityEstimator estimator(
      nullptr, std::map<std::string, std::string>(), false, false, true, true,
      base::MakeUnique<net::BoundTestNetLog>());
  NetworkQualityObserverImpl impl(&estimator);

  // Verify that the network quality is rounded properly.
  net::nqe::internal::NetworkQuality network_quality_1(
      base::TimeDelta::FromMilliseconds(1123),
      base::TimeDelta::FromMilliseconds(1212), 1303);
  estimator.NotifyObserversOfRTTOrThroughputEstimatesComputed(
      network_quality_1);

  EXPECT_TRUE(embedded_test_server()->Start());
  EXPECT_TRUE(
      NavigateToURL(shell(), embedded_test_server()->GetURL("/net_info.html")));
  EXPECT_EQ(1200, RunScriptExtractDouble("getRtt()"));
  EXPECT_EQ(1.300, RunScriptExtractDouble("getDownlink()"));

  // All the 3 metrics change by less than 10%. So, the observers are not
  // notified.
  net::nqe::internal::NetworkQuality network_quality_2(
      base::TimeDelta::FromMilliseconds(1223),
      base::TimeDelta::FromMilliseconds(1312), 1403);
  estimator.NotifyObserversOfRTTOrThroughputEstimatesComputed(
      network_quality_2);
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(1200, RunScriptExtractDouble("getRtt()"));
  EXPECT_EQ(1.300, RunScriptExtractDouble("getDownlink()"));

  // Transport RTT has changed by more than 10% from the last notified value of
  // |network_quality_1|. The observers should be notified.
  net::nqe::internal::NetworkQuality network_quality_3(
      base::TimeDelta::FromMilliseconds(1223),
      base::TimeDelta::FromMilliseconds(2312), 1403);
  estimator.NotifyObserversOfRTTOrThroughputEstimatesComputed(
      network_quality_3);
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(2300, RunScriptExtractDouble("getRtt()"));
  EXPECT_EQ(1.400, RunScriptExtractDouble("getDownlink()"));
}

}  // namespace content
