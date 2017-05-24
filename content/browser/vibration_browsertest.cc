// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>

#include "base/bind.h"
#include "base/macros.h"
#include "base/run_loop.h"
#include "base/strings/string_number_conversions.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/content_browser_test.h"
#include "content/public/test/content_browser_test_utils.h"
#include "content/shell/browser/shell.h"
#include "mojo/public/cpp/bindings/binding.h"
#include "services/device/public/interfaces/constants.mojom.h"
#include "services/device/public/interfaces/vibration_manager.mojom.h"
#include "services/service_manager/public/cpp/service_context.h"

namespace content {

namespace {

class VibrationTest : public ContentBrowserTest,
                      public device::mojom::VibrationManager {
 public:
  VibrationTest() : binding_(this){};

  void SetUpOnMainThread() override {
    // Because Device Service also runs in this process(browser process), here
    // we can directly set our binder to intercept interface requests against
    // it.
    service_manager::ServiceContext::SetGlobalBinderForTesting(
        device::mojom::kServiceName, device::mojom::VibrationManager::Name_,
        base::Bind(&VibrationTest::BindVibrationManager,
                   base::Unretained(this)));
  }

  void BindVibrationManager(const service_manager::BindSourceInfo& source_info,
                            const std::string& interface_name,
                            mojo::ScopedMessagePipeHandle handle) {
    binding_.Bind(device::mojom::VibrationManagerRequest(std::move(handle)));
  }

 protected:
  bool TriggerVibrate(int duration, base::Closure vibrate_done) {
    vibrate_done_ = vibrate_done;

    bool result;
    RenderFrameHost* frame = shell()->web_contents()->GetMainFrame();
    std::string script = "domAutomationController.send(navigator.vibrate(" +
                         base::IntToString(duration) + "))";
    EXPECT_TRUE(ExecuteScriptAndExtractBool(frame, script, &result));
    return result;
  }

  int64_t vibrate_milliseconds() { return vibrate_milliseconds_; }

 private:
  // device::mojom::VibrationManager:
  void Vibrate(int64_t milliseconds, const VibrateCallback& callback) override {
    vibrate_milliseconds_ = milliseconds;
    callback.Run();
    vibrate_done_.Run();
  }
  void Cancel(const CancelCallback& callback) override { callback.Run(); }

  int64_t vibrate_milliseconds_ = -1;
  base::Closure vibrate_done_;
  mojo::Binding<device::mojom::VibrationManager> binding_;

  DISALLOW_COPY_AND_ASSIGN(VibrationTest);
};

IN_PROC_BROWSER_TEST_F(VibrationTest, Vibrate) {
  ASSERT_EQ(-1, vibrate_milliseconds());

  ASSERT_TRUE(NavigateToURL(shell(), GetTestUrl(".", "simple_page.html")));
  base::RunLoop run_loop;
  ASSERT_TRUE(TriggerVibrate(1234, run_loop.QuitClosure()));
  run_loop.Run();

  ASSERT_EQ(1234, vibrate_milliseconds());
}

}  //  namespace

}  //  namespace content
