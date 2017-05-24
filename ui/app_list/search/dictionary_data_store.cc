// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/app_list/search/dictionary_data_store.h"

#include <utility>

#include "base/callback.h"
#include "base/json/json_file_value_serializer.h"
#include "base/json/json_string_value_serializer.h"
#include "base/logging.h"
#include "base/sequenced_task_runner.h"
#include "base/strings/string_number_conversions.h"
#include "base/task_runner_util.h"
#include "base/threading/sequenced_worker_pool.h"
#include "base/values.h"

namespace app_list {

DictionaryDataStore::DictionaryDataStore(const base::FilePath& data_file,
                                         base::SequencedWorkerPool* worker_pool)
    : data_file_(data_file), worker_pool_(worker_pool) {
  std::string token("app-launcher-data-store");
  token.append(data_file.AsUTF8Unsafe());

  // Uses a SKIP_ON_SHUTDOWN file task runner because losing a couple
  // associations is better than blocking shutdown.
  file_task_runner_ = worker_pool->GetSequencedTaskRunnerWithShutdownBehavior(
      worker_pool->GetNamedSequenceToken(token),
      base::SequencedWorkerPool::SKIP_ON_SHUTDOWN);
  writer_.reset(
      new base::ImportantFileWriter(data_file, file_task_runner_.get()));

  cached_dict_.reset(new base::DictionaryValue);
}

DictionaryDataStore::~DictionaryDataStore() {
  Flush(OnFlushedCallback());
}

void DictionaryDataStore::Flush(const OnFlushedCallback& on_flushed) {
  if (writer_->HasPendingWrite())
    writer_->DoScheduledWrite();

  if (on_flushed.is_null())
    return;

  file_task_runner_->PostTaskAndReply(
      FROM_HERE, base::Bind(&base::DoNothing), on_flushed);
}

void DictionaryDataStore::Load(
    const DictionaryDataStore::OnLoadedCallback& on_loaded) {
  base::PostTaskAndReplyWithResult(
      file_task_runner_.get(),
      FROM_HERE,
      base::Bind(&DictionaryDataStore::LoadOnBlockingPool, this),
      on_loaded);
}

void DictionaryDataStore::ScheduleWrite() {
  writer_->ScheduleWrite(this);
}

std::unique_ptr<base::DictionaryValue>
DictionaryDataStore::LoadOnBlockingPool() {
  DCHECK(worker_pool_->RunsTasksInCurrentSequence());

  int error_code = JSONFileValueDeserializer::JSON_NO_ERROR;
  std::string error_message;
  JSONFileValueDeserializer deserializer(data_file_);
  std::unique_ptr<base::DictionaryValue> dict_value =
      base::DictionaryValue::From(
          deserializer.Deserialize(&error_code, &error_message));
  if (error_code != JSONFileValueDeserializer::JSON_NO_ERROR || !dict_value) {
    return nullptr;
  }

  std::unique_ptr<base::DictionaryValue> return_dict =
      dict_value->CreateDeepCopy();
  cached_dict_ = std::move(dict_value);
  return return_dict;
}

bool DictionaryDataStore::SerializeData(std::string* data) {
  JSONStringValueSerializer serializer(data);
  serializer.set_pretty_print(true);
  return serializer.Serialize(*cached_dict_.get());
}

}  // namespace app_list
