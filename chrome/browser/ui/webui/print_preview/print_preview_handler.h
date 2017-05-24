// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_PRINT_PREVIEW_PRINT_PREVIEW_HANDLER_H_
#define CHROME_BROWSER_UI_WEBUI_PRINT_PREVIEW_PRINT_PREVIEW_HANDLER_H_

#include <memory>
#include <string>

#include "base/files/file_path.h"
#include "base/gtest_prod_util.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "chrome/common/features.h"
#include "components/signin/core/browser/gaia_cookie_manager_service.h"
#include "content/public/browser/web_ui_message_handler.h"
#include "printing/backend/print_backend.h"
#include "printing/features/features.h"
#include "ui/shell_dialogs/select_file_dialog.h"

#if BUILDFLAG(ENABLE_SERVICE_DISCOVERY)
#include "chrome/browser/local_discovery/service_discovery_shared_client.h"
#include "chrome/browser/printing/cloud_print/privet_local_printer_lister.h"
#endif

class PrinterHandler;
class PrintPreviewUI;

namespace base {
class DictionaryValue;
class RefCountedBytes;
}

namespace content {
class WebContents;
}

namespace gfx {
class Size;
}

namespace printing {
class PrinterBackendProxy;
}

// The handler for Javascript messages related to the print preview dialog.
class PrintPreviewHandler
    : public content::WebUIMessageHandler,
#if BUILDFLAG(ENABLE_SERVICE_DISCOVERY)
      public cloud_print::PrivetLocalPrinterLister::Delegate,
      public cloud_print::PrivetLocalPrintOperation::Delegate,
#endif
      public ui::SelectFileDialog::Listener,
      public GaiaCookieManagerService::Observer {
 public:
  PrintPreviewHandler();
  ~PrintPreviewHandler() override;

  // WebUIMessageHandler implementation.
  void RegisterMessages() override;

  // SelectFileDialog::Listener implementation.
  void FileSelected(const base::FilePath& path,
                    int index,
                    void* params) override;
  void FileSelectionCanceled(void* params) override;

  // GaiaCookieManagerService::Observer implementation.
  void OnAddAccountToCookieCompleted(
      const std::string& account_id,
      const GoogleServiceAuthError& error) override;

  // Called when print preview failed.
  void OnPrintPreviewFailed();

#if BUILDFLAG(ENABLE_BASIC_PRINTING)
  // Called when the user press ctrl+shift+p to display the native system
  // dialog.
  void ShowSystemDialog();
#endif  // BUILDFLAG(ENABLE_BASIC_PRINTING)

#if BUILDFLAG(ENABLE_SERVICE_DISCOVERY)
  // PrivetLocalPrinterLister::Delegate implementation.
  void LocalPrinterChanged(
      const std::string& name,
      bool has_local_printing,
      const cloud_print::DeviceDescription& description) override;
  void LocalPrinterRemoved(const std::string& name) override;
  void LocalPrinterCacheFlushed() override;

  // PrivetLocalPrintOperation::Delegate implementation.
  void OnPrivetPrintingDone(
      const cloud_print::PrivetLocalPrintOperation* print_operation) override;
  void OnPrivetPrintingError(
      const cloud_print::PrivetLocalPrintOperation* print_operation,
      int http_code) override;
#endif

  int regenerate_preview_request_count() const {
    return regenerate_preview_request_count_;
  }

  // Sets |pdf_file_saved_closure_| to |closure|.
  void SetPdfSavedClosureForTesting(const base::Closure& closure);

 protected:
  // If |prompt_user| is true, displays a modal dialog, prompting the user to
  // select a file. Otherwise, just accept |default_path| and uniquify it.
  // Protected so unit tests can access.
  virtual void SelectFile(const base::FilePath& default_path, bool prompt_user);

  // Handles printing to PDF. Protected to expose to unit tests.
  void PrintToPdf();

  // The underlying dialog object. Protected to expose to unit tests.
  scoped_refptr<ui::SelectFileDialog> select_file_dialog_;

 private:
  friend class PrintPreviewPdfGeneratedBrowserTest;
  FRIEND_TEST_ALL_PREFIXES(PrintPreviewPdfGeneratedBrowserTest,
                           MANUAL_DummyTest);
  class AccessTokenService;

  content::WebContents* preview_web_contents() const;

  PrintPreviewUI* print_preview_ui() const;

  printing::PrinterBackendProxy* printer_backend_proxy();

  // Gets the list of printers. |args| is unused.
  void HandleGetPrinters(const base::ListValue* args);

  // Starts getting all local privet printers. |args| is unused.
  void HandleGetPrivetPrinters(const base::ListValue* args);

  // Starts getting all local extension managed printers. |args| is unused.
  void HandleGetExtensionPrinters(const base::ListValue* args);

  // Grants an extension access to a provisional printer.  First element of
  // |args| is the provisional printer ID.
  void HandleGrantExtensionPrinterAccess(const base::ListValue* args);

  // Stops getting all local privet printers. |arg| is unused.
  void HandleStopGetPrivetPrinters(const base::ListValue* args);

  // Asks the initiator renderer to generate a preview.  First element of |args|
  // is a job settings JSON string.
  void HandleGetPreview(const base::ListValue* args);

  // Gets the job settings from Web UI and initiate printing. First element of
  // |args| is a job settings JSON string.
  void HandlePrint(const base::ListValue* args);

  // Handles the request to hide the preview dialog for printing.
  // |args| is unused.
  void HandleHidePreview(const base::ListValue* args);

  // Handles the request to cancel the pending print request. |args| is unused.
  void HandleCancelPendingPrintRequest(const base::ListValue* args);

  // Handles a request to store data that the web ui wishes to persist.
  // First element of |args| is the data to persist.
  void HandleSaveAppState(const base::ListValue* args);

  // Gets the printer capabilities. First element of |args| is the printer name.
  void HandleGetPrinterCapabilities(const base::ListValue* args);

  // Performs printer setup. First element of |args| is the printer name.
  void HandlePrinterSetup(const base::ListValue* args);

#if BUILDFLAG(ENABLE_BASIC_PRINT_DIALOG)
  // Asks the initiator renderer to show the native print system dialog. |args|
  // is unused.
  void HandleShowSystemDialog(const base::ListValue* args);
#endif

  // Callback for the signin dialog to call once signin is complete.
  void OnSigninComplete();

  // Brings up a dialog to allow the user to sign into cloud print.
  // |args| is unused.
  void HandleSignin(const base::ListValue* args);

  // Generates new token and sends back to UI.
  void HandleGetAccessToken(const base::ListValue* args);

  // Brings up a web page to allow the user to configure cloud print.
  // |args| is unused.
  void HandleManageCloudPrint(const base::ListValue* args);

  // Gathers UMA stats when the print preview dialog is about to close.
  // |args| is unused.
  void HandleClosePreviewDialog(const base::ListValue* args);

  // Asks the browser to show the native printer management dialog.
  // |args| is unused.
  void HandleManagePrinters(const base::ListValue* args);

  // Asks the browser for several settings that are needed before the first
  // preview is displayed.
  void HandleGetInitialSettings(const base::ListValue* args);

  // Reports histogram data for a print preview UI action. |args| should consist
  // of two elements: the bucket name, and the bucket event.
  void HandleReportUiEvent(const base::ListValue* args);

  // Forces the opening of a new tab. |args| should consist of one element: the
  // URL to set the new tab to.
  //
  // NOTE: This is needed to open register promo for Cloud Print as a new tab.
  // Javascript's "window.open" opens a new window popup (since initiated from
  // async HTTP request) and worse yet, on Windows and Chrome OS, the opened
  // window opens behind the initiator window.
  void HandleForceOpenNewTab(const base::ListValue* args);

  void HandleGetPrivetPrinterCapabilities(const base::ListValue* arg);

  // Requests an extension managed printer's capabilities.
  // |arg| contains the ID of the printer whose capabilities are requested.
  void HandleGetExtensionPrinterCapabilities(const base::ListValue* args);

  void SendInitialSettings(const std::string& callback_id,
                           const std::string& default_printer);

  // Send OAuth2 access token.
  void SendAccessToken(const std::string& type,
                       const std::string& access_token);

  // Sends the printer capabilities to the Web UI. |settings_info| contains
  // printer capabilities information. If |settings_info| is empty, sends
  // error notification to the Web UI instead.
  void SendPrinterCapabilities(
      const std::string& printer_name,
      std::unique_ptr<base::DictionaryValue> settings_info);

  // Send the result of performing printer setup. |settings_info| contains
  // printer capabilities.
  void SendPrinterSetup(const std::string& callback_id,
                        const std::string& printer_name,
                        std::unique_ptr<base::DictionaryValue> settings_info);

  // Send the list of printers to the Web UI.
  void SetupPrinterList(const printing::PrinterList& printer_list);

  // Send whether cloud print integration should be enabled.
  void SendCloudPrintEnabled();

  // Send the PDF data to the cloud to print.
  void SendCloudPrintJob(const base::RefCountedBytes* data);

  // Gets the initiator for the print preview dialog.
  content::WebContents* GetInitiator() const;

  // Closes the preview dialog.
  void ClosePreviewDialog();

  // Adds all the recorded stats taken so far to histogram counts.
  void ReportStats();

  // Clears initiator details for the print preview dialog.
  void ClearInitiatorDetails();

  // Posts a task to save |data| to pdf at |print_to_pdf_path_|.
  void PostPrintToPdfTask();

  // Populates |settings| according to the current locale.
  void GetNumberFormatAndMeasurementSystem(base::DictionaryValue* settings);

  bool GetPreviewDataAndTitle(scoped_refptr<base::RefCountedBytes>* data,
                              base::string16* title) const;

  // Helper for getting a unique file name for SelectFile() without prompting
  // the user. Just an adaptor for FileSelected().
  void OnGotUniqueFileName(const base::FilePath& path);

#if defined(USE_CUPS)
  void SaveCUPSColorSetting(const base::DictionaryValue* settings);

  void ConvertColorSettingToCUPSColorModel(
      base::DictionaryValue* settings) const;
#endif

#if BUILDFLAG(ENABLE_SERVICE_DISCOVERY)
  void StartPrivetLister(const scoped_refptr<
      local_discovery::ServiceDiscoverySharedClient>& client);
  void OnPrivetCapabilities(const base::DictionaryValue* capabilities);
  void PrivetCapabilitiesUpdateClient(
      std::unique_ptr<cloud_print::PrivetHTTPClient> http_client);
  void PrivetLocalPrintUpdateClient(
      std::string print_ticket,
      std::string capabilities,
      gfx::Size page_size,
      std::unique_ptr<cloud_print::PrivetHTTPClient> http_client);
  bool PrivetUpdateClient(
      std::unique_ptr<cloud_print::PrivetHTTPClient> http_client);
  void StartPrivetLocalPrint(const std::string& print_ticket,
                             const std::string& capabilities,
                             const gfx::Size& page_size);
  void SendPrivetCapabilitiesError(const std::string& id);
  void PrintToPrivetPrinter(const std::string& printer_name,
                            const std::string& print_ticket,
                            const std::string& capabilities,
                            const gfx::Size& page_size);
  bool CreatePrivetHTTP(
      const std::string& name,
      const cloud_print::PrivetHTTPAsynchronousFactory::ResultCallback&
          callback);
  void FillPrinterDescription(
      const std::string& name,
      const cloud_print::DeviceDescription& description,
      bool has_local_printing,
      base::DictionaryValue* printer_value);
#endif

  // Lazily creates |extension_printer_handler_| that can be used to handle
  // extension printers requests.
  void EnsureExtensionPrinterHandlerSet();

  // Called when a list of printers is reported by an extension.
  // |printers|: The list of printers managed by the extension.
  // |done|: Whether all the extensions have reported the list of printers
  //     they manage.
  void OnGotPrintersForExtension(const base::ListValue& printers, bool done);

  // Called when an extension reports information requested for a provisional
  // printer.
  // |printer_id|: The provisional printer id.
  // |printer_info|: The data reported by the extension.
  void OnGotExtensionPrinterInfo(const std::string& printer_id,
                                 const base::DictionaryValue& printer_info);

  // Called when an extension reports the set of print capabilites for a
  // printer.
  // |printer_id|: The id of the printer whose capabilities are reported.
  // |capabilities|: The printer capabilities.
  void OnGotExtensionPrinterCapabilities(
      const std::string& printer_id,
      const base::DictionaryValue& capabilities);

  // Called when an extension print job is completed.
  // |success|: Whether the job succeeded.
  // |status|: The returned print job status. Useful for reporting a specific
  //     error.
  void OnExtensionPrintResult(bool success, const std::string& status);

  // Register/unregister from notifications of changes done to the GAIA
  // cookie.
  void RegisterForGaiaCookieChanges();
  void UnregisterForGaiaCookieChanges();

  // A count of how many requests received to regenerate preview data.
  // Initialized to 0 then incremented and emitted to a histogram.
  int regenerate_preview_request_count_;

  // A count of how many requests received to show manage printers dialog.
  int manage_printers_dialog_request_count_;
  int manage_cloud_printers_dialog_request_count_;

  // Whether we have already logged a failed print preview.
  bool reported_failed_preview_;

  // Whether we have already logged the number of printers this session.
  bool has_logged_printers_count_;

  // Holds the path to the print to pdf request. It is empty if no such request
  // exists.
  base::FilePath print_to_pdf_path_;

  // Holds token service to get OAuth2 access tokens.
  std::unique_ptr<AccessTokenService> token_service_;

  // Pointer to cookie manager service so that print preview can listen for GAIA
  // cookie changes.
  GaiaCookieManagerService* gaia_cookie_manager_service_;

#if BUILDFLAG(ENABLE_SERVICE_DISCOVERY)
  scoped_refptr<local_discovery::ServiceDiscoverySharedClient>
      service_discovery_client_;
  std::unique_ptr<cloud_print::PrivetLocalPrinterLister> printer_lister_;

  std::unique_ptr<cloud_print::PrivetHTTPAsynchronousFactory>
      privet_http_factory_;
  std::unique_ptr<cloud_print::PrivetHTTPResolution> privet_http_resolution_;
  std::unique_ptr<cloud_print::PrivetV1HTTPClient> privet_http_client_;
  std::unique_ptr<cloud_print::PrivetJSONOperation>
      privet_capabilities_operation_;
  std::unique_ptr<cloud_print::PrivetLocalPrintOperation>
      privet_local_print_operation_;
#endif

  // Handles requests for extension printers. Created lazily by calling
  // |EnsureExtensionPrinterHandlerSet|.
  std::unique_ptr<PrinterHandler> extension_printer_handler_;

  // Notifies tests that want to know if the PDF has been saved. This doesn't
  // notify the test if it was a successful save, only that it was attempted.
  base::Closure pdf_file_saved_closure_;

  // Proxy for calls to the print backend.  Lazily initialized since web_ui() is
  // not available at construction time.
  std::unique_ptr<printing::PrinterBackendProxy> printer_backend_proxy_;

  base::WeakPtrFactory<PrintPreviewHandler> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(PrintPreviewHandler);
};

#endif  // CHROME_BROWSER_UI_WEBUI_PRINT_PREVIEW_PRINT_PREVIEW_HANDLER_H_
