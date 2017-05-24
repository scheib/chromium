// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

suite('<bookmarks-list>', function() {
  var list;
  var store;

  setup(function() {
    store = new bookmarks.TestStore({
      nodes: testTree(createFolder(
          '10',
          [
            createItem('1'),
            createFolder('3', []),
            createItem('5'),
            createItem('7'),
          ])),
      selectedFolder: '10',
    });
    bookmarks.Store.instance_ = store;

    list = document.createElement('bookmarks-list');
    list.style.height = '100%';
    list.style.width = '100%';
    list.style.position= 'absolute';

    replaceBody(list);
    Polymer.dom.flush();
  });

  test('renders correct <bookmark-item> elements', function() {
    var items = list.root.querySelectorAll('bookmarks-item');
    var ids = Array.from(items).map((item) => item.itemId);

    assertDeepEquals(['1', '3', '5', '7'], ids);
  });

  test('shift-selects multiple items', function() {
    var items = list.root.querySelectorAll('bookmarks-item');

    customClick(items[0]);

    assertEquals('select-items', store.lastAction.name);
    assertTrue(store.lastAction.clear);
    assertEquals('1', store.lastAction.anchor);
    assertDeepEquals(['1'], store.lastAction.items);

    store.data.selection.anchor = '1';
    customClick(items[2], {shiftKey: true, ctrlKey: true});

    assertEquals('select-items', store.lastAction.name);
    assertFalse(store.lastAction.clear);
    assertEquals('1', store.lastAction.anchor);
    assertDeepEquals(['1', '3', '5'], store.lastAction.items);
  });

  test('deselects items on click outside of card', function() {
    customClick(list);
    assertEquals('deselect-items', store.lastAction.name);
  });

  test('adds, deletes, and moves update displayedList_', function() {
    list.displayedIds_ = ['1', '7', '3', '5'];
    assertDeepEquals(list.displayedIds_, list.displayedList_.map(n => n.id));

    list.displayedIds_ = ['1', '3', '5'];
    assertDeepEquals(list.displayedIds_, list.displayedList_.map(n => n.id));

    list.displayedIds_ = ['1', '3', '7', '5'];
    assertDeepEquals(list.displayedIds_, list.displayedList_.map(n => n.id));
  });
});

suite('<bookmarks-list> integration test', function() {
  var list;
  var store;
  var items;

  setup(function() {
    store = new bookmarks.TestStore({
      nodes: testTree(createFolder(
          '10',
          [
            createItem('1'),
            createFolder('3', []),
            createItem('5'),
            createItem('7'),
            createItem('9'),
          ])),
      selectedFolder: '10',
    });
    bookmarks.Store.instance_ = store;
    store.setReducersEnabled(true);

    list = document.createElement('bookmarks-list');
    list.style.height = '100%';
    list.style.width = '100%';
    list.style.position = 'absolute';

    replaceBody(list);
    Polymer.dom.flush();

    items = list.root.querySelectorAll('bookmarks-item');
  });

  test('shift-selects multiple items', function() {
    customClick(items[1]);
    assertDeepEquals(['3'], normalizeSet(store.data.selection.items));
    assertDeepEquals('3', store.data.selection.anchor);

    customClick(items[3], {shiftKey: true});
    assertDeepEquals(['3', '5', '7'], normalizeSet(store.data.selection.items));
    assertDeepEquals('3', store.data.selection.anchor);

    customClick(items[0], {shiftKey: true});
    assertDeepEquals(['1', '3'], normalizeSet(store.data.selection.items));
    assertDeepEquals('3', store.data.selection.anchor);
  });

  test('ctrl toggles multiple items', function() {
    customClick(items[1]);
    assertDeepEquals(['3'], normalizeSet(store.data.selection.items));
    assertDeepEquals('3', store.data.selection.anchor);

    customClick(items[3], {ctrlKey: true});
    assertDeepEquals(['3', '7'], normalizeSet(store.data.selection.items));
    assertDeepEquals('7', store.data.selection.anchor);

    customClick(items[1], {ctrlKey: true});
    assertDeepEquals(['7'], normalizeSet(store.data.selection.items));
    assertDeepEquals('3', store.data.selection.anchor);
  });

  test('ctrl+shift adds ranges to selection', function() {
    customClick(items[0]);
    assertDeepEquals(['1'], normalizeSet(store.data.selection.items));
    assertDeepEquals('1', store.data.selection.anchor);

    customClick(items[2], {ctrlKey: true});
    assertDeepEquals(['1', '5'], normalizeSet(store.data.selection.items));
    assertDeepEquals('5', store.data.selection.anchor);

    customClick(items[4], {ctrlKey: true, shiftKey: true});
    assertDeepEquals(
        ['1', '5', '7', '9'], normalizeSet(store.data.selection.items));
    assertDeepEquals('5', store.data.selection.anchor);

    customClick(items[0], {ctrlKey: true, shiftKey: true});
    assertDeepEquals(
        ['1', '3', '5', '7', '9'], normalizeSet(store.data.selection.items));
    assertDeepEquals('5', store.data.selection.anchor);
  });
});
