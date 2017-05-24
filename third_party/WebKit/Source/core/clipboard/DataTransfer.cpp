/*
 * Copyright (C) 2006, 2007, 2008 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE COMPUTER, INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE COMPUTER, INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "core/clipboard/DataTransfer.h"

#include "core/HTMLNames.h"
#include "core/clipboard/DataObject.h"
#include "core/clipboard/DataTransferItem.h"
#include "core/clipboard/DataTransferItemList.h"
#include "core/editing/EphemeralRange.h"
#include "core/editing/FrameSelection.h"
#include "core/editing/serializers/Serialization.h"
#include "core/fileapi/FileList.h"
#include "core/frame/LocalFrame.h"
#include "core/html/HTMLImageElement.h"
#include "core/html/TextControlElement.h"
#include "core/layout/LayoutImage.h"
#include "core/layout/LayoutObject.h"
#include "core/loader/resource/ImageResourceContent.h"
#include "platform/DragImage.h"
#include "platform/clipboard/ClipboardMimeTypes.h"
#include "platform/clipboard/ClipboardUtilities.h"
#include "platform/network/mime/MIMETypeRegistry.h"
#include <memory>

namespace blink {

static DragOperation ConvertEffectAllowedToDragOperation(const String& op) {
  // Values specified in
  // http://www.whatwg.org/specs/web-apps/current-work/multipage/dnd.html#dom-datatransfer-effectallowed
  if (op == "uninitialized")
    return kDragOperationEvery;
  if (op == "none")
    return kDragOperationNone;
  if (op == "copy")
    return kDragOperationCopy;
  if (op == "link")
    return kDragOperationLink;
  if (op == "move")
    return (DragOperation)(kDragOperationGeneric | kDragOperationMove);
  if (op == "copyLink")
    return (DragOperation)(kDragOperationCopy | kDragOperationLink);
  if (op == "copyMove")
    return (DragOperation)(kDragOperationCopy | kDragOperationGeneric |
                           kDragOperationMove);
  if (op == "linkMove")
    return (DragOperation)(kDragOperationLink | kDragOperationGeneric |
                           kDragOperationMove);
  if (op == "all")
    return kDragOperationEvery;
  return kDragOperationPrivate;  // really a marker for "no conversion"
}

static String ConvertDragOperationToEffectAllowed(DragOperation op) {
  bool move_set = !!((kDragOperationGeneric | kDragOperationMove) & op);

  if ((move_set && (op & kDragOperationCopy) && (op & kDragOperationLink)) ||
      (op == kDragOperationEvery))
    return "all";
  if (move_set && (op & kDragOperationCopy))
    return "copyMove";
  if (move_set && (op & kDragOperationLink))
    return "linkMove";
  if ((op & kDragOperationCopy) && (op & kDragOperationLink))
    return "copyLink";
  if (move_set)
    return "move";
  if (op & kDragOperationCopy)
    return "copy";
  if (op & kDragOperationLink)
    return "link";
  return "none";
}

// We provide the IE clipboard types (URL and Text), and the clipboard types
// specified in the WHATWG Web Applications 1.0 draft see
// http://www.whatwg.org/specs/web-apps/current-work/ Section 6.3.5.3
static String NormalizeType(const String& type, bool* convert_to_url = 0) {
  String clean_type = type.StripWhiteSpace().DeprecatedLower();
  if (clean_type == kMimeTypeText ||
      clean_type.StartsWith(kMimeTypeTextPlainEtc))
    return kMimeTypeTextPlain;
  if (clean_type == kMimeTypeURL) {
    if (convert_to_url)
      *convert_to_url = true;
    return kMimeTypeTextURIList;
  }
  return clean_type;
}

DataTransfer* DataTransfer::Create() {
  DataTransfer* data =
      Create(kCopyAndPaste, kDataTransferWritable, DataObject::Create());
  data->drop_effect_ = "none";
  data->effect_allowed_ = "none";
  return data;
}

DataTransfer* DataTransfer::Create(DataTransferType type,
                                   DataTransferAccessPolicy policy,
                                   DataObject* data_object) {
  return new DataTransfer(type, policy, data_object);
}

DataTransfer::~DataTransfer() {}

void DataTransfer::setDropEffect(const String& effect) {
  if (!IsForDragAndDrop())
    return;

  // The attribute must ignore any attempts to set it to a value other than
  // none, copy, link, and move.
  if (effect != "none" && effect != "copy" && effect != "link" &&
      effect != "move")
    return;

  // The specification states that dropEffect can be changed at all times, even
  // if the DataTransfer instance is protected or neutered.
  //
  // Allowing these changes seems inconsequential, but findDropZone() in
  // EventHandler.cpp relies on being able to call setDropEffect during
  // dragenter, when the DataTransfer policy is DataTransferTypesReadable.
  drop_effect_ = effect;
}

void DataTransfer::setEffectAllowed(const String& effect) {
  if (!IsForDragAndDrop())
    return;

  if (ConvertEffectAllowedToDragOperation(effect) == kDragOperationPrivate) {
    // This means that there was no conversion, and the effectAllowed that
    // we are passed isn't a valid effectAllowed, so we should ignore it,
    // and not set m_effectAllowed.

    // The attribute must ignore any attempts to set it to a value other than
    // none, copy, copyLink, copyMove, link, linkMove, move, all, and
    // uninitialized.
    return;
  }

  if (CanWriteData())
    effect_allowed_ = effect;
}

void DataTransfer::clearData(const String& type) {
  if (!CanWriteData())
    return;

  if (type.IsNull())
    data_object_->ClearAll();
  else
    data_object_->ClearData(NormalizeType(type));
}

String DataTransfer::getData(const String& type) const {
  if (!CanReadData())
    return String();

  bool convert_to_url = false;
  String data = data_object_->GetData(NormalizeType(type, &convert_to_url));
  if (!convert_to_url)
    return data;
  return ConvertURIListToURL(data);
}

void DataTransfer::setData(const String& type, const String& data) {
  if (!CanWriteData())
    return;

  data_object_->SetData(NormalizeType(type), data);
}

bool DataTransfer::hasDataStoreItemListChanged() const {
  return data_store_item_list_changed_ || !CanReadTypes();
}

void DataTransfer::OnItemListChanged() {
  data_store_item_list_changed_ = true;
}

Vector<String> DataTransfer::types() {
  if (!CanReadTypes())
    return Vector<String>();

  data_store_item_list_changed_ = false;
  return data_object_->Types();
}

FileList* DataTransfer::files() const {
  FileList* files = FileList::Create();
  if (!CanReadData())
    return files;

  for (size_t i = 0; i < data_object_->length(); ++i) {
    if (data_object_->Item(i)->Kind() == DataObjectItem::kFileKind) {
      Blob* blob = data_object_->Item(i)->GetAsFile();
      if (blob && blob->IsFile())
        files->Append(ToFile(blob));
    }
  }

  return files;
}

void DataTransfer::setDragImage(Element* image, int x, int y) {
  DCHECK(image);

  if (!IsForDragAndDrop())
    return;

  IntPoint location(x, y);
  if (isHTMLImageElement(*image) && !image->isConnected())
    SetDragImageResource(toHTMLImageElement(*image).CachedImage(), location);
  else
    SetDragImageElement(image, location);
}

void DataTransfer::ClearDragImage() {
  if (!CanSetDragImage())
    return;

  drag_image_ = nullptr;
  drag_loc_ = IntPoint();
  drag_image_element_ = nullptr;
}

void DataTransfer::SetDragImageResource(ImageResourceContent* img,
                                        const IntPoint& loc) {
  setDragImage(img, 0, loc);
}

void DataTransfer::SetDragImageElement(Node* node, const IntPoint& loc) {
  setDragImage(0, node, loc);
}

std::unique_ptr<DragImage> DataTransfer::CreateDragImage(
    IntPoint& loc,
    LocalFrame* frame) const {
  if (drag_image_element_) {
    loc = drag_loc_;

    return frame->NodeImage(*drag_image_element_);
  }
  if (drag_image_) {
    loc = drag_loc_;
    return DragImage::Create(drag_image_->GetImage());
  }
  return nullptr;
}

static ImageResourceContent* GetImageResourceContent(Element* element) {
  // Attempt to pull ImageResourceContent from element
  DCHECK(element);
  LayoutObject* layout_object = element->GetLayoutObject();
  if (!layout_object || !layout_object->IsImage())
    return 0;

  LayoutImage* image = ToLayoutImage(layout_object);
  if (image->CachedImage() && !image->CachedImage()->ErrorOccurred())
    return image->CachedImage();

  return 0;
}

static void WriteImageToDataObject(DataObject* data_object,
                                   Element* element,
                                   const KURL& image_url) {
  // Shove image data into a DataObject for use as a file
  ImageResourceContent* cached_image = GetImageResourceContent(element);
  if (!cached_image || !cached_image->GetImage() || !cached_image->IsLoaded())
    return;

  RefPtr<SharedBuffer> image_buffer = cached_image->GetImage()->Data();
  if (!image_buffer || !image_buffer->size())
    return;

  data_object->AddSharedBuffer(
      image_buffer, image_url, cached_image->GetImage()->FilenameExtension(),
      cached_image->GetResponse().HttpHeaderFields().Get(
          HTTPNames::Content_Disposition));
}

void DataTransfer::DeclareAndWriteDragImage(Element* element,
                                            const KURL& link_url,
                                            const KURL& image_url,
                                            const String& title) {
  if (!data_object_)
    return;

  data_object_->SetURLAndTitle(link_url.IsValid() ? link_url : image_url,
                               title);

  // Write the bytes in the image to the file format.
  WriteImageToDataObject(data_object_.Get(), element, image_url);

  // Put img tag on the clipboard referencing the image
  data_object_->SetData(kMimeTypeTextHTML,
                        CreateMarkup(element, kIncludeNode, kResolveAllURLs));
}

void DataTransfer::WriteURL(Node* node, const KURL& url, const String& title) {
  if (!data_object_)
    return;
  DCHECK(!url.IsEmpty());

  data_object_->SetURLAndTitle(url, title);

  // The URL can also be used as plain text.
  data_object_->SetData(kMimeTypeTextPlain, url.GetString());

  // The URL can also be used as an HTML fragment.
  data_object_->SetHTMLAndBaseURL(
      CreateMarkup(node, kIncludeNode, kResolveAllURLs), url);
}

void DataTransfer::WriteSelection(const FrameSelection& selection) {
  if (!data_object_)
    return;

  if (!EnclosingTextControl(
          selection.ComputeVisibleSelectionInDOMTreeDeprecated().Start())) {
    data_object_->SetHTMLAndBaseURL(selection.SelectedHTMLForClipboard(),
                                    selection.GetFrame()->GetDocument()->Url());
  }

  String str = selection.SelectedTextForClipboard();
#if OS(WIN)
  ReplaceNewlinesWithWindowsStyleNewlines(str);
#endif
  ReplaceNBSPWithSpace(str);
  data_object_->SetData(kMimeTypeTextPlain, str);
}

void DataTransfer::SetAccessPolicy(DataTransferAccessPolicy policy) {
  // once you go numb, can never go back
  DCHECK(policy_ != kDataTransferNumb || policy == kDataTransferNumb);
  policy_ = policy;
}

bool DataTransfer::CanReadTypes() const {
  return policy_ == kDataTransferReadable ||
         policy_ == kDataTransferTypesReadable ||
         policy_ == kDataTransferWritable;
}

bool DataTransfer::CanReadData() const {
  return policy_ == kDataTransferReadable || policy_ == kDataTransferWritable;
}

bool DataTransfer::CanWriteData() const {
  return policy_ == kDataTransferWritable;
}

bool DataTransfer::CanSetDragImage() const {
  return policy_ == kDataTransferImageWritable ||
         policy_ == kDataTransferWritable;
}

DragOperation DataTransfer::SourceOperation() const {
  DragOperation op = ConvertEffectAllowedToDragOperation(effect_allowed_);
  DCHECK_NE(op, kDragOperationPrivate);
  return op;
}

DragOperation DataTransfer::DestinationOperation() const {
  DragOperation op = ConvertEffectAllowedToDragOperation(drop_effect_);
  DCHECK(op == kDragOperationCopy || op == kDragOperationNone ||
         op == kDragOperationLink ||
         op == (DragOperation)(kDragOperationGeneric | kDragOperationMove) ||
         op == kDragOperationEvery);
  return op;
}

void DataTransfer::SetSourceOperation(DragOperation op) {
  DCHECK_NE(op, kDragOperationPrivate);
  effect_allowed_ = ConvertDragOperationToEffectAllowed(op);
}

void DataTransfer::SetDestinationOperation(DragOperation op) {
  DCHECK(op == kDragOperationCopy || op == kDragOperationNone ||
         op == kDragOperationLink || op == kDragOperationGeneric ||
         op == kDragOperationMove ||
         op == static_cast<DragOperation>(kDragOperationGeneric |
                                          kDragOperationMove));
  drop_effect_ = ConvertDragOperationToEffectAllowed(op);
}

bool DataTransfer::HasDropZoneType(const String& keyword) {
  if (keyword.StartsWith("file:"))
    return HasFileOfType(keyword.Substring(5));

  if (keyword.StartsWith("string:"))
    return HasStringOfType(keyword.Substring(7));

  return false;
}

DataTransferItemList* DataTransfer::items() {
  // FIXME: According to the spec, we are supposed to return the same collection
  // of items each time. We now return a wrapper that always wraps the *same*
  // set of items, so JS shouldn't be able to tell, but we probably still want
  // to fix this.
  return DataTransferItemList::Create(this, data_object_);
}

DataObject* DataTransfer::GetDataObject() const {
  return data_object_;
}

DataTransfer::DataTransfer(DataTransferType type,
                           DataTransferAccessPolicy policy,
                           DataObject* data_object)
    : policy_(policy),
      drop_effect_("uninitialized"),
      effect_allowed_("uninitialized"),
      transfer_type_(type),
      data_object_(data_object),
      data_store_item_list_changed_(true) {
  data_object_->AddObserver(this);
}

void DataTransfer::setDragImage(ImageResourceContent* image,
                                Node* node,
                                const IntPoint& loc) {
  if (!CanSetDragImage())
    return;

  drag_image_ = image;
  drag_loc_ = loc;
  drag_image_element_ = node;
}

bool DataTransfer::HasFileOfType(const String& type) const {
  if (!CanReadTypes())
    return false;

  for (size_t i = 0; i < data_object_->length(); ++i) {
    if (data_object_->Item(i)->Kind() == DataObjectItem::kFileKind) {
      Blob* blob = data_object_->Item(i)->GetAsFile();
      if (blob && blob->IsFile() &&
          DeprecatedEqualIgnoringCase(blob->type(), type))
        return true;
    }
  }
  return false;
}

bool DataTransfer::HasStringOfType(const String& type) const {
  if (!CanReadTypes())
    return false;

  return data_object_->Types().Contains(type);
}

DragOperation ConvertDropZoneOperationToDragOperation(
    const String& drag_operation) {
  if (drag_operation == "copy")
    return kDragOperationCopy;
  if (drag_operation == "move")
    return kDragOperationMove;
  if (drag_operation == "link")
    return kDragOperationLink;
  return kDragOperationNone;
}

String ConvertDragOperationToDropZoneOperation(DragOperation operation) {
  switch (operation) {
    case kDragOperationCopy:
      return String("copy");
    case kDragOperationMove:
      return String("move");
    case kDragOperationLink:
      return String("link");
    default:
      return String("copy");
  }
}

DEFINE_TRACE(DataTransfer) {
  visitor->Trace(data_object_);
  visitor->Trace(drag_image_);
  visitor->Trace(drag_image_element_);
}

}  // namespace blink
