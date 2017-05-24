// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/dom_storage/local_storage_context_mojo.h"

#include "base/memory/ptr_util.h"
#include "base/metrics/histogram_macros.h"
#include "base/single_thread_task_runner.h"
#include "base/strings/string_number_conversions.h"
#include "components/leveldb/public/cpp/util.h"
#include "components/leveldb/public/interfaces/leveldb.mojom.h"
#include "content/browser/dom_storage/dom_storage_area.h"
#include "content/browser/dom_storage/dom_storage_database.h"
#include "content/browser/dom_storage/dom_storage_task_runner.h"
#include "content/browser/dom_storage/local_storage_database.pb.h"
#include "content/browser/leveldb_wrapper_impl.h"
#include "content/common/dom_storage/dom_storage_types.h"
#include "content/public/browser/local_storage_usage_info.h"
#include "services/file/public/interfaces/constants.mojom.h"
#include "services/service_manager/public/cpp/connector.h"
#include "sql/connection.h"
#include "storage/browser/quota/special_storage_policy.h"
#include "third_party/leveldatabase/env_chromium.h"

namespace content {

// LevelDB database schema
// =======================
//
// Version 1 (in sorted order):
//   key: "VERSION"
//   value: "1"
//
//   key: "META:" + <url::Origin 'origin'>
//   value: <LocalStorageOriginMetaData serialized as a string>
//
//   key: "_" + <url::Origin> 'origin'> + '\x00' + <script controlled key>
//   value: <script controlled value>

namespace {

const char kVersionKey[] = "VERSION";
const char kOriginSeparator = '\x00';
const char kDataPrefix[] = "_";
const uint8_t kMetaPrefix[] = {'M', 'E', 'T', 'A', ':'};
const int64_t kMinSchemaVersion = 1;
const int64_t kCurrentSchemaVersion = 1;

const char kStorageOpenHistogramName[] = "LocalStorageContext.OpenError";
// These values are written to logs.  New enum values can be added, but existing
// enums must never be renumbered or deleted and reused.
enum class LocalStorageOpenHistogram {
  DIRECTORY_OPEN_FAILED = 0,
  DATABASE_OPEN_FAILED = 1,
  INVALID_VERSION = 2,
  VERSION_READ_ERROR = 3,
  MAX
};

std::vector<uint8_t> CreateMetaDataKey(const url::Origin& origin) {
  auto serialized_origin = leveldb::StdStringToUint8Vector(origin.Serialize());
  std::vector<uint8_t> key;
  key.reserve(arraysize(kMetaPrefix) + serialized_origin.size());
  key.insert(key.end(), kMetaPrefix, kMetaPrefix + arraysize(kMetaPrefix));
  key.insert(key.end(), serialized_origin.begin(), serialized_origin.end());
  return key;
}

void NoOpSuccess(bool success) {}
void NoOpDatabaseError(leveldb::mojom::DatabaseError error) {}

void MigrateStorageHelper(
    base::FilePath db_path,
    const scoped_refptr<base::SingleThreadTaskRunner> reply_task_runner,
    base::Callback<void(std::unique_ptr<LevelDBWrapperImpl::ValueMap>)>
        callback) {
  DOMStorageDatabase db(db_path);
  DOMStorageValuesMap map;
  db.ReadAllValues(&map);
  auto values = base::MakeUnique<LevelDBWrapperImpl::ValueMap>();
  for (const auto& it : map) {
    (*values)[LocalStorageContextMojo::MigrateString(it.first)] =
        LocalStorageContextMojo::MigrateString(it.second.string());
  }
  reply_task_runner->PostTask(FROM_HERE,
                              base::Bind(callback, base::Passed(&values)));
}

// Helper to convert from OnceCallback to Callback.
void CallMigrationCalback(LevelDBWrapperImpl::ValueMapCallback callback,
                          std::unique_ptr<LevelDBWrapperImpl::ValueMap> data) {
  std::move(callback).Run(std::move(data));
}

void AddDeleteOriginOperations(
    std::vector<leveldb::mojom::BatchedOperationPtr>* operations,
    const url::Origin& origin) {
  leveldb::mojom::BatchedOperationPtr item =
      leveldb::mojom::BatchedOperation::New();
  item->type = leveldb::mojom::BatchOperationType::DELETE_PREFIXED_KEY;
  item->key = leveldb::StdStringToUint8Vector(kDataPrefix + origin.Serialize() +
                                              kOriginSeparator);
  operations->push_back(std::move(item));

  item = leveldb::mojom::BatchedOperation::New();
  item->type = leveldb::mojom::BatchOperationType::DELETE_KEY;
  item->key = CreateMetaDataKey(origin);
  operations->push_back(std::move(item));
}

}  // namespace

class LocalStorageContextMojo::LevelDBWrapperHolder
    : public LevelDBWrapperImpl::Delegate {
 public:
  LevelDBWrapperHolder(LocalStorageContextMojo* context,
                       const url::Origin& origin)
      : context_(context), origin_(origin) {
    // Delay for a moment after a value is set in anticipation
    // of other values being set, so changes are batched.
    const int kCommitDefaultDelaySecs = 5;

    // To avoid excessive IO we apply limits to the amount of data being written
    // and the frequency of writes.
    const int kMaxBytesPerHour = kPerStorageAreaQuota;
    const int kMaxCommitsPerHour = 60;

    level_db_wrapper_ = base::MakeUnique<LevelDBWrapperImpl>(
        context_->database_.get(),
        kDataPrefix + origin_.Serialize() + kOriginSeparator,
        kPerStorageAreaQuota + kPerStorageAreaOverQuotaAllowance,
        base::TimeDelta::FromSeconds(kCommitDefaultDelaySecs), kMaxBytesPerHour,
        kMaxCommitsPerHour, this);
    level_db_wrapper_ptr_ = level_db_wrapper_.get();
  }

  LevelDBWrapperImpl* level_db_wrapper() { return level_db_wrapper_ptr_; }

  void OnNoBindings() override {
    // Will delete |this|.
    DCHECK(context_->level_db_wrappers_.find(origin_) !=
           context_->level_db_wrappers_.end());
    context_->level_db_wrappers_.erase(origin_);
  }

  std::vector<leveldb::mojom::BatchedOperationPtr> PrepareToCommit() override {
    std::vector<leveldb::mojom::BatchedOperationPtr> operations;

    // Write schema version if not already done so before.
    if (!context_->database_initialized_) {
      leveldb::mojom::BatchedOperationPtr item =
          leveldb::mojom::BatchedOperation::New();
      item->type = leveldb::mojom::BatchOperationType::PUT_KEY;
      item->key = leveldb::StdStringToUint8Vector(kVersionKey);
      item->value = leveldb::StdStringToUint8Vector(
          base::Int64ToString(kCurrentSchemaVersion));
      operations.push_back(std::move(item));
      context_->database_initialized_ = true;
    }

    leveldb::mojom::BatchedOperationPtr item =
        leveldb::mojom::BatchedOperation::New();
    item->type = leveldb::mojom::BatchOperationType::PUT_KEY;
    item->key = CreateMetaDataKey(origin_);
    if (level_db_wrapper()->empty()) {
      item->type = leveldb::mojom::BatchOperationType::DELETE_KEY;
    } else {
      item->type = leveldb::mojom::BatchOperationType::PUT_KEY;
      LocalStorageOriginMetaData data;
      data.set_last_modified(base::Time::Now().ToInternalValue());
      data.set_size_bytes(level_db_wrapper()->bytes_used());
      item->value = leveldb::StdStringToUint8Vector(data.SerializeAsString());
    }
    operations.push_back(std::move(item));

    return operations;
  }

  void DidCommit(leveldb::mojom::DatabaseError error) override {
    UMA_HISTOGRAM_ENUMERATION("LocalStorageContext.CommitResult",
                              leveldb::GetLevelDBStatusUMAValue(error),
                              leveldb_env::LEVELDB_STATUS_MAX);

    // Delete any old database that might still exist if we successfully wrote
    // data to LevelDB, and our LevelDB is actually disk backed.
    if (error == leveldb::mojom::DatabaseError::OK && !deleted_old_data_ &&
        !context_->subdirectory_.empty() && context_->task_runner_ &&
        !context_->old_localstorage_path_.empty()) {
      deleted_old_data_ = true;
      context_->task_runner_->PostShutdownBlockingTask(
          FROM_HERE, DOMStorageTaskRunner::PRIMARY_SEQUENCE,
          base::Bind(base::IgnoreResult(&sql::Connection::Delete),
                     sql_db_path()));
    }
  }

  void MigrateData(LevelDBWrapperImpl::ValueMapCallback callback) override {
    if (context_->task_runner_ && !context_->old_localstorage_path_.empty()) {
      context_->task_runner_->PostShutdownBlockingTask(
          FROM_HERE, DOMStorageTaskRunner::PRIMARY_SEQUENCE,
          base::Bind(
              &MigrateStorageHelper, sql_db_path(),
              base::ThreadTaskRunnerHandle::Get(),
              base::Bind(&CallMigrationCalback, base::Passed(&callback))));
      return;
    }
    std::move(callback).Run(nullptr);
  }

  void OnMapLoaded(leveldb::mojom::DatabaseError error) override {
    if (error != leveldb::mojom::DatabaseError::OK)
      UMA_HISTOGRAM_ENUMERATION("LocalStorageContext.MapLoadError",
                                leveldb::GetLevelDBStatusUMAValue(error),
                                leveldb_env::LEVELDB_STATUS_MAX);
  }

 private:
  base::FilePath sql_db_path() const {
    if (context_->old_localstorage_path_.empty())
      return base::FilePath();
    return context_->old_localstorage_path_.Append(
        DOMStorageArea::DatabaseFileNameFromOrigin(origin_.GetURL()));
  }

  LocalStorageContextMojo* context_;
  url::Origin origin_;
  std::unique_ptr<LevelDBWrapperImpl> level_db_wrapper_;
  // Holds the same value as |level_db_wrapper_|. The reason for this is that
  // during destruction of the LevelDBWrapperImpl instance we might still get
  // called and need access  to the LevelDBWrapperImpl instance. The unique_ptr
  // could already be null, but this field should still be valid.
  LevelDBWrapperImpl* level_db_wrapper_ptr_;
  bool deleted_old_data_ = false;
};

LocalStorageContextMojo::LocalStorageContextMojo(
    service_manager::Connector* connector,
    scoped_refptr<DOMStorageTaskRunner> task_runner,
    const base::FilePath& old_localstorage_path,
    const base::FilePath& subdirectory,
    scoped_refptr<storage::SpecialStoragePolicy> special_storage_policy)
    : connector_(connector ? connector->Clone() : nullptr),
      subdirectory_(subdirectory),
      special_storage_policy_(std::move(special_storage_policy)),
      task_runner_(std::move(task_runner)),
      old_localstorage_path_(old_localstorage_path),
      weak_ptr_factory_(this) {}

void LocalStorageContextMojo::OpenLocalStorage(
    const url::Origin& origin,
    mojom::LevelDBWrapperRequest request) {
  RunWhenConnected(base::BindOnce(&LocalStorageContextMojo::BindLocalStorage,
                                  weak_ptr_factory_.GetWeakPtr(), origin,
                                  std::move(request)));
}

void LocalStorageContextMojo::GetStorageUsage(
    GetStorageUsageCallback callback) {
  RunWhenConnected(
      base::BindOnce(&LocalStorageContextMojo::RetrieveStorageUsage,
                     weak_ptr_factory_.GetWeakPtr(), std::move(callback)));
}

void LocalStorageContextMojo::DeleteStorage(const url::Origin& origin) {
  if (connection_state_ != CONNECTION_FINISHED) {
    RunWhenConnected(base::BindOnce(&LocalStorageContextMojo::DeleteStorage,
                                    weak_ptr_factory_.GetWeakPtr(), origin));
    return;
  }

  auto found = level_db_wrappers_.find(origin);
  if (found != level_db_wrappers_.end()) {
    // Renderer process expects |source| to always be two newline separated
    // strings.
    found->second->level_db_wrapper()->DeleteAll("\n",
                                                 base::Bind(&NoOpSuccess));
    found->second->level_db_wrapper()->ScheduleImmediateCommit();
  } else if (database_) {
    std::vector<leveldb::mojom::BatchedOperationPtr> operations;
    AddDeleteOriginOperations(&operations, origin);
    database_->Write(std::move(operations), base::Bind(&NoOpDatabaseError));
  }
}

void LocalStorageContextMojo::DeleteStorageForPhysicalOrigin(
    const url::Origin& origin) {
  GetStorageUsage(base::BindOnce(
      &LocalStorageContextMojo::OnGotStorageUsageForDeletePhysicalOrigin,
      weak_ptr_factory_.GetWeakPtr(), origin));
}

void LocalStorageContextMojo::Flush() {
  if (connection_state_ != CONNECTION_FINISHED) {
    RunWhenConnected(base::BindOnce(&LocalStorageContextMojo::Flush,
                                    weak_ptr_factory_.GetWeakPtr()));
    return;
  }
  for (const auto& it : level_db_wrappers_)
    it.second->level_db_wrapper()->ScheduleImmediateCommit();
}

void LocalStorageContextMojo::ShutdownAndDelete() {
  DCHECK_NE(connection_state_, CONNECTION_SHUTDOWN);

  // Nothing to do if no connection to the database was ever finished.
  if (connection_state_ != CONNECTION_FINISHED) {
    connection_state_ = CONNECTION_SHUTDOWN;
    OnShutdownComplete(leveldb::mojom::DatabaseError::OK);
    return;
  }

  connection_state_ = CONNECTION_SHUTDOWN;

  // Flush any uncommitted data.
  for (const auto& it : level_db_wrappers_)
    it.second->level_db_wrapper()->ScheduleImmediateCommit();

  // Respect the content policy settings about what to
  // keep and what to discard.
  if (force_keep_session_state_) {
    OnShutdownComplete(leveldb::mojom::DatabaseError::OK);
    return;  // Keep everything.
  }

  bool has_session_only_origins =
      special_storage_policy_.get() &&
      special_storage_policy_->HasSessionOnlyOrigins();

  if (has_session_only_origins) {
    RetrieveStorageUsage(
        base::BindOnce(&LocalStorageContextMojo::OnGotStorageUsageForShutdown,
                       base::Unretained(this)));
  } else {
    OnShutdownComplete(leveldb::mojom::DatabaseError::OK);
  }
}

void LocalStorageContextMojo::PurgeMemory() {
  for (const auto& it : level_db_wrappers_)
    it.second->level_db_wrapper()->PurgeMemory();
}

leveldb::mojom::LevelDBDatabaseAssociatedRequest
LocalStorageContextMojo::DatabaseRequestForTesting() {
  DCHECK_EQ(connection_state_, NO_CONNECTION);
  connection_state_ = CONNECTION_IN_PROGRESS;
  leveldb::mojom::LevelDBDatabaseAssociatedRequest request =
      MakeIsolatedRequest(&database_);
  OnDatabaseOpened(true, leveldb::mojom::DatabaseError::OK);
  return request;
}

// static
std::vector<uint8_t> LocalStorageContextMojo::MigrateString(
    const base::string16& input) {
  static const uint8_t kUTF16Format = 0;

  const uint8_t* data = reinterpret_cast<const uint8_t*>(input.data());
  std::vector<uint8_t> result;
  result.reserve(input.size() * sizeof(base::char16) + 1);
  result.push_back(kUTF16Format);
  result.insert(result.end(), data, data + input.size() * sizeof(base::char16));
  return result;
}

LocalStorageContextMojo::~LocalStorageContextMojo() {
  DCHECK_EQ(connection_state_, CONNECTION_SHUTDOWN);
}

void LocalStorageContextMojo::RunWhenConnected(base::OnceClosure callback) {
  DCHECK_NE(connection_state_, CONNECTION_SHUTDOWN);

  // If we don't have a filesystem_connection_, we'll need to establish one.
  if (connection_state_ == NO_CONNECTION) {
    connection_state_ = CONNECTION_IN_PROGRESS;
    InitiateConnection();
  }

  if (connection_state_ == CONNECTION_IN_PROGRESS) {
    // Queue this OpenLocalStorage call for when we have a level db pointer.
    on_database_opened_callbacks_.push_back(std::move(callback));
    return;
  }

  std::move(callback).Run();
}

void LocalStorageContextMojo::InitiateConnection(bool in_memory_only) {
  DCHECK_EQ(connection_state_, CONNECTION_IN_PROGRESS);
  CHECK(connector_);
  if (!subdirectory_.empty() && !in_memory_only) {
    // We were given a subdirectory to write to. Get it and use a disk backed
    // database.
    connector_->BindInterface(file::mojom::kServiceName, &file_system_);
    file_system_->GetSubDirectory(
        subdirectory_.AsUTF8Unsafe(), MakeRequest(&directory_),
        base::Bind(&LocalStorageContextMojo::OnDirectoryOpened,
                   weak_ptr_factory_.GetWeakPtr()));
  } else {
    // We were not given a subdirectory. Use a memory backed database.
    connector_->BindInterface(file::mojom::kServiceName, &leveldb_service_);
    leveldb_service_->OpenInMemory(
        MakeRequest(&database_),
        base::Bind(&LocalStorageContextMojo::OnDatabaseOpened,
                   weak_ptr_factory_.GetWeakPtr(), true));
  }
}

void LocalStorageContextMojo::OnDirectoryOpened(
    filesystem::mojom::FileError err) {
  if (err != filesystem::mojom::FileError::OK) {
    // We failed to open the directory; continue with startup so that we create
    // the |level_db_wrappers_|.
    UMA_HISTOGRAM_ENUMERATION("LocalStorageContext.DirectoryOpenError",
                              -static_cast<base::File::Error>(err),
                              -base::File::FILE_ERROR_MAX);
    UMA_HISTOGRAM_ENUMERATION(
        kStorageOpenHistogramName,
        static_cast<int>(LocalStorageOpenHistogram::DIRECTORY_OPEN_FAILED),
        static_cast<int>(LocalStorageOpenHistogram::MAX));
    OnDatabaseOpened(false, leveldb::mojom::DatabaseError::OK);
    return;
  }

  // Now that we have a directory, connect to the LevelDB service and get our
  // database.
  connector_->BindInterface(file::mojom::kServiceName, &leveldb_service_);

  // We might still need to use the directory, so create a clone.
  filesystem::mojom::DirectoryPtr directory_clone;
  directory_->Clone(MakeRequest(&directory_clone));

  auto options = leveldb::mojom::OpenOptions::New();
  options->create_if_missing = true;
  leveldb_service_->OpenWithOptions(
      std::move(options), std::move(directory_clone), "leveldb",
      MakeRequest(&database_),
      base::Bind(&LocalStorageContextMojo::OnDatabaseOpened,
                 weak_ptr_factory_.GetWeakPtr(), false));
}

void LocalStorageContextMojo::OnDatabaseOpened(
    bool in_memory,
    leveldb::mojom::DatabaseError status) {
  if (status != leveldb::mojom::DatabaseError::OK) {
    UMA_HISTOGRAM_ENUMERATION("LocalStorageContext.DatabaseOpenError",
                              leveldb::GetLevelDBStatusUMAValue(status),
                              leveldb_env::LEVELDB_STATUS_MAX);
    if (in_memory) {
      UMA_HISTOGRAM_ENUMERATION("LocalStorageContext.DatabaseOpenError.Memory",
                                leveldb::GetLevelDBStatusUMAValue(status),
                                leveldb_env::LEVELDB_STATUS_MAX);
    } else {
      UMA_HISTOGRAM_ENUMERATION("LocalStorageContext.DatabaseOpenError.Disk",
                                leveldb::GetLevelDBStatusUMAValue(status),
                                leveldb_env::LEVELDB_STATUS_MAX);
    }
    UMA_HISTOGRAM_ENUMERATION(
        kStorageOpenHistogramName,
        static_cast<int>(LocalStorageOpenHistogram::DATABASE_OPEN_FAILED),
        static_cast<int>(LocalStorageOpenHistogram::MAX));
    // If we failed to open the database, reset the service object so we pass
    // null pointers to our wrappers.
    database_.reset();
  }

  // Verify DB schema version.
  if (database_) {
    database_->Get(leveldb::StdStringToUint8Vector(kVersionKey),
                   base::Bind(&LocalStorageContextMojo::OnGotDatabaseVersion,
                              weak_ptr_factory_.GetWeakPtr()));
    return;
  }

  OnConnectionFinished();
}

void LocalStorageContextMojo::OnGotDatabaseVersion(
    leveldb::mojom::DatabaseError status,
    const std::vector<uint8_t>& value) {
  if (status == leveldb::mojom::DatabaseError::NOT_FOUND) {
    // New database, nothing more to do. Current version will get written
    // when first data is committed.
  } else if (status == leveldb::mojom::DatabaseError::OK) {
    // Existing database, check if version number matches current schema
    // version.
    int64_t db_version;
    if (!base::StringToInt64(leveldb::Uint8VectorToStdString(value),
                             &db_version) ||
        db_version < kMinSchemaVersion || db_version > kCurrentSchemaVersion) {
      UMA_HISTOGRAM_ENUMERATION(
          kStorageOpenHistogramName,
          static_cast<int>(LocalStorageOpenHistogram::INVALID_VERSION),
          static_cast<int>(LocalStorageOpenHistogram::MAX));
      DeleteAndRecreateDatabase();
      return;
    }

    database_initialized_ = true;
  } else {
    // Other read error. Possibly database corruption.
    UMA_HISTOGRAM_ENUMERATION("LocalStorageContext.ReadVersionError",
                              leveldb::GetLevelDBStatusUMAValue(status),
                              leveldb_env::LEVELDB_STATUS_MAX);
    UMA_HISTOGRAM_ENUMERATION(
        kStorageOpenHistogramName,
        static_cast<int>(LocalStorageOpenHistogram::VERSION_READ_ERROR),
        static_cast<int>(LocalStorageOpenHistogram::MAX));
    DeleteAndRecreateDatabase();
    return;
  }

  OnConnectionFinished();
}

void LocalStorageContextMojo::OnConnectionFinished() {
  DCHECK_EQ(connection_state_, CONNECTION_IN_PROGRESS);

  // We no longer need the file service; we've either transferred |directory_|
  // to the leveldb service, or we got a file error and no more is possible.
  directory_.reset();
  file_system_.reset();
  if (!database_)
    leveldb_service_.reset();

  // |database_| should be known to either be valid or invalid by now. Run our
  // delayed bindings.
  connection_state_ = CONNECTION_FINISHED;
  for (size_t i = 0; i < on_database_opened_callbacks_.size(); ++i)
    std::move(on_database_opened_callbacks_[i]).Run();
  on_database_opened_callbacks_.clear();
}

void LocalStorageContextMojo::DeleteAndRecreateDatabase() {
  // For now don't support deletion and recreation when already connected.
  DCHECK_EQ(connection_state_, CONNECTION_IN_PROGRESS);

  bool recreate_in_memory = false;

  // If tried to recreate database on disk already, try again but this time
  // in memory.
  if (tried_to_recreate_ && !subdirectory_.empty()) {
    recreate_in_memory = true;
  } else if (tried_to_recreate_) {
    // Give up completely, run without any database.
    database_ = nullptr;
    OnConnectionFinished();
    return;
  }

  tried_to_recreate_ = true;

  // Unit tests might not have a bound file_service_, in which case there is
  // nothing to retry.
  if (!file_system_.is_bound()) {
    database_ = nullptr;
    OnConnectionFinished();
    return;
  }

  // Close and destroy database, and try again.
  database_ = nullptr;
  if (directory_.is_bound()) {
    leveldb_service_->Destroy(
        std::move(directory_), "leveldb",
        base::Bind(&LocalStorageContextMojo::OnDBDestroyed,
                   weak_ptr_factory_.GetWeakPtr(), recreate_in_memory));
  } else {
    // No directory, so nothing to destroy. Retrying to recreate will probably
    // fail, but try anyway.
    InitiateConnection(recreate_in_memory);
  }
}

void LocalStorageContextMojo::OnDBDestroyed(
    bool recreate_in_memory,
    leveldb::mojom::DatabaseError status) {
  UMA_HISTOGRAM_ENUMERATION("LocalStorageContext.DestroyDBResult",
                            leveldb::GetLevelDBStatusUMAValue(status),
                            leveldb_env::LEVELDB_STATUS_MAX);
  // We're essentially ignoring the status here. Even if destroying failed we
  // still want to go ahead and try to recreate.
  InitiateConnection(recreate_in_memory);
}

// The (possibly delayed) implementation of OpenLocalStorage(). Can be called
// directly from that function, or through |on_database_open_callbacks_|.
void LocalStorageContextMojo::BindLocalStorage(
    const url::Origin& origin,
    mojom::LevelDBWrapperRequest request) {
  GetOrCreateDBWrapper(origin)->Bind(std::move(request));
}

LevelDBWrapperImpl* LocalStorageContextMojo::GetOrCreateDBWrapper(
    const url::Origin& origin) {
  DCHECK_EQ(connection_state_, CONNECTION_FINISHED);
  auto found = level_db_wrappers_.find(origin);
  if (found != level_db_wrappers_.end())
    return found->second->level_db_wrapper();

  auto holder = base::MakeUnique<LevelDBWrapperHolder>(this, origin);
  LevelDBWrapperImpl* wrapper_ptr = holder->level_db_wrapper();
  level_db_wrappers_[origin] = std::move(holder);
  return wrapper_ptr;
}

void LocalStorageContextMojo::RetrieveStorageUsage(
    GetStorageUsageCallback callback) {
  database_->GetPrefixed(
      std::vector<uint8_t>(kMetaPrefix, kMetaPrefix + arraysize(kMetaPrefix)),
      base::Bind(&LocalStorageContextMojo::OnGotMetaData,
                 weak_ptr_factory_.GetWeakPtr(), base::Passed(&callback)));
}

void LocalStorageContextMojo::OnGotMetaData(
    GetStorageUsageCallback callback,
    leveldb::mojom::DatabaseError status,
    std::vector<leveldb::mojom::KeyValuePtr> data) {
  std::vector<LocalStorageUsageInfo> result;
  for (const auto& row : data) {
    DCHECK_GT(row->key.size(), arraysize(kMetaPrefix));
    LocalStorageUsageInfo info;
    info.origin = GURL(leveldb::Uint8VectorToStdString(row->key).substr(
        arraysize(kMetaPrefix)));
    if (!info.origin.is_valid()) {
      // TODO(mek): Deal with database corruption.
      continue;
    }

    LocalStorageOriginMetaData data;
    if (!data.ParseFromArray(row->value.data(), row->value.size())) {
      // TODO(mek): Deal with database corruption.
      continue;
    }
    info.data_size = data.size_bytes();
    info.last_modified = base::Time::FromInternalValue(data.last_modified());
    result.push_back(std::move(info));
  }
  std::move(callback).Run(std::move(result));
}

void LocalStorageContextMojo::OnGotStorageUsageForDeletePhysicalOrigin(
    const url::Origin& origin,
    std::vector<LocalStorageUsageInfo> usage) {
  for (const auto& info : usage) {
    url::Origin origin_candidate(info.origin);
    if (!origin_candidate.IsSameOriginWith(origin) &&
        origin_candidate.IsSamePhysicalOriginWith(origin))
      DeleteStorage(origin_candidate);
  }
  DeleteStorage(origin);
}

void LocalStorageContextMojo::OnGotStorageUsageForShutdown(
    std::vector<LocalStorageUsageInfo> usage) {
  std::vector<leveldb::mojom::BatchedOperationPtr> operations;
  for (const auto& info : usage) {
    if (special_storage_policy_->IsStorageProtected(info.origin))
      continue;
    if (!special_storage_policy_->IsStorageSessionOnly(info.origin))
      continue;

    AddDeleteOriginOperations(&operations, url::Origin(info.origin));
  }

  if (!operations.empty()) {
    database_->Write(std::move(operations),
                     base::Bind(&LocalStorageContextMojo::OnShutdownComplete,
                                base::Unretained(this)));
  } else {
    OnShutdownComplete(leveldb::mojom::DatabaseError::OK);
  }
}

void LocalStorageContextMojo::OnShutdownComplete(
    leveldb::mojom::DatabaseError error) {
  delete this;
}

}  // namespace content
