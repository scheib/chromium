// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/media/media_internals.h"

#include <stddef.h>

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/json/json_reader.h"
#include "base/run_loop.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/histogram_tester.h"
#include "base/test/test_message_loop.h"
#include "base/threading/thread_task_runner_handle.h"
#include "build/build_config.h"
#include "content/public/test/test_browser_thread_bundle.h"
#include "media/base/audio_parameters.h"
#include "media/base/channel_layout.h"
#include "media/base/media_log.h"
#include "media/blink/watch_time_reporter.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/gfx/geometry/size.h"

namespace {
const int kTestComponentID = 0;
const char kTestDeviceID[] = "test-device-id";

// This class encapsulates a MediaInternals reference. It also has some useful
// methods to receive a callback, deserialize its associated data and expect
// integer/string values.
class MediaInternalsTestBase {
 public:
  MediaInternalsTestBase()
      : media_internals_(content::MediaInternals::GetInstance()) {}
  virtual ~MediaInternalsTestBase() {}

 protected:
  // Extracts and deserializes the JSON update data; merges into |update_data_|.
  void UpdateCallbackImpl(const base::string16& update) {
    // Each update string looks like "<JavaScript Function Name>({<JSON>});"
    // or for video capabilities: "<JavaScript Function Name>([{<JSON>}]);".
    // In the second case we will be able to extract the dictionary if it is the
    // only member of the list.
    // To use the JSON reader we need to strip out the JS function name and ().
    std::string utf8_update = base::UTF16ToUTF8(update);
    const std::string::size_type first_brace = utf8_update.find('{');
    const std::string::size_type last_brace = utf8_update.rfind('}');
    std::unique_ptr<base::Value> output_value = base::JSONReader::Read(
        utf8_update.substr(first_brace, last_brace - first_brace + 1));
    CHECK(output_value);

    base::DictionaryValue* output_dict = NULL;
    CHECK(output_value->GetAsDictionary(&output_dict));
    update_data_.MergeDictionary(output_dict);
  }

  void ExpectInt(const std::string& key, int expected_value) const {
    int actual_value = 0;
    ASSERT_TRUE(update_data_.GetInteger(key, &actual_value));
    EXPECT_EQ(expected_value, actual_value);
  }

  void ExpectString(const std::string& key,
                    const std::string& expected_value) const {
    std::string actual_value;
    ASSERT_TRUE(update_data_.GetString(key, &actual_value));
    EXPECT_EQ(expected_value, actual_value);
  }

  void ExpectStatus(const std::string& expected_value) const {
    ExpectString("status", expected_value);
  }

  void ExpectListOfStrings(const std::string& key,
                           const base::ListValue& expected_list) const {
    const base::ListValue* actual_list;
    ASSERT_TRUE(update_data_.GetList(key, &actual_list));
    const size_t expected_size = expected_list.GetSize();
    const size_t actual_size = actual_list->GetSize();
    ASSERT_EQ(expected_size, actual_size);
    for (size_t i = 0; i < expected_size; ++i) {
      std::string expected_value, actual_value;
      ASSERT_TRUE(expected_list.GetString(i, &expected_value));
      ASSERT_TRUE(actual_list->GetString(i, &actual_value));
      EXPECT_EQ(expected_value, actual_value);
    }
  }

  const content::TestBrowserThreadBundle thread_bundle_;
  base::DictionaryValue update_data_;
  content::MediaInternals* const media_internals_;
};

}  // namespace

namespace content {

class MediaInternalsVideoCaptureDeviceTest : public testing::Test,
                                             public MediaInternalsTestBase {
 public:
  MediaInternalsVideoCaptureDeviceTest()
      : update_cb_(base::Bind(
            &MediaInternalsVideoCaptureDeviceTest::UpdateCallbackImpl,
            base::Unretained(this))) {
    media_internals_->AddUpdateCallback(update_cb_);
  }

  ~MediaInternalsVideoCaptureDeviceTest() override {
    media_internals_->RemoveUpdateCallback(update_cb_);
  }

 protected:
  MediaInternals::UpdateCallback update_cb_;
};

// TODO(chfremer): Consider removing this. This test seem be
// a duplicate implementation of the functionality under test.
// https://crbug.com/630694
#if defined(OS_WIN) || defined(OS_MACOSX) || defined(OS_LINUX) || \
    defined(OS_ANDROID)
TEST_F(MediaInternalsVideoCaptureDeviceTest,
       AllCaptureApiTypesHaveProperStringRepresentation) {
  using VideoCaptureApi = media::VideoCaptureApi;
  std::map<VideoCaptureApi, std::string> api_to_string_map;
  api_to_string_map[VideoCaptureApi::LINUX_V4L2_SINGLE_PLANE] = "V4L2 SPLANE";
  api_to_string_map[VideoCaptureApi::WIN_MEDIA_FOUNDATION] = "Media Foundation";
  api_to_string_map[VideoCaptureApi::WIN_DIRECT_SHOW] = "Direct Show";
  api_to_string_map[VideoCaptureApi::MACOSX_AVFOUNDATION] = "AV Foundation";
  api_to_string_map[VideoCaptureApi::MACOSX_DECKLINK] = "DeckLink";
  api_to_string_map[VideoCaptureApi::MACOSX_SYPHON] = "Syphon";
  api_to_string_map[VideoCaptureApi::ANDROID_API1] = "Camera API1";
  api_to_string_map[VideoCaptureApi::ANDROID_API2_LEGACY] =
      "Camera API2 Legacy";
  api_to_string_map[VideoCaptureApi::ANDROID_API2_FULL] = "Camera API2 Full";
  api_to_string_map[VideoCaptureApi::ANDROID_API2_LIMITED] =
      "Camera API2 Limited";
  api_to_string_map[VideoCaptureApi::ANDROID_TANGO] = "Tango API";
  EXPECT_EQ(static_cast<size_t>(VideoCaptureApi::UNKNOWN),
            api_to_string_map.size());
  for (const auto& map_entry : api_to_string_map) {
    media::VideoCaptureDeviceDescriptor descriptor;
    descriptor.capture_api = map_entry.first;
    EXPECT_EQ(map_entry.second, descriptor.GetCaptureApiTypeString());
  }
}
#endif

TEST_F(MediaInternalsVideoCaptureDeviceTest,
       VideoCaptureFormatStringIsInExpectedFormat) {
  // Since media internals will send video capture capabilities to JavaScript in
  // an expected format and there are no public methods for accessing the
  // resolutions, frame rates or pixel formats, this test checks that the format
  // has not changed. If the test fails because of the changed format, it should
  // be updated at the same time as the media internals JS files.
  const float kFrameRate = 30.0f;
  const gfx::Size kFrameSize(1280, 720);
  const media::VideoPixelFormat kPixelFormat =
      media::PIXEL_FORMAT_I420;
  const media::VideoPixelStorage kPixelStorage = media::PIXEL_STORAGE_CPU;
  const media::VideoCaptureFormat capture_format(kFrameSize, kFrameRate,
                                                 kPixelFormat, kPixelStorage);
  const std::string expected_string = base::StringPrintf(
      "(%s)@%.3ffps, pixel format: %s, storage: %s",
      kFrameSize.ToString().c_str(), kFrameRate,
      media::VideoPixelFormatToString(kPixelFormat).c_str(),
      media::VideoCaptureFormat::PixelStorageToString(kPixelStorage).c_str());
  EXPECT_EQ(expected_string,
            media::VideoCaptureFormat::ToString(capture_format));
}

TEST_F(MediaInternalsVideoCaptureDeviceTest,
       NotifyVideoCaptureDeviceCapabilitiesEnumerated) {
  const int kWidth = 1280;
  const int kHeight = 720;
  const float kFrameRate = 30.0f;
  const media::VideoPixelFormat kPixelFormat =
      media::PIXEL_FORMAT_I420;
  const media::VideoCaptureFormat format_hd({kWidth, kHeight},
      kFrameRate, kPixelFormat);
  media::VideoCaptureFormats formats{};
  formats.push_back(format_hd);
  media::VideoCaptureDeviceDescriptor descriptor;
  descriptor.device_id = "dummy";
  descriptor.display_name = "dummy";
#if defined(OS_MACOSX)
  descriptor.capture_api = media::VideoCaptureApi::MACOSX_AVFOUNDATION;
#elif defined(OS_WIN)
  descriptor.capture_api = media::VideoCaptureApi::WIN_DIRECT_SHOW;
#elif defined(OS_LINUX)
  descriptor.device_id = "/dev/dummy";
  descriptor.capture_api = media::VideoCaptureApi::LINUX_V4L2_SINGLE_PLANE;
#elif defined(OS_ANDROID)
  descriptor.capture_api = media::VideoCaptureApi::ANDROID_API2_LEGACY;
#endif
  std::vector<std::tuple<media::VideoCaptureDeviceDescriptor,
                         media::VideoCaptureFormats>>
      descriptors_and_formats{};
  descriptors_and_formats.push_back(std::make_tuple(descriptor, formats));

  // When updating video capture capabilities, the update will serialize
  // a JSON array of objects to string. So here, the |UpdateCallbackImpl| will
  // deserialize the first object in the array. This means we have to have
  // exactly one device_info in the |descriptors_and_formats|.
  media_internals_->UpdateVideoCaptureDeviceCapabilities(
      descriptors_and_formats);

#if defined(OS_LINUX)
  ExpectString("id", "/dev/dummy");
#else
  ExpectString("id", "dummy");
#endif
  ExpectString("name", "dummy");
  base::ListValue expected_list;
  expected_list.AppendString(media::VideoCaptureFormat::ToString(format_hd));
  ExpectListOfStrings("formats", expected_list);
#if defined(OS_LINUX)
  ExpectString("captureApi", "V4L2 SPLANE");
#elif defined(OS_WIN)
  ExpectString("captureApi", "Direct Show");
#elif defined(OS_MACOSX)
  ExpectString("captureApi", "AV Foundation");
#elif defined(OS_ANDROID)
  ExpectString("captureApi", "Camera API2 Legacy");
#endif
}

class MediaInternalsAudioLogTest
    : public MediaInternalsTestBase,
      public testing::TestWithParam<media::AudioLogFactory::AudioComponent> {
 public:
  MediaInternalsAudioLogTest()
      : update_cb_(base::Bind(&MediaInternalsAudioLogTest::UpdateCallbackImpl,
                              base::Unretained(this))),
        test_params_(MakeAudioParams()),
        test_component_(GetParam()),
        audio_log_(media_internals_->CreateAudioLog(test_component_)) {
    media_internals_->AddUpdateCallback(update_cb_);
  }

  virtual ~MediaInternalsAudioLogTest() {
    media_internals_->RemoveUpdateCallback(update_cb_);
  }

 protected:
  MediaInternals::UpdateCallback update_cb_;
  const media::AudioParameters test_params_;
  const media::AudioLogFactory::AudioComponent test_component_;
  std::unique_ptr<media::AudioLog> audio_log_;

 private:
  static media::AudioParameters MakeAudioParams() {
    media::AudioParameters params(media::AudioParameters::AUDIO_PCM_LINEAR,
                                  media::CHANNEL_LAYOUT_MONO, 48000, 16, 128);
    params.set_effects(media::AudioParameters::ECHO_CANCELLER |
                       media::AudioParameters::DUCKING);
    return params;
  }
};

TEST_P(MediaInternalsAudioLogTest, AudioLogCreateStartStopErrorClose) {
  audio_log_->OnCreated(kTestComponentID, test_params_, kTestDeviceID);
  base::RunLoop().RunUntilIdle();

  ExpectString("channel_layout",
               media::ChannelLayoutToString(test_params_.channel_layout()));
  ExpectInt("sample_rate", test_params_.sample_rate());
  ExpectInt("frames_per_buffer", test_params_.frames_per_buffer());
  ExpectInt("channels", test_params_.channels());
  ExpectString("effects", "ECHO_CANCELLER | DUCKING");
  ExpectString("device_id", kTestDeviceID);
  ExpectInt("component_id", kTestComponentID);
  ExpectInt("component_type", test_component_);
  ExpectStatus("created");

  // Verify OnStarted().
  audio_log_->OnStarted(kTestComponentID);
  base::RunLoop().RunUntilIdle();
  ExpectStatus("started");

  // Verify OnStopped().
  audio_log_->OnStopped(kTestComponentID);
  base::RunLoop().RunUntilIdle();
  ExpectStatus("stopped");

  // Verify OnError().
  const char kErrorKey[] = "error_occurred";
  std::string no_value;
  ASSERT_FALSE(update_data_.GetString(kErrorKey, &no_value));
  audio_log_->OnError(kTestComponentID);
  base::RunLoop().RunUntilIdle();
  ExpectString(kErrorKey, "true");

  // Verify OnClosed().
  audio_log_->OnClosed(kTestComponentID);
  base::RunLoop().RunUntilIdle();
  ExpectStatus("closed");
}

TEST_P(MediaInternalsAudioLogTest, AudioLogCreateClose) {
  audio_log_->OnCreated(kTestComponentID, test_params_, kTestDeviceID);
  base::RunLoop().RunUntilIdle();
  ExpectStatus("created");

  audio_log_->OnClosed(kTestComponentID);
  base::RunLoop().RunUntilIdle();
  ExpectStatus("closed");
}

INSTANTIATE_TEST_CASE_P(
    MediaInternalsAudioLogTest,
    MediaInternalsAudioLogTest,
    testing::Values(media::AudioLogFactory::AUDIO_INPUT_CONTROLLER,
                    media::AudioLogFactory::AUDIO_OUTPUT_CONTROLLER,
                    media::AudioLogFactory::AUDIO_OUTPUT_STREAM));

class DirectMediaLog : public media::MediaLog {
 public:
  explicit DirectMediaLog(int render_process_id)
      : render_process_id_(render_process_id),
        internals_(content::MediaInternals::GetInstance()) {}
  ~DirectMediaLog() override {}

  void AddEvent(std::unique_ptr<media::MediaLogEvent> event) override {
    std::vector<media::MediaLogEvent> events(1, *event);
    internals_->OnMediaEvents(render_process_id_, events);
  }

 private:
  const int render_process_id_;
  MediaInternals* const internals_;

  DISALLOW_COPY_AND_ASSIGN(DirectMediaLog);
};

class MediaInternalsWatchTimeTest : public testing::Test,
                                    public MediaInternalsTestBase {
 public:
  MediaInternalsWatchTimeTest()
      : render_process_id_(0),
        internals_(content::MediaInternals::GetInstance()),
        media_log_(new DirectMediaLog(render_process_id_)),
        histogram_tester_(new base::HistogramTester()),
        watch_time_keys_(media::MediaLog::GetWatchTimeKeys()),
        watch_time_power_keys_(media::MediaLog::GetWatchTimePowerKeys()) {}

  void Initialize(bool has_audio,
                  bool has_video,
                  bool is_mse,
                  bool is_encrypted) {
    wtr_.reset(new media::WatchTimeReporter(
        has_audio, has_video, is_mse, is_encrypted, true, media_log_.get(),
        gfx::Size(800, 600),
        base::Bind(&MediaInternalsWatchTimeTest::GetCurrentMediaTime,
                   base::Unretained(this))));
    wtr_->set_reporting_interval_for_testing();
  }

  void CycleWatchTimeReporter() {
    base::RunLoop run_loop;
    base::ThreadTaskRunnerHandle::Get()->PostTask(FROM_HERE,
                                                  run_loop.QuitClosure());
    run_loop.Run();
  }

  void ExpectWatchTime(const std::vector<base::StringPiece>& keys,
                       base::TimeDelta value) {
    for (auto key : watch_time_keys_) {
      auto it = std::find(keys.begin(), keys.end(), key);
      if (it == keys.end()) {
        histogram_tester_->ExpectTotalCount(key.as_string(), 0);
      } else {
        histogram_tester_->ExpectUniqueSample(key.as_string(),
                                              value.InMilliseconds(), 1);
      }
    }
  }

  void ResetHistogramTester() {
    histogram_tester_.reset(new base::HistogramTester());
  }

  MOCK_METHOD0(GetCurrentMediaTime, base::TimeDelta());

 protected:
  const int render_process_id_;
  MediaInternals* const internals_;
  std::unique_ptr<DirectMediaLog> media_log_;
  std::unique_ptr<base::HistogramTester> histogram_tester_;
  std::unique_ptr<media::WatchTimeReporter> wtr_;
  const base::flat_set<base::StringPiece> watch_time_keys_;
  const base::flat_set<base::StringPiece> watch_time_power_keys_;

  DISALLOW_COPY_AND_ASSIGN(MediaInternalsWatchTimeTest);
};

TEST_F(MediaInternalsWatchTimeTest, BasicAudio) {
  constexpr base::TimeDelta kWatchTimeEarly = base::TimeDelta::FromSeconds(5);
  constexpr base::TimeDelta kWatchTimeLate = base::TimeDelta::FromSeconds(10);
  EXPECT_CALL(*this, GetCurrentMediaTime())
      .WillOnce(testing::Return(base::TimeDelta()))
      .WillOnce(testing::Return(kWatchTimeEarly))
      .WillRepeatedly(testing::Return(kWatchTimeLate));
  Initialize(true, false, true, true);
  wtr_->OnPlaying();

  // No log should have been generated yet since the message loop has not had
  // any chance to pump.
  CycleWatchTimeReporter();
  ExpectWatchTime(std::vector<base::StringPiece>(), base::TimeDelta());

  CycleWatchTimeReporter();
  wtr_.reset();

  ExpectWatchTime(
      {media::MediaLog::kWatchTimeAudioAll, media::MediaLog::kWatchTimeAudioMse,
       media::MediaLog::kWatchTimeAudioEme, media::MediaLog::kWatchTimeAudioAc,
       media::MediaLog::kWatchTimeAudioEmbeddedExperience},
      kWatchTimeLate);
}

TEST_F(MediaInternalsWatchTimeTest, BasicVideo) {
  constexpr base::TimeDelta kWatchTimeEarly = base::TimeDelta::FromSeconds(5);
  constexpr base::TimeDelta kWatchTimeLate = base::TimeDelta::FromSeconds(10);
  EXPECT_CALL(*this, GetCurrentMediaTime())
      .WillOnce(testing::Return(base::TimeDelta()))
      .WillOnce(testing::Return(kWatchTimeEarly))
      .WillRepeatedly(testing::Return(kWatchTimeLate));
  Initialize(true, true, false, true);
  wtr_->OnPlaying();

  // No log should have been generated yet since the message loop has not had
  // any chance to pump.
  CycleWatchTimeReporter();
  ExpectWatchTime(std::vector<base::StringPiece>(), base::TimeDelta());

  CycleWatchTimeReporter();
  wtr_.reset();

  ExpectWatchTime({media::MediaLog::kWatchTimeAudioVideoAll,
                   media::MediaLog::kWatchTimeAudioVideoSrc,
                   media::MediaLog::kWatchTimeAudioVideoEme,
                   media::MediaLog::kWatchTimeAudioVideoAc,
                   media::MediaLog::kWatchTimeAudioVideoEmbeddedExperience},
                  kWatchTimeLate);
}

TEST_F(MediaInternalsWatchTimeTest, BasicPower) {
  constexpr base::TimeDelta kWatchTime1 = base::TimeDelta::FromSeconds(5);
  constexpr base::TimeDelta kWatchTime2 = base::TimeDelta::FromSeconds(10);
  constexpr base::TimeDelta kWatchTime3 = base::TimeDelta::FromSeconds(30);
  EXPECT_CALL(*this, GetCurrentMediaTime())
      .WillOnce(testing::Return(base::TimeDelta()))
      .WillOnce(testing::Return(kWatchTime1))
      .WillOnce(testing::Return(kWatchTime2))
      .WillOnce(testing::Return(kWatchTime2))
      .WillRepeatedly(testing::Return(kWatchTime3));

  Initialize(true, true, false, true);
  wtr_->OnPlaying();
  wtr_->set_is_on_battery_power_for_testing(true);

  // No log should have been generated yet since the message loop has not had
  // any chance to pump.
  CycleWatchTimeReporter();
  ExpectWatchTime(std::vector<base::StringPiece>(), base::TimeDelta());

  CycleWatchTimeReporter();

  // Transition back to AC power, this should generate AC watch time as well.
  wtr_->OnPowerStateChangeForTesting(false);
  CycleWatchTimeReporter();

  // This should finalize the power watch time on battery.
  ExpectWatchTime({media::MediaLog::kWatchTimeAudioVideoBattery}, kWatchTime2);
  ResetHistogramTester();
  wtr_.reset();

  std::vector<base::StringPiece> normal_keys = {
      media::MediaLog::kWatchTimeAudioVideoAll,
      media::MediaLog::kWatchTimeAudioVideoSrc,
      media::MediaLog::kWatchTimeAudioVideoEme,
      media::MediaLog::kWatchTimeAudioVideoEmbeddedExperience};

  for (auto key : watch_time_keys_) {
    if (key == media::MediaLog::kWatchTimeAudioVideoAc) {
      histogram_tester_->ExpectUniqueSample(
          key.as_string(), (kWatchTime3 - kWatchTime2).InMilliseconds(), 1);
      continue;
    }

    auto it = std::find(normal_keys.begin(), normal_keys.end(), key);
    if (it == normal_keys.end()) {
      histogram_tester_->ExpectTotalCount(key.as_string(), 0);
    } else {
      histogram_tester_->ExpectUniqueSample(key.as_string(),
                                            kWatchTime3.InMilliseconds(), 1);
    }
  }
}

TEST_F(MediaInternalsWatchTimeTest, BasicHidden) {
  constexpr base::TimeDelta kWatchTimeEarly = base::TimeDelta::FromSeconds(5);
  constexpr base::TimeDelta kWatchTimeLate = base::TimeDelta::FromSeconds(10);
  EXPECT_CALL(*this, GetCurrentMediaTime())
      .WillOnce(testing::Return(base::TimeDelta()))
      .WillOnce(testing::Return(kWatchTimeEarly))
      .WillRepeatedly(testing::Return(kWatchTimeLate));
  Initialize(true, true, false, true);
  wtr_->OnHidden();
  wtr_->OnPlaying();

  // No log should have been generated yet since the message loop has not had
  // any chance to pump.
  CycleWatchTimeReporter();
  ExpectWatchTime(std::vector<base::StringPiece>(), base::TimeDelta());

  CycleWatchTimeReporter();
  wtr_.reset();

  ExpectWatchTime(
      {media::MediaLog::kWatchTimeAudioVideoBackgroundAll,
       media::MediaLog::kWatchTimeAudioVideoBackgroundSrc,
       media::MediaLog::kWatchTimeAudioVideoBackgroundEme,
       media::MediaLog::kWatchTimeAudioVideoBackgroundAc,
       media::MediaLog::kWatchTimeAudioVideoBackgroundEmbeddedExperience},
      kWatchTimeLate);
}

TEST_F(MediaInternalsWatchTimeTest, PlayerDestructionFinalizes) {
  constexpr base::TimeDelta kWatchTimeEarly = base::TimeDelta::FromSeconds(5);
  constexpr base::TimeDelta kWatchTimeLate = base::TimeDelta::FromSeconds(10);
  EXPECT_CALL(*this, GetCurrentMediaTime())
      .WillOnce(testing::Return(base::TimeDelta()))
      .WillOnce(testing::Return(kWatchTimeEarly))
      .WillRepeatedly(testing::Return(kWatchTimeLate));
  Initialize(true, true, false, true);
  wtr_->OnPlaying();

  // No log should have been generated yet since the message loop has not had
  // any chance to pump.
  CycleWatchTimeReporter();
  ExpectWatchTime(std::vector<base::StringPiece>(), base::TimeDelta());

  CycleWatchTimeReporter();

  media_log_->AddEvent(
      media_log_->CreateEvent(media::MediaLogEvent::WEBMEDIAPLAYER_DESTROYED));

  ExpectWatchTime({media::MediaLog::kWatchTimeAudioVideoAll,
                   media::MediaLog::kWatchTimeAudioVideoSrc,
                   media::MediaLog::kWatchTimeAudioVideoEme,
                   media::MediaLog::kWatchTimeAudioVideoAc,
                   media::MediaLog::kWatchTimeAudioVideoEmbeddedExperience},
                  kWatchTimeLate);
}

TEST_F(MediaInternalsWatchTimeTest, ProcessDestructionFinalizes) {
  constexpr base::TimeDelta kWatchTimeEarly = base::TimeDelta::FromSeconds(5);
  constexpr base::TimeDelta kWatchTimeLate = base::TimeDelta::FromSeconds(10);
  EXPECT_CALL(*this, GetCurrentMediaTime())
      .WillOnce(testing::Return(base::TimeDelta()))
      .WillOnce(testing::Return(kWatchTimeEarly))
      .WillRepeatedly(testing::Return(kWatchTimeLate));
  Initialize(true, true, false, true);
  wtr_->OnPlaying();

  // No log should have been generated yet since the message loop has not had
  // any chance to pump.
  CycleWatchTimeReporter();
  ExpectWatchTime(std::vector<base::StringPiece>(), base::TimeDelta());

  CycleWatchTimeReporter();

  internals_->OnProcessTerminatedForTesting(render_process_id_);
  ExpectWatchTime({media::MediaLog::kWatchTimeAudioVideoAll,
                   media::MediaLog::kWatchTimeAudioVideoSrc,
                   media::MediaLog::kWatchTimeAudioVideoEme,
                   media::MediaLog::kWatchTimeAudioVideoAc,
                   media::MediaLog::kWatchTimeAudioVideoEmbeddedExperience},
                  kWatchTimeLate);
}

}  // namespace content
