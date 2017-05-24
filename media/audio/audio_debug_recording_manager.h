// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEDIA_AUDIO_AUDIO_DEBUG_RECORDING_MANAGER_H_
#define MEDIA_AUDIO_AUDIO_DEBUG_RECORDING_MANAGER_H_

#include <map>
#include <memory>
#include <string>
#include <utility>

#include "base/callback.h"
#include "base/files/file_path.h"
#include "base/gtest_prod_util.h"
#include "base/memory/weak_ptr.h"
#include "base/threading/thread_checker.h"
#include "media/audio/audio_debug_recording_helper.h"
#include "media/base/audio_parameters.h"
#include "media/base/media_export.h"

namespace base {
class FilePath;
class SingleThreadTaskRunner;
}

namespace media {

class AudioDebugRecordingHelper;

// A manager for audio debug recording that handles registration of data
// sources and hands them a recorder (AudioDebugRecordingHelper) to feed data
// to. The recorder will unregister with the manager automatically when deleted.
// When debug recording is enabled, it is enabled on all recorders and
// constructs a unique file name for each recorder by using a running ID.
// A somewhat simplified diagram of the the debug recording infrastructure,
// interfaces omitted:
//
//                                AudioDebugFileWriter
//                                        ^
//                                        | owns
//                        owns            |
//   OnMoreDataConverter  ---->  AudioDebugRecordingHelper
//            ^                           ^
//            | owns several              | raw pointer to several
//            |                           |
//   AudioOutputResampler        AudioDebugRecordingManager
//            ^                           ^
//            |                           | owns
//            | owns several              |
//             ------------------  AudioManagerBase
//
// AudioDebugRecordingManager is created when
// AudioManager::InitializeOutputDebugRecording() is called. That is done
// in AudioManager::Create() in WebRTC enabled builds, but not in non WebRTC
// enabled builds.
// If AudioDebugRecordingManager is not created, neither is
// AudioDebugRecordingHelper or AudioDebugFileWriter. In this case the pointers
// to AudioDebugRecordingManager and AudioDebugRecordingHelper are null.
//
class MEDIA_EXPORT AudioDebugRecordingManager {
 public:
  AudioDebugRecordingManager(
      scoped_refptr<base::SingleThreadTaskRunner> task_runner);
  virtual ~AudioDebugRecordingManager();

  // Enables and disables debug recording.
  virtual void EnableDebugRecording(const base::FilePath& base_file_name);
  virtual void DisableDebugRecording();

  // Registers a source and returns a wrapped recorder. |file_name_extension| is
  // added to the base filename, along with a unique running ID.
  std::unique_ptr<AudioDebugRecorder> RegisterDebugRecordingSource(
      const base::FilePath::StringType& file_name_extension,
      const AudioParameters& params);

 protected:
  // Creates a AudioDebugRecordingHelper. Overridden by test.
  virtual std::unique_ptr<AudioDebugRecordingHelper>
  CreateAudioDebugRecordingHelper(
      const AudioParameters& params,
      scoped_refptr<base::SingleThreadTaskRunner> task_runner,
      base::OnceClosure on_destruction_closure);

  // The task runner this class lives on. Also handed to
  // AudioDebugRecordingHelpers.
  scoped_refptr<base::SingleThreadTaskRunner> task_runner_;

 private:
  FRIEND_TEST_ALL_PREFIXES(AudioDebugRecordingManagerTest,
                           RegisterAutomaticUnregisterAtDelete);
  FRIEND_TEST_ALL_PREFIXES(AudioDebugRecordingManagerTest,
                           RegisterEnableDisable);
  FRIEND_TEST_ALL_PREFIXES(AudioDebugRecordingManagerTest,
                           EnableRegisterDisable);

  // Map type from source id to recorder and its filename extension.
  using DebugRecordingHelperMap = std::map<
      int,
      std::pair<AudioDebugRecordingHelper*, base::FilePath::StringType>>;

  // Unregisters a source.
  void UnregisterDebugRecordingSource(int id);

  bool IsDebugRecordingEnabled();

  // Recorders, one per source.
  DebugRecordingHelperMap debug_recording_helpers_;

  // The base file name for debug recording files. If this is non-empty, debug
  // recording is enabled.
  base::FilePath debug_recording_base_file_name_;

  base::WeakPtrFactory<AudioDebugRecordingManager> weak_factory_;
  DISALLOW_COPY_AND_ASSIGN(AudioDebugRecordingManager);
};

}  // namespace media

#endif  // MEDIA_AUDIO_AUDIO_DEBUG_RECORDING_MANAGER_H_
