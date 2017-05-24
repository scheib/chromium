// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * Class to manage SwitchAccess and interact with other controllers.
 *
 * @constructor
 * @implements {SwitchAccessInterface}
 */
function SwitchAccess() {
  console.log('Switch access is enabled');

  /**
   * User preferences.
   *
   * @type {SwitchAccessPrefs}
   */
  this.switchAccessPrefs = null;

  /**
   * Handles changes to auto-scan.
   *
   * @private {AutoScanManager}
   */
  this.autoScanManager_ = null;

  /**
   * Handles keyboard input.
   *
   * @private {KeyboardHandler}
   */
  this.keyboardHandler_ = null;

  /**
   * Handles interactions with the accessibility tree, including moving to and
   * selecting nodes.
   *
   * @private {AutomationManager}
   */
  this.automationManager_ = null;

  this.init_();
};

SwitchAccess.prototype = {
  /**
   * Set up preferences, controllers, and event listeners.
   *
   * @private
   */
  init_: function() {
    this.switchAccessPrefs = new SwitchAccessPrefs();
    this.autoScanManager_ = new AutoScanManager(this);
    this.keyboardHandler_ = new KeyboardHandler(this);

    chrome.automation.getDesktop(function(desktop) {
      this.automationManager_ = new AutomationManager(desktop);
    }.bind(this));

    document.addEventListener(
        'prefsUpdate', this.handlePrefsUpdate_.bind(this));
  },

  /**
   * Move to the next/previous interesting node. If |doNext| is true, move to
   * the next node. Otherwise, move to the previous node.
   *
   * @param {boolean} doNext
   * @override
   */
  moveToNode: function(doNext) {
    if (this.automationManager_)
      this.automationManager_.moveToNode(doNext);
  },

  /**
   * Perform the default action on the current node.
   *
   * @override
   */
  selectCurrentNode: function() {
    if (this.automationManager_)
      this.automationManager_.selectCurrentNode();
  },

  /**
   * Open the options page in a new tab.
   *
   * @override
   */
  showOptionsPage: function() {
    let optionsPage = {url: 'options.html'};
    chrome.tabs.create(optionsPage);
  },

  /**
   * Perform actions as the result of actions by the user. Currently, restarts
   * auto-scan if it is enabled.
   *
   * @override
   */
  performedUserAction: function() {
    this.autoScanManager_.restartIfRunning();
  },

  /**
   * Handle a change in user preferences.
   *
   * @param {!Event} event
   * @private
   */
  handlePrefsUpdate_: function(event) {
    let updatedPrefs = event.detail;
    for (let key of Object.keys(updatedPrefs)) {
      switch (key) {
        case 'enableAutoScan':
          this.autoScanManager_.setEnabled(updatedPrefs[key]);
          break;
        case 'autoScanTime':
          this.autoScanManager_.setScanTime(updatedPrefs[key]);
          break;
      }
    }
  },

  /**
   * Move to the next sibling of the current node if it has one.
   *
   * @override
   */
  debugMoveToNext: function() {
    if (this.automationManager_)
      this.automationManager_.debugMoveToNext();
  },

  /**
   * Move to the previous sibling of the current node if it has one.
   *
   * @override
   */
  debugMoveToPrevious: function() {
    if (this.automationManager_)
      this.automationManager_.debugMoveToPrevious();
  },

  /**
   * Move to the first child of the current node if it has one.
   *
   * @override
   */
  debugMoveToFirstChild: function() {
    if (this.automationManager_)
      this.automationManager_.debugMoveToFirstChild();
  },

  /**
   * Move to the parent of the current node if it has one.
   *
   * @override
   */
  debugMoveToParent: function() {
    if (this.automationManager_)
      this.automationManager_.debugMoveToParent();
  }
};
