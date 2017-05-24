// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/update_client/component.h"

#include <algorithm>
#include <utility>

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/files/file_util.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/memory/ptr_util.h"
#include "base/single_thread_task_runner.h"
#include "base/threading/thread_task_runner_handle.h"
#include "components/update_client/component_unpacker.h"
#include "components/update_client/configurator.h"
#include "components/update_client/protocol_builder.h"
#include "components/update_client/update_client.h"
#include "components/update_client/update_client_errors.h"
#include "components/update_client/update_engine.h"
#include "components/update_client/utils.h"

// The state machine representing how a CRX component changes during an update.
//
//
//                  on-demand                on-demand
//   +--------------------------> kNew <---------------+-------------+
//   |                              |                  |             |
//   |                              V                  |             |
//   |   +---------------0----> kChecking -<-------+---|---<-----+   |
//   |   |                          |              |   |         |   |
//   |   |            error         V       no     |   |         |   |
//  kUpdateError <------------- [update?] ->---- kUpToDate     kUpdated
//     ^                            |                              ^
//     |                        yes |                              |
//     |                            V                              |
//     |                        kCanUpdate                         |
//     |                            |                              |
//     |                            V              no              |
//     |                 [differential update?]--->----+           |
//     |                            |                  |           |
//     |                        yes |                  |           |
//     |                            V          error   |           |
//     |                 kDownloadingDiff --->---------+           |
//     |                            |                  |           |
//     |                            |                  |           |
//     |                            V          error   |           |
//     |                     kUpdatingDiff --->--------+-----------+ success
//     |                                               |           |
//     |              error                            V           |
//     +----------------------------------------- kDownloading     |
//     |                                               |           |
//     |              error                            V           |
//     +------------------------------------------ kUpdating ->----+ success

namespace update_client {

namespace {

using InstallOnBlockingTaskRunnerCompleteCallback =
    base::Callback<void(int error_category, int error_code, int extra_code1)>;

CrxInstaller::Result DoInstallOnBlockingTaskRunner(
    const scoped_refptr<base::SingleThreadTaskRunner>& main_task_runner,
    const scoped_refptr<base::SequencedTaskRunner>& blocking_task_runner,
    const base::FilePath& unpack_path,
    const std::string& fingerprint,
    const scoped_refptr<CrxInstaller>& installer,
    InstallOnBlockingTaskRunnerCompleteCallback callback) {
  DCHECK(blocking_task_runner->RunsTasksOnCurrentThread());

  if (static_cast<int>(fingerprint.size()) !=
      base::WriteFile(
          unpack_path.Append(FILE_PATH_LITERAL("manifest.fingerprint")),
          fingerprint.c_str(), base::checked_cast<int>(fingerprint.size()))) {
    return CrxInstaller::Result(InstallError::FINGERPRINT_WRITE_FAILED);
  }

  std::unique_ptr<base::DictionaryValue> manifest = ReadManifest(unpack_path);
  if (!manifest)
    return CrxInstaller::Result(InstallError::BAD_MANIFEST);

  return installer->Install(*manifest, unpack_path);
}

void InstallOnBlockingTaskRunner(
    const scoped_refptr<base::SingleThreadTaskRunner>& main_task_runner,
    const scoped_refptr<base::SequencedTaskRunner>& blocking_task_runner,
    const base::FilePath& unpack_path,
    const std::string& fingerprint,
    const scoped_refptr<CrxInstaller>& installer,
    InstallOnBlockingTaskRunnerCompleteCallback callback) {
  DCHECK(blocking_task_runner->RunsTasksOnCurrentThread());

  const auto result = DoInstallOnBlockingTaskRunner(
      main_task_runner, blocking_task_runner, unpack_path, fingerprint,
      installer, callback);

  const ErrorCategory error_category =
      result.error ? ErrorCategory::kInstallError : ErrorCategory::kErrorNone;
  main_task_runner->PostTask(
      FROM_HERE,
      base::Bind(callback, static_cast<int>(error_category),
                 static_cast<int>(result.error), result.extended_error));
}

void UnpackCompleteOnBlockingTaskRunner(
    const scoped_refptr<base::SingleThreadTaskRunner>& main_task_runner,
    const scoped_refptr<base::SequencedTaskRunner>& blocking_task_runner,
    const base::FilePath& crx_path,
    const std::string& fingerprint,
    const scoped_refptr<CrxInstaller>& installer,
    InstallOnBlockingTaskRunnerCompleteCallback callback,
    const ComponentUnpacker::Result& result) {
  DCHECK(blocking_task_runner->RunsTasksOnCurrentThread());

  update_client::DeleteFileAndEmptyParentDirectory(crx_path);

  if (result.error != UnpackerError::kNone) {
    main_task_runner->PostTask(
        FROM_HERE,
        base::Bind(callback, static_cast<int>(ErrorCategory::kUnpackError),
                   static_cast<int>(result.error), result.extended_error));
    return;
  }

  blocking_task_runner->PostTask(
      FROM_HERE, base::Bind(&InstallOnBlockingTaskRunner, main_task_runner,
                            blocking_task_runner, result.unpack_path,
                            fingerprint, installer, callback));
}

void StartInstallOnBlockingTaskRunner(
    const scoped_refptr<base::SingleThreadTaskRunner>& main_task_runner,
    const scoped_refptr<base::SequencedTaskRunner>& blocking_task_runner,
    const std::vector<uint8_t>& pk_hash,
    const base::FilePath& crx_path,
    const std::string& fingerprint,
    const scoped_refptr<CrxInstaller>& installer,
    const scoped_refptr<OutOfProcessPatcher>& oop_patcher,
    InstallOnBlockingTaskRunnerCompleteCallback callback) {
  DCHECK(blocking_task_runner->RunsTasksOnCurrentThread());

  auto unpacker = base::MakeRefCounted<ComponentUnpacker>(
      pk_hash, crx_path, installer, oop_patcher, blocking_task_runner);

  unpacker->Unpack(base::Bind(&UnpackCompleteOnBlockingTaskRunner,
                              main_task_runner, blocking_task_runner, crx_path,
                              fingerprint, installer, callback));
}

}  // namespace

Component::Component(const UpdateContext& update_context, const std::string& id)
    : id_(id),
      state_(base::MakeUnique<StateNew>(this)),
      update_context_(update_context) {}

Component::~Component() {}

void Component::Handle(CallbackHandleComplete callback) {
  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK(state_);

  callback_handle_complete_ = callback;

  state_->Handle(base::Bind(&Component::ChangeState, base::Unretained(this)));
}

void Component::ChangeState(std::unique_ptr<State> next_state) {
  DCHECK(thread_checker_.CalledOnValidThread());

  if (next_state)
    state_ = std::move(next_state);

  base::ThreadTaskRunnerHandle::Get()->PostTask(FROM_HERE,
                                                callback_handle_complete_);
}

CrxUpdateItem Component::GetCrxUpdateItem() const {
  DCHECK(thread_checker_.CalledOnValidThread());

  CrxUpdateItem crx_update_item;
  crx_update_item.state = state_->state();
  crx_update_item.id = id_;
  crx_update_item.component = crx_component_;
  crx_update_item.last_check = last_check_;
  crx_update_item.next_version = next_version_;
  crx_update_item.next_fp = next_fp_;

  return crx_update_item;
}

void Component::SetParseResult(const ProtocolParser::Result& result) {
  DCHECK(thread_checker_.CalledOnValidThread());

  DCHECK_EQ(0, update_check_error_);

  status_ = result.status;
  action_run_ = result.action_run;

  if (result.manifest.packages.empty())
    return;

  next_version_ = base::Version(result.manifest.version);
  const auto& package = result.manifest.packages.front();
  next_fp_ = package.fingerprint;

  // Resolve the urls by combining the base urls with the package names.
  for (const auto& crx_url : result.crx_urls) {
    const GURL url = crx_url.Resolve(package.name);
    if (url.is_valid())
      crx_urls_.push_back(url);
  }
  for (const auto& crx_diffurl : result.crx_diffurls) {
    const GURL url = crx_diffurl.Resolve(package.namediff);
    if (url.is_valid())
      crx_diffurls_.push_back(url);
  }

  hash_sha256_ = package.hash_sha256;
  hashdiff_sha256_ = package.hashdiff_sha256;
}

void Component::Uninstall(const base::Version& version, int reason) {
  DCHECK(thread_checker_.CalledOnValidThread());

  DCHECK_EQ(ComponentState::kNew, state());

  previous_version_ = version;
  next_version_ = base::Version("0");
  extra_code1_ = reason;

  state_ = base::MakeUnique<StateUninstalled>(this);
}

void Component::UpdateCheckComplete() const {
  DCHECK(thread_checker_.CalledOnValidThread());

  DCHECK_EQ(ComponentState::kChecking, state());

  base::ThreadTaskRunnerHandle::Get()->PostTask(FROM_HERE,
                                                update_check_complete_);
}

bool Component::CanDoBackgroundDownload() const {
  // On demand component updates are always downloaded in foreground.
  return !on_demand_ && crx_component_.allows_background_download &&
         update_context_.config->EnabledBackgroundDownloader();
}

void Component::AppendEvent(const std::string& event) {
  events_.push_back(event);
}

void Component::NotifyObservers(UpdateClient::Observer::Events event) const {
  DCHECK(thread_checker_.CalledOnValidThread());
  update_context_.notify_observers_callback.Run(event, id_);
}

base::TimeDelta Component::GetUpdateDuration() const {
  DCHECK(thread_checker_.CalledOnValidThread());

  if (update_begin_.is_null())
    return base::TimeDelta();

  const base::TimeDelta update_cost(base::TimeTicks::Now() - update_begin_);
  DCHECK_GE(update_cost, base::TimeDelta());
  const base::TimeDelta max_update_delay =
      base::TimeDelta::FromSeconds(update_context_.config->UpdateDelay());
  return std::min(update_cost, max_update_delay);
}

Component::State::State(Component* component, ComponentState state)
    : state_(state), component_(*component) {}

Component::State::~State() {}

void Component::State::Handle(CallbackNextState callback) {
  DCHECK(thread_checker_.CalledOnValidThread());

  callback_ = callback;

  DCHECK(!is_final_);
  DoHandle();
}

void Component::State::TransitionState(std::unique_ptr<State> next_state) {
  if (!next_state)
    is_final_ = true;

  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE, base::Bind(callback(), base::Passed(&next_state)));
}

Component::StateNew::StateNew(Component* component)
    : State(component, ComponentState::kNew) {}

Component::StateNew::~StateNew() {
  DCHECK(thread_checker_.CalledOnValidThread());
}

void Component::StateNew::DoHandle() {
  DCHECK(thread_checker_.CalledOnValidThread());

  auto& component = State::component();

  TransitionState(base::MakeUnique<StateChecking>(&component));
}

Component::StateChecking::StateChecking(Component* component)
    : State(component, ComponentState::kChecking) {}

Component::StateChecking::~StateChecking() {
  DCHECK(thread_checker_.CalledOnValidThread());
}

// Unlike how other states are handled, this function does not change the
// state right away. The state transition happens when the UpdateChecker
// calls Component::UpdateCheckComplete and |update_check_complete_| is invoked.
// This is an artifact of how multiple components must be checked for updates
// together but the state machine defines the transitions for one component
// at a time.
void Component::StateChecking::DoHandle() {
  DCHECK(thread_checker_.CalledOnValidThread());

  auto& component = State::component();

  component.last_check_ = base::TimeTicks::Now();
  component.update_check_complete_ = base::Bind(
      &Component::StateChecking::UpdateCheckComplete, base::Unretained(this));

  component.NotifyObservers(Events::COMPONENT_CHECKING_FOR_UPDATES);
}

void Component::StateChecking::UpdateCheckComplete() {
  DCHECK(thread_checker_.CalledOnValidThread());
  auto& component = State::component();
  if (!component.update_check_error_) {
    if (component.status_ == "ok") {
      TransitionState(base::MakeUnique<StateCanUpdate>(&component));
      return;
    }

    if (component.status_ == "noupdate") {
      TransitionState(base::MakeUnique<StateUpToDate>(&component));
      return;
    }
  }

  TransitionState(base::MakeUnique<StateUpdateError>(&component));
}

Component::StateUpdateError::StateUpdateError(Component* component)
    : State(component, ComponentState::kUpdateError) {}

Component::StateUpdateError::~StateUpdateError() {
  DCHECK(thread_checker_.CalledOnValidThread());
}

void Component::StateUpdateError::DoHandle() {
  DCHECK(thread_checker_.CalledOnValidThread());

  auto& component = State::component();

  // Create an event only when the server response included an update.
  if (component.IsUpdateAvailable())
    component.AppendEvent(BuildUpdateCompleteEventElement(component));

  TransitionState(nullptr);
  component.NotifyObservers(Events::COMPONENT_NOT_UPDATED);
}

Component::StateCanUpdate::StateCanUpdate(Component* component)
    : State(component, ComponentState::kCanUpdate) {}

Component::StateCanUpdate::~StateCanUpdate() {
  DCHECK(thread_checker_.CalledOnValidThread());
}

void Component::StateCanUpdate::DoHandle() {
  DCHECK(thread_checker_.CalledOnValidThread());

  auto& component = State::component();

  component.is_update_available_ = true;
  component.NotifyObservers(Events::COMPONENT_UPDATE_FOUND);

  if (component.crx_component_.supports_group_policy_enable_component_updates &&
      !component.update_context_.enabled_component_updates) {
    component.error_category_ = static_cast<int>(ErrorCategory::kServiceError);
    component.error_code_ = static_cast<int>(ServiceError::UPDATE_DISABLED);
    component.extra_code1_ = 0;
    TransitionState(base::MakeUnique<StateUpdateError>(&component));
    return;
  }

  // Start computing the cost of the this update from here on.
  component.update_begin_ = base::TimeTicks::Now();

  if (CanTryDiffUpdate())
    TransitionState(base::MakeUnique<StateDownloadingDiff>(&component));
  else
    TransitionState(base::MakeUnique<StateDownloading>(&component));
}

// Returns true if a differential update is available, it has not failed yet,
// and the configuration allows this update.
bool Component::StateCanUpdate::CanTryDiffUpdate() const {
  const auto& component = Component::State::component();
  return HasDiffUpdate(component) && !component.diff_error_code_ &&
         component.update_context_.config->EnabledDeltas();
}

Component::StateUpToDate::StateUpToDate(Component* component)
    : State(component, ComponentState::kUpToDate) {}

Component::StateUpToDate::~StateUpToDate() {
  DCHECK(thread_checker_.CalledOnValidThread());
}

void Component::StateUpToDate::DoHandle() {
  DCHECK(thread_checker_.CalledOnValidThread());

  auto& component = State::component();

  TransitionState(nullptr);
  component.NotifyObservers(Events::COMPONENT_NOT_UPDATED);
}

Component::StateDownloadingDiff::StateDownloadingDiff(Component* component)
    : State(component, ComponentState::kDownloadingDiff) {}

Component::StateDownloadingDiff::~StateDownloadingDiff() {
  DCHECK(thread_checker_.CalledOnValidThread());
}

void Component::StateDownloadingDiff::DoHandle() {
  DCHECK(thread_checker_.CalledOnValidThread());

  const auto& component = Component::State::component();
  const auto& update_context = component.update_context_;

  crx_downloader_ = update_context.crx_downloader_factory(
      component.CanDoBackgroundDownload(),
      update_context.config->RequestContext(),
      update_context.blocking_task_runner);

  const auto& id = component.id_;
  crx_downloader_->set_progress_callback(
      base::Bind(&Component::StateDownloadingDiff::DownloadProgress,
                 base::Unretained(this), id));
  crx_downloader_->StartDownload(
      component.crx_diffurls_, component.hashdiff_sha256_,
      base::Bind(&Component::StateDownloadingDiff::DownloadComplete,
                 base::Unretained(this), id));

  component.NotifyObservers(Events::COMPONENT_UPDATE_DOWNLOADING);
}

// Called when progress is being made downloading a CRX. The progress may
// not monotonically increase due to how the CRX downloader switches between
// different downloaders and fallback urls.
void Component::StateDownloadingDiff::DownloadProgress(
    const std::string& id,
    const CrxDownloader::Result& download_result) {
  DCHECK(thread_checker_.CalledOnValidThread());

  component().NotifyObservers(Events::COMPONENT_UPDATE_DOWNLOADING);
}

void Component::StateDownloadingDiff::DownloadComplete(
    const std::string& id,
    const CrxDownloader::Result& download_result) {
  DCHECK(thread_checker_.CalledOnValidThread());

  auto& component = Component::State::component();

  for (const auto& metrics : crx_downloader_->download_metrics())
    component.AppendEvent(BuildDownloadCompleteEventElement(metrics));

  crx_downloader_.reset();

  if (download_result.error) {
    component.diff_error_category_ =
        static_cast<int>(ErrorCategory::kNetworkError);
    component.diff_error_code_ = download_result.error;

    TransitionState(base::MakeUnique<StateDownloading>(&component));
    return;
  }

  component.crx_path_ = download_result.response;

  TransitionState(base::MakeUnique<StateUpdatingDiff>(&component));
}

Component::StateDownloading::StateDownloading(Component* component)
    : State(component, ComponentState::kDownloading) {}

Component::StateDownloading::~StateDownloading() {
  DCHECK(thread_checker_.CalledOnValidThread());
}

void Component::StateDownloading::DoHandle() {
  DCHECK(thread_checker_.CalledOnValidThread());

  const auto& component = Component::State::component();
  const auto& update_context = component.update_context_;

  crx_downloader_ = update_context.crx_downloader_factory(
      component.CanDoBackgroundDownload(),
      update_context.config->RequestContext(),
      update_context.blocking_task_runner);

  const auto& id = component.id_;
  crx_downloader_->set_progress_callback(
      base::Bind(&Component::StateDownloading::DownloadProgress,
                 base::Unretained(this), id));
  crx_downloader_->StartDownload(
      component.crx_urls_, component.hash_sha256_,
      base::Bind(&Component::StateDownloading::DownloadComplete,
                 base::Unretained(this), id));

  component.NotifyObservers(Events::COMPONENT_UPDATE_DOWNLOADING);
}

// Called when progress is being made downloading a CRX. The progress may
// not monotonically increase due to how the CRX downloader switches between
// different downloaders and fallback urls.
void Component::StateDownloading::DownloadProgress(
    const std::string& id,
    const CrxDownloader::Result& download_result) {
  DCHECK(thread_checker_.CalledOnValidThread());

  component().NotifyObservers(Events::COMPONENT_UPDATE_DOWNLOADING);
}

void Component::StateDownloading::DownloadComplete(
    const std::string& id,
    const CrxDownloader::Result& download_result) {
  DCHECK(thread_checker_.CalledOnValidThread());

  auto& component = Component::State::component();

  for (const auto& metrics : crx_downloader_->download_metrics())
    component.AppendEvent(BuildDownloadCompleteEventElement(metrics));

  crx_downloader_.reset();

  if (download_result.error) {
    component.error_category_ = static_cast<int>(ErrorCategory::kNetworkError);
    component.error_code_ = download_result.error;

    TransitionState(base::MakeUnique<StateUpdateError>(&component));
    return;
  }

  component.crx_path_ = download_result.response;

  TransitionState(base::MakeUnique<StateUpdating>(&component));
}

Component::StateUpdatingDiff::StateUpdatingDiff(Component* component)
    : State(component, ComponentState::kUpdatingDiff) {}

Component::StateUpdatingDiff::~StateUpdatingDiff() {
  DCHECK(thread_checker_.CalledOnValidThread());
}

void Component::StateUpdatingDiff::DoHandle() {
  DCHECK(thread_checker_.CalledOnValidThread());

  const auto& component = Component::State::component();
  const auto& update_context = component.update_context_;

  component.NotifyObservers(Events::COMPONENT_UPDATE_READY);

  update_context.blocking_task_runner->PostTask(
      FROM_HERE,
      base::Bind(&update_client::StartInstallOnBlockingTaskRunner,
                 base::ThreadTaskRunnerHandle::Get(),
                 update_context.blocking_task_runner,
                 component.crx_component_.pk_hash, component.crx_path_,
                 component.next_fp_, component.crx_component_.installer,
                 update_context.config->CreateOutOfProcessPatcher(),
                 base::Bind(&Component::StateUpdatingDiff::InstallComplete,
                            base::Unretained(this))));
}

void Component::StateUpdatingDiff::InstallComplete(int error_category,
                                                   int error_code,
                                                   int extra_code1) {
  DCHECK(thread_checker_.CalledOnValidThread());

  auto& component = Component::State::component();

  component.diff_error_category_ = error_category;
  component.diff_error_code_ = error_code;
  component.diff_extra_code1_ = extra_code1;

  if (component.diff_error_code_ != 0) {
    TransitionState(base::MakeUnique<StateDownloading>(&component));
    return;
  }

  DCHECK_EQ(static_cast<int>(ErrorCategory::kErrorNone),
            component.diff_error_category_);
  DCHECK_EQ(0, component.diff_error_code_);
  DCHECK_EQ(0, component.diff_extra_code1_);

  DCHECK_EQ(static_cast<int>(ErrorCategory::kErrorNone),
            component.error_category_);
  DCHECK_EQ(0, component.error_code_);
  DCHECK_EQ(0, component.extra_code1_);

  TransitionState(base::MakeUnique<StateUpdated>(&component));
}

Component::StateUpdating::StateUpdating(Component* component)
    : State(component, ComponentState::kUpdating) {}

Component::StateUpdating::~StateUpdating() {
  DCHECK(thread_checker_.CalledOnValidThread());
}

void Component::StateUpdating::DoHandle() {
  DCHECK(thread_checker_.CalledOnValidThread());

  const auto& component = Component::State::component();
  const auto& update_context = component.update_context_;

  component.NotifyObservers(Events::COMPONENT_UPDATE_READY);

  update_context.blocking_task_runner->PostTask(
      FROM_HERE,
      base::Bind(&update_client::StartInstallOnBlockingTaskRunner,
                 base::ThreadTaskRunnerHandle::Get(),
                 update_context.blocking_task_runner,
                 component.crx_component_.pk_hash, component.crx_path_,
                 component.next_fp_, component.crx_component_.installer,
                 update_context.config->CreateOutOfProcessPatcher(),
                 base::Bind(&Component::StateUpdating::InstallComplete,
                            base::Unretained(this))));
}

void Component::StateUpdating::InstallComplete(int error_category,
                                               int error_code,
                                               int extra_code1) {
  DCHECK(thread_checker_.CalledOnValidThread());

  auto& component = Component::State::component();

  component.error_category_ = error_category;
  component.error_code_ = error_code;
  component.extra_code1_ = extra_code1;

  if (component.error_code_ != 0) {
    TransitionState(base::MakeUnique<StateUpdateError>(&component));
    return;
  }

  DCHECK_EQ(static_cast<int>(ErrorCategory::kErrorNone),
            component.error_category_);
  DCHECK_EQ(0, component.error_code_);
  DCHECK_EQ(0, component.extra_code1_);

  TransitionState(base::MakeUnique<StateUpdated>(&component));
}

Component::StateUpdated::StateUpdated(Component* component)
    : State(component, ComponentState::kUpdated) {
  DCHECK(thread_checker_.CalledOnValidThread());
}

Component::StateUpdated::~StateUpdated() {
  DCHECK(thread_checker_.CalledOnValidThread());
}

void Component::StateUpdated::DoHandle() {
  DCHECK(thread_checker_.CalledOnValidThread());

  auto& component = State::component();
  component.crx_component_.version = component.next_version_;
  component.crx_component_.fingerprint = component.next_fp_;

  component.AppendEvent(BuildUpdateCompleteEventElement(component));

  TransitionState(nullptr);
  component.NotifyObservers(Events::COMPONENT_UPDATED);
}

Component::StateUninstalled::StateUninstalled(Component* component)
    : State(component, ComponentState::kUninstalled) {
  DCHECK(thread_checker_.CalledOnValidThread());
}

Component::StateUninstalled::~StateUninstalled() {
  DCHECK(thread_checker_.CalledOnValidThread());
}

void Component::StateUninstalled::DoHandle() {
  DCHECK(thread_checker_.CalledOnValidThread());

  auto& component = State::component();
  component.AppendEvent(BuildUninstalledEventElement(component));

  TransitionState(nullptr);
}

}  // namespace update_client
