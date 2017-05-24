/*
 * Copyright (C) 1999 Lars Knoll (knoll@kde.org)
 *           (C) 1999 Antti Koivisto (koivisto@kde.org)
 *           (C) 2001 Dirk Mueller (mueller@kde.org)
 * Copyright (C) 2003, 2004, 2005, 2006, 2007, 2008 Apple Inc. All rights
 * reserved.
 * Copyright (C) 2008 Nikolas Zimmermann <zimmermann@kde.org>
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

#include "core/dom/ScriptLoader.h"

#include "bindings/core/v8/ScriptController.h"
#include "bindings/core/v8/ScriptSourceCode.h"
#include "bindings/core/v8/V8BindingForCore.h"
#include "core/HTMLNames.h"
#include "core/SVGNames.h"
#include "core/dom/ClassicPendingScript.h"
#include "core/dom/ClassicScript.h"
#include "core/dom/Document.h"
#include "core/dom/DocumentParserTiming.h"
#include "core/dom/DocumentWriteIntervention.h"
#include "core/dom/IgnoreDestructiveWriteCountIncrementer.h"
#include "core/dom/Modulator.h"
#include "core/dom/ModulePendingScript.h"
#include "core/dom/Script.h"
#include "core/dom/ScriptElementBase.h"
#include "core/dom/ScriptRunner.h"
#include "core/dom/ScriptableDocumentParser.h"
#include "core/dom/Text.h"
#include "core/events/Event.h"
#include "core/frame/LocalFrame.h"
#include "core/frame/SubresourceIntegrity.h"
#include "core/frame/csp/ContentSecurityPolicy.h"
#include "core/html/imports/HTMLImport.h"
#include "core/html/parser/HTMLParserIdioms.h"
#include "core/inspector/ConsoleMessage.h"
#include "core/loader/modulescript/ModuleScriptFetchRequest.h"
#include "core/loader/resource/ScriptResource.h"
#include "platform/WebFrameScheduler.h"
#include "platform/loader/fetch/AccessControlStatus.h"
#include "platform/loader/fetch/FetchParameters.h"
#include "platform/loader/fetch/ResourceFetcher.h"
#include "platform/network/mime/MIMETypeRegistry.h"
#include "platform/weborigin/SecurityOrigin.h"
#include "platform/wtf/StdLibExtras.h"
#include "platform/wtf/text/StringBuilder.h"
#include "platform/wtf/text/StringHash.h"
#include "public/platform/WebCachePolicy.h"

namespace blink {

ScriptLoader::ScriptLoader(ScriptElementBase* element,
                           bool parser_inserted,
                           bool already_started,
                           bool created_during_document_write)
    : element_(element),
      start_line_number_(WTF::OrdinalNumber::BeforeFirst()),
      have_fired_load_(false),
      will_be_parser_executed_(false),
      will_execute_when_document_finished_parsing_(false),
      created_during_document_write_(created_during_document_write),
      async_exec_type_(ScriptRunner::kNone),
      document_write_intervention_(
          DocumentWriteIntervention::kDocumentWriteInterventionNone) {
  // https://html.spec.whatwg.org/#already-started
  // "The cloning steps for script elements must set the "already started"
  //  flag on the copy if it is set on the element being cloned."
  // TODO(hiroshige): Cloning is implemented together with
  // {HTML,SVG}ScriptElement::cloneElementWithoutAttributesAndChildren().
  // Clean up these later.
  if (already_started)
    already_started_ = true;

  if (parser_inserted) {
    // https://html.spec.whatwg.org/#parser-inserted
    // "It is set by the HTML parser and the XML parser
    //  on script elements they insert"
    parser_inserted_ = true;

    // https://html.spec.whatwg.org/#non-blocking
    // "It is unset by the HTML parser and the XML parser
    //  on script elements they insert."
    non_blocking_ = false;
  }

  if (parser_inserted &&
      element_->GetDocument().GetScriptableDocumentParser() &&
      !element_->GetDocument().IsInDocumentWrite()) {
    start_line_number_ =
        element_->GetDocument().GetScriptableDocumentParser()->LineNumber();
  }
}

ScriptLoader::~ScriptLoader() {}

DEFINE_TRACE(ScriptLoader) {
  visitor->Trace(element_);
  visitor->Trace(resource_);
  visitor->Trace(pending_script_);
  visitor->Trace(module_tree_client_);
  PendingScriptClient::Trace(visitor);
}

void ScriptLoader::SetFetchDocWrittenScriptDeferIdle() {
  DCHECK(!created_during_document_write_);
  document_write_intervention_ =
      DocumentWriteIntervention::kFetchDocWrittenScriptDeferIdle;
}

void ScriptLoader::DidNotifySubtreeInsertionsToDocument() {
  if (!parser_inserted_)
    PrepareScript();  // FIXME: Provide a real starting line number here.
}

void ScriptLoader::ChildrenChanged() {
  if (!parser_inserted_ && element_->IsConnected())
    PrepareScript();  // FIXME: Provide a real starting line number here.
}

void ScriptLoader::HandleSourceAttribute(const String& source_url) {
  if (IgnoresLoadRequest() || source_url.IsEmpty())
    return;

  PrepareScript();  // FIXME: Provide a real starting line number here.
}

void ScriptLoader::HandleAsyncAttribute() {
  // https://html.spec.whatwg.org/#non-blocking
  // "In addition, whenever a script element whose "non-blocking" flag is set
  //  has an async content attribute added, the element's "non-blocking" flag
  //  must be unset."
  non_blocking_ = false;
}

void ScriptLoader::DetachPendingScript() {
  if (!pending_script_)
    return;
  pending_script_->Dispose();
  pending_script_ = nullptr;
}

void ScriptLoader::DispatchErrorEvent() {
  element_->DispatchErrorEvent();
}

void ScriptLoader::DispatchLoadEvent() {
  element_->DispatchLoadEvent();
  SetHaveFiredLoadEvent(true);
}

namespace {

bool IsValidClassicScriptTypeAndLanguage(
    const String& type,
    const String& language,
    ScriptLoader::LegacyTypeSupport support_legacy_types) {
  // FIXME: isLegacySupportedJavaScriptLanguage() is not valid HTML5. It is used
  // here to maintain backwards compatibility with existing layout tests. The
  // specific violations are:
  // - Allowing type=javascript. type= should only support MIME types, such as
  //   text/javascript.
  // - Allowing a different set of languages for language= and type=. language=
  //   supports Javascript 1.1 and 1.4-1.6, but type= does not.
  if (type.IsEmpty()) {
    return language.IsEmpty() ||  // assume text/javascript.
           MIMETypeRegistry::IsSupportedJavaScriptMIMEType("text/" +
                                                           language) ||
           MIMETypeRegistry::IsLegacySupportedJavaScriptLanguage(language);
  } else if (MIMETypeRegistry::IsSupportedJavaScriptMIMEType(
                 type.StripWhiteSpace()) ||
             (support_legacy_types ==
                  ScriptLoader::kAllowLegacyTypeInTypeAttribute &&
              MIMETypeRegistry::IsLegacySupportedJavaScriptLanguage(type))) {
    return true;
  }

  return false;
}

}  // namespace

// Step 6 of https://html.spec.whatwg.org/#prepare-a-script
bool ScriptLoader::IsValidScriptTypeAndLanguage(
    const String& type,
    const String& language,
    LegacyTypeSupport support_legacy_types,
    ScriptType& out_script_type) {
  if (IsValidClassicScriptTypeAndLanguage(type, language,
                                          support_legacy_types)) {
    // - "If the script block's type string is an ASCII case-insensitive match
    //    for any JavaScript MIME type, the script's type is "classic"."
    // TODO(hiroshige): Annotate and/or cleanup this step.
    out_script_type = ScriptType::kClassic;
    return true;
  }

  if (RuntimeEnabledFeatures::moduleScriptsEnabled() && type == "module") {
    // - "If the script block's type string is an ASCII case-insensitive match
    //    for the string "module", the script's type is "module"."
    out_script_type = ScriptType::kModule;
    return true;
  }

  // - "If neither of the above conditions are true, then abort these steps
  //    at this point. No script is executed."
  return false;
}

bool ScriptLoader::BlockForNoModule(ScriptType script_type, bool nomodule) {
  return nomodule && script_type == ScriptType::kClassic &&
         RuntimeEnabledFeatures::moduleScriptsEnabled();
}

bool ScriptLoader::IsScriptTypeSupported(LegacyTypeSupport support_legacy_types,
                                         ScriptType& out_script_type) const {
  return IsValidScriptTypeAndLanguage(element_->TypeAttributeValue(),
                                      element_->LanguageAttributeValue(),
                                      support_legacy_types, out_script_type);
}

// https://html.spec.whatwg.org/#prepare-a-script
bool ScriptLoader::PrepareScript(const TextPosition& script_start_position,
                                 LegacyTypeSupport support_legacy_types) {
  // 1. "If the script element is marked as having "already started", then
  //     abort these steps at this point. The script is not executed."
  if (already_started_)
    return false;

  // 2. "If the element has its "parser-inserted" flag set, then
  //     set was-parser-inserted to true and unset the element's
  //     "parser-inserted" flag.
  //     Otherwise, set was-parser-inserted to false."
  bool was_parser_inserted;
  if (parser_inserted_) {
    was_parser_inserted = true;
    parser_inserted_ = false;
  } else {
    was_parser_inserted = false;
  }

  // 3. "If was-parser-inserted is true and the element does not have an
  //     async attribute, then set the element's "non-blocking" flag to true."
  if (was_parser_inserted && !element_->AsyncAttributeValue())
    non_blocking_ = true;

  // 4. "If the element has no src attribute, and its child nodes, if any,
  //     consist only of comment nodes and empty Text nodes,
  //     then abort these steps at this point. The script is not executed."
  // FIXME: HTML5 spec says we should check that all children are either
  // comments or empty text nodes.
  if (!element_->HasSourceAttribute() && !element_->HasChildren())
    return false;

  // 5. "If the element is not connected, then abort these steps.
  //     The script is not executed."
  if (!element_->IsConnected())
    return false;

  // 6. "Determine the script's type as follows:"
  // |script_type_| is set here.
  if (!IsScriptTypeSupported(support_legacy_types, script_type_))
    return false;

  // 7. "If was-parser-inserted is true,
  //     then flag the element as "parser-inserted" again,
  //     and set the element's "non-blocking" flag to false."
  if (was_parser_inserted) {
    parser_inserted_ = true;
    non_blocking_ = false;
  }

  // 8. "Set the element's "already started" flag."
  already_started_ = true;

  // 9. "If the element is flagged as "parser-inserted", but the element's
  // node document is not the Document of the parser that created the element,
  // then abort these steps."
  // FIXME: If script is parser inserted, verify it's still in the original
  // document.
  Document& element_document = element_->GetDocument();
  Document* context_document = element_document.ContextDocument();
  if (!element_document.ExecutingFrame())
    return false;
  if (!context_document || !context_document->ExecutingFrame())
    return false;

  // 10. "If scripting is disabled for the script element, then abort these
  //      steps at this point. The script is not executed."
  if (!context_document->CanExecuteScripts(kAboutToExecuteScript))
    return false;

  // 11. "If the script element has a nomodule content attribute
  //      and the script's type is "classic", then abort these steps.
  //      The script is not executed."
  if (BlockForNoModule(script_type_, element_->NomoduleAttributeValue()))
    return false;

  // 13.
  if (!IsScriptForEventSupported())
    return false;

  // 14. is handled below.

  // 15. "Let CORS setting be the current state of the element's
  //      crossorigin content attribute."
  CrossOriginAttributeValue cross_origin =
      GetCrossOriginAttributeValue(element_->CrossOriginAttributeValue());

  // 16. "Let module script credentials mode be determined by switching
  //      on CORS setting:"
  WebURLRequest::FetchCredentialsMode credentials_mode =
      WebURLRequest::kFetchCredentialsModeOmit;
  switch (cross_origin) {
    case kCrossOriginAttributeNotSet:
      credentials_mode = WebURLRequest::kFetchCredentialsModeOmit;
      break;
    case kCrossOriginAttributeAnonymous:
      credentials_mode = WebURLRequest::kFetchCredentialsModeSameOrigin;
      break;
    case kCrossOriginAttributeUseCredentials:
      credentials_mode = WebURLRequest::kFetchCredentialsModeInclude;
      break;
  }

  // 17. "If the script element has a nonce attribute,
  //      then let cryptographic nonce be that attribute's value.
  //      Otherwise, let cryptographic nonce be the empty string."
  String nonce = element_->GetNonceForElement();

  // 18. is handled below.

  // 19. "Let parser state be "parser-inserted"
  //      if the script element has been flagged as "parser-inserted",
  //      and "not parser-inserted" otherwise."
  ParserDisposition parser_state =
      IsParserInserted() ? kParserInserted : kNotParserInserted;

  // 21. "If the element has a src content attribute, run these substeps:"
  if (element_->HasSourceAttribute()) {
    // 21.1. Let src be the value of the element's src attribute.
    String src =
        StripLeadingAndTrailingHTMLSpaces(element_->SourceAttributeValue());

    // 21.2. "If src is the empty string, queue a task to
    //        fire an event named error at the element, and abort these steps."
    if (src.IsEmpty()) {
      // TODO(hiroshige): Make this asynchronous. Currently we fire the error
      // event synchronously to keep the existing behavior.
      DispatchErrorEvent();
      return false;
    }

    // 21.3. "Set the element's from an external file flag."
    is_external_script_ = true;

    // 21.4. "Parse src relative to the element's node document."
    KURL url = element_document.CompleteURL(src);

    // 21.5. "If the previous step failed, queue a task to
    //        fire an event named error at the element, and abort these steps."
    if (!url.IsValid()) {
      // TODO(hiroshige): Make this asynchronous. Currently we fire the error
      // event synchronously to keep the existing behavior.
      DispatchErrorEvent();
      return false;
    }

    DCHECK(!resource_);
    DCHECK(!module_tree_client_);

    // 21.6. "Switch on the script's type:"
    if (GetScriptType() == ScriptType::kClassic) {
      // - "classic":

      // 14. "If the script element has a charset attribute,
      //      then let encoding be the result of
      //      getting an encoding from the value of the charset attribute."
      //     "If the script element does not have a charset attribute,
      //      or if getting an encoding failed, let encoding
      //      be the same as the encoding of the script element's node
      //      document."
      // TODO(hiroshige): Should we handle failure in getting an encoding?
      String encoding;
      if (!element_->CharsetAttributeValue().IsEmpty())
        encoding = element_->CharsetAttributeValue();
      else
        encoding = element_document.characterSet();

      // 18. "If the script element has an integrity attribute,
      //      then let integrity metadata be that attribute's value.
      //      Otherwise, let integrity metadata be the empty string."
      String integrity_attr = element_->IntegrityAttributeValue();
      IntegrityMetadataSet integrity_metadata;
      if (!integrity_attr.IsEmpty()) {
        SubresourceIntegrity::ParseIntegrityAttribute(
            integrity_attr, integrity_metadata, &element_document);
      }

      if (!FetchClassicScript(url, element_document.Fetcher(), nonce,
                              integrity_metadata, parser_state, cross_origin,
                              element_document.GetSecurityOrigin(), encoding)) {
        // TODO(hiroshige): Make this asynchronous. Currently we fire the error
        // event synchronously to keep the existing behavior.
        DispatchErrorEvent();
        return false;
      }

      DCHECK(resource_);
      DCHECK(!module_tree_client_);
    } else {
      // - "module":

      // Steps 14 and 18 are skipped because they are not used in module
      // scripts.

      DCHECK(RuntimeEnabledFeatures::moduleScriptsEnabled());
      Modulator* modulator = Modulator::From(
          ToScriptStateForMainWorld(element_document.GetFrame()));
      FetchModuleScriptTree(url, modulator, nonce, parser_state,
                            credentials_mode);

      DCHECK(!resource_);
      DCHECK(module_tree_client_);
    }

    // "When the chosen algorithm asynchronously completes, set
    //  the script's script to the result. At that time, the script is ready."
    // When the script is ready, PendingScriptClient::pendingScriptFinished()
    // is used as the notification, and the action to take when
    // the script is ready is specified later, in
    // - ScriptLoader::PrepareScript(), or
    // - HTMLParserScriptRunner,
    // depending on the conditions in Step 23 of "prepare a script".
  }

  // 22. "If the element does not have a src content attribute,
  //      run these substeps:"
  if (!element_->HasSourceAttribute()) {
    // 22.1. "Let source text be the value of the text IDL attribute."
    // This step is done later:
    // - in ScriptLoader::pendingScript() (Step 23, 6th Clause),
    //   as Element::textFromChildren() in ScriptLoader::scriptContent(),
    // - in HTMLParserScriptRunner::processScriptElementInternal()
    //   (Duplicated code of Step 23, 6th Clause),
    //   as Element::textContent(),
    // - in XMLDocumentParser::endElementNs() (Step 23, 5th Clause),
    //   as Element::textFromChildren() in ScriptLoader::scriptContent(),
    // - PendingScript::getSource() (Indirectly used via
    //   HTMLParserScriptRunner::processScriptElementInternal(),
    //   Step 23, 5th Clause),
    //   as Element::textContent().
    // TODO(hiroshige): Make them merged or consistent.

    // 22.2. "Switch on the script's type:"
    switch (GetScriptType()) {
      // - "classic":
      case ScriptType::kClassic:
        // TODO(hiroshige): Clarify how Step 22.2 is implemented for "classic".
        break;

      // - "module":
      case ScriptType::kModule: {
        // 1. "Let base URL be the script element's node document's document
        //     base URL."
        KURL base_url = element_document.BaseURL();

        TextPosition position = element_document.IsInDocumentWrite()
                                    ? TextPosition()
                                    : script_start_position;
        // 2. "Let script be the result of creating a module script using
        //     source text, settings, base URL, cryptographic nonce,
        //     parser state, and module script credentials mode."
        Modulator* modulator = Modulator::From(
            ToScriptStateForMainWorld(element_document.GetFrame()));
        ModuleScript* module_script = ModuleScript::Create(
            ScriptContent(), modulator, base_url, nonce, parser_state,
            credentials_mode, kSharableCrossOrigin, position);

        // 3. "If this returns null, set the script's script to null and abort
        //     these substeps; the script is ready."
        if (!module_script)
          return false;

        // 4. "Fetch the descendants of script (using an empty ancestor list).
        //     When this asynchronously completes, set the script's script to
        //     the result. At that time, the script is ready."
        DCHECK(!module_tree_client_);
        module_tree_client_ = ModulePendingScriptTreeClient::Create();
        modulator->FetchDescendantsForInlineScript(module_script,
                                                   module_tree_client_);
        break;
      }
    }
  }

  // [Intervention]
  // Since the asynchronous, low priority fetch for doc.written blocked
  // script is not for execution, return early from here. Watch for its
  // completion to be able to remove it from the memory cache.
  if (GetScriptType() == ScriptType::kClassic &&
      document_write_intervention_ ==
          DocumentWriteIntervention::kFetchDocWrittenScriptDeferIdle) {
    pending_script_ = CreatePendingScript();
    pending_script_->WatchForLoad(this);
    return true;
  }

  // 23. "Then, follow the first of the following options that describes the
  //      situation:"

  // Three flags are used to instruct the caller of prepareScript() to execute
  // a part of Step 23, when |m_willBeParserExecuted| is true:
  // - |m_willBeParserExecuted|
  // - |m_willExecuteWhenDocumentFinishedParsing|
  // - |m_readyToBeParserExecuted|
  // TODO(hiroshige): Clean up the dependency.

  // 1st Clause:
  // - "If the script's type is "classic", and
  //    the element has a src attribute, and the element has a defer attribute,
  //    and the element has been flagged as "parser-inserted",
  //    and the element does not have an async attribute"
  // - "If the script's type is "module",
  //    and the element has been flagged as "parser-inserted",
  //    and the element does not have an async attribute"
  if ((GetScriptType() == ScriptType::kClassic &&
       element_->HasSourceAttribute() && element_->DeferAttributeValue() &&
       parser_inserted_ && !element_->AsyncAttributeValue()) ||
      (GetScriptType() == ScriptType::kModule && parser_inserted_ &&
       !element_->AsyncAttributeValue())) {
    // This clause is implemented by the caller-side of prepareScript():
    // - HTMLParserScriptRunner::requestDeferredScript(), and
    // - TODO(hiroshige): Investigate XMLDocumentParser::endElementNs()
    will_execute_when_document_finished_parsing_ = true;
    will_be_parser_executed_ = true;

    return true;
  }

  // 2nd Clause:
  // - "If the script's type is "classic",
  //    and the element has a src attribute,
  //    and the element has been flagged as "parser-inserted",
  //    and the element does not have an async attribute"
  // TODO(hiroshige): Check the script's type.
  if (GetScriptType() == ScriptType::kClassic &&
      element_->HasSourceAttribute() && parser_inserted_ &&
      !element_->AsyncAttributeValue()) {
    // This clause is implemented by the caller-side of prepareScript():
    // - HTMLParserScriptRunner::requestParsingBlockingScript()
    // - TODO(hiroshige): Investigate XMLDocumentParser::endElementNs()
    will_be_parser_executed_ = true;

    return true;
  }

  // 3rd Clause:
  // - "If the script's type is "classic",
  //    and the element has a src attribute,
  //    and the element does not have an async attribute,
  //    and the element does not have the "non-blocking" flag set"
  // - "If the script's type is "module",
  //    and the element does not have an async attribute,
  //    and the element does not have the "non-blocking" flag set"
  if ((GetScriptType() == ScriptType::kClassic &&
       element_->HasSourceAttribute() && !element_->AsyncAttributeValue() &&
       !non_blocking_) ||
      (GetScriptType() == ScriptType::kModule &&
       !element_->AsyncAttributeValue() && !non_blocking_)) {
    // "Add the element to the end of the list of scripts that will execute
    // in order as soon as possible associated with the node document of the
    // script element at the time the prepare a script algorithm started."
    pending_script_ = CreatePendingScript();
    async_exec_type_ = ScriptRunner::kInOrder;
    // TODO(hiroshige): Here |contextDocument| is used as "node document"
    // while Step 14 uses |elementDocument| as "node document". Fix this.
    context_document->GetScriptRunner()->QueueScriptForExecution(
        this, async_exec_type_);
    // Note that watchForLoad can immediately call pendingScriptFinished.
    pending_script_->WatchForLoad(this);
    // The part "When the script is ready..." is implemented in
    // ScriptRunner::notifyScriptReady().
    // TODO(hiroshige): Annotate it.

    return true;
  }

  // 4th Clause:
  // - "If the script's type is "classic", and the element has a src attribute"
  // - "If the script's type is "module""
  if ((GetScriptType() == ScriptType::kClassic &&
       element_->HasSourceAttribute()) ||
      GetScriptType() == ScriptType::kModule) {
    // "The element must be added to the set of scripts that will execute
    //  as soon as possible of the node document of the script element at the
    //  time the prepare a script algorithm started."
    pending_script_ = CreatePendingScript();
    async_exec_type_ = ScriptRunner::kAsync;
    pending_script_->StartStreamingIfPossible(&element_->GetDocument(),
                                              ScriptStreamer::kAsync);
    // TODO(hiroshige): Here |contextDocument| is used as "node document"
    // while Step 14 uses |elementDocument| as "node document". Fix this.
    context_document->GetScriptRunner()->QueueScriptForExecution(
        this, async_exec_type_);
    // Note that watchForLoad can immediately call pendingScriptFinished.
    pending_script_->WatchForLoad(this);
    // The part "When the script is ready..." is implemented in
    // ScriptRunner::notifyScriptReady().
    // TODO(hiroshige): Annotate it.

    return true;
  }

  // The following clauses are executed only if the script's type is "classic"
  // and the element doesn't have a src attribute.
  DCHECK_EQ(GetScriptType(), ScriptType::kClassic);
  DCHECK(!is_external_script_);

  // 5th Clause:
  // - "If the element does not have a src attribute,
  //    and the element has been flagged as "parser-inserted",
  //    and either the parser that created the script is an XML parser
  //      or it's an HTML parser whose script nesting level is not greater than
  //      one,
  //    and the Document of the HTML parser or XML parser that created
  //      the script element has a style sheet that is blocking scripts"
  // The last part "... has a style sheet that is blocking scripts"
  // is implemented in Document::isScriptExecutionReady().
  // Part of the condition check is done in
  // HTMLParserScriptRunner::processScriptElementInternal().
  // TODO(hiroshige): Clean up the split condition check.
  if (!element_->HasSourceAttribute() && parser_inserted_ &&
      !element_document.IsScriptExecutionReady()) {
    // The former part of this clause is
    // implemented by the caller-side of prepareScript():
    // - HTMLParserScriptRunner::requestParsingBlockingScript()
    // - TODO(hiroshige): Investigate XMLDocumentParser::endElementNs()
    will_be_parser_executed_ = true;
    // "Set the element's "ready to be parser-executed" flag."
    ready_to_be_parser_executed_ = true;

    return true;
  }

  // 6th Clause:
  // - "Otherwise"
  // "Immediately execute the script block,
  //  even if other scripts are already executing."
  // Note: this block is also duplicated in
  // HTMLParserScriptRunner::processScriptElementInternal().
  // TODO(hiroshige): Merge the duplicated code.

  // Reset line numbering for nested writes.
  TextPosition position = element_document.IsInDocumentWrite()
                              ? TextPosition()
                              : script_start_position;
  KURL script_url = (!element_document.IsInDocumentWrite() && parser_inserted_)
                        ? element_document.Url()
                        : KURL();

  if (!ExecuteScript(ClassicScript::Create(
          ScriptSourceCode(ScriptContent(), script_url, position)))) {
    DispatchErrorEvent();
    return false;
  }

  return true;
}

bool ScriptLoader::FetchClassicScript(
    const KURL& url,
    ResourceFetcher* fetcher,
    const String& nonce,
    const IntegrityMetadataSet& integrity_metadata,
    ParserDisposition parser_state,
    CrossOriginAttributeValue cross_origin,
    SecurityOrigin* security_origin,
    const String& encoding) {
  // https://html.spec.whatwg.org/#prepare-a-script
  // 21.6, "classic":
  // "Fetch a classic script given url, settings, ..."
  ResourceRequest resource_request(url);

  FetchParameters::DeferOption defer = FetchParameters::kNoDefer;
  if (!parser_inserted_ || element_->AsyncAttributeValue() ||
      element_->DeferAttributeValue())
    defer = FetchParameters::kLazyLoad;

  // [Intervention]
  if (document_write_intervention_ ==
      DocumentWriteIntervention::kFetchDocWrittenScriptDeferIdle) {
    resource_request.SetHTTPHeaderField(
        "Intervention",
        "<https://www.chromestatus.com/feature/5718547946799104>");
    defer = FetchParameters::kIdleLoad;
  }

  // [Intervention]
  // For users on slow connections, we want to avoid blocking the parser in
  // the main frame on script loads inserted via document.write, since it can
  // add significant delays before page content is displayed on the screen.
  if (MaybeDisallowFetchForDocWrittenScript(resource_request, defer,
                                            element_->GetDocument())) {
    document_write_intervention_ =
        DocumentWriteIntervention::kDoNotFetchDocWrittenScript;
  }

  FetchParameters params(resource_request, element_->InitiatorName());

  // "... cryptographic nonce, ..."
  params.SetContentSecurityPolicyNonce(nonce);

  // "... integrity metadata, ..."
  params.SetIntegrityMetadata(integrity_metadata);

  // "... parser state, ..."
  params.SetParserDisposition(parser_state);

  // "... CORS setting, ..."
  if (cross_origin != kCrossOriginAttributeNotSet) {
    params.SetCrossOriginAccessControl(security_origin, cross_origin);
  }

  // "... and encoding."
  params.SetCharset(encoding);

  // This DeferOption logic is only for classic scripts, as we always set
  // |kLazyLoad| for module scripts in ModuleScriptLoader.
  params.SetDefer(defer);

  resource_ = ScriptResource::Fetch(params, fetcher);

  if (!resource_)
    return false;

  return true;
}

void ScriptLoader::FetchModuleScriptTree(
    const KURL& url,
    Modulator* modulator,
    const String& nonce,
    ParserDisposition parser_state,
    WebURLRequest::FetchCredentialsMode credentials_mode) {
  // https://html.spec.whatwg.org/#prepare-a-script
  // 21.6, "module":
  //     "Fetch a module script graph given url, settings, "script",
  //      cryptographic nonce, parser state, and
  //      module script credentials mode."
  ModuleScriptFetchRequest module_request(url, nonce, parser_state,
                                          credentials_mode);

  module_tree_client_ = ModulePendingScriptTreeClient::Create();

  modulator->FetchTree(module_request, module_tree_client_);
}

PendingScript* ScriptLoader::CreatePendingScript() {
  switch (GetScriptType()) {
    case ScriptType::kClassic:
      CHECK(resource_);
      return ClassicPendingScript::Create(element_, resource_);
    case ScriptType::kModule:
      CHECK(module_tree_client_);
      return ModulePendingScript::Create(element_, module_tree_client_);
  }
  NOTREACHED();
  return nullptr;
}

bool ScriptLoader::ExecuteScript(const Script* script) {
  double script_exec_start_time = MonotonicallyIncreasingTime();
  bool result = DoExecuteScript(script);

  // NOTE: we do not check m_willBeParserExecuted here, since
  // m_willBeParserExecuted is false for inline scripts, and we want to
  // include inline script execution time as part of parser blocked script
  // execution time.
  if (async_exec_type_ == ScriptRunner::kNone)
    DocumentParserTiming::From(element_->GetDocument())
        .RecordParserBlockedOnScriptExecutionDuration(
            MonotonicallyIncreasingTime() - script_exec_start_time,
            WasCreatedDuringDocumentWrite());
  return result;
}

// https://html.spec.whatwg.org/#execute-the-script-block
// with additional support for HTML imports.
// Note that Steps 2 and 8 must be handled by the caller of doExecuteScript(),
// i.e. load/error events are dispatched by the caller.
// Steps 3--7 are implemented here in doExecuteScript().
// TODO(hiroshige): Move event dispatching code to doExecuteScript().
bool ScriptLoader::DoExecuteScript(const Script* script) {
  DCHECK(already_started_);
  CHECK_EQ(script->GetScriptType(), GetScriptType());

  if (script->IsEmpty())
    return true;

  Document* element_document = &(element_->GetDocument());
  Document* context_document = element_document->ContextDocument();
  if (!context_document)
    return true;

  LocalFrame* frame = context_document->GetFrame();
  if (!frame)
    return true;

  if (!is_external_script_) {
    const ContentSecurityPolicy* csp =
        element_document->GetContentSecurityPolicy();
    bool should_bypass_main_world_csp =
        (frame->GetScriptController().ShouldBypassMainWorldCSP()) ||
        csp->AllowScriptWithHash(script->InlineSourceTextForCSP(),
                                 ContentSecurityPolicy::InlineType::kBlock);

    AtomicString nonce = element_->GetNonceForElement();
    if (!should_bypass_main_world_csp &&
        !element_->AllowInlineScriptForCSP(nonce, start_line_number_,
                                           script->InlineSourceTextForCSP())) {
      return false;
    }
  }

  if (is_external_script_) {
    if (!script->CheckMIMETypeBeforeRunScript(
            context_document, element_->GetDocument().GetSecurityOrigin()))
      return false;
  }

  const bool is_imported_script = context_document != element_document;

  // 3. "If the script is from an external file,
  //     or the script's type is module",
  //     then increment the ignore-destructive-writes counter of the
  //     script element's node document. Let neutralized doc be that Document."
  IgnoreDestructiveWriteCountIncrementer
      ignore_destructive_write_count_incrementer(
          is_external_script_ ||
                  script->GetScriptType() == ScriptType::kModule ||
                  is_imported_script
              ? context_document
              : 0);

  // 4. "Let old script element be the value to which the script element's
  //     node document's currentScript object was most recently set."
  // This is implemented as push/popCurrentScript().

  // 5. "Switch on the script's type:"
  //    - "classic":
  //    1. "If the script element's root is not a shadow root,
  //        then set the script element's node document's currentScript
  //        attribute to the script element. Otherwise, set it to null."
  //    - "module":
  //    1. "Set the script element's node document's currentScript attribute
  //        to null."
  ScriptElementBase* current_script = nullptr;
  if (script->GetScriptType() == ScriptType::kClassic)
    current_script = element_;
  context_document->PushCurrentScript(current_script);

  //    - "classic":
  //    2. "Run the classic script given by the script's script."
  // Note: This is where the script is compiled and actually executed.
  //    - "module":
  //    2. "Run the module script given by the script's script."
  script->RunScript(frame, element_->GetDocument().GetSecurityOrigin());

  // 6. "Set the script element's node document's currentScript attribute
  //     to old script element."
  context_document->PopCurrentScript(current_script);

  return true;

  // 7. "Decrement the ignore-destructive-writes counter of neutralized doc,
  //     if it was incremented in the earlier step."
  // Implemented as the scope out of IgnoreDestructiveWriteCountIncrementer.
}

void ScriptLoader::Execute() {
  DCHECK(!will_be_parser_executed_);
  DCHECK(async_exec_type_ != ScriptRunner::kNone);
  DCHECK(pending_script_->IsExternal());
  bool error_occurred = false;
  Script* script = pending_script_->GetSource(KURL(), error_occurred);
  const bool wasCanceled = pending_script_->WasCanceled();
  DetachPendingScript();
  if (error_occurred) {
    DispatchErrorEvent();
  } else if (!wasCanceled) {
    if (ExecuteScript(script))
      DispatchLoadEvent();
    else
      DispatchErrorEvent();
  }
  resource_ = nullptr;
  module_tree_client_ = nullptr;
}

void ScriptLoader::PendingScriptFinished(PendingScript* pending_script) {
  DCHECK(!will_be_parser_executed_);
  DCHECK_EQ(pending_script_, pending_script);
  DCHECK_EQ(pending_script_->GetScriptType(), GetScriptType());

  // We do not need this script in the memory cache. The primary goals of
  // sending this fetch request are to let the third party server know
  // about the document.write scripts intervention and populate the http
  // cache for subsequent uses.
  if (document_write_intervention_ ==
      DocumentWriteIntervention::kFetchDocWrittenScriptDeferIdle) {
    DCHECK_EQ(pending_script_->GetScriptType(), ScriptType::kClassic);
    pending_script_->RemoveFromMemoryCache();
    pending_script_->StopWatchingForLoad();
    return;
  }

  DCHECK(async_exec_type_ != ScriptRunner::kNone);

  Document* context_document = element_->GetDocument().ContextDocument();
  if (!context_document) {
    DetachPendingScript();
    return;
  }

  if (ErrorOccurred()) {
    context_document->GetScriptRunner()->NotifyScriptLoadError(
        this, async_exec_type_);
    DetachPendingScript();
    DispatchErrorEvent();
    return;
  }
  context_document->GetScriptRunner()->NotifyScriptReady(this,
                                                         async_exec_type_);
  pending_script_->StopWatchingForLoad();
}

bool ScriptLoader::IgnoresLoadRequest() const {
  return already_started_ || is_external_script_ || parser_inserted_ ||
         !element_->IsConnected();
}

// Step 13 of https://html.spec.whatwg.org/#prepare-a-script
bool ScriptLoader::IsScriptForEventSupported() const {
  // 1. "Let for be the value of the for attribute."
  String event_attribute = element_->EventAttributeValue();
  // 2. "Let event be the value of the event attribute."
  String for_attribute = element_->ForAttributeValue();

  // "If the script element has an event attribute and a for attribute, and
  //  the script's type is "classic", then run these substeps:"
  if (GetScriptType() != ScriptType::kClassic || event_attribute.IsNull() ||
      for_attribute.IsNull())
    return true;

  // 3. "Strip leading and trailing ASCII whitespace from event and for."
  for_attribute = for_attribute.StripWhiteSpace();
  // 4. "If for is not an ASCII case-insensitive match for the string
  //     "window",
  //     then abort these steps at this point. The script is not executed."
  if (!DeprecatedEqualIgnoringCase(for_attribute, "window"))
    return false;
  event_attribute = event_attribute.StripWhiteSpace();
  // 5. "If event is not an ASCII case-insensitive match for either the
  //     string "onload" or the string "onload()",
  //     then abort these steps at this point. The script is not executed.
  return DeprecatedEqualIgnoringCase(event_attribute, "onload") ||
         DeprecatedEqualIgnoringCase(event_attribute, "onload()");
}

String ScriptLoader::ScriptContent() const {
  return element_->TextFromChildren();
}

}  // namespace blink
