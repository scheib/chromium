// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/renderer/dom_storage/local_storage_cached_area.h"

#include <stdint.h>

#include <list>

#include "base/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "content/common/leveldb_wrapper.mojom.h"
#include "content/common/storage_partition_service.mojom.h"
#include "content/public/test/test_browser_thread_bundle.h"
#include "content/renderer/dom_storage/local_storage_cached_areas.h"
#include "mojo/public/cpp/bindings/binding_set.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace content {

namespace {

class MockLevelDBWrapper : public mojom::StoragePartitionService,
                           public mojom::LevelDBWrapper {
 public:
  // StoragePartitionService implementation:
  void OpenLocalStorage(const url::Origin& origin,
                        mojom::LevelDBWrapperRequest database) override {
    bindings_.AddBinding(this, std::move(database));
  }

  // LevelDBWrapper implementation:
  void AddObserver(mojom::LevelDBObserverAssociatedPtrInfo observer) override {}

  void Put(const std::vector<uint8_t>& key,
           const std::vector<uint8_t>& value,
           const std::string& source,
           PutCallback callback) override {
    observed_put_ = true;
    observed_key_ = key;
    observed_value_ = value;
    observed_source_ = source;
    pending_callbacks_.push_back(std::move(callback));
  }

  void Delete(const std::vector<uint8_t>& key,
              const std::string& source,
              DeleteCallback callback) override {
    observed_delete_ = true;
    observed_key_ = key;
    observed_source_ = source;
    pending_callbacks_.push_back(std::move(callback));
  }

  void DeleteAll(const std::string& source,
                 DeleteAllCallback callback) override {
    observed_delete_all_ = true;
    observed_source_ = source;
    pending_callbacks_.push_back(std::move(callback));
  }

  void Get(const std::vector<uint8_t>& key, GetCallback callback) override {}

  void GetAll(
      mojom::LevelDBWrapperGetAllCallbackAssociatedPtrInfo complete_callback,
      GetAllCallback callback) override {
    mojom::LevelDBWrapperGetAllCallbackAssociatedPtr complete_ptr;
    complete_ptr.Bind(std::move(complete_callback));
    pending_callbacks_.push_back(
        base::BindOnce(&mojom::LevelDBWrapperGetAllCallback::Complete,
                       std::move(complete_ptr)));

    observed_get_all_ = true;
    std::vector<mojom::KeyValuePtr> all;
    for (const auto& it : get_all_return_values_) {
      mojom::KeyValuePtr kv = mojom::KeyValue::New();
      kv->key = it.first;
      kv->value = it.second;
      all.push_back(std::move(kv));
    }
    std::move(callback).Run(leveldb::mojom::DatabaseError::OK, std::move(all));
  }

  // Methods and members for use by test fixtures.
  bool HasBindings() { return !bindings_.empty(); }

  void ResetObservations() {
    observed_get_all_ = false;
    observed_put_ = false;
    observed_delete_ = false;
    observed_delete_all_ = false;
    observed_key_.clear();
    observed_value_.clear();
    observed_source_.clear();
  }

  void CompleteAllPendingCallbacks() {
    while (!pending_callbacks_.empty())
      CompleteOnePendingCallback(true);
  }

  void CompleteOnePendingCallback(bool success) {
    ASSERT_TRUE(!pending_callbacks_.empty());
    std::move(pending_callbacks_.front()).Run(success);
    pending_callbacks_.pop_front();
  }

  void Flush() { bindings_.FlushForTesting(); }

  using ResultCallback = base::OnceCallback<void(bool)>;
  std::list<ResultCallback> pending_callbacks_;
  bool observed_get_all_ = false;
  bool observed_put_ = false;
  bool observed_delete_ = false;
  bool observed_delete_all_ = false;
  std::vector<uint8_t> observed_key_;
  std::vector<uint8_t> observed_value_;
  std::string observed_source_;

  std::map<std::vector<uint8_t>, std::vector<uint8_t>> get_all_return_values_;

 private:
  mojo::BindingSet<mojom::LevelDBWrapper> bindings_;
};

}  // namespace

class LocalStorageCachedAreaTest : public testing::Test {
 public:
  LocalStorageCachedAreaTest()
      : kOrigin(GURL("http://dom_storage/")),
        kKey(base::ASCIIToUTF16("key")),
        kValue(base::ASCIIToUTF16("value")),
        kPageUrl("http://dom_storage/page"),
        kStorageAreaId("7"),
        kSource(kPageUrl.spec() + "\n" + kStorageAreaId),
        cached_areas_(&mock_leveldb_wrapper_) {}

  const url::Origin kOrigin;
  const base::string16 kKey;
  const base::string16 kValue;
  const GURL kPageUrl;
  const std::string kStorageAreaId;
  const std::string kSource;

  bool IsCacheLoaded(LocalStorageCachedArea* cached_area) {
    return cached_area->map_.get();
  }

  bool IsIgnoringAllMutations(LocalStorageCachedArea* cached_area) {
    return cached_area->ignore_all_mutations_;
  }

  bool IsIgnoringKeyMutations(LocalStorageCachedArea* cached_area,
                              const base::string16& key) {
    return cached_area->ignore_key_mutations_.find(key) !=
           cached_area->ignore_key_mutations_.end();
  }

  void ResetAll(LocalStorageCachedArea* cached_area) {
    mock_leveldb_wrapper_.CompleteAllPendingCallbacks();
    mock_leveldb_wrapper_.ResetObservations();
    cached_area->Reset();
  }

  void ResetCacheOnly(LocalStorageCachedArea* cached_area) {
    cached_area->Reset();
  }

  static std::vector<uint8_t> String16ToUint8Vector(
      const base::string16& input) {
    return LocalStorageCachedArea::String16ToUint8Vector(input);
  }

 protected:
  TestBrowserThreadBundle test_browser_thread_bundle_;
  MockLevelDBWrapper mock_leveldb_wrapper_;
  LocalStorageCachedAreas cached_areas_;
};

TEST_F(LocalStorageCachedAreaTest, Basics) {
  EXPECT_FALSE(mock_leveldb_wrapper_.HasBindings());
  scoped_refptr<LocalStorageCachedArea> cached_area =
      cached_areas_.GetCachedArea(kOrigin);
  EXPECT_EQ(kOrigin, cached_area->origin());
  EXPECT_TRUE(mock_leveldb_wrapper_.HasBindings());

  EXPECT_FALSE(IsCacheLoaded(cached_area.get()));

  const std::string kStorageAreaId = "123";
  EXPECT_EQ(0u, cached_area->GetLength());
  EXPECT_TRUE(cached_area->SetItem(kKey, kValue, kPageUrl, kStorageAreaId));
  EXPECT_EQ(1u, cached_area->GetLength());
  EXPECT_EQ(kKey, cached_area->GetKey(0).string());
  EXPECT_EQ(kValue, cached_area->GetItem(kKey).string());
  cached_area->RemoveItem(kKey, kPageUrl, kStorageAreaId);
  EXPECT_EQ(0u, cached_area->GetLength());
}

TEST_F(LocalStorageCachedAreaTest, Getters) {
  scoped_refptr<LocalStorageCachedArea> cached_area =
      cached_areas_.GetCachedArea(kOrigin);

  // GetLength, we expect to see one call to load in the db.
  EXPECT_FALSE(IsCacheLoaded(cached_area.get()));
  EXPECT_EQ(0u, cached_area->GetLength());
  EXPECT_TRUE(IsCacheLoaded(cached_area.get()));
  EXPECT_TRUE(mock_leveldb_wrapper_.observed_get_all_);
  EXPECT_EQ(1u, mock_leveldb_wrapper_.pending_callbacks_.size());
  EXPECT_TRUE(IsIgnoringAllMutations(cached_area.get()));
  mock_leveldb_wrapper_.CompleteAllPendingCallbacks();
  mock_leveldb_wrapper_.Flush();
  EXPECT_FALSE(IsIgnoringAllMutations(cached_area.get()));

  // GetKey, expect the one call to load.
  ResetAll(cached_area.get());
  EXPECT_FALSE(IsCacheLoaded(cached_area.get()));
  EXPECT_TRUE(cached_area->GetKey(2).is_null());
  EXPECT_TRUE(IsCacheLoaded(cached_area.get()));
  EXPECT_TRUE(mock_leveldb_wrapper_.observed_get_all_);
  EXPECT_EQ(1u, mock_leveldb_wrapper_.pending_callbacks_.size());

  // GetItem, ditto.
  ResetAll(cached_area.get());
  EXPECT_FALSE(IsCacheLoaded(cached_area.get()));
  EXPECT_TRUE(cached_area->GetItem(kKey).is_null());
  EXPECT_TRUE(IsCacheLoaded(cached_area.get()));
  EXPECT_TRUE(mock_leveldb_wrapper_.observed_get_all_);
  EXPECT_EQ(1u, mock_leveldb_wrapper_.pending_callbacks_.size());
}

TEST_F(LocalStorageCachedAreaTest, Setters) {
  scoped_refptr<LocalStorageCachedArea> cached_area =
      cached_areas_.GetCachedArea(kOrigin);

  // SetItem, we expect a call to load followed by a call to put in the db.
  EXPECT_FALSE(IsCacheLoaded(cached_area.get()));
  EXPECT_TRUE(cached_area->SetItem(kKey, kValue, kPageUrl, kStorageAreaId));
  mock_leveldb_wrapper_.Flush();
  EXPECT_TRUE(IsCacheLoaded(cached_area.get()));
  EXPECT_TRUE(mock_leveldb_wrapper_.observed_get_all_);
  EXPECT_TRUE(mock_leveldb_wrapper_.observed_put_);
  EXPECT_EQ(kSource, mock_leveldb_wrapper_.observed_source_);
  EXPECT_EQ(String16ToUint8Vector(kKey), mock_leveldb_wrapper_.observed_key_);
  EXPECT_EQ(String16ToUint8Vector(kValue),
            mock_leveldb_wrapper_.observed_value_);
  EXPECT_EQ(2u, mock_leveldb_wrapper_.pending_callbacks_.size());

  // Clear, we expect a just the one call to clear in the db since
  // there's no need to load the data prior to deleting it.
  ResetAll(cached_area.get());
  EXPECT_FALSE(IsCacheLoaded(cached_area.get()));
  cached_area->Clear(kPageUrl, kStorageAreaId);
  mock_leveldb_wrapper_.Flush();
  EXPECT_TRUE(IsCacheLoaded(cached_area.get()));
  EXPECT_TRUE(mock_leveldb_wrapper_.observed_delete_all_);
  EXPECT_EQ(kSource, mock_leveldb_wrapper_.observed_source_);
  EXPECT_EQ(1u, mock_leveldb_wrapper_.pending_callbacks_.size());

  // RemoveItem with nothing to remove, expect just one call to load.
  ResetAll(cached_area.get());
  EXPECT_FALSE(IsCacheLoaded(cached_area.get()));
  cached_area->RemoveItem(kKey, kPageUrl, kStorageAreaId);
  mock_leveldb_wrapper_.Flush();
  EXPECT_TRUE(IsCacheLoaded(cached_area.get()));
  EXPECT_TRUE(mock_leveldb_wrapper_.observed_get_all_);
  EXPECT_FALSE(mock_leveldb_wrapper_.observed_delete_);
  EXPECT_EQ(1u, mock_leveldb_wrapper_.pending_callbacks_.size());

  // RemoveItem with something to remove, expect a call to load followed
  // by a call to remove.
  ResetAll(cached_area.get());
  mock_leveldb_wrapper_.get_all_return_values_[String16ToUint8Vector(kKey)] =
      String16ToUint8Vector(kValue);
  EXPECT_FALSE(IsCacheLoaded(cached_area.get()));
  cached_area->RemoveItem(kKey, kPageUrl, kStorageAreaId);
  mock_leveldb_wrapper_.Flush();
  EXPECT_TRUE(IsCacheLoaded(cached_area.get()));
  EXPECT_TRUE(mock_leveldb_wrapper_.observed_get_all_);
  EXPECT_TRUE(mock_leveldb_wrapper_.observed_delete_);
  EXPECT_EQ(kSource, mock_leveldb_wrapper_.observed_source_);
  EXPECT_EQ(String16ToUint8Vector(kKey), mock_leveldb_wrapper_.observed_key_);
  EXPECT_EQ(2u, mock_leveldb_wrapper_.pending_callbacks_.size());
}

TEST_F(LocalStorageCachedAreaTest, MutationsAreIgnoredUntilLoadCompletion) {
  scoped_refptr<LocalStorageCachedArea> cached_area =
      cached_areas_.GetCachedArea(kOrigin);
  mojom::LevelDBObserver* observer = cached_area.get();

  EXPECT_TRUE(cached_area->GetItem(kKey).is_null());
  EXPECT_TRUE(IsCacheLoaded(cached_area.get()));
  EXPECT_TRUE(IsIgnoringAllMutations(cached_area.get()));

  // Before load completion, the mutation should be ignored.
  observer->KeyAdded(String16ToUint8Vector(kKey), String16ToUint8Vector(kValue),
                     kSource);
  EXPECT_TRUE(cached_area->GetItem(kKey).is_null());

  // Call the load completion callback.
  mock_leveldb_wrapper_.CompleteOnePendingCallback(true);
  mock_leveldb_wrapper_.Flush();
  EXPECT_FALSE(IsIgnoringAllMutations(cached_area.get()));

  // Verify that mutations are now applied.
  observer->KeyAdded(String16ToUint8Vector(kKey), String16ToUint8Vector(kValue),
                     kSource);
  EXPECT_EQ(kValue, cached_area->GetItem(kKey).string());
}

TEST_F(LocalStorageCachedAreaTest, MutationsAreIgnoredUntilClearCompletion) {
  scoped_refptr<LocalStorageCachedArea> cached_area =
      cached_areas_.GetCachedArea(kOrigin);

  cached_area->Clear(kPageUrl, kStorageAreaId);
  mock_leveldb_wrapper_.Flush();
  EXPECT_TRUE(IsIgnoringAllMutations(cached_area.get()));
  mock_leveldb_wrapper_.CompleteOnePendingCallback(true);
  mock_leveldb_wrapper_.Flush();
  EXPECT_FALSE(IsIgnoringAllMutations(cached_area.get()));

  // Verify that calling Clear twice works as expected, the first
  // completion callback should have been cancelled.
  ResetCacheOnly(cached_area.get());
  cached_area->Clear(kPageUrl, kStorageAreaId);
  mock_leveldb_wrapper_.Flush();
  EXPECT_TRUE(IsIgnoringAllMutations(cached_area.get()));
  cached_area->Clear(kPageUrl, kStorageAreaId);
  mock_leveldb_wrapper_.Flush();
  EXPECT_TRUE(IsIgnoringAllMutations(cached_area.get()));
  mock_leveldb_wrapper_.CompleteOnePendingCallback(true);
  mock_leveldb_wrapper_.Flush();
  EXPECT_TRUE(IsIgnoringAllMutations(cached_area.get()));
  mock_leveldb_wrapper_.CompleteOnePendingCallback(true);
  mock_leveldb_wrapper_.Flush();
  EXPECT_FALSE(IsIgnoringAllMutations(cached_area.get()));
}

TEST_F(LocalStorageCachedAreaTest, KeyMutationsAreIgnoredUntilCompletion) {
  scoped_refptr<LocalStorageCachedArea> cached_area =
      cached_areas_.GetCachedArea(kOrigin);
  mojom::LevelDBObserver* observer = cached_area.get();

  // SetItem
  EXPECT_TRUE(cached_area->SetItem(kKey, kValue, kPageUrl, kStorageAreaId));
  mock_leveldb_wrapper_.CompleteOnePendingCallback(true);  // load completion
  mock_leveldb_wrapper_.Flush();
  EXPECT_FALSE(IsIgnoringAllMutations(cached_area.get()));
  EXPECT_TRUE(IsIgnoringKeyMutations(cached_area.get(), kKey));
  observer->KeyDeleted(String16ToUint8Vector(kKey), {0}, kSource);
  mock_leveldb_wrapper_.Flush();
  EXPECT_EQ(kValue, cached_area->GetItem(kKey).string());
  mock_leveldb_wrapper_.CompleteOnePendingCallback(true);  // set completion
  mock_leveldb_wrapper_.Flush();
  EXPECT_FALSE(IsIgnoringKeyMutations(cached_area.get(), kKey));

  // RemoveItem
  cached_area->RemoveItem(kKey, kPageUrl, kStorageAreaId);
  mock_leveldb_wrapper_.Flush();
  EXPECT_TRUE(IsIgnoringKeyMutations(cached_area.get(), kKey));
  mock_leveldb_wrapper_.CompleteOnePendingCallback(true);  // remove completion
  mock_leveldb_wrapper_.Flush();
  EXPECT_FALSE(IsIgnoringKeyMutations(cached_area.get(), kKey));

  // Multiple mutations to the same key.
  EXPECT_TRUE(cached_area->SetItem(kKey, kValue, kPageUrl, kStorageAreaId));
  cached_area->RemoveItem(kKey, kPageUrl, kStorageAreaId);
  mock_leveldb_wrapper_.Flush();
  EXPECT_TRUE(IsIgnoringKeyMutations(cached_area.get(), kKey));
  mock_leveldb_wrapper_.CompleteOnePendingCallback(true);  // set completion
  mock_leveldb_wrapper_.Flush();
  EXPECT_TRUE(IsIgnoringKeyMutations(cached_area.get(), kKey));
  mock_leveldb_wrapper_.CompleteOnePendingCallback(true);  // remove completion
  mock_leveldb_wrapper_.Flush();
  EXPECT_FALSE(IsIgnoringKeyMutations(cached_area.get(), kKey));

  // A failed set item operation should Reset the cache.
  EXPECT_TRUE(cached_area->SetItem(kKey, kValue, kPageUrl, kStorageAreaId));
  mock_leveldb_wrapper_.Flush();
  EXPECT_TRUE(IsIgnoringKeyMutations(cached_area.get(), kKey));
  mock_leveldb_wrapper_.CompleteOnePendingCallback(false);
  mock_leveldb_wrapper_.Flush();
  EXPECT_FALSE(IsCacheLoaded(cached_area.get()));
}

}  // namespace content
