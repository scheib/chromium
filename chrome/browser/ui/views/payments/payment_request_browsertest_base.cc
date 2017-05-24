// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/payments/payment_request_browsertest_base.h"

#include <algorithm>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/command_line.h"
#include "base/memory/ptr_util.h"
#include "base/run_loop.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/autofill/personal_data_manager_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/payments/editor_view_controller.h"
#include "chrome/browser/ui/views/payments/payment_request_dialog_view_ids.h"
#include "chrome/browser/ui/views/payments/validating_combobox.h"
#include "chrome/browser/ui/views/payments/validating_textfield.h"
#include "chrome/browser/ui/views/payments/view_stack.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/autofill/core/browser/address_combobox_model.h"
#include "components/autofill/core/browser/autofill_profile.h"
#include "components/autofill/core/browser/credit_card.h"
#include "components/autofill/core/browser/personal_data_manager.h"
#include "components/payments/content/payment_request.h"
#include "components/payments/content/payment_request_web_contents_manager.h"
#include "components/web_modal/web_contents_modal_dialog_manager.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/content_switches.h"
#include "content/public/test/browser_test_utils.h"
#include "net/dns/mock_host_resolver.h"
#include "services/service_manager/public/cpp/bind_source_info.h"
#include "services/service_manager/public/cpp/binder_registry.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/test/ui_controls.h"
#include "ui/events/base_event_utils.h"
#include "ui/events/event.h"
#include "ui/gfx/animation/test_animation_delegate.h"
#include "ui/gfx/geometry/point.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/styled_label.h"

namespace payments {

namespace {
const auto kBillingAddressType = autofill::ADDRESS_BILLING_LINE1;
}  // namespace

PersonalDataLoadedObserverMock::PersonalDataLoadedObserverMock() {}
PersonalDataLoadedObserverMock::~PersonalDataLoadedObserverMock() {}

PaymentRequestBrowserTestBase::PaymentRequestBrowserTestBase(
    const std::string& test_file_path)
    : test_file_path_(test_file_path),
      delegate_(nullptr),
      is_incognito_(false),
      is_valid_ssl_(true) {}
PaymentRequestBrowserTestBase::~PaymentRequestBrowserTestBase() {}

void PaymentRequestBrowserTestBase::SetUpCommandLine(
    base::CommandLine* command_line) {
  // HTTPS server only serves a valid cert for localhost, so this is needed to
  // load pages from "a.com" without an interstitial.
  command_line->AppendSwitch(switches::kIgnoreCertificateErrors);
}

void PaymentRequestBrowserTestBase::SetUpOnMainThread() {
  // Setup the https server.
  https_server_ = base::MakeUnique<net::EmbeddedTestServer>(
      net::EmbeddedTestServer::TYPE_HTTPS);
  host_resolver()->AddRule("a.com", "127.0.0.1");
  host_resolver()->AddRule("b.com", "127.0.0.1");
  ASSERT_TRUE(https_server_->InitializeAndListen());
  https_server_->ServeFilesFromSourceDirectory("chrome/test/data/payments");
  https_server_->StartAcceptingConnections();

  NavigateTo(test_file_path_);

  // Starting now, PaymentRequest Mojo messages sent by the renderer will
  // create PaymentRequest objects via this test's CreatePaymentRequestForTest,
  // allowing the test to inject itself as a dialog observer.
  content::WebContents* web_contents = GetActiveWebContents();
  service_manager::BinderRegistry* registry =
      web_contents->GetMainFrame()->GetInterfaceRegistry();
  registry->RemoveInterface(payments::mojom::PaymentRequest::Name_);
  registry->AddInterface(
      base::Bind(&PaymentRequestBrowserTestBase::CreatePaymentRequestForTest,
                 base::Unretained(this), web_contents));
}

void PaymentRequestBrowserTestBase::NavigateTo(const std::string& file_path) {
  if (file_path.find("data:") == 0U) {
    ui_test_utils::NavigateToURL(browser(), GURL(file_path));
  } else {
    ui_test_utils::NavigateToURL(browser(),
                                 https_server()->GetURL("a.com", file_path));
  }
}

void PaymentRequestBrowserTestBase::SetIncognito() {
  is_incognito_ = true;
}

void PaymentRequestBrowserTestBase::SetInvalidSsl() {
  is_valid_ssl_ = false;
}

void PaymentRequestBrowserTestBase::OnCanMakePaymentCalled() {
  if (event_observer_)
    event_observer_->Observe(DialogEvent::CAN_MAKE_PAYMENT_CALLED);
}

void PaymentRequestBrowserTestBase::OnNotSupportedError() {
  if (event_observer_)
    event_observer_->Observe(DialogEvent::NOT_SUPPORTED_ERROR);
}

void PaymentRequestBrowserTestBase::OnConnectionTerminated() {
  if (event_observer_)
    event_observer_->Observe(DialogEvent::DIALOG_CLOSED);
}

void PaymentRequestBrowserTestBase::OnAbortCalled() {
  if (event_observer_)
    event_observer_->Observe(DialogEvent::ABORT_CALLED);
}

void PaymentRequestBrowserTestBase::OnDialogOpened() {
  if (event_observer_)
    event_observer_->Observe(DialogEvent::DIALOG_OPENED);
}

void PaymentRequestBrowserTestBase::OnOrderSummaryOpened() {
  if (event_observer_)
    event_observer_->Observe(DialogEvent::ORDER_SUMMARY_OPENED);
}

void PaymentRequestBrowserTestBase::OnPaymentMethodOpened() {
  if (event_observer_)
    event_observer_->Observe(DialogEvent::PAYMENT_METHOD_OPENED);
}

void PaymentRequestBrowserTestBase::OnShippingAddressSectionOpened() {
  if (event_observer_)
    event_observer_->Observe(DialogEvent::SHIPPING_ADDRESS_SECTION_OPENED);
}

void PaymentRequestBrowserTestBase::OnShippingOptionSectionOpened() {
  if (event_observer_)
    event_observer_->Observe(DialogEvent::SHIPPING_OPTION_SECTION_OPENED);
}

void PaymentRequestBrowserTestBase::OnCreditCardEditorOpened() {
  if (event_observer_)
    event_observer_->Observe(DialogEvent::CREDIT_CARD_EDITOR_OPENED);
}

void PaymentRequestBrowserTestBase::OnShippingAddressEditorOpened() {
  if (event_observer_)
    event_observer_->Observe(DialogEvent::SHIPPING_ADDRESS_EDITOR_OPENED);
}

void PaymentRequestBrowserTestBase::OnContactInfoEditorOpened() {
  if (event_observer_)
    event_observer_->Observe(DialogEvent::CONTACT_INFO_EDITOR_OPENED);
}

void PaymentRequestBrowserTestBase::OnBackNavigation() {
  if (event_observer_)
    event_observer_->Observe(DialogEvent::BACK_NAVIGATION);
}

void PaymentRequestBrowserTestBase::OnBackToPaymentSheetNavigation() {
  if (event_observer_)
    event_observer_->Observe(DialogEvent::BACK_TO_PAYMENT_SHEET_NAVIGATION);
}

void PaymentRequestBrowserTestBase::OnContactInfoOpened() {
  if (event_observer_)
    event_observer_->Observe(DialogEvent::CONTACT_INFO_OPENED);
}

void PaymentRequestBrowserTestBase::OnEditorViewUpdated() {
  if (event_observer_)
    event_observer_->Observe(DialogEvent::EDITOR_VIEW_UPDATED);
}

void PaymentRequestBrowserTestBase::OnErrorMessageShown() {
  if (event_observer_)
    event_observer_->Observe(DialogEvent::ERROR_MESSAGE_SHOWN);
}

void PaymentRequestBrowserTestBase::OnSpecDoneUpdating() {
  if (event_observer_)
    event_observer_->Observe(DialogEvent::SPEC_DONE_UPDATING);
}

void PaymentRequestBrowserTestBase::OnCvcPromptShown() {
  if (event_observer_)
    event_observer_->Observe(DialogEvent::CVC_PROMPT_SHOWN);
}

void PaymentRequestBrowserTestBase::InvokePaymentRequestUI() {
  ResetEventObserver(DialogEvent::DIALOG_OPENED);

  content::WebContents* web_contents = GetActiveWebContents();
  const std::string click_buy_button_js =
      "(function() { document.getElementById('buy').click(); })();";
  ASSERT_TRUE(content::ExecuteScript(web_contents, click_buy_button_js));

  WaitForObservedEvent();

  // The web-modal dialog should be open.
  web_modal::WebContentsModalDialogManager* web_contents_modal_dialog_manager =
      web_modal::WebContentsModalDialogManager::FromWebContents(web_contents);
  EXPECT_TRUE(web_contents_modal_dialog_manager->IsDialogActive());
}

void PaymentRequestBrowserTestBase::ExpectBodyContains(
    const std::vector<std::string>& expected_strings) {
  content::WebContents* web_contents = GetActiveWebContents();
  const std::string extract_contents_js =
      "(function() { "
      "window.domAutomationController.send(window.document.body.textContent); "
      "})()";
  std::string contents;
  EXPECT_TRUE(content::ExecuteScriptAndExtractString(
      web_contents, extract_contents_js, &contents));
  for (const std::string& expected_string : expected_strings) {
    EXPECT_NE(std::string::npos, contents.find(expected_string))
        << "String \"" << expected_string
        << "\" is not present in the content \"" << contents << "\"";
  }
}

void PaymentRequestBrowserTestBase::ExpectBodyContains(
    const std::vector<base::string16>& expected_strings) {
  std::vector<std::string> converted(expected_strings.size());
  std::transform(expected_strings.begin(), expected_strings.end(),
                 converted.begin(),
                 [](const base::string16& s) { return base::UTF16ToUTF8(s); });
  ExpectBodyContains(converted);
}

void PaymentRequestBrowserTestBase::OpenOrderSummaryScreen() {
  ResetEventObserver(DialogEvent::ORDER_SUMMARY_OPENED);

  ClickOnDialogViewAndWait(DialogViewID::PAYMENT_SHEET_SUMMARY_SECTION);
}

void PaymentRequestBrowserTestBase::OpenPaymentMethodScreen() {
  ResetEventObserver(DialogEvent::PAYMENT_METHOD_OPENED);

  views::View* view = delegate_->dialog_view()->GetViewByID(
      static_cast<int>(DialogViewID::PAYMENT_SHEET_PAYMENT_METHOD_SECTION));
  if (!view) {
    view = delegate_->dialog_view()->GetViewByID(static_cast<int>(
        DialogViewID::PAYMENT_SHEET_PAYMENT_METHOD_SECTION_BUTTON));
  }

  EXPECT_TRUE(view);

  ClickOnDialogViewAndWait(view);
}

void PaymentRequestBrowserTestBase::OpenShippingAddressSectionScreen() {
  ResetEventObserver(DialogEvent::SHIPPING_ADDRESS_SECTION_OPENED);

  views::View* view = delegate_->dialog_view()->GetViewByID(
      static_cast<int>(DialogViewID::PAYMENT_SHEET_SHIPPING_ADDRESS_SECTION));
  if (!view) {
    view = delegate_->dialog_view()->GetViewByID(static_cast<int>(
        DialogViewID::PAYMENT_SHEET_SHIPPING_ADDRESS_SECTION_BUTTON));
  }

  EXPECT_TRUE(view);

  ClickOnDialogViewAndWait(view);
}

void PaymentRequestBrowserTestBase::OpenShippingOptionSectionScreen() {
  ResetEventObserver(DialogEvent::SHIPPING_OPTION_SECTION_OPENED);

  views::View* view = delegate_->dialog_view()->GetViewByID(
      static_cast<int>(DialogViewID::PAYMENT_SHEET_SHIPPING_OPTION_SECTION));
  if (!view) {
    view = delegate_->dialog_view()->GetViewByID(static_cast<int>(
        DialogViewID::PAYMENT_SHEET_SHIPPING_OPTION_SECTION_BUTTON));
  }

  EXPECT_TRUE(view);

  ClickOnDialogViewAndWait(DialogViewID::PAYMENT_SHEET_SHIPPING_OPTION_SECTION);
}

void PaymentRequestBrowserTestBase::OpenContactInfoScreen() {
  ResetEventObserver(DialogEvent::CONTACT_INFO_OPENED);

  views::View* view = delegate_->dialog_view()->GetViewByID(
      static_cast<int>(DialogViewID::PAYMENT_SHEET_CONTACT_INFO_SECTION));
  if (!view) {
    view = delegate_->dialog_view()->GetViewByID(static_cast<int>(
        DialogViewID::PAYMENT_SHEET_CONTACT_INFO_SECTION_BUTTON));
  }

  EXPECT_TRUE(view);
  ClickOnDialogViewAndWait(view);
}

void PaymentRequestBrowserTestBase::OpenCreditCardEditorScreen() {
  ResetEventObserver(DialogEvent::CREDIT_CARD_EDITOR_OPENED);

  views::View* view = delegate_->dialog_view()->GetViewByID(
      static_cast<int>(DialogViewID::PAYMENT_METHOD_ADD_CARD_BUTTON));
  if (!view) {
    view = delegate_->dialog_view()->GetViewByID(static_cast<int>(
        DialogViewID::PAYMENT_SHEET_PAYMENT_METHOD_SECTION_BUTTON));
  }

  EXPECT_TRUE(view);
  ClickOnDialogViewAndWait(view);
}

void PaymentRequestBrowserTestBase::OpenShippingAddressEditorScreen() {
  ResetEventObserver(DialogEvent::SHIPPING_ADDRESS_EDITOR_OPENED);

  views::View* view = delegate_->dialog_view()->GetViewByID(
      static_cast<int>(DialogViewID::PAYMENT_METHOD_ADD_SHIPPING_BUTTON));
  if (!view) {
    view = delegate_->dialog_view()->GetViewByID(static_cast<int>(
        DialogViewID::PAYMENT_SHEET_SHIPPING_ADDRESS_SECTION_BUTTON));
  }

  EXPECT_TRUE(view);
  ClickOnDialogViewAndWait(view);
}

void PaymentRequestBrowserTestBase::OpenContactInfoEditorScreen() {
  ResetEventObserver(DialogEvent::CONTACT_INFO_EDITOR_OPENED);

  views::View* view = delegate_->dialog_view()->GetViewByID(
      static_cast<int>(DialogViewID::PAYMENT_METHOD_ADD_CONTACT_BUTTON));
  if (!view) {
    view = delegate_->dialog_view()->GetViewByID(static_cast<int>(
        DialogViewID::PAYMENT_SHEET_CONTACT_INFO_SECTION_BUTTON));
  }

  EXPECT_TRUE(view);
  ClickOnDialogViewAndWait(view);
}

void PaymentRequestBrowserTestBase::ClickOnBackArrow() {
  ResetEventObserver(DialogEvent::BACK_NAVIGATION);

  ClickOnDialogViewAndWait(DialogViewID::BACK_BUTTON);
}

void PaymentRequestBrowserTestBase::ClickOnCancel() {
  ResetEventObserver(DialogEvent::DIALOG_CLOSED);

  ClickOnDialogViewAndWait(DialogViewID::CANCEL_BUTTON, false);
}

content::WebContents* PaymentRequestBrowserTestBase::GetActiveWebContents() {
  return browser()->tab_strip_model()->GetActiveWebContents();
}

const std::vector<PaymentRequest*>
PaymentRequestBrowserTestBase::GetPaymentRequests(
    content::WebContents* web_contents) {
  PaymentRequestWebContentsManager* manager =
      PaymentRequestWebContentsManager::GetOrCreateForWebContents(web_contents);
  if (!manager)
    return std::vector<PaymentRequest*>();

  std::vector<PaymentRequest*> payment_requests_ptrs;
  for (const auto& p : manager->payment_requests_)
    payment_requests_ptrs.push_back(p.first);
  return payment_requests_ptrs;
}

autofill::PersonalDataManager* PaymentRequestBrowserTestBase::GetDataManager() {
  return autofill::PersonalDataManagerFactory::GetForProfile(
      Profile::FromBrowserContext(GetActiveWebContents()->GetBrowserContext()));
}

void PaymentRequestBrowserTestBase::AddAutofillProfile(
    const autofill::AutofillProfile& profile) {
  autofill::PersonalDataManager* personal_data_manager = GetDataManager();
  size_t profile_count = personal_data_manager->GetProfiles().size();

  PersonalDataLoadedObserverMock personal_data_observer;
  personal_data_manager->AddObserver(&personal_data_observer);
  base::RunLoop data_loop;
  EXPECT_CALL(personal_data_observer, OnPersonalDataChanged())
      .WillOnce(QuitMessageLoop(&data_loop));
  personal_data_manager->AddProfile(profile);
  data_loop.Run();

  personal_data_manager->RemoveObserver(&personal_data_observer);
  EXPECT_EQ(profile_count + 1, personal_data_manager->GetProfiles().size());
}

void PaymentRequestBrowserTestBase::AddCreditCard(
    const autofill::CreditCard& card) {
  autofill::PersonalDataManager* personal_data_manager = GetDataManager();
  size_t card_count = personal_data_manager->GetCreditCards().size();

  PersonalDataLoadedObserverMock personal_data_observer;
  personal_data_manager->AddObserver(&personal_data_observer);
  base::RunLoop data_loop;
  EXPECT_CALL(personal_data_observer, OnPersonalDataChanged())
      .WillOnce(QuitMessageLoop(&data_loop));
  personal_data_manager->AddCreditCard(card);
  data_loop.Run();

  personal_data_manager->RemoveObserver(&personal_data_observer);
  EXPECT_EQ(card_count + 1, personal_data_manager->GetCreditCards().size());
}

void PaymentRequestBrowserTestBase::CreatePaymentRequestForTest(
    content::WebContents* web_contents,
    const service_manager::BindSourceInfo& source_info,
    payments::mojom::PaymentRequestRequest request) {
  DCHECK(web_contents);
  std::unique_ptr<TestChromePaymentRequestDelegate> delegate =
      base::MakeUnique<TestChromePaymentRequestDelegate>(
          web_contents, this /* observer */, is_incognito_, is_valid_ssl_);
  delegate_ = delegate.get();
  PaymentRequestWebContentsManager::GetOrCreateForWebContents(web_contents)
      ->CreatePaymentRequest(web_contents->GetMainFrame(), web_contents,
                             std::move(delegate), std::move(request), this);
}

void PaymentRequestBrowserTestBase::ClickOnDialogViewAndWait(
    DialogViewID view_id,
    bool wait_for_animation) {
  views::View* view =
      delegate_->dialog_view()->GetViewByID(static_cast<int>(view_id));
  DCHECK(view);
  ClickOnDialogViewAndWait(view, wait_for_animation);
}

void PaymentRequestBrowserTestBase::ClickOnDialogViewAndWait(
    views::View* view,
    bool wait_for_animation) {
  DCHECK(view);
  ui::MouseEvent pressed(ui::ET_MOUSE_PRESSED, gfx::Point(), gfx::Point(),
                         ui::EventTimeForNow(), ui::EF_LEFT_MOUSE_BUTTON,
                         ui::EF_LEFT_MOUSE_BUTTON);
  view->OnMousePressed(pressed);
  ui::MouseEvent released_event = ui::MouseEvent(
      ui::ET_MOUSE_RELEASED, gfx::Point(), gfx::Point(), ui::EventTimeForNow(),
      ui::EF_LEFT_MOUSE_BUTTON, ui::EF_LEFT_MOUSE_BUTTON);
  view->OnMouseReleased(released_event);

  if (wait_for_animation)
    WaitForAnimation();

  WaitForObservedEvent();
}

void PaymentRequestBrowserTestBase::ClickOnChildInListViewAndWait(
    int child_index,
    int total_num_children,
    DialogViewID list_view_id,
    bool wait_for_animation) {
  views::View* list_view =
      dialog_view()->GetViewByID(static_cast<int>(list_view_id));
  EXPECT_TRUE(list_view);
  EXPECT_EQ(total_num_children, list_view->child_count());
  ClickOnDialogViewAndWait(list_view->child_at(child_index),
                           wait_for_animation);
}

std::vector<base::string16>
PaymentRequestBrowserTestBase::GetProfileLabelValues(
    DialogViewID parent_view_id) {
  std::vector<base::string16> line_labels;
  views::View* parent_view =
      dialog_view()->GetViewByID(static_cast<int>(parent_view_id));
  EXPECT_TRUE(parent_view);

  views::View* view = parent_view->GetViewByID(
      static_cast<int>(DialogViewID::PROFILE_LABEL_LINE_1));
  if (view)
    line_labels.push_back(static_cast<views::Label*>(view)->text());
  view = parent_view->GetViewByID(
      static_cast<int>(DialogViewID::PROFILE_LABEL_LINE_2));
  if (view)
    line_labels.push_back(static_cast<views::Label*>(view)->text());
  view = parent_view->GetViewByID(
      static_cast<int>(DialogViewID::PROFILE_LABEL_LINE_3));
  if (view)
    line_labels.push_back(static_cast<views::Label*>(view)->text());
  view = parent_view->GetViewByID(
      static_cast<int>(DialogViewID::PROFILE_LABEL_ERROR));
  if (view)
    line_labels.push_back(static_cast<views::Label*>(view)->text());

  return line_labels;
}

std::vector<base::string16>
PaymentRequestBrowserTestBase::GetShippingOptionLabelValues(
    DialogViewID parent_view_id) {
  std::vector<base::string16> labels;
  views::View* parent_view =
      dialog_view()->GetViewByID(static_cast<int>(parent_view_id));
  EXPECT_TRUE(parent_view);

  views::View* view = parent_view->GetViewByID(
      static_cast<int>(DialogViewID::SHIPPING_OPTION_DESCRIPTION));
  DCHECK(view);
  labels.push_back(static_cast<views::Label*>(view)->text());
  view = parent_view->GetViewByID(
      static_cast<int>(DialogViewID::SHIPPING_OPTION_AMOUNT));
  DCHECK(view);
  labels.push_back(static_cast<views::Label*>(view)->text());
  return labels;
}

void PaymentRequestBrowserTestBase::OpenCVCPromptWithCVC(
    const base::string16& cvc) {
  ResetEventObserver(DialogEvent::CVC_PROMPT_SHOWN);
  ClickOnDialogViewAndWait(DialogViewID::PAY_BUTTON);

  views::Textfield* cvc_field =
      static_cast<views::Textfield*>(delegate_->dialog_view()->GetViewByID(
          static_cast<int>(DialogViewID::CVC_PROMPT_TEXT_FIELD)));
  cvc_field->InsertOrReplaceText(cvc);
}

void PaymentRequestBrowserTestBase::PayWithCreditCardAndWait(
    const base::string16& cvc) {
  OpenCVCPromptWithCVC(cvc);

  ResetEventObserver(DialogEvent::DIALOG_CLOSED);
  ClickOnDialogViewAndWait(DialogViewID::CVC_PROMPT_CONFIRM_BUTTON);
}

base::string16 PaymentRequestBrowserTestBase::GetEditorTextfieldValue(
    autofill::ServerFieldType type) {
  ValidatingTextfield* textfield =
      static_cast<ValidatingTextfield*>(delegate_->dialog_view()->GetViewByID(
          EditorViewController::GetInputFieldViewId(type)));
  DCHECK(textfield);
  return textfield->text();
}

void PaymentRequestBrowserTestBase::SetEditorTextfieldValue(
    const base::string16& value,
    autofill::ServerFieldType type) {
  ValidatingTextfield* textfield =
      static_cast<ValidatingTextfield*>(delegate_->dialog_view()->GetViewByID(
          EditorViewController::GetInputFieldViewId(type)));
  DCHECK(textfield);
  textfield->SetText(value);
  textfield->OnContentsChanged();
  textfield->OnBlur();
}

base::string16 PaymentRequestBrowserTestBase::GetComboboxValue(
    autofill::ServerFieldType type) {
  ValidatingCombobox* combobox =
      static_cast<ValidatingCombobox*>(delegate_->dialog_view()->GetViewByID(
          EditorViewController::GetInputFieldViewId(type)));
  DCHECK(combobox);
  return combobox->model()->GetItemAt(combobox->selected_index());
}

void PaymentRequestBrowserTestBase::SetComboboxValue(
    const base::string16& value,
    autofill::ServerFieldType type) {
  ValidatingCombobox* combobox =
      static_cast<ValidatingCombobox*>(delegate_->dialog_view()->GetViewByID(
          EditorViewController::GetInputFieldViewId(type)));
  DCHECK(combobox);
  combobox->SelectValue(value);
  combobox->OnContentsChanged();
  combobox->OnBlur();
}

void PaymentRequestBrowserTestBase::SelectBillingAddress(
    const std::string& billing_address_id) {
  views::Combobox* address_combobox(
      static_cast<views::Combobox*>(dialog_view()->GetViewByID(
          EditorViewController::GetInputFieldViewId(kBillingAddressType))));
  ASSERT_NE(address_combobox, nullptr);
  autofill::AddressComboboxModel* address_combobox_model(
      static_cast<autofill::AddressComboboxModel*>(address_combobox->model()));
  address_combobox->SetSelectedRow(
      address_combobox_model->GetIndexOfIdentifier(billing_address_id));
  address_combobox->OnBlur();
}

bool PaymentRequestBrowserTestBase::IsEditorTextfieldInvalid(
    autofill::ServerFieldType type) {
  ValidatingTextfield* textfield =
      static_cast<ValidatingTextfield*>(delegate_->dialog_view()->GetViewByID(
          EditorViewController::GetInputFieldViewId(type)));
  DCHECK(textfield);
  return textfield->invalid();
}

bool PaymentRequestBrowserTestBase::IsEditorComboboxInvalid(
    autofill::ServerFieldType type) {
  ValidatingCombobox* combobox =
      static_cast<ValidatingCombobox*>(delegate_->dialog_view()->GetViewByID(
          EditorViewController::GetInputFieldViewId(type)));
  DCHECK(combobox);
  return combobox->invalid();
}

bool PaymentRequestBrowserTestBase::IsPayButtonEnabled() {
  views::Button* button =
      static_cast<views::Button*>(delegate_->dialog_view()->GetViewByID(
          static_cast<int>(DialogViewID::PAY_BUTTON)));
  DCHECK(button);
  return button->enabled();
}

void PaymentRequestBrowserTestBase::WaitForAnimation() {
  ViewStack* view_stack = dialog_view()->view_stack_for_testing();
  if (view_stack->slide_in_animator_->IsAnimating()) {
    view_stack->slide_in_animator_->SetAnimationDuration(1);
    view_stack->slide_in_animator_->SetAnimationDelegate(
        view_stack->top(), std::unique_ptr<gfx::AnimationDelegate>(
                               new gfx::TestAnimationDelegate()));
    base::RunLoop().Run();
  } else if (view_stack->slide_out_animator_->IsAnimating()) {
    view_stack->slide_out_animator_->SetAnimationDuration(1);
    view_stack->slide_out_animator_->SetAnimationDelegate(
        view_stack->top(), std::unique_ptr<gfx::AnimationDelegate>(
                               new gfx::TestAnimationDelegate()));
    base::RunLoop().Run();
  }
}

const base::string16& PaymentRequestBrowserTestBase::GetLabelText(
    DialogViewID view_id) {
  views::View* view = dialog_view()->GetViewByID(static_cast<int>(view_id));
  DCHECK(view);
  return static_cast<views::Label*>(view)->text();
}

const base::string16& PaymentRequestBrowserTestBase::GetStyledLabelText(
    DialogViewID view_id) {
  views::View* view = dialog_view()->GetViewByID(static_cast<int>(view_id));
  DCHECK(view);
  return static_cast<views::StyledLabel*>(view)->text();
}

const base::string16& PaymentRequestBrowserTestBase::GetErrorLabelForType(
    autofill::ServerFieldType type) {
  views::View* view = dialog_view()->GetViewByID(
      static_cast<int>(DialogViewID::ERROR_LABEL_OFFSET) + type);
  DCHECK(view);
  return static_cast<views::Label*>(view)->text();
}

PaymentRequestBrowserTestBase::DialogEventObserver::DialogEventObserver(
    std::list<PaymentRequestBrowserTestBase::DialogEvent> event_sequence)
    : events_(std::move(event_sequence)) {}
PaymentRequestBrowserTestBase::DialogEventObserver::~DialogEventObserver() {}

void PaymentRequestBrowserTestBase::DialogEventObserver::Wait() {
  if (events_.empty())
    return;

  DCHECK(!run_loop_.running());
  run_loop_.Run();
}

void PaymentRequestBrowserTestBase::DialogEventObserver::Observe(
    PaymentRequestBrowserTestBase::DialogEvent event) {
  if (events_.empty())
    return;

  DCHECK_EQ(events_.front(), event);
  events_.pop_front();
  // Only quit the loop if no other events are expected.
  if (events_.empty() && run_loop_.running())
    run_loop_.Quit();
}

void PaymentRequestBrowserTestBase::ResetEventObserver(DialogEvent event) {
  event_observer_ =
      base::MakeUnique<DialogEventObserver>(std::list<DialogEvent>{event});
}

void PaymentRequestBrowserTestBase::ResetEventObserverForSequence(
    std::list<DialogEvent> event_sequence) {
  event_observer_ =
      base::MakeUnique<DialogEventObserver>(std::move(event_sequence));
}

void PaymentRequestBrowserTestBase::WaitForObservedEvent() {
  event_observer_->Wait();
}

}  // namespace payments

std::ostream& operator<<(
    std::ostream& out,
    payments::PaymentRequestBrowserTestBase::DialogEvent event) {
  using DialogEvent = payments::PaymentRequestBrowserTestBase::DialogEvent;
  switch (event) {
    case DialogEvent::DIALOG_OPENED:
      out << "DIALOG_OPENED";
      break;
    case DialogEvent::DIALOG_CLOSED:
      out << "DIALOG_CLOSED";
      break;
    case DialogEvent::ORDER_SUMMARY_OPENED:
      out << "ORDER_SUMMARY_OPENED";
      break;
    case DialogEvent::PAYMENT_METHOD_OPENED:
      out << "PAYMENT_METHOD_OPENED";
      break;
    case DialogEvent::SHIPPING_ADDRESS_SECTION_OPENED:
      out << "SHIPPING_ADDRESS_SECTION_OPENED";
      break;
    case DialogEvent::SHIPPING_OPTION_SECTION_OPENED:
      out << "SHIPPING_OPTION_SECTION_OPENED";
      break;
    case DialogEvent::CREDIT_CARD_EDITOR_OPENED:
      out << "CREDIT_CARD_EDITOR_OPENED";
      break;
    case DialogEvent::SHIPPING_ADDRESS_EDITOR_OPENED:
      out << "SHIPPING_ADDRESS_EDITOR_OPENED";
      break;
    case DialogEvent::CONTACT_INFO_EDITOR_OPENED:
      out << "CONTACT_INFO_EDITOR_OPENED";
      break;
    case DialogEvent::BACK_NAVIGATION:
      out << "BACK_NAVIGATION";
      break;
    case DialogEvent::BACK_TO_PAYMENT_SHEET_NAVIGATION:
      out << "BACK_TO_PAYMENT_SHEET_NAVIGATION";
      break;
    case DialogEvent::CONTACT_INFO_OPENED:
      out << "CONTACT_INFO_OPENED";
      break;
    case DialogEvent::EDITOR_VIEW_UPDATED:
      out << "EDITOR_VIEW_UPDATED";
      break;
    case DialogEvent::CAN_MAKE_PAYMENT_CALLED:
      out << "CAN_MAKE_PAYMENT_CALLED";
      break;
    case DialogEvent::ERROR_MESSAGE_SHOWN:
      out << "ERROR_MESSAGE_SHOWN";
      break;
    case DialogEvent::SPEC_DONE_UPDATING:
      out << "SPEC_DONE_UPDATING";
      break;
    case DialogEvent::CVC_PROMPT_SHOWN:
      out << "CVC_PROMPT_SHOWN";
      break;
    case DialogEvent::NOT_SUPPORTED_ERROR:
      out << "NOT_SUPPORTED_ERROR";
      break;
    case DialogEvent::ABORT_CALLED:
      out << "ABORT_CALLED";
      break;
  }
  return out;
}
