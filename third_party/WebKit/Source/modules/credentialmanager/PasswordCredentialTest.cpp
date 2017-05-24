// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "modules/credentialmanager/PasswordCredential.h"

#include <memory>
#include "bindings/core/v8/ExceptionState.h"
#include "core/dom/Document.h"
#include "core/dom/ExceptionCode.h"
#include "core/dom/URLSearchParams.h"
#include "core/frame/FrameView.h"
#include "core/html/FormData.h"
#include "core/html/HTMLFormElement.h"
#include "core/html/forms/FormController.h"
#include "core/testing/DummyPageHolder.h"
#include "platform/wtf/text/StringBuilder.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace blink {

class PasswordCredentialTest : public ::testing::Test {
 protected:
  void SetUp() override {
    dummy_page_holder_ = DummyPageHolder::Create();
    document_ = &dummy_page_holder_->GetDocument();
  }

  Document& GetDocument() const { return *document_; }

  HTMLFormElement* PopulateForm(const char* enctype, const char* html) {
    StringBuilder b;
    b.Append("<!DOCTYPE html><html><body><form id='theForm' enctype='");
    b.Append(enctype);
    b.Append("'>");
    b.Append(html);
    b.Append("</form></body></html>");
    GetDocument().documentElement()->setInnerHTML(b.ToString());
    GetDocument().View()->UpdateAllLifecyclePhases();
    HTMLFormElement* form =
        toHTMLFormElement(GetDocument().getElementById("theForm"));
    EXPECT_NE(nullptr, form);
    return form;
  }

 private:
  std::unique_ptr<DummyPageHolder> dummy_page_holder_;
  Persistent<Document> document_;
};

TEST_F(PasswordCredentialTest, CreateFromMultipartForm) {
  HTMLFormElement* form =
      PopulateForm("multipart/form-data",
                   "<input type='text' name='theId' value='musterman' "
                   "autocomplete='username'>"
                   "<input type='text' name='thePassword' value='sekrit' "
                   "autocomplete='current-password'>"
                   "<input type='text' name='theIcon' "
                   "value='https://example.com/photo' autocomplete='photo'>"
                   "<input type='text' name='theExtraField' value='extra'>"
                   "<input type='text' name='theName' value='friendly name' "
                   "autocomplete='name'>");
  PasswordCredential* credential =
      PasswordCredential::Create(form, ASSERT_NO_EXCEPTION);
  ASSERT_NE(nullptr, credential);
  EXPECT_EQ("theId", credential->idName());
  EXPECT_EQ("thePassword", credential->passwordName());

  EXPECT_EQ("musterman", credential->id());
  EXPECT_EQ("sekrit", credential->password());
  EXPECT_EQ(KURL(kParsedURLString, "https://example.com/photo"),
            credential->iconURL());
  EXPECT_EQ("friendly name", credential->name());
  EXPECT_EQ("password", credential->type());

  FormDataOrURLSearchParams additional_data;
  credential->additionalData(additional_data);
  ASSERT_TRUE(additional_data.isFormData());
  EXPECT_TRUE(additional_data.getAsFormData()->has("theId"));
  EXPECT_TRUE(additional_data.getAsFormData()->has("thePassword"));
  EXPECT_TRUE(additional_data.getAsFormData()->has("theIcon"));
  EXPECT_TRUE(additional_data.getAsFormData()->has("theName"));
  EXPECT_TRUE(additional_data.getAsFormData()->has("theExtraField"));
}

TEST_F(PasswordCredentialTest, CreateFromURLEncodedForm) {
  HTMLFormElement* form =
      PopulateForm("application/x-www-form-urlencoded",
                   "<input type='text' name='theId' value='musterman' "
                   "autocomplete='username'>"
                   "<input type='text' name='thePassword' value='sekrit' "
                   "autocomplete='current-password'>"
                   "<input type='text' name='theIcon' "
                   "value='https://example.com/photo' autocomplete='photo'>"
                   "<input type='text' name='theExtraField' value='extra'>"
                   "<input type='text' name='theName' value='friendly name' "
                   "autocomplete='name'>");
  PasswordCredential* credential =
      PasswordCredential::Create(form, ASSERT_NO_EXCEPTION);
  ASSERT_NE(nullptr, credential);
  EXPECT_EQ("theId", credential->idName());
  EXPECT_EQ("thePassword", credential->passwordName());

  EXPECT_EQ("musterman", credential->id());
  EXPECT_EQ("sekrit", credential->password());
  EXPECT_EQ(KURL(kParsedURLString, "https://example.com/photo"),
            credential->iconURL());
  EXPECT_EQ("friendly name", credential->name());
  EXPECT_EQ("password", credential->type());

  FormDataOrURLSearchParams additional_data;
  credential->additionalData(additional_data);
  ASSERT_TRUE(additional_data.isURLSearchParams());
  EXPECT_TRUE(additional_data.getAsURLSearchParams()->has("theId"));
  EXPECT_TRUE(additional_data.getAsURLSearchParams()->has("thePassword"));
  EXPECT_TRUE(additional_data.getAsURLSearchParams()->has("theIcon"));
  EXPECT_TRUE(additional_data.getAsURLSearchParams()->has("theName"));
  EXPECT_TRUE(additional_data.getAsURLSearchParams()->has("theExtraField"));
}

TEST_F(PasswordCredentialTest, CreateFromFormNoPassword) {
  HTMLFormElement* form =
      PopulateForm("multipart/form-data",
                   "<input type='text' name='theId' value='musterman' "
                   "autocomplete='username'>"
                   "<!-- No password field -->"
                   "<input type='text' name='theIcon' "
                   "value='https://example.com/photo' autocomplete='photo'>"
                   "<input type='text' name='theName' value='friendly name' "
                   "autocomplete='name'>");
  DummyExceptionStateForTesting exception_state;
  PasswordCredential* credential =
      PasswordCredential::Create(form, exception_state);
  EXPECT_EQ(nullptr, credential);
  EXPECT_TRUE(exception_state.HadException());
  EXPECT_EQ(kV8TypeError, exception_state.Code());
  EXPECT_EQ("'password' must not be empty.", exception_state.Message());
}

TEST_F(PasswordCredentialTest, CreateFromFormNoId) {
  HTMLFormElement* form =
      PopulateForm("multipart/form-data",
                   "<!-- No username field. -->"
                   "<input type='text' name='thePassword' value='sekrit' "
                   "autocomplete='current-password'>"
                   "<input type='text' name='theIcon' "
                   "value='https://example.com/photo' autocomplete='photo'>"
                   "<input type='text' name='theName' value='friendly name' "
                   "autocomplete='name'>");
  DummyExceptionStateForTesting exception_state;
  PasswordCredential* credential =
      PasswordCredential::Create(form, exception_state);
  EXPECT_EQ(nullptr, credential);
  EXPECT_TRUE(exception_state.HadException());
  EXPECT_EQ(kV8TypeError, exception_state.Code());
  EXPECT_EQ("'id' must not be empty.", exception_state.Message());
}

}  // namespace blink
