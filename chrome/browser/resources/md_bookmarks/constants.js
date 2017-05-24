// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * Enumeration of valid drop locations relative to an element. These are
 * bit masks to allow combining multiple locations in a single value.
 * @enum {number}
 * @const
 */
var DropPosition = {
  NONE: 0,
  ABOVE: 1,
  ON: 2,
  BELOW: 4,
};

/**
 * @enum {string}
 * @const
 */
var Command = {
  EDIT: 'edit',
  COPY: 'copy',
  DELETE: 'delete',
  OPEN_NEW_TAB: 'open-new-tab',
  OPEN_NEW_WINDOW: 'open-new-window',
  OPEN_INCOGNITO: 'open-incognito',
  // OPEN triggers when you double-click an item.
  OPEN: 'open',
};

/** @const */
var LOCAL_STORAGE_CLOSED_FOLDERS_KEY = 'closedState';

/** @const */
var LOCAL_STORAGE_TREE_WIDTH_KEY = 'treeWidth';

/** @const */
var ROOT_NODE_ID = '0';
