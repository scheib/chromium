// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/chrome/browser/autofill/autofill_agent.h"

#include <memory>
#include <string>
#include <utility>

#include "base/format_macros.h"
#include "base/guid.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/mac/foundation_util.h"
#include "base/mac/scoped_block.h"
#include "base/memory/ptr_util.h"
#include "base/metrics/field_trial.h"
#include "base/strings/string16.h"
#include "base/strings/sys_string_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "components/autofill/core/browser/autofill_manager.h"
#include "components/autofill/core/browser/autofill_metrics.h"
#include "components/autofill/core/browser/autofill_profile.h"
#include "components/autofill/core/browser/credit_card.h"
#include "components/autofill/core/browser/keyboard_accessory_metrics_logger.h"
#include "components/autofill/core/browser/popup_item_ids.h"
#include "components/autofill/core/common/autofill_constants.h"
#include "components/autofill/core/common/autofill_pref_names.h"
#include "components/autofill/core/common/autofill_util.h"
#include "components/autofill/core/common/form_data.h"
#include "components/autofill/core/common/form_field_data.h"
#include "components/autofill/ios/browser/autofill_driver_ios.h"
#import "components/autofill/ios/browser/form_suggestion.h"
#import "components/autofill/ios/browser/js_autofill_manager.h"
#include "components/prefs/pref_service.h"
#include "ios/chrome/browser/application_context.h"
#include "ios/chrome/browser/browser_state/chrome_browser_state.h"
#include "ios/chrome/browser/pref_names.h"
#include "ios/web/public/url_scheme_util.h"
#import "ios/web/public/web_state/js/crw_js_injection_receiver.h"
#include "ios/web/public/web_state/url_verification_constants.h"
#import "ios/web/public/web_state/web_state.h"
#include "ui/gfx/geometry/rect.h"
#include "url/gurl.h"

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

namespace {

using FormDataVector = std::vector<autofill::FormData>;

// The type of the completion handler block for
// |fetchFormsWithName:minimumRequiredFieldsCount:pageURL:completionHandler|
typedef void (^FetchFormsCompletionHandler)(BOOL, const FormDataVector&);

// Gets the first form and field specified by |fieldName| from |forms|,
// modifying the returned field so that input elements are also handled.
void GetFormAndField(autofill::FormData* form,
                     autofill::FormFieldData* field,
                     const FormDataVector& forms,
                     const std::string& fieldName,
                     const std::string& type) {
  DCHECK_GE(forms.size(), 1U);
  *form = forms[0];
  const base::string16 fieldName16 = base::UTF8ToUTF16(fieldName);
  for (const auto& currentField : form->fields) {
    if (currentField.name == fieldName16) {
      *field = currentField;
      break;
    }
  }
  if (field->SameFieldAs(autofill::FormFieldData()))
    return;

  // Hack to get suggestions from select input elements.
  if (field->form_control_type == "select-one") {
    // Any value set will cause the AutofillManager to filter suggestions (only
    // show suggestions that begin the same as the current value) with the
    // effect that one only suggestion would be returned; the value itself.
    field->value = base::string16();
  }
}

}  // namespace

@interface AutofillAgent ()<CRWWebStateObserver>

// Notifies the autofill manager when forms are detected on a page.
- (void)notifyAutofillManager:(autofill::AutofillManager*)autofillManager
                  ofFormsSeen:(const FormDataVector&)forms;

// Notifies the autofill manager when forms are submitted.
- (void)notifyAutofillManager:(autofill::AutofillManager*)autofillManager
             ofFormsSubmitted:(const FormDataVector&)forms
                userInitiated:(BOOL)userInitiated;

// Invokes the form extraction script and loads the output into the format
// expected by the AutofillManager.
// If |formName| is non-empty, only a form of that name is extracted.
// Only forms with at least |requiredFieldsCount| fields are extracted.
// Calls |completionHandler| with a success BOOL of YES and the form data that
// was extracted.
// Calls |completionHandler| with NO if the forms could not be extracted.
// |completionHandler| cannot be nil.
- (void)fetchFormsWithName:(const base::string16&)formName
    minimumRequiredFieldsCount:(NSUInteger)requiredFieldsCount
                       pageURL:(const GURL&)pageURL
             completionHandler:(FetchFormsCompletionHandler)completionHandler;

// Processes the JSON form data extracted from the page into the format expected
// by AutofillManager and fills it in |formsData|.
// |formsData| cannot be nil.
// Returns a BOOL indicating the success value and the vector of form data.
- (BOOL)getExtractedFormsData:(FormDataVector*)formsData
                     fromJSON:(NSString*)formJSON
                     formName:(const base::string16&)formName
                      pageURL:(const GURL&)pageURL;

// Processes the JSON form data extracted from the page when form activity is
// detected and informs the AutofillManager.
- (void)processFormActivityExtractedData:(const FormDataVector&)forms
                               fieldName:(const std::string&)fieldName
                                    type:(const std::string&)type
                                webState:(web::WebState*)webState;

// Sends a request to AutofillManager to retrieve suggestions for the specified
// form and field.
- (void)queryAutofillWithForms:(const FormDataVector&)forms
                         field:(NSString*)fieldName
                          type:(NSString*)type
                    typedValue:(NSString*)typedValue
                      webState:(web::WebState*)webState
             completionHandler:(SuggestionsAvailableCompletion)completion;

// Rearranges and filters the suggestions to move profile or credit card
// suggestions to the front if the user has selected one recently and remove
// key/value suggestions if the user hasn't started typing.
- (NSArray*)processSuggestions:(NSArray*)suggestions;

@end

@implementation AutofillAgent {
  // Timestamp of the first time forms are seen.
  base::TimeTicks formsSeenTimestamp_;

  // Bridge to observe the web state from Objective-C.
  std::unique_ptr<web::WebStateObserverBridge> _webStateObserverBridge;

  // The browser state for which this agent was created.
  ios::ChromeBrowserState* browserState_;

  // Manager for Autofill JavaScripts.
  JsAutofillManager* jsAutofillManager_;

  // The name of the most recent autocomplete field; tracks the currently-
  // focused form element in order to force filling of the currently selected
  // form element, even if it's non-empty.
  base::string16 pendingAutocompleteField_;
  // The identifier of the most recent suggestion accepted by the user. Only
  // used to reorder future suggestion lists, placing matching suggestions first
  // in the list.
  NSInteger mostRecentSelectedIdentifier_;

  // Suggestions state:
  // The most recent form suggestions.
  NSArray* mostRecentSuggestions_;
  // The completion to inform FormSuggestionController that a user selection
  // has been handled.
  SuggestionHandledCompletion suggestionHandledCompletion_;
  // The completion to inform FormSuggestionController that suggestions are
  // available for a given form and field.
  SuggestionsAvailableCompletion suggestionsAvailableCompletion_;
  // The text entered by the user into the active field.
  NSString* typedValue_;
  // Popup delegate for the most recent suggestions.
  // The reference is weak because a weak pointer is sent to our
  // AutofillManagerDelegate.
  base::WeakPtr<autofill::AutofillPopupDelegate> popupDelegate_;
}

@synthesize browserState = browserState_;

- (instancetype)initWithBrowserState:(ios::ChromeBrowserState*)browserState
                            webState:(web::WebState*)webState {
  DCHECK(browserState);
  DCHECK(webState);
  self = [super init];
  if (self) {
    browserState_ = browserState;
    _webStateObserverBridge.reset(
        new web::WebStateObserverBridge(webState, self));
    jsAutofillManager_ = base::mac::ObjCCastStrict<JsAutofillManager>(
        [webState->GetJSInjectionReceiver()
            instanceOfClass:[JsAutofillManager class]]);
  }
  return self;
}

- (instancetype)init {
  NOTREACHED();
  return nil;
}

- (void)detachFromWebState {
  [[NSNotificationCenter defaultCenter] removeObserver:self];
  _webStateObserverBridge.reset();
}

#pragma mark -
#pragma mark Private

// Returns the autofill manager associated with a web::WebState instance.
// Returns nullptr if there is no autofill manager associated anymore, this can
// happen when |close| has been called on the |webState|. Also returns nullptr
// if detachFromWebState has been called.
- (autofill::AutofillManager*)autofillManagerFromWebState:
    (web::WebState*)webState {
  if (!webState || !_webStateObserverBridge)
    return nullptr;
  return autofill::AutofillDriverIOS::FromWebState(webState)
      ->autofill_manager();
}

// Extracts a single form field from the JSON dictionary into a FormFieldData
// object.
// Returns NO if the field could not be extracted.
- (BOOL)extractFormField:(const base::DictionaryValue&)field
             asFieldData:(autofill::FormFieldData*)fieldData {
  if (!field.GetString("name", &fieldData->name) ||
      !field.GetString("form_control_type", &fieldData->form_control_type)) {
    return NO;
  }

  // Optional fields.
  field.GetString("label", &fieldData->label);
  field.GetString("value", &fieldData->value);
  field.GetString("autocomplete_attribute", &fieldData->autocomplete_attribute);

  int maxLength;
  if (field.GetInteger("max_length", &maxLength))
    fieldData->max_length = maxLength;

  field.GetBoolean("is_autofilled", &fieldData->is_autofilled);

  // TODO(crbug.com/427614): Extract |is_checked|.
  bool isCheckable = false;
  field.GetBoolean("is_checkable", &isCheckable);
  autofill::SetCheckStatus(fieldData, isCheckable, false);

  field.GetBoolean("is_focusable", &fieldData->is_focusable);
  field.GetBoolean("should_autocomplete", &fieldData->should_autocomplete);

  // ROLE_ATTRIBUTE_OTHER is the default value. The only other value as of this
  // writing is ROLE_ATTRIBUTE_PRESENTATION.
  int role;
  if (field.GetInteger("role", &role) &&
      role == autofill::AutofillField::ROLE_ATTRIBUTE_PRESENTATION) {
    fieldData->role = autofill::AutofillField::ROLE_ATTRIBUTE_PRESENTATION;
  }

  // TODO(crbug.com/427614): Extract |text_direction|.

  // Load option values where present.
  const base::ListValue* optionValues;
  if (field.GetList("option_values", &optionValues)) {
    for (const auto& optionValue : *optionValues) {
      base::string16 value;
      if (optionValue.GetAsString(&value))
        fieldData->option_values.push_back(value);
    }
  }

  // Load option contents where present.
  const base::ListValue* optionContents;
  if (field.GetList("option_contents", &optionContents)) {
    for (const auto& optionContent : *optionContents) {
      base::string16 content;
      if (optionContent.GetAsString(&content))
        fieldData->option_contents.push_back(content);
    }
  }

  if (fieldData->option_values.size() != fieldData->option_contents.size())
    return NO;  // Option values and contents lists should match 1-1.

  return YES;
}

- (void)notifyAutofillManager:(autofill::AutofillManager*)autofillManager
                  ofFormsSeen:(const FormDataVector&)forms {
  DCHECK(autofillManager);
  DCHECK(!forms.empty());
  autofillManager->Reset();
  autofillManager->OnFormsSeen(forms, formsSeenTimestamp_);
}

- (void)notifyAutofillManager:(autofill::AutofillManager*)autofillManager
             ofFormsSubmitted:(const FormDataVector&)forms
                userInitiated:(BOOL)userInitiated {
  DCHECK(autofillManager);
  // Exactly one form should be extracted.
  DCHECK_EQ(1U, forms.size());
  autofill::FormData form = forms[0];

  // iOS doesn't get a separate "will submit form" notification so call
  // OnWillSubmitForm() here.
  autofillManager->OnWillSubmitForm(form, base::TimeTicks::Now());
  autofillManager->OnFormSubmitted(form);
  autofill::KeyboardAccessoryMetricsLogger::OnFormSubmitted();
}

- (void)fetchFormsWithName:(const base::string16&)formName
    minimumRequiredFieldsCount:(NSUInteger)requiredFieldsCount
                       pageURL:(const GURL&)pageURL
             completionHandler:(FetchFormsCompletionHandler)completionHandler {
  DCHECK(completionHandler);

  // TODO(crbug.com/418827): Early-inject the script and replace the following
  // if statement with a DCHECK if a race condition between the script injection
  // and the call to the this function continues to happen.
  // Return if the script has not been injected.
  if (![jsAutofillManager_ hasBeenInjected]) {
    dispatch_async(dispatch_get_main_queue(), ^{
      completionHandler(NO, FormDataVector());
    });
    return;
  }

  // Necessary so the values can be used inside a block.
  base::string16 formNameCopy = formName;
  GURL pageURLCopy = pageURL;
  __weak AutofillAgent* weakSelf = self;
  [jsAutofillManager_
      fetchFormsWithMinimumRequiredFieldsCount:requiredFieldsCount
                             completionHandler:^(NSString* formJSON) {
                               std::vector<autofill::FormData> formData;
                               BOOL success =
                                   [weakSelf getExtractedFormsData:&formData
                                                          fromJSON:formJSON
                                                          formName:formNameCopy
                                                           pageURL:pageURLCopy];
                               completionHandler(success, formData);
                             }];
}

- (BOOL)getExtractedFormsData:(FormDataVector*)formsData
                     fromJSON:(NSString*)formJSON
                     formName:(const base::string16&)formName
                      pageURL:(const GURL&)pageURL {
  DCHECK(formsData);
  // Convert JSON string to JSON object |dataJson|.
  int errorCode = 0;
  std::string errorMessage;
  std::unique_ptr<base::Value> dataJson(base::JSONReader::ReadAndReturnError(
      base::SysNSStringToUTF8(formJSON), base::JSON_PARSE_RFC, &errorCode,
      &errorMessage));
  if (errorCode) {
    LOG(WARNING) << "JSON parse error in form extraction: "
                 << errorMessage.c_str();
    return NO;
  }

  // Returned data should be a dictionary.
  const base::DictionaryValue* data;
  if (!dataJson->GetAsDictionary(&data))
    return NO;

  // Get the list of forms.
  const base::ListValue* formsList;
  if (!data->GetList("forms", &formsList))
    return NO;

  // Iterate through all the extracted forms and copy the data from JSON into
  // AutofillManager structures.
  for (const auto& formDict : *formsList) {
    // Each form list entry should be a JSON dictionary.
    const base::DictionaryValue* formData;
    if (!formDict.GetAsDictionary(&formData))
      return NO;

    // Form data is copied into a FormData object field-by-field.
    autofill::FormData form;
    if (!formData->GetString("name", &form.name))
      return NO;
    if (!formName.empty() && formName != form.name)
      continue;

    // Origin is mandatory.
    base::string16 origin;
    if (!formData->GetString("origin", &origin))
      return NO;

    // Use GURL object to verify origin of host page URL.
    form.origin = GURL(origin);
    if (form.origin.GetOrigin() != pageURL.GetOrigin()) {
      LOG(WARNING) << "Form extraction aborted due to same origin policy";
      return NO;
    }

    // Action is optional.
    base::string16 action;
    formData->GetString("action", &action);
    form.action = GURL(action);

    // Is form tag is optional.
    bool is_form_tag;
    if (formData->GetBoolean("is_form_tag", &is_form_tag))
      form.is_form_tag = is_form_tag;

    // Field list (mandatory) is extracted.
    const base::ListValue* fieldsList;
    if (!formData->GetList("fields", &fieldsList))
      return NO;
    for (const auto& fieldDict : *fieldsList) {
      const base::DictionaryValue* field;
      autofill::FormFieldData fieldData;
      if (fieldDict.GetAsDictionary(&field) &&
          [self extractFormField:*field asFieldData:&fieldData]) {
        form.fields.push_back(fieldData);
      } else {
        return NO;
      }
    }
    formsData->push_back(form);
  }
  return YES;
}

- (NSArray*)processSuggestions:(NSArray*)suggestions {
  // The suggestion array is cloned (to claim ownership) and to slightly
  // reorder; a future improvement is to base order on text typed in other
  // fields by users as well as accepted suggestions (crbug.com/245261).
  NSMutableArray* suggestionsCopy = [suggestions mutableCopy];

  // If the most recently selected suggestion was a profile or credit card
  // suggestion, move it to the front of the suggestions.
  if (mostRecentSelectedIdentifier_ > 0) {
    NSUInteger idx = [suggestionsCopy
        indexOfObjectPassingTest:^BOOL(id obj, NSUInteger, BOOL*) {
          FormSuggestion* suggestion = obj;
          return suggestion.identifier == mostRecentSelectedIdentifier_;
        }];

    if (idx != NSNotFound) {
      FormSuggestion* suggestion = suggestionsCopy[idx];
      [suggestionsCopy removeObjectAtIndex:idx];
      [suggestionsCopy insertObject:suggestion atIndex:0];
    }
  }

  // Filter out any key/value suggestions if the user hasn't typed yet.
  if ([typedValue_ length] == 0) {
    for (NSInteger idx = [suggestionsCopy count] - 1; idx >= 0; idx--) {
      FormSuggestion* suggestion = suggestionsCopy[idx];
      if (suggestion.identifier == autofill::POPUP_ITEM_ID_AUTOCOMPLETE_ENTRY) {
        [suggestionsCopy removeObjectAtIndex:idx];
      }
    }
  }

  // If "clear form" entry exists then move it to the front of the suggestions.
  for (NSInteger idx = [suggestionsCopy count] - 1; idx > 0; idx--) {
    FormSuggestion* suggestion = suggestionsCopy[idx];
    if (suggestion.identifier == autofill::POPUP_ITEM_ID_CLEAR_FORM) {
      FormSuggestion* suggestionToMove = suggestionsCopy[idx];
      [suggestionsCopy removeObjectAtIndex:idx];
      [suggestionsCopy insertObject:suggestionToMove atIndex:0];
      break;
    }
  }

  return suggestionsCopy;
}

- (void)onSuggestionsReady:(NSArray*)suggestions
             popupDelegate:
                 (const base::WeakPtr<autofill::AutofillPopupDelegate>&)
                     delegate {
  popupDelegate_ = delegate;
  mostRecentSuggestions_ = [[self processSuggestions:suggestions] copy];
  if (suggestionsAvailableCompletion_)
    suggestionsAvailableCompletion_([mostRecentSuggestions_ count] > 0);
  suggestionsAvailableCompletion_ = nil;
}

#pragma mark -
#pragma mark FormSuggestionProvider

- (void)queryAutofillWithForms:(const FormDataVector&)forms
                         field:(NSString*)fieldName
                          type:(NSString*)type
                    typedValue:(NSString*)typedValue
                      webState:(web::WebState*)webState
             completionHandler:(SuggestionsAvailableCompletion)completion {
  autofill::AutofillManager* autofillManager =
      [self autofillManagerFromWebState:webState];
  if (!autofillManager)
    return;

  // Passed to delegates; we don't use it so it's set to zero.
  int queryId = 0;

  // Find the right form and field.
  autofill::FormFieldData field;
  autofill::FormData form;
  GetFormAndField(&form, &field, forms, base::SysNSStringToUTF8(fieldName),
                  base::SysNSStringToUTF8(type));

  // Save the completion and go look for suggestions.
  suggestionsAvailableCompletion_ = [completion copy];
  typedValue_ = [typedValue copy];

  // Query the AutofillManager for suggestions. Results will arrive in
  // [AutofillController showAutofillPopup].
  autofillManager->OnQueryFormFieldAutofill(queryId, form, field, gfx::RectF());
}

- (void)checkIfSuggestionsAvailableForForm:(NSString*)formName
                                     field:(NSString*)fieldName
                                      type:(NSString*)type
                                typedValue:(NSString*)typedValue
                                  webState:(web::WebState*)webState
                         completionHandler:
                             (SuggestionsAvailableCompletion)completion {
  web::URLVerificationTrustLevel trustLevel;
  const GURL pageURL(webState->GetCurrentURL(&trustLevel));
  if (trustLevel != web::URLVerificationTrustLevel::kAbsolute) {
    DLOG(WARNING) << "Suggestions not offered on untrusted page";
    completion(NO);
    return;
  }

  // Once the active form and field are extracted, send a query to the
  // AutofillManager for suggestions.
  __weak AutofillAgent* weakSelf = self;
  id completionHandler = ^(BOOL success, const FormDataVector& forms) {
    if (success && forms.size() == 1) {
      [weakSelf queryAutofillWithForms:forms
                                 field:fieldName
                                  type:type
                            typedValue:typedValue
                              webState:webState
                     completionHandler:completion];
    }
  };

  // Re-extract the active form and field only. All forms with at least one
  // input element are considered because key/value suggestions are offered
  // even on short forms.
  [self fetchFormsWithName:base::SysNSStringToUTF16(formName)
      minimumRequiredFieldsCount:1
                         pageURL:pageURL
               completionHandler:completionHandler];
}

- (void)retrieveSuggestionsForForm:(NSString*)formName
                             field:(NSString*)fieldName
                              type:(NSString*)type
                        typedValue:(NSString*)typedValue
                          webState:(web::WebState*)webState
                 completionHandler:(SuggestionsReadyCompletion)completion {
  DCHECK(mostRecentSuggestions_)
      << "Requestor should have called "
      << "|checkIfSuggestionsAvailableForForm:field:type:completionHandler:| "
      << "and waited for the result before calling "
      << "|retrieveSuggestionsForForm:field:type:completionHandler:|.";
  completion(mostRecentSuggestions_, self);
}

- (void)didSelectSuggestion:(FormSuggestion*)suggestion
                   forField:(NSString*)fieldName
                       form:(NSString*)formName
          completionHandler:(SuggestionHandledCompletion)completion {
  [[UIDevice currentDevice] playInputClick];
  suggestionHandledCompletion_ = [completion copy];
  mostRecentSelectedIdentifier_ = suggestion.identifier;

  if (suggestion.identifier > 0) {
    pendingAutocompleteField_ = base::SysNSStringToUTF16(fieldName);
    if (popupDelegate_) {
      popupDelegate_->DidAcceptSuggestion(
          base::SysNSStringToUTF16(suggestion.value), suggestion.identifier, 0);
    }
  } else if (suggestion.identifier ==
             autofill::POPUP_ITEM_ID_AUTOCOMPLETE_ENTRY) {
    // FormSuggestion is a simple, single value that can be filled out now.
    [self fillField:base::SysNSStringToUTF8(fieldName)
           formName:base::SysNSStringToUTF8(formName)
              value:base::SysNSStringToUTF16(suggestion.value)];
  } else if (suggestion.identifier == autofill::POPUP_ITEM_ID_CLEAR_FORM) {
    [jsAutofillManager_
        clearAutofilledFieldsForFormNamed:formName
                        completionHandler:suggestionHandledCompletion_];
    suggestionHandledCompletion_ = nil;
  } else {
    NOTREACHED() << "unknown identifier " << suggestion.identifier;
  }
}

#pragma mark -
#pragma mark CRWWebStateObserver

- (void)webStateDestroyed:(web::WebState*)webState {
  [self detachFromWebState];
}

- (void)webState:(web::WebState*)webState
    didSubmitDocumentWithFormNamed:(const std::string&)formName
                     userInitiated:(BOOL)userInitiated {
  if (!browserState_->GetPrefs()->GetBoolean(autofill::prefs::kAutofillEnabled))
    return;

  web::URLVerificationTrustLevel trustLevel;
  const GURL pageURL(webState->GetCurrentURL(&trustLevel));
  if (trustLevel != web::URLVerificationTrustLevel::kAbsolute) {
    DLOG(WARNING) << "Form submit not handled on untrusted page";
    return;
  }

  __weak AutofillAgent* weakSelf = self;
  id completionHandler = ^(BOOL success, const FormDataVector& forms) {
    AutofillAgent* strongSelf = weakSelf;
    if (!strongSelf || !success)
      return;
    autofill::AutofillManager* autofillManager =
        [strongSelf autofillManagerFromWebState:webState];
    if (!autofillManager || forms.empty())
      return;
    if (forms.size() > 1) {
      DLOG(WARNING) << "Only one form should be extracted.";
      return;
    }
    [strongSelf notifyAutofillManager:autofillManager
                     ofFormsSubmitted:forms
                        userInitiated:userInitiated];

  };
  // This code is racing against the new page loading and will not get the
  // password form data if the page has changed. In most cases this code wins
  // the race.
  // TODO(crbug.com/418827): Fix this by passing in more data from the JS side.
  [self fetchFormsWithName:base::UTF8ToUTF16(formName)
      minimumRequiredFieldsCount:1
                         pageURL:pageURL
               completionHandler:completionHandler];
}

- (void)webState:(web::WebState*)webState didLoadPageWithSuccess:(BOOL)success {
  if (!browserState_->GetPrefs()->GetBoolean(
          autofill::prefs::kAutofillEnabled) ||
      !webState->ContentIsHTML()) {
    return;
  }
  [self processPage:webState];
}

- (void)processPage:(web::WebState*)webState {
  web::URLVerificationTrustLevel trustLevel;
  const GURL pageURL(webState->GetCurrentURL(&trustLevel));
  if (trustLevel != web::URLVerificationTrustLevel::kAbsolute) {
    DLOG(WARNING) << "Page load not handled on untrusted page";
    return;
  }

  if (!web::UrlHasWebScheme(pageURL))
    return;

  // This process is only done once.
  if ([jsAutofillManager_ hasBeenInjected])
    return;

  popupDelegate_.reset();
  suggestionsAvailableCompletion_ = nil;
  suggestionHandledCompletion_ = nil;
  mostRecentSuggestions_ = nil;
  typedValue_ = nil;

  [jsAutofillManager_ inject];

  __weak AutofillAgent* weakSelf = self;
  id completionHandler = ^(BOOL success, const FormDataVector& forms) {
    AutofillAgent* strongSelf = weakSelf;
    if (!strongSelf || !success)
      return;
    autofill::AutofillManager* autofillManager =
        [strongSelf autofillManagerFromWebState:webState];
    if (!autofillManager || forms.empty())
      return;
    [strongSelf notifyAutofillManager:autofillManager ofFormsSeen:forms];
  };
  // The document has now been fully loaded. Scan for forms to be extracted.
  // Because of the cost of communicating with the server, only forms that have
  // enough forms to make them likely candidates for profile completion are
  // extracted.
  [self fetchFormsWithName:base::string16()
      minimumRequiredFieldsCount:autofill::kRequiredFieldsForPredictionRoutines
                         pageURL:pageURL
               completionHandler:completionHandler];
}

- (void)webState:(web::WebState*)webState
    didRegisterFormActivityWithFormNamed:(const std::string&)formName
                               fieldName:(const std::string&)fieldName
                                    type:(const std::string&)type
                                   value:(const std::string&)value
                            inputMissing:(BOOL)inputMissing {
  if (!browserState_->GetPrefs()->GetBoolean(autofill::prefs::kAutofillEnabled))
    return;
  web::URLVerificationTrustLevel trustLevel;
  const GURL pageURL(webState->GetCurrentURL(&trustLevel));
  if (trustLevel != web::URLVerificationTrustLevel::kAbsolute) {
    DLOG(WARNING) << "Form activity not handled on untrusted page";
    return;
  }

  // Autofill and suggestion scripts are only injected for web URLs.
  if (!web::UrlHasWebScheme(pageURL) || !webState->ContentIsHTML())
    return;

  // Returns early and reset the suggestion state if an error occurs.
  if (inputMissing)
    return;

  // Processing the page can be needed here if Autofill is enabled in settings
  // when the page is already loaded, or if the user focuses a field before the
  // page is fully loaded.
  [self processPage:webState];

  // Blur not handled; we don't reset the suggestion state because if the
  // keyboard is about to be dismissed there's no point. If not it means the
  // next focus event will update the suggestion state within milliseconds, so
  // if we do it now a flicker will be seen.
  if (type.compare("blur") == 0)
    return;

  // Necessary so the strings can be used inside a block.
  std::string fieldNameCopy = fieldName;
  std::string typeCopy = type;

  __weak AutofillAgent* weakSelf = self;
  id completionHandler = ^(BOOL success, const FormDataVector& forms) {
    if (success && forms.size() == 1) {
      [weakSelf processFormActivityExtractedData:forms
                                       fieldName:fieldNameCopy
                                            type:typeCopy
                                        webState:webState];
    }
  };

  // Re-extract the active form and field only. There is no minimum field
  // requirement because key/value suggestions are offered event on short forms.
  [self fetchFormsWithName:base::UTF8ToUTF16(formName)
      minimumRequiredFieldsCount:1
                         pageURL:pageURL
               completionHandler:completionHandler];
}

- (void)processFormActivityExtractedData:(const FormDataVector&)forms
                               fieldName:(const std::string&)fieldName
                                    type:(const std::string&)type
                                webState:(web::WebState*)webState {
  autofill::AutofillManager* autofillManager =
      [self autofillManagerFromWebState:webState];
  if (!autofillManager)
    return;

  autofill::FormFieldData field;
  autofill::FormData form;
  GetFormAndField(&form, &field, forms, fieldName, type);

  // Tell the manager about the form activity (for metrics).
  if (type.compare("input") == 0 && (field.form_control_type == "text" ||
                                     field.form_control_type == "password")) {
    autofillManager->OnTextFieldDidChange(form, field, base::TimeTicks::Now());
  }
}

#pragma mark -
#pragma mark AutofillViewClient

// Complete a field named |fieldName| on the form named |formName| using |value|
// then move the cursor.
// TODO(crbug.com/661621): |dataString| ends up at fillFormField() in
// autofill_controller.js. fillFormField() expects an AutofillFormFieldData
// object, which |dataString| is not because 'form' is not a specified member of
// AutofillFormFieldData. fillFormField() also expects members 'max_length' and
// 'is_checked' to exist.
- (void)fillField:(const std::string&)fieldName
         formName:(const std::string&)formName
            value:(const base::string16)value {
  base::DictionaryValue data;
  data.SetString("name", fieldName);
  data.SetString("form", formName);
  data.SetString("value", value);
  std::string dataString;
  base::JSONWriter::Write(data, &dataString);

  DCHECK(suggestionHandledCompletion_);
  [jsAutofillManager_ fillActiveFormField:base::SysUTF8ToNSString(dataString)
                        completionHandler:suggestionHandledCompletion_];
  suggestionHandledCompletion_ = nil;
}

- (void)onFormDataFilled:(const autofill::FormData&)form {
  std::unique_ptr<base::DictionaryValue> formData(new base::DictionaryValue);
  formData->SetString("formName", base::UTF16ToUTF8(form.name));
  // Note: Destruction of all child base::Value types is handled by the root
  // formData object on its own destruction.
  base::DictionaryValue* fieldsData = new base::DictionaryValue;

  const std::vector<autofill::FormFieldData>& fields = form.fields;
  for (const auto& fieldData : fields) {
    fieldsData->SetStringWithoutPathExpansion(base::UTF16ToUTF8(fieldData.name),
                                              fieldData.value);
  }
  formData->Set("fields", fieldsData);

  // Stringify the JSON data and send it to the UIWebView-side fillForm method.
  std::string dataString;
  base::JSONWriter::Write(*formData.get(), &dataString);

  // It is possible that the fill was not initiated by selecting a suggestion.
  // In this case we provide an empty callback.
  if (!suggestionHandledCompletion_)
    suggestionHandledCompletion_ = [^{
    } copy];
  [jsAutofillManager_
                fillForm:base::SysUTF8ToNSString(dataString)
      forceFillFieldName:base::SysUTF16ToNSString(pendingAutocompleteField_)
       completionHandler:suggestionHandledCompletion_];
  suggestionHandledCompletion_ = nil;
}

// Returns the first value from the array of possible values that has a match in
// the list of accepted values in the AutofillField, or nil if there is no
// match. If AutofillField does not specify valid values, the first value is
// returned from the list.
+ (NSString*)selectFrom:(NSArray*)values for:(autofill::AutofillField*)field {
  if (field->option_values.empty())
    return values[0];

  for (NSString* value in values) {
    std::string valueString = base::SysNSStringToUTF8(value);
    for (size_t i = 0; i < field->option_values.size(); ++i) {
      if (base::UTF16ToUTF8(field->option_values[i]) == valueString)
        return value;
    }
    for (size_t i = 0; i < field->option_contents.size(); ++i) {
      if (base::UTF16ToUTF8(field->option_contents[i]) == valueString)
        return value;
    }
  }

  return nil;
}

- (void)renderAutofillTypePredictions:
    (const std::vector<autofill::FormStructure*>&)structure {
  base::DictionaryValue predictionData;
  for (autofill::FormStructure* form : structure) {
    auto formJSONData = base::MakeUnique<base::DictionaryValue>();
    autofill::FormData formData = form->ToFormData();
    for (const auto& field : *form) {
      autofill::AutofillType type(field->Type());
      if (type.IsUnknown())
        continue;
      formJSONData->SetStringWithoutPathExpansion(
          base::UTF16ToUTF8(field->name), type.ToString());
    }
    predictionData.SetWithoutPathExpansion(base::UTF16ToUTF8(formData.name),
                                           std::move(formJSONData));
  }
  std::string dataString;
  base::JSONWriter::Write(predictionData, &dataString);
  [jsAutofillManager_ fillPredictionData:base::SysUTF8ToNSString(dataString)];
}

@end
