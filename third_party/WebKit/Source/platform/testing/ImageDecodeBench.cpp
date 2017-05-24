// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Provides a minimal wrapping of the Blink image decoders. Used to perform
// a non-threaded, memory-to-memory image decode using micro second accuracy
// clocks to measure image decode time. Optionally applies color correction
// during image decoding on supported platforms (default off). Usage:
//
//   % ninja -C out/Release image_decode_bench &&
//      ./out/Release/image_decode_bench file [iterations]
//
// TODO(noel): Consider adding md5 checksum support to WTF. Use it to compute
// the decoded image frame md5 and output that value.
//
// TODO(noel): Consider integrating this tool in Chrome telemetry for realz,
// using the image corpii used to assess Blink image decode performance. Refer
// to http://crbug.com/398235#c103 and http://crbug.com/258324#c5

#include <memory>
#include "base/command_line.h"
#include "platform/SharedBuffer.h"
#include "platform/image-decoders/ImageDecoder.h"
#include "platform/wtf/PassRefPtr.h"
#include "platform/wtf/PtrUtil.h"
#include "public/platform/Platform.h"
#include "ui/gfx/test/icc_profiles.h"

#if defined(_WIN32)
#include <mmsystem.h>
#include <sys/stat.h>
#include <time.h>
#define stat(x, y) _stat(x, y)
typedef struct _stat sttype;
#else
#include <sys/stat.h>
#include <sys/time.h>
typedef struct stat sttype;
#endif

namespace blink {

namespace {

#if defined(_WIN32)

// There is no real platform support herein, so adopt the WIN32 performance
// counter from WTF
// http://trac.webkit.org/browser/trunk/Source/WTF/wtf/CurrentTime.cpp?rev=152438

static double LowResUTCTime() {
  FILETIME file_time;
  GetSystemTimeAsFileTime(&file_time);

  // As per Windows documentation for FILETIME, copy the resulting FILETIME
  // structure to a ULARGE_INTEGER structure using memcpy (using memcpy instead
  // of direct assignment can prevent alignment faults on 64-bit Windows).
  ULARGE_INTEGER date_time;
  memcpy(&date_time, &file_time, sizeof(date_time));

  // Number of 100 nanosecond between January 1, 1601 and January 1, 1970.
  static const ULONGLONG kEpochBias = 116444736000000000ULL;
  // Windows file times are in 100s of nanoseconds.
  static const double kHundredsOfNanosecondsPerMillisecond = 10000;
  return (date_time.QuadPart - kEpochBias) /
         kHundredsOfNanosecondsPerMillisecond;
}

static LARGE_INTEGER g_qpc_frequency;
static bool g_synced_time;

static double HighResUpTime() {
  // We use QPC, but only after sanity checking its result, due to bugs:
  // http://support.microsoft.com/kb/274323
  // http://support.microsoft.com/kb/895980
  // http://msdn.microsoft.com/en-us/library/ms644904.aspx ("you can get
  // different results on different processors due to bugs in the basic
  // input/output system (BIOS) or the hardware abstraction layer (HAL).").

  static LARGE_INTEGER qpc_last;
  static DWORD tick_count_last;
  static bool inited;

  LARGE_INTEGER qpc;
  QueryPerformanceCounter(&qpc);
  DWORD tick_count = GetTickCount();

  if (inited) {
    __int64 qpc_elapsed =
        ((qpc.QuadPart - qpc_last.QuadPart) * 1000) / g_qpc_frequency.QuadPart;
    __int64 tick_count_elapsed;
    if (tick_count >= tick_count_last) {
      tick_count_elapsed = (tick_count - tick_count_last);
    } else {
      __int64 tick_count_large = tick_count + 0x100000000I64;
      tick_count_elapsed = tick_count_large - tick_count_last;
    }

    // Force a re-sync if QueryPerformanceCounter differs from GetTickCount() by
    // more than 500ms. (The 500ms value is from
    // http://support.microsoft.com/kb/274323).
    __int64 diff = tick_count_elapsed - qpc_elapsed;
    if (diff > 500 || diff < -500)
      g_synced_time = false;
  } else {
    inited = true;
  }

  qpc_last = qpc;
  tick_count_last = tick_count;

  return (1000.0 * qpc.QuadPart) /
         static_cast<double>(g_qpc_frequency.QuadPart);
}

static bool QpcAvailable() {
  static bool available;
  static bool checked;

  if (checked)
    return available;

  available = QueryPerformanceFrequency(&g_qpc_frequency);
  checked = true;
  return available;
}

static double GetCurrentTime() {
  // Use a combination of ftime and QueryPerformanceCounter.
  // ftime returns the information we want, but doesn't have sufficient
  // resolution.  QueryPerformanceCounter has high resolution, but is only
  // usable to measure time intervals.  To combine them, we call ftime and
  // QueryPerformanceCounter initially. Later calls will use
  // QueryPerformanceCounter by itself, adding the delta to the saved ftime.  We
  // periodically re-sync to correct for drift.
  static double sync_low_res_utc_time;
  static double sync_high_res_up_time;
  static double last_utc_time;

  double low_res_time = LowResUTCTime();
  if (!QpcAvailable())
    return low_res_time * (1.0 / 1000.0);

  double high_res_time = HighResUpTime();
  if (!g_synced_time) {
    timeBeginPeriod(1);  // increase time resolution around low-res time getter
    sync_low_res_utc_time = low_res_time = LowResUTCTime();
    timeEndPeriod(1);  // restore time resolution
    sync_high_res_up_time = high_res_time;
    g_synced_time = true;
  }

  double high_res_elapsed = high_res_time - sync_high_res_up_time;
  double utc = sync_low_res_utc_time + high_res_elapsed;

  // Force a clock re-sync if we've drifted.
  double low_res_elapsed = low_res_time - sync_low_res_utc_time;
  const double kMaximumAllowedDriftMsec =
      15.625 * 2.0;  // 2x the typical low-res accuracy
  if (fabs(high_res_elapsed - low_res_elapsed) > kMaximumAllowedDriftMsec)
    g_synced_time = false;

  // Make sure time doesn't run backwards (only correct if the difference is < 2
  // seconds, since DST or clock changes could occur).
  const double kBackwardTimeLimit = 2000.0;
  if (utc < last_utc_time && (last_utc_time - utc) < kBackwardTimeLimit)
    return last_utc_time * (1.0 / 1000.0);

  last_utc_time = utc;
  return utc * (1.0 / 1000.0);
}

#else

static double GetCurrentTime() {
  struct timeval now;
  gettimeofday(&now, 0);
  return now.tv_sec + now.tv_usec * (1.0 / 1000000.0);
}

#endif

PassRefPtr<SharedBuffer> ReadFile(const char* file_name) {
  FILE* fp = fopen(file_name, "rb");
  if (!fp) {
    fprintf(stderr, "Can't open file %s\n", file_name);
    exit(2);
  }

  sttype s;
  stat(file_name, &s);
  size_t file_size = s.st_size;
  if (s.st_size <= 0)
    return SharedBuffer::Create();

  std::unique_ptr<unsigned char[]> buffer =
      WrapArrayUnique(new unsigned char[file_size]);
  if (file_size != fread(buffer.get(), 1, file_size, fp)) {
    fprintf(stderr, "Error reading file %s\n", file_name);
    exit(2);
  }

  fclose(fp);
  return SharedBuffer::Create(buffer.get(), file_size);
}

bool DecodeImageData(SharedBuffer* data,
                     bool color_correction,
                     size_t packet_size) {
  std::unique_ptr<ImageDecoder> decoder = ImageDecoder::Create(
      data, true, ImageDecoder::kAlphaPremultiplied,
      color_correction ? ColorBehavior::TransformToTargetForTesting()
                       : ColorBehavior::Ignore());
  if (!packet_size) {
    bool all_data_received = true;
    decoder->SetData(data, all_data_received);

    int frame_count = decoder->FrameCount();
    for (int i = 0; i < frame_count; ++i) {
      if (!decoder->FrameBufferAtIndex(i))
        return false;
    }

    return !decoder->Failed();
  }

  RefPtr<SharedBuffer> packet_data = SharedBuffer::Create();
  size_t position = 0;
  size_t next_frame_to_decode = 0;
  while (true) {
    const char* packet;
    size_t length = data->GetSomeData(packet, position);

    length = std::min(length, packet_size);
    packet_data->Append(packet, length);
    position += length;

    bool all_data_received = position == data->size();
    decoder->SetData(packet_data.Get(), all_data_received);

    size_t frame_count = decoder->FrameCount();
    for (; next_frame_to_decode < frame_count; ++next_frame_to_decode) {
      ImageFrame* frame = decoder->FrameBufferAtIndex(next_frame_to_decode);
      if (frame->GetStatus() != ImageFrame::kFrameComplete)
        break;
    }

    if (all_data_received || decoder->Failed())
      break;
  }

  return !decoder->Failed();
}

}  // namespace

int Main(int argc, char* argv[]) {
  base::CommandLine::Init(argc, argv);

  // If the platform supports color correction, allow it to be controlled.

  bool apply_color_correction = false;

  if (argc >= 2 && strcmp(argv[1], "--color-correct") == 0) {
    apply_color_correction = (--argc, ++argv, true);
    gfx::ICCProfile profile = gfx::ICCProfileForTestingColorSpin();
    ColorBehavior::SetGlobalTargetColorProfile(profile);
  }

  if (argc < 2) {
    fprintf(stderr,
            "Usage: %s [--color-correct] file [iterations] [packetSize]\n",
            argv[0]);
    exit(1);
  }

  // Control decode bench iterations and packet size.

  size_t iterations = 1;
  if (argc >= 3) {
    char* end = 0;
    iterations = strtol(argv[2], &end, 10);
    if (*end != '\0' || !iterations) {
      fprintf(stderr,
              "Second argument should be number of iterations. "
              "The default is 1. You supplied %s\n",
              argv[2]);
      exit(1);
    }
  }

  size_t packet_size = 0;
  if (argc >= 4) {
    char* end = 0;
    packet_size = strtol(argv[3], &end, 10);
    if (*end != '\0') {
      fprintf(stderr,
              "Third argument should be packet size. Default is "
              "0, meaning to decode the entire image in one packet. You "
              "supplied %s\n",
              argv[3]);
      exit(1);
    }
  }

  // Create a web platform.  blink::Platform can't be used directly because its
  // constructor is protected.

  class WebPlatform : public blink::Platform {};

  Platform::Initialize(new WebPlatform());

  // Read entire file content to data, and consolidate the SharedBuffer data
  // segments into one, contiguous block of memory.

  RefPtr<SharedBuffer> data = ReadFile(argv[1]);
  if (!data.Get() || !data->size()) {
    fprintf(stderr, "Error reading image data from [%s]\n", argv[1]);
    exit(2);
  }

  data->Data();

  // Warm-up: throw out the first iteration for more consistent results.

  if (!DecodeImageData(data.Get(), apply_color_correction, packet_size)) {
    fprintf(stderr, "Image decode failed [%s]\n", argv[1]);
    exit(3);
  }

  // Image decode bench for iterations.

  double total_time = 0.0;

  for (size_t i = 0; i < iterations; ++i) {
    double start_time = GetCurrentTime();
    bool decoded =
        DecodeImageData(data.Get(), apply_color_correction, packet_size);
    double elapsed_time = GetCurrentTime() - start_time;
    total_time += elapsed_time;
    if (!decoded) {
      fprintf(stderr, "Image decode failed [%s]\n", argv[1]);
      exit(3);
    }
  }

  // Results to stdout.

  double average_time = total_time / static_cast<double>(iterations);
  printf("%f %f\n", total_time, average_time);
  return 0;
}

}  // namespace blink

int main(int argc, char* argv[]) {
  return blink::Main(argc, argv);
}
