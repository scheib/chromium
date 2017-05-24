/*
 * Copyright (C) 2012 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "web/WebHelperPluginImpl.h"

#include "core/exported/WebPluginContainerBase.h"
#include "core/frame/LocalFrameClient.h"
#include "core/frame/WebLocalFrameBase.h"
#include "core/html/HTMLObjectElement.h"
#include "core/loader/FrameLoader.h"
#include "public/web/WebPlugin.h"

namespace blink {

DEFINE_TYPE_CASTS(WebHelperPluginImpl, WebHelperPlugin, plugin, true, true);

WebHelperPlugin* WebHelperPlugin::Create(const WebString& plugin_type,
                                         WebLocalFrame* frame) {
  WebHelperPluginUniquePtr plugin(new WebHelperPluginImpl());
  if (!ToWebHelperPluginImpl(plugin.get())
           ->Initialize(plugin_type, ToWebLocalFrameBase(frame)))
    return 0;
  return plugin.release();
}

WebHelperPluginImpl::WebHelperPluginImpl()
    : destruction_timer_(this, &WebHelperPluginImpl::ReallyDestroy) {}

bool WebHelperPluginImpl::Initialize(const String& plugin_type,
                                     WebLocalFrameBase* frame) {
  DCHECK(!object_element_ && !plugin_container_);
  if (!frame->GetFrame()->Loader().Client())
    return false;

  object_element_ =
      HTMLObjectElement::Create(*frame->GetFrame()->GetDocument(), false);
  Vector<String> attribute_names;
  Vector<String> attribute_values;
  DCHECK(frame->GetFrame()->GetDocument()->Url().IsValid());
  plugin_container_ = ToWebPluginContainerBase(
      frame->GetFrame()->Loader().Client()->CreatePlugin(
          object_element_.Get(), frame->GetFrame()->GetDocument()->Url(),
          attribute_names, attribute_values, plugin_type, false,
          LocalFrameClient::kAllowDetachedPlugin));

  if (!plugin_container_)
    return false;

  // Getting a placeholder plugin is also failure, since it's not the plugin the
  // caller needed.
  return !GetPlugin()->IsPlaceholder();
}

void WebHelperPluginImpl::ReallyDestroy(TimerBase*) {
  if (plugin_container_)
    plugin_container_->Dispose();
  delete this;
}

void WebHelperPluginImpl::Destroy() {
  // Defer deletion so we don't do too much work when called via
  // stopSuspendableObjects().
  // FIXME: It's not clear why we still need this. The original code held a
  // Page and a WebFrame, and destroying it would cause JavaScript triggered by
  // frame detach to run, which isn't allowed inside stopSuspendableObjects().
  // Removing this causes one Chrome test to fail with a timeout.
  destruction_timer_.StartOneShot(0, BLINK_FROM_HERE);
}

WebPlugin* WebHelperPluginImpl::GetPlugin() {
  DCHECK(plugin_container_);
  DCHECK(plugin_container_->Plugin());
  return plugin_container_->Plugin();
}

}  // namespace blink
