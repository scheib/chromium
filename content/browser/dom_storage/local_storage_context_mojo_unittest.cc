// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/dom_storage/local_storage_context_mojo.h"

#include "base/files/file_enumerator.h"
#include "base/files/scoped_temp_dir.h"
#include "base/run_loop.h"
#include "base/strings/utf_string_conversions.h"
#include "components/filesystem/public/interfaces/file_system.mojom.h"
#include "components/leveldb/public/cpp/util.h"
#include "content/browser/dom_storage/dom_storage_area.h"
#include "content/browser/dom_storage/dom_storage_context_impl.h"
#include "content/browser/dom_storage/dom_storage_namespace.h"
#include "content/browser/dom_storage/dom_storage_task_runner.h"
#include "content/common/dom_storage/dom_storage_types.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/local_storage_usage_info.h"
#include "content/public/test/test_browser_thread_bundle.h"
#include "content/test/mock_leveldb_database.h"
#include "mojo/public/cpp/bindings/associated_binding.h"
#include "mojo/public/cpp/bindings/binding.h"
#include "mojo/public/cpp/bindings/binding_set.h"
#include "services/file/file_service.h"
#include "services/file/public/interfaces/constants.mojom.h"
#include "services/file/user_id_map.h"
#include "services/service_manager/public/cpp/service_context.h"
#include "services/service_manager/public/cpp/service_test.h"
#include "services/service_manager/public/interfaces/service_factory.mojom.h"
#include "storage/browser/test/mock_special_storage_policy.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/leveldatabase/env_chromium.h"
#include "third_party/leveldatabase/src/include/leveldb/db.h"

using leveldb::StdStringToUint8Vector;
using leveldb::Uint8VectorToStdString;

namespace content {

namespace {

void NoOpSuccess(bool success) {}

void GetStorageUsageCallback(const base::Closure& callback,
                             std::vector<LocalStorageUsageInfo>* out_result,
                             std::vector<LocalStorageUsageInfo> result) {
  *out_result = std::move(result);
  callback.Run();
}

void GetCallback(const base::Closure& callback,
                 bool* success_out,
                 std::vector<uint8_t>* value_out,
                 bool success,
                 const std::vector<uint8_t>& value) {
  *success_out = success;
  *value_out = value;
  callback.Run();
}

void NoOpGet(bool success, const std::vector<uint8_t>& value) {}

class TestLevelDBObserver : public mojom::LevelDBObserver {
 public:
  struct Observation {
    enum { kAdd, kChange, kDelete, kDeleteAll } type;
    std::string key;
    std::string old_value;
    std::string new_value;
    std::string source;
  };

  TestLevelDBObserver() : binding_(this) {}

  mojom::LevelDBObserverAssociatedPtrInfo Bind() {
    mojom::LevelDBObserverAssociatedPtrInfo ptr_info;
    binding_.Bind(mojo::MakeRequest(&ptr_info));
    return ptr_info;
  }

  const std::vector<Observation>& observations() { return observations_; }

 private:
  void KeyAdded(const std::vector<uint8_t>& key,
                const std::vector<uint8_t>& value,
                const std::string& source) override {
    observations_.push_back({Observation::kAdd, Uint8VectorToStdString(key), "",
                             Uint8VectorToStdString(value), source});
  }
  void KeyChanged(const std::vector<uint8_t>& key,
                  const std::vector<uint8_t>& new_value,
                  const std::vector<uint8_t>& old_value,
                  const std::string& source) override {
    observations_.push_back({Observation::kChange, Uint8VectorToStdString(key),
                             Uint8VectorToStdString(old_value),
                             Uint8VectorToStdString(new_value), source});
  }
  void KeyDeleted(const std::vector<uint8_t>& key,
                  const std::vector<uint8_t>& old_value,
                  const std::string& source) override {
    observations_.push_back({Observation::kDelete, Uint8VectorToStdString(key),
                             Uint8VectorToStdString(old_value), "", source});
  }
  void AllDeleted(const std::string& source) override {
    observations_.push_back({Observation::kDeleteAll, "", "", "", source});
  }

  std::vector<Observation> observations_;
  mojo::AssociatedBinding<mojom::LevelDBObserver> binding_;
};

}  // namespace

class LocalStorageContextMojoTest : public testing::Test {
 public:
  LocalStorageContextMojoTest()
      : db_(&mock_data_),
        db_binding_(&db_),
        task_runner_(new MockDOMStorageTaskRunner(
            base::ThreadTaskRunnerHandle::Get().get())),
        mock_special_storage_policy_(new MockSpecialStoragePolicy()) {
    EXPECT_TRUE(temp_path_.CreateUniqueTempDir());
    dom_storage_context_ = new DOMStorageContextImpl(
        temp_path_.GetPath(), base::FilePath(), nullptr, task_runner_);
  }

  ~LocalStorageContextMojoTest() override {
    if (dom_storage_context_)
      dom_storage_context_->Shutdown();
    if (context_)
      ShutdownContext();
  }

  LocalStorageContextMojo* context() {
    if (!context_) {
      context_ = new LocalStorageContextMojo(
          nullptr, task_runner_, temp_path_.GetPath(),
          base::FilePath(FILE_PATH_LITERAL("leveldb")),
          special_storage_policy());
      db_binding_.Bind(context_->DatabaseRequestForTesting());
    }
    return context_;
  }

  void ShutdownContext() {
    context_->ShutdownAndDelete();
    context_ = nullptr;
    base::RunLoop().RunUntilIdle();
  }

  DOMStorageNamespace* local_storage_namespace() {
    return dom_storage_context_->GetStorageNamespace(kLocalStorageNamespaceId);
  }

  MockSpecialStoragePolicy* special_storage_policy() {
    return mock_special_storage_policy_.get();
  }

  void FlushAndPurgeDOMStorageMemory() {
    dom_storage_context_->Flush();
    base::RunLoop().RunUntilIdle();
    dom_storage_context_->PurgeMemory(DOMStorageContextImpl::PURGE_AGGRESSIVE);
  }

  const std::map<std::vector<uint8_t>, std::vector<uint8_t>>& mock_data() {
    return mock_data_;
  }

  void set_mock_data(const std::string& key, const std::string& value) {
    mock_data_[StdStringToUint8Vector(key)] = StdStringToUint8Vector(value);
  }

  std::vector<LocalStorageUsageInfo> GetStorageUsageSync() {
    base::RunLoop run_loop;
    std::vector<LocalStorageUsageInfo> result;
    context()->GetStorageUsage(base::BindOnce(&GetStorageUsageCallback,
                                              run_loop.QuitClosure(), &result));
    run_loop.Run();
    return result;
  }

 private:
  TestBrowserThreadBundle thread_bundle_;
  base::ScopedTempDir temp_path_;
  std::map<std::vector<uint8_t>, std::vector<uint8_t>> mock_data_;
  MockLevelDBDatabase db_;
  mojo::AssociatedBinding<leveldb::mojom::LevelDBDatabase> db_binding_;

  scoped_refptr<MockDOMStorageTaskRunner> task_runner_;
  scoped_refptr<DOMStorageContextImpl> dom_storage_context_;

  LocalStorageContextMojo* context_ = nullptr;

  scoped_refptr<MockSpecialStoragePolicy> mock_special_storage_policy_;

  DISALLOW_COPY_AND_ASSIGN(LocalStorageContextMojoTest);
};

TEST_F(LocalStorageContextMojoTest, Basic) {
  auto key = StdStringToUint8Vector("key");
  auto value = StdStringToUint8Vector("value");

  mojom::LevelDBWrapperPtr wrapper;
  context()->OpenLocalStorage(url::Origin(GURL("http://foobar.com")),
                              MakeRequest(&wrapper));
  wrapper->Put(key, value, "source", base::Bind(&NoOpSuccess));
  wrapper.reset();

  base::RunLoop().RunUntilIdle();

  // Should have three rows of data, one for the version, one for the actual
  // data and one for metadata.
  ASSERT_EQ(3u, mock_data().size());
}

TEST_F(LocalStorageContextMojoTest, OriginsAreIndependent) {
  url::Origin origin1(GURL("http://foobar.com:123"));
  url::Origin origin2(GURL("http://foobar.com:1234"));
  auto key1 = StdStringToUint8Vector("4key");
  auto key2 = StdStringToUint8Vector("key");
  auto value = StdStringToUint8Vector("value");

  mojom::LevelDBWrapperPtr wrapper;
  context()->OpenLocalStorage(origin1, MakeRequest(&wrapper));
  wrapper->Put(key1, value, "source", base::Bind(&NoOpSuccess));
  wrapper.reset();

  context()->OpenLocalStorage(origin2, MakeRequest(&wrapper));
  wrapper->Put(key2, value, "source", base::Bind(&NoOpSuccess));
  wrapper.reset();

  base::RunLoop().RunUntilIdle();
  ASSERT_EQ(5u, mock_data().size());
}

TEST_F(LocalStorageContextMojoTest, ValidVersion) {
  set_mock_data("VERSION", "1");
  set_mock_data(std::string("_http://foobar.com") + '\x00' + "key", "value");

  mojom::LevelDBWrapperPtr wrapper;
  context()->OpenLocalStorage(url::Origin(GURL("http://foobar.com")),
                              MakeRequest(&wrapper));

  base::RunLoop run_loop;
  bool success = false;
  std::vector<uint8_t> result;
  wrapper->Get(
      StdStringToUint8Vector("key"),
      base::Bind(&GetCallback, run_loop.QuitClosure(), &success, &result));
  run_loop.Run();
  EXPECT_TRUE(success);
  EXPECT_EQ(StdStringToUint8Vector("value"), result);
}

TEST_F(LocalStorageContextMojoTest, InvalidVersion) {
  set_mock_data("VERSION", "foobar");
  set_mock_data(std::string("_http://foobar.com") + '\x00' + "key", "value");

  mojom::LevelDBWrapperPtr wrapper;
  context()->OpenLocalStorage(url::Origin(GURL("http://foobar.com")),
                              MakeRequest(&wrapper));

  base::RunLoop run_loop;
  bool success = false;
  std::vector<uint8_t> result;
  wrapper->Get(
      StdStringToUint8Vector("key"),
      base::Bind(&GetCallback, run_loop.QuitClosure(), &success, &result));
  run_loop.Run();
  EXPECT_FALSE(success);
}

TEST_F(LocalStorageContextMojoTest, VersionOnlyWrittenOnCommit) {
  mojom::LevelDBWrapperPtr wrapper;
  context()->OpenLocalStorage(url::Origin(GURL("http://foobar.com")),
                              MakeRequest(&wrapper));

  base::RunLoop run_loop;
  bool success = false;
  std::vector<uint8_t> result;
  wrapper->Get(
      StdStringToUint8Vector("key"),
      base::Bind(&GetCallback, run_loop.QuitClosure(), &success, &result));
  run_loop.Run();
  EXPECT_FALSE(success);
  wrapper.reset();

  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(mock_data().empty());
}

TEST_F(LocalStorageContextMojoTest, GetStorageUsage_NoData) {
  std::vector<LocalStorageUsageInfo> info = GetStorageUsageSync();
  EXPECT_EQ(0u, info.size());
}

TEST_F(LocalStorageContextMojoTest, GetStorageUsage_Data) {
  url::Origin origin1(GURL("http://foobar.com"));
  url::Origin origin2(GURL("http://example.com"));
  auto key1 = StdStringToUint8Vector("key1");
  auto key2 = StdStringToUint8Vector("key");
  auto value = StdStringToUint8Vector("value");

  base::Time before_write = base::Time::Now();

  mojom::LevelDBWrapperPtr wrapper;
  context()->OpenLocalStorage(origin1, MakeRequest(&wrapper));
  wrapper->Put(key1, value, "source", base::Bind(&NoOpSuccess));
  wrapper->Put(key2, value, "source", base::Bind(&NoOpSuccess));
  wrapper.reset();

  context()->OpenLocalStorage(origin2, MakeRequest(&wrapper));
  wrapper->Put(key2, value, "source", base::Bind(&NoOpSuccess));
  wrapper.reset();

  // GetStorageUsage only include committed data, so nothing at this point.
  std::vector<LocalStorageUsageInfo> info = GetStorageUsageSync();
  EXPECT_EQ(0u, info.size());

  // Make sure all data gets committed to disk.
  base::RunLoop().RunUntilIdle();

  base::Time after_write = base::Time::Now();

  info = GetStorageUsageSync();
  ASSERT_EQ(2u, info.size());
  if (url::Origin(info[0].origin) == origin2)
    std::swap(info[0], info[1]);

  EXPECT_EQ(origin1, url::Origin(info[0].origin));
  EXPECT_EQ(origin2, url::Origin(info[1].origin));
  EXPECT_LE(before_write, info[0].last_modified);
  EXPECT_LE(before_write, info[1].last_modified);
  EXPECT_GE(after_write, info[0].last_modified);
  EXPECT_GE(after_write, info[1].last_modified);
  EXPECT_GT(info[0].data_size, info[1].data_size);
}

TEST_F(LocalStorageContextMojoTest, MetaDataClearedOnDelete) {
  url::Origin origin1(GURL("http://foobar.com"));
  url::Origin origin2(GURL("http://example.com"));
  auto key = StdStringToUint8Vector("key");
  auto value = StdStringToUint8Vector("value");

  mojom::LevelDBWrapperPtr wrapper;
  context()->OpenLocalStorage(origin1, MakeRequest(&wrapper));
  wrapper->Put(key, value, "source", base::Bind(&NoOpSuccess));
  wrapper.reset();
  context()->OpenLocalStorage(origin2, MakeRequest(&wrapper));
  wrapper->Put(key, value, "source", base::Bind(&NoOpSuccess));
  wrapper.reset();
  context()->OpenLocalStorage(origin1, MakeRequest(&wrapper));
  wrapper->Delete(key, "source", base::Bind(&NoOpSuccess));
  wrapper.reset();

  // Make sure all data gets committed to disk.
  base::RunLoop().RunUntilIdle();

  // Data from origin2 should exist, including meta-data, but nothing should
  // exist for origin1.
  EXPECT_EQ(3u, mock_data().size());
  for (const auto& it : mock_data()) {
    if (Uint8VectorToStdString(it.first) == "VERSION")
      continue;
    EXPECT_EQ(std::string::npos,
              Uint8VectorToStdString(it.first).find(origin1.Serialize()));
    EXPECT_NE(std::string::npos,
              Uint8VectorToStdString(it.first).find(origin2.Serialize()));
  }
}

TEST_F(LocalStorageContextMojoTest, MetaDataClearedOnDeleteAll) {
  url::Origin origin1(GURL("http://foobar.com"));
  url::Origin origin2(GURL("http://example.com"));
  auto key = StdStringToUint8Vector("key");
  auto value = StdStringToUint8Vector("value");

  mojom::LevelDBWrapperPtr wrapper;
  context()->OpenLocalStorage(origin1, MakeRequest(&wrapper));
  wrapper->Put(key, value, "source", base::Bind(&NoOpSuccess));
  wrapper.reset();
  context()->OpenLocalStorage(origin2, MakeRequest(&wrapper));
  wrapper->Put(key, value, "source", base::Bind(&NoOpSuccess));
  wrapper.reset();

  context()->OpenLocalStorage(origin1, MakeRequest(&wrapper));
  wrapper->DeleteAll("source", base::Bind(&NoOpSuccess));
  wrapper.reset();

  // Make sure all data gets committed to disk.
  base::RunLoop().RunUntilIdle();

  // Data from origin2 should exist, including meta-data, but nothing should
  // exist for origin1.
  EXPECT_EQ(3u, mock_data().size());
  for (const auto& it : mock_data()) {
    if (Uint8VectorToStdString(it.first) == "VERSION")
      continue;
    EXPECT_EQ(std::string::npos,
              Uint8VectorToStdString(it.first).find(origin1.Serialize()));
    EXPECT_NE(std::string::npos,
              Uint8VectorToStdString(it.first).find(origin2.Serialize()));
  }
}

TEST_F(LocalStorageContextMojoTest, DeleteStorage) {
  set_mock_data("VERSION", "1");
  set_mock_data(std::string("_http://foobar.com") + '\x00' + "key", "value");

  context()->DeleteStorage(url::Origin(GURL("http://foobar.com")));
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(1u, mock_data().size());
}

TEST_F(LocalStorageContextMojoTest, DeleteStorageWithoutConnection) {
  url::Origin origin1(GURL("http://foobar.com"));
  url::Origin origin2(GURL("http://example.com"));
  auto key = StdStringToUint8Vector("key");
  auto value = StdStringToUint8Vector("value");

  mojom::LevelDBWrapperPtr wrapper;
  context()->OpenLocalStorage(origin1, MakeRequest(&wrapper));
  wrapper->Put(key, value, "source", base::Bind(&NoOpSuccess));
  wrapper.reset();

  context()->OpenLocalStorage(origin2, MakeRequest(&wrapper));
  wrapper->Put(key, value, "source", base::Bind(&NoOpSuccess));
  wrapper.reset();

  // Make sure all data gets committed to disk.
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(mock_data().empty());

  context()->DeleteStorage(origin1);
  base::RunLoop().RunUntilIdle();

  // Data from origin2 should exist, including meta-data, but nothing should
  // exist for origin1.
  EXPECT_EQ(3u, mock_data().size());
  for (const auto& it : mock_data()) {
    if (Uint8VectorToStdString(it.first) == "VERSION")
      continue;
    EXPECT_EQ(std::string::npos,
              Uint8VectorToStdString(it.first).find(origin1.Serialize()));
    EXPECT_NE(std::string::npos,
              Uint8VectorToStdString(it.first).find(origin2.Serialize()));
  }
}

TEST_F(LocalStorageContextMojoTest, DeleteStorageNotifiesWrapper) {
  url::Origin origin1(GURL("http://foobar.com"));
  url::Origin origin2(GURL("http://example.com"));
  auto key = StdStringToUint8Vector("key");
  auto value = StdStringToUint8Vector("value");

  mojom::LevelDBWrapperPtr wrapper;
  context()->OpenLocalStorage(origin1, MakeRequest(&wrapper));
  wrapper->Put(key, value, "source", base::Bind(&NoOpSuccess));
  wrapper.reset();

  context()->OpenLocalStorage(origin2, MakeRequest(&wrapper));
  wrapper->Put(key, value, "source", base::Bind(&NoOpSuccess));
  wrapper.reset();

  // Make sure all data gets committed to disk.
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(mock_data().empty());

  TestLevelDBObserver observer;
  context()->OpenLocalStorage(origin1, MakeRequest(&wrapper));
  wrapper->AddObserver(observer.Bind());
  base::RunLoop().RunUntilIdle();

  context()->DeleteStorage(origin1);
  base::RunLoop().RunUntilIdle();

  ASSERT_EQ(1u, observer.observations().size());
  EXPECT_EQ(TestLevelDBObserver::Observation::kDeleteAll,
            observer.observations()[0].type);

  // Data from origin2 should exist, including meta-data, but nothing should
  // exist for origin1.
  EXPECT_EQ(3u, mock_data().size());
  for (const auto& it : mock_data()) {
    if (Uint8VectorToStdString(it.first) == "VERSION")
      continue;
    EXPECT_EQ(std::string::npos,
              Uint8VectorToStdString(it.first).find(origin1.Serialize()));
    EXPECT_NE(std::string::npos,
              Uint8VectorToStdString(it.first).find(origin2.Serialize()));
  }
}

TEST_F(LocalStorageContextMojoTest, DeleteStorageWithPendingWrites) {
  url::Origin origin1(GURL("http://foobar.com"));
  url::Origin origin2(GURL("http://example.com"));
  auto key = StdStringToUint8Vector("key");
  auto value = StdStringToUint8Vector("value");

  mojom::LevelDBWrapperPtr wrapper;
  context()->OpenLocalStorage(origin1, MakeRequest(&wrapper));
  wrapper->Put(key, value, "source", base::Bind(&NoOpSuccess));
  wrapper.reset();

  context()->OpenLocalStorage(origin2, MakeRequest(&wrapper));
  wrapper->Put(key, value, "source", base::Bind(&NoOpSuccess));
  wrapper.reset();

  // Make sure all data gets committed to disk.
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(mock_data().empty());

  TestLevelDBObserver observer;
  context()->OpenLocalStorage(origin1, MakeRequest(&wrapper));
  wrapper->AddObserver(observer.Bind());
  wrapper->Put(StdStringToUint8Vector("key2"), value, "source",
               base::Bind(&NoOpSuccess));
  base::RunLoop().RunUntilIdle();

  context()->DeleteStorage(origin1);
  base::RunLoop().RunUntilIdle();

  ASSERT_EQ(2u, observer.observations().size());
  EXPECT_EQ(TestLevelDBObserver::Observation::kAdd,
            observer.observations()[0].type);
  EXPECT_EQ(TestLevelDBObserver::Observation::kDeleteAll,
            observer.observations()[1].type);

  // Data from origin2 should exist, including meta-data, but nothing should
  // exist for origin1.
  EXPECT_EQ(3u, mock_data().size());
  for (const auto& it : mock_data()) {
    if (Uint8VectorToStdString(it.first) == "VERSION")
      continue;
    EXPECT_EQ(std::string::npos,
              Uint8VectorToStdString(it.first).find(origin1.Serialize()));
    EXPECT_NE(std::string::npos,
              Uint8VectorToStdString(it.first).find(origin2.Serialize()));
  }
}

TEST_F(LocalStorageContextMojoTest, DeleteStorageForPhysicalOrigin) {
  url::Origin origin1a(GURL("http://foobar.com"));
  url::Origin origin1b(GURL("http-so://suborigin.foobar.com"));
  url::Origin origin2(GURL("https://foobar.com"));
  auto key = StdStringToUint8Vector("key");
  auto value = StdStringToUint8Vector("value");

  mojom::LevelDBWrapperPtr wrapper;
  context()->OpenLocalStorage(origin1a, MakeRequest(&wrapper));
  wrapper->Put(key, value, "source", base::Bind(&NoOpSuccess));
  wrapper.reset();
  context()->OpenLocalStorage(origin1b, MakeRequest(&wrapper));
  wrapper->Put(key, value, "source", base::Bind(&NoOpSuccess));
  wrapper.reset();

  context()->OpenLocalStorage(origin2, MakeRequest(&wrapper));
  wrapper->Put(key, value, "source", base::Bind(&NoOpSuccess));
  wrapper.reset();

  // Make sure all data gets committed to disk.
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(mock_data().empty());

  context()->DeleteStorageForPhysicalOrigin(origin1b);
  base::RunLoop().RunUntilIdle();

  // Data from origin2 should exist, including meta-data, but nothing should
  // exist for origin1.
  EXPECT_EQ(3u, mock_data().size());
  for (const auto& it : mock_data()) {
    if (Uint8VectorToStdString(it.first) == "VERSION")
      continue;
    EXPECT_EQ(std::string::npos,
              Uint8VectorToStdString(it.first).find(origin1a.Serialize()));
    EXPECT_EQ(std::string::npos,
              Uint8VectorToStdString(it.first).find(origin1b.Serialize()));
    EXPECT_NE(std::string::npos,
              Uint8VectorToStdString(it.first).find(origin2.Serialize()));
  }
}

TEST_F(LocalStorageContextMojoTest, Migration) {
  url::Origin origin1(GURL("http://foobar.com"));
  url::Origin origin2(GURL("http://example.com"));
  base::string16 key = base::ASCIIToUTF16("key");
  base::string16 value = base::ASCIIToUTF16("value");

  DOMStorageNamespace* local = local_storage_namespace();
  DOMStorageArea* area = local->OpenStorageArea(origin1.GetURL());
  base::NullableString16 dummy;
  area->SetItem(key, value, &dummy);
  local->CloseStorageArea(area);
  FlushAndPurgeDOMStorageMemory();

  // Opening origin2 and accessing its data should not migrate anything.
  mojom::LevelDBWrapperPtr wrapper;
  context()->OpenLocalStorage(origin2, MakeRequest(&wrapper));
  wrapper->Get(std::vector<uint8_t>(), base::Bind(&NoOpGet));
  wrapper.reset();
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(mock_data().empty());

  // Opening origin1 and accessing its data should migrate its storage.
  context()->OpenLocalStorage(origin1, MakeRequest(&wrapper));
  wrapper->Get(std::vector<uint8_t>(), base::Bind(&NoOpGet));
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(mock_data().empty());

  base::RunLoop run_loop;
  bool success = false;
  std::vector<uint8_t> result;
  wrapper->Get(
      LocalStorageContextMojo::MigrateString(key),
      base::Bind(&GetCallback, run_loop.QuitClosure(), &success, &result));
  run_loop.Run();
  EXPECT_TRUE(success);
  EXPECT_EQ(LocalStorageContextMojo::MigrateString(value), result);

  // Origin1 should no longer exist in old storage.
  area = local->OpenStorageArea(origin1.GetURL());
  ASSERT_EQ(0u, area->Length());
  local->CloseStorageArea(area);
}

TEST_F(LocalStorageContextMojoTest, ShutdownClearsData) {
  url::Origin origin1(GURL("http://foobar.com"));
  url::Origin origin2(GURL("http://example.com"));
  auto key1 = StdStringToUint8Vector("key1");
  auto key2 = StdStringToUint8Vector("key");
  auto value = StdStringToUint8Vector("value");

  mojom::LevelDBWrapperPtr wrapper;
  context()->OpenLocalStorage(origin1, MakeRequest(&wrapper));
  wrapper->Put(key1, value, "source", base::Bind(&NoOpSuccess));
  wrapper->Put(key2, value, "source", base::Bind(&NoOpSuccess));
  wrapper.reset();

  context()->OpenLocalStorage(origin2, MakeRequest(&wrapper));
  wrapper->Put(key2, value, "source", base::Bind(&NoOpSuccess));
  wrapper.reset();

  // Make sure all data gets committed to the DB.
  base::RunLoop().RunUntilIdle();

  special_storage_policy()->AddSessionOnly(origin1.GetURL());
  ShutdownContext();

  // Data from origin2 should exist, including meta-data, but nothing should
  // exist for origin1.
  EXPECT_EQ(3u, mock_data().size());
  for (const auto& it : mock_data()) {
    if (Uint8VectorToStdString(it.first) == "VERSION")
      continue;
    EXPECT_EQ(std::string::npos,
              Uint8VectorToStdString(it.first).find(origin1.Serialize()));
    EXPECT_NE(std::string::npos,
              Uint8VectorToStdString(it.first).find(origin2.Serialize()));
  }
}

namespace {

class ServiceTestClient : public service_manager::test::ServiceTestClient,
                          public service_manager::mojom::ServiceFactory {
 public:
  explicit ServiceTestClient(service_manager::test::ServiceTest* test)
      : service_manager::test::ServiceTestClient(test) {
    registry_.AddInterface<service_manager::mojom::ServiceFactory>(base::Bind(
        &ServiceTestClient::BindServiceFactoryRequest, base::Unretained(this)));
  }
  ~ServiceTestClient() override {}

 protected:
  void OnBindInterface(const service_manager::BindSourceInfo& source_info,
                       const std::string& interface_name,
                       mojo::ScopedMessagePipeHandle interface_pipe) override {
    registry_.BindInterface(source_info, interface_name,
                            std::move(interface_pipe));
  }

  void CreateService(service_manager::mojom::ServiceRequest request,
                     const std::string& name) override {
    if (name == file::mojom::kServiceName) {
      file_service_context_.reset(new service_manager::ServiceContext(
          file::CreateFileService(
              BrowserThread::GetTaskRunnerForThread(BrowserThread::FILE),
              BrowserThread::GetTaskRunnerForThread(BrowserThread::DB)),
          std::move(request)));
    }
  }

  void BindServiceFactoryRequest(
      const service_manager::BindSourceInfo& source_info,
      service_manager::mojom::ServiceFactoryRequest request) {
    service_factory_bindings_.AddBinding(this, std::move(request));
  }

 private:
  service_manager::BinderRegistry registry_;
  mojo::BindingSet<service_manager::mojom::ServiceFactory>
      service_factory_bindings_;
  std::unique_ptr<service_manager::ServiceContext> file_service_context_;
};

}  // namespace

class LocalStorageContextMojoTestWithService
    : public service_manager::test::ServiceTest {
 public:
  LocalStorageContextMojoTestWithService()
      : ServiceTest("content_unittests", false),
        thread_bundle_(TestBrowserThreadBundle::REAL_FILE_THREAD) {}
  ~LocalStorageContextMojoTestWithService() override {}

 protected:
  void SetUp() override {
    ServiceTest::SetUp();
    ASSERT_TRUE(temp_path_.CreateUniqueTempDir());
    file::AssociateServiceUserIdWithUserDir(test_userid(),
                                            temp_path_.GetPath());
  }

  void TearDown() override {
    ServiceTest::TearDown();
  }

  std::unique_ptr<service_manager::Service> CreateService() override {
    return base::MakeUnique<ServiceTestClient>(this);
  }

  const base::FilePath& temp_path() { return temp_path_.GetPath(); }

  base::FilePath FirstEntryInDir() {
    base::FileEnumerator enumerator(
        temp_path(), false /* recursive */,
        base::FileEnumerator::FILES | base::FileEnumerator::DIRECTORIES);
    return enumerator.Next();
  }

  void DoTestPut(LocalStorageContextMojo* context,
                 const std::vector<uint8_t>& key,
                 const std::vector<uint8_t>& value) {
    mojom::LevelDBWrapperPtr wrapper;
    context->OpenLocalStorage(url::Origin(GURL("http://foobar.com")),
                              MakeRequest(&wrapper));
    wrapper->Put(key, value, "source", base::Bind(&NoOpSuccess));
    wrapper.reset();
    base::RunLoop().RunUntilIdle();
  }

  bool DoTestGet(LocalStorageContextMojo* context,
                 const std::vector<uint8_t>& key,
                 std::vector<uint8_t>* result) {
    mojom::LevelDBWrapperPtr wrapper;
    context->OpenLocalStorage(url::Origin(GURL("http://foobar.com")),
                              MakeRequest(&wrapper));
    base::RunLoop run_loop;
    bool success = false;
    wrapper->Get(key, base::Bind(&GetCallback, run_loop.QuitClosure(), &success,
                                 result));
    run_loop.Run();
    return success;
  }

 private:
  TestBrowserThreadBundle thread_bundle_;
  base::ScopedTempDir temp_path_;

  DISALLOW_COPY_AND_ASSIGN(LocalStorageContextMojoTestWithService);
};

TEST_F(LocalStorageContextMojoTestWithService, InMemory) {
  auto* context = new LocalStorageContextMojo(
      connector(), nullptr, base::FilePath(), base::FilePath(), nullptr);
  auto key = StdStringToUint8Vector("key");
  auto value = StdStringToUint8Vector("value");

  mojom::LevelDBWrapperPtr wrapper;
  context->OpenLocalStorage(url::Origin(GURL("http://foobar.com")),
                            MakeRequest(&wrapper));

  DoTestPut(context, key, value);
  std::vector<uint8_t> result;
  EXPECT_TRUE(DoTestGet(context, key, &result));
  EXPECT_EQ(value, result);

  context->ShutdownAndDelete();
  context = nullptr;
  base::RunLoop().RunUntilIdle();

  // Should not have created any files.
  EXPECT_TRUE(FirstEntryInDir().empty());

  // Re-opening should get fresh data.
  context = new LocalStorageContextMojo(connector(), nullptr, base::FilePath(),
                                        base::FilePath(), nullptr);
  EXPECT_FALSE(DoTestGet(context, key, &result));
  context->ShutdownAndDelete();
}

TEST_F(LocalStorageContextMojoTestWithService, InMemoryInvalidPath) {
  auto* context = new LocalStorageContextMojo(
      connector(), nullptr, base::FilePath(),
      base::FilePath(FILE_PATH_LITERAL("../../")), nullptr);
  auto key = StdStringToUint8Vector("key");
  auto value = StdStringToUint8Vector("value");

  mojom::LevelDBWrapperPtr wrapper;
  context->OpenLocalStorage(url::Origin(GURL("http://foobar.com")),
                            MakeRequest(&wrapper));

  DoTestPut(context, key, value);
  std::vector<uint8_t> result;
  EXPECT_TRUE(DoTestGet(context, key, &result));
  EXPECT_EQ(value, result);

  context->ShutdownAndDelete();
  context = nullptr;
  base::RunLoop().RunUntilIdle();

  // Should not have created any files.
  EXPECT_TRUE(FirstEntryInDir().empty());
}

TEST_F(LocalStorageContextMojoTestWithService, OnDisk) {
  base::FilePath test_path(FILE_PATH_LITERAL("test_path"));
  auto* context = new LocalStorageContextMojo(
      connector(), nullptr, base::FilePath(), test_path, nullptr);
  auto key = StdStringToUint8Vector("key");
  auto value = StdStringToUint8Vector("value");

  DoTestPut(context, key, value);
  std::vector<uint8_t> result;
  EXPECT_TRUE(DoTestGet(context, key, &result));
  EXPECT_EQ(value, result);

  context->ShutdownAndDelete();
  context = nullptr;
  base::RunLoop().RunUntilIdle();

  // Should have created files.
  EXPECT_EQ(test_path, FirstEntryInDir().BaseName());

  // Should be able to re-open.
  context = new LocalStorageContextMojo(connector(), nullptr, base::FilePath(),
                                        test_path, nullptr);
  EXPECT_TRUE(DoTestGet(context, key, &result));
  EXPECT_EQ(value, result);
  context->ShutdownAndDelete();
}

TEST_F(LocalStorageContextMojoTestWithService, InvalidVersionOnDisk) {
  base::FilePath test_path(FILE_PATH_LITERAL("test_path"));

  // Create context and add some data to it.
  auto* context = new LocalStorageContextMojo(
      connector(), nullptr, base::FilePath(), test_path, nullptr);
  auto key = StdStringToUint8Vector("key");
  auto value = StdStringToUint8Vector("value");

  DoTestPut(context, key, value);
  std::vector<uint8_t> result;
  EXPECT_TRUE(DoTestGet(context, key, &result));
  EXPECT_EQ(value, result);

  context->ShutdownAndDelete();
  context = nullptr;
  base::RunLoop().RunUntilIdle();

  {
    // Mess up version number in database.
    leveldb_env::ChromiumEnv env;
    leveldb::DB* db = nullptr;
    leveldb::Options options;
    options.env = &env;
    base::FilePath db_path =
        temp_path().Append(test_path).Append(FILE_PATH_LITERAL("leveldb"));
    ASSERT_TRUE(leveldb::DB::Open(options, db_path.AsUTF8Unsafe(), &db).ok());
    std::unique_ptr<leveldb::DB> db_owner(db);
    ASSERT_TRUE(db->Put(leveldb::WriteOptions(), "VERSION", "argh").ok());
  }

  // Make sure data is gone.
  context = new LocalStorageContextMojo(connector(), nullptr, base::FilePath(),
                                        test_path, nullptr);
  EXPECT_FALSE(DoTestGet(context, key, &result));

  // Write data again.
  DoTestPut(context, key, value);

  context->ShutdownAndDelete();
  context = nullptr;
  base::RunLoop().RunUntilIdle();

  // Data should have been preserved now.
  context = new LocalStorageContextMojo(connector(), nullptr, base::FilePath(),
                                        test_path, nullptr);
  EXPECT_TRUE(DoTestGet(context, key, &result));
  EXPECT_EQ(value, result);
  context->ShutdownAndDelete();
}

}  // namespace content
