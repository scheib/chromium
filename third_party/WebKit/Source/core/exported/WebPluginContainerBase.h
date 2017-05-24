// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.#ifndef WebViewBase_h

#ifndef WebPluginContainerBase_h
#define WebPluginContainerBase_h

#include "core/CoreExport.h"
#include "core/dom/ContextLifecycleObserver.h"
#include "core/plugins/PluginView.h"
#include "public/web/WebPluginContainer.h"

namespace blink {

class ResourceError;

struct WebPrintParams;
struct WebPrintPresetOptions;

// WebPluginContainerBase is a temporary class the provides a layer of
// abstraction for WebPluginContainerImpl. Mehtods that are declared public in
// WebPluginContainerImpl that are not overrides from PluginView,
// WebPuglinContainer or ContextClient will be declared pure virtual in
// WebPluginContainerBase. Classes that then have a dependency on
// WebPluginContainerImpl will then take a dependency on WebPluginContainerBase
// instead, so we can remove cyclic dependencies in web/ and move classes from
// web/ into core/ or modules.
// TODO(slangley): Remove this class once WebPluginContainerImpl is in core/.
class CORE_EXPORT WebPluginContainerBase
    : public GarbageCollectedFinalized<WebPluginContainerBase>,
      public PluginView,
      NON_EXPORTED_BASE(public WebPluginContainer),
      public ContextClient {
  USING_GARBAGE_COLLECTED_MIXIN(WebPluginContainerBase);

 public:
  virtual int PrintBegin(const WebPrintParams&) const = 0;
  virtual void PrintPage(int page_number, GraphicsContext&, const IntRect&) = 0;
  virtual void PrintEnd() = 0;
  virtual bool ExecuteEditCommand(const WebString& name) = 0;
  virtual bool ExecuteEditCommand(const WebString& name,
                                  const WebString& value) = 0;
  virtual bool SupportsPaginatedPrint() const = 0;
  virtual bool IsPrintScalingDisabled() const = 0;
  virtual bool GetPrintPresetOptionsFromDocument(
      WebPrintPresetOptions*) const = 0;
  virtual void DidFinishLoading() = 0;
  virtual void DidFailLoading(const ResourceError&) = 0;
  virtual void CalculateGeometry(IntRect& window_rect,
                                 IntRect& clip_rect,
                                 IntRect& unobscured_rect) = 0;

  DECLARE_VIRTUAL_TRACE();

 protected:
  explicit WebPluginContainerBase(LocalFrame*);
};

DEFINE_TYPE_CASTS(WebPluginContainerBase,
                  PluginView,
                  plugin,
                  plugin->IsPluginContainer(),
                  plugin.IsPluginContainer());
// Unlike FrameViewBase, we need not worry about object type for container.
// WebPluginContainerBase is the only subclass of WebPluginContainer.
DEFINE_TYPE_CASTS(WebPluginContainerBase,
                  WebPluginContainer,
                  container,
                  true,
                  true);

}  // nammespace blink

#endif  // WebPluginContainerBase_h
