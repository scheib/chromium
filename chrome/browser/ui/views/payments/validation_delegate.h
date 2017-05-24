// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_PAYMENTS_VALIDATION_DELEGATE_H_
#define CHROME_BROWSER_UI_VIEWS_PAYMENTS_VALIDATION_DELEGATE_H_

namespace views {
class Combobox;
class Textfield;
}  // namespace views

namespace payments {

class ValidationDelegate {
 public:
  virtual ~ValidationDelegate() {}

  // Only the delegate knows how to validate the input fields.
  virtual bool IsValidTextfield(views::Textfield* textfield) = 0;
  virtual bool IsValidCombobox(views::Combobox* combobox) = 0;

  // Notifications to let delegate react to input field changes and also let
  // caller know if the new values are valid.
  virtual bool TextfieldValueChanged(views::Textfield* textfield) = 0;
  virtual bool ComboboxValueChanged(views::Combobox* combobox) = 0;

  // Lets the delegate know that the model of the combobox has changed, e.g.,
  // when it gets filled asynchronously as for the state field.
  virtual void ComboboxModelChanged(views::Combobox* combobox) = 0;
};

}  // namespace payments

#endif  // CHROME_BROWSER_UI_VIEWS_PAYMENTS_VALIDATION_DELEGATE_H_
