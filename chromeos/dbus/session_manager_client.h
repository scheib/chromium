// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMEOS_DBUS_SESSION_MANAGER_CLIENT_H_
#define CHROMEOS_DBUS_SESSION_MANAGER_CLIENT_H_

#include <map>
#include <string>
#include <vector>

#include "base/callback.h"
#include "base/macros.h"
#include "base/observer_list.h"
#include "base/time/time.h"
#include "chromeos/chromeos_export.h"
#include "chromeos/dbus/dbus_client.h"
#include "chromeos/dbus/dbus_client_implementation_type.h"
#include "chromeos/dbus/dbus_method_call_status.h"
#include "third_party/cros_system_api/dbus/login_manager/dbus-constants.h"

namespace cryptohome {
class Identification;
}

namespace chromeos {

// SessionManagerClient is used to communicate with the session manager.
class CHROMEOS_EXPORT SessionManagerClient : public DBusClient {
 public:
  // The result type received from session manager on request to retrieve the
  // policy. Used to define the buckets for an enumerated UMA histogram.
  // Hence,
  //   (a) existing enumerated constants should never be deleted or reordered.
  //   (b) new constants should be inserted immediately before COUNT.
  enum class RetrievePolicyResponseType {
    // Other type of error while retrieving policy data (e.g. D-Bus timeout).
    OTHER_ERROR = 0,
    // The policy was retrieved successfully.
    SUCCESS = 1,
    // Retrieve policy request issued before session started.
    SESSION_DOES_NOT_EXIST = 2,
    // Session manager failed to encode the policy data.
    POLICY_ENCODE_ERROR = 3,
    // Has to be the last value of enumeration. Used for UMA.
    COUNT
  };

  // Interface for observing changes from the session manager.
  class Observer {
   public:
    virtual ~Observer() {}

    // Called when the owner key is set.
    virtual void OwnerKeySet(bool success) {}

    // Called when the property change is complete.
    virtual void PropertyChangeComplete(bool success) {}

    // Called when the session manager announces that the screen has been locked
    // successfully (i.e. after NotifyLockScreenShown() has been called).
    virtual void ScreenIsLocked() {}

    // Called when the session manager announces that the screen has been
    // unlocked successfully (i.e. after NotifyLockScreenDismissed() has
    // been called).
    virtual void ScreenIsUnlocked() {}

    // Called after EmitLoginPromptVisible is called.
    virtual void EmitLoginPromptVisibleCalled() {}

    // Called when the ARC instance is stopped after it had already started.
    // |clean| is true if the instance was stopped as a result of an explicit
    // request, false if it died unexpectedly.
    // |container_instance_id| is the identifier of the container instance.
    // See details for StartArcInstanceCallback.
    virtual void ArcInstanceStopped(bool clean,
                                    const std::string& container_instance_id) {}
  };

  // Interface for performing actions on behalf of the stub implementation.
  class StubDelegate {
   public:
    virtual ~StubDelegate() {}

    // Locks the screen. Invoked by the stub when RequestLockScreen() is called.
    // In the real implementation of SessionManagerClient::RequestLockScreen(),
    // a lock request is forwarded to the session manager; in the stub, this is
    // short-circuited and the screen is locked immediately.
    virtual void LockScreenForStub() = 0;
  };

  // Sets the delegate used by the stub implementation. Ownership of |delegate|
  // remains with the caller.
  virtual void SetStubDelegate(StubDelegate* delegate) = 0;

  // Adds and removes the observer.
  virtual void AddObserver(Observer* observer) = 0;
  virtual void RemoveObserver(Observer* observer) = 0;
  virtual bool HasObserver(const Observer* observer) const = 0;

  // Returns the most recent screen-lock state received from session_manager.
  // This mirrors the last Observer::ScreenIsLocked() or ScreenIsUnlocked()
  // call.
  virtual bool IsScreenLocked() const = 0;

  // Kicks off an attempt to emit the "login-prompt-visible" upstart signal.
  virtual void EmitLoginPromptVisible() = 0;

  // Restarts the browser job, passing |argv| as the updated command line.
  // The session manager requires a RestartJob caller to open a socket pair and
  // pass one end while holding the local end open for the duration of the call.
  // The session manager uses this to determine whether the PID the restart
  // request originates from belongs to the browser itself.
  // This method duplicates |socket_fd| so it's OK to close the FD without
  // waiting for the result.
  virtual void RestartJob(int socket_fd,
                          const std::vector<std::string>& argv,
                          const VoidDBusMethodCallback& callback) = 0;

  // Starts the session for the user.
  virtual void StartSession(
      const cryptohome::Identification& cryptohome_id) = 0;

  // Stops the current session.
  virtual void StopSession() = 0;

  // Starts the factory reset.
  virtual void StartDeviceWipe() = 0;

  // Locks the screen.
  virtual void RequestLockScreen() = 0;

  // Notifies that the lock screen is shown.
  virtual void NotifyLockScreenShown() = 0;

  // Notifies that the lock screen is dismissed.
  virtual void NotifyLockScreenDismissed() = 0;

  // Notifies that supervised user creation have started.
  virtual void NotifySupervisedUserCreationStarted() = 0;

  // Notifies that supervised user creation have finished.
  virtual void NotifySupervisedUserCreationFinished() = 0;

  // Map that is used to describe the set of active user sessions where |key|
  // is cryptohome id and |value| is user_id_hash.
  using ActiveSessionsMap = std::map<cryptohome::Identification, std::string>;

  // The ActiveSessionsCallback is used for the RetrieveActiveSessions()
  // method. It receives |sessions| argument where the keys are cryptohome_ids
  // for all users that are currently active and |success| argument which
  // indicates whether or not the request succeded.
  using ActiveSessionsCallback =
      base::Callback<void(const ActiveSessionsMap& sessions, bool success)>;

  // Enumerates active user sessions. Usually Chrome naturally keeps track of
  // active users when they are added into current session. When Chrome is
  // restarted after crash by session_manager it only receives cryptohome id and
  // user_id_hash for one user. This method is used to retrieve list of all
  // active users.
  virtual void RetrieveActiveSessions(
      const ActiveSessionsCallback& callback) = 0;

  // Used for RetrieveDevicePolicy, RetrievePolicyForUser and
  // RetrieveDeviceLocalAccountPolicy. Takes a serialized protocol buffer as
  // string.  Upon success, we will pass a protobuf and SUCCESS |response_type|
  // to the callback. On failure, we will pass "" and the details of error type
  // in |response_type|.
  using RetrievePolicyCallback =
      base::Callback<void(const std::string& protobuf,
                          RetrievePolicyResponseType response_type)>;

  // Fetches the device policy blob stored by the session manager.  Upon
  // completion of the retrieve attempt, we will call the provided callback.
  virtual void RetrieveDevicePolicy(const RetrievePolicyCallback& callback) = 0;

  // Same as RetrieveDevicePolicy() but blocks until a reply is received, and
  // populates the policy synchronously. Returns SUCCESS when successful, or
  // the corresponding error from enum in case of a failure.
  // This may only be called in situations where blocking the UI thread is
  // considered acceptable (e.g. restarting the browser after a crash or after
  // a flag change).
  // TODO: Get rid of blocking calls (crbug.com/160522).
  virtual RetrievePolicyResponseType BlockingRetrieveDevicePolicy(
      std::string* policy_out) = 0;

  // Fetches the user policy blob stored by the session manager for the given
  // |cryptohome_id|. Upon completion of the retrieve attempt, we will call the
  // provided callback.
  virtual void RetrievePolicyForUser(
      const cryptohome::Identification& cryptohome_id,
      const RetrievePolicyCallback& callback) = 0;

  // Same as RetrievePolicyForUser() but blocks until a reply is received, and
  // populates the policy synchronously. Returns SUCCESS when successful, or
  // the corresponding error from enum in case of a failure.
  // This may only be called in situations where blocking the UI thread is
  // considered acceptable (e.g. restarting the browser after a crash or after
  // a flag change).
  // TODO: Get rid of blocking calls (crbug.com/160522).
  virtual RetrievePolicyResponseType BlockingRetrievePolicyForUser(
      const cryptohome::Identification& cryptohome_id,
      std::string* policy_out) = 0;

  // Fetches the policy blob associated with the specified device-local account
  // from session manager.  |callback| is invoked up on completion.
  virtual void RetrieveDeviceLocalAccountPolicy(
      const std::string& account_id,
      const RetrievePolicyCallback& callback) = 0;

  // Same as RetrieveDeviceLocalAccountPolicy() but blocks until a reply is
  // received, and populates the policy synchronously. Returns SUCCESS when
  // successful, or the corresponding error from enum in case of a failure.
  // This may only be called in situations where blocking the UI thread is
  // considered acceptable (e.g. restarting the browser after a crash or after
  // a flag change).
  // TODO: Get rid of blocking calls (crbug.com/160522).
  virtual RetrievePolicyResponseType BlockingRetrieveDeviceLocalAccountPolicy(
      const std::string& account_id,
      std::string* policy_out) = 0;

  // Used for StoreDevicePolicy, StorePolicyForUser and
  // StoreDeviceLocalAccountPolicy. Takes a boolean indicating whether the
  // operation was successful or not.
  using StorePolicyCallback = base::Callback<void(bool success)>;

  // Attempts to asynchronously store |policy_blob| as device policy.  Upon
  // completion of the store attempt, we will call callback.
  virtual void StoreDevicePolicy(const std::string& policy_blob,
                                 const StorePolicyCallback& callback) = 0;

  // Attempts to asynchronously store |policy_blob| as user policy for the
  // given |cryptohome_id|. Upon completion of the store attempt, we will call
  // callback.
  virtual void StorePolicyForUser(
      const cryptohome::Identification& cryptohome_id,
      const std::string& policy_blob,
      const StorePolicyCallback& callback) = 0;

  // Sends a request to store a policy blob for the specified device-local
  // account. The result of the operation is reported through |callback|.
  virtual void StoreDeviceLocalAccountPolicy(
      const std::string& account_id,
      const std::string& policy_blob,
      const StorePolicyCallback& callback) = 0;

  // Returns whether session manager can be used to restart Chrome in order to
  // apply per-user session flags.
  virtual bool SupportsRestartToApplyUserFlags() const = 0;

  // Sets the flags to be applied next time by the session manager when Chrome
  // is restarted inside an already started session for a particular user.
  virtual void SetFlagsForUser(const cryptohome::Identification& cryptohome_id,
                               const std::vector<std::string>& flags) = 0;

  using StateKeysCallback =
      base::Callback<void(const std::vector<std::string>& state_keys)>;

  // Get the currently valid server-backed state keys for the device.
  // Server-backed state keys are opaque, device-unique, time-dependent,
  // client-determined identifiers that are used for keying state in the cloud
  // for the device to retrieve after a device factory reset.
  //
  // The state keys are returned asynchronously via |callback|. The callback
  // is invoked with an empty state key vector in case of errors. If the time
  // sync fails or there's no network, the callback is never invoked.
  virtual void GetServerBackedStateKeys(const StateKeysCallback& callback) = 0;

  // Used for several ARC methods.  Takes a boolean indicating whether the
  // operation was successful or not.
  using ArcCallback = base::Callback<void(bool success)>;

  // Used for GetArcStartTime. Takes a boolean indicating whether the
  // operation was successful or not and the ticks of ARC start time if it
  // is successful.
  using GetArcStartTimeCallback =
      base::Callback<void(bool success, base::TimeTicks ticks)>;

  // Asynchronously checks if starting the ARC instance is available.
  // The result of the operation is reported through |callback|.
  // If the operation fails, it is reported as unavailable.
  virtual void CheckArcAvailability(const ArcCallback& callback) = 0;

  // Asynchronously starts the ARC instance for the user whose cryptohome is
  // located by |cryptohome_id|.  Flag |disable_boot_completed_broadcast|
  // blocks Android ACTION_BOOT_COMPLETED broadcast for 3rd party applications.
  // Upon completion, invokes |callback| with the result.
  // Running ARC requires some amount of disk space. LOW_FREE_DISK_SPACE will
  // be returned when there is not enough free disk space for ARC.
  // UNKNOWN_ERROR is returned for any other errors.
  enum class StartArcInstanceResult {
    SUCCESS,
    UNKNOWN_ERROR,
    LOW_FREE_DISK_SPACE,
  };
  // In case of success, |container_instance_id| will be passed as its second
  // param. The ID is passed to ArcInstanceStopped() to identify which instance
  // is stopped.
  using StartArcInstanceCallback =
      base::Callback<void(StartArcInstanceResult result,
                          const std::string& container_instance_id)>;
  virtual void StartArcInstance(const cryptohome::Identification& cryptohome_id,
                                bool disable_boot_completed_broadcast,
                                bool enable_vendor_privileged,
                                const StartArcInstanceCallback& callback) = 0;

  // Asynchronously stops the ARC instance.  Upon completion, invokes
  // |callback| with the result; true on success, false on failure (either
  // session manager failed to stop an instance or session manager can not be
  // reached).
  virtual void StopArcInstance(const ArcCallback& callback) = 0;

  // Adjusts the amount of CPU the ARC instance is allowed to use. When
  // |restriction_state| is CONTAINER_CPU_RESTRICTION_FOREGROUND the limit is
  // adjusted so ARC can use all the system's CPU if needed. When it is
  // CONTAINER_CPU_RESTRICTION_BACKGROUND, ARC can only use tightly restricted
  // CPU resources. The ARC instance is started in a state that is more
  // restricted than CONTAINER_CPU_RESTRICTION_BACKGROUND. When ARC is not
  // supported, the function asynchronously runs the |callback| with false.
  virtual void SetArcCpuRestriction(
      login_manager::ContainerCpuRestrictionState restriction_state,
      const ArcCallback& callback) = 0;

  // Emits the "arc-booted" upstart signal.
  virtual void EmitArcBooted(const cryptohome::Identification& cryptohome_id,
                             const ArcCallback& callback) = 0;

  // Asynchronously retrieves the timestamp which ARC instance is invoked or
  // returns false if there is no ARC instance or ARC is not available.
  virtual void GetArcStartTime(const GetArcStartTimeCallback& callback) = 0;

  // Asynchronously removes all ARC user data for the user whose cryptohome is
  // located by |cryptohome_id|. Upon completion, invokes |callback| with the
  // result; true on success, false on failure (either session manager failed
  // to remove user data or session manager can not be reached).
  virtual void RemoveArcData(const cryptohome::Identification& cryptohome_id,
                             const ArcCallback& callback) = 0;

  // Creates the instance.
  static SessionManagerClient* Create(DBusClientImplementationType type);

  ~SessionManagerClient() override;

 protected:
  // Create() should be used instead.
  SessionManagerClient();

 private:
  DISALLOW_COPY_AND_ASSIGN(SessionManagerClient);
};

}  // namespace chromeos

#endif  // CHROMEOS_DBUS_SESSION_MANAGER_CLIENT_H_
