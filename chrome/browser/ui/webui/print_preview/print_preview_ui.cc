// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/print_preview/print_preview_ui.h"

#include <map>
#include <utility>
#include <vector>

#include "base/feature_list.h"
#include "base/id_map.h"
#include "base/lazy_instance.h"
#include "base/macros.h"
#include "base/memory/ptr_util.h"
#include "base/memory/ref_counted_memory.h"
#include "base/metrics/histogram_macros.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/synchronization/lock.h"
#include "base/values.h"
#include "build/build_config.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/printing/background_printing_manager.h"
#include "chrome/browser/printing/print_preview_data_service.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/webui/metrics_handler.h"
#include "chrome/browser/ui/webui/print_preview/print_preview_handler.h"
#include "chrome/browser/ui/webui/theme_source.h"
#include "chrome/common/chrome_features.h"
#include "chrome/common/url_constants.h"
#include "chrome/grit/browser_resources.h"
#include "chrome/grit/chromium_strings.h"
#include "chrome/grit/generated_resources.h"
#include "components/printing/common/print_messages.h"
#include "components/strings/grit/components_strings.h"
#include "content/public/browser/url_data_source.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui_data_source.h"
#include "extensions/common/constants.h"
#include "printing/features/features.h"
#include "printing/page_size_margins.h"
#include "printing/print_job_constants.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/web_dialogs/web_dialog_delegate.h"
#include "ui/web_dialogs/web_dialog_ui.h"

#if defined(OS_CHROMEOS)
#include "base/command_line.h"
#include "chrome/common/chrome_switches.h"
#endif

using content::WebContents;
using printing::PageSizeMargins;

namespace {

#if defined(OS_MACOSX)
// U+0028 U+21E7 U+2318 U+0050 U+0029 in UTF8
const char kBasicPrintShortcut[] = "\x28\xE2\x8c\xA5\xE2\x8C\x98\x50\x29";
#elif !defined(OS_CHROMEOS)
const char kBasicPrintShortcut[] = "(Ctrl+Shift+P)";
#endif

PrintPreviewUI::TestingDelegate* g_testing_delegate = nullptr;

// Thread-safe wrapper around a std::map to keep track of mappings from
// PrintPreviewUI IDs to most recent print preview request IDs.
class PrintPreviewRequestIdMapWithLock {
 public:
  PrintPreviewRequestIdMapWithLock() {}
  ~PrintPreviewRequestIdMapWithLock() {}

  // Gets the value for |preview_id|.
  // Returns true and sets |out_value| on success.
  bool Get(int32_t preview_id, int* out_value) {
    base::AutoLock lock(lock_);
    PrintPreviewRequestIdMap::const_iterator it = map_.find(preview_id);
    if (it == map_.end())
      return false;
    *out_value = it->second;
    return true;
  }

  // Sets the |value| for |preview_id|.
  void Set(int32_t preview_id, int value) {
    base::AutoLock lock(lock_);
    map_[preview_id] = value;
  }

  // Erases the entry for |preview_id|.
  void Erase(int32_t preview_id) {
    base::AutoLock lock(lock_);
    map_.erase(preview_id);
  }

 private:
  // Mapping from PrintPreviewUI ID to print preview request ID.
  typedef std::map<int, int> PrintPreviewRequestIdMap;

  PrintPreviewRequestIdMap map_;
  base::Lock lock_;

  DISALLOW_COPY_AND_ASSIGN(PrintPreviewRequestIdMapWithLock);
};

// Written to on the UI thread, read from any thread.
base::LazyInstance<PrintPreviewRequestIdMapWithLock>::DestructorAtExit
    g_print_preview_request_id_map = LAZY_INSTANCE_INITIALIZER;

// PrintPreviewUI IDMap used to avoid exposing raw pointer addresses to WebUI.
// Only accessed on the UI thread.
base::LazyInstance<IDMap<PrintPreviewUI*>>::DestructorAtExit
    g_print_preview_ui_id_map = LAZY_INSTANCE_INITIALIZER;

// PrintPreviewUI serves data for chrome://print requests.
//
// The format for requesting PDF data is as follows:
// chrome://print/<PrintPreviewUIID>/<PageIndex>/print.pdf
//
// Parameters (< > required):
//    <PrintPreviewUIID> = PrintPreview UI ID
//    <PageIndex> = Page index is zero-based or
//                  |printing::COMPLETE_PREVIEW_DOCUMENT_INDEX| to represent
//                  a print ready PDF.
//
// Example:
//    chrome://print/123/10/print.pdf
//
// Requests to chrome://print with paths not ending in /print.pdf are used
// to return the markup or other resources for the print preview page itself.
bool HandleRequestCallback(
    const std::string& path,
    const content::WebUIDataSource::GotDataCallback& callback) {
  // ChromeWebUIDataSource handles most requests except for the print preview
  // data.
  std::string file_path = path.substr(0, path.find_first_of('?'));
  if (!base::EndsWith(file_path, "/print.pdf", base::CompareCase::SENSITIVE))
    return false;

  // Print Preview data.
  scoped_refptr<base::RefCountedBytes> data;
  std::vector<std::string> url_substr = base::SplitString(
      path, "/", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);
  int preview_ui_id = -1;
  int page_index = 0;
  if (url_substr.size() == 3 &&
      base::StringToInt(url_substr[0], &preview_ui_id),
      base::StringToInt(url_substr[1], &page_index) &&
      preview_ui_id >= 0) {
    PrintPreviewDataService::GetInstance()->GetDataEntry(
        preview_ui_id, page_index, &data);
  }
  if (data.get()) {
    callback.Run(data.get());
    return true;
  }
  // Invalid request.
  scoped_refptr<base::RefCountedBytes> empty_bytes(new base::RefCountedBytes);
  callback.Run(empty_bytes.get());
  return true;
}

content::WebUIDataSource* CreatePrintPreviewUISource() {
  content::WebUIDataSource* source =
      content::WebUIDataSource::Create(chrome::kChromeUIPrintHost);
#if defined(OS_CHROMEOS)
  source->AddLocalizedString("title",
                             IDS_PRINT_PREVIEW_GOOGLE_CLOUD_PRINT_TITLE);
#else
  source->AddLocalizedString("title", IDS_PRINT_PREVIEW_TITLE);
#endif
  source->AddLocalizedString("loading", IDS_PRINT_PREVIEW_LOADING);
  source->AddLocalizedString("noPlugin", IDS_PRINT_PREVIEW_NO_PLUGIN);
  source->AddLocalizedString("launchNativeDialog",
                             IDS_PRINT_PREVIEW_NATIVE_DIALOG);
  source->AddLocalizedString("previewFailed", IDS_PRINT_PREVIEW_FAILED);
  source->AddLocalizedString("invalidPrinterSettings",
                             IDS_PRINT_INVALID_PRINTER_SETTINGS);
  source->AddLocalizedString("printButton", IDS_PRINT_PREVIEW_PRINT_BUTTON);
  source->AddLocalizedString("saveButton", IDS_PRINT_PREVIEW_SAVE_BUTTON);
  source->AddLocalizedString("printing", IDS_PRINT_PREVIEW_PRINTING);
  source->AddLocalizedString("saving", IDS_PRINT_PREVIEW_SAVING);
  source->AddLocalizedString("printingToPDFInProgress",
                             IDS_PRINT_PREVIEW_PRINTING_TO_PDF_IN_PROGRESS);
#if defined(OS_MACOSX)
  source->AddLocalizedString("openingPDFInPreview",
                             IDS_PRINT_PREVIEW_OPENING_PDF_IN_PREVIEW);
#endif
  source->AddLocalizedString("destinationLabel",
                             IDS_PRINT_PREVIEW_DESTINATION_LABEL);
  source->AddLocalizedString("copiesLabel", IDS_PRINT_PREVIEW_COPIES_LABEL);
  source->AddLocalizedString("scalingLabel", IDS_PRINT_PREVIEW_SCALING_LABEL);
  source->AddLocalizedString("examplePageRangeText",
                             IDS_PRINT_PREVIEW_EXAMPLE_PAGE_RANGE_TEXT);
  source->AddLocalizedString("layoutLabel", IDS_PRINT_PREVIEW_LAYOUT_LABEL);
  source->AddLocalizedString("optionAllPages",
                             IDS_PRINT_PREVIEW_OPTION_ALL_PAGES);
  source->AddLocalizedString("optionBw", IDS_PRINT_PREVIEW_OPTION_BW);
  source->AddLocalizedString("optionCollate", IDS_PRINT_PREVIEW_OPTION_COLLATE);
  source->AddLocalizedString("optionColor", IDS_PRINT_PREVIEW_OPTION_COLOR);
  source->AddLocalizedString("optionLandscape",
                             IDS_PRINT_PREVIEW_OPTION_LANDSCAPE);
  source->AddLocalizedString("optionPortrait",
                             IDS_PRINT_PREVIEW_OPTION_PORTRAIT);
  source->AddLocalizedString("optionTwoSided",
                             IDS_PRINT_PREVIEW_OPTION_TWO_SIDED);
  source->AddLocalizedString("pagesLabel", IDS_PRINT_PREVIEW_PAGES_LABEL);
  source->AddLocalizedString("pageRangeTextBox",
                             IDS_PRINT_PREVIEW_PAGE_RANGE_TEXT);
  source->AddLocalizedString("pageRangeRadio",
                             IDS_PRINT_PREVIEW_PAGE_RANGE_RADIO);
  source->AddLocalizedString("printToPDF", IDS_PRINT_PREVIEW_PRINT_TO_PDF);
  source->AddLocalizedString("printPreviewSummaryFormatShort",
                             IDS_PRINT_PREVIEW_SUMMARY_FORMAT_SHORT);
  source->AddLocalizedString("printPreviewSummaryFormatLong",
                             IDS_PRINT_PREVIEW_SUMMARY_FORMAT_LONG);
  source->AddLocalizedString("printPreviewSheetsLabelSingular",
                             IDS_PRINT_PREVIEW_SHEETS_LABEL_SINGULAR);
  source->AddLocalizedString("printPreviewSheetsLabelPlural",
                             IDS_PRINT_PREVIEW_SHEETS_LABEL_PLURAL);
  source->AddLocalizedString("printPreviewPageLabelSingular",
                             IDS_PRINT_PREVIEW_PAGE_LABEL_SINGULAR);
  source->AddLocalizedString("printPreviewPageLabelPlural",
                             IDS_PRINT_PREVIEW_PAGE_LABEL_PLURAL);
  source->AddLocalizedString("selectButton",
                             IDS_PRINT_PREVIEW_BUTTON_SELECT);
  source->AddLocalizedString("goBackButton",
                             IDS_PRINT_PREVIEW_BUTTON_GO_BACK);
  source->AddLocalizedString(
      "resolveExtensionUSBPermissionMessage",
      IDS_PRINT_PREVIEW_RESOLVE_EXTENSION_USB_PERMISSION_MESSAGE);
  source->AddLocalizedString(
      "resolveExtensionUSBErrorMessage",
      IDS_PRINT_PREVIEW_RESOLVE_EXTENSION_USB_ERROR_MESSAGE);
#if defined(OS_CHROMEOS)
  source->AddLocalizedString("configuringInProgressText",
                             IDS_PRINT_CONFIGURING_IN_PROGRESS_TEXT);
  source->AddLocalizedString("configuringFailedText",
                             IDS_PRINT_CONFIGURING_FAILED_TEXT);
#else
  const base::string16 shortcut_text(base::UTF8ToUTF16(kBasicPrintShortcut));
  source->AddString(
      "systemDialogOption",
      l10n_util::GetStringFUTF16(
          IDS_PRINT_PREVIEW_SYSTEM_DIALOG_OPTION,
          shortcut_text));
#endif
#if defined(OS_MACOSX)
  source->AddLocalizedString("openPdfInPreviewOption",
                             IDS_PRINT_PREVIEW_OPEN_PDF_IN_PREVIEW_APP);
#endif
  source->AddString(
      "printWithCloudPrintWait",
      l10n_util::GetStringFUTF16(
          IDS_PRINT_PREVIEW_PRINT_WITH_CLOUD_PRINT_WAIT,
          l10n_util::GetStringUTF16(IDS_GOOGLE_CLOUD_PRINT)));
  source->AddString(
      "noDestsPromoLearnMoreUrl",
      chrome::kCloudPrintNoDestinationsLearnMoreURL);
  source->AddLocalizedString("pageRangeLimitInstruction",
                             IDS_PRINT_PREVIEW_PAGE_RANGE_LIMIT_INSTRUCTION);
  source->AddLocalizedString(
      "pageRangeLimitInstructionWithValue",
      IDS_PRINT_PREVIEW_PAGE_RANGE_LIMIT_INSTRUCTION_WITH_VALUE);
  source->AddLocalizedString("pageRangeSyntaxInstruction",
                             IDS_PRINT_PREVIEW_PAGE_RANGE_SYNTAX_INSTRUCTION);
  source->AddLocalizedString("copiesInstruction",
                             IDS_PRINT_PREVIEW_COPIES_INSTRUCTION);
  source->AddLocalizedString("scalingInstruction",
                             IDS_PRINT_PREVIEW_SCALING_INSTRUCTION);
  source->AddLocalizedString("printPagesLabel",
                             IDS_PRINT_PREVIEW_PRINT_PAGES_LABEL);
  source->AddLocalizedString("optionsLabel", IDS_PRINT_PREVIEW_OPTIONS_LABEL);
  source->AddLocalizedString("optionHeaderFooter",
                             IDS_PRINT_PREVIEW_OPTION_HEADER_FOOTER);
  source->AddLocalizedString("optionFitToPage",
                             IDS_PRINT_PREVIEW_OPTION_FIT_TO_PAGE);
  source->AddLocalizedString(
      "optionBackgroundColorsAndImages",
      IDS_PRINT_PREVIEW_OPTION_BACKGROUND_COLORS_AND_IMAGES);
  source->AddLocalizedString("optionSelectionOnly",
                             IDS_PRINT_PREVIEW_OPTION_SELECTION_ONLY);
  source->AddLocalizedString("optionRasterize",
                             IDS_PRINT_PREVIEW_OPTION_RASTERIZE);
  source->AddLocalizedString("marginsLabel", IDS_PRINT_PREVIEW_MARGINS_LABEL);
  source->AddLocalizedString("defaultMargins",
                             IDS_PRINT_PREVIEW_DEFAULT_MARGINS);
  source->AddLocalizedString("noMargins", IDS_PRINT_PREVIEW_NO_MARGINS);
  source->AddLocalizedString("customMargins", IDS_PRINT_PREVIEW_CUSTOM_MARGINS);
  source->AddLocalizedString("minimumMargins",
                             IDS_PRINT_PREVIEW_MINIMUM_MARGINS);
  source->AddLocalizedString("top", IDS_PRINT_PREVIEW_TOP_MARGIN_LABEL);
  source->AddLocalizedString("bottom", IDS_PRINT_PREVIEW_BOTTOM_MARGIN_LABEL);
  source->AddLocalizedString("left", IDS_PRINT_PREVIEW_LEFT_MARGIN_LABEL);
  source->AddLocalizedString("right", IDS_PRINT_PREVIEW_RIGHT_MARGIN_LABEL);
  source->AddLocalizedString("mediaSizeLabel",
                             IDS_PRINT_PREVIEW_MEDIA_SIZE_LABEL);
  source->AddLocalizedString("dpiLabel", IDS_PRINT_PREVIEW_DPI_LABEL);
  source->AddLocalizedString("dpiItemLabel", IDS_PRINT_PREVIEW_DPI_ITEM_LABEL);
  source->AddLocalizedString("nonIsotropicDpiItemLabel",
                             IDS_PRINT_PREVIEW_NON_ISOTROPIC_DPI_ITEM_LABEL);
  source->AddLocalizedString("destinationSearchTitle",
                             IDS_PRINT_PREVIEW_DESTINATION_SEARCH_TITLE);
  source->AddLocalizedString("accountSelectTitle",
                             IDS_PRINT_PREVIEW_ACCOUNT_SELECT_TITLE);
  source->AddLocalizedString("addAccountTitle",
                             IDS_PRINT_PREVIEW_ADD_ACCOUNT_TITLE);
  source->AddLocalizedString("cloudPrintPromotion",
                             IDS_PRINT_PREVIEW_CLOUD_PRINT_PROMOTION);
  source->AddLocalizedString("searchBoxPlaceholder",
                             IDS_PRINT_PREVIEW_SEARCH_BOX_PLACEHOLDER);
  source->AddLocalizedString("noDestinationsMessage",
                             IDS_PRINT_PREVIEW_NO_DESTINATIONS_MESSAGE);
  source->AddLocalizedString("showAllButtonText",
                             IDS_PRINT_PREVIEW_SHOW_ALL_BUTTON_TEXT);
  source->AddLocalizedString("destinationCount",
                             IDS_PRINT_PREVIEW_DESTINATION_COUNT);
  source->AddLocalizedString("recentDestinationsTitle",
                             IDS_PRINT_PREVIEW_RECENT_DESTINATIONS_TITLE);
  source->AddLocalizedString("localDestinationsTitle",
                             IDS_PRINT_PREVIEW_LOCAL_DESTINATIONS_TITLE);
  source->AddLocalizedString("cloudDestinationsTitle",
                             IDS_PRINT_PREVIEW_CLOUD_DESTINATIONS_TITLE);
  source->AddLocalizedString("manage", IDS_PRINT_PREVIEW_MANAGE);
  source->AddLocalizedString("setupCloudPrinters",
                             IDS_PRINT_PREVIEW_SETUP_CLOUD_PRINTERS);
  source->AddLocalizedString("changeDestination",
                             IDS_PRINT_PREVIEW_CHANGE_DESTINATION);
  source->AddLocalizedString("offlineForYear",
                             IDS_PRINT_PREVIEW_OFFLINE_FOR_YEAR);
  source->AddLocalizedString("offlineForMonth",
                             IDS_PRINT_PREVIEW_OFFLINE_FOR_MONTH);
  source->AddLocalizedString("offlineForWeek",
                             IDS_PRINT_PREVIEW_OFFLINE_FOR_WEEK);
  source->AddLocalizedString("offline", IDS_PRINT_PREVIEW_OFFLINE);
  source->AddLocalizedString("noDestsPromoTitle",
                             IDS_PRINT_PREVIEW_NO_DESTS_PROMO_TITLE);
  source->AddLocalizedString("noDestsPromoBody",
                             IDS_PRINT_PREVIEW_NO_DESTS_PROMO_BODY);
  source->AddLocalizedString("noDestsPromoGcpDesc",
                             IDS_PRINT_PREVIEW_NO_DESTS_GCP_DESC);
  source->AddLocalizedString("learnMore",
                             IDS_LEARN_MORE);
  source->AddLocalizedString(
      "noDestsPromoAddPrinterButtonLabel",
      IDS_PRINT_PREVIEW_NO_DESTS_PROMO_ADD_PRINTER_BUTTON_LABEL);
  source->AddLocalizedString("noDestsPromoNotNowButtonLabel", IDS_NOT_NOW);
  source->AddLocalizedString("couldNotPrint",
                             IDS_PRINT_PREVIEW_COULD_NOT_PRINT);
  source->AddLocalizedString("registerPromoButtonText",
                             IDS_PRINT_PREVIEW_REGISTER_PROMO_BUTTON_TEXT);
  source->AddLocalizedString(
      "extensionDestinationIconTooltip",
      IDS_PRINT_PREVIEW_EXTENSION_DESTINATION_ICON_TOOLTIP);
  source->AddLocalizedString(
      "advancedSettingsSearchBoxPlaceholder",
      IDS_PRINT_PREVIEW_ADVANCED_SETTINGS_SEARCH_BOX_PLACEHOLDER);
  source->AddLocalizedString("advancedSettingsDialogTitle",
                             IDS_PRINT_PREVIEW_ADVANCED_SETTINGS_DIALOG_TITLE);
  source->AddLocalizedString(
      "noAdvancedSettingsMatchSearchHint",
      IDS_PRINT_PREVIEW_NO_ADVANCED_SETTINGS_MATCH_SEARCH_HINT);
  source->AddLocalizedString(
      "advancedSettingsDialogConfirm",
      IDS_PRINT_PREVIEW_ADVANCED_SETTINGS_DIALOG_CONFIRM);
  source->AddLocalizedString("cancel", IDS_CANCEL);
  source->AddLocalizedString("advancedOptionsLabel",
                             IDS_PRINT_PREVIEW_ADVANCED_OPTIONS_LABEL);
  source->AddLocalizedString("showAdvancedOptions",
                             IDS_PRINT_PREVIEW_SHOW_ADVANCED_OPTIONS);

  source->AddLocalizedString("accept", IDS_PRINT_PREVIEW_ACCEPT_INVITE);
  source->AddLocalizedString(
      "acceptForGroup", IDS_PRINT_PREVIEW_ACCEPT_GROUP_INVITE);
  source->AddLocalizedString("reject", IDS_PRINT_PREVIEW_REJECT_INVITE);
  source->AddLocalizedString(
      "groupPrinterSharingInviteText", IDS_PRINT_PREVIEW_GROUP_INVITE_TEXT);
  source->AddLocalizedString(
      "printerSharingInviteText", IDS_PRINT_PREVIEW_INVITE_TEXT);

  source->SetJsonPath("strings.js");
  source->AddResourcePath("print_preview.js", IDR_PRINT_PREVIEW_JS);
  source->AddResourcePath("pdf_preview.html",
                          IDR_PRINT_PREVIEW_PDF_PREVIEW_HTML);
  source->AddResourcePath("images/printer.png",
                          IDR_PRINT_PREVIEW_IMAGES_PRINTER);
  source->AddResourcePath("images/printer_shared.png",
                          IDR_PRINT_PREVIEW_IMAGES_PRINTER_SHARED);
  source->AddResourcePath("images/business.svg",
                          IDR_PRINT_PREVIEW_IMAGES_ENTERPRISE_PRINTER);
  source->AddResourcePath("images/third_party.png",
                          IDR_PRINT_PREVIEW_IMAGES_THIRD_PARTY);
  source->AddResourcePath("images/google_doc.png",
                          IDR_PRINT_PREVIEW_IMAGES_GOOGLE_DOC);
  source->AddResourcePath("images/pdf.png", IDR_PRINT_PREVIEW_IMAGES_PDF);
  source->AddResourcePath("images/mobile.png", IDR_PRINT_PREVIEW_IMAGES_MOBILE);
  source->AddResourcePath("images/mobile_shared.png",
                          IDR_PRINT_PREVIEW_IMAGES_MOBILE_SHARED);
  source->SetDefaultResource(IDR_PRINT_PREVIEW_HTML);
  source->SetRequestFilter(base::Bind(&HandleRequestCallback));
  source->OverrideContentSecurityPolicyScriptSrc(
      base::StringPrintf("script-src chrome://resources 'self' 'unsafe-eval' "
                         "chrome-extension://%s;",
                         extension_misc::kPdfExtensionId));
  source->OverrideContentSecurityPolicyChildSrc("child-src 'self';");
  source->DisableDenyXFrameOptions();
  source->OverrideContentSecurityPolicyObjectSrc("object-src 'self';");
  source->AddLocalizedString("moreOptionsLabel", IDS_MORE_OPTIONS_LABEL);
  source->AddLocalizedString("lessOptionsLabel", IDS_LESS_OPTIONS_LABEL);

#if !defined(OS_MACOSX) && !defined(OS_WIN)
  bool print_pdf_as_image_enabled = base::FeatureList::IsEnabled(
      features::kPrintPdfAsImage);
  source->AddBoolean("printPdfAsImageEnabled", print_pdf_as_image_enabled);
#else
  source->AddBoolean("printPdfAsImageEnabled", false);
#endif
#if defined(OS_CHROMEOS)
  bool cups_and_md_settings_enabled =
      !base::CommandLine::ForCurrentProcess()->HasSwitch(
          ::switches::kDisableNativeCups);
  source->AddBoolean("showLocalManageButton", cups_and_md_settings_enabled);
#else
  source->AddBoolean("showLocalManageButton", true);
#endif
  return source;
}

}  // namespace

PrintPreviewUI::PrintPreviewUI(content::WebUI* web_ui)
    : ConstrainedWebDialogUI(web_ui),
      initial_preview_start_time_(base::TimeTicks::Now()),
      id_(g_print_preview_ui_id_map.Get().Add(this)),
      handler_(nullptr),
      source_is_modifiable_(true),
      source_has_selection_(false),
      print_selection_only_(false),
      dialog_closed_(false) {
  // Set up the chrome://print/ data source.
  Profile* profile = Profile::FromWebUI(web_ui);
  content::WebUIDataSource::Add(profile, CreatePrintPreviewUISource());

  // Set up the chrome://theme/ source.
  content::URLDataSource::Add(profile, new ThemeSource(profile));

  auto handler = base::MakeUnique<PrintPreviewHandler>();
  handler_ = handler.get();
  web_ui->AddMessageHandler(std::move(handler));
  web_ui->AddMessageHandler(base::MakeUnique<MetricsHandler>());

  g_print_preview_request_id_map.Get().Set(id_, -1);
}

PrintPreviewUI::~PrintPreviewUI() {
  PrintPreviewDataService::GetInstance()->RemoveEntry(id_);
  g_print_preview_request_id_map.Get().Erase(id_);
  g_print_preview_ui_id_map.Get().Remove(id_);
}

void PrintPreviewUI::GetPrintPreviewDataForIndex(
    int index,
    scoped_refptr<base::RefCountedBytes>* data) const {
  PrintPreviewDataService::GetInstance()->GetDataEntry(id_, index, data);
}

void PrintPreviewUI::SetPrintPreviewDataForIndex(
    int index,
    scoped_refptr<base::RefCountedBytes> data) {
  PrintPreviewDataService::GetInstance()->SetDataEntry(id_, index,
                                                       std::move(data));
}

void PrintPreviewUI::ClearAllPreviewData() {
  PrintPreviewDataService::GetInstance()->RemoveEntry(id_);
}

int PrintPreviewUI::GetAvailableDraftPageCount() const {
  return PrintPreviewDataService::GetInstance()->GetAvailableDraftPageCount(
      id_);
}

void PrintPreviewUI::SetInitiatorTitle(
    const base::string16& job_title) {
  initiator_title_ = job_title;
}

// static
void PrintPreviewUI::SetInitialParams(
    content::WebContents* print_preview_dialog,
    const PrintHostMsg_RequestPrintPreview_Params& params) {
  if (!print_preview_dialog || !print_preview_dialog->GetWebUI())
    return;
  PrintPreviewUI* print_preview_ui = static_cast<PrintPreviewUI*>(
      print_preview_dialog->GetWebUI()->GetController());
  print_preview_ui->source_is_modifiable_ = params.is_modifiable;
  print_preview_ui->source_has_selection_ = params.has_selection;
  print_preview_ui->print_selection_only_ = params.selection_only;
}

// static
void PrintPreviewUI::GetCurrentPrintPreviewStatus(int32_t preview_ui_id,
                                                  int request_id,
                                                  bool* cancel) {
  int current_id = -1;
  if (!g_print_preview_request_id_map.Get().Get(preview_ui_id, &current_id)) {
    *cancel = true;
    return;
  }
  *cancel = (request_id != current_id);
}

int32_t PrintPreviewUI::GetIDForPrintPreviewUI() const {
  return id_;
}

void PrintPreviewUI::OnPrintPreviewDialogClosed() {
  WebContents* preview_dialog = web_ui()->GetWebContents();
  printing::BackgroundPrintingManager* background_printing_manager =
      g_browser_process->background_printing_manager();
  if (background_printing_manager->HasPrintPreviewDialog(preview_dialog))
    return;
  OnClosePrintPreviewDialog();
}

void PrintPreviewUI::OnInitiatorClosed() {
  WebContents* preview_dialog = web_ui()->GetWebContents();
  printing::BackgroundPrintingManager* background_printing_manager =
      g_browser_process->background_printing_manager();
  if (background_printing_manager->HasPrintPreviewDialog(preview_dialog))
    web_ui()->CallJavascriptFunctionUnsafe("cancelPendingPrintRequest");
  else
    OnClosePrintPreviewDialog();
}

void PrintPreviewUI::OnPrintPreviewRequest(int request_id) {
  if (!initial_preview_start_time_.is_null()) {
    UMA_HISTOGRAM_TIMES("PrintPreview.InitializationTime",
                        base::TimeTicks::Now() - initial_preview_start_time_);
  }
  g_print_preview_request_id_map.Get().Set(id_, request_id);
}

void PrintPreviewUI::OnDidGetPreviewPageCount(
    const PrintHostMsg_DidGetPreviewPageCount_Params& params) {
  DCHECK_GT(params.page_count, 0);
  if (g_testing_delegate)
    g_testing_delegate->DidGetPreviewPageCount(params.page_count);
  base::Value count(params.page_count);
  base::Value request_id(params.preview_request_id);
  base::Value fit_to_page_scaling(params.fit_to_page_scaling);
  web_ui()->CallJavascriptFunctionUnsafe("onDidGetPreviewPageCount", count,
                                         request_id, fit_to_page_scaling);
}

void PrintPreviewUI::OnDidGetDefaultPageLayout(
    const PageSizeMargins& page_layout, const gfx::Rect& printable_area,
    bool has_custom_page_size_style) {
  if (page_layout.margin_top < 0 || page_layout.margin_left < 0 ||
      page_layout.margin_bottom < 0 || page_layout.margin_right < 0 ||
      page_layout.content_width < 0 || page_layout.content_height < 0 ||
      printable_area.width() <= 0 || printable_area.height() <= 0) {
    NOTREACHED();
    return;
  }

  base::DictionaryValue layout;
  layout.SetDouble(printing::kSettingMarginTop, page_layout.margin_top);
  layout.SetDouble(printing::kSettingMarginLeft, page_layout.margin_left);
  layout.SetDouble(printing::kSettingMarginBottom, page_layout.margin_bottom);
  layout.SetDouble(printing::kSettingMarginRight, page_layout.margin_right);
  layout.SetDouble(printing::kSettingContentWidth, page_layout.content_width);
  layout.SetDouble(printing::kSettingContentHeight, page_layout.content_height);
  layout.SetInteger(printing::kSettingPrintableAreaX, printable_area.x());
  layout.SetInteger(printing::kSettingPrintableAreaY, printable_area.y());
  layout.SetInteger(printing::kSettingPrintableAreaWidth,
                    printable_area.width());
  layout.SetInteger(printing::kSettingPrintableAreaHeight,
                    printable_area.height());

  base::Value has_page_size_style(has_custom_page_size_style);
  web_ui()->CallJavascriptFunctionUnsafe("onDidGetDefaultPageLayout", layout,
                                         has_page_size_style);
}

void PrintPreviewUI::OnDidPreviewPage(int page_number,
                                      int preview_request_id) {
  DCHECK_GE(page_number, 0);
  base::Value number(page_number);
  base::Value ui_identifier(id_);
  base::Value request_id(preview_request_id);
  if (g_testing_delegate)
    g_testing_delegate->DidRenderPreviewPage(web_ui()->GetWebContents());
  web_ui()->CallJavascriptFunctionUnsafe("onDidPreviewPage", number,
                                         ui_identifier, request_id);
}

void PrintPreviewUI::OnPreviewDataIsAvailable(int expected_pages_count,
                                              int preview_request_id) {
  VLOG(1) << "Print preview request finished with "
          << expected_pages_count << " pages";

  if (!initial_preview_start_time_.is_null()) {
    UMA_HISTOGRAM_TIMES("PrintPreview.InitialDisplayTime",
                        base::TimeTicks::Now() - initial_preview_start_time_);
    UMA_HISTOGRAM_COUNTS("PrintPreview.PageCount.Initial",
                         expected_pages_count);
    UMA_HISTOGRAM_COUNTS(
        "PrintPreview.RegeneratePreviewRequest.BeforeFirstData",
        handler_->regenerate_preview_request_count());
    initial_preview_start_time_ = base::TimeTicks();
  }
  base::Value ui_identifier(id_);
  base::Value ui_preview_request_id(preview_request_id);
  web_ui()->CallJavascriptFunctionUnsafe("updatePrintPreview", ui_identifier,
                                         ui_preview_request_id);
}

void PrintPreviewUI::OnFileSelectionCancelled() {
  web_ui()->CallJavascriptFunctionUnsafe("fileSelectionCancelled");
}

void PrintPreviewUI::OnCancelPendingPreviewRequest() {
  g_print_preview_request_id_map.Get().Set(id_, -1);
}

void PrintPreviewUI::OnPrintPreviewFailed() {
  handler_->OnPrintPreviewFailed();
  web_ui()->CallJavascriptFunctionUnsafe("printPreviewFailed");
}

void PrintPreviewUI::OnInvalidPrinterSettings() {
  web_ui()->CallJavascriptFunctionUnsafe("invalidPrinterSettings");
}

void PrintPreviewUI::OnHidePreviewDialog() {
  WebContents* preview_dialog = web_ui()->GetWebContents();
  printing::BackgroundPrintingManager* background_printing_manager =
      g_browser_process->background_printing_manager();
  if (background_printing_manager->HasPrintPreviewDialog(preview_dialog))
    return;

  ConstrainedWebDialogDelegate* delegate = GetConstrainedDelegate();
  if (!delegate)
    return;
  std::unique_ptr<content::WebContents> preview_contents =
      delegate->ReleaseWebContents();
  DCHECK_EQ(preview_dialog, preview_contents.get());
  background_printing_manager->OwnPrintPreviewDialog(
      preview_contents.release());
  OnClosePrintPreviewDialog();
}

void PrintPreviewUI::OnClosePrintPreviewDialog() {
  if (dialog_closed_)
    return;
  dialog_closed_ = true;
  ConstrainedWebDialogDelegate* delegate = GetConstrainedDelegate();
  if (!delegate)
    return;
  delegate->GetWebDialogDelegate()->OnDialogClosed(std::string());
  delegate->OnDialogCloseFromWebUI();
}

void PrintPreviewUI::OnReloadPrintersList() {
  web_ui()->CallJavascriptFunctionUnsafe("reloadPrintersList");
}

void PrintPreviewUI::OnSetOptionsFromDocument(
    const PrintHostMsg_SetOptionsFromDocument_Params& params) {
  base::DictionaryValue options;
  options.SetBoolean(printing::kSettingDisableScaling,
                     params.is_scaling_disabled);
  options.SetInteger(printing::kSettingCopies, params.copies);
  options.SetInteger(printing::kSettingDuplexMode, params.duplex);
  web_ui()->CallJavascriptFunctionUnsafe("printPresetOptionsFromDocument",
                                         options);
}

// static
void PrintPreviewUI::SetDelegateForTesting(TestingDelegate* delegate) {
  g_testing_delegate = delegate;
}

void PrintPreviewUI::SetSelectedFileForTesting(const base::FilePath& path) {
  handler_->FileSelected(path, 0, NULL);
}

void PrintPreviewUI::SetPdfSavedClosureForTesting(
    const base::Closure& closure) {
  handler_->SetPdfSavedClosureForTesting(closure);
}
