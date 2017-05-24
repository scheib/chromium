/*
 * Copyright (C) 1999 Lars Knoll (knoll@kde.org)
 *           (C) 1999 Antti Koivisto (koivisto@kde.org)
 * Copyright (C) 2004, 2005, 2006, 2007, 2009, 2010 Apple Inc. All rights
 * reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "core/loader/ImageLoader.h"

#include <memory>
#include "bindings/core/v8/ScriptController.h"
#include "bindings/core/v8/V8BindingForCore.h"
#include "core/dom/Document.h"
#include "core/dom/Element.h"
#include "core/dom/IncrementLoadEventDelayCount.h"
#include "core/events/Event.h"
#include "core/events/EventSender.h"
#include "core/frame/LocalFrame.h"
#include "core/frame/Settings.h"
#include "core/frame/UseCounter.h"
#include "core/html/CrossOriginAttribute.h"
#include "core/html/HTMLImageElement.h"
#include "core/html/parser/HTMLParserIdioms.h"
#include "core/layout/LayoutImage.h"
#include "core/layout/LayoutVideo.h"
#include "core/layout/svg/LayoutSVGImage.h"
#include "core/probe/CoreProbes.h"
#include "core/svg/graphics/SVGImage.h"
#include "platform/bindings/Microtask.h"
#include "platform/bindings/ScriptState.h"
#include "platform/bindings/V8PerIsolateData.h"
#include "platform/loader/fetch/FetchParameters.h"
#include "platform/loader/fetch/MemoryCache.h"
#include "platform/loader/fetch/ResourceFetcher.h"
#include "platform/loader/fetch/ResourceLoadingLog.h"
#include "platform/weborigin/SecurityOrigin.h"
#include "platform/weborigin/SecurityPolicy.h"
#include "platform/wtf/PtrUtil.h"
#include "public/platform/WebCachePolicy.h"
#include "public/platform/WebURLRequest.h"

namespace blink {

static ImageEventSender& LoadEventSender() {
  DEFINE_STATIC_LOCAL(ImageEventSender, sender,
                      (ImageEventSender::Create(EventTypeNames::load)));
  return sender;
}

static ImageEventSender& ErrorEventSender() {
  DEFINE_STATIC_LOCAL(ImageEventSender, sender,
                      (ImageEventSender::Create(EventTypeNames::error)));
  return sender;
}

static inline bool PageIsBeingDismissed(Document* document) {
  return document->PageDismissalEventBeingDispatched() !=
         Document::kNoDismissal;
}

static ImageLoader::BypassMainWorldBehavior ShouldBypassMainWorldCSP(
    ImageLoader* loader) {
  DCHECK(loader);
  DCHECK(loader->GetElement());
  if (loader->GetElement()->GetDocument().GetFrame() &&
      loader->GetElement()
          ->GetDocument()
          .GetFrame()
          ->GetScriptController()
          .ShouldBypassMainWorldCSP())
    return ImageLoader::kBypassMainWorldCSP;
  return ImageLoader::kDoNotBypassMainWorldCSP;
}

class ImageLoader::Task {
 public:
  static std::unique_ptr<Task> Create(ImageLoader* loader,
                                      UpdateFromElementBehavior update_behavior,
                                      ReferrerPolicy referrer_policy) {
    return WTF::MakeUnique<Task>(loader, update_behavior, referrer_policy);
  }

  Task(ImageLoader* loader,
       UpdateFromElementBehavior update_behavior,
       ReferrerPolicy referrer_policy)
      : loader_(loader),
        should_bypass_main_world_csp_(ShouldBypassMainWorldCSP(loader)),
        update_behavior_(update_behavior),
        weak_factory_(this),
        referrer_policy_(referrer_policy) {
    ExecutionContext& context = loader_->GetElement()->GetDocument();
    probe::AsyncTaskScheduled(&context, "Image", this);
    v8::Isolate* isolate = V8PerIsolateData::MainThreadIsolate();
    v8::HandleScope scope(isolate);
    // If we're invoked from C++ without a V8 context on the stack, we should
    // run the microtask in the context of the element's document's main world.
    if (!isolate->GetCurrentContext().IsEmpty()) {
      script_state_ = ScriptState::Current(isolate);
    } else {
      script_state_ = ToScriptStateForMainWorld(
          loader->GetElement()->GetDocument().GetFrame());
      DCHECK(script_state_);
    }
    request_url_ =
        loader->ImageSourceToKURL(loader->GetElement()->ImageSourceURL());
  }

  void Run() {
    if (!loader_)
      return;
    ExecutionContext& context = loader_->GetElement()->GetDocument();
    probe::AsyncTask async_task(&context, this);
    if (script_state_->ContextIsValid()) {
      ScriptState::Scope scope(script_state_.Get());
      loader_->DoUpdateFromElement(should_bypass_main_world_csp_,
                                   update_behavior_, request_url_,
                                   referrer_policy_);
    } else {
      loader_->DoUpdateFromElement(should_bypass_main_world_csp_,
                                   update_behavior_, request_url_,
                                   referrer_policy_);
    }
  }

  void ClearLoader() {
    loader_ = nullptr;
    script_state_.Clear();
  }

  WeakPtr<Task> CreateWeakPtr() { return weak_factory_.CreateWeakPtr(); }

 private:
  WeakPersistent<ImageLoader> loader_;
  BypassMainWorldBehavior should_bypass_main_world_csp_;
  UpdateFromElementBehavior update_behavior_;
  RefPtr<ScriptState> script_state_;
  WeakPtrFactory<Task> weak_factory_;
  ReferrerPolicy referrer_policy_;
  KURL request_url_;
};

ImageLoader::ImageLoader(Element* element)
    : element_(element),
      deref_element_timer_(this, &ImageLoader::TimerFired),
      has_pending_load_event_(false),
      has_pending_error_event_(false),
      image_complete_(true),
      loading_image_document_(false),
      element_is_protected_(false),
      suppress_error_events_(false) {
  RESOURCE_LOADING_DVLOG(1) << "new ImageLoader " << this;
}

ImageLoader::~ImageLoader() {}

void ImageLoader::Dispose() {
  RESOURCE_LOADING_DVLOG(1)
      << "~ImageLoader " << this
      << "; has_pending_load_event_=" << has_pending_load_event_
      << ", has_pending_error_event_=" << has_pending_error_event_;

  if (image_) {
    image_->RemoveObserver(this);
    image_ = nullptr;
    delay_until_image_notify_finished_ = nullptr;
  }
}

DEFINE_TRACE(ImageLoader) {
  visitor->Trace(image_);
  visitor->Trace(image_resource_for_image_document_);
  visitor->Trace(element_);
}

void ImageLoader::SetImageForTest(ImageResourceContent* new_image) {
  DCHECK(new_image);
  SetImageWithoutConsideringPendingLoadEvent(new_image);

  // Only consider updating the protection ref-count of the Element immediately
  // before returning from this function as doing so might result in the
  // destruction of this ImageLoader.
  UpdatedHasPendingEvent();
}

void ImageLoader::ClearImage() {
  SetImageWithoutConsideringPendingLoadEvent(nullptr);

  // Only consider updating the protection ref-count of the Element immediately
  // before returning from this function as doing so might result in the
  // destruction of this ImageLoader.
  UpdatedHasPendingEvent();
}

void ImageLoader::SetImageForImageDocument(ImageResource* new_image_resource) {
  DCHECK(loading_image_document_);
  DCHECK(new_image_resource);
  DCHECK(new_image_resource->GetContent());

  image_resource_for_image_document_ = new_image_resource;
  SetImageWithoutConsideringPendingLoadEvent(new_image_resource->GetContent());

  // |image_complete_| is always true for ImageDocument loading, while the
  // loading is just started.
  // TODO(hiroshige): clean up the behavior of flags. https://crbug.com/719759
  image_complete_ = true;

  // Only consider updating the protection ref-count of the Element immediately
  // before returning from this function as doing so might result in the
  // destruction of this ImageLoader.
  UpdatedHasPendingEvent();
}

void ImageLoader::SetImageWithoutConsideringPendingLoadEvent(
    ImageResourceContent* new_image) {
  DCHECK(failed_load_url_.IsEmpty());
  ImageResourceContent* old_image = image_.Get();
  if (new_image != old_image) {
    if (has_pending_load_event_) {
      LoadEventSender().CancelEvent(this);
      has_pending_load_event_ = false;
    }
    if (has_pending_error_event_) {
      ErrorEventSender().CancelEvent(this);
      has_pending_error_event_ = false;
    }
    UpdateImageState(new_image);
    if (new_image) {
      new_image->AddObserver(this);
    }
    if (old_image) {
      old_image->RemoveObserver(this);
    }
  }

  if (LayoutImageResource* image_resource = GetLayoutImageResource())
    image_resource->ResetAnimation();
}

static void ConfigureRequest(
    FetchParameters& params,
    ImageLoader::BypassMainWorldBehavior bypass_behavior,
    Element& element,
    const ClientHintsPreferences& client_hints_preferences) {
  if (bypass_behavior == ImageLoader::kBypassMainWorldCSP)
    params.SetContentSecurityCheck(kDoNotCheckContentSecurityPolicy);

  CrossOriginAttributeValue cross_origin = GetCrossOriginAttributeValue(
      element.FastGetAttribute(HTMLNames::crossoriginAttr));
  if (cross_origin != kCrossOriginAttributeNotSet) {
    params.SetCrossOriginAccessControl(
        element.GetDocument().GetSecurityOrigin(), cross_origin);
  }

  if (client_hints_preferences.ShouldSendResourceWidth() &&
      isHTMLImageElement(element))
    params.SetResourceWidth(toHTMLImageElement(element).GetResourceWidth());
}

inline void ImageLoader::DispatchErrorEvent() {
  // There can be cases where DispatchErrorEvent() is called when there is
  // already a scheduled error event for the previous load attempt.
  // In such cases we cancel the previous event and then re-schedule a new
  // error event here. crbug.com/722500
  if (has_pending_error_event_)
    ErrorEventSender().CancelEvent(this);

  has_pending_error_event_ = true;
  ErrorEventSender().DispatchEventSoon(this);
}

inline void ImageLoader::CrossSiteOrCSPViolationOccurred(
    AtomicString image_source_url) {
  failed_load_url_ = image_source_url;
}

inline void ImageLoader::ClearFailedLoadURL() {
  failed_load_url_ = AtomicString();
}

inline void ImageLoader::EnqueueImageLoadingMicroTask(
    UpdateFromElementBehavior update_behavior,
    ReferrerPolicy referrer_policy) {
  std::unique_ptr<Task> task =
      Task::Create(this, update_behavior, referrer_policy);
  pending_task_ = task->CreateWeakPtr();
  Microtask::EnqueueMicrotask(
      WTF::Bind(&Task::Run, WTF::Passed(std::move(task))));
  delay_until_do_update_from_element_ =
      IncrementLoadEventDelayCount::Create(element_->GetDocument());
}

void ImageLoader::UpdateImageState(ImageResourceContent* new_image) {
  image_ = new_image;
  image_complete_ = !new_image;
  delay_until_image_notify_finished_ = nullptr;
}

void ImageLoader::DoUpdateFromElement(BypassMainWorldBehavior bypass_behavior,
                                      UpdateFromElementBehavior update_behavior,
                                      const KURL& url,
                                      ReferrerPolicy referrer_policy) {
  // FIXME: According to
  // http://www.whatwg.org/specs/web-apps/current-work/multipage/embedded-content.html#the-img-element:the-img-element-55
  // When "update image" is called due to environment changes and the load
  // fails, onerror should not be called. That is currently not the case.
  //
  // We don't need to call clearLoader here: Either we were called from the
  // task, or our caller updateFromElement cleared the task's loader (and set
  // pending_task_ to null).
  pending_task_.reset();
  // Make sure to only decrement the count when we exit this function
  std::unique_ptr<IncrementLoadEventDelayCount> load_delay_counter;
  load_delay_counter.swap(delay_until_do_update_from_element_);

  Document& document = element_->GetDocument();
  if (!document.IsActive())
    return;

  AtomicString image_source_url = element_->ImageSourceURL();
  ImageResourceContent* new_image = nullptr;
  if (!url.IsNull()) {
    // Unlike raw <img>, we block mixed content inside of <picture> or
    // <img srcset>.
    ResourceLoaderOptions resource_loader_options =
        ResourceFetcher::DefaultResourceOptions();
    ResourceRequest resource_request(url);
    if (update_behavior == kUpdateForcedReload) {
      resource_request.SetCachePolicy(WebCachePolicy::kBypassingCache);
      resource_request.SetPreviewsState(WebURLRequest::kPreviewsNoTransform);
    }

    if (referrer_policy != kReferrerPolicyDefault) {
      resource_request.SetHTTPReferrer(SecurityPolicy::GenerateReferrer(
          referrer_policy, url, document.OutgoingReferrer()));
    }

    if (isHTMLPictureElement(GetElement()->parentNode()) ||
        !GetElement()->FastGetAttribute(HTMLNames::srcsetAttr).IsNull())
      resource_request.SetRequestContext(
          WebURLRequest::kRequestContextImageSet);
    FetchParameters params(resource_request, GetElement()->localName(),
                           resource_loader_options);
    ConfigureRequest(params, bypass_behavior, *element_,
                     document.GetClientHintsPreferences());

    if (update_behavior != kUpdateForcedReload && document.GetFrame())
      document.GetFrame()->MaybeAllowImagePlaceholder(params);

    new_image = ImageResourceContent::Fetch(params, document.Fetcher());

    if (!new_image && !PageIsBeingDismissed(&document)) {
      CrossSiteOrCSPViolationOccurred(image_source_url);
      DispatchErrorEvent();
    } else {
      ClearFailedLoadURL();
    }
  } else {
    if (!image_source_url.IsNull()) {
      // Fire an error event if the url string is not empty, but the KURL is.
      DispatchErrorEvent();
    }
    NoImageResourceToLoad();
  }

  ImageResourceContent* old_image = image_.Get();
  if (update_behavior == kUpdateSizeChanged && element_->GetLayoutObject() &&
      element_->GetLayoutObject()->IsImage() && new_image == old_image) {
    ToLayoutImage(element_->GetLayoutObject())->IntrinsicSizeChanged();
  } else {
    if (has_pending_load_event_) {
      LoadEventSender().CancelEvent(this);
      has_pending_load_event_ = false;
    }

    // Cancel error events that belong to the previous load, which is now
    // cancelled by changing the src attribute. If newImage is null and
    // has_pending_error_event_ is true, we know the error event has been just
    // posted by this load and we should not cancel the event.
    // FIXME: If both previous load and this one got blocked with an error, we
    // can receive one error event instead of two.
    if (has_pending_error_event_ && new_image) {
      ErrorEventSender().CancelEvent(this);
      has_pending_error_event_ = false;
    }

    UpdateImageState(new_image);

    UpdateLayoutObject();
    // If newImage exists and is cached, addObserver() will result in the load
    // event being queued to fire. Ensure this happens after beforeload is
    // dispatched.
    if (new_image) {
      new_image->AddObserver(this);
    }
    if (old_image) {
      old_image->RemoveObserver(this);
    }
  }

  if (LayoutImageResource* image_resource = GetLayoutImageResource())
    image_resource->ResetAnimation();

  // Only consider updating the protection ref-count of the Element immediately
  // before returning from this function as doing so might result in the
  // destruction of this ImageLoader.
  UpdatedHasPendingEvent();
}

void ImageLoader::UpdateFromElement(UpdateFromElementBehavior update_behavior,
                                    ReferrerPolicy referrer_policy) {
  AtomicString image_source_url = element_->ImageSourceURL();
  suppress_error_events_ = (update_behavior == kUpdateSizeChanged);

  if (update_behavior == kUpdateIgnorePreviousError)
    ClearFailedLoadURL();

  if (!failed_load_url_.IsEmpty() && image_source_url == failed_load_url_)
    return;

  if (loading_image_document_ && update_behavior == kUpdateForcedReload) {
    // Prepares for reloading ImageDocument.
    // We turn the ImageLoader into non-ImageDocument here, and proceed to
    // reloading just like an ordinary <img> element below.
    loading_image_document_ = false;
    image_resource_for_image_document_ = nullptr;
    ClearImage();
  }

  // Prevent the creation of a ResourceLoader (and therefore a network request)
  // for ImageDocument loads. In this case, the image contents have already been
  // requested as a main resource and ImageDocumentParser will take care of
  // funneling the main resource bytes into image_, so just create an
  // ImageResource to be populated later.
  if (loading_image_document_) {
    ImageResource* image_resource = ImageResource::Create(
        ResourceRequest(ImageSourceToKURL(element_->ImageSourceURL())));
    image_resource->SetStatus(ResourceStatus::kPending);
    image_resource->NotifyStartLoad();
    SetImageForImageDocument(image_resource);
    return;
  }

  // If we have a pending task, we have to clear it -- either we're now loading
  // immediately, or we need to reset the task's state.
  if (pending_task_) {
    pending_task_->ClearLoader();
    pending_task_.reset();
  }

  KURL url = ImageSourceToKURL(image_source_url);
  if (ShouldLoadImmediately(url)) {
    DoUpdateFromElement(kDoNotBypassMainWorldCSP, update_behavior, url,
                        referrer_policy);
    return;
  }
  // Allow the idiom "img.src=''; img.src='.." to clear down the image before an
  // asynchronous load completes.
  if (image_source_url.IsEmpty()) {
    ImageResourceContent* image = image_.Get();
    if (image) {
      image->RemoveObserver(this);
    }
    image_ = nullptr;
    delay_until_image_notify_finished_ = nullptr;
  }

  // Don't load images for inactive documents. We don't want to slow down the
  // raw HTML parsing case by loading images we don't intend to display.
  Document& document = element_->GetDocument();
  if (document.IsActive())
    EnqueueImageLoadingMicroTask(update_behavior, referrer_policy);
}

KURL ImageLoader::ImageSourceToKURL(AtomicString image_source_url) const {
  KURL url;

  // Don't load images for inactive documents. We don't want to slow down the
  // raw HTML parsing case by loading images we don't intend to display.
  Document& document = element_->GetDocument();
  if (!document.IsActive())
    return url;

  // Do not load any image if the 'src' attribute is missing or if it is
  // an empty string.
  if (!image_source_url.IsNull()) {
    String stripped_image_source_url =
        StripLeadingAndTrailingHTMLSpaces(image_source_url);
    if (!stripped_image_source_url.IsEmpty())
      url = document.CompleteURL(stripped_image_source_url);
  }
  return url;
}

bool ImageLoader::ShouldLoadImmediately(const KURL& url) const {
  // We force any image loads which might require alt content through the
  // asynchronous path so that we can add the shadow DOM for the alt-text
  // content when style recalc is over and DOM mutation is allowed again.
  if (!url.IsNull()) {
    Resource* resource = GetMemoryCache()->ResourceForURL(
        url, element_->GetDocument().Fetcher()->GetCacheIdentifier());
    if (resource && !resource->ErrorOccurred())
      return true;
  }
  return (isHTMLObjectElement(element_) || isHTMLEmbedElement(element_));
}

void ImageLoader::ImageChanged(ImageResourceContent* content, const IntRect*) {
  DCHECK_EQ(content, image_.Get());
  if (image_complete_ || !content->IsLoading() ||
      delay_until_image_notify_finished_)
    return;

  Document& document = element_->GetDocument();
  if (!document.IsActive())
    return;

  delay_until_image_notify_finished_ =
      IncrementLoadEventDelayCount::Create(document);
}

void ImageLoader::ImageNotifyFinished(ImageResourceContent* resource) {
  RESOURCE_LOADING_DVLOG(1)
      << "ImageLoader::imageNotifyFinished " << this
      << "; has_pending_load_event_=" << has_pending_load_event_;

  DCHECK(failed_load_url_.IsEmpty());
  DCHECK_EQ(resource, image_.Get());

  // |image_complete_| is always true for entire ImageDocument loading for
  // historical reason.
  // DoUpdateFromElement() is not called and SetImageForImageDocument()
  // is called instead for ImageDocument loading.
  // TODO(hiroshige): Turn the CHECK()s to DCHECK()s before going to beta.
  if (loading_image_document_)
    CHECK(image_complete_);
  else
    CHECK(!image_complete_);

  image_complete_ = true;
  delay_until_image_notify_finished_ = nullptr;

  // Update ImageAnimationPolicy for image_.
  if (image_)
    image_->UpdateImageAnimationPolicy();

  UpdateLayoutObject();

  if (image_ && image_->GetImage() && image_->GetImage()->IsSVGImage()) {
    // SVG's document should be completely loaded before access control
    // checks, which can occur anytime after ImageNotifyFinished()
    // (See SVGImage::CurrentFrameHasSingleSecurityOrigin()).
    // We check the document is loaded here to catch violation of the
    // assumption reliably.
    ToSVGImage(image_->GetImage())->CheckLoaded();

    ToSVGImage(image_->GetImage())
        ->UpdateUseCounters(GetElement()->GetDocument());
  }

  if (loading_image_document_)
    return;

  if (resource->ErrorOccurred()) {
    LoadEventSender().CancelEvent(this);
    has_pending_load_event_ = false;

    if (resource->GetResourceError().IsAccessCheck()) {
      CrossSiteOrCSPViolationOccurred(
          AtomicString(resource->GetResourceError().FailingURL()));
    }

    // The error event should not fire if the image data update is a result of
    // environment change.
    // https://html.spec.whatwg.org/multipage/embedded-content.html#the-img-element:the-img-element-55
    if (!suppress_error_events_)
      DispatchErrorEvent();

    // Only consider updating the protection ref-count of the Element
    // immediately before returning from this function as doing so might result
    // in the destruction of this ImageLoader.
    UpdatedHasPendingEvent();
    return;
  }
  has_pending_load_event_ = true;
  LoadEventSender().DispatchEventSoon(this);
}

LayoutImageResource* ImageLoader::GetLayoutImageResource() {
  LayoutObject* layout_object = element_->GetLayoutObject();

  if (!layout_object)
    return 0;

  // We don't return style generated image because it doesn't belong to the
  // ImageLoader. See <https://bugs.webkit.org/show_bug.cgi?id=42840>
  if (layout_object->IsImage() &&
      !static_cast<LayoutImage*>(layout_object)->IsGeneratedContent())
    return ToLayoutImage(layout_object)->ImageResource();

  if (layout_object->IsSVGImage())
    return ToLayoutSVGImage(layout_object)->ImageResource();

  if (layout_object->IsVideo())
    return ToLayoutVideo(layout_object)->ImageResource();

  return 0;
}

void ImageLoader::UpdateLayoutObject() {
  LayoutImageResource* image_resource = GetLayoutImageResource();

  if (!image_resource)
    return;

  // Only update the layoutObject if it doesn't have an image or if what we have
  // is a complete image.  This prevents flickering in the case where a dynamic
  // change is happening between two images.
  ImageResourceContent* cached_image = image_resource->CachedImage();
  if (image_ != cached_image && (image_complete_ || !cached_image))
    image_resource->SetImageResource(image_.Get());
}

bool ImageLoader::HasPendingEvent() const {
  // Regular image loading is in progress.
  if (image_ && !image_complete_ && !loading_image_document_)
    return true;

  if (has_pending_load_event_ || has_pending_error_event_)
    return true;

  return false;
}

void ImageLoader::UpdatedHasPendingEvent() {
  // If an Element that does image loading is removed from the DOM the
  // load/error event for the image is still observable. As long as the
  // ImageLoader is actively loading, the Element itself needs to be ref'ed to
  // keep it from being destroyed by DOM manipulation or garbage collection. If
  // such an Element wishes for the load to stop when removed from the DOM it
  // needs to stop the ImageLoader explicitly.
  bool was_protected = element_is_protected_;
  element_is_protected_ = HasPendingEvent();
  if (was_protected == element_is_protected_)
    return;

  if (element_is_protected_) {
    if (deref_element_timer_.IsActive())
      deref_element_timer_.Stop();
    else
      keep_alive_ = element_;
  } else {
    DCHECK(!deref_element_timer_.IsActive());
    deref_element_timer_.StartOneShot(0, BLINK_FROM_HERE);
  }
}

void ImageLoader::TimerFired(TimerBase*) {
  keep_alive_.Clear();
}

void ImageLoader::DispatchPendingEvent(ImageEventSender* event_sender) {
  RESOURCE_LOADING_DVLOG(1) << "ImageLoader::dispatchPendingEvent " << this;
  DCHECK(event_sender == &LoadEventSender() ||
         event_sender == &ErrorEventSender());
  const AtomicString& event_type = event_sender->EventType();
  if (event_type == EventTypeNames::load)
    DispatchPendingLoadEvent();
  if (event_type == EventTypeNames::error)
    DispatchPendingErrorEvent();
}

void ImageLoader::DispatchPendingLoadEvent() {
  CHECK(has_pending_load_event_);
  if (!image_)
    return;
  CHECK(image_complete_);
  has_pending_load_event_ = false;
  if (GetElement()->GetDocument().GetFrame())
    DispatchLoadEvent();

  // Only consider updating the protection ref-count of the Element immediately
  // before returning from this function as doing so might result in the
  // destruction of this ImageLoader.
  UpdatedHasPendingEvent();
}

void ImageLoader::DispatchPendingErrorEvent() {
  CHECK(has_pending_error_event_);
  has_pending_error_event_ = false;

  if (GetElement()->GetDocument().GetFrame())
    GetElement()->DispatchEvent(Event::Create(EventTypeNames::error));

  // Only consider updating the protection ref-count of the Element immediately
  // before returning from this function as doing so might result in the
  // destruction of this ImageLoader.
  UpdatedHasPendingEvent();
}

bool ImageLoader::GetImageAnimationPolicy(ImageAnimationPolicy& policy) {
  if (!GetElement()->GetDocument().GetSettings())
    return false;

  policy = GetElement()->GetDocument().GetSettings()->GetImageAnimationPolicy();
  return true;
}

void ImageLoader::DispatchPendingLoadEvents() {
  LoadEventSender().DispatchPendingEvents();
}

void ImageLoader::DispatchPendingErrorEvents() {
  ErrorEventSender().DispatchPendingEvents();
}

void ImageLoader::ElementDidMoveToNewDocument() {
  if (delay_until_do_update_from_element_) {
    delay_until_do_update_from_element_->DocumentChanged(
        element_->GetDocument());
  }
  if (delay_until_image_notify_finished_) {
    delay_until_image_notify_finished_->DocumentChanged(
        element_->GetDocument());
  }
  ClearFailedLoadURL();
  ClearImage();
}

}  // namespace blink
