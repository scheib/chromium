// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This is the shared code for security interstitials. It is used for both SSL
// interstitials and Safe Browsing interstitials.

// Should match security_interstitials::SecurityInterstitialCommands
/** @enum| {string} */
var SecurityInterstitialCommandId = {
  CMD_DONT_PROCEED: 0,
  CMD_PROCEED: 1,
  // Ways for user to get more information
  CMD_SHOW_MORE_SECTION: 2,
  CMD_OPEN_HELP_CENTER: 3,
  CMD_OPEN_DIAGNOSTIC: 4,
  // Primary button actions
  CMD_RELOAD: 5,
  CMD_OPEN_DATE_SETTINGS: 6,
  CMD_OPEN_LOGIN: 7,
  // Safe Browsing Extended Reporting
  CMD_DO_REPORT: 8,
  CMD_DONT_REPORT: 9,
  CMD_OPEN_REPORTING_PRIVACY: 10,
  CMD_OPEN_WHITEPAPER: 11,
  // Report a phishing error.
  CMD_REPORT_PHISHING_ERROR: 12
};

/**
 * A convenience method for sending commands to the parent page.
 * @param {string} cmd  The command to send.
 */
function sendCommand(cmd) {
// <if expr="not is_ios">
  window.domAutomationController.setAutomationId(1);
  window.domAutomationController.send(cmd);
// </if>
// <if expr="is_ios">
  // TODO(crbug.com/565877): Revisit message passing for WKWebView.
  var iframe = document.createElement('IFRAME');
  iframe.setAttribute('src', 'js-command:' + cmd);
  document.documentElement.appendChild(iframe);
  iframe.parentNode.removeChild(iframe);
// </if>
}