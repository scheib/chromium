// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/payments/validating_textfield.h"

#include <utility>

namespace payments {

ValidatingTextfield::ValidatingTextfield(
    std::unique_ptr<ValidationDelegate> delegate)
    : Textfield(), delegate_(std::move(delegate)) {}

ValidatingTextfield::~ValidatingTextfield() {}

void ValidatingTextfield::OnBlur() {
  Textfield::OnBlur();

  // The first validation should be on a blur. The subsequent validations will
  // occur when the content changes. Do not validate if the view is being
  // removed.
  if (!was_blurred_ && !being_removed_) {
    was_blurred_ = true;
    Validate();
  }
}

void ValidatingTextfield::ViewHierarchyChanged(
    const ViewHierarchyChangedDetails& details) {
  if (details.child == this && !details.is_add)
    being_removed_ = true;
}

void ValidatingTextfield::OnContentsChanged() {
  // Validation on every keystroke only happens if the field has been validated
  // before as part of a blur.
  if (!was_blurred_)
    return;

  Validate();
}

bool ValidatingTextfield::IsValid() {
  bool valid = delegate_->IsValidTextfield(this);
  SetInvalid(!valid);
  return valid;
}

void ValidatingTextfield::Validate() {
  // TextfieldValueChanged may have side-effects, such as displaying errors.
  SetInvalid(!delegate_->TextfieldValueChanged(this));
}

}  // namespace payments
