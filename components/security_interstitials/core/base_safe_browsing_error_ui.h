// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_SECURITY_INTERSTITIALS_CORE_BASE_SAFE_BROWSING_ERROR_UI_H_
#define COMPONENTS_SECURITY_INTERSTITIALS_CORE_BASE_SAFE_BROWSING_ERROR_UI_H_

#include "base/macros.h"
#include "base/time/time.h"
#include "base/values.h"
#include "components/security_interstitials/core/controller_client.h"
#include "url/gurl.h"

namespace security_interstitials {

// A base class for quiet vs loud versions of the safe browsing interstitial.
// This class displays UI for Safe Browsing errors that block page loads. This
// class is purely about visual display; it does not do any error-handling logic
// to determine what type of error should be displayed when.
class BaseSafeBrowsingErrorUI {
 public:
  enum SBInterstitialReason {
    SB_REASON_MALWARE,
    SB_REASON_HARMFUL,
    SB_REASON_PHISHING,
  };

  struct SBErrorDisplayOptions {
    SBErrorDisplayOptions(bool is_main_frame_load_blocked,
                          bool is_extended_reporting_opt_in_allowed,
                          bool is_off_the_record,
                          bool is_extended_reporting_enabled,
                          bool is_scout_reporting_enabled,
                          bool is_proceed_anyway_disabled,
                          bool is_resource_cancellable,
                          const std::string& help_center_article_link);

    SBErrorDisplayOptions(const SBErrorDisplayOptions& other);

    // Indicates if this SB interstitial is blocking main frame load.
    bool is_main_frame_load_blocked;

    // Indicates if user is allowed to opt-in extended reporting preference.
    bool is_extended_reporting_opt_in_allowed;

    // Indicates if user is in incognito mode.
    bool is_off_the_record;

    // Indicates if user opted in for SB extended reporting.
    bool is_extended_reporting_enabled;

    // Indicates if user opted in for Scout extended reporting.
    bool is_scout_reporting_enabled;

    // Indicates if kSafeBrowsingProceedAnywayDisabled preference is set.
    bool is_proceed_anyway_disabled;

    // Indicates if "back to safety" should cancel the pending navigation or
    // navigate back after it's committed.
    bool is_resource_cancellable;

    // The p= query parameter used when visiting the Help Center. If this is
    // nullptr, then a default value will be used for the SafeBrowsing article.
    std::string help_center_article_link;
  };

  BaseSafeBrowsingErrorUI(
      const GURL& request_url,
      const GURL& main_frame_url,
      BaseSafeBrowsingErrorUI::SBInterstitialReason reason,
      const BaseSafeBrowsingErrorUI::SBErrorDisplayOptions& display_options,
      const std::string& app_locale,
      const base::Time& time_triggered,
      ControllerClient* controller);
  virtual ~BaseSafeBrowsingErrorUI();

  bool is_main_frame_load_blocked() const {
    return display_options_.is_main_frame_load_blocked;
  }

  bool is_extended_reporting_opt_in_allowed() const {
    return display_options_.is_extended_reporting_opt_in_allowed;
  }

  bool is_off_the_record() const { return display_options_.is_off_the_record; }

  bool is_extended_reporting_enabled() const {
    return display_options_.is_extended_reporting_enabled;
  }

  void set_extended_reporting(bool pref) {
    display_options_.is_extended_reporting_enabled = pref;
  }

  bool is_scout_reporting_enabled() const {
    return display_options_.is_scout_reporting_enabled;
  }

  bool is_proceed_anyway_disabled() const {
    return display_options_.is_proceed_anyway_disabled;
  }

  bool is_resource_cancellable() const {
    return display_options_.is_resource_cancellable;
  }

  const std::string& get_help_center_article_link() const {
    return display_options_.help_center_article_link;
  }

  // Checks if we should even show the extended reporting option. We don't show
  // it in incognito mode or if kSafeBrowsingExtendedReportingOptInAllowed
  // preference is disabled.
  bool CanShowExtendedReportingOption() {
    return !is_off_the_record() && is_extended_reporting_opt_in_allowed();
  }

  SBInterstitialReason interstitial_reason() const {
    return interstitial_reason_;
  }

  const std::string app_locale() const { return app_locale_; }

  ControllerClient* controller() { return controller_; };

  GURL request_url() const { return request_url_; }
  GURL main_frame_url() const { return main_frame_url_; }

  virtual void PopulateStringsForHtml(
      base::DictionaryValue* load_time_data) = 0;
  virtual void HandleCommand(SecurityInterstitialCommands command) = 0;

 private:
  const GURL request_url_;
  const GURL main_frame_url_;
  const SBInterstitialReason interstitial_reason_;
  SBErrorDisplayOptions display_options_;
  const std::string app_locale_;
  const base::Time time_triggered_;

  ControllerClient* controller_;

  DISALLOW_COPY_AND_ASSIGN(BaseSafeBrowsingErrorUI);
};

}  // security_interstitials

#endif  // COMPONENTS_SECURITY_INTERSTITIALS_CORE_BASE_SAFE_BROWSING_ERROR_UI_H_
