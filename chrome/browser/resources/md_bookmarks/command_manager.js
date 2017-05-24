// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview Element which shows context menus and handles keyboard
 * shortcuts.
 */
cr.define('bookmarks', function() {

  var CommandManager = Polymer({
    is: 'bookmarks-command-manager',

    behaviors: [
      bookmarks.StoreClient,
    ],

    properties: {
      /** @private {!Array<Command>} */
      menuCommands_: {
        type: Array,
        value: function() {
          return [
            Command.EDIT,
            Command.COPY,
            Command.DELETE,
            // <hr>
            Command.OPEN_NEW_TAB,
            Command.OPEN_NEW_WINDOW,
            Command.OPEN_INCOGNITO,
          ];
        },
      },

      /** @type {Set<string>} */
      menuIds_: Object,
    },

    attached: function() {
      assert(CommandManager.instance_ == null);
      CommandManager.instance_ = this;

      /** @private {function(!Event)} */
      this.boundOnOpenItemMenu_ = this.onOpenItemMenu_.bind(this);
      document.addEventListener('open-item-menu', this.boundOnOpenItemMenu_);

      /** @private {function(!Event)} */
      this.boundOnKeydown_ = this.onKeydown_.bind(this);
      document.addEventListener('keydown', this.boundOnKeydown_);

      /** @private {Object<Command, string>} */
      this.shortcuts_ = {};
      this.shortcuts_[Command.EDIT] = cr.isMac ? 'enter' : 'f2';
      this.shortcuts_[Command.COPY] = cr.isMac ? 'meta+c' : 'ctrl+c';
      this.shortcuts_[Command.DELETE] =
          cr.isMac ? 'delete backspace' : 'delete';
      this.shortcuts_[Command.OPEN_NEW_TAB] =
          cr.isMac ? 'meta+enter' : 'ctrl+enter';
      this.shortcuts_[Command.OPEN_NEW_WINDOW] = 'shift+enter';
      this.shortcuts_[Command.OPEN] = cr.isMac ? 'meta+down' : 'enter';
    },

    detached: function() {
      CommandManager.instance_ = null;
      document.removeEventListener('open-item-menu', this.boundOnOpenItemMenu_);
      document.removeEventListener('keydown', this.boundOnKeydown_);
    },

    /**
     * Display the command context menu at (|x|, |y|) in window co-ordinates.
     * Commands will execute on the currently selected items.
     * @param {number} x
     * @param {number} y
     */
    openCommandMenuAtPosition: function(x, y) {
      this.menuIds_ = this.getState().selection.items;
      /** @type {!CrActionMenuElement} */ (this.$.dropdown)
          .showAtPosition({top: y, left: x});
    },

    /**
     * Display the command context menu positioned to cover the |target|
     * element. Commands will execute on the currently selected items.
     * @param {!Element} target
     */
    openCommandMenuAtElement: function(target) {
      this.menuIds_ = this.getState().selection.items;
      /** @type {!CrActionMenuElement} */ (this.$.dropdown).showAt(target);
    },

    closeCommandMenu: function() {
      /** @type {!CrActionMenuElement} */ (this.$.dropdown).close();
    },

    ////////////////////////////////////////////////////////////////////////////
    // Command handlers:

    /**
     * Determine if the |command| can be executed with the given |itemIds|.
     * Commands which appear in the context menu should be implemented
     * separately using `isCommandVisible_` and `isCommandEnabled_`.
     * @param {Command} command
     * @param {!Set<string>} itemIds
     * @return {boolean}
     */
    canExecute: function(command, itemIds) {
      switch (command) {
        case Command.OPEN:
          return itemIds.size > 0;
        default:
          return this.isCommandVisible_(command, itemIds) &&
              this.isCommandEnabled_(command, itemIds);
      }
    },

    /**
     * @param {Command} command
     * @param {!Set<string>} itemIds
     * @return {boolean} True if the command should be visible in the context
     *     menu.
     */
    isCommandVisible_: function(command, itemIds) {
      switch (command) {
        case Command.EDIT:
          return itemIds.size == 1;
        case Command.COPY:
          return itemIds.size == 1 &&
              this.containsMatchingNode_(itemIds, function(node) {
                return !!node.url;
              });
        case Command.DELETE:
        case Command.OPEN_NEW_TAB:
        case Command.OPEN_NEW_WINDOW:
        case Command.OPEN_INCOGNITO:
          return itemIds.size > 0;
        default:
          return false;
      }
    },

    /**
     * @param {Command} command
     * @param {!Set<string>} itemIds
     * @return {boolean} True if the command should be clickable in the context
     *     menu.
     */
    isCommandEnabled_: function(command, itemIds) {
      switch (command) {
        case Command.OPEN_NEW_TAB:
        case Command.OPEN_NEW_WINDOW:
        case Command.OPEN_INCOGNITO:
          return this.expandUrls_(itemIds).length > 0;
        default:
          return true;
      }
    },

    /**
     * @param {Command} command
     * @param {!Set<string>} itemIds
     */
    handle: function(command, itemIds) {
      switch (command) {
        case Command.EDIT:
          var id = Array.from(itemIds)[0];
          /** @type {!BookmarksEditDialogElement} */ (this.$.editDialog.get())
              .showEditDialog(this.getState().nodes[id]);
          break;
        case Command.COPY:
          var idList = Array.from(itemIds);
          chrome.bookmarkManagerPrivate.copy(idList, function() {
            // TODO(jiaxi): Add toast later.
          });
          break;
        case Command.DELETE:
          chrome.bookmarkManagerPrivate.removeTrees(
              Array.from(this.minimizeDeletionSet_(itemIds)), function() {
                // TODO(jiaxi): Add toast later.
              });
          break;
        case Command.OPEN_NEW_TAB:
        case Command.OPEN_NEW_WINDOW:
        case Command.OPEN_INCOGNITO:
          this.openUrls_(this.expandUrls_(itemIds), command);
          break;
        case Command.OPEN:
          var isFolder = itemIds.size == 1 &&
              this.containsMatchingNode_(itemIds, function(node) {
                return !node.url;
              });
          if (isFolder) {
            var folderId = Array.from(itemIds)[0];
            this.dispatch(bookmarks.actions.selectFolder(
                folderId, this.getState().nodes));
          } else {
            this.openUrls_(this.expandUrls_(itemIds), command);
          }
          break;
        default:
          assert(false);
      }
    },

    ////////////////////////////////////////////////////////////////////////////
    // Private functions:

    /**
     * Minimize the set of |itemIds| by removing any node which has an ancestor
     * node already in the set. This ensures that instead of trying to delete
     * both a node and its descendant, we will only try to delete the topmost
     * node, preventing an error in the bookmarkManagerPrivate.removeTrees API
     * call.
     * @param {!Set<string>} itemIds
     * @return {!Set<string>}
     */
    minimizeDeletionSet_: function(itemIds) {
      var minimizedSet = new Set();
      var nodes = this.getState().nodes;
      itemIds.forEach(function(itemId) {
        var currentId = itemId;
        while (currentId != ROOT_NODE_ID) {
          currentId = assert(nodes[currentId].parentId);
          if (itemIds.has(currentId))
            return;
        }
        minimizedSet.add(itemId);
      });
      return minimizedSet;
    },

    /**
     * @param {!Array<string>} urls
     * @param {Command} command
     * @private
     */
    openUrls_: function(urls, command) {
      assert(
          command == Command.OPEN || command == Command.OPEN_NEW_TAB ||
          command == Command.OPEN_NEW_WINDOW ||
          command == Command.OPEN_INCOGNITO);

      if (urls.length == 0)
        return;

      var incognito = command == Command.OPEN_INCOGNITO;
      if (command == Command.OPEN_NEW_WINDOW || incognito) {
        chrome.windows.create({url: urls, incognito: incognito});
      } else {
        if (command == Command.OPEN)
          chrome.tabs.create({url: urls.shift(), active: true});
        urls.forEach(function(url) {
          chrome.tabs.create({url: url, active: false});
        });
      }
    },

    /**
     * Returns all URLs in the given set of nodes and their immediate children.
     * Note that these will be ordered by insertion order into the |itemIds|
     * set, and that it is possible to duplicate a URL by passing in both the
     * parent ID and child ID.
     * @param {!Set<string>} itemIds
     * @return {!Array<string>}
     * @private
     */
    expandUrls_: function(itemIds) {
      var urls = [];
      var nodes = this.getState().nodes;

      itemIds.forEach(function(id) {
        var node = nodes[id];
        if (node.url) {
          urls.push(node.url);
        } else {
          node.children.forEach(function(childId) {
            var childNode = nodes[childId];
            if (childNode.url)
              urls.push(childNode.url);
          });
        }
      });

      return urls;
    },

    /**
     * @param {!Set<string>} itemIds
     * @param {function(BookmarkNode):boolean} predicate
     * @return {boolean} True if any node in |itemIds| returns true for
     *     |predicate|.
     */
    containsMatchingNode_: function(itemIds, predicate) {
      var nodes = this.getState().nodes;

      return Array.from(itemIds).some(function(id) {
        return predicate(nodes[id]);
      });
    },

    /**
     * @param {Event} e
     * @private
     */
    onOpenItemMenu_: function(e) {
      if (e.detail.targetElement) {
        this.openCommandMenuAtElement(e.detail.targetElement);
      } else {
        this.openCommandMenuAtPosition(e.detail.x, e.detail.y);
      }
    },

    /**
     * @param {Event} e
     * @private
     */
    onCommandClick_: function(e) {
      this.closeCommandMenu();
      this.handle(e.target.getAttribute('command'), assert(this.menuIds_));
    },

    /**
     * @param {!Event} e
     * @private
     */
    onKeydown_: function(e) {
      var selection = this.getState().selection.items;
      // TODO(tsergeant): Prevent keyboard shortcuts when a dialog is open or
      // text field is focused.
      for (var commandName in this.shortcuts_) {
        var shortcut = this.shortcuts_[commandName];
        if (Polymer.IronA11yKeysBehavior.keyboardEventMatchesKeys(
                e, shortcut) &&
            this.canExecute(commandName, selection)) {
          this.handle(commandName, selection);

          e.stopPropagation();
          e.preventDefault();
          return;
        }
      }
    },

    /**
     * Close the menu on mousedown so clicks can propagate to the underlying UI.
     * This allows the user to right click the list while a context menu is
     * showing and get another context menu.
     * @param {Event} e
     * @private
     */
    onMenuMousedown_: function(e) {
      if (e.path[0] != this.$.dropdown)
        return;

      this.$.dropdown.close();
    },

    /**
     * @param {Command} command
     * @return {string}
     * @private
     */
    getCommandLabel_: function(command) {
      var multipleNodes = this.menuIds_.size > 1 ||
          this.containsMatchingNode_(this.menuIds_, function(node) {
            return !node.url;
          });
      var label;
      switch (command) {
        case Command.EDIT:
          if (this.menuIds_.size > 1)
            return '';

          var id = Array.from(this.menuIds_)[0];
          var itemUrl = this.getState().nodes[id].url;
          label = itemUrl ? 'menuEdit' : 'menuRename';
          break;
        case Command.COPY:
          label = 'menuCopyURL';
          break;
        case Command.DELETE:
          label = 'menuDelete';
          break;
        case Command.OPEN_NEW_TAB:
          label = multipleNodes ? 'menuOpenAllNewTab' : 'menuOpenNewTab';
          break;
        case Command.OPEN_NEW_WINDOW:
          label = multipleNodes ? 'menuOpenAllNewWindow' : 'menuOpenNewWindow';
          break;
        case Command.OPEN_INCOGNITO:
          label = multipleNodes ? 'menuOpenAllIncognito' : 'menuOpenIncognito';
          break;
      }

      return loadTimeData.getString(assert(label));
    },

    /**
     * @param {Command} command
     * @return {boolean}
     * @private
     */
    showDividerAfter_: function(command) {
      return command == Command.DELETE;
    },
  });

  /** @private {bookmarks.CommandManager} */
  CommandManager.instance_ = null;

  /** @return {!bookmarks.CommandManager} */
  CommandManager.getInstance = function() {
    return assert(CommandManager.instance_);
  };

  return {
    CommandManager: CommandManager,
  };
});
