// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_DOM_STORAGE_LOCAL_STORAGE_CONTEXT_MOJO_H_
#define CONTENT_BROWSER_DOM_STORAGE_LOCAL_STORAGE_CONTEXT_MOJO_H_

#include <memory>

#include "base/files/file_path.h"
#include "content/common/content_export.h"
#include "content/common/leveldb_wrapper.mojom.h"
#include "content/public/browser/browser_thread.h"
#include "services/file/public/interfaces/file_system.mojom.h"
#include "url/origin.h"

namespace service_manager {
class Connector;
}

namespace storage {
class SpecialStoragePolicy;
}

namespace content {

class DOMStorageTaskRunner;
class LevelDBWrapperImpl;
struct LocalStorageUsageInfo;

// Used for mojo-based LocalStorage implementation (behind --mojo-local-storage
// for now).
// Created on the UI thread, but all further methods are called on the IO
// thread. Furthermore since destruction of this class can involve asynchronous
// steps, it can only be deleted by calling ShutdownAndDelete (on the IO
// thread),
class CONTENT_EXPORT LocalStorageContextMojo {
 public:
  using GetStorageUsageCallback =
      base::OnceCallback<void(std::vector<LocalStorageUsageInfo>)>;

  LocalStorageContextMojo(
      service_manager::Connector* connector,
      scoped_refptr<DOMStorageTaskRunner> task_runner,
      const base::FilePath& old_localstorage_path,
      const base::FilePath& subdirectory,
      scoped_refptr<storage::SpecialStoragePolicy> special_storage_policy);

  void OpenLocalStorage(const url::Origin& origin,
                        mojom::LevelDBWrapperRequest request);
  void GetStorageUsage(GetStorageUsageCallback callback);
  void DeleteStorage(const url::Origin& origin);
  // Like DeleteStorage(), but also deletes storage for all sub-origins.
  void DeleteStorageForPhysicalOrigin(const url::Origin& origin);
  void Flush();

  // Used by content settings to alter the behavior around
  // what data to keep and what data to discard at shutdown.
  // The policy is not so straight forward to describe, see
  // the implementation for details.
  void SetForceKeepSessionState() { force_keep_session_state_ = true; }

  // Called when the owning BrowserContext is ending.
  // Schedules the commit of any unsaved changes and will delete
  // and keep data on disk per the content settings and special storage
  // policies.
  void ShutdownAndDelete();

  // Clears any caches, to free up as much memory as possible. Next access to
  // storage for a particular origin will reload the data from the database.
  void PurgeMemory();

  leveldb::mojom::LevelDBDatabaseAssociatedRequest DatabaseRequestForTesting();

  // Converts a string from the old storage format to the new storage format.
  static std::vector<uint8_t> MigrateString(const base::string16& input);

 private:
  friend class MojoDOMStorageBrowserTest;

  class LevelDBWrapperHolder;

  ~LocalStorageContextMojo();

  // Runs |callback| immediately if already connected to a database, otherwise
  // delays running |callback| untill after a connection has been established.
  // Initiates connecting to the database if no connection is in progres yet.
  void RunWhenConnected(base::OnceClosure callback);

  // Part of our asynchronous directory opening called from RunWhenConnected().
  void InitiateConnection(bool in_memory_only = false);
  void OnDirectoryOpened(filesystem::mojom::FileError err);
  void OnDatabaseOpened(bool in_memory, leveldb::mojom::DatabaseError status);
  void OnGotDatabaseVersion(leveldb::mojom::DatabaseError status,
                            const std::vector<uint8_t>& value);
  void OnConnectionFinished();
  void DeleteAndRecreateDatabase();
  void OnDBDestroyed(bool recreate_in_memory,
                     leveldb::mojom::DatabaseError status);

  // The (possibly delayed) implementation of OpenLocalStorage(). Can be called
  // directly from that function, or through |on_database_open_callbacks_|.
  void BindLocalStorage(const url::Origin& origin,
                        mojom::LevelDBWrapperRequest request);
  LevelDBWrapperImpl* GetOrCreateDBWrapper(const url::Origin& origin);

  // The (possibly delayed) implementation of GetStorageUsage(). Can be called
  // directly from that function, or through |on_database_open_callbacks_|.
  void RetrieveStorageUsage(GetStorageUsageCallback callback);
  void OnGotMetaData(GetStorageUsageCallback callback,
                     leveldb::mojom::DatabaseError status,
                     std::vector<leveldb::mojom::KeyValuePtr> data);

  void OnGotStorageUsageForDeletePhysicalOrigin(
      const url::Origin& origin,
      std::vector<LocalStorageUsageInfo> usage);

  void OnGotStorageUsageForShutdown(std::vector<LocalStorageUsageInfo> usage);
  void OnShutdownComplete(leveldb::mojom::DatabaseError error);

  std::unique_ptr<service_manager::Connector> connector_;
  const base::FilePath subdirectory_;

  enum ConnectionState {
    NO_CONNECTION,
    CONNECTION_IN_PROGRESS,
    CONNECTION_FINISHED,
    CONNECTION_SHUTDOWN
  } connection_state_ = NO_CONNECTION;
  bool database_initialized_ = false;

  bool force_keep_session_state_ = false;
  scoped_refptr<storage::SpecialStoragePolicy> special_storage_policy_;

  file::mojom::FileSystemPtr file_system_;
  filesystem::mojom::DirectoryPtr directory_;

  leveldb::mojom::LevelDBServicePtr leveldb_service_;
  leveldb::mojom::LevelDBDatabaseAssociatedPtr database_;
  bool tried_to_recreate_ = false;

  std::vector<base::OnceClosure> on_database_opened_callbacks_;

  // Maps between an origin and its prefixed LevelDB view.
  std::map<url::Origin, std::unique_ptr<LevelDBWrapperHolder>>
      level_db_wrappers_;

  // Used to access old data for migration.
  scoped_refptr<DOMStorageTaskRunner> task_runner_;
  base::FilePath old_localstorage_path_;

  base::WeakPtrFactory<LocalStorageContextMojo> weak_ptr_factory_;
};

}  // namespace content

#endif  // CONTENT_BROWSER_DOM_STORAGE_LOCAL_STORAGE_CONTEXT_MOJO_H_
