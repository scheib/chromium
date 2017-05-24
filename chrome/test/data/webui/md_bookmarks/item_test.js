// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

suite('<bookmarks-item>', function() {
  var item;
  var store;
  var TEST_ITEM = createItem('0');

  setup(function() {
    store = new bookmarks.TestStore({
      nodes: testTree(createFolder(
          '1',
          [
            createItem('2', {url: 'http://example.com/'}),
            createItem('3'),
          ])),
    });
    bookmarks.Store.instance_ = store;

    item = document.createElement('bookmarks-item');
    item.itemId = '2';
    replaceBody(item);
  });

  test('changing the url changes the favicon', function() {
    var favicon = item.$.icon.style.backgroundImage;
    store.data.nodes['2'] = createItem('0', {url: 'https://mail.google.com'});
    store.notifyObservers();
    assertNotEquals(favicon, item.$.icon.style.backgroundImage);
  });

  test('changing to folder hides/unhides the folder/icon', function() {
    // Starts test as an item.
    assertTrue(item.$['folder-icon'].hidden);
    assertFalse(item.$.icon.hidden);

    // Change to a folder.
    item.itemId = '1';

    assertFalse(item.$['folder-icon'].hidden);
    assertTrue(item.$.icon.hidden);
  });

  test('pressing the menu button selects the item', function() {
    MockInteractions.tap(item.$$('.more-vert-button'));
    assertDeepEquals(
        bookmarks.actions.selectItem('2', store.data, {
          clear: true,
          range: false,
          toggle: false,
        }),
        store.lastAction);
  });

  test('context menu selects item if unselected', function() {
    item.isSelectedItem_ = true;
    item.dispatchEvent(new MouseEvent('contextmenu'));
    assertEquals(null, store.lastAction);

    item.isSelectedItem_ = false;
    item.dispatchEvent(new MouseEvent('contextmenu'));
    assertDeepEquals(
        bookmarks.actions.selectItem('2', store.data, {
          clear: true,
          range: false,
          toggle: false,
        }),
        store.lastAction);
  });

  test('anchor tag updates with item url', function() {
    assertEquals('http://example.com/', item.$.url.href);

    store.data.nodes['2'] = createItem('0', {url: 'https://mail.google.com'});
    store.notifyObservers();
    assertEquals('https://mail.google.com/', item.$.url.href);

    // Change to a folder.
    item.itemId = '1';
    assertEquals('chrome://bookmarks/?id=1', item.$.url.href);
  });
});
