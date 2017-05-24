// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

Polymer({
  is: 'bookmarks-item',

  behaviors: [
    bookmarks.StoreClient,
  ],

  properties: {
    itemId: {
      type: String,
      observer: 'onItemIdChanged_',
    },

    ironListTabIndex: String,

    /** @private {BookmarkNode} */
    item_: {
      type: Object,
      observer: 'onItemChanged_',
    },

    /** @private */
    isSelectedItem_: {
      type: Boolean,
      reflectToAttribute: true,
    },

    /** @private */
    mouseFocus_: {
      type: Boolean,
      reflectToAttribute: true,
    },

    /** @private */
    isFolder_: Boolean,

    /** @private */
    openItemUrl_: String,
  },

  observers: [
    'updateFavicon_(item_.url)',
  ],

  listeners: {
    'mousedown': 'onMousedown_',
    'blur': 'onItemBlur_',
    'click': 'onClick_',
    'dblclick': 'onDblClick_',
    'contextmenu': 'onContextMenu_',
  },

  /** @override */
  attached: function() {
    this.watch('item_', function(store) {
      return store.nodes[this.itemId];
    }.bind(this));
    this.watch('isSelectedItem_', function(store) {
      return !!store.selection.items.has(this.itemId);
    }.bind(this));

    this.updateFromStore();
  },

  /** @return {BookmarksItemElement} */
  getDropTarget: function() {
    return this;
  },

  /**
   * @param {Event} e
   * @private
   */
  onContextMenu_: function(e) {
    e.preventDefault();
    if (!this.isSelectedItem_)
      this.selectThisItem_();

    this.fire('open-item-menu', {
      x: e.clientX,
      y: e.clientY,
    });
  },

  /**
   * @param {Event} e
   * @private
   */
  onMenuButtonClick_: function(e) {
    e.stopPropagation();
    e.preventDefault();
    this.selectThisItem_();
    this.fire('open-item-menu', {
      targetElement: e.target,
    });
  },

  /**
   * @param {Event} e
   * @private
   */
  onMenuButtonDblClick_: function(e) {
    e.stopPropagation();
  },

  /** @private */
  selectThisItem_: function() {
    this.dispatch(bookmarks.actions.selectItem(this.itemId, this.getState(), {
      clear: true,
      range: false,
      toggle: false,
    }));
  },

  /** @private */
  onItemIdChanged_: function() {
    // TODO(tsergeant): Add a histogram to measure whether this assertion fails
    // for real users.
    assert(this.getState().nodes[this.itemId]);
    this.updateFromStore();
  },

  /** @private */
  onItemChanged_: function() {
    this.isFolder_ = !this.item_.url;
    if (this.item_.url)
      this.openItemUrl_ = this.item_.url;
    else
      this.openItemUrl_ = 'chrome://bookmarks/?id=' + this.itemId;
  },

  /**
   * @private
   */
  onMousedown_: function() {
    this.mouseFocus_ = true;
  },

  /**
   * @private
   */
  onItemBlur_: function() {
    this.mouseFocus_ = false;
  },

  /**
   * @param {MouseEvent} e
   * @private
   */
  onClick_: function(e) {
    // Ignore double clicks so that Ctrl double-clicking an item won't deselect
    // the item before opening.
    if (e.detail != 2) {
      this.dispatch(bookmarks.actions.selectItem(this.itemId, this.getState(), {
        clear: !e.ctrlKey,
        range: e.shiftKey,
        toggle: e.ctrlKey && !e.shiftKey,
      }));
    }
    e.stopPropagation();
    e.preventDefault();
  },

  /**
   * @param {MouseEvent} e
   * @private
   */
  onDblClick_: function(e) {
    var commandManager = bookmarks.CommandManager.getInstance();
    var itemSet = this.getState().selection.items;
    if (commandManager.canExecute(Command.OPEN, itemSet))
      commandManager.handle(Command.OPEN, itemSet);
  },

  /**
   * @param {string} url
   * @private
   */
  updateFavicon_: function(url) {
    this.$.icon.style.backgroundImage = cr.icon.getFavicon(url);
  },
});
