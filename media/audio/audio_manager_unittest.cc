// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/audio/audio_manager.h"

#include <memory>
#include <vector>

#include "base/bind.h"
#include "base/environment.h"
#include "base/logging.h"
#include "base/memory/ptr_util.h"
#include "base/run_loop.h"
#include "base/single_thread_task_runner.h"
#include "base/strings/string_number_conversions.h"
#include "base/synchronization/waitable_event.h"
#include "base/test/test_message_loop.h"
#include "base/threading/thread_task_runner_handle.h"
#include "build/build_config.h"
#include "media/audio/audio_device_description.h"
#include "media/audio/audio_device_info_accessor_for_tests.h"
#include "media/audio/audio_device_name.h"
#include "media/audio/audio_output_proxy.h"
#include "media/audio/audio_unittest_util.h"
#include "media/audio/fake_audio_log_factory.h"
#include "media/audio/fake_audio_manager.h"
#include "media/audio/test_audio_thread.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

#if defined(USE_ALSA)
#include "media/audio/alsa/audio_manager_alsa.h"
#endif  // defined(USE_ALSA)

#if defined(OS_WIN)
#include "base/win/scoped_com_initializer.h"
#include "media/audio/win/audio_manager_win.h"
#endif

#if defined(USE_PULSEAUDIO)
#include "media/audio/pulse/audio_manager_pulse.h"
#include "media/audio/pulse/pulse_util.h"
#endif  // defined(USE_PULSEAUDIO)

#if defined(USE_CRAS)
#include "chromeos/audio/audio_devices_pref_handler_stub.h"
#include "chromeos/audio/cras_audio_handler.h"
#include "chromeos/dbus/dbus_thread_manager.h"
#include "chromeos/dbus/fake_cras_audio_client.h"
#include "media/audio/cras/audio_manager_cras.h"
#endif  // defined(USE_CRAS)

namespace media {

namespace {

template <typename T>
struct TestAudioManagerFactory {
  static std::unique_ptr<AudioManager> Create(
      AudioLogFactory* audio_log_factory) {
    return base::MakeUnique<T>(base::MakeUnique<TestAudioThread>(),
                               audio_log_factory);
  }
};

#if defined(USE_PULSEAUDIO)
template <>
struct TestAudioManagerFactory<AudioManagerPulse> {
  static std::unique_ptr<AudioManager> Create(
      AudioLogFactory* audio_log_factory) {
    pa_threaded_mainloop* pa_mainloop = nullptr;
    pa_context* pa_context = nullptr;
    if (!pulse::InitPulse(&pa_mainloop, &pa_context))
      return nullptr;
    return base::MakeUnique<AudioManagerPulse>(
        base::MakeUnique<TestAudioThread>(), audio_log_factory, pa_mainloop,
        pa_context);
  }
};
#endif  // defined(USE_PULSEAUDIO)

template <>
struct TestAudioManagerFactory<std::nullptr_t> {
  static std::unique_ptr<AudioManager> Create(
      AudioLogFactory* audio_log_factory) {
    return AudioManager::CreateForTesting(base::MakeUnique<TestAudioThread>());
  }
};

#if defined(USE_CRAS)
using chromeos::AudioNode;
using chromeos::AudioNodeList;

const uint64_t kJabraSpeaker1Id = 30001;
const uint64_t kJabraSpeaker1StableDeviceId = 80001;
const uint64_t kJabraSpeaker2Id = 30002;
const uint64_t kJabraSpeaker2StableDeviceId = 80002;
const uint64_t kHDMIOutputId = 30003;
const uint64_t kHDMIOutputStabeDevicelId = 80003;
const uint64_t kJabraMic1Id = 40001;
const uint64_t kJabraMic1StableDeviceId = 90001;
const uint64_t kJabraMic2Id = 40002;
const uint64_t kJabraMic2StableDeviceId = 90002;
const uint64_t kWebcamMicId = 40003;
const uint64_t kWebcamMicStableDeviceId = 90003;

const AudioNode kJabraSpeaker1(false,
                               kJabraSpeaker1Id,
                               true,
                               kJabraSpeaker1StableDeviceId,
                               kJabraSpeaker1StableDeviceId ^ 0xFF,
                               "Jabra Speaker",
                               "USB",
                               "Jabra Speaker 1",
                               false,
                               0);

const AudioNode kJabraSpeaker2(false,
                               kJabraSpeaker2Id,
                               true,
                               kJabraSpeaker2StableDeviceId,
                               kJabraSpeaker2StableDeviceId ^ 0xFF,
                               "Jabra Speaker",
                               "USB",
                               "Jabra Speaker 2",
                               false,
                               0);

const AudioNode kHDMIOutput(false,
                            kHDMIOutputId,
                            true,
                            kHDMIOutputStabeDevicelId,
                            kHDMIOutputStabeDevicelId ^ 0xFF,
                            "HDMI output",
                            "HDMI",
                            "HDA Intel MID",
                            false,
                            0);

const AudioNode kJabraMic1(true,
                           kJabraMic1Id,
                           true,
                           kJabraMic1StableDeviceId,
                           kJabraMic1StableDeviceId ^ 0xFF,
                           "Jabra Mic",
                           "USB",
                           "Jabra Mic 1",
                           false,
                           0);

const AudioNode kJabraMic2(true,
                           kJabraMic2Id,
                           true,
                           kJabraMic2StableDeviceId,
                           kJabraMic2StableDeviceId ^ 0xFF,
                           "Jabra Mic",
                           "USB",
                           "Jabra Mic 2",
                           false,
                           0);

const AudioNode kUSBCameraMic(true,
                              kWebcamMicId,
                              true,
                              kWebcamMicStableDeviceId,
                              kWebcamMicStableDeviceId ^ 0xFF,
                              "Webcam Mic",
                              "USB",
                              "Logitech Webcam",
                              false,
                              0);
#endif  // defined(USE_CRAS)

}  // namespace

// Test fixture which allows us to override the default enumeration API on
// Windows.
class AudioManagerTest : public ::testing::Test {
 public:
  void HandleDefaultDeviceIDsTest() {
    AudioParameters params(AudioParameters::AUDIO_PCM_LOW_LATENCY,
                           CHANNEL_LAYOUT_STEREO, 48000, 16, 2048);

    // Create a stream with the default device id "".
    AudioOutputStream* stream =
        audio_manager_->MakeAudioOutputStreamProxy(params, "");
    ASSERT_TRUE(stream);
    AudioOutputDispatcher* dispatcher1 =
        reinterpret_cast<AudioOutputProxy*>(stream)
            ->get_dispatcher_for_testing();

    // Closing this stream will put it up for reuse.
    stream->Close();
    stream = audio_manager_->MakeAudioOutputStreamProxy(
        params, AudioDeviceDescription::kDefaultDeviceId);

    // Verify both streams are created with the same dispatcher (which is unique
    // per device).
    ASSERT_EQ(dispatcher1, reinterpret_cast<AudioOutputProxy*>(stream)
                               ->get_dispatcher_for_testing());
    stream->Close();

    // Create a non-default device and ensure it gets a different dispatcher.
    stream = audio_manager_->MakeAudioOutputStreamProxy(params, "123456");
    ASSERT_NE(dispatcher1, reinterpret_cast<AudioOutputProxy*>(stream)
                               ->get_dispatcher_for_testing());
    stream->Close();
  }

  void GetDefaultOutputStreamParameters(media::AudioParameters* params) {
    *params = device_info_accessor_->GetDefaultOutputStreamParameters();
  }

  void GetAssociatedOutputDeviceID(const std::string& input_device_id,
                                   std::string* output_device_id) {
    *output_device_id =
        device_info_accessor_->GetAssociatedOutputDeviceID(input_device_id);
  }

#if defined(USE_CRAS)
  void TearDown() override {
    chromeos::CrasAudioHandler::Shutdown();
    audio_pref_handler_ = nullptr;
    chromeos::DBusThreadManager::Shutdown();
  }

  void SetUpCrasAudioHandlerWithTestingNodes(const AudioNodeList& audio_nodes) {
    chromeos::DBusThreadManager::Initialize();
    audio_client_ = static_cast<chromeos::FakeCrasAudioClient*>(
        chromeos::DBusThreadManager::Get()->GetCrasAudioClient());
    audio_client_->SetAudioNodesForTesting(audio_nodes);
    audio_pref_handler_ = new chromeos::AudioDevicesPrefHandlerStub();
    chromeos::CrasAudioHandler::Initialize(audio_pref_handler_);
    cras_audio_handler_ = chromeos::CrasAudioHandler::Get();
    base::RunLoop().RunUntilIdle();
  }
#endif  // defined(USE_CRAS)

 protected:
  AudioManagerTest() { CreateAudioManagerForTesting(); }
  ~AudioManagerTest() override { audio_manager_->Shutdown(); }

  // Helper method which verifies that the device list starts with a valid
  // default record followed by non-default device names.
  static void CheckDeviceDescriptions(
      const AudioDeviceDescriptions& device_descriptions) {
    DVLOG(2) << "Got " << device_descriptions.size() << " audio devices.";
    if (!device_descriptions.empty()) {
      AudioDeviceDescriptions::const_iterator it = device_descriptions.begin();

      // The first device in the list should always be the default device.
      EXPECT_EQ(AudioDeviceDescription::GetDefaultDeviceName(),
                it->device_name);
      EXPECT_EQ(std::string(AudioDeviceDescription::kDefaultDeviceId),
                it->unique_id);
      ++it;

      // Other devices should have non-empty name and id and should not contain
      // default name or id.
      while (it != device_descriptions.end()) {
        EXPECT_FALSE(it->device_name.empty());
        EXPECT_FALSE(it->unique_id.empty());
        EXPECT_FALSE(it->group_id.empty());
        DVLOG(2) << "Device ID(" << it->unique_id
                 << "), label: " << it->device_name
                 << "group: " << it->group_id;
        EXPECT_NE(AudioDeviceDescription::GetDefaultDeviceName(),
                  it->device_name);
        EXPECT_NE(std::string(AudioDeviceDescription::kDefaultDeviceId),
                  it->unique_id);
        ++it;
      }
    } else {
      // Log a warning so we can see the status on the build bots.  No need to
      // break the test though since this does successfully test the code and
      // some failure cases.
      LOG(WARNING) << "No input devices detected";
    }
  }

#if defined(USE_CRAS)
  // Helper method for (USE_CRAS) which verifies that the device list starts
  // with a valid default record followed by physical device names.
  static void CheckDeviceDescriptionsCras(
      const AudioDeviceDescriptions& device_descriptions,
      const std::map<uint64_t, std::string>& expectation) {
    DVLOG(2) << "Got " << device_descriptions.size() << " audio devices.";
    if (!device_descriptions.empty()) {
      AudioDeviceDescriptions::const_iterator it = device_descriptions.begin();

      // The first device in the list should always be the default device.
      EXPECT_EQ(AudioDeviceDescription::GetDefaultDeviceName(),
                it->device_name);
      EXPECT_EQ(std::string(AudioDeviceDescription::kDefaultDeviceId),
                it->unique_id);

      // |device_descriptions|'size should be |expectation|'s size plus one
      // because of
      // default device.
      EXPECT_EQ(device_descriptions.size(), expectation.size() + 1);
      ++it;
      // Check other devices that should have non-empty name and id, and should
      // be contained in expectation.
      while (it != device_descriptions.end()) {
        EXPECT_FALSE(it->device_name.empty());
        EXPECT_FALSE(it->unique_id.empty());
        EXPECT_FALSE(it->group_id.empty());
        DVLOG(2) << "Device ID(" << it->unique_id
                 << "), label: " << it->device_name
                 << "group: " << it->group_id;
        uint64_t key;
        EXPECT_TRUE(base::StringToUint64(it->unique_id, &key));
        EXPECT_TRUE(expectation.find(key) != expectation.end());
        EXPECT_EQ(expectation.find(key)->second, it->device_name);
        ++it;
      }
    } else {
      // Log a warning so we can see the status on the build bots. No need to
      // break the test though since this does successfully test the code and
      // some failure cases.
      LOG(WARNING) << "No input devices detected";
    }
  }
#endif  // defined(USE_CRAS)

  bool InputDevicesAvailable() {
    return device_info_accessor_->HasAudioInputDevices();
  }
  bool OutputDevicesAvailable() {
    return device_info_accessor_->HasAudioOutputDevices();
  }

  template <typename T = std::nullptr_t>
  void CreateAudioManagerForTesting() {
    // Only one AudioManager may exist at a time, so destroy the one we're
    // currently holding before creating a new one.
    // Flush the message loop to run any shutdown tasks posted by AudioManager.
    if (audio_manager_) {
      audio_manager_->Shutdown();
      audio_manager_.reset();
    }

    audio_manager_ =
        TestAudioManagerFactory<T>::Create(&fake_audio_log_factory_);
    // A few AudioManager implementations post initialization tasks to
    // audio thread. Flush the thread to ensure that |audio_manager_| is
    // initialized and ready to use before returning from this function.
    // TODO(alokp): We should perhaps do this in AudioManager::Create().
    base::RunLoop().RunUntilIdle();
    device_info_accessor_ =
        base::MakeUnique<AudioDeviceInfoAccessorForTests>(audio_manager_.get());
  }

  base::TestMessageLoop message_loop_;
  FakeAudioLogFactory fake_audio_log_factory_;
  std::unique_ptr<AudioManager> audio_manager_;
  std::unique_ptr<AudioDeviceInfoAccessorForTests> device_info_accessor_;

#if defined(USE_CRAS)
  chromeos::CrasAudioHandler* cras_audio_handler_ = nullptr;  // Not owned.
  chromeos::FakeCrasAudioClient* audio_client_ = nullptr;     // Not owned.
  scoped_refptr<chromeos::AudioDevicesPrefHandlerStub> audio_pref_handler_;
#endif  // defined(USE_CRAS)
};

#if defined(USE_CRAS)
TEST_F(AudioManagerTest, EnumerateInputDevicesCras) {
  // Setup the devices without internal mic, so that it doesn't exist
  // beamforming capable mic.
  AudioNodeList audio_nodes;
  audio_nodes.push_back(kJabraMic1);
  audio_nodes.push_back(kJabraMic2);
  audio_nodes.push_back(kUSBCameraMic);
  audio_nodes.push_back(kHDMIOutput);
  audio_nodes.push_back(kJabraSpeaker1);
  SetUpCrasAudioHandlerWithTestingNodes(audio_nodes);

  ABORT_AUDIO_TEST_IF_NOT(InputDevicesAvailable());

  // Setup expectation with physical devices.
  std::map<uint64_t, std::string> expectation;
  expectation[kJabraMic1.id] =
      cras_audio_handler_->GetDeviceFromId(kJabraMic1.id)->display_name;
  expectation[kJabraMic2.id] =
      cras_audio_handler_->GetDeviceFromId(kJabraMic2.id)->display_name;
  expectation[kUSBCameraMic.id] =
      cras_audio_handler_->GetDeviceFromId(kUSBCameraMic.id)->display_name;

  DVLOG(2) << "Testing AudioManagerCras.";
  CreateAudioManagerForTesting<AudioManagerCras>();
  AudioDeviceDescriptions device_descriptions;
  device_info_accessor_->GetAudioInputDeviceDescriptions(&device_descriptions);
  CheckDeviceDescriptionsCras(device_descriptions, expectation);
}

TEST_F(AudioManagerTest, EnumerateOutputDevicesCras) {
  // Setup the devices without internal mic, so that it doesn't exist
  // beamforming capable mic.
  AudioNodeList audio_nodes;
  audio_nodes.push_back(kJabraMic1);
  audio_nodes.push_back(kJabraMic2);
  audio_nodes.push_back(kUSBCameraMic);
  audio_nodes.push_back(kHDMIOutput);
  audio_nodes.push_back(kJabraSpeaker1);
  SetUpCrasAudioHandlerWithTestingNodes(audio_nodes);

  ABORT_AUDIO_TEST_IF_NOT(OutputDevicesAvailable());

  // Setup expectation with physical devices.
  std::map<uint64_t, std::string> expectation;
  expectation[kHDMIOutput.id] =
      cras_audio_handler_->GetDeviceFromId(kHDMIOutput.id)->display_name;
  expectation[kJabraSpeaker1.id] =
      cras_audio_handler_->GetDeviceFromId(kJabraSpeaker1.id)->display_name;

  DVLOG(2) << "Testing AudioManagerCras.";
  CreateAudioManagerForTesting<AudioManagerCras>();
  AudioDeviceDescriptions device_descriptions;
  device_info_accessor_->GetAudioOutputDeviceDescriptions(&device_descriptions);
  CheckDeviceDescriptionsCras(device_descriptions, expectation);
}
#else  // !defined(USE_CRAS)

TEST_F(AudioManagerTest, HandleDefaultDeviceIDs) {
  // Use a fake manager so we can makeup device ids, this will still use the
  // AudioManagerBase code.
  CreateAudioManagerForTesting<FakeAudioManager>();
  HandleDefaultDeviceIDsTest();
  base::RunLoop().RunUntilIdle();
}

// Test that devices can be enumerated.
TEST_F(AudioManagerTest, EnumerateInputDevices) {
  ABORT_AUDIO_TEST_IF_NOT(InputDevicesAvailable());

  AudioDeviceDescriptions device_descriptions;
  device_info_accessor_->GetAudioInputDeviceDescriptions(&device_descriptions);
  CheckDeviceDescriptions(device_descriptions);
}

// Test that devices can be enumerated.
TEST_F(AudioManagerTest, EnumerateOutputDevices) {
  ABORT_AUDIO_TEST_IF_NOT(OutputDevicesAvailable());

  AudioDeviceDescriptions device_descriptions;
  device_info_accessor_->GetAudioOutputDeviceDescriptions(&device_descriptions);
  CheckDeviceDescriptions(device_descriptions);
}

// Run additional tests for Windows since enumeration can be done using
// two different APIs. MMDevice is default for Vista and higher and Wave
// is default for XP and lower.
#if defined(OS_WIN)

// Override default enumeration API and force usage of Windows MMDevice.
// This test will only run on Windows Vista and higher.
TEST_F(AudioManagerTest, EnumerateInputDevicesWinMMDevice) {
  ABORT_AUDIO_TEST_IF_NOT(InputDevicesAvailable());

  AudioDeviceDescriptions device_descriptions;
  device_info_accessor_->GetAudioInputDeviceDescriptions(&device_descriptions);
  CheckDeviceDescriptions(device_descriptions);
}

TEST_F(AudioManagerTest, EnumerateOutputDevicesWinMMDevice) {
  ABORT_AUDIO_TEST_IF_NOT(OutputDevicesAvailable());

  AudioDeviceDescriptions device_descriptions;
  device_info_accessor_->GetAudioOutputDeviceDescriptions(&device_descriptions);
  CheckDeviceDescriptions(device_descriptions);
}
#endif  // defined(OS_WIN)

#if defined(USE_PULSEAUDIO)
// On Linux, there are two implementations available and both can
// sometimes be tested on a single system. These tests specifically
// test Pulseaudio.

TEST_F(AudioManagerTest, EnumerateInputDevicesPulseaudio) {
  ABORT_AUDIO_TEST_IF_NOT(InputDevicesAvailable());

  CreateAudioManagerForTesting<AudioManagerPulse>();
  if (audio_manager_.get()) {
    AudioDeviceDescriptions device_descriptions;
    device_info_accessor_->GetAudioInputDeviceDescriptions(
        &device_descriptions);
    CheckDeviceDescriptions(device_descriptions);
  } else {
    LOG(WARNING) << "No pulseaudio on this system.";
  }
}

TEST_F(AudioManagerTest, EnumerateOutputDevicesPulseaudio) {
  ABORT_AUDIO_TEST_IF_NOT(OutputDevicesAvailable());

  CreateAudioManagerForTesting<AudioManagerPulse>();
  if (audio_manager_.get()) {
    AudioDeviceDescriptions device_descriptions;
    device_info_accessor_->GetAudioOutputDeviceDescriptions(
        &device_descriptions);
    CheckDeviceDescriptions(device_descriptions);
  } else {
    LOG(WARNING) << "No pulseaudio on this system.";
  }
}
#endif  // defined(USE_PULSEAUDIO)

#if defined(USE_ALSA)
// On Linux, there are two implementations available and both can
// sometimes be tested on a single system. These tests specifically
// test Alsa.

TEST_F(AudioManagerTest, EnumerateInputDevicesAlsa) {
  ABORT_AUDIO_TEST_IF_NOT(InputDevicesAvailable());

  DVLOG(2) << "Testing AudioManagerAlsa.";
  CreateAudioManagerForTesting<AudioManagerAlsa>();
  AudioDeviceDescriptions device_descriptions;
  device_info_accessor_->GetAudioInputDeviceDescriptions(&device_descriptions);
  CheckDeviceDescriptions(device_descriptions);
}

TEST_F(AudioManagerTest, EnumerateOutputDevicesAlsa) {
  ABORT_AUDIO_TEST_IF_NOT(OutputDevicesAvailable());

  DVLOG(2) << "Testing AudioManagerAlsa.";
  CreateAudioManagerForTesting<AudioManagerAlsa>();
  AudioDeviceDescriptions device_descriptions;
  device_info_accessor_->GetAudioOutputDeviceDescriptions(&device_descriptions);
  CheckDeviceDescriptions(device_descriptions);
}
#endif  // defined(USE_ALSA)

TEST_F(AudioManagerTest, GetDefaultOutputStreamParameters) {
#if defined(OS_WIN) || defined(OS_MACOSX)
  ABORT_AUDIO_TEST_IF_NOT(InputDevicesAvailable());

  AudioParameters params;
  GetDefaultOutputStreamParameters(&params);
  EXPECT_TRUE(params.IsValid());
#endif  // defined(OS_WIN) || defined(OS_MACOSX)
}

TEST_F(AudioManagerTest, GetAssociatedOutputDeviceID) {
#if defined(OS_WIN) || defined(OS_MACOSX)
  ABORT_AUDIO_TEST_IF_NOT(InputDevicesAvailable() && OutputDevicesAvailable());

  AudioDeviceDescriptions device_descriptions;
  device_info_accessor_->GetAudioInputDeviceDescriptions(&device_descriptions);
  bool found_an_associated_device = false;
  for (const auto& description : device_descriptions) {
    EXPECT_FALSE(description.unique_id.empty());
    EXPECT_FALSE(description.device_name.empty());
    EXPECT_FALSE(description.group_id.empty());
    std::string output_device_id;
    GetAssociatedOutputDeviceID(description.unique_id, &output_device_id);
    if (!output_device_id.empty()) {
      DVLOG(2) << description.unique_id << " matches with " << output_device_id;
      found_an_associated_device = true;
    }
  }

  EXPECT_TRUE(found_an_associated_device);
#endif  // defined(OS_WIN) || defined(OS_MACOSX)
}
#endif  // defined(USE_CRAS)

// Mock class to verify enable and disable debug recording calls.
class MockAudioDebugRecordingManager : public AudioDebugRecordingManager {
 public:
  MockAudioDebugRecordingManager(
      scoped_refptr<base::SingleThreadTaskRunner> task_runner)
      : AudioDebugRecordingManager(std::move(task_runner)) {}

  ~MockAudioDebugRecordingManager() override {}

  MOCK_METHOD1(EnableDebugRecording, void(const base::FilePath&));
  MOCK_METHOD0(DisableDebugRecording, void());

 private:
  DISALLOW_COPY_AND_ASSIGN(MockAudioDebugRecordingManager);
};

class TestAudioManager : public FakeAudioManager {
  // For testing the default implementation of GetGroupId(Input|Output)
  // input$i is associated to output$i, if both exist.
  // Default input is input1.
  // Default output is output2.
 public:
  TestAudioManager(std::unique_ptr<AudioThread> audio_thread,
                   AudioLogFactory* audio_log_factory)
      : FakeAudioManager(std::move(audio_thread), audio_log_factory) {}

  std::string GetDefaultOutputDeviceID() override { return "output4"; }

  std::string GetAssociatedOutputDeviceID(
      const std::string& input_id) override {
    if (input_id == "input1")
      return "output1";
    if (input_id == "input2")
      return "output2";
    if (input_id == "default")
      return "output1";
    return "";
  }

 private:
  void GetAudioInputDeviceNames(AudioDeviceNames* device_names) override {
    device_names->emplace_back("Input 1", "input1");
    device_names->emplace_back("Input 2", "input2");
    device_names->emplace_back("Input 3", "input3");
    device_names->push_front(AudioDeviceName::CreateDefault());
  }

  void GetAudioOutputDeviceNames(AudioDeviceNames* device_names) override {
    device_names->emplace_back("Output 1", "output1");
    device_names->emplace_back("Output 2", "output2");
    device_names->emplace_back("Output 3", "output3");
    device_names->emplace_back("Output 4", "output4");
    device_names->push_front(AudioDeviceName::CreateDefault());
  }

  std::unique_ptr<AudioDebugRecordingManager> CreateAudioDebugRecordingManager(
      scoped_refptr<base::SingleThreadTaskRunner> task_runner) override {
    return base::MakeUnique<MockAudioDebugRecordingManager>(
        std::move(task_runner));
  }
};

TEST_F(AudioManagerTest, GroupId) {
  CreateAudioManagerForTesting<TestAudioManager>();
  // Groups:
  // input1, output1, default input
  // input2, output2
  // input3,
  // output3
  // output4, default output
  AudioDeviceDescriptions inputs;
  device_info_accessor_->GetAudioInputDeviceDescriptions(&inputs);
  AudioDeviceDescriptions outputs;
  device_info_accessor_->GetAudioOutputDeviceDescriptions(&outputs);
  EXPECT_EQ(inputs[0].group_id, outputs[1].group_id);
  EXPECT_EQ(inputs[1].group_id, outputs[1].group_id);
  EXPECT_EQ(inputs[2].group_id, outputs[2].group_id);
  EXPECT_NE(inputs[3].group_id, outputs[3].group_id);
  EXPECT_EQ(outputs[4].group_id, outputs[0].group_id);
  EXPECT_NE(inputs[0].group_id, outputs[0].group_id);
  EXPECT_NE(inputs[1].group_id, outputs[2].group_id);
  EXPECT_NE(inputs[2].group_id, outputs[3].group_id);
  EXPECT_NE(inputs[1].group_id, outputs[3].group_id);
}

TEST_F(AudioManagerTest, AudioDebugRecording) {
  CreateAudioManagerForTesting<TestAudioManager>();

  AudioManagerBase* audio_manager_base =
      static_cast<AudioManagerBase*>(audio_manager_.get());

  // Initialize is normally done in AudioManager::Create(), but since we don't
  // use that in this test, we need to initialize here.
  audio_manager_->InitializeOutputDebugRecording();

  MockAudioDebugRecordingManager* mock_debug_recording_manager =
      static_cast<MockAudioDebugRecordingManager*>(
          audio_manager_base->debug_recording_manager_.get());
  ASSERT_TRUE(mock_debug_recording_manager);

  EXPECT_CALL(*mock_debug_recording_manager, DisableDebugRecording());
  audio_manager_->DisableOutputDebugRecording();

  base::FilePath file_path(FILE_PATH_LITERAL("path"));
  EXPECT_CALL(*mock_debug_recording_manager, EnableDebugRecording(file_path));
  audio_manager_->EnableOutputDebugRecording(file_path);

  EXPECT_CALL(*mock_debug_recording_manager, DisableDebugRecording());
  audio_manager_->DisableOutputDebugRecording();
}

}  // namespace media
