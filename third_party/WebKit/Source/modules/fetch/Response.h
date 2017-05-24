// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef Response_h
#define Response_h

#include "bindings/core/v8/Dictionary.h"
#include "bindings/core/v8/ScriptValue.h"
#include "modules/ModulesExport.h"
#include "modules/fetch/Body.h"
#include "modules/fetch/BodyStreamBuffer.h"
#include "modules/fetch/FetchResponseData.h"
#include "modules/fetch/Headers.h"
#include "platform/bindings/ScriptWrappable.h"
#include "platform/blob/BlobData.h"
#include "platform/heap/Handle.h"
#include "platform/wtf/Vector.h"
#include "platform/wtf/text/WTFString.h"

namespace blink {

class ExceptionState;
class ResponseInit;
class ScriptState;
class WebServiceWorkerResponse;

class MODULES_EXPORT Response final : public Body {
  DEFINE_WRAPPERTYPEINFO();
  WTF_MAKE_NONCOPYABLE(Response);

 public:
  // These "create" function which takes a ScriptState* must be called with
  // entering an appropriate V8 context.
  // From Response.idl:
  static Response* Create(ScriptState*, ExceptionState&);
  static Response* Create(ScriptState*,
                          ScriptValue body,
                          const ResponseInit&,
                          ExceptionState&);

  static Response* Create(ScriptState*,
                          BodyStreamBuffer*,
                          const String& content_type,
                          const ResponseInit&,
                          ExceptionState&);
  static Response* Create(ExecutionContext*, FetchResponseData*);
  static Response* Create(ScriptState*, const WebServiceWorkerResponse&);

  static Response* CreateClone(const Response&);

  static Response* error(ScriptState*);
  static Response* redirect(ScriptState*,
                            const String& url,
                            unsigned short status,
                            ExceptionState&);

  const FetchResponseData* GetResponse() const { return response_; }

  // From Response.idl:
  String type() const;
  String url() const;
  bool redirected() const;
  unsigned short status() const;
  bool ok() const;
  String statusText() const;
  Headers* headers() const;

  // From Response.idl:
  // This function must be called with entering an appropriate V8 context.
  Response* clone(ScriptState*, ExceptionState&);

  // ScriptWrappable
  bool HasPendingActivity() const final;

  // Does not call response.setBlobDataHandle().
  void PopulateWebServiceWorkerResponse(
      WebServiceWorkerResponse& /* response */);

  bool HasBody() const;
  BodyStreamBuffer* BodyBuffer() override { return response_->Buffer(); }
  // Returns the BodyStreamBuffer of |m_response|. This method doesn't check
  // the internal response of |m_response| even if |m_response| has it.
  const BodyStreamBuffer* BodyBuffer() const override {
    return response_->Buffer();
  }
  // Returns the BodyStreamBuffer of the internal response of |m_response| if
  // any. Otherwise, returns one of |m_response|.
  BodyStreamBuffer* InternalBodyBuffer() { return response_->InternalBuffer(); }
  const BodyStreamBuffer* InternalBodyBuffer() const {
    return response_->InternalBuffer();
  }
  bool bodyUsed() override;

  String ContentType() const override;
  String MimeType() const override;
  String InternalMIMEType() const;

  const Vector<KURL>& InternalURLList() const;

  DECLARE_VIRTUAL_TRACE();

 private:
  explicit Response(ExecutionContext*);
  Response(ExecutionContext*, FetchResponseData*);
  Response(ExecutionContext*, FetchResponseData*, Headers*);

  void InstallBody();
  void RefreshBody(ScriptState*);

  const Member<FetchResponseData> response_;
  const Member<Headers> headers_;
};

}  // namespace blink

#endif  // Response_h
