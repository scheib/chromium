// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef Request_h
#define Request_h

#include "bindings/core/v8/Dictionary.h"
#include "bindings/modules/v8/RequestOrUSVString.h"
#include "modules/ModulesExport.h"
#include "modules/fetch/Body.h"
#include "modules/fetch/FetchRequestData.h"
#include "modules/fetch/Headers.h"
#include "platform/bindings/ScriptWrappable.h"
#include "platform/heap/Handle.h"
#include "platform/network/EncodedFormData.h"
#include "platform/weborigin/KURL.h"
#include "platform/wtf/text/WTFString.h"
#include "public/platform/WebURLRequest.h"

namespace blink {

class BodyStreamBuffer;
class EncodedFormData;
class RequestInit;
class WebServiceWorkerRequest;

using RequestInfo = RequestOrUSVString;

class MODULES_EXPORT Request final : public Body {
  DEFINE_WRAPPERTYPEINFO();
  WTF_MAKE_NONCOPYABLE(Request);

 public:
  // These "create" function must be called with entering an appropriate
  // V8 context.
  // From Request.idl:
  static Request* Create(ScriptState*,
                         const RequestInfo&,
                         const Dictionary&,
                         ExceptionState&);

  static Request* Create(ScriptState*, const String&, ExceptionState&);
  static Request* Create(ScriptState*,
                         const String&,
                         const Dictionary&,
                         ExceptionState&);
  static Request* Create(ScriptState*, Request*, ExceptionState&);
  static Request* Create(ScriptState*,
                         Request*,
                         const Dictionary&,
                         ExceptionState&);
  static Request* Create(ScriptState*, FetchRequestData*);
  static Request* Create(ScriptState*, const WebServiceWorkerRequest&);

  // From Request.idl:
  String method() const;
  KURL url() const;
  Headers* getHeaders() const { return headers_; }
  String Context() const;
  String referrer() const;
  String getReferrerPolicy() const;
  String mode() const;
  String credentials() const;
  String cache() const;
  String redirect() const;
  String integrity() const;

  // From Request.idl:
  // This function must be called with entering an appropriate V8 context.
  Request* clone(ScriptState*, ExceptionState&);

  FetchRequestData* PassRequestData(ScriptState*);
  void PopulateWebServiceWorkerRequest(WebServiceWorkerRequest&) const;
  bool HasBody() const;
  BodyStreamBuffer* BodyBuffer() override { return request_->Buffer(); }
  const BodyStreamBuffer* BodyBuffer() const override {
    return request_->Buffer();
  }
  PassRefPtr<EncodedFormData> AttachedCredential() const {
    return request_->AttachedCredential();
  }

  DECLARE_VIRTUAL_TRACE();

 private:
  Request(ScriptState*, FetchRequestData*, Headers*);
  Request(ScriptState*, FetchRequestData*);

  const FetchRequestData* GetRequest() const { return request_; }
  static Request* CreateRequestWithRequestOrString(ScriptState*,
                                                   Request*,
                                                   const String&,
                                                   RequestInit&,
                                                   ExceptionState&);

  String ContentType() const override;
  String MimeType() const override;
  void RefreshBody(ScriptState*);

  const Member<FetchRequestData> request_;
  const Member<Headers> headers_;
};

}  // namespace blink

#endif  // Request_h
