/*
 * Copyright (C) 2011 Google, Inc. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY GOOGLE INC. ``AS IS'' AND ANY
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

#include "core/frame/csp/ContentSecurityPolicy.h"

#include <memory>
#include "bindings/core/v8/ScriptController.h"
#include "core/dom/DOMStringList.h"
#include "core/dom/Document.h"
#include "core/dom/Element.h"
#include "core/dom/SandboxFlags.h"
#include "core/dom/TaskRunnerHelper.h"
#include "core/events/EventQueue.h"
#include "core/events/SecurityPolicyViolationEvent.h"
#include "core/frame/FrameClient.h"
#include "core/frame/LocalDOMWindow.h"
#include "core/frame/LocalFrame.h"
#include "core/frame/LocalFrameClient.h"
#include "core/frame/UseCounter.h"
#include "core/frame/csp/CSPDirectiveList.h"
#include "core/frame/csp/CSPSource.h"
#include "core/frame/csp/MediaListDirective.h"
#include "core/frame/csp/SourceListDirective.h"
#include "core/html/HTMLScriptElement.h"
#include "core/inspector/ConsoleMessage.h"
#include "core/loader/DocumentLoader.h"
#include "core/loader/PingLoader.h"
#include "core/probe/CoreProbes.h"
#include "core/workers/WorkerGlobalScope.h"
#include "platform/RuntimeEnabledFeatures.h"
#include "platform/json/JSONValues.h"
#include "platform/loader/fetch/IntegrityMetadata.h"
#include "platform/loader/fetch/ResourceRequest.h"
#include "platform/loader/fetch/ResourceResponse.h"
#include "platform/network/ContentSecurityPolicyParsers.h"
#include "platform/network/ContentSecurityPolicyResponseHeaders.h"
#include "platform/network/EncodedFormData.h"
#include "platform/weborigin/KURL.h"
#include "platform/weborigin/KnownPorts.h"
#include "platform/weborigin/SecurityOrigin.h"
#include "platform/wtf/NotFound.h"
#include "platform/wtf/PtrUtil.h"
#include "platform/wtf/StringHasher.h"
#include "platform/wtf/text/ParsingUtilities.h"
#include "platform/wtf/text/StringBuilder.h"
#include "platform/wtf/text/StringUTF8Adaptor.h"
#include "public/platform/Platform.h"
#include "public/platform/WebAddressSpace.h"
#include "public/platform/WebURLRequest.h"

namespace blink {

namespace {

// Helper function that returns true if the given |header_type| should be
// checked when the CheckHeaderType is |check_header_type|.
bool CheckHeaderTypeMatches(
    ContentSecurityPolicy::CheckHeaderType check_header_type,
    ContentSecurityPolicyHeaderType header_type) {
  switch (check_header_type) {
    case ContentSecurityPolicy::CheckHeaderType::kCheckAll:
      return true;
    case ContentSecurityPolicy::CheckHeaderType::kCheckReportOnly:
      return header_type == kContentSecurityPolicyHeaderTypeReport;
    case ContentSecurityPolicy::CheckHeaderType::kCheckEnforce:
      return header_type == kContentSecurityPolicyHeaderTypeEnforce;
  }
  NOTREACHED();
  return false;
}

}  // namespace

bool ContentSecurityPolicy::IsNonceableElement(const Element* element) {
  if (RuntimeEnabledFeatures::hideNonceContentAttributeEnabled()) {
    if (element->nonce().IsNull())
      return false;
  } else if (!element->FastHasAttribute(HTMLNames::nonceAttr)) {
    return false;
  }

  bool nonceable = true;

  // To prevent an attacker from hijacking an existing nonce via a dangling
  // markup injection, we walk through the attributes of each nonced script
  // element: if their names or values contain "<script" or "<style", we won't
  // apply the nonce when loading script.
  //
  // See http://blog.innerht.ml/csp-2015/#danglingmarkupinjection for an example
  // of the kind of attack this is aimed at mitigating.
  static const char kScriptString[] = "<script";
  static const char kStyleString[] = "<style";
  for (const Attribute& attr : element->Attributes()) {
    AtomicString name = attr.LocalName().LowerASCII();
    AtomicString value = attr.Value().LowerASCII();
    if (name.Find(kScriptString) != WTF::kNotFound ||
        name.Find(kStyleString) != WTF::kNotFound ||
        value.Find(kScriptString) != WTF::kNotFound ||
        value.Find(kStyleString) != WTF::kNotFound) {
      nonceable = false;
      break;
    }
  }

  UseCounter::Count(
      element->GetDocument(),
      nonceable ? UseCounter::kCleanScriptElementWithNonce
                : UseCounter::kPotentiallyInjectedScriptElementWithNonce);

  // This behavior is locked behind the experimental flag for the moment; if we
  // decide to ship it, drop this check. https://crbug.com/639293
  return !RuntimeEnabledFeatures::
             experimentalContentSecurityPolicyFeaturesEnabled() ||
         nonceable;
}

static UseCounter::Feature GetUseCounterType(
    ContentSecurityPolicyHeaderType type) {
  switch (type) {
    case kContentSecurityPolicyHeaderTypeEnforce:
      return UseCounter::kContentSecurityPolicy;
    case kContentSecurityPolicyHeaderTypeReport:
      return UseCounter::kContentSecurityPolicyReportOnly;
  }
  NOTREACHED();
  return UseCounter::kNumberOfFeatures;
}

ContentSecurityPolicy::ContentSecurityPolicy()
    : execution_context_(nullptr),
      override_inline_style_allowed_(false),
      script_hash_algorithms_used_(kContentSecurityPolicyHashAlgorithmNone),
      style_hash_algorithms_used_(kContentSecurityPolicyHashAlgorithmNone),
      sandbox_mask_(0),
      treat_as_public_address_(false),
      insecure_request_policy_(kLeaveInsecureRequestsAlone) {}

void ContentSecurityPolicy::BindToExecutionContext(
    ExecutionContext* execution_context) {
  execution_context_ = execution_context;
  ApplyPolicySideEffectsToExecutionContext();
}

void ContentSecurityPolicy::SetupSelf(const SecurityOrigin& security_origin) {
  // Ensure that 'self' processes correctly.
  self_protocol_ = security_origin.Protocol();
  self_source_ = new CSPSource(this, self_protocol_, security_origin.Host(),
                               security_origin.Port(), String(),
                               CSPSource::kNoWildcard, CSPSource::kNoWildcard);
}

void ContentSecurityPolicy::ApplyPolicySideEffectsToExecutionContext() {
  DCHECK(execution_context_ &&
         execution_context_->GetSecurityContext().GetSecurityOrigin());

  SetupSelf(*execution_context_->GetSecurityContext().GetSecurityOrigin());

  // Set mixed content checking and sandbox flags, then dump all the parsing
  // error messages, then poke at histograms.
  Document* document = this->GetDocument();
  if (sandbox_mask_ != kSandboxNone) {
    UseCounter::Count(execution_context_, UseCounter::kSandboxViaCSP);
    if (document)
      document->EnforceSandboxFlags(sandbox_mask_);
    else
      execution_context_->GetSecurityContext().ApplySandboxFlags(sandbox_mask_);
  }
  if (treat_as_public_address_) {
    execution_context_->GetSecurityContext().SetAddressSpace(
        kWebAddressSpacePublic);
  }

  if (document) {
    document->EnforceInsecureRequestPolicy(insecure_request_policy_);
  } else {
    execution_context_->GetSecurityContext().SetInsecureRequestPolicy(
        insecure_request_policy_);
  }

  if (insecure_request_policy_ & kUpgradeInsecureRequests) {
    UseCounter::Count(execution_context_,
                      UseCounter::kUpgradeInsecureRequestsEnabled);
    if (!execution_context_->Url().Host().IsEmpty()) {
      execution_context_->GetSecurityContext().AddInsecureNavigationUpgrade(
          execution_context_->Url().Host().Impl()->GetHash());
    }
  }

  for (const auto& console_message : console_messages_)
    execution_context_->AddConsoleMessage(console_message);
  console_messages_.clear();

  for (const auto& policy : policies_) {
    UseCounter::Count(execution_context_,
                      GetUseCounterType(policy->HeaderType()));
    if (policy->AllowDynamic())
      UseCounter::Count(execution_context_, UseCounter::kCSPWithStrictDynamic);
  }

  // We disable 'eval()' even in the case of report-only policies, and rely on
  // the check in the V8Initializer::codeGenerationCheckCallbackInMainThread
  // callback to determine whether the call should execute or not.
  if (!disable_eval_error_message_.IsNull())
    execution_context_->DisableEval(disable_eval_error_message_);
}

ContentSecurityPolicy::~ContentSecurityPolicy() {}

DEFINE_TRACE(ContentSecurityPolicy) {
  visitor->Trace(execution_context_);
  visitor->Trace(policies_);
  visitor->Trace(console_messages_);
  visitor->Trace(self_source_);
}

Document* ContentSecurityPolicy::GetDocument() const {
  return (execution_context_ && execution_context_->IsDocument())
             ? ToDocument(execution_context_)
             : nullptr;
}

void ContentSecurityPolicy::CopyStateFrom(const ContentSecurityPolicy* other) {
  DCHECK(policies_.IsEmpty());
  for (const auto& policy : other->policies_)
    AddAndReportPolicyFromHeaderValue(policy->Header(), policy->HeaderType(),
                                      policy->HeaderSource());
}

void ContentSecurityPolicy::CopyPluginTypesFrom(
    const ContentSecurityPolicy* other) {
  for (const auto& policy : other->policies_) {
    if (policy->HasPluginTypes()) {
      AddAndReportPolicyFromHeaderValue(policy->PluginTypesText(),
                                        policy->HeaderType(),
                                        policy->HeaderSource());
    }
  }
}

void ContentSecurityPolicy::DidReceiveHeaders(
    const ContentSecurityPolicyResponseHeaders& headers) {
  if (!headers.ContentSecurityPolicy().IsEmpty())
    AddAndReportPolicyFromHeaderValue(headers.ContentSecurityPolicy(),
                                      kContentSecurityPolicyHeaderTypeEnforce,
                                      kContentSecurityPolicyHeaderSourceHTTP);
  if (!headers.ContentSecurityPolicyReportOnly().IsEmpty())
    AddAndReportPolicyFromHeaderValue(headers.ContentSecurityPolicyReportOnly(),
                                      kContentSecurityPolicyHeaderTypeReport,
                                      kContentSecurityPolicyHeaderSourceHTTP);
}

void ContentSecurityPolicy::DidReceiveHeader(
    const String& header,
    ContentSecurityPolicyHeaderType type,
    ContentSecurityPolicyHeaderSource source) {
  AddAndReportPolicyFromHeaderValue(header, type, source);

  // This might be called after we've been bound to an execution context. For
  // example, a <meta> element might be injected after page load.
  if (execution_context_)
    ApplyPolicySideEffectsToExecutionContext();
}

bool ContentSecurityPolicy::ShouldEnforceEmbeddersPolicy(
    const ResourceResponse& response,
    SecurityOrigin* parent_origin) {
  if (response.Url().IsEmpty() || response.Url().ProtocolIsAbout() ||
      response.Url().ProtocolIsData() || response.Url().ProtocolIs("blob") ||
      response.Url().ProtocolIs("filesystem")) {
    return true;
  }

  if (parent_origin->CanAccess(SecurityOrigin::Create(response.Url()).Get()))
    return true;

  String header = response.HttpHeaderField(HTTPNames::Allow_CSP_From);
  header = header.StripWhiteSpace();
  if (header == "*")
    return true;
  if (RefPtr<SecurityOrigin> child_origin =
          SecurityOrigin::CreateFromString(header)) {
    return parent_origin->CanAccess(child_origin.Get());
  }

  return false;
}

void ContentSecurityPolicy::AddPolicyFromHeaderValue(
    const String& header,
    ContentSecurityPolicyHeaderType type,
    ContentSecurityPolicyHeaderSource source) {
  // If this is a report-only header inside a <meta> element, bail out.
  if (source == kContentSecurityPolicyHeaderSourceMeta &&
      type == kContentSecurityPolicyHeaderTypeReport) {
    ReportReportOnlyInMeta(header);
    return;
  }

  if (source == kContentSecurityPolicyHeaderSourceHTTP)
    header_delivered_ = true;

  Vector<UChar> characters;
  header.AppendTo(characters);

  const UChar* begin = characters.data();
  const UChar* end = begin + characters.size();

  // RFC2616, section 4.2 specifies that headers appearing multiple times can
  // be combined with a comma. Walk the header string, and parse each comma
  // separated chunk as a separate header.
  const UChar* position = begin;
  while (position < end) {
    skipUntil<UChar>(position, end, ',');

    // header1,header2 OR header1
    //        ^                  ^
    Member<CSPDirectiveList> policy =
        CSPDirectiveList::Create(this, begin, position, type, source);

    if (!policy->AllowEval(
            0, SecurityViolationReportingPolicy::kSuppressReporting) &&
        disable_eval_error_message_.IsNull())
      disable_eval_error_message_ = policy->EvalDisabledErrorMessage();

    policies_.push_back(policy.Release());

    // Skip the comma, and begin the next header from the current position.
    DCHECK(position == end || *position == ',');
    skipExactly<UChar>(position, end, ',');
    begin = position;
  }
}

void ContentSecurityPolicy::ReportAccumulatedHeaders(
    LocalFrameClient* client) const {
  // Notify the embedder about headers that have accumulated before the
  // navigation got committed.  See comments in
  // addAndReportPolicyFromHeaderValue for more details and context.
  DCHECK(client);
  WebVector<WebContentSecurityPolicy> policies(policies_.size());
  for (size_t i = 0; i < policies_.size(); ++i)
    policies[i] = policies_[i]->ExposeForNavigationalChecks();
  client->DidAddContentSecurityPolicies(policies);
}

void ContentSecurityPolicy::AddAndReportPolicyFromHeaderValue(
    const String& header,
    ContentSecurityPolicyHeaderType type,
    ContentSecurityPolicyHeaderSource source) {
  size_t previous_policy_count = policies_.size();
  AddPolicyFromHeaderValue(header, type, source);
  // Notify about the new header, so that it can be reported back to the
  // browser process.  This is needed in order to:
  // 1) replicate CSP directives (i.e. frame-src) to OOPIFs (only for now /
  // short-term).
  // 2) enforce CSP in the browser process (long-term - see
  // https://crbug.com/376522).
  // TODO(arthursonzogni): policies are actually replicated (1) and some of
  // them are enforced on the browser process (2). Stop doing (1) when (2) is
  // finished.
  WebVector<WebContentSecurityPolicy> policies(policies_.size() -
                                               previous_policy_count);
  for (size_t i = previous_policy_count; i < policies_.size(); ++i) {
    policies[i - previous_policy_count] =
        policies_[i]->ExposeForNavigationalChecks();
  }
  if (GetDocument() && GetDocument()->GetFrame()) {
    GetDocument()->GetFrame()->Client()->DidAddContentSecurityPolicies(
        policies);
  }
}

void ContentSecurityPolicy::SetOverrideAllowInlineStyle(bool value) {
  override_inline_style_allowed_ = value;
}

void ContentSecurityPolicy::SetOverrideURLForSelf(const KURL& url) {
  // Create a temporary CSPSource so that 'self' expressions can be resolved
  // before we bind to an execution context (for 'frame-ancestor' resolution,
  // for example). This CSPSource will be overwritten when we bind this object
  // to an execution context.
  RefPtr<SecurityOrigin> origin = SecurityOrigin::Create(url);
  self_protocol_ = origin->Protocol();
  self_source_ =
      new CSPSource(this, self_protocol_, origin->Host(), origin->Port(),
                    String(), CSPSource::kNoWildcard, CSPSource::kNoWildcard);
}

std::unique_ptr<Vector<CSPHeaderAndType>> ContentSecurityPolicy::Headers()
    const {
  std::unique_ptr<Vector<CSPHeaderAndType>> headers =
      WTF::WrapUnique(new Vector<CSPHeaderAndType>);
  for (const auto& policy : policies_) {
    CSPHeaderAndType header_and_type(policy->Header(), policy->HeaderType());
    headers->push_back(header_and_type);
  }
  return headers;
}

template <bool (CSPDirectiveList::*allowed)(
    ScriptState* script_state,
    SecurityViolationReportingPolicy,
    ContentSecurityPolicy::ExceptionStatus) const>
bool IsAllowedByAll(const CSPDirectiveListVector& policies,
                    ScriptState* script_state,
                    SecurityViolationReportingPolicy reporting_policy,
                    ContentSecurityPolicy::ExceptionStatus exception_status) {
  bool is_allowed = true;
  for (const auto& policy : policies) {
    is_allowed &= (policy.Get()->*allowed)(script_state, reporting_policy,
                                           exception_status);
  }
  return is_allowed;
}

template <bool (CSPDirectiveList::*allowed)(Element*,
                                            const String&,
                                            const String&,
                                            const WTF::OrdinalNumber&,
                                            SecurityViolationReportingPolicy)
              const>
bool IsAllowedByAll(const CSPDirectiveListVector& policies,
                    Element* element,
                    const String& source,
                    const String& context_url,
                    const WTF::OrdinalNumber& context_line,
                    SecurityViolationReportingPolicy reporting_policy) {
  bool is_allowed = true;
  for (const auto& policy : policies) {
    is_allowed &= (policy.Get()->*allowed)(element, source, context_url,
                                           context_line, reporting_policy);
  }
  return is_allowed;
}

template <bool (CSPDirectiveList::*allowed)(Element*,
                                            const String&,
                                            const String&,
                                            const WTF::OrdinalNumber&,
                                            SecurityViolationReportingPolicy,
                                            const String& content) const>
bool IsAllowedByAll(const CSPDirectiveListVector& policies,
                    Element* element,
                    const String& context_url,
                    const String& nonce,
                    const WTF::OrdinalNumber& context_line,
                    SecurityViolationReportingPolicy reporting_policy,
                    const String& content) {
  bool is_allowed = true;
  for (const auto& policy : policies) {
    is_allowed &= (policy.Get()->*allowed)(
        element, context_url, nonce, context_line, reporting_policy, content);
  }
  return is_allowed;
}

template <bool (CSPDirectiveList::*allowed)(const CSPHashValue&,
                                            ContentSecurityPolicy::InlineType)
              const>
bool IsAllowedByAll(const CSPDirectiveListVector& policies,
                    const CSPHashValue& hash_value,
                    ContentSecurityPolicy::InlineType type) {
  bool is_allowed = true;
  for (const auto& policy : policies)
    is_allowed &= (policy.Get()->*allowed)(hash_value, type);
  return is_allowed;
}

template <bool (CSPDirectiveList::*allowFromURL)(
    const KURL&,
    RedirectStatus,
    SecurityViolationReportingPolicy) const>
bool IsAllowedByAll(const CSPDirectiveListVector& policies,
                    const KURL& url,
                    RedirectStatus redirect_status,
                    SecurityViolationReportingPolicy reporting_policy,
                    ContentSecurityPolicy::CheckHeaderType check_header_type) {
  if (ContentSecurityPolicy::ShouldBypassContentSecurityPolicy(url))
    return true;

  bool is_allowed = true;
  for (const auto& policy : policies) {
    if (!CheckHeaderTypeMatches(check_header_type, policy->HeaderType()))
      continue;
    is_allowed &=
        (policy.Get()->*allowFromURL)(url, redirect_status, reporting_policy);
  }

  return is_allowed;
}

template <bool (CSPDirectiveList::*allowFromURLWithNonce)(
    const KURL&,
    const String& nonce,
    RedirectStatus,
    SecurityViolationReportingPolicy) const>
bool IsAllowedByAll(const CSPDirectiveListVector& policies,
                    const KURL& url,
                    const String& nonce,
                    RedirectStatus redirect_status,
                    SecurityViolationReportingPolicy reporting_policy,
                    ContentSecurityPolicy::CheckHeaderType check_header_type) {
  if (ContentSecurityPolicy::ShouldBypassContentSecurityPolicy(url))
    return true;

  bool is_allowed = true;
  for (const auto& policy : policies) {
    if (!CheckHeaderTypeMatches(check_header_type, policy->HeaderType()))
      continue;
    is_allowed &= (policy.Get()->*allowFromURLWithNonce)(
        url, nonce, redirect_status, reporting_policy);
  }
  return is_allowed;
}

template <bool (CSPDirectiveList::*allowFromURLWithNonceAndParser)(
    const KURL&,
    const String& nonce,
    const IntegrityMetadataSet& hashes,
    ParserDisposition parser_disposition,
    RedirectStatus,
    SecurityViolationReportingPolicy) const>
bool IsAllowedByAll(const CSPDirectiveListVector& policies,
                    const KURL& url,
                    const String& nonce,
                    const IntegrityMetadataSet& hashes,
                    ParserDisposition parser_disposition,
                    RedirectStatus redirect_status,
                    SecurityViolationReportingPolicy reporting_policy,
                    ContentSecurityPolicy::CheckHeaderType check_header_type) {
  if (ContentSecurityPolicy::ShouldBypassContentSecurityPolicy(url)) {
    // If we're running experimental features, bypass CSP only for
    // non-parser-inserted resources whose scheme otherwise bypasses CSP. If
    // we're not running experimental features, bypass CSP for all resources
    // regardless of parser state. Once we have more data via the
    // 'ScriptWithCSPBypassingScheme*' metrics, make a decision about what
    // behavior to ship. https://crbug.com/653521
    if (parser_disposition == kNotParserInserted ||
        !RuntimeEnabledFeatures::
            experimentalContentSecurityPolicyFeaturesEnabled()) {
      return true;
    }
  }

  bool is_allowed = true;
  for (const auto& policy : policies) {
    if (!CheckHeaderTypeMatches(check_header_type, policy->HeaderType()))
      continue;
    is_allowed &= (policy.Get()->*allowFromURLWithNonceAndParser)(
        url, nonce, hashes, parser_disposition, redirect_status,
        reporting_policy);
  }
  return is_allowed;
}

template <bool (CSPDirectiveList::*allowed)(LocalFrame*,
                                            const KURL&,
                                            SecurityViolationReportingPolicy)
              const>
bool IsAllowedByAll(const CSPDirectiveListVector& policies,
                    LocalFrame* frame,
                    const KURL& url,
                    SecurityViolationReportingPolicy reporting_policy) {
  bool is_allowed = true;
  for (const auto& policy : policies)
    is_allowed &= (policy.Get()->*allowed)(frame, url, reporting_policy);
  return is_allowed;
}

template <bool (CSPDirectiveList::*allowed)(const CSPHashValue&,
                                            ContentSecurityPolicy::InlineType)
              const>
bool CheckDigest(const String& source,
                 ContentSecurityPolicy::InlineType type,
                 uint8_t hash_algorithms_used,
                 const CSPDirectiveListVector& policies) {
  // Any additions or subtractions from this struct should also modify the
  // respective entries in the kSupportedPrefixes array in
  // SourceListDirective::parseHash().
  static const struct {
    ContentSecurityPolicyHashAlgorithm csp_hash_algorithm;
    HashAlgorithm algorithm;
  } kAlgorithmMap[] = {
      {kContentSecurityPolicyHashAlgorithmSha1, kHashAlgorithmSha1},
      {kContentSecurityPolicyHashAlgorithmSha256, kHashAlgorithmSha256},
      {kContentSecurityPolicyHashAlgorithmSha384, kHashAlgorithmSha384},
      {kContentSecurityPolicyHashAlgorithmSha512, kHashAlgorithmSha512}};

  // Only bother normalizing the source/computing digests if there are any
  // checks to be done.
  if (hash_algorithms_used == kContentSecurityPolicyHashAlgorithmNone)
    return false;

  StringUTF8Adaptor utf8_source(source);

  for (const auto& algorithm_map : kAlgorithmMap) {
    DigestValue digest;
    if (algorithm_map.csp_hash_algorithm & hash_algorithms_used) {
      bool digest_success =
          ComputeDigest(algorithm_map.algorithm, utf8_source.Data(),
                        utf8_source.length(), digest);
      if (digest_success &&
          IsAllowedByAll<allowed>(
              policies, CSPHashValue(algorithm_map.csp_hash_algorithm, digest),
              type))
        return true;
    }
  }

  return false;
}

bool ContentSecurityPolicy::AllowJavaScriptURLs(
    Element* element,
    const String& source,
    const String& context_url,
    const WTF::OrdinalNumber& context_line,
    SecurityViolationReportingPolicy reporting_policy) const {
  return IsAllowedByAll<&CSPDirectiveList::AllowJavaScriptURLs>(
      policies_, element, source, context_url, context_line, reporting_policy);
}

bool ContentSecurityPolicy::AllowInlineEventHandler(
    Element* element,
    const String& source,
    const String& context_url,
    const WTF::OrdinalNumber& context_line,
    SecurityViolationReportingPolicy reporting_policy) const {
  // Inline event handlers may be whitelisted by hash, if
  // 'unsafe-hash-attributes' is present in a policy. Check against the digest
  // of the |source| first before proceeding on to checking whether inline
  // script is allowed.
  if (CheckDigest<&CSPDirectiveList::AllowScriptHash>(
          source, InlineType::kAttribute, script_hash_algorithms_used_,
          policies_))
    return true;
  return IsAllowedByAll<&CSPDirectiveList::AllowInlineEventHandlers>(
      policies_, element, source, context_url, context_line, reporting_policy);
}

bool ContentSecurityPolicy::AllowInlineScript(
    Element* element,
    const String& context_url,
    const String& nonce,
    const WTF::OrdinalNumber& context_line,
    const String& script_content,
    SecurityViolationReportingPolicy reporting_policy) const {
  DCHECK(element);
  return IsAllowedByAll<&CSPDirectiveList::AllowInlineScript>(
      policies_, element, context_url, nonce, context_line, reporting_policy,
      script_content);
}

bool ContentSecurityPolicy::AllowInlineStyle(
    Element* element,
    const String& context_url,
    const String& nonce,
    const WTF::OrdinalNumber& context_line,
    const String& style_content,
    SecurityViolationReportingPolicy reporting_policy) const {
  DCHECK(element);
  if (override_inline_style_allowed_)
    return true;
  return IsAllowedByAll<&CSPDirectiveList::AllowInlineStyle>(
      policies_, element, context_url, nonce, context_line, reporting_policy,
      style_content);
}

bool ContentSecurityPolicy::AllowEval(
    ScriptState* script_state,
    SecurityViolationReportingPolicy reporting_policy,
    ContentSecurityPolicy::ExceptionStatus exception_status) const {
  return IsAllowedByAll<&CSPDirectiveList::AllowEval>(
      policies_, script_state, reporting_policy, exception_status);
}

String ContentSecurityPolicy::EvalDisabledErrorMessage() const {
  for (const auto& policy : policies_) {
    if (!policy->AllowEval(
            0, SecurityViolationReportingPolicy::kSuppressReporting))
      return policy->EvalDisabledErrorMessage();
  }
  return String();
}

bool ContentSecurityPolicy::AllowPluginType(
    const String& type,
    const String& type_attribute,
    const KURL& url,
    SecurityViolationReportingPolicy reporting_policy) const {
  for (const auto& policy : policies_) {
    if (!policy->AllowPluginType(type, type_attribute, url, reporting_policy))
      return false;
  }
  return true;
}

bool ContentSecurityPolicy::AllowPluginTypeForDocument(
    const Document& document,
    const String& type,
    const String& type_attribute,
    const KURL& url,
    SecurityViolationReportingPolicy reporting_policy) const {
  if (document.GetContentSecurityPolicy() &&
      !document.GetContentSecurityPolicy()->AllowPluginType(
          type, type_attribute, url))
    return false;

  // CSP says that a plugin document in a nested browsing context should
  // inherit the plugin-types of its parent.
  //
  // FIXME: The plugin-types directive should be pushed down into the
  // current document instead of reaching up to the parent for it here.
  LocalFrame* frame = document.GetFrame();
  if (frame && frame->Tree().Parent() && document.IsPluginDocument()) {
    ContentSecurityPolicy* parent_csp = frame->Tree()
                                            .Parent()
                                            ->GetSecurityContext()
                                            ->GetContentSecurityPolicy();
    if (parent_csp && !parent_csp->AllowPluginType(type, type_attribute, url))
      return false;
  }

  return true;
}

bool ContentSecurityPolicy::AllowScriptFromSource(
    const KURL& url,
    const String& nonce,
    const IntegrityMetadataSet& hashes,
    ParserDisposition parser_disposition,
    RedirectStatus redirect_status,
    SecurityViolationReportingPolicy reporting_policy,
    CheckHeaderType check_header_type) const {
  if (ShouldBypassContentSecurityPolicy(url)) {
    UseCounter::Count(
        GetDocument(),
        parser_disposition == kParserInserted
            ? UseCounter::kScriptWithCSPBypassingSchemeParserInserted
            : UseCounter::kScriptWithCSPBypassingSchemeNotParserInserted);
  }
  return IsAllowedByAll<&CSPDirectiveList::AllowScriptFromSource>(
      policies_, url, nonce, hashes, parser_disposition, redirect_status,
      reporting_policy, check_header_type);
}

bool ContentSecurityPolicy::AllowScriptWithHash(const String& source,
                                                InlineType type) const {
  return CheckDigest<&CSPDirectiveList::AllowScriptHash>(
      source, type, script_hash_algorithms_used_, policies_);
}

bool ContentSecurityPolicy::AllowStyleWithHash(const String& source,
                                               InlineType type) const {
  return CheckDigest<&CSPDirectiveList::AllowStyleHash>(
      source, type, style_hash_algorithms_used_, policies_);
}

bool ContentSecurityPolicy::AllowRequestWithoutIntegrity(
    WebURLRequest::RequestContext context,
    const KURL& url,
    RedirectStatus redirect_status,
    SecurityViolationReportingPolicy reporting_policy,
    CheckHeaderType check_header_type) const {
  for (const auto& policy : policies_) {
    if (CheckHeaderTypeMatches(check_header_type, policy->HeaderType()) &&
        !policy->AllowRequestWithoutIntegrity(context, url, redirect_status,
                                              reporting_policy))
      return false;
  }
  return true;
}

bool ContentSecurityPolicy::AllowRequest(
    WebURLRequest::RequestContext context,
    const KURL& url,
    const String& nonce,
    const IntegrityMetadataSet& integrity_metadata,
    ParserDisposition parser_disposition,
    RedirectStatus redirect_status,
    SecurityViolationReportingPolicy reporting_policy,
    CheckHeaderType check_header_type) const {
  if (integrity_metadata.IsEmpty() &&
      !AllowRequestWithoutIntegrity(context, url, redirect_status,
                                    reporting_policy, check_header_type)) {
    return false;
  }

  switch (context) {
    case WebURLRequest::kRequestContextAudio:
    case WebURLRequest::kRequestContextTrack:
    case WebURLRequest::kRequestContextVideo:
      return AllowMediaFromSource(url, redirect_status, reporting_policy,
                                  check_header_type);
    case WebURLRequest::kRequestContextBeacon:
    case WebURLRequest::kRequestContextEventSource:
    case WebURLRequest::kRequestContextFetch:
    case WebURLRequest::kRequestContextXMLHttpRequest:
    case WebURLRequest::kRequestContextSubresource:
      return AllowConnectToSource(url, redirect_status, reporting_policy,
                                  check_header_type);
    case WebURLRequest::kRequestContextEmbed:
    case WebURLRequest::kRequestContextObject:
      return AllowObjectFromSource(url, redirect_status, reporting_policy,
                                   check_header_type);
    case WebURLRequest::kRequestContextFavicon:
    case WebURLRequest::kRequestContextImage:
    case WebURLRequest::kRequestContextImageSet:
      return AllowImageFromSource(url, redirect_status, reporting_policy,
                                  check_header_type);
    case WebURLRequest::kRequestContextFont:
      return AllowFontFromSource(url, redirect_status, reporting_policy,
                                 check_header_type);
    case WebURLRequest::kRequestContextForm:
      return AllowFormAction(url, redirect_status, reporting_policy,
                             check_header_type);
    case WebURLRequest::kRequestContextFrame:
    case WebURLRequest::kRequestContextIframe:
      return AllowFrameFromSource(url, redirect_status, reporting_policy,
                                  check_header_type);
    case WebURLRequest::kRequestContextImport:
    case WebURLRequest::kRequestContextScript:
    case WebURLRequest::kRequestContextXSLT:
      return AllowScriptFromSource(url, nonce, integrity_metadata,
                                   parser_disposition, redirect_status,
                                   reporting_policy, check_header_type);
    case WebURLRequest::kRequestContextManifest:
      return AllowManifestFromSource(url, redirect_status, reporting_policy,
                                     check_header_type);
    case WebURLRequest::kRequestContextServiceWorker:
    case WebURLRequest::kRequestContextSharedWorker:
    case WebURLRequest::kRequestContextWorker:
      return AllowWorkerContextFromSource(url, redirect_status,
                                          reporting_policy, check_header_type);
    case WebURLRequest::kRequestContextStyle:
      return AllowStyleFromSource(url, nonce, redirect_status, reporting_policy,
                                  check_header_type);
    case WebURLRequest::kRequestContextCSPReport:
    case WebURLRequest::kRequestContextDownload:
    case WebURLRequest::kRequestContextHyperlink:
    case WebURLRequest::kRequestContextInternal:
    case WebURLRequest::kRequestContextLocation:
    case WebURLRequest::kRequestContextPing:
    case WebURLRequest::kRequestContextPlugin:
    case WebURLRequest::kRequestContextPrefetch:
    case WebURLRequest::kRequestContextUnspecified:
      return true;
  }
  NOTREACHED();
  return true;
}

void ContentSecurityPolicy::UsesScriptHashAlgorithms(uint8_t algorithms) {
  script_hash_algorithms_used_ |= algorithms;
}

void ContentSecurityPolicy::UsesStyleHashAlgorithms(uint8_t algorithms) {
  style_hash_algorithms_used_ |= algorithms;
}

bool ContentSecurityPolicy::AllowObjectFromSource(
    const KURL& url,
    RedirectStatus redirect_status,
    SecurityViolationReportingPolicy reporting_policy,
    CheckHeaderType check_header_type) const {
  return IsAllowedByAll<&CSPDirectiveList::AllowObjectFromSource>(
      policies_, url, redirect_status, reporting_policy, check_header_type);
}

bool ContentSecurityPolicy::AllowFrameFromSource(
    const KURL& url,
    RedirectStatus redirect_status,
    SecurityViolationReportingPolicy reporting_policy,
    CheckHeaderType check_header_type) const {
  return IsAllowedByAll<&CSPDirectiveList::AllowFrameFromSource>(
      policies_, url, redirect_status, reporting_policy, check_header_type);
}

bool ContentSecurityPolicy::AllowImageFromSource(
    const KURL& url,
    RedirectStatus redirect_status,
    SecurityViolationReportingPolicy reporting_policy,
    CheckHeaderType check_header_type) const {
  if (ShouldBypassContentSecurityPolicy(url, SchemeRegistry::kPolicyAreaImage))
    return true;
  return IsAllowedByAll<&CSPDirectiveList::AllowImageFromSource>(
      policies_, url, redirect_status, reporting_policy, check_header_type);
}

bool ContentSecurityPolicy::AllowStyleFromSource(
    const KURL& url,
    const String& nonce,
    RedirectStatus redirect_status,
    SecurityViolationReportingPolicy reporting_policy,
    CheckHeaderType check_header_type) const {
  if (ShouldBypassContentSecurityPolicy(url, SchemeRegistry::kPolicyAreaStyle))
    return true;
  return IsAllowedByAll<&CSPDirectiveList::AllowStyleFromSource>(
      policies_, url, nonce, redirect_status, reporting_policy,
      check_header_type);
}

bool ContentSecurityPolicy::AllowFontFromSource(
    const KURL& url,
    RedirectStatus redirect_status,
    SecurityViolationReportingPolicy reporting_policy,
    CheckHeaderType check_header_type) const {
  return IsAllowedByAll<&CSPDirectiveList::AllowFontFromSource>(
      policies_, url, redirect_status, reporting_policy, check_header_type);
}

bool ContentSecurityPolicy::AllowMediaFromSource(
    const KURL& url,
    RedirectStatus redirect_status,
    SecurityViolationReportingPolicy reporting_policy,
    CheckHeaderType check_header_type) const {
  return IsAllowedByAll<&CSPDirectiveList::AllowMediaFromSource>(
      policies_, url, redirect_status, reporting_policy, check_header_type);
}

bool ContentSecurityPolicy::AllowConnectToSource(
    const KURL& url,
    RedirectStatus redirect_status,
    SecurityViolationReportingPolicy reporting_policy,
    CheckHeaderType check_header_type) const {
  return IsAllowedByAll<&CSPDirectiveList::AllowConnectToSource>(
      policies_, url, redirect_status, reporting_policy, check_header_type);
}

bool ContentSecurityPolicy::AllowFormAction(
    const KURL& url,
    RedirectStatus redirect_status,
    SecurityViolationReportingPolicy reporting_policy,
    CheckHeaderType check_header_type) const {
  return IsAllowedByAll<&CSPDirectiveList::AllowFormAction>(
      policies_, url, redirect_status, reporting_policy, check_header_type);
}

bool ContentSecurityPolicy::AllowBaseURI(
    const KURL& url,
    RedirectStatus redirect_status,
    SecurityViolationReportingPolicy reporting_policy) const {
  // `base-uri` isn't affected by 'upgrade-insecure-requests', so we'll check
  // both report-only and enforce headers here.
  return IsAllowedByAll<&CSPDirectiveList::AllowBaseURI>(
      policies_, url, redirect_status, reporting_policy,
      ContentSecurityPolicy::CheckHeaderType::kCheckAll);
}

bool ContentSecurityPolicy::AllowWorkerContextFromSource(
    const KURL& url,
    RedirectStatus redirect_status,
    SecurityViolationReportingPolicy reporting_policy,
    CheckHeaderType check_header_type) const {
  // CSP 1.1 moves workers from 'script-src' to the new 'child-src'. Measure the
  // impact of this backwards-incompatible change.
  // TODO(mkwst): We reverted this.
  if (Document* document = this->GetDocument()) {
    UseCounter::Count(*document, UseCounter::kWorkerSubjectToCSP);
    if (IsAllowedByAll<&CSPDirectiveList::AllowWorkerFromSource>(
            policies_, url, redirect_status,
            SecurityViolationReportingPolicy::kSuppressReporting,
            check_header_type) &&
        !IsAllowedByAll<&CSPDirectiveList::AllowScriptFromSource>(
            policies_, url, AtomicString(), IntegrityMetadataSet(),
            kNotParserInserted, redirect_status,
            SecurityViolationReportingPolicy::kSuppressReporting,
            check_header_type)) {
      UseCounter::Count(*document,
                        UseCounter::kWorkerAllowedByChildBlockedByScript);
    }
  }

  return IsAllowedByAll<&CSPDirectiveList::AllowWorkerFromSource>(
      policies_, url, redirect_status, reporting_policy, check_header_type);
}

bool ContentSecurityPolicy::AllowManifestFromSource(
    const KURL& url,
    RedirectStatus redirect_status,
    SecurityViolationReportingPolicy reporting_policy,
    CheckHeaderType check_header_type) const {
  return IsAllowedByAll<&CSPDirectiveList::AllowManifestFromSource>(
      policies_, url, redirect_status, reporting_policy, check_header_type);
}

bool ContentSecurityPolicy::AllowAncestors(
    LocalFrame* frame,
    const KURL& url,
    SecurityViolationReportingPolicy reporting_policy) const {
  return IsAllowedByAll<&CSPDirectiveList::AllowAncestors>(
      policies_, frame, url, reporting_policy);
}

bool ContentSecurityPolicy::IsFrameAncestorsEnforced() const {
  for (const auto& policy : policies_) {
    if (policy->IsFrameAncestorsEnforced())
      return true;
  }
  return false;
}

bool ContentSecurityPolicy::IsActive() const {
  return !policies_.IsEmpty();
}

const KURL ContentSecurityPolicy::Url() const {
  return execution_context_->ContextURL();
}

KURL ContentSecurityPolicy::CompleteURL(const String& url) const {
  return execution_context_->ContextCompleteURL(url);
}

void ContentSecurityPolicy::EnforceSandboxFlags(SandboxFlags mask) {
  sandbox_mask_ |= mask;
}

void ContentSecurityPolicy::TreatAsPublicAddress() {
  if (!RuntimeEnabledFeatures::corsRFC1918Enabled())
    return;
  treat_as_public_address_ = true;
}

void ContentSecurityPolicy::EnforceStrictMixedContentChecking() {
  insecure_request_policy_ |= kBlockAllMixedContent;
}

void ContentSecurityPolicy::UpgradeInsecureRequests() {
  insecure_request_policy_ |= kUpgradeInsecureRequests;
}

static String StripURLForUseInReport(
    ExecutionContext* context,
    const KURL& url,
    RedirectStatus redirect_status,
    const ContentSecurityPolicy::DirectiveType& effective_type) {
  if (!url.IsValid())
    return String();
  if (!url.IsHierarchical() || url.ProtocolIs("file"))
    return url.Protocol();

  // Until we're more careful about the way we deal with navigations in frames
  // (and, by extension, in plugin documents), strip cross-origin 'frame-src'
  // and 'object-src' violations down to an origin. https://crbug.com/633306
  bool can_safely_expose_url =
      context->GetSecurityOrigin()->CanRequest(url) ||
      (redirect_status == RedirectStatus::kNoRedirect &&
       effective_type != ContentSecurityPolicy::DirectiveType::kFrameSrc &&
       effective_type != ContentSecurityPolicy::DirectiveType::kObjectSrc);

  if (can_safely_expose_url) {
    // 'KURL::strippedForUseAsReferrer()' dumps 'String()' for non-webby URLs.
    // It's better for developers if we return the origin of those URLs rather
    // than nothing.
    if (url.ProtocolIsInHTTPFamily())
      return url.StrippedForUseAsReferrer();
  }
  return SecurityOrigin::Create(url)->ToString();
}

static void GatherSecurityPolicyViolationEventData(
    SecurityPolicyViolationEventInit& init,
    ExecutionContext* context,
    const String& directive_text,
    const ContentSecurityPolicy::DirectiveType& effective_type,
    const KURL& blocked_url,
    const String& header,
    RedirectStatus redirect_status,
    ContentSecurityPolicyHeaderType header_type,
    ContentSecurityPolicy::ViolationType violation_type,
    std::unique_ptr<SourceLocation> source_location,
    const String& script_source) {
  if (effective_type == ContentSecurityPolicy::DirectiveType::kFrameAncestors) {
    // If this load was blocked via 'frame-ancestors', then the URL of
    // |document| has not yet been initialized. In this case, we'll set both
    // 'documentURI' and 'blockedURI' to the blocked document's URL.
    String stripped_url = StripURLForUseInReport(
        context, blocked_url, RedirectStatus::kNoRedirect,
        ContentSecurityPolicy::DirectiveType::kDefaultSrc);
    init.setDocumentURI(stripped_url);
    init.setBlockedURI(stripped_url);
  } else {
    String stripped_url = StripURLForUseInReport(
        context, context->Url(), RedirectStatus::kNoRedirect,
        ContentSecurityPolicy::DirectiveType::kDefaultSrc);
    init.setDocumentURI(stripped_url);
    switch (violation_type) {
      case ContentSecurityPolicy::kInlineViolation:
        init.setBlockedURI("inline");
        break;
      case ContentSecurityPolicy::kEvalViolation:
        init.setBlockedURI("eval");
        break;
      case ContentSecurityPolicy::kURLViolation:
        init.setBlockedURI(StripURLForUseInReport(
            context, blocked_url, redirect_status, effective_type));
        break;
    }
  }

  String effective_directive =
      ContentSecurityPolicy::GetDirectiveName(effective_type);
  init.setViolatedDirective(effective_directive);
  init.setEffectiveDirective(effective_directive);
  init.setOriginalPolicy(header);
  init.setDisposition(header_type == kContentSecurityPolicyHeaderTypeEnforce
                          ? "enforce"
                          : "report");
  init.setStatusCode(0);

  // TODO(mkwst): We only have referrer and status code information for
  // Documents. It would be nice to get them for Workers as well.
  if (context->IsDocument()) {
    Document* document = ToDocument(context);
    DCHECK(document);
    init.setReferrer(document->referrer());
    if (!SecurityOrigin::IsSecure(context->Url()) && document->Loader())
      init.setStatusCode(document->Loader()->GetResponse().HttpStatusCode());
  }

  // If no source location is provided, use the source location of the context.
  if (!source_location)
    source_location = SourceLocation::Capture(context);
  if (source_location->LineNumber()) {
    KURL source = KURL(kParsedURLString, source_location->Url());
    init.setSourceFile(StripURLForUseInReport(context, source, redirect_status,
                                              effective_type));
    init.setLineNumber(source_location->LineNumber());
    init.setColumnNumber(source_location->ColumnNumber());
  } else {
    init.setSourceFile(String());
    init.setLineNumber(0);
    init.setColumnNumber(0);
  }

  if (!script_source.IsEmpty())
    init.setSample(script_source.StripWhiteSpace().Left(40));
}

void ContentSecurityPolicy::ReportViolation(
    const String& directive_text,
    const DirectiveType& effective_type,
    const String& console_message,
    const KURL& blocked_url,
    const Vector<String>& report_endpoints,
    const String& header,
    ContentSecurityPolicyHeaderType header_type,
    ViolationType violation_type,
    std::unique_ptr<SourceLocation> source_location,
    LocalFrame* context_frame,
    RedirectStatus redirect_status,
    Element* element,
    const String& source) {
  DCHECK(violation_type == kURLViolation || blocked_url.IsEmpty());

  // TODO(lukasza): Support sending reports from OOPIFs -
  // https://crbug.com/611232 (or move CSP child-src and frame-src checks to the
  // browser process - see https://crbug.com/376522).
  if (!execution_context_ && !context_frame) {
    DCHECK(effective_type == DirectiveType::kChildSrc ||
           effective_type == DirectiveType::kFrameSrc ||
           effective_type == DirectiveType::kPluginTypes);
    return;
  }

  DCHECK((execution_context_ && !context_frame) ||
         ((effective_type == DirectiveType::kFrameAncestors) && context_frame));

  SecurityPolicyViolationEventInit violation_data;

  // If we're processing 'frame-ancestors', use |contextFrame|'s execution
  // context to gather data. Otherwise, use the policy's execution context.
  ExecutionContext* relevant_context =
      context_frame ? context_frame->GetDocument() : execution_context_;
  DCHECK(relevant_context);
  GatherSecurityPolicyViolationEventData(
      violation_data, relevant_context, directive_text, effective_type,
      blocked_url, header, redirect_status, header_type, violation_type,
      std::move(source_location), source);

  // TODO(mkwst): Obviously, we shouldn't hit this check, as extension-loaded
  // resources should be allowed regardless. We apparently do, however, so
  // we should at least stop spamming reporting endpoints. See
  // https://crbug.com/524356 for detail.
  if (!violation_data.sourceFile().IsEmpty() &&
      ShouldBypassContentSecurityPolicy(
          KURL(kParsedURLString, violation_data.sourceFile()))) {
    return;
  }

  PostViolationReport(violation_data, context_frame, report_endpoints);

  // Fire a violation event if we're working within an execution context (e.g.
  // we're not processing 'frame-ancestors').
  if (execution_context_) {
    TaskRunnerHelper::Get(TaskType::kNetworking, execution_context_)
        ->PostTask(BLINK_FROM_HERE,
                   WTF::Bind(&ContentSecurityPolicy::DispatchViolationEvents,
                             WrapPersistent(this), violation_data,
                             WrapPersistent(element)));
  }
}

void ContentSecurityPolicy::PostViolationReport(
    const SecurityPolicyViolationEventInit& violation_data,
    LocalFrame* context_frame,
    const Vector<String>& report_endpoints) {
  // We need to be careful here when deciding what information to send to the
  // report-uri. Currently, we send only the current document's URL and the
  // directive that was violated. The document's URL is safe to send because
  // it's the document itself that's requesting that it be sent. You could
  // make an argument that we shouldn't send HTTPS document URLs to HTTP
  // report-uris (for the same reasons that we supress the Referer in that
  // case), but the Referer is sent implicitly whereas this request is only
  // sent explicitly. As for which directive was violated, that's pretty
  // harmless information.
  //
  // TODO(mkwst): This justification is BS. Insecure reports are mixed content,
  // let's kill them. https://crbug.com/695363

  std::unique_ptr<JSONObject> csp_report = JSONObject::Create();
  csp_report->SetString("document-uri", violation_data.documentURI());
  csp_report->SetString("referrer", violation_data.referrer());
  csp_report->SetString("violated-directive",
                        violation_data.violatedDirective());
  csp_report->SetString("effective-directive",
                        violation_data.effectiveDirective());
  csp_report->SetString("original-policy", violation_data.originalPolicy());
  csp_report->SetString("disposition", violation_data.disposition());
  csp_report->SetString("blocked-uri", violation_data.blockedURI());
  if (violation_data.lineNumber())
    csp_report->SetInteger("line-number", violation_data.lineNumber());
  if (violation_data.columnNumber())
    csp_report->SetInteger("column-number", violation_data.columnNumber());
  if (!violation_data.sourceFile().IsEmpty())
    csp_report->SetString("source-file", violation_data.sourceFile());
  csp_report->SetInteger("status-code", violation_data.statusCode());

  csp_report->SetString("script-sample", violation_data.sample());

  std::unique_ptr<JSONObject> report_object = JSONObject::Create();
  report_object->SetObject("csp-report", std::move(csp_report));
  String stringified_report = report_object->ToJSONString();

  // Only POST unique reports to the external endpoint; repeated reports add no
  // value on the server side, as they're indistinguishable. Note that we'll
  // fire the DOM event for every violation, as the page has enough context to
  // react in some reasonable way to each violation as it occurs.
  if (ShouldSendViolationReport(stringified_report)) {
    DidSendViolationReport(stringified_report);

    // TODO(mkwst): Support POSTing violation reports from a Worker.
    Document* document =
        context_frame ? context_frame->GetDocument() : this->GetDocument();
    if (!document)
      return;

    LocalFrame* frame = document->GetFrame();
    if (!frame)
      return;

    RefPtr<EncodedFormData> report =
        EncodedFormData::Create(stringified_report.Utf8());

    for (const String& endpoint : report_endpoints) {
      // If we have a context frame we're dealing with 'frame-ancestors' and we
      // don't have our own execution context. Use the frame's document to
      // complete the endpoint URL, overriding its URL with the blocked
      // document's URL.
      DCHECK(!context_frame || !execution_context_);
      DCHECK(!context_frame ||
             GetDirectiveType(violation_data.effectiveDirective()) ==
                 DirectiveType::kFrameAncestors);
      KURL url = context_frame
                     ? frame->GetDocument()->CompleteURLWithOverride(
                           endpoint,
                           KURL(kParsedURLString, violation_data.blockedURI()))
                     : CompleteURL(endpoint);
      PingLoader::SendViolationReport(
          frame, url, report,
          PingLoader::kContentSecurityPolicyViolationReport);
    }
  }
}

void ContentSecurityPolicy::DispatchViolationEvents(
    const SecurityPolicyViolationEventInit& violation_data,
    Element* element) {
  // If the context is detached or closed (thus clearing its event queue)
  // between the violation occuring and this event dispatch, exit early.
  EventQueue* queue = execution_context_->GetEventQueue();
  if (!queue)
    return;

  SecurityPolicyViolationEvent* event = SecurityPolicyViolationEvent::Create(
      EventTypeNames::securitypolicyviolation, violation_data);
  DCHECK(event->bubbles());

  if (execution_context_->IsDocument()) {
    Document* document = ToDocument(execution_context_);
    if (element && element->isConnected() && element->GetDocument() == document)
      event->SetTarget(element);
    else
      event->SetTarget(document);
  } else if (execution_context_->IsWorkerGlobalScope()) {
    event->SetTarget(ToWorkerGlobalScope(execution_context_));
  }
  queue->EnqueueEvent(event);
}

void ContentSecurityPolicy::ReportMixedContent(const KURL& mixed_url,
                                               RedirectStatus redirect_status) {
  for (const auto& policy : policies_)
    policy->ReportMixedContent(mixed_url, redirect_status);
}

void ContentSecurityPolicy::ReportReportOnlyInMeta(const String& header) {
  LogToConsole("The report-only Content Security Policy '" + header +
               "' was delivered via a <meta> element, which is disallowed. The "
               "policy has been ignored.");
}

void ContentSecurityPolicy::ReportMetaOutsideHead(const String& header) {
  LogToConsole("The Content Security Policy '" + header +
               "' was delivered via a <meta> element outside the document's "
               "<head>, which is disallowed. The policy has been ignored.");
}

void ContentSecurityPolicy::ReportValueForEmptyDirective(const String& name,
                                                         const String& value) {
  LogToConsole("The Content Security Policy directive '" + name +
               "' should be empty, but was delivered with a value of '" +
               value +
               "'. The directive has been applied, and the value ignored.");
}

void ContentSecurityPolicy::ReportInvalidInReportOnly(const String& name) {
  LogToConsole("The Content Security Policy directive '" + name +
               "' is ignored when delivered in a report-only policy.");
}

void ContentSecurityPolicy::ReportInvalidDirectiveInMeta(
    const String& directive) {
  LogToConsole(
      "Content Security Policies delivered via a <meta> element may not "
      "contain the " +
      directive + " directive.");
}

void ContentSecurityPolicy::ReportUnsupportedDirective(const String& name) {
  static const char kAllow[] = "allow";
  static const char kOptions[] = "options";
  static const char kPolicyURI[] = "policy-uri";
  static const char kAllowMessage[] =
      "The 'allow' directive has been replaced with 'default-src'. Please use "
      "that directive instead, as 'allow' has no effect.";
  static const char kOptionsMessage[] =
      "The 'options' directive has been replaced with 'unsafe-inline' and "
      "'unsafe-eval' source expressions for the 'script-src' and 'style-src' "
      "directives. Please use those directives instead, as 'options' has no "
      "effect.";
  static const char kPolicyURIMessage[] =
      "The 'policy-uri' directive has been removed from the "
      "specification. Please specify a complete policy via "
      "the Content-Security-Policy header.";

  String message =
      "Unrecognized Content-Security-Policy directive '" + name + "'.\n";
  MessageLevel level = kErrorMessageLevel;
  if (EqualIgnoringASCIICase(name, kAllow)) {
    message = kAllowMessage;
  } else if (EqualIgnoringASCIICase(name, kOptions)) {
    message = kOptionsMessage;
  } else if (EqualIgnoringASCIICase(name, kPolicyURI)) {
    message = kPolicyURIMessage;
  } else if (GetDirectiveType(name) != DirectiveType::kUndefined) {
    message = "The Content-Security-Policy directive '" + name +
              "' is implemented behind a flag which is currently disabled.\n";
    level = kInfoMessageLevel;
  }

  LogToConsole(message, level);
}

void ContentSecurityPolicy::ReportDirectiveAsSourceExpression(
    const String& directive_name,
    const String& source_expression) {
  String message = "The Content Security Policy directive '" + directive_name +
                   "' contains '" + source_expression +
                   "' as a source expression. Did you mean '" + directive_name +
                   " ...; " + source_expression + "...' (note the semicolon)?";
  LogToConsole(message);
}

void ContentSecurityPolicy::ReportDuplicateDirective(const String& name) {
  String message =
      "Ignoring duplicate Content-Security-Policy directive '" + name + "'.\n";
  LogToConsole(message);
}

void ContentSecurityPolicy::ReportInvalidPluginTypes(
    const String& plugin_type) {
  String message;
  if (plugin_type.IsNull())
    message =
        "'plugin-types' Content Security Policy directive is empty; all "
        "plugins will be blocked.\n";
  else if (plugin_type == "'none'")
    message =
        "Invalid plugin type in 'plugin-types' Content Security Policy "
        "directive: '" +
        plugin_type +
        "'. Did you mean to set the object-src directive to 'none'?\n";
  else
    message =
        "Invalid plugin type in 'plugin-types' Content Security Policy "
        "directive: '" +
        plugin_type + "'.\n";
  LogToConsole(message);
}

void ContentSecurityPolicy::ReportInvalidSandboxFlags(
    const String& invalid_flags) {
  LogToConsole(
      "Error while parsing the 'sandbox' Content Security Policy directive: " +
      invalid_flags);
}

void ContentSecurityPolicy::ReportInvalidRequireSRIForTokens(
    const String& invalid_tokens) {
  LogToConsole(
      "Error while parsing the 'require-sri-for' Content Security Policy "
      "directive: " +
      invalid_tokens);
}

void ContentSecurityPolicy::ReportInvalidDirectiveValueCharacter(
    const String& directive_name,
    const String& value) {
  String message = "The value for Content Security Policy directive '" +
                   directive_name + "' contains an invalid character: '" +
                   value +
                   "'. Non-whitespace characters outside ASCII 0x21-0x7E must "
                   "be percent-encoded, as described in RFC 3986, section 2.1: "
                   "http://tools.ietf.org/html/rfc3986#section-2.1.";
  LogToConsole(message);
}

void ContentSecurityPolicy::ReportInvalidPathCharacter(
    const String& directive_name,
    const String& value,
    const char invalid_char) {
  DCHECK(invalid_char == '#' || invalid_char == '?');

  String ignoring =
      "The fragment identifier, including the '#', will be ignored.";
  if (invalid_char == '?')
    ignoring = "The query component, including the '?', will be ignored.";
  String message = "The source list for Content Security Policy directive '" +
                   directive_name +
                   "' contains a source with an invalid path: '" + value +
                   "'. " + ignoring;
  LogToConsole(message);
}

void ContentSecurityPolicy::ReportInvalidSourceExpression(
    const String& directive_name,
    const String& source) {
  String message = "The source list for Content Security Policy directive '" +
                   directive_name + "' contains an invalid source: '" + source +
                   "'. It will be ignored.";
  if (EqualIgnoringASCIICase(source, "'none'"))
    message = message +
              " Note that 'none' has no effect unless it is the only "
              "expression in the source list.";
  LogToConsole(message);
}

void ContentSecurityPolicy::ReportMissingReportURI(const String& policy) {
  LogToConsole("The Content Security Policy '" + policy +
               "' was delivered in report-only mode, but does not specify a "
               "'report-uri'; the policy will have no effect. Please either "
               "add a 'report-uri' directive, or deliver the policy via the "
               "'Content-Security-Policy' header.");
}

void ContentSecurityPolicy::LogToConsole(const String& message,
                                         MessageLevel level) {
  LogToConsole(ConsoleMessage::Create(kSecurityMessageSource, level, message));
}

void ContentSecurityPolicy::LogToConsole(ConsoleMessage* console_message,
                                         LocalFrame* frame) {
  if (frame)
    frame->GetDocument()->AddConsoleMessage(console_message);
  else if (execution_context_)
    execution_context_->AddConsoleMessage(console_message);
  else
    console_messages_.push_back(console_message);
}

void ContentSecurityPolicy::ReportBlockedScriptExecutionToInspector(
    const String& directive_text) const {
  probe::scriptExecutionBlockedByCSP(execution_context_, directive_text);
}

bool ContentSecurityPolicy::ExperimentalFeaturesEnabled() const {
  return RuntimeEnabledFeatures::
      experimentalContentSecurityPolicyFeaturesEnabled();
}

bool ContentSecurityPolicy::ShouldSendCSPHeader(Resource::Type type) const {
  for (const auto& policy : policies_) {
    if (policy->ShouldSendCSPHeader(type))
      return true;
  }
  return false;
}

bool ContentSecurityPolicy::UrlMatchesSelf(const KURL& url) const {
  return self_source_->Matches(url, RedirectStatus::kNoRedirect);
}

bool ContentSecurityPolicy::ProtocolEqualsSelf(const String& protocol) const {
  return EqualIgnoringASCIICase(protocol, self_protocol_);
}

const String& ContentSecurityPolicy::GetSelfProtocol() const {
  return self_protocol_;
}

bool ContentSecurityPolicy::ShouldBypassMainWorld(
    const ExecutionContext* context) {
  if (context && context->IsDocument()) {
    const Document* document = ToDocument(context);
    if (document->GetFrame()) {
      return document->GetFrame()
          ->GetScriptController()
          .ShouldBypassMainWorldCSP();
    }
  }
  return false;
}

bool ContentSecurityPolicy::ShouldSendViolationReport(
    const String& report) const {
  // Collisions have no security impact, so we can save space by storing only
  // the string's hash rather than the whole report.
  return !violation_reports_sent_.Contains(report.Impl()->GetHash());
}

void ContentSecurityPolicy::DidSendViolationReport(const String& report) {
  violation_reports_sent_.insert(report.Impl()->GetHash());
}

const char* ContentSecurityPolicy::GetDirectiveName(const DirectiveType& type) {
  switch (type) {
    case DirectiveType::kBaseURI:
      return "base-uri";
    case DirectiveType::kBlockAllMixedContent:
      return "block-all-mixed-content";
    case DirectiveType::kChildSrc:
      return "child-src";
    case DirectiveType::kConnectSrc:
      return "connect-src";
    case DirectiveType::kDefaultSrc:
      return "default-src";
    case DirectiveType::kFrameAncestors:
      return "frame-ancestors";
    case DirectiveType::kFrameSrc:
      return "frame-src";
    case DirectiveType::kFontSrc:
      return "font-src";
    case DirectiveType::kFormAction:
      return "form-action";
    case DirectiveType::kImgSrc:
      return "img-src";
    case DirectiveType::kManifestSrc:
      return "manifest-src";
    case DirectiveType::kMediaSrc:
      return "media-src";
    case DirectiveType::kObjectSrc:
      return "object-src";
    case DirectiveType::kPluginTypes:
      return "plugin-types";
    case DirectiveType::kReportURI:
      return "report-uri";
    case DirectiveType::kRequireSRIFor:
      return "require-sri-for";
    case DirectiveType::kSandbox:
      return "sandbox";
    case DirectiveType::kScriptSrc:
      return "script-src";
    case DirectiveType::kStyleSrc:
      return "style-src";
    case DirectiveType::kTreatAsPublicAddress:
      return "treat-as-public-address";
    case DirectiveType::kUpgradeInsecureRequests:
      return "upgrade-insecure-requests";
    case DirectiveType::kWorkerSrc:
      return "worker-src";
    case DirectiveType::kUndefined:
      NOTREACHED();
      return "";
  }

  NOTREACHED();
  return "";
}

ContentSecurityPolicy::DirectiveType ContentSecurityPolicy::GetDirectiveType(
    const String& name) {
  if (name == "base-uri")
    return DirectiveType::kBaseURI;
  if (name == "block-all-mixed-content")
    return DirectiveType::kBlockAllMixedContent;
  if (name == "child-src")
    return DirectiveType::kChildSrc;
  if (name == "connect-src")
    return DirectiveType::kConnectSrc;
  if (name == "default-src")
    return DirectiveType::kDefaultSrc;
  if (name == "frame-ancestors")
    return DirectiveType::kFrameAncestors;
  if (name == "frame-src")
    return DirectiveType::kFrameSrc;
  if (name == "font-src")
    return DirectiveType::kFontSrc;
  if (name == "form-action")
    return DirectiveType::kFormAction;
  if (name == "img-src")
    return DirectiveType::kImgSrc;
  if (name == "manifest-src")
    return DirectiveType::kManifestSrc;
  if (name == "media-src")
    return DirectiveType::kMediaSrc;
  if (name == "object-src")
    return DirectiveType::kObjectSrc;
  if (name == "plugin-types")
    return DirectiveType::kPluginTypes;
  if (name == "report-uri")
    return DirectiveType::kReportURI;
  if (name == "require-sri-for")
    return DirectiveType::kRequireSRIFor;
  if (name == "sandbox")
    return DirectiveType::kSandbox;
  if (name == "script-src")
    return DirectiveType::kScriptSrc;
  if (name == "style-src")
    return DirectiveType::kStyleSrc;
  if (name == "treat-as-public-address")
    return DirectiveType::kTreatAsPublicAddress;
  if (name == "upgrade-insecure-requests")
    return DirectiveType::kUpgradeInsecureRequests;
  if (name == "worker-src")
    return DirectiveType::kWorkerSrc;

  return DirectiveType::kUndefined;
}

bool ContentSecurityPolicy::Subsumes(const ContentSecurityPolicy& other) const {
  if (!policies_.size() || !other.policies_.size())
    return !policies_.size();
  // Required-CSP must specify only one policy.
  if (policies_.size() != 1)
    return false;

  CSPDirectiveListVector other_vector;
  for (const auto& policy : other.policies_) {
    if (!policy->IsReportOnly())
      other_vector.push_back(policy);
  }

  return policies_[0]->Subsumes(other_vector);
}

bool ContentSecurityPolicy::ShouldBypassContentSecurityPolicy(
    const KURL& url,
    SchemeRegistry::PolicyAreas area) {
  if (SecurityOrigin::ShouldUseInnerURL(url)) {
    return SchemeRegistry::SchemeShouldBypassContentSecurityPolicy(
        SecurityOrigin::ExtractInnerURL(url).Protocol(), area);
  } else {
    return SchemeRegistry::SchemeShouldBypassContentSecurityPolicy(
        url.Protocol(), area);
  }
}

}  // namespace blink
