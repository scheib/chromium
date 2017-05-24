/*
 * Copyright (C) 2009 Google Inc. All rights reserved.
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

#include "public/web/WebNode.h"

#include "bindings/core/v8/ExceptionState.h"
#include "core/dom/AXObjectCacheBase.h"
#include "core/dom/Document.h"
#include "core/dom/Element.h"
#include "core/dom/Node.h"
#include "core/dom/NodeList.h"
#include "core/dom/StaticNodeList.h"
#include "core/dom/TagCollection.h"
#include "core/dom/TaskRunnerHelper.h"
#include "core/editing/EditingUtilities.h"
#include "core/editing/serializers/Serialization.h"
#include "core/events/Event.h"
#include "core/exported/WebPluginContainerBase.h"
#include "core/html/HTMLCollection.h"
#include "core/html/HTMLElement.h"
#include "core/layout/LayoutObject.h"
#include "core/layout/LayoutPart.h"
#include "modules/accessibility/AXObjectImpl.h"
#include "platform/wtf/PtrUtil.h"
#include "public/platform/WebString.h"
#include "public/web/WebAXObject.h"
#include "public/web/WebDOMEvent.h"
#include "public/web/WebDocument.h"
#include "public/web/WebElement.h"
#include "public/web/WebElementCollection.h"
#include "public/web/WebPluginContainer.h"

namespace blink {

void WebNode::Reset() {
  private_.Reset();
}

void WebNode::Assign(const WebNode& other) {
  private_ = other.private_;
}

bool WebNode::Equals(const WebNode& n) const {
  return private_.Get() == n.private_.Get();
}

bool WebNode::LessThan(const WebNode& n) const {
  return private_.Get() < n.private_.Get();
}

WebNode WebNode::ParentNode() const {
  return WebNode(const_cast<ContainerNode*>(private_->parentNode()));
}

WebString WebNode::NodeValue() const {
  return private_->nodeValue();
}

WebDocument WebNode::GetDocument() const {
  return WebDocument(&private_->GetDocument());
}

WebNode WebNode::FirstChild() const {
  return WebNode(private_->firstChild());
}

WebNode WebNode::LastChild() const {
  return WebNode(private_->lastChild());
}

WebNode WebNode::PreviousSibling() const {
  return WebNode(private_->previousSibling());
}

WebNode WebNode::NextSibling() const {
  return WebNode(private_->nextSibling());
}

bool WebNode::IsLink() const {
  return private_->IsLink();
}

bool WebNode::IsTextNode() const {
  return private_->IsTextNode();
}

bool WebNode::IsCommentNode() const {
  return private_->getNodeType() == Node::kCommentNode;
}

bool WebNode::IsFocusable() const {
  if (!private_->IsElementNode())
    return false;
  private_->GetDocument().UpdateStyleAndLayoutIgnorePendingStylesheets();
  return ToElement(private_.Get())->IsFocusable();
}

bool WebNode::IsContentEditable() const {
  private_->GetDocument().UpdateStyleAndLayoutTree();
  return HasEditableStyle(*private_);
}

bool WebNode::IsInsideFocusableElementOrARIAWidget() const {
  return AXObjectImpl::IsInsideFocusableElementOrARIAWidget(
      *this->ConstUnwrap<Node>());
}

bool WebNode::IsElementNode() const {
  return private_->IsElementNode();
}

bool WebNode::IsDocumentNode() const {
  return private_->IsDocumentNode();
}

bool WebNode::IsDocumentTypeNode() const {
  return private_->getNodeType() == Node::kDocumentTypeNode;
}

void WebNode::SimulateClick() {
  TaskRunnerHelper::Get(TaskType::kUserInteraction,
                        private_->GetExecutionContext())
      ->PostTask(
          FROM_HERE,
          WTF::Bind(&Node::DispatchSimulatedClick,
                    WrapWeakPersistent(private_.Get()), nullptr, kSendNoEvents,
                    SimulatedClickCreationScope::kFromUserAgent));
}

WebElementCollection WebNode::GetElementsByHTMLTagName(
    const WebString& tag) const {
  if (private_->IsContainerNode())
    return WebElementCollection(
        ToContainerNode(private_.Get())
            ->getElementsByTagNameNS(HTMLNames::xhtmlNamespaceURI, tag));
  return WebElementCollection();
}

WebElement WebNode::QuerySelector(const WebString& selector) const {
  if (!private_->IsContainerNode())
    return WebElement();
  return ToContainerNode(private_.Get())
      ->QuerySelector(selector, IGNORE_EXCEPTION_FOR_TESTING);
}

bool WebNode::Focused() const {
  return private_->IsFocused();
}

WebPluginContainer* WebNode::PluginContainerFromNode(const Node* node) {
  if (!node)
    return nullptr;

  if (!isHTMLObjectElement(node) && !isHTMLEmbedElement(node))
    return nullptr;

  LayoutObject* object = node->GetLayoutObject();
  if (object && object->IsLayoutPart()) {
    PluginView* plugin = ToLayoutPart(object)->Plugin();
    if (plugin && plugin->IsPluginContainer())
      return ToWebPluginContainerBase(plugin);
  }

  return nullptr;
}

WebPluginContainer* WebNode::PluginContainer() const {
  return PluginContainerFromNode(ConstUnwrap<Node>());
}

WebAXObject WebNode::AccessibilityObject() {
  WebDocument web_document = GetDocument();
  const Document* doc = GetDocument().ConstUnwrap<Document>();
  AXObjectCacheBase* cache = ToAXObjectCacheBase(doc->ExistingAXObjectCache());
  Node* node = Unwrap<Node>();
  return cache ? WebAXObject(cache->Get(node)) : WebAXObject();
}

WebNode::WebNode(Node* node) : private_(node) {}

WebNode& WebNode::operator=(Node* node) {
  private_ = node;
  return *this;
}

WebNode::operator Node*() const {
  return private_.Get();
}

}  // namespace blink
