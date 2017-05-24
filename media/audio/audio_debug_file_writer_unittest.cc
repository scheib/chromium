// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>

#include "base/bind.h"
#include "base/files/file_util.h"
#include "base/memory/ptr_util.h"
#include "base/message_loop/message_loop.h"
#include "base/synchronization/waitable_event.h"
#include "base/sys_byteorder.h"
#include "base/test/scoped_task_environment.h"
#include "base/threading/thread.h"
#include "media/audio/audio_debug_file_writer.h"
#include "media/base/audio_bus.h"
#include "media/base/audio_sample_types.h"
#include "media/base/test_helpers.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace media {

namespace {

static const uint16_t kBytesPerSample = sizeof(uint16_t);
static const uint16_t kPcmEncoding = 1;
static const size_t kWavHeaderSize = 44;

uint16_t ReadLE2(const char* buf) {
  return static_cast<uint8_t>(buf[0]) | (static_cast<uint8_t>(buf[1]) << 8);
}

uint32_t ReadLE4(const char* buf) {
  return static_cast<uint8_t>(buf[0]) | (static_cast<uint8_t>(buf[1]) << 8) |
         (static_cast<uint8_t>(buf[2]) << 16) |
         (static_cast<uint8_t>(buf[3]) << 24);
}

}  // namespace

// <channel layout, sample rate, frames per buffer, number of buffer writes
typedef std::tr1::tuple<ChannelLayout, int, int, int>
    AudioDebugFileWriterTestData;

class AudioDebugFileWriterTest
    : public testing::TestWithParam<AudioDebugFileWriterTestData> {
 public:
  AudioDebugFileWriterTest()
      : file_thread_("FileThread"),
        params_(AudioParameters::Format::AUDIO_PCM_LINEAR,
                std::tr1::get<0>(GetParam()),
                std::tr1::get<1>(GetParam()),
                kBytesPerSample * 8,
                std::tr1::get<2>(GetParam())),
        writes_(std::tr1::get<3>(GetParam())),
        source_samples_(params_.frames_per_buffer() * params_.channels() *
                        writes_),
        source_interleaved_(source_samples_ ? new int16_t[source_samples_]
                                            : nullptr) {
    file_thread_.StartAndWaitForTesting();
    InitSourceInterleaved(source_interleaved_.get(), source_samples_);
  }

 protected:
  virtual ~AudioDebugFileWriterTest() {}

  static void InitSourceInterleaved(int16_t* source_interleaved,
                                    int source_samples) {
    if (source_samples) {
      // equal steps to cover int16_t range of values
      int16_t step = 0xffff / source_samples;
      int16_t val = std::numeric_limits<int16_t>::min();
      for (int i = 0; i < source_samples; ++i, val += step)
        source_interleaved[i] = val;
    }
  }

  static void VerifyHeader(const char (&wav_header)[kWavHeaderSize],
                           const AudioParameters& params,
                           int writes,
                           int64_t file_length) {
    uint32_t block_align = params.channels() * kBytesPerSample;
    uint32_t data_size =
        static_cast<uint32_t>(params.frames_per_buffer() * params.channels() *
                              writes * kBytesPerSample);
    // Offset Length  Content
    //  0      4     "RIFF"
    EXPECT_EQ(0, strncmp(wav_header, "RIFF", 4));
    //  4      4     <file length - 8>
    ASSERT_GT(file_length, 8);
    EXPECT_EQ(static_cast<uint64_t>(file_length - 8), ReadLE4(wav_header + 4));
    EXPECT_EQ(static_cast<uint32_t>(data_size + kWavHeaderSize - 8),
              ReadLE4(wav_header + 4));
    //  8      4     "WAVE"
    // 12      4     "fmt "
    EXPECT_EQ(0, strncmp(wav_header + 8, "WAVEfmt ", 8));
    // 16      4     <length of the fmt data> (=16)
    EXPECT_EQ(16U, ReadLE4(wav_header + 16));
    // 20      2     <WAVE file encoding tag>
    EXPECT_EQ(kPcmEncoding, ReadLE2(wav_header + 20));
    // 22      2     <channels>
    EXPECT_EQ(params.channels(), ReadLE2(wav_header + 22));
    // 24      4     <sample rate>
    EXPECT_EQ(static_cast<uint32_t>(params.sample_rate()),
              ReadLE4(wav_header + 24));
    // 28      4     <bytes per second> (sample rate * block align)
    EXPECT_EQ(static_cast<uint32_t>(params.sample_rate()) * block_align,
              ReadLE4(wav_header + 28));
    // 32      2     <block align>  (channels * bits per sample / 8)
    EXPECT_EQ(block_align, ReadLE2(wav_header + 32));
    // 34      2     <bits per sample>
    EXPECT_EQ(kBytesPerSample * 8, ReadLE2(wav_header + 34));
    // 36      4     "data"
    EXPECT_EQ(0, strncmp(wav_header + 36, "data", 4));
    // 40      4     <sample data size(n)>
    EXPECT_EQ(data_size, ReadLE4(wav_header + 40));
  }

  // |result_interleaved| is expected to be little-endian.
  static void VerifyDataRecording(const int16_t* source_interleaved,
                                  const int16_t* result_interleaved,
                                  int16_t source_samples) {
    // Allow mismatch by 1 due to rounding error in int->float->int
    // calculations.
    for (int i = 0; i < source_samples; ++i)
      EXPECT_LE(std::abs(static_cast<int16_t>(
                             base::ByteSwapToLE16(source_interleaved[i])) -
                         result_interleaved[i]),
                1)
          << "i = " << i << " source " << source_interleaved[i] << " result "
          << result_interleaved[i];
  }

  void VerifyRecording(const base::FilePath& file_path) {
    base::File file(file_path, base::File::FLAG_OPEN | base::File::FLAG_READ);
    EXPECT_TRUE(file.IsValid());

    char wav_header[kWavHeaderSize];
    EXPECT_EQ(file.Read(0, wav_header, kWavHeaderSize),
              static_cast<int>(kWavHeaderSize));
    VerifyHeader(wav_header, params_, writes_, file.GetLength());

    if (source_samples_ > 0) {
      std::unique_ptr<int16_t[]> result_interleaved(
          new int16_t[source_samples_]);
      memset(result_interleaved.get(), 0, source_samples_ * kBytesPerSample);

      // Recording is read from file as a byte sequence, so it stored as
      // little-endian.
      int read = file.Read(kWavHeaderSize,
                           reinterpret_cast<char*>(result_interleaved.get()),
                           source_samples_ * kBytesPerSample);
      EXPECT_EQ(static_cast<int>(file.GetLength() - kWavHeaderSize), read);

      VerifyDataRecording(source_interleaved_.get(), result_interleaved.get(),
                          source_samples_);
    }
  }

  void DoDebugRecording() {
    // Write tasks are posted to |file_thread_|.
    for (int i = 0; i < writes_; ++i) {
      std::unique_ptr<AudioBus> bus =
          AudioBus::Create(params_.channels(), params_.frames_per_buffer());

      bus->FromInterleaved<media::SignedInt16SampleTypeTraits>(
          source_interleaved_.get() +
              i * params_.channels() * params_.frames_per_buffer(),
          params_.frames_per_buffer());

      debug_writer_->Write(std::move(bus));
    }
  }

  void RecordAndVerifyOnce() {
    base::FilePath file_path;
    EXPECT_TRUE(base::CreateTemporaryFile(&file_path));

    debug_writer_->Start(file_path);

    DoDebugRecording();

    debug_writer_->Stop();

    scoped_task_environment_.RunUntilIdle();

    VerifyRecording(file_path);

    if (::testing::Test::HasFailure()) {
      LOG(ERROR) << "Test failed; keeping recording(s) at ["
                 << file_path.value().c_str() << "].";
    } else {
      EXPECT_TRUE(base::DeleteFile(file_path, false));
    }
  }

 protected:
  // |file_thread_| must to be declared before |debug_writer_| so that it's
  // destroyed after. This ensures all tasks posted in |debug_writer_| to the
  // file thread are run before exiting the test.
  base::Thread file_thread_;

  // The test task environment.
  base::test::ScopedTaskEnvironment scoped_task_environment_;

  // Writer under test.
  std::unique_ptr<AudioDebugFileWriter> debug_writer_;

  // AudioBus parameters.
  AudioParameters params_;

  // Number of times to write AudioBus to the file.
  int writes_;

  // Number of samples in the source data.
  int source_samples_;

  // Source data.
  std::unique_ptr<int16_t[]> source_interleaved_;

 private:
  DISALLOW_COPY_AND_ASSIGN(AudioDebugFileWriterTest);
};

class AudioDebugFileWriterBehavioralTest : public AudioDebugFileWriterTest {};

TEST_P(AudioDebugFileWriterTest, WaveRecordingTest) {
  debug_writer_.reset(new AudioDebugFileWriter(params_));
  RecordAndVerifyOnce();
}

TEST_P(AudioDebugFileWriterTest, GetFileNameExtension) {
  debug_writer_.reset(new AudioDebugFileWriter(params_));
  EXPECT_EQ(FILE_PATH_LITERAL("wav"),
            base::FilePath::StringType(debug_writer_->GetFileNameExtension()));
}

TEST_P(AudioDebugFileWriterBehavioralTest,
       DeletedBeforeRecordingFinishedOnFileThread) {
  debug_writer_.reset(new AudioDebugFileWriter(params_));

  base::FilePath file_path;
  EXPECT_TRUE(base::CreateTemporaryFile(&file_path));

  base::WaitableEvent* wait_for_deletion =
      new base::WaitableEvent(base::WaitableEvent::ResetPolicy::MANUAL,
                              base::WaitableEvent::InitialState::NOT_SIGNALED);

  file_thread_.task_runner()->PostTask(
      FROM_HERE,
      base::Bind(&base::WaitableEvent::Wait, base::Owned(wait_for_deletion)));

  debug_writer_->Start(file_path);

  DoDebugRecording();

  debug_writer_.reset();
  wait_for_deletion->Signal();

  scoped_task_environment_.RunUntilIdle();

  VerifyRecording(file_path);

  if (::testing::Test::HasFailure()) {
    LOG(ERROR) << "Test failed; keeping recording(s) at ["
               << file_path.value().c_str() << "].";
  } else {
    EXPECT_TRUE(base::DeleteFile(file_path, false));
  }
}

TEST_P(AudioDebugFileWriterBehavioralTest, FileCreationError) {
  debug_writer_.reset(new AudioDebugFileWriter(params_));
  base::FilePath file_path;  // Empty file name.
  debug_writer_->Start(file_path);
  DoDebugRecording();
}

TEST_P(AudioDebugFileWriterBehavioralTest, StartStopStartStop) {
  debug_writer_.reset(new AudioDebugFileWriter(params_));
  RecordAndVerifyOnce();
  RecordAndVerifyOnce();
}

TEST_P(AudioDebugFileWriterBehavioralTest, DestroyNotStarted) {
  debug_writer_.reset(new AudioDebugFileWriter(params_));
  debug_writer_.reset();
}

TEST_P(AudioDebugFileWriterBehavioralTest, DestroyStarted) {
  debug_writer_.reset(new AudioDebugFileWriter(params_));
  base::FilePath file_path;
  EXPECT_TRUE(base::CreateTemporaryFile(&file_path));
  debug_writer_->Start(file_path);
  debug_writer_.reset();
}

INSTANTIATE_TEST_CASE_P(
    AudioDebugFileWriterTest,
    AudioDebugFileWriterTest,
    // Using 10ms frames per buffer everywhere.
    testing::Values(
        // No writes.
        std::tr1::make_tuple(ChannelLayout::CHANNEL_LAYOUT_MONO,
                             44100,
                             44100 / 100,
                             0),
        // 1 write of mono.
        std::tr1::make_tuple(ChannelLayout::CHANNEL_LAYOUT_MONO,
                             44100,
                             44100 / 100,
                             1),
        // 1 second of mono.
        std::tr1::make_tuple(ChannelLayout::CHANNEL_LAYOUT_MONO,
                             44100,
                             44100 / 100,
                             100),
        // 1 second of mono, higher rate.
        std::tr1::make_tuple(ChannelLayout::CHANNEL_LAYOUT_MONO,
                             48000,
                             48000 / 100,
                             100),
        // 1 second of stereo.
        std::tr1::make_tuple(ChannelLayout::CHANNEL_LAYOUT_STEREO,
                             44100,
                             44100 / 100,
                             100),
        // 15 seconds of stereo, higher rate.
        std::tr1::make_tuple(ChannelLayout::CHANNEL_LAYOUT_STEREO,
                             48000,
                             48000 / 100,
                             1500)));

INSTANTIATE_TEST_CASE_P(
    AudioDebugFileWriterBehavioralTest,
    AudioDebugFileWriterBehavioralTest,
    // Using 10ms frames per buffer everywhere.
    testing::Values(
        // No writes.
        std::tr1::make_tuple(ChannelLayout::CHANNEL_LAYOUT_MONO,
                             44100,
                             44100 / 100,
                             100)));

}  // namespace media
