/*
 * Copyright (C) Research In Motion Limited 2010. All rights reserved.
 * Copyright (C) 2006 Apple Computer, Inc.
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

#include "core/page/FrameTree.h"

#include "core/dom/Document.h"
#include "core/frame/FrameClient.h"
#include "core/frame/FrameView.h"
#include "core/frame/LocalFrame.h"
#include "core/frame/RemoteFrame.h"
#include "core/frame/RemoteFrameView.h"
#include "core/frame/UseCounter.h"
#include "core/page/Page.h"
#include "platform/wtf/Assertions.h"
#include "platform/wtf/Vector.h"
#include "platform/wtf/text/CString.h"
#include "platform/wtf/text/StringBuilder.h"

using std::swap;

namespace blink {

namespace {

const unsigned kInvalidChildCount = ~0U;

}  // namespace

FrameTree::FrameTree(Frame* this_frame)
    : this_frame_(this_frame), scoped_child_count_(kInvalidChildCount) {}

FrameTree::~FrameTree() {}

const AtomicString& FrameTree::GetName() const {
  // TODO(andypaicu): remove this once we have gathered the data
  if (experimental_set_nulled_name_) {
    UseCounter::Count(this_frame_.Get(),
                      UseCounter::kCrossOriginMainFrameNulledNameAccessed);
  }
  return name_;
}

// TODO(andypaicu): remove this once we have gathered the data
void FrameTree::ExperimentalSetNulledName() {
  experimental_set_nulled_name_ = true;
}

void FrameTree::SetName(const AtomicString& name) {
  // TODO(andypaicu): remove this once we have gathered the data
  experimental_set_nulled_name_ = false;
  name_ = name;
}

DISABLE_CFI_PERF
Frame* FrameTree::Parent() const {
  if (!this_frame_->Client())
    return nullptr;
  return this_frame_->Client()->Parent();
}

Frame& FrameTree::Top() const {
  // FIXME: top() should never return null, so here are some hacks to deal
  // with EmptyLocalFrameClient and cases where the frame is detached
  // already...
  if (!this_frame_->Client())
    return *this_frame_;
  Frame* candidate = this_frame_->Client()->Top();
  return candidate ? *candidate : *this_frame_;
}

Frame* FrameTree::NextSibling() const {
  if (!this_frame_->Client())
    return nullptr;
  return this_frame_->Client()->NextSibling();
}

Frame* FrameTree::FirstChild() const {
  if (!this_frame_->Client())
    return nullptr;
  return this_frame_->Client()->FirstChild();
}

Frame* FrameTree::ScopedChild(unsigned index) const {
  unsigned scoped_index = 0;
  for (Frame* child = FirstChild(); child;
       child = child->Tree().NextSibling()) {
    if (child->Client()->InShadowTree())
      continue;
    if (scoped_index == index)
      return child;
    scoped_index++;
  }

  return nullptr;
}

Frame* FrameTree::ScopedChild(const AtomicString& name) const {
  for (Frame* child = FirstChild(); child;
       child = child->Tree().NextSibling()) {
    if (child->Client()->InShadowTree())
      continue;
    if (child->Tree().GetName() == name)
      return child;
  }
  return nullptr;
}

unsigned FrameTree::ScopedChildCount() const {
  if (scoped_child_count_ == kInvalidChildCount) {
    unsigned scoped_count = 0;
    for (Frame* child = FirstChild(); child;
         child = child->Tree().NextSibling()) {
      if (child->Client()->InShadowTree())
        continue;
      scoped_count++;
    }
    scoped_child_count_ = scoped_count;
  }
  return scoped_child_count_;
}

void FrameTree::InvalidateScopedChildCount() {
  scoped_child_count_ = kInvalidChildCount;
}

unsigned FrameTree::ChildCount() const {
  unsigned count = 0;
  for (Frame* result = FirstChild(); result;
       result = result->Tree().NextSibling())
    ++count;
  return count;
}

Frame* FrameTree::Find(const AtomicString& name) const {
  if (EqualIgnoringASCIICase(name, "_self") ||
      EqualIgnoringASCIICase(name, "_current") || name.IsEmpty())
    return this_frame_;

  if (EqualIgnoringASCIICase(name, "_top"))
    return &Top();

  if (EqualIgnoringASCIICase(name, "_parent"))
    return Parent() ? Parent() : this_frame_.Get();

  // Since "_blank" should never be any frame's name, the following just amounts
  // to an optimization.
  if (EqualIgnoringASCIICase(name, "_blank"))
    return nullptr;

  // Search subtree starting with this frame first.
  for (Frame* frame = this_frame_; frame;
       frame = frame->Tree().TraverseNext(this_frame_)) {
    if (frame->Tree().GetName() == name)
      return frame;
  }

  // Search the entire tree for this page next.
  Page* page = this_frame_->GetPage();

  // The frame could have been detached from the page, so check it.
  if (!page)
    return nullptr;

  for (Frame* frame = page->MainFrame(); frame;
       frame = frame->Tree().TraverseNext()) {
    if (frame->Tree().GetName() == name)
      return frame;
  }

  // Search the entire tree of each of the other pages in this namespace.
  // FIXME: Is random order OK?
  for (const Page* other_page : Page::OrdinaryPages()) {
    if (other_page == page || other_page->IsClosing())
      continue;
    for (Frame* frame = other_page->MainFrame(); frame;
         frame = frame->Tree().TraverseNext()) {
      if (frame->Tree().GetName() == name)
        return frame;
    }
  }

  return nullptr;
}

bool FrameTree::IsDescendantOf(const Frame* ancestor) const {
  if (!ancestor)
    return false;

  if (this_frame_->GetPage() != ancestor->GetPage())
    return false;

  for (Frame* frame = this_frame_; frame; frame = frame->Tree().Parent()) {
    if (frame == ancestor)
      return true;
  }
  return false;
}

DISABLE_CFI_PERF
Frame* FrameTree::TraverseNext(const Frame* stay_within) const {
  Frame* child = FirstChild();
  if (child) {
    DCHECK(!stay_within || child->Tree().IsDescendantOf(stay_within));
    return child;
  }

  if (this_frame_ == stay_within)
    return nullptr;

  Frame* sibling = NextSibling();
  if (sibling) {
    DCHECK(!stay_within || sibling->Tree().IsDescendantOf(stay_within));
    return sibling;
  }

  Frame* frame = this_frame_;
  while (!sibling && (!stay_within || frame->Tree().Parent() != stay_within)) {
    frame = frame->Tree().Parent();
    if (!frame)
      return nullptr;
    sibling = frame->Tree().NextSibling();
  }

  if (frame) {
    DCHECK(!stay_within || !sibling ||
           sibling->Tree().IsDescendantOf(stay_within));
    return sibling;
  }

  return nullptr;
}

DEFINE_TRACE(FrameTree) {
  visitor->Trace(this_frame_);
}

}  // namespace blink

#ifndef NDEBUG

static void printIndent(int indent) {
  for (int i = 0; i < indent; ++i)
    printf("    ");
}

static void printFrames(const blink::Frame* frame,
                        const blink::Frame* targetFrame,
                        int indent) {
  if (frame == targetFrame) {
    printf("--> ");
    printIndent(indent - 1);
  } else {
    printIndent(indent);
  }

  blink::FrameView* view =
      frame->IsLocalFrame() ? ToLocalFrame(frame)->View() : 0;
  printf("Frame %p %dx%d\n", frame, view ? view->Width() : 0,
         view ? view->Height() : 0);
  printIndent(indent);
  printf("  owner=%p\n", frame->Owner());
  printIndent(indent);
  printf("  frameView=%p\n", view);
  printIndent(indent);
  printf("  document=%p\n",
         frame->IsLocalFrame() ? ToLocalFrame(frame)->GetDocument() : 0);
  printIndent(indent);
  printf(
      "  uri=%s\n\n",
      frame->IsLocalFrame()
          ? ToLocalFrame(frame)->GetDocument()->Url().GetString().Utf8().data()
          : 0);

  for (blink::Frame* child = frame->Tree().FirstChild(); child;
       child = child->Tree().NextSibling())
    printFrames(child, targetFrame, indent + 1);
}

void showFrameTree(const blink::Frame* frame) {
  if (!frame) {
    printf("Null input frame\n");
    return;
  }

  printFrames(&frame->Tree().Top(), frame, 0);
}

#endif
