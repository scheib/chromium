// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "modules/credentialmanager/PasswordCredential.h"

#include "bindings/core/v8/Dictionary.h"
#include "bindings/core/v8/ExceptionState.h"
#include "core/HTMLNames.h"
#include "core/dom/ExecutionContext.h"
#include "core/dom/URLSearchParams.h"
#include "core/html/FormData.h"
#include "core/html/HTMLFormElement.h"
#include "core/html/ListedElement.h"
#include "modules/credentialmanager/FormDataOptions.h"
#include "modules/credentialmanager/PasswordCredentialData.h"
#include "platform/credentialmanager/PlatformPasswordCredential.h"
#include "platform/weborigin/SecurityOrigin.h"
#include "public/platform/WebCredential.h"
#include "public/platform/WebPasswordCredential.h"

namespace blink {

PasswordCredential* PasswordCredential::Create(
    WebPasswordCredential* web_password_credential) {
  return new PasswordCredential(web_password_credential);
}

PasswordCredential* PasswordCredential::Create(
    const PasswordCredentialData& data,
    ExceptionState& exception_state) {
  if (data.id().IsEmpty()) {
    exception_state.ThrowTypeError("'id' must not be empty.");
    return nullptr;
  }
  if (data.password().IsEmpty()) {
    exception_state.ThrowTypeError("'password' must not be empty.");
    return nullptr;
  }

  KURL icon_url = ParseStringAsURL(data.iconURL(), exception_state);
  if (exception_state.HadException())
    return nullptr;

  return new PasswordCredential(data.id(), data.password(), data.name(),
                                icon_url);
}

// https://w3c.github.io/webappsec-credential-management/#passwordcredential-form-constructor
PasswordCredential* PasswordCredential::Create(
    HTMLFormElement* form,
    ExceptionState& exception_state) {
  // Extract data from the form, then use the extracted |formData| object's
  // value to populate |data|.
  FormData* form_data = FormData::Create(form);
  PasswordCredentialData data;

  AtomicString id_name;
  AtomicString password_name;
  for (ListedElement* element : form->ListedElements()) {
    // If |element| isn't a "submittable element" with string data, then it
    // won't have a matching value in |formData|, and we can safely skip it.
    FileOrUSVString result;
    form_data->get(element->GetName(), result);
    if (!result.isUSVString())
      continue;

    Vector<String> autofill_tokens;
    ToHTMLElement(element)
        ->FastGetAttribute(HTMLNames::autocompleteAttr)
        .GetString()
        .DeprecatedLower()  // Lowercase here once to avoid the case-folding
                            // logic below.
        .Split(' ', autofill_tokens);
    for (const auto& token : autofill_tokens) {
      if (token == "current-password" || token == "new-password") {
        data.setPassword(result.getAsUSVString());
        password_name = element->GetName();
      } else if (token == "photo") {
        data.setIconURL(result.getAsUSVString());
      } else if (token == "name" || token == "nickname") {
        data.setName(result.getAsUSVString());
      } else if (token == "username") {
        data.setId(result.getAsUSVString());
        id_name = element->GetName();
      }
    }
  }

  // Create a PasswordCredential using the data gathered above.
  PasswordCredential* credential =
      PasswordCredential::Create(data, exception_state);
  if (exception_state.HadException())
    return nullptr;
  DCHECK(credential);

  // After creating the Credential, populate its 'additionalData', 'idName', and
  // 'passwordName' attributes.  If the form's 'enctype' is anything other than
  // multipart, generate a URLSearchParams using the
  // data in |formData|.
  credential->setIdName(id_name);
  credential->setPasswordName(password_name);

  FormDataOrURLSearchParams additional_data;
  if (form->enctype() == "multipart/form-data") {
    additional_data.setFormData(form_data);
  } else {
    URLSearchParams* params = URLSearchParams::Create(String());
    for (const FormData::Entry* entry : form_data->Entries()) {
      if (entry->IsString())
        params->append(entry->name().data(), entry->Value().data());
    }
    additional_data.setURLSearchParams(params);
  }

  credential->setAdditionalData(additional_data);
  return credential;
}

PasswordCredential::PasswordCredential(
    WebPasswordCredential* web_password_credential)
    : CredentialUserData(web_password_credential->GetPlatformCredential()),
      id_name_("username"),
      password_name_("password") {}

PasswordCredential::PasswordCredential(const String& id,
                                       const String& password,
                                       const String& name,
                                       const KURL& icon)
    : CredentialUserData(
          PlatformPasswordCredential::Create(id, password, name, icon)),
      id_name_("username"),
      password_name_("password") {}

const String& PasswordCredential::password() const {
  return static_cast<PlatformPasswordCredential*>(platform_credential_.Get())
      ->Password();
}

PassRefPtr<EncodedFormData> PasswordCredential::EncodeFormData(
    String& content_type) const {
  if (additional_data_.isURLSearchParams()) {
    // If |additionalData| is a 'URLSearchParams' object, build a urlencoded
    // response.
    URLSearchParams* params = URLSearchParams::Create(String());
    URLSearchParams* additional_data = additional_data_.getAsURLSearchParams();
    for (const auto& param : additional_data->Params()) {
      const String& name = param.first;
      if (name != idName() && name != passwordName())
        params->append(name, param.second);
    }
    params->append(idName(), id());
    params->append(passwordName(), password());

    content_type =
        AtomicString("application/x-www-form-urlencoded;charset=UTF-8");

    return params->ToEncodedFormData();
  }

  // Otherwise, we'll build a multipart response.
  FormData* form_data = FormData::Create(nullptr);
  if (additional_data_.isFormData()) {
    FormData* additional_data = additional_data_.getAsFormData();
    for (const FormData::Entry* entry : additional_data->Entries()) {
      const String& name = form_data->Decode(entry->name());
      if (name == idName() || name == passwordName())
        continue;

      if (entry->GetBlob())
        form_data->append(name, entry->GetBlob(), entry->Filename());
      else
        form_data->append(name, form_data->Decode(entry->Value()));
    }
  }
  form_data->append(idName(), id());
  form_data->append(passwordName(), password());

  RefPtr<EncodedFormData> encoded_data = form_data->EncodeMultiPartFormData();
  content_type = AtomicString("multipart/form-data; boundary=") +
                 encoded_data->Boundary().data();
  return encoded_data.Release();
}

DEFINE_TRACE(PasswordCredential) {
  CredentialUserData::Trace(visitor);
  visitor->Trace(additional_data_);
}

}  // namespace blink
