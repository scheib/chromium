// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_PAYMENTS_PAYMENT_REQUEST_BROWSERTEST_BASE_H_
#define CHROME_BROWSER_UI_VIEWS_PAYMENTS_PAYMENT_REQUEST_BROWSERTEST_BASE_H_

#include <iosfwd>
#include <list>
#include <memory>
#include <string>
#include <vector>

#include "base/command_line.h"
#include "base/macros.h"
#include "base/run_loop.h"
#include "base/strings/string16.h"
#include "chrome/browser/ui/views/payments/payment_request_dialog_view.h"
#include "chrome/browser/ui/views/payments/test_chrome_payment_request_delegate.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "components/autofill/core/browser/field_types.h"
#include "components/autofill/core/browser/personal_data_manager_observer.h"
#include "components/payments/content/payment_request.h"
#include "components/payments/mojom/payment_request.mojom.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "testing/gmock/include/gmock/gmock.h"

namespace autofill {
class AutofillProfile;
class CreditCard;
}

namespace content {
class WebContents;
}  // namespace content

namespace service_manager {
struct BindSourceInfo;
}

namespace payments {

enum class DialogViewID;

ACTION_P(QuitMessageLoop, loop) {
  loop->Quit();
}

class PersonalDataLoadedObserverMock
    : public autofill::PersonalDataManagerObserver {
 public:
  PersonalDataLoadedObserverMock();
  ~PersonalDataLoadedObserverMock() override;

  MOCK_METHOD0(OnPersonalDataChanged, void());
};

// Base class for any interactive PaymentRequest test that will need to open
// the UI and interact with it.
class PaymentRequestBrowserTestBase
    : public InProcessBrowserTest,
      public PaymentRequest::ObserverForTest,
      public PaymentRequestDialogView::ObserverForTest {
 public:
  // Various events that can be waited on by the DialogEventObserver.
  enum DialogEvent : int {
    DIALOG_OPENED,
    DIALOG_CLOSED,
    ORDER_SUMMARY_OPENED,
    PAYMENT_METHOD_OPENED,
    SHIPPING_ADDRESS_SECTION_OPENED,
    SHIPPING_OPTION_SECTION_OPENED,
    CREDIT_CARD_EDITOR_OPENED,
    SHIPPING_ADDRESS_EDITOR_OPENED,
    CONTACT_INFO_EDITOR_OPENED,
    BACK_NAVIGATION,
    BACK_TO_PAYMENT_SHEET_NAVIGATION,
    CONTACT_INFO_OPENED,
    EDITOR_VIEW_UPDATED,
    CAN_MAKE_PAYMENT_CALLED,
    ERROR_MESSAGE_SHOWN,
    SPEC_DONE_UPDATING,
    CVC_PROMPT_SHOWN,
    NOT_SUPPORTED_ERROR,
    ABORT_CALLED,
  };

 protected:
  // Test will open a browser window to |test_file_path| (relative to
  // chrome/test/data/payments).
  explicit PaymentRequestBrowserTestBase(const std::string& test_file_path);
  ~PaymentRequestBrowserTestBase() override;

  void SetUpCommandLine(base::CommandLine* command_line) override;
  void SetUpOnMainThread() override;

  void NavigateTo(const std::string& file_path);

  void SetIncognito();
  void SetInvalidSsl();

  // PaymentRequest::ObserverForTest:
  void OnCanMakePaymentCalled() override;
  void OnNotSupportedError() override;
  void OnConnectionTerminated() override;
  void OnAbortCalled() override;

  // PaymentRequestDialogView::ObserverForTest:
  void OnDialogOpened() override;
  void OnOrderSummaryOpened() override;
  void OnPaymentMethodOpened() override;
  void OnShippingAddressSectionOpened() override;
  void OnShippingOptionSectionOpened() override;
  void OnCreditCardEditorOpened() override;
  void OnShippingAddressEditorOpened() override;
  void OnContactInfoEditorOpened() override;
  void OnBackNavigation() override;
  void OnBackToPaymentSheetNavigation() override;
  void OnContactInfoOpened() override;
  void OnEditorViewUpdated() override;
  void OnErrorMessageShown() override;
  void OnSpecDoneUpdating() override;
  void OnCvcPromptShown() override;

  // Will call JavaScript to invoke the PaymentRequest dialog and verify that
  // it's open.
  void InvokePaymentRequestUI();

  // Will expect that all strings in |expected_strings| are present in output.
  void ExpectBodyContains(const std::vector<std::string>& expected_strings);
  void ExpectBodyContains(const std::vector<base::string16>& expected_strings);

  // Utility functions that will click on Dialog views and wait for the
  // associated action to happen.
  void OpenOrderSummaryScreen();
  void OpenPaymentMethodScreen();
  void OpenShippingAddressSectionScreen();
  void OpenShippingOptionSectionScreen();
  void OpenContactInfoScreen();
  void OpenCreditCardEditorScreen();
  void OpenShippingAddressEditorScreen();
  void OpenContactInfoEditorScreen();
  void ClickOnBackArrow();
  void ClickOnCancel();

  content::WebContents* GetActiveWebContents();

  // Convenience method to get a list of PaymentRequest associated with
  // |web_contents|.
  const std::vector<PaymentRequest*> GetPaymentRequests(
      content::WebContents* web_contents);

  autofill::PersonalDataManager* GetDataManager();
  // Adds the various models to the database, waiting until the personal data
  // manager notifies that they are added.
  // NOTE: If no use_count is specified on the models and multiple items are
  // inserted, the order in which they are returned is undefined, since they
  // are added close to each other.
  void AddAutofillProfile(const autofill::AutofillProfile& profile);
  void AddCreditCard(const autofill::CreditCard& card);

  void CreatePaymentRequestForTest(
      content::WebContents* web_contents,
      const service_manager::BindSourceInfo& source_info,
      payments::mojom::PaymentRequestRequest request);

  // Click on a view from within the dialog and waits for an observed event
  // to be observed.
  void ClickOnDialogViewAndWait(DialogViewID view_id,
                                bool wait_for_animation = true);
  void ClickOnDialogViewAndWait(views::View* view,
                                bool wait_for_animation = true);
  void ClickOnChildInListViewAndWait(int child_index,
                                     int total_num_children,
                                     DialogViewID list_view_id,
                                     bool wait_for_animation = true);
  // Returns profile label values under |parent_view|.
  std::vector<base::string16> GetProfileLabelValues(
      DialogViewID parent_view_id);
  // Returns the shipping option labels under |parent_view_id|.
  std::vector<base::string16> GetShippingOptionLabelValues(
      DialogViewID parent_view_id);

  void OpenCVCPromptWithCVC(const base::string16& cvc);
  void PayWithCreditCardAndWait(const base::string16& cvc);

  // Getting/setting the |value| in the textfield of a given |type|.
  base::string16 GetEditorTextfieldValue(autofill::ServerFieldType type);
  void SetEditorTextfieldValue(const base::string16& value,
                               autofill::ServerFieldType type);
  // Getting/setting the |value| in the combobox of a given |type|.
  base::string16 GetComboboxValue(autofill::ServerFieldType type);
  void SetComboboxValue(const base::string16& value,
                        autofill::ServerFieldType type);
  // Special case for the billing address since the interesting value is not
  // the visible one accessible directly on the base combobox model.
  void SelectBillingAddress(const std::string& billing_address_id);

  // Whether the editor textfield/combobox for the given |type| is currently in
  // an invalid state.
  bool IsEditorTextfieldInvalid(autofill::ServerFieldType type);
  bool IsEditorComboboxInvalid(autofill::ServerFieldType type);

  bool IsPayButtonEnabled();

  // Sets proper animation delegates and waits for animation to finish.
  void WaitForAnimation();

  // Returns the text of the Label or StyledLabel with the specific |view_id|
  // that is a child of the Payment Request dialog view.
  const base::string16& GetLabelText(DialogViewID view_id);
  const base::string16& GetStyledLabelText(DialogViewID view_id);
  // Returns the error label text associated with a given field |type|.
  const base::string16& GetErrorLabelForType(autofill::ServerFieldType type);

  net::EmbeddedTestServer* https_server() { return https_server_.get(); }

  PaymentRequestDialogView* dialog_view() { return delegate_->dialog_view(); }

  void SetRegionDataLoader(autofill::RegionDataLoader* region_data_loader) {
    delegate_->SetRegionDataLoader(region_data_loader);
  }

  // DialogEventObserver is used to wait on specific events that may have
  // occured before the call to Wait(), or after, in which case a RunLoop is
  // used.
  //
  // Usage:
  // observer_ =
  // base::MakeUnique<DialogEventObserver>(std:list<DialogEvent>(...));
  //
  // Do stuff, which (a)synchronously calls observer_->Observe(...).
  //
  // observer_->Wait();  <- Will either return right away if events were
  //                     <- observed, or use a RunLoop's Run/Quit to wait.
  class DialogEventObserver {
   public:
    explicit DialogEventObserver(std::list<DialogEvent> event_sequence);
    ~DialogEventObserver();

    // Either returns right away if all events were observed between this
    // object's construction and this call to Wait(), or use a RunLoop to wait
    // for them.
    void Wait();

    // Observes and event (quits the RunLoop if we are done waiting).
    void Observe(DialogEvent event);

   private:
    std::list<DialogEvent> events_;
    base::RunLoop run_loop_;

    DISALLOW_COPY_AND_ASSIGN(DialogEventObserver);
  };

  // Resets the event observer for a given |event| or |event_sequence|.
  void ResetEventObserver(DialogEvent event);
  void ResetEventObserverForSequence(std::list<DialogEvent> event_sequence);
  // Wait for the event(s) passed to ResetEventObserver*() to occur.
  void WaitForObservedEvent();

 private:
  std::unique_ptr<DialogEventObserver> event_observer_;
  const std::string test_file_path_;
  std::unique_ptr<net::EmbeddedTestServer> https_server_;
  // Weak, owned by the PaymentRequest object.
  TestChromePaymentRequestDelegate* delegate_;
  bool is_incognito_;
  bool is_valid_ssl_;

  DISALLOW_COPY_AND_ASSIGN(PaymentRequestBrowserTestBase);
};

}  // namespace payments

std::ostream& operator<<(
    std::ostream& out,
    payments::PaymentRequestBrowserTestBase::DialogEvent event);

#endif  // CHROME_BROWSER_UI_VIEWS_PAYMENTS_PAYMENT_REQUEST_BROWSERTEST_BASE_H_
