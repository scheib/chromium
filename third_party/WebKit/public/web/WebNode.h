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

#ifndef WebNode_h
#define WebNode_h

#include "public/platform/WebCommon.h"
#include "public/platform/WebPrivatePtr.h"
#include "public/platform/WebString.h"
#include "public/platform/WebVector.h"

namespace blink {

class Node;
class WebAXObject;
class WebDocument;
class WebElement;
class WebElementCollection;
class WebPluginContainer;

// Provides access to some properties of a DOM node.
// Note that the class design requires that neither this class nor any of its
// subclasses have any virtual methods (other than the destructor), so that it
// is possible to safely static_cast an instance of one class to the appropriate
// subclass based on the actual type of the wrapped blink::Node. For the same
// reason, subclasses must not add any additional data members.
class WebNode {
 public:
  virtual ~WebNode() { Reset(); }

  WebNode() {}
  WebNode(const WebNode& n) { Assign(n); }
  WebNode& operator=(const WebNode& n) {
    Assign(n);
    return *this;
  }

  BLINK_EXPORT void Reset();
  BLINK_EXPORT void Assign(const WebNode&);

  BLINK_EXPORT bool Equals(const WebNode&) const;
  // Required for using WebNodes in std maps.  Note the order used is
  // arbitrary and should not be expected to have any specific meaning.
  BLINK_EXPORT bool LessThan(const WebNode&) const;

  bool IsNull() const { return private_.IsNull(); }

  BLINK_EXPORT WebNode ParentNode() const;
  BLINK_EXPORT WebString NodeValue() const;
  BLINK_EXPORT WebDocument GetDocument() const;
  BLINK_EXPORT WebNode FirstChild() const;
  BLINK_EXPORT WebNode LastChild() const;
  BLINK_EXPORT WebNode PreviousSibling() const;
  BLINK_EXPORT WebNode NextSibling() const;

  BLINK_EXPORT bool IsLink() const;
  BLINK_EXPORT bool IsDocumentNode() const;
  BLINK_EXPORT bool IsDocumentTypeNode() const;
  BLINK_EXPORT bool IsCommentNode() const;
  BLINK_EXPORT bool IsTextNode() const;
  BLINK_EXPORT bool IsFocusable() const;
  BLINK_EXPORT bool IsContentEditable() const;
  BLINK_EXPORT bool IsElementNode() const;
  BLINK_EXPORT void SimulateClick();

  // The argument should be lower-cased.
  BLINK_EXPORT WebElementCollection
  GetElementsByHTMLTagName(const WebString&) const;

  // https://developer.mozilla.org/en-US/docs/Web/API/Document/querySelector
  // If the JS API would have thrown this returns null instead.
  BLINK_EXPORT WebElement QuerySelector(const WebString& selector) const;

  BLINK_EXPORT bool Focused() const;

  BLINK_EXPORT WebPluginContainer* PluginContainer() const;

  BLINK_EXPORT bool IsInsideFocusableElementOrARIAWidget() const;
  BLINK_EXPORT WebAXObject AccessibilityObject();

  template <typename T>
  T To();
  template <typename T>
  const T ToConst() const;

#if BLINK_IMPLEMENTATION
  BLINK_EXPORT static WebPluginContainer* PluginContainerFromNode(const Node*);

  BLINK_EXPORT WebNode(Node*);
  BLINK_EXPORT WebNode& operator=(Node*);
  BLINK_EXPORT operator Node*() const;

  template <typename T>
  T* Unwrap() {
    return static_cast<T*>(private_.Get());
  }

  template <typename T>
  const T* ConstUnwrap() const {
    return static_cast<const T*>(private_.Get());
  }
#endif

 protected:
  WebPrivatePtr<Node> private_;
};

#define DECLARE_WEB_NODE_TYPE_CASTS(type) \
  template <>                             \
  BLINK_EXPORT type WebNode::To<type>();  \
  template <>                             \
  BLINK_EXPORT const type WebNode::ToConst<type>() const;

#if BLINK_IMPLEMENTATION
#define DEFINE_WEB_NODE_TYPE_CASTS(type, predicate) \
  template <>                                       \
  type WebNode::To<type>() {                        \
    SECURITY_DCHECK(IsNull() || (predicate));       \
    type result;                                    \
    result.WebNode::Assign(*this);                  \
    return result;                                  \
  }                                                 \
  template <>                                       \
  const type WebNode::ToConst<type>() const {       \
    SECURITY_DCHECK(IsNull() || (predicate));       \
    type result;                                    \
    result.WebNode::Assign(*this);                  \
    return result;                                  \
  }
#endif

inline bool operator==(const WebNode& a, const WebNode& b) {
  return a.Equals(b);
}

inline bool operator!=(const WebNode& a, const WebNode& b) {
  return !(a == b);
}

inline bool operator<(const WebNode& a, const WebNode& b) {
  return a.LessThan(b);
}

}  // namespace blink

#endif
