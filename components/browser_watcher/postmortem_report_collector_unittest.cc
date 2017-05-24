// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/browser_watcher/postmortem_report_collector.h"

#include <stdint.h>

#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "base/debug/activity_analyzer.h"
#include "base/debug/activity_tracker.h"
#include "base/files/file.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/memory_mapped_file.h"
#include "base/files/scoped_file.h"
#include "base/files/scoped_temp_dir.h"
#include "base/memory/ptr_util.h"
#include "base/metrics/persistent_memory_allocator.h"
#include "base/process/process_handle.h"
#include "base/stl_util.h"
#include "base/test/histogram_tester.h"
#include "base/threading/platform_thread.h"
#include "components/browser_watcher/stability_data_names.h"
#include "components/browser_watcher/stability_report_extractor.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/crashpad/crashpad/client/crash_report_database.h"

namespace browser_watcher {

using base::debug::ActivityData;
using base::debug::ActivityTrackerMemoryAllocator;
using base::debug::ActivityUserData;
using base::debug::GlobalActivityTracker;
using base::debug::ThreadActivityTracker;
using base::File;
using base::FilePersistentMemoryAllocator;
using base::MemoryMappedFile;
using base::PersistentMemoryAllocator;
using base::WrapUnique;
using crashpad::CrashReportDatabase;
using crashpad::Settings;
using crashpad::UUID;
using testing::_;
using testing::DoAll;
using testing::Return;
using testing::SetArgPointee;

namespace {

const char kProductName[] = "TestProduct";
const char kVersionNumber[] = "TestVersionNumber";
const char kChannelName[] = "TestChannel";

// The tracker creates some data entries internally.
const size_t kInternalProcessDatums = 1;

void ContainsKeyValue(
    const google::protobuf::Map<std::string, TypedValue>& data,
    const std::string& key,
    const std::string& value) {
  auto it = data.find(key);
  ASSERT_TRUE(it != data.end());
  EXPECT_EQ(TypedValue::kStringValue, it->second.value_case());
  EXPECT_EQ(value, it->second.string_value());
}

// Exposes a public constructor in order to create a dummy database.
class MockCrashReportDatabase : public CrashReportDatabase {
 public:
  MockCrashReportDatabase() {}
  MOCK_METHOD0(GetSettings, Settings*());
  MOCK_METHOD1(PrepareNewCrashReport,
               CrashReportDatabase::CrashReportDatabase::OperationStatus(
                   NewReport** report));
  MOCK_METHOD2(FinishedWritingCrashReport,
               CrashReportDatabase::CrashReportDatabase::OperationStatus(
                   CrashReportDatabase::NewReport* report,
                   crashpad::UUID* uuid));
  MOCK_METHOD1(ErrorWritingCrashReport,
               CrashReportDatabase::CrashReportDatabase::OperationStatus(
                   NewReport* report));
  MOCK_METHOD2(LookUpCrashReport,
               CrashReportDatabase::CrashReportDatabase::OperationStatus(
                   const UUID& uuid,
                   Report* report));
  MOCK_METHOD1(
      GetPendingReports,
      CrashReportDatabase::OperationStatus(std::vector<Report>* reports));
  MOCK_METHOD1(
      GetCompletedReports,
      CrashReportDatabase::OperationStatus(std::vector<Report>* reports));
  MOCK_METHOD2(GetReportForUploading,
               CrashReportDatabase::OperationStatus(const UUID& uuid,
                                                    const Report** report));
  MOCK_METHOD3(RecordUploadAttempt,
               CrashReportDatabase::OperationStatus(const Report* report,
                                                    bool successful,
                                                    const std::string& id));
  MOCK_METHOD2(SkipReportUpload,
               CrashReportDatabase::OperationStatus(
                   const UUID& uuid,
                   crashpad::Metrics::CrashSkippedReason reason));
  MOCK_METHOD1(DeleteReport,
               CrashReportDatabase::OperationStatus(const UUID& uuid));
  MOCK_METHOD1(RequestUpload,
               CrashReportDatabase::OperationStatus(const UUID& uuid));
};

// Used for testing CollectAndSubmitAllPendingReports.
class MockPostmortemReportCollector : public PostmortemReportCollector {
 public:
  MockPostmortemReportCollector()
      : PostmortemReportCollector(kProductName,
                                  kVersionNumber,
                                  kChannelName,
                                  nullptr) {}

  MOCK_METHOD3(GetDebugStateFilePaths,
               std::vector<base::FilePath>(
                   const base::FilePath& debug_info_dir,
                   const base::FilePath::StringType& debug_file_pattern,
                   const std::set<base::FilePath>&));
  MOCK_METHOD2(CollectOneReport,
               CollectionStatus(const base::FilePath&,
                                StabilityReport* report));
  MOCK_METHOD4(WriteReportToMinidump,
               bool(StabilityReport* report,
                    const crashpad::UUID& client_id,
                    const crashpad::UUID& report_id,
                    base::PlatformFile minidump_file));
};

class MockSystemSessionAnalyzer : public SystemSessionAnalyzer {
 public:
  MockSystemSessionAnalyzer() : SystemSessionAnalyzer(10U) {}
  MOCK_METHOD1(IsSessionUnclean, Status(base::Time timestamp));
};

// Checks if two proto messages are the same based on their serializations. Note
// this only works if serialization is deterministic, which is not guaranteed.
// In practice, serialization is deterministic (even for protocol buffers with
// maps) and such matchers are common in the Chromium code base. Also note that
// in the context of this test, false positive matches are the problem and these
// are not possible (otherwise serialization would be ambiguous). False
// negatives would lead to test failure and developer action. Alternatives are:
// 1) a generic matcher (likely not possible without reflections, missing from
// lite runtime),  2) a specialized matcher or 3) implementing deterministic
// serialization.
// TODO(manzagop): switch a matcher with guarantees.
MATCHER_P(EqualsProto, message, "") {
  std::string expected_serialized;
  std::string actual_serialized;
  message.SerializeToString(&expected_serialized);
  arg.SerializeToString(&actual_serialized);
  return expected_serialized == actual_serialized;
}

}  // namespace

class PostmortemReportCollectorCollectAndSubmitAllPendingReportsTest
    : public testing::Test {
 public:
  void SetUpTest(bool system_session_clean) {
    // Create a dummy debug file.
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    debug_file_ = temp_dir_.GetPath().AppendASCII("foo-1.pma");
    {
      base::ScopedFILE file(base::OpenFile(debug_file_, "w"));
      ASSERT_NE(file.get(), nullptr);
    }
    ASSERT_TRUE(base::PathExists(debug_file_));

    // Expect collection of the debug file paths.
    debug_file_pattern_ = FILE_PATH_LITERAL("foo-*.pma");
    std::vector<base::FilePath> debug_files{debug_file_};
    EXPECT_CALL(collector_,
                GetDebugStateFilePaths(debug_file_.DirName(),
                                       debug_file_pattern_, no_excluded_files_))
        .Times(1)
        .WillOnce(Return(debug_files));

    EXPECT_CALL(database_, GetSettings()).Times(1).WillOnce(Return(nullptr));

    // Expect a single collection call.
    StabilityReport report;
    report.mutable_system_state()->set_session_state(
        system_session_clean ? SystemState::CLEAN : SystemState::UNCLEAN);
    EXPECT_CALL(collector_, CollectOneReport(debug_file_, _))
        .Times(1)
        .WillOnce(DoAll(SetArgPointee<1>(report), Return(SUCCESS)));

    // Expect the call to write the proto to a minidump. This involves
    // requesting a report from the crashpad database, writing the report, then
    // finalizing it with the database.
    base::FilePath minidump_path = temp_dir_.GetPath().AppendASCII("foo-1.dmp");
    base::File minidump_file(
        minidump_path, base::File::FLAG_CREATE | base::File::File::FLAG_WRITE);
    crashpad::UUID new_report_uuid;
    new_report_uuid.InitializeWithNew();
    crashpad_report_ = {minidump_file.GetPlatformFile(), new_report_uuid,
                        minidump_path};
    EXPECT_CALL(database_, PrepareNewCrashReport(_))
        .Times(1)
        .WillOnce(DoAll(SetArgPointee<0>(&crashpad_report_),
                        Return(CrashReportDatabase::kNoError)));

    EXPECT_CALL(collector_,
                WriteReportToMinidump(_, _, _, minidump_file.GetPlatformFile()))
        .Times(1)
        .WillOnce(Return(true));
  }
  void ValidateHistograms(int unclean_cnt, int unclean_system_cnt) {
    histogram_tester_.ExpectTotalCount(
        "ActivityTracker.Collect.StabilityFileCount", 1);
    histogram_tester_.ExpectBucketCount(
        "ActivityTracker.Collect.StabilityFileCount", 1, 1);
    histogram_tester_.ExpectTotalCount(
        "ActivityTracker.Collect.UncleanShutdownCount", 1);
    histogram_tester_.ExpectBucketCount(
        "ActivityTracker.Collect.UncleanShutdownCount", unclean_cnt, 1);
    histogram_tester_.ExpectTotalCount(
        "ActivityTracker.Collect.UncleanSystemCount", 1);
    histogram_tester_.ExpectBucketCount(
        "ActivityTracker.Collect.UncleanSystemCount", unclean_system_cnt, 1);
  }
  void CollectReports(bool is_session_clean) {
    SetUpTest(is_session_clean);

    EXPECT_CALL(database_, FinishedWritingCrashReport(&crashpad_report_, _))
        .Times(1)
        .WillOnce(Return(CrashReportDatabase::kNoError));

    // Run the test.
    int success_cnt = collector_.CollectAndSubmitAllPendingReports(
        debug_file_.DirName(), debug_file_pattern_, no_excluded_files_,
        &database_);
    ASSERT_EQ(1, success_cnt);
    ASSERT_FALSE(base::PathExists(debug_file_));
  }

 protected:
  base::HistogramTester histogram_tester_;
  base::ScopedTempDir temp_dir_;
  base::FilePath debug_file_;
  MockCrashReportDatabase database_;
  MockPostmortemReportCollector collector_;
  base::FilePath::StringType debug_file_pattern_;
  std::set<base::FilePath> no_excluded_files_;
  CrashReportDatabase::NewReport crashpad_report_;
};

TEST_F(PostmortemReportCollectorCollectAndSubmitAllPendingReportsTest,
       CollectAndSubmitAllPendingReportsCleanSession) {
  CollectReports(true);
  int expected_unclean = 1;
  int expected_system_unclean = 0;
  ValidateHistograms(expected_unclean, expected_system_unclean);
}

TEST_F(PostmortemReportCollectorCollectAndSubmitAllPendingReportsTest,
       CollectAndSubmitAllPendingReportsUncleanSession) {
  CollectReports(false);
  int expected_unclean = 1;
  int expected_system_unclean = 1;
  ValidateHistograms(expected_unclean, expected_system_unclean);
}

TEST_F(PostmortemReportCollectorCollectAndSubmitAllPendingReportsTest,
       CollectAndSubmitAllPendingReportsStuckFile) {
  SetUpTest(true);

  // Open the stability debug file to prevent its deletion.
  base::ScopedFILE file(base::OpenFile(debug_file_, "w"));
  ASSERT_NE(file.get(), nullptr);

  // Expect Crashpad is notified of an error writing the crash report.
  EXPECT_CALL(database_, ErrorWritingCrashReport(&crashpad_report_))
      .Times(1)
      .WillOnce(Return(CrashReportDatabase::kNoError));

  // Run the test.
  int success_cnt = collector_.CollectAndSubmitAllPendingReports(
      debug_file_.DirName(), debug_file_pattern_, no_excluded_files_,
      &database_);
  ASSERT_EQ(0, success_cnt);
  ASSERT_TRUE(base::PathExists(debug_file_));

  int expected_unclean = 0;
  int expected_system_unclean = 0;
  ValidateHistograms(expected_unclean, expected_system_unclean);
}

TEST(PostmortemReportCollectorTest, GetDebugStateFilePaths) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());

  // Create files.
  std::vector<base::FilePath> expected_paths;
  std::set<base::FilePath> excluded_paths;
  {
    // Matches the pattern.
    base::FilePath path = temp_dir.GetPath().AppendASCII("foo1.pma");
    base::ScopedFILE file(base::OpenFile(path, "w"));
    ASSERT_NE(file.get(), nullptr);
    expected_paths.push_back(path);

    // Matches the pattern, but is excluded.
    path = temp_dir.GetPath().AppendASCII("foo2.pma");
    file.reset(base::OpenFile(path, "w"));
    ASSERT_NE(file.get(), nullptr);
    ASSERT_TRUE(excluded_paths.insert(path).second);

    // Matches the pattern.
    path = temp_dir.GetPath().AppendASCII("foo3.pma");
    file.reset(base::OpenFile(path, "w"));
    ASSERT_NE(file.get(), nullptr);
    expected_paths.push_back(path);

    // Does not match the pattern.
    path = temp_dir.GetPath().AppendASCII("bar.baz");
    file.reset(base::OpenFile(path, "w"));
    ASSERT_NE(file.get(), nullptr);
  }

  PostmortemReportCollector collector(kProductName, kVersionNumber,
                                      kChannelName, nullptr);
  EXPECT_THAT(
      collector.GetDebugStateFilePaths(
          temp_dir.GetPath(), FILE_PATH_LITERAL("foo*.pma"), excluded_paths),
      testing::UnorderedElementsAreArray(expected_paths));
}

TEST(PostmortemReportCollectorTest, CollectEmptyFile) {
  // Create an empty file.
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  base::FilePath file_path = temp_dir.GetPath().AppendASCII("empty.pma");
  {
    base::ScopedFILE file(base::OpenFile(file_path, "w"));
    ASSERT_NE(file.get(), nullptr);
  }
  ASSERT_TRUE(PathExists(file_path));

  // Validate collection: an empty file cannot suppport an analyzer.
  PostmortemReportCollector collector(kProductName, kVersionNumber,
                                      kChannelName, nullptr);
  StabilityReport report;
  ASSERT_EQ(ANALYZER_CREATION_FAILED,
            collector.CollectOneReport(file_path, &report));
}

TEST(PostmortemReportCollectorTest, CollectRandomFile) {
  // Create a file with content we don't expect to be valid for a debug file.
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  base::FilePath file_path =
      temp_dir.GetPath().AppendASCII("invalid_content.pma");
  {
    base::ScopedFILE file(base::OpenFile(file_path, "w"));
    ASSERT_NE(file.get(), nullptr);
    // Assuming this size is greater than the minimum size of a debug file.
    std::vector<uint8_t> data(1024);
    for (size_t i = 0; i < data.size(); ++i)
      data[i] = i % UINT8_MAX;
    ASSERT_EQ(data.size(),
              fwrite(&data.at(0), sizeof(uint8_t), data.size(), file.get()));
  }
  ASSERT_TRUE(PathExists(file_path));

  // Validate collection: random content appears as though there is not
  // stability data.
  PostmortemReportCollector collector(kProductName, kVersionNumber,
                                      kChannelName, nullptr);
  StabilityReport report;
  ASSERT_NE(SUCCESS, collector.CollectOneReport(file_path, &report));
}

namespace {

// Parameters for the activity tracking.
const size_t kFileSize = 64 << 10;  // 64 KiB
const int kStackDepth = 6;
const uint64_t kAllocatorId = 0;
const char kAllocatorName[] = "PostmortemReportCollectorCollectionTest";
const uint64_t kTaskSequenceNum = 42;
const uintptr_t kTaskOrigin = 1000U;
const uintptr_t kLockAddress = 1001U;
const uintptr_t kEventAddress = 1002U;
const int kThreadId = 43;
const int kProcessId = 44;
const int kAnotherThreadId = 45;
const uint32_t kGenericId = 46U;
const int32_t kGenericData = 47;

}  // namespace

// Sets up a file backed thread tracker for direct access. A
// GlobalActivityTracker is not created, meaning there is no risk of
// the instrumentation interfering with the file's content.
class PostmortemReportCollectorCollectionTest : public testing::Test {
 public:
  // Create a proper debug file.
  void SetUp() override {
    testing::Test::SetUp();

    // Create a file backed allocator.
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    debug_file_path_ = temp_dir_.GetPath().AppendASCII("debug_file.pma");
    allocator_ = CreateAllocator();
    ASSERT_NE(nullptr, allocator_);

    size_t tracker_mem_size =
        ThreadActivityTracker::SizeForStackDepth(kStackDepth);
    ASSERT_GT(kFileSize, tracker_mem_size);

    // Create a tracker.
    tracker_ = CreateTracker(allocator_.get(), tracker_mem_size);
    ASSERT_NE(nullptr, tracker_);
    ASSERT_TRUE(tracker_->IsValid());
  }

  std::unique_ptr<PersistentMemoryAllocator> CreateAllocator() {
    // Create the memory mapped file.
    std::unique_ptr<MemoryMappedFile> mmfile(new MemoryMappedFile());
    bool success = mmfile->Initialize(
        File(debug_file_path_, File::FLAG_CREATE | File::FLAG_READ |
                                   File::FLAG_WRITE | File::FLAG_SHARE_DELETE),
        {0, static_cast<int64_t>(kFileSize)},
        MemoryMappedFile::READ_WRITE_EXTEND);
    if (!success || !mmfile->IsValid())
      return nullptr;

    // Create a persistent memory allocator.
    if (!FilePersistentMemoryAllocator::IsFileAcceptable(*mmfile, true))
      return nullptr;
    return WrapUnique(new FilePersistentMemoryAllocator(
        std::move(mmfile), kFileSize, kAllocatorId, kAllocatorName, false));
  }

  std::unique_ptr<ThreadActivityTracker> CreateTracker(
      PersistentMemoryAllocator* allocator,
      size_t tracker_mem_size) {
    // Allocate a block of memory for the tracker to use.
    PersistentMemoryAllocator::Reference mem_reference = allocator->Allocate(
        tracker_mem_size, GlobalActivityTracker::kTypeIdActivityTracker);
    if (mem_reference == 0U)
      return nullptr;

    // Get the memory's base address.
    void* mem_base = allocator->GetAsArray<char>(
        mem_reference, GlobalActivityTracker::kTypeIdActivityTracker,
        PersistentMemoryAllocator::kSizeAny);
    if (mem_base == nullptr)
      return nullptr;

    // Make the allocation iterable so it can be found by other processes.
    allocator->MakeIterable(mem_reference);

    return WrapUnique(new ThreadActivityTracker(mem_base, tracker_mem_size));
  }

  const base::FilePath& debug_file_path() const { return debug_file_path_; }

 protected:
  base::ScopedTempDir temp_dir_;
  base::FilePath debug_file_path_;

  std::unique_ptr<PersistentMemoryAllocator> allocator_;
  std::unique_ptr<ThreadActivityTracker> tracker_;
};

TEST_F(PostmortemReportCollectorCollectionTest, CollectSuccess) {
  // Create some activity data.
  tracker_->PushActivity(reinterpret_cast<void*>(kTaskOrigin),
                         base::debug::Activity::ACT_TASK_RUN,
                         ActivityData::ForTask(kTaskSequenceNum));
  tracker_->PushActivity(
      nullptr, base::debug::Activity::ACT_LOCK_ACQUIRE,
      ActivityData::ForLock(reinterpret_cast<void*>(kLockAddress)));
  ThreadActivityTracker::ActivityId activity_id = tracker_->PushActivity(
      nullptr, base::debug::Activity::ACT_EVENT_WAIT,
      ActivityData::ForEvent(reinterpret_cast<void*>(kEventAddress)));
  tracker_->PushActivity(nullptr, base::debug::Activity::ACT_THREAD_JOIN,
                         ActivityData::ForThread(kThreadId));
  tracker_->PushActivity(nullptr, base::debug::Activity::ACT_PROCESS_WAIT,
                         ActivityData::ForProcess(kProcessId));
  tracker_->PushActivity(nullptr, base::debug::Activity::ACT_GENERIC,
                         ActivityData::ForGeneric(kGenericId, kGenericData));
  // Note: this exceeds the activity stack's capacity.
  tracker_->PushActivity(nullptr, base::debug::Activity::ACT_THREAD_JOIN,
                         ActivityData::ForThread(kAnotherThreadId));

  // Add some user data.
  ActivityTrackerMemoryAllocator user_data_allocator(
      allocator_.get(), GlobalActivityTracker::kTypeIdUserDataRecord,
      GlobalActivityTracker::kTypeIdUserDataRecordFree, 1024U, 10U, false);
  std::unique_ptr<ActivityUserData> user_data =
      tracker_->GetUserData(activity_id, &user_data_allocator);
  user_data->SetInt("some_int", 42);

  // Validate collection returns the expected report.
  PostmortemReportCollector collector(kProductName, kVersionNumber,
                                      kChannelName, nullptr);
  StabilityReport report;
  ASSERT_EQ(SUCCESS, collector.CollectOneReport(debug_file_path(), &report));

  // Validate the report.
  ASSERT_EQ(1, report.process_states_size());
  const ProcessState& process_state = report.process_states(0);
  EXPECT_EQ(base::GetCurrentProcId(), process_state.process_id());
  ASSERT_EQ(1, process_state.threads_size());

  const ThreadState& thread_state = process_state.threads(0);
  EXPECT_EQ(base::PlatformThread::GetName(), thread_state.thread_name());
#if defined(OS_WIN)
  EXPECT_EQ(base::PlatformThread::CurrentId(), thread_state.thread_id());
#elif defined(OS_POSIX)
  EXPECT_EQ(base::PlatformThread::CurrentHandle().platform_handle(),
            thread_state.thread_id());
#endif

  EXPECT_EQ(7, thread_state.activity_count());
  ASSERT_EQ(6, thread_state.activities_size());
  {
    const Activity& activity = thread_state.activities(0);
    EXPECT_EQ(Activity::ACT_TASK_RUN, activity.type());
    EXPECT_EQ(kTaskOrigin, activity.origin_address());
    EXPECT_EQ(kTaskSequenceNum, activity.task_sequence_id());
    EXPECT_EQ(0U, activity.user_data().size());
  }
  {
    const Activity& activity = thread_state.activities(1);
    EXPECT_EQ(Activity::ACT_LOCK_ACQUIRE, activity.type());
    EXPECT_EQ(kLockAddress, activity.lock_address());
    EXPECT_EQ(0U, activity.user_data().size());
  }
  {
    const Activity& activity = thread_state.activities(2);
    EXPECT_EQ(Activity::ACT_EVENT_WAIT, activity.type());
    EXPECT_EQ(kEventAddress, activity.event_address());
    ASSERT_EQ(1U, activity.user_data().size());
    ASSERT_TRUE(base::ContainsKey(activity.user_data(), "some_int"));
    EXPECT_EQ(TypedValue::kSignedValue,
              activity.user_data().at("some_int").value_case());
    EXPECT_EQ(42, activity.user_data().at("some_int").signed_value());
  }
  {
    const Activity& activity = thread_state.activities(3);
    EXPECT_EQ(Activity::ACT_THREAD_JOIN, activity.type());
    EXPECT_EQ(kThreadId, activity.thread_id());
    EXPECT_EQ(0U, activity.user_data().size());
  }
  {
    const Activity& activity = thread_state.activities(4);
    EXPECT_EQ(Activity::ACT_PROCESS_WAIT, activity.type());
    EXPECT_EQ(kProcessId, activity.process_id());
    EXPECT_EQ(0U, activity.user_data().size());
  }
  {
    const Activity& activity = thread_state.activities(5);
    EXPECT_EQ(Activity::ACT_GENERIC, activity.type());
    EXPECT_EQ(kGenericId, activity.generic_id());
    EXPECT_EQ(kGenericData, activity.generic_data());
    EXPECT_EQ(0U, activity.user_data().size());
  }
}

class PostmortemReportCollectorCollectionFromGlobalTrackerTest
    : public testing::Test {
 public:
  const int kMemorySize = 1 << 20;  // 1MiB

  PostmortemReportCollectorCollectionFromGlobalTrackerTest() {}
  ~PostmortemReportCollectorCollectionFromGlobalTrackerTest() override {
    GlobalActivityTracker* global_tracker = GlobalActivityTracker::Get();
    if (global_tracker) {
      global_tracker->ReleaseTrackerForCurrentThreadForTesting();
      delete global_tracker;
    }
  }

  void SetUp() override {
    testing::Test::SetUp();

    // Set up a debug file path.
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    debug_file_path_ = temp_dir_.GetPath().AppendASCII("debug.pma");
  }

  const base::FilePath& debug_file_path() { return debug_file_path_; }

 protected:
  base::ScopedTempDir temp_dir_;
  base::FilePath debug_file_path_;
};

TEST_F(PostmortemReportCollectorCollectionFromGlobalTrackerTest,
       LogCollection) {
  // Record some log messages.
  GlobalActivityTracker::CreateWithFile(debug_file_path(), kMemorySize, 0ULL,
                                        "", 3);
  GlobalActivityTracker::Get()->RecordLogMessage("hello world");
  GlobalActivityTracker::Get()->RecordLogMessage("foo bar");

  // Collect the stability report.
  PostmortemReportCollector collector(kProductName, kVersionNumber,
                                      kChannelName, nullptr);
  StabilityReport report;
  ASSERT_EQ(SUCCESS, collector.CollectOneReport(debug_file_path(), &report));

  // Validate the report's log content.
  ASSERT_EQ(2, report.log_messages_size());
  ASSERT_EQ("hello world", report.log_messages(0));
  ASSERT_EQ("foo bar", report.log_messages(1));
}

TEST_F(PostmortemReportCollectorCollectionFromGlobalTrackerTest,
       ProcessUserDataCollection) {
  const char string1[] = "foo";
  const char string2[] = "bar";

  // Record some process user data.
  GlobalActivityTracker::CreateWithFile(debug_file_path(), kMemorySize, 0ULL,
                                        "", 3);
  ActivityUserData& process_data = GlobalActivityTracker::Get()->process_data();
  ActivityUserData::Snapshot snapshot;
  ASSERT_TRUE(process_data.CreateSnapshot(&snapshot));
  ASSERT_EQ(kInternalProcessDatums, snapshot.size());
  process_data.Set("raw", "foo", 3);
  process_data.SetString("string", "bar");
  process_data.SetChar("char", '9');
  process_data.SetInt("int", -9999);
  process_data.SetUint("uint", 9999);
  process_data.SetBool("bool", true);
  process_data.SetReference("ref", string1, strlen(string1));
  process_data.SetStringReference("sref", string2);

  // Collect the stability report.
  PostmortemReportCollector collector(kProductName, kVersionNumber,
                                      kChannelName, nullptr);
  StabilityReport report;
  ASSERT_EQ(SUCCESS, collector.CollectOneReport(debug_file_path(), &report));

  // Validate the report's user data.
  const auto& collected_data = report.global_data();
  ASSERT_EQ(kInternalProcessDatums + 12U, collected_data.size());

  ASSERT_TRUE(base::ContainsKey(collected_data, "raw"));
  EXPECT_EQ(TypedValue::kBytesValue, collected_data.at("raw").value_case());
  EXPECT_EQ("foo", collected_data.at("raw").bytes_value());

  ASSERT_TRUE(base::ContainsKey(collected_data, "string"));
  EXPECT_EQ(TypedValue::kStringValue, collected_data.at("string").value_case());
  EXPECT_EQ("bar", collected_data.at("string").string_value());

  ASSERT_TRUE(base::ContainsKey(collected_data, "char"));
  EXPECT_EQ(TypedValue::kCharValue, collected_data.at("char").value_case());
  EXPECT_EQ("9", collected_data.at("char").char_value());

  ASSERT_TRUE(base::ContainsKey(collected_data, "int"));
  EXPECT_EQ(TypedValue::kSignedValue, collected_data.at("int").value_case());
  EXPECT_EQ(-9999, collected_data.at("int").signed_value());

  ASSERT_TRUE(base::ContainsKey(collected_data, "uint"));
  EXPECT_EQ(TypedValue::kUnsignedValue, collected_data.at("uint").value_case());
  EXPECT_EQ(9999U, collected_data.at("uint").unsigned_value());

  ASSERT_TRUE(base::ContainsKey(collected_data, "bool"));
  EXPECT_EQ(TypedValue::kBoolValue, collected_data.at("bool").value_case());
  EXPECT_TRUE(collected_data.at("bool").bool_value());

  ASSERT_TRUE(base::ContainsKey(collected_data, "ref"));
  EXPECT_EQ(TypedValue::kBytesReference, collected_data.at("ref").value_case());
  const TypedValue::Reference& ref = collected_data.at("ref").bytes_reference();
  EXPECT_EQ(reinterpret_cast<uintptr_t>(string1), ref.address());
  EXPECT_EQ(strlen(string1), static_cast<uint64_t>(ref.size()));

  ASSERT_TRUE(base::ContainsKey(collected_data, "sref"));
  EXPECT_EQ(TypedValue::kStringReference,
            collected_data.at("sref").value_case());
  const TypedValue::Reference& sref =
      collected_data.at("sref").string_reference();
  EXPECT_EQ(reinterpret_cast<uintptr_t>(string2), sref.address());
  EXPECT_EQ(strlen(string2), static_cast<uint64_t>(sref.size()));

  // Reporter's version details.
  ContainsKeyValue(collected_data, kStabilityReporterProduct, kProductName);
  ContainsKeyValue(collected_data, kStabilityReporterChannel, kChannelName);
#if defined(ARCH_CPU_X86)
  ContainsKeyValue(collected_data, kStabilityReporterPlatform, "Win32");
#elif defined(ARCH_CPU_X86_64)
  ContainsKeyValue(collected_data, kStabilityReporterPlatform, "Win64");
#endif
  ContainsKeyValue(collected_data, kStabilityReporterVersion, kVersionNumber);
}

TEST_F(PostmortemReportCollectorCollectionFromGlobalTrackerTest,
       FieldTrialCollection) {
  // Record some data.
  GlobalActivityTracker::CreateWithFile(debug_file_path(), kMemorySize, 0ULL,
                                        "", 3);
  ActivityUserData& process_data = GlobalActivityTracker::Get()->process_data();
  process_data.SetString("string", "bar");
  process_data.SetString("FieldTrial.string", "bar");
  process_data.SetString("FieldTrial.foo", "bar");

  // Collect the stability report.
  PostmortemReportCollector collector(kProductName, kVersionNumber,
                                      kChannelName, nullptr);
  StabilityReport report;
  ASSERT_EQ(SUCCESS, collector.CollectOneReport(debug_file_path(), &report));

  // Validate the report's experiment and global data.
  ASSERT_EQ(2, report.field_trials_size());
  EXPECT_NE(0U, report.field_trials(0).name_id());
  EXPECT_NE(0U, report.field_trials(0).group_id());
  EXPECT_NE(0U, report.field_trials(1).name_id());
  EXPECT_EQ(report.field_trials(0).group_id(),
            report.field_trials(1).group_id());

  // Expect 5 key/value pairs (including product details).
  const auto& collected_data = report.global_data();
  EXPECT_EQ(kInternalProcessDatums + 5U, collected_data.size());
  EXPECT_TRUE(base::ContainsKey(collected_data, "string"));
}

TEST_F(PostmortemReportCollectorCollectionFromGlobalTrackerTest,
       ModuleCollection) {
  // Record some module information.
  GlobalActivityTracker::CreateWithFile(debug_file_path(), kMemorySize, 0ULL,
                                        "", 3);

  base::debug::GlobalActivityTracker::ModuleInfo module_info = {};
  module_info.is_loaded = true;
  module_info.address = 0x123456;
  module_info.load_time = 1111LL;
  module_info.size = 0x2d000;
  module_info.timestamp = 0xCAFECAFE;
  module_info.age = 1;
  crashpad::UUID debug_uuid;
  debug_uuid.InitializeFromString("11223344-5566-7788-abcd-0123456789ab");
  memcpy(module_info.identifier, &debug_uuid, sizeof(module_info.identifier));
  module_info.file = "foo";
  module_info.debug_file = "bar";

  GlobalActivityTracker::Get()->RecordModuleInfo(module_info);

  // Collect the stability report.
  PostmortemReportCollector collector(kProductName, kVersionNumber,
                                      kChannelName, nullptr);
  StabilityReport report;
  ASSERT_EQ(SUCCESS, collector.CollectOneReport(debug_file_path(), &report));

  // Validate the report's modules content.
  ASSERT_EQ(1, report.process_states_size());
  const ProcessState& process_state = report.process_states(0);
  ASSERT_EQ(1, process_state.modules_size());

  const CodeModule collected_module = process_state.modules(0);
  EXPECT_EQ(module_info.address,
            static_cast<uintptr_t>(collected_module.base_address()));
  EXPECT_EQ(module_info.size, static_cast<size_t>(collected_module.size()));
  EXPECT_EQ(module_info.file, collected_module.code_file());
  EXPECT_EQ("CAFECAFE2d000", collected_module.code_identifier());
  EXPECT_EQ(module_info.debug_file, collected_module.debug_file());
  EXPECT_EQ("1122334455667788ABCD0123456789AB1",
            collected_module.debug_identifier());
  EXPECT_EQ("", collected_module.version());
  EXPECT_EQ(0LL, collected_module.shrink_down_delta());
  EXPECT_EQ(!module_info.is_loaded, collected_module.is_unloaded());
}

TEST_F(PostmortemReportCollectorCollectionFromGlobalTrackerTest,
       SystemStateTest) {
  // Setup.
  GlobalActivityTracker::CreateWithFile(debug_file_path(), kMemorySize, 0ULL,
                                        "", 3);
  ActivityUserData& process_data = GlobalActivityTracker::Get()->process_data();
  process_data.SetInt(kStabilityStartTimestamp, 12345LL);

  // Collect.
  MockSystemSessionAnalyzer analyzer;
  EXPECT_CALL(analyzer,
              IsSessionUnclean(base::Time::FromInternalValue(12345LL)))
      .Times(1)
      .WillOnce(Return(SystemSessionAnalyzer::CLEAN));
  PostmortemReportCollector collector(kProductName, kVersionNumber,
                                      kChannelName, &analyzer);
  StabilityReport report;
  ASSERT_EQ(SUCCESS, collector.CollectOneReport(debug_file_path(), &report));

  // Validate the report.
  ASSERT_EQ(SystemState::CLEAN, report.system_state().session_state());
}

}  // namespace browser_watcher
