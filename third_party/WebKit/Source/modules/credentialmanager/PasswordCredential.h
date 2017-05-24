// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PasswordCredential_h
#define PasswordCredential_h

#include "bindings/core/v8/serialization/SerializedScriptValue.h"
#include "bindings/modules/v8/FormDataOrURLSearchParams.h"
#include "modules/ModulesExport.h"
#include "modules/credentialmanager/CredentialUserData.h"
#include "platform/bindings/ScriptWrappable.h"
#include "platform/heap/Handle.h"
#include "platform/network/EncodedFormData.h"
#include "platform/weborigin/KURL.h"

namespace blink {

class HTMLFormElement;
class PasswordCredentialData;
class WebPasswordCredential;

using CredentialPostBodyType = FormDataOrURLSearchParams;

class MODULES_EXPORT PasswordCredential final : public CredentialUserData {
  DEFINE_WRAPPERTYPEINFO();

 public:
  static PasswordCredential* Create(const PasswordCredentialData&,
                                    ExceptionState&);
  static PasswordCredential* Create(HTMLFormElement*, ExceptionState&);
  static PasswordCredential* Create(WebPasswordCredential*);

  // PasswordCredential.idl
  void setIdName(const String& name) { id_name_ = name; }
  const String& idName() const { return id_name_; }

  void setPasswordName(const String& name) { password_name_ = name; }
  const String& passwordName() const { return password_name_; }

  void setAdditionalData(const CredentialPostBodyType& data) {
    additional_data_ = data;
  }
  void additionalData(CredentialPostBodyType& out) const {
    out = additional_data_;
  }

  const String& password() const;

  // Internal methods
  PassRefPtr<EncodedFormData> EncodeFormData(String& content_type) const;
  DECLARE_VIRTUAL_TRACE();

 private:
  PasswordCredential(WebPasswordCredential*);
  PasswordCredential(const String& id,
                     const String& password,
                     const String& name,
                     const KURL& icon);

  String id_name_;
  String password_name_;
  CredentialPostBodyType additional_data_;
};

}  // namespace blink

#endif  // PasswordCredential_h
