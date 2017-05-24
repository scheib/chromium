// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include "base/command_line.h"
#include "base/files/file_util.h"
#include "base/macros.h"
#include "base/strings/string16.h"
#include "base/strings/string_split.h"
#include "base/strings/utf_string_conversions.h"
#include "content/public/common/content_switches.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/content_browser_test.h"
#include "content/public/test/content_browser_test_utils.h"
#include "content/shell/browser/shell.h"
#include "media/base/media_switches.h"
#include "media/base/test_data_util.h"

const char kDecodeTestFile[] = "decode_capabilities_test.html";
const char kSupported[] = "SUPPORTED";
const char kUnsupported[] = "UNSUPPORTED";
const char kError[] = "ERROR";

#if BUILDFLAG(USE_PROPRIETARY_CODECS)
const char* kPropSupported = kSupported;
#else
const char* kPropSupported = kUnsupported;
#endif  // USE_PROPRIETARY_CODECS

enum ConfigType { AUDIO, VIDEO };

namespace content {

class MediaCapabilitiesTest : public ContentBrowserTest {
 public:
  MediaCapabilitiesTest() = default;

  void SetUpCommandLine(base::CommandLine* command_line) override {
    command_line->AppendSwitchASCII(switches::kEnableBlinkFeatures,
                                    "MediaCapabilities");
    command_line->AppendSwitch(switches::kEnableNewVp9CodecString);
    command_line->AppendSwitch(switches::kEnableVp9InMp4);
  }

  std::string CanDecodeAudio(const std::string& content_type) {
    return CanDecode(content_type, ConfigType::AUDIO);
  }

  std::string CanDecodeVideo(const std::string& content_type) {
    return CanDecode(content_type, ConfigType::VIDEO);
  }

  std::string CanDecode(const std::string& content_type,
                        ConfigType configType) {
    std::string command;
    if (configType == ConfigType::AUDIO) {
      command.append("testAudioDecodeContentType(");
    } else {
      command.append("testVideoDecodeContentType(");
    }

    command.append(content_type);
    command.append(");");

    EXPECT_TRUE(ExecuteScript(shell(), command));

    TitleWatcher title_watcher(shell()->web_contents(),
                               base::ASCIIToUTF16(kSupported));
    title_watcher.AlsoWaitForTitle(base::ASCIIToUTF16(kUnsupported));
    title_watcher.AlsoWaitForTitle(base::ASCIIToUTF16(kError));
    return base::UTF16ToASCII(title_watcher.WaitAndGetTitle());
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(MediaCapabilitiesTest);
};

// Cover basic codec support. See media_canplaytype_browsertest.cc for more
// exhaustive codec string testing.
IN_PROC_BROWSER_TEST_F(MediaCapabilitiesTest, VideoDecodeTypes) {
  base::FilePath file_path = media::GetTestDataFilePath(kDecodeTestFile);

  EXPECT_TRUE(
      NavigateToURL(shell(), content::GetFileUrlWithQuery(file_path, "")));

  EXPECT_EQ(kSupported, CanDecodeVideo("'video/webm; codecs=\"vp8\"'"));

  // Only support the new vp09 format which provides critical profile
  // information.
  EXPECT_EQ(kUnsupported, CanDecodeVideo("'video/webm; codecs=\"vp9\"'"));
  // Requires command line flag switches::kEnableNewVp9CodecString
  EXPECT_EQ(kSupported,
            CanDecodeVideo("'video/webm; codecs=\"vp09.00.10.08\"'"));

  // Supported when built with USE_PROPRIETARY_CODECS
  EXPECT_EQ(kPropSupported,
            CanDecodeVideo("'video/mp4; codecs=\"avc1.42E01E\"'"));
  EXPECT_EQ(kPropSupported,
            CanDecodeVideo("'video/mp4; codecs=\"avc1.42101E\"'"));
  EXPECT_EQ(kPropSupported,
            CanDecodeVideo("'video/mp4; codecs=\"avc1.42701E\"'"));
  EXPECT_EQ(kPropSupported,
            CanDecodeVideo("'video/mp4; codecs=\"avc1.42F01E\"'"));
  // Requires command line flag switches::kEnableVp9InMp4
  EXPECT_EQ(kPropSupported,
            CanDecodeVideo("'video/mp4; codecs=\"vp09.00.10.08\"'"));

  // Test a handful of invalid strings.
  EXPECT_EQ(kUnsupported, CanDecodeVideo("'video/webm; codecs=\"theora\"'"));
  EXPECT_EQ(kUnsupported,
            CanDecodeVideo("'video/webm; codecs=\"avc1.42E01E\"'"));
  // Only new vp09 format is supported with MP4.
  EXPECT_EQ(kUnsupported, CanDecodeVideo("'video/mp4; codecs=\"vp9\"'"));
}

// Cover basic codec support. See media_canplaytype_browsertest.cc for more
// exhaustive codec string testing.
IN_PROC_BROWSER_TEST_F(MediaCapabilitiesTest, AudioDecodeTypes) {
  base::FilePath file_path = media::GetTestDataFilePath(kDecodeTestFile);

  EXPECT_TRUE(
      NavigateToURL(shell(), content::GetFileUrlWithQuery(file_path, "")));

  EXPECT_EQ(kSupported, CanDecodeAudio("'audio/ogg; codecs=\"flac\"'"));
  EXPECT_EQ(kSupported, CanDecodeAudio("'audio/ogg; codecs=\"vorbis\"'"));
  EXPECT_EQ(kSupported, CanDecodeAudio("'audio/ogg; codecs=\"opus\"'"));

  EXPECT_EQ(kSupported, CanDecodeAudio("'audio/webm; codecs=\"opus\"'"));
  EXPECT_EQ(kSupported, CanDecodeAudio("'audio/webm; codecs=\"vorbis\"'"));

  EXPECT_EQ(kSupported, CanDecodeAudio("'audio/flac'"));

  // Supported when built with USE_PROPRIETARY_CODECS
  EXPECT_EQ(kPropSupported, CanDecodeAudio("'audio/mpeg; codecs=\"mp4a.69\"'"));
  EXPECT_EQ(kPropSupported,
            CanDecodeAudio("'audio/mp4; codecs=\"mp4a.40.02\"'"));
  EXPECT_EQ(kPropSupported, CanDecodeAudio("'audio/aac'"));

  // Test a handful of invalid strings.
  EXPECT_EQ(kUnsupported, CanDecodeAudio("'audio/wav; codecs=\"mp3\"'"));
  EXPECT_EQ(kUnsupported, CanDecodeAudio("'audio/webm; codecs=\"vp8\"'"));
}

}  // namespace content
