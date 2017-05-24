// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_SERVICE_WORKER_SERVICE_WORKER_PROVIDER_HOST_H_
#define CONTENT_BROWSER_SERVICE_WORKER_SERVICE_WORKER_PROVIDER_HOST_H_

#include <stddef.h>
#include <stdint.h>

#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "base/gtest_prod_util.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "content/browser/service_worker/service_worker_registration.h"
#include "content/common/content_export.h"
#include "content/common/service_worker/service_worker_provider_host_info.h"
#include "content/common/service_worker/service_worker_types.h"
#include "content/common/worker_url_loader_factory_provider.mojom.h"
#include "content/public/common/request_context_frame_type.h"
#include "content/public/common/request_context_type.h"
#include "content/public/common/resource_type.h"

namespace storage {
class BlobStorageContext;
}

namespace content {

class MessagePort;
class ResourceRequestBodyImpl;
class ServiceWorkerContextCore;
class ServiceWorkerDispatcherHost;
class ServiceWorkerRequestHandler;
class ServiceWorkerVersion;
class WebContents;

// This class is the browser-process representation of a service worker
// provider. There are two general types of providers: 1) those for a client
// (windows, dedicated workers, or shared workers), and 2) those for hosting a
// running service worker.
//
// For client providers, there is a provider per document or a worker and the
// lifetime of this object is tied to the lifetime of its document or the worker
// in the renderer process. This class holds service worker state that is scoped
// to an individual document or a worker.
//
// For providers hosting a running service worker, this class will observe
// resource loads made directly by the service worker.
class CONTENT_EXPORT ServiceWorkerProviderHost
    : public NON_EXPORTED_BASE(ServiceWorkerRegistration::Listener),
      public base::SupportsWeakPtr<ServiceWorkerProviderHost> {
 public:
  using GetRegistrationForReadyCallback =
      base::Callback<void(ServiceWorkerRegistration* reigstration)>;

  using WebContentsGetter = base::Callback<WebContents*(void)>;

  // PlzNavigate
  // Used to pre-create a ServiceWorkerProviderHost for a navigation. The
  // ServiceWorkerNetworkProvider will later be created in the renderer, should
  // the navigation succeed. |is_parent_frame_is_secure| should be true for main
  // frames. Otherwise it is true iff all ancestor frames of this frame have a
  // secure origin. |web_contents_getter| indicates the tab where the navigation
  // is occurring.
  static std::unique_ptr<ServiceWorkerProviderHost> PreCreateNavigationHost(
      base::WeakPtr<ServiceWorkerContextCore> context,
      bool are_ancestors_secure,
      const WebContentsGetter& web_contents_getter);

  // Used to create a ServiceWorkerProviderHost when the renderer-side provider
  // is created. This ProviderHost will be created for the process specified by
  // |process_id|.
  static std::unique_ptr<ServiceWorkerProviderHost> Create(
      int process_id,
      ServiceWorkerProviderHostInfo info,
      base::WeakPtr<ServiceWorkerContextCore> context,
      ServiceWorkerDispatcherHost* dispatcher_host);

  virtual ~ServiceWorkerProviderHost();

  const std::string& client_uuid() const { return client_uuid_; }
  int process_id() const { return render_process_id_; }
  int provider_id() const { return info_.provider_id; }
  int frame_id() const;
  int route_id() const { return info_.route_id; }
  const WebContentsGetter& web_contents_getter() const {
    return web_contents_getter_;
  }

  bool is_parent_frame_secure() const { return info_.is_parent_frame_secure; }

  // Returns whether this provider host is secure enough to have a service
  // worker controller.
  // Analogous to Blink's Document::isSecureContext. Because of how service
  // worker intercepts main resource requests, this check must be done
  // browser-side once the URL is known (see comments in
  // ServiceWorkerNetworkProvider::CreateForNavigation). This function uses
  // |document_url_| and |is_parent_frame_secure_| to determine context
  // security, so they must be set properly before calling this function.
  bool IsContextSecureForServiceWorker() const;

  bool IsHostToRunningServiceWorker() {
    return running_hosted_version_.get() != NULL;
  }

  // Returns this provider's controller. The controller is typically the same as
  // active_version() but can differ in the following cases:
  // (1) The client was created before the registration existed or had an active
  // version (in spec language, it is not "using" the registration).
  // (2) The client had a controller but NotifyControllerLost() was called due
  // to an exceptional circumstance (here also it is not "using" the
  // registration).
  // (3) During algorithms such as the update, skipWaiting(), and claim() steps,
  // the active_version and controlling_version may temporarily differ. For
  // example, to perform skipWaiting(), the registration's active version is
  // updated first and then the provider host's controlling version is updated
  // to match it.
  ServiceWorkerVersion* controlling_version() const {
    // Only clients can have controllers.
    DCHECK(!controlling_version_ || IsProviderForClient());
    return controlling_version_.get();
  }

  ServiceWorkerVersion* active_version() const {
    return associated_registration_.get() ?
        associated_registration_->active_version() : NULL;
  }

  ServiceWorkerVersion* waiting_version() const {
    return associated_registration_.get() ?
        associated_registration_->waiting_version() : NULL;
  }

  ServiceWorkerVersion* installing_version() const {
    return associated_registration_.get() ?
        associated_registration_->installing_version() : NULL;
  }

  // Returns the associated registration. The provider host listens to this
  // registration to resolve the .ready promise and set its controller.
  ServiceWorkerRegistration* associated_registration() const {
    // Only clients can have an associated registration.
    DCHECK(!associated_registration_ || IsProviderForClient());
    return associated_registration_.get();
  }

  // The running version, if any, that this provider is providing resource
  // loads for.
  ServiceWorkerVersion* running_hosted_version() const {
    // Only providers for controllers can host a running version.
    DCHECK(!running_hosted_version_ ||
           info_.type == SERVICE_WORKER_PROVIDER_FOR_CONTROLLER);
    return running_hosted_version_.get();
  }

  // Sets the |document_url_|.  When this object is for a client,
  // |matching_registrations_| gets also updated to ensure that |document_url_|
  // is in scope of all |matching_registrations_|.
  void SetDocumentUrl(const GURL& url);
  const GURL& document_url() const { return document_url_; }

  void SetTopmostFrameUrl(const GURL& url);
  const GURL& topmost_frame_url() const { return topmost_frame_url_; }

  ServiceWorkerProviderType provider_type() const { return info_.type; }
  bool IsProviderForClient() const;
  blink::WebServiceWorkerClientType client_type() const;

  // Associates to |registration| to listen for its version change events and
  // sets the controller. If |notify_controllerchange| is true, instructs the
  // renderer to dispatch a 'controllerchange' event.
  void AssociateRegistration(ServiceWorkerRegistration* registration,
                             bool notify_controllerchange);

  // Clears the associated registration and stop listening to it.
  void DisassociateRegistration();

  void SetHostedVersion(ServiceWorkerVersion* version);

  // Returns a handler for a request, the handler may return NULL if
  // the request doesn't require special handling.
  std::unique_ptr<ServiceWorkerRequestHandler> CreateRequestHandler(
      FetchRequestMode request_mode,
      FetchCredentialsMode credentials_mode,
      FetchRedirectMode redirect_mode,
      ResourceType resource_type,
      RequestContextType request_context_type,
      RequestContextFrameType frame_type,
      base::WeakPtr<storage::BlobStorageContext> blob_storage_context,
      scoped_refptr<ResourceRequestBodyImpl> body,
      bool skip_service_worker);

  // Used to get a ServiceWorkerObjectInfo to send to the renderer. Finds an
  // existing ServiceWorkerHandle, and increments its reference count, or else
  // creates a new one (initialized to ref count 1). Returns the
  // ServiceWorkerInfo from the handle. The renderer is expected to use
  // ServiceWorkerHandleReference::Adopt to balance out the ref count.
  ServiceWorkerObjectInfo GetOrCreateServiceWorkerHandle(
      ServiceWorkerVersion* version);

  // Returns true if |registration| can be associated with this provider.
  bool CanAssociateRegistration(ServiceWorkerRegistration* registration);

  // For use by the ServiceWorkerControlleeRequestHandler to disallow
  // new registration association while a navigation is occurring and
  // an existing registration is being looked for.
  void SetAllowAssociation(bool allow) { allow_association_ = allow; }

  // Returns true if the context referred to by this host (i.e. |context_|) is
  // still alive.
  bool IsContextAlive();

  // Dispatches message event to the document.
  void PostMessageToClient(ServiceWorkerVersion* version,
                           const base::string16& message,
                           const std::vector<MessagePort>& sent_message_ports);

  // Notifies the client that its controller used a feature, for UseCounter
  // purposes. This can only be called if IsProviderForClient() is true.
  void CountFeature(uint32_t feature);

  // Adds reference of this host's process to the |pattern|, the reference will
  // be removed in destructor.
  void AddScopedProcessReferenceToPattern(const GURL& pattern);

  // |registration| claims the document to be controlled.
  void ClaimedByRegistration(ServiceWorkerRegistration* registration);

  // Called by dispatcher host to get the registration for the "ready" property.
  // Returns false if there's a completed or ongoing request for the document.
  // https://slightlyoff.github.io/ServiceWorker/spec/service_worker/#navigator-service-worker-ready
  bool GetRegistrationForReady(const GetRegistrationForReadyCallback& callback);

  // Methods to support cross site navigations.
  std::unique_ptr<ServiceWorkerProviderHost> PrepareForCrossSiteTransfer();
  void CompleteCrossSiteTransfer(ServiceWorkerProviderHost* provisional_host);
  ServiceWorkerDispatcherHost* dispatcher_host() const {
    return dispatcher_host_;
  }

  // PlzNavigate
  // Completes initialization of provider hosts used for navigation requests.
  void CompleteNavigationInitialized(
      int process_id,
      int frame_routing_id,
      ServiceWorkerDispatcherHost* dispatcher_host);

  // Sends event messages to the renderer. Events for the worker are queued up
  // until the worker thread id is known via SetReadyToSendMessagesToWorker().
  void SendUpdateFoundMessage(
      int registration_handle_id);
  void SendSetVersionAttributesMessage(
      int registration_handle_id,
      ChangedVersionAttributesMask changed_mask,
      ServiceWorkerVersion* installing_version,
      ServiceWorkerVersion* waiting_version,
      ServiceWorkerVersion* active_version);
  void SendServiceWorkerStateChangedMessage(
      int worker_handle_id,
      blink::WebServiceWorkerState state);

  // Sets the worker thread id and flushes queued events.
  void SetReadyToSendMessagesToWorker(int render_thread_id);

  void AddMatchingRegistration(ServiceWorkerRegistration* registration);
  void RemoveMatchingRegistration(ServiceWorkerRegistration* registration);

  // An optimized implementation of [[Match Service Worker Registration]]
  // for current document.
  ServiceWorkerRegistration* MatchRegistration() const;

  // Called when our controller has been terminated and doomed due to an
  // exceptional condition like it could no longer be read from the script
  // cache.
  void NotifyControllerLost();

  // Binds the ServiceWorkerWorkerClient of a dedicated (or shared) worker to
  // the parent frame's ServiceWorkerProviderHost. (This is used only when
  // off-main-thread-fetch is enabled.)
  void BindWorkerFetchContext(
      mojom::ServiceWorkerWorkerClientAssociatedPtrInfo client_ptr_info);

 private:
  friend class ForeignFetchRequestHandlerTest;
  friend class LinkHeaderServiceWorkerTest;
  friend class ServiceWorkerProviderHostTest;
  friend class ServiceWorkerWriteToCacheJobTest;
  friend class ServiceWorkerContextRequestHandlerTest;
  FRIEND_TEST_ALL_PREFIXES(ServiceWorkerWriteToCacheJobTest, Update_SameScript);
  FRIEND_TEST_ALL_PREFIXES(ServiceWorkerWriteToCacheJobTest,
                           Update_SameSizeScript);
  FRIEND_TEST_ALL_PREFIXES(ServiceWorkerWriteToCacheJobTest,
                           Update_TruncatedScript);
  FRIEND_TEST_ALL_PREFIXES(ServiceWorkerWriteToCacheJobTest,
                           Update_ElongatedScript);
  FRIEND_TEST_ALL_PREFIXES(ServiceWorkerWriteToCacheJobTest,
                           Update_EmptyScript);
  FRIEND_TEST_ALL_PREFIXES(ServiceWorkerDispatcherHostTest,
                           DispatchExtendableMessageEvent);
  FRIEND_TEST_ALL_PREFIXES(ServiceWorkerDispatcherHostTest,
                           DispatchExtendableMessageEvent_Fail);
  FRIEND_TEST_ALL_PREFIXES(ServiceWorkerProviderHostTest, ContextSecurity);

  struct OneShotGetReadyCallback {
    GetRegistrationForReadyCallback callback;
    bool called;

    explicit OneShotGetReadyCallback(
        const GetRegistrationForReadyCallback& callback);
    ~OneShotGetReadyCallback();
  };

  ServiceWorkerProviderHost(int render_process_id,
                            ServiceWorkerProviderHostInfo info,
                            base::WeakPtr<ServiceWorkerContextCore> context,
                            ServiceWorkerDispatcherHost* dispatcher_host);

  // ServiceWorkerRegistration::Listener overrides.
  void OnVersionAttributesChanged(
      ServiceWorkerRegistration* registration,
      ChangedVersionAttributesMask changed_mask,
      const ServiceWorkerRegistrationInfo& info) override;
  void OnRegistrationFailed(ServiceWorkerRegistration* registration) override;
  void OnRegistrationFinishedUninstalling(
      ServiceWorkerRegistration* registration) override;
  void OnSkippedWaiting(ServiceWorkerRegistration* registration) override;

  // Sets the controller version field to |version| or if |version| is NULL,
  // clears the field. If |notify_controllerchange| is true, instructs the
  // renderer to dispatch a 'controller' change event.
  void SetControllerVersionAttribute(ServiceWorkerVersion* version,
                                     bool notify_controllerchange);

  void SendAssociateRegistrationMessage();

  // Syncs matching registrations with live registrations.
  void SyncMatchingRegistrations();

  // Discards all references to matching registrations.
  void RemoveAllMatchingRegistrations();

  // Increase/decrease this host's process reference for |pattern|.
  void IncreaseProcessReference(const GURL& pattern);
  void DecreaseProcessReference(const GURL& pattern);

  void ReturnRegistrationForReadyIfNeeded();

  bool IsReadyToSendMessages() const;
  void Send(IPC::Message* message) const;

  // Finalizes cross-site transfers and navigation-initalized hosts.
  void FinalizeInitialization(int process_id,
                              int frame_routing_id,
                              ServiceWorkerDispatcherHost* dispatcher_host);

  // Clears the information of the ServiceWorkerWorkerClient of dedicated (or
  // shared) worker, when the connection to the worker is disconnected.
  void UnregisterWorkerFetchContext(mojom::ServiceWorkerWorkerClient*);

  std::string client_uuid_;
  int render_process_id_;

  // For provider hosts that are hosting a running service worker, the id of the
  // service worker thread. Otherwise, |kDocumentMainThreadId|. May be
  // |kInvalidEmbeddedWorkerThreadId| before the hosted service worker starts
  // up, or during cross-site transfers.
  int render_thread_id_;

  // Keeps the basic provider's info provided from the renderer side.
  ServiceWorkerProviderHostInfo info_;

  // PlzNavigate
  // Only set when this object is pre-created for a navigation. It indicates the
  // tab where the navigation occurs.
  WebContentsGetter web_contents_getter_;

  GURL document_url_;
  GURL topmost_frame_url_;

  std::vector<GURL> associated_patterns_;
  scoped_refptr<ServiceWorkerRegistration> associated_registration_;

  // Keyed by registration scope URL length.
  typedef std::map<size_t, scoped_refptr<ServiceWorkerRegistration>>
      ServiceWorkerRegistrationMap;
  // Contains all living registrations whose pattern this document's URL
  // starts with. It is empty if IsContextSecureForServiceWorker() is
  // false.
  ServiceWorkerRegistrationMap matching_registrations_;

  std::unique_ptr<OneShotGetReadyCallback> get_ready_callback_;
  scoped_refptr<ServiceWorkerVersion> controlling_version_;
  scoped_refptr<ServiceWorkerVersion> running_hosted_version_;
  base::WeakPtr<ServiceWorkerContextCore> context_;
  ServiceWorkerDispatcherHost* dispatcher_host_;
  bool allow_association_;

  std::vector<base::Closure> queued_events_;

  // Keeps ServiceWorkerWorkerClient pointers of dedicated or shared workers
  // which are associated with the ServiceWorkerProviderHost.
  std::unordered_map<mojom::ServiceWorkerWorkerClient*,
                     mojom::ServiceWorkerWorkerClientAssociatedPtr>
      worker_clients_;

  DISALLOW_COPY_AND_ASSIGN(ServiceWorkerProviderHost);
};

}  // namespace content

#endif  // CONTENT_BROWSER_SERVICE_WORKER_SERVICE_WORKER_PROVIDER_HOST_H_
