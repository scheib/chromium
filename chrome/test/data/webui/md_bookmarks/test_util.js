// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * Replace the current body of the test with a new element.
 * @param {Element} element
 */
function replaceBody(element) {
  PolymerTest.clearBody();

  window.history.replaceState({}, '', '/');

  document.body.appendChild(element);
}

/**
 * Convert a list of top-level bookmark nodes into a normalized lookup table of
 * nodes.
 * @param {...BookmarkTreeNode} nodes
 */
function testTree(nodes) {
  return bookmarks.util.normalizeNodes(
      createFolder('0', Array.from(arguments)));
}

/**
 * Creates a folder with given properties.
 * @param {string} id
 * @param {Array<BookmarkTreeNode>} children
 * @param {Object=} config
 * @return {BookmarkTreeNode}
 */
function createFolder(id, children, config) {
  var newFolder = {
    id: id,
    children: children,
    title: '',
  };
  if (config) {
    for (var key in config)
      newFolder[key] = config[key];
  }
  if (children.length) {
    for (var i = 0; i < children.length; i++) {
      children[i].index = i;
      children[i].parentId = newFolder.id;
    }
  }
  return newFolder;
}

/**
 * Splices out the item/folder at |index| and adjusts the indices of all the
 * items after that.
 * @param {BookmarkTreeNode} tree
 * @param {number} index
 */
function removeChild(tree, index) {
  tree.children.splice(index, 1);
  for (var i = index; i < tree.children.length; i++)
    tree.children[i].index = i;
}

/**
 * Creates a bookmark with given properties.
 * @param {string} id
 * @param {Object=} config
 * @return {BookmarkTreeNode}
 */
function createItem(id, config) {
  var newItem = {
    id: id,
    title: '',
    url: 'http://www.google.com/',
  };
  if (config) {
    for (var key in config)
      newItem[key] = config[key];
  }
  return newItem;
}

/**
 * @param {Set<T>}
 * @return {Array<T>}
 * @template T
 */
function normalizeSet(set) {
  return Array.from(set).sort();
}

/**
 * Sends a custom click event to |element|.
 * @param {HTMLElement} element
 * @param {Object=} config
 */
function customClick(element, config) {
  var props = {
    bubbles: true,
    cancelable: true,
    button: 0,
    buttons: 1,
    shiftKey: false,
    ctrlKey: false,
    detail: 1,
  };

  if (config) {
    for (var key in config)
      props[key] = config[key];
  }

  element.dispatchEvent(new MouseEvent('mousedown', props));
  element.dispatchEvent(new MouseEvent('mouseup', props));
  element.dispatchEvent(new MouseEvent('click', props));
  if (config && config.detail == 2)
    element.dispatchEvent(new MouseEvent('dblclick', props));
}

/**
 * Returns a folder node beneath |rootNode| which matches |id|.
 * @param {BookmarksFolderNodeElement} rootNode
 * @param {string} id
 * @return {BookmarksFolderNodeElement}
 */
function findFolderNode(rootNode, id) {
  var nodes = [rootNode];
  var node;
  while (nodes.length) {
    node = nodes.pop();
    if (node.itemId == id)
      return node;

    node.root.querySelectorAll('bookmarks-folder-node')
        .forEach((x) => {nodes.unshift(x)});
  }
}
