// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

suite('<bookmarks-command-manager>', function() {
  var commandManager;
  var store;
  var lastCommand;
  var lastCommandIds;

  function assertLastCommand(command, ids) {
    assertEquals(lastCommand, command);
    if (ids)
      assertDeepEquals(normalizeSet(lastCommandIds), ids);
    lastCommand = null;
    lastCommandIds = null;
  }

  suiteSetup(function() {
    // Overwrite bookmarkManagerPrivate APIs which will crash if called with
    // fake data.
    chrome.bookmarkManagerPrivate.copy = function() {};
    chrome.bookmarkManagerPrivate.removeTrees = function() {};
  });

  setup(function() {
    store = new bookmarks.TestStore({
      nodes: testTree(
          createFolder(
              '1',
              [
                createFolder(
                    '11',
                    [
                      createItem('111', {url: 'http://111/'}),
                    ]),
                createFolder(
                    '12',
                    [
                      createItem('121', {url: 'http://121/'}),
                    ]),
                createItem('13', {url: 'http://13/'}),
              ]),
          createFolder(
              '2',
              [
                createFolder('21', []),
              ]))
    });
    bookmarks.Store.instance_ = store;

    commandManager = document.createElement('bookmarks-command-manager');

    var realHandle = commandManager.handle.bind(commandManager);
    commandManager.handle = function(command, itemIds) {
      lastCommand = command;
      lastCommandIds = itemIds;
      realHandle(command, itemIds);
    };
    replaceBody(commandManager);

    Polymer.dom.flush();
  });

  test('can only copy single URL items', function() {
    assertFalse(commandManager.canExecute(Command.COPY, new Set(['11'])));
    assertFalse(commandManager.canExecute(Command.COPY, new Set(['11', '13'])));
    assertTrue(commandManager.canExecute(Command.COPY, new Set(['13'])));
  });

  test('context menu hides invalid commands', function() {
    store.data.selection.items = new Set(['11', '13']);
    store.notifyObservers();

    commandManager.openCommandMenuAtPosition(0, 0);
    var commandHidden = {};
    commandManager.root.querySelectorAll('.dropdown-item').forEach(element => {
      commandHidden[element.getAttribute('command')] = element.hidden;
    });

    // With a folder and an item selected, the only available context menu item
    // is 'Delete'.
    assertTrue(commandHidden['edit']);
    assertTrue(commandHidden['copy']);
    assertFalse(commandHidden['delete']);
  });

  test('keyboard shortcuts trigger when valid', function() {
    var modifier = cr.isMac ? 'meta' : 'ctrl';

    store.data.selection.items = new Set(['13']);
    store.notifyObservers();

    MockInteractions.pressAndReleaseKeyOn(document, 67, modifier, 'c');
    assertLastCommand('copy', ['13']);

    // Doesn't trigger when a folder is selected.
    store.data.selection.items = new Set(['11']);
    store.notifyObservers();

    MockInteractions.pressAndReleaseKeyOn(document, 67, modifier, 'c');
    assertLastCommand(null);

    // Doesn't trigger when nothing is selected.
    store.data.selection.items = new Set();
    store.notifyObservers();

    MockInteractions.pressAndReleaseKeyOn(document, 67, modifier, 'c');
    assertLastCommand(null);
  });

  test('delete command triggers', function() {
    store.data.selection.items = new Set(['12', '13']);
    store.notifyObservers();

    MockInteractions.pressAndReleaseKeyOn(document, 46, '', 'Delete');
    assertLastCommand('delete', ['12', '13']);
  });

  test('edit command triggers', function() {
    var key = cr.isMac ? 'Enter' : 'F2';
    var keyCode = cr.isMac ? 13 : 113;

    store.data.selection.items = new Set(['11']);
    store.notifyObservers();

    MockInteractions.pressAndReleaseKeyOn(document, keyCode, '', key);
    assertLastCommand('edit', ['11']);
  });

  test('does not delete children at same time as ancestor', function() {
    var lastDelete = null;
    chrome.bookmarkManagerPrivate.removeTrees = function(idArray) {
      lastDelete = idArray.sort();
    };

    var parentAndChildren = new Set(['1', '2', '12', '111']);
    assertTrue(commandManager.canExecute(Command.DELETE, parentAndChildren));
    commandManager.handle(Command.DELETE, parentAndChildren);

    assertDeepEquals(['1', '2'], lastDelete);
  });

  test('expandUrls_ expands one level of URLs', function() {
    var urls = commandManager.expandUrls_(new Set(['1']));
    assertDeepEquals(['http://13/'], urls);

    urls = commandManager.expandUrls_(new Set(['11', '12', '13']));
    assertDeepEquals(['http://111/', 'http://121/', 'http://13/'], urls);
  });

  test('shift-enter opens URLs in new window', function() {
    store.data.selection.items = new Set(['12', '13']);
    store.notifyObservers();

    var lastCreate;
    chrome.windows.create = function(createConfig) {
      lastCreate = createConfig;
    };

    MockInteractions.pressAndReleaseKeyOn(document, 13, 'shift', 'Enter');
    assertLastCommand(Command.OPEN_NEW_WINDOW, ['12', '13']);
    assertDeepEquals(['http://121/', 'http://13/'], lastCreate.url);
    assertFalse(lastCreate.incognito);
  });

  test('cannot execute "Open in New Tab" on folders with no items', function() {
    var items = new Set(['2']);
    assertFalse(commandManager.canExecute(Command.OPEN_NEW_TAB, items));

    store.data.selection.items = items;

    commandManager.openCommandMenuAtPosition(0, 0);
    var commandItem = {};
    commandManager.root.querySelectorAll('.dropdown-item').forEach(element => {
      commandItem[element.getAttribute('command')] = element;
    });

    assertTrue(commandItem[Command.OPEN_NEW_TAB].disabled);
    assertFalse(commandItem[Command.OPEN_NEW_TAB].hidden);

    assertTrue(commandItem[Command.OPEN_NEW_WINDOW].disabled);
    assertFalse(commandItem[Command.OPEN_NEW_WINDOW].hidden);

    assertTrue(commandItem[Command.OPEN_INCOGNITO].disabled);
    assertFalse(commandItem[Command.OPEN_INCOGNITO].hidden);
  });
});

suite('<bookmarks-item> CommandManager integration', function() {
  var list;
  var items;
  var commandManager;
  var openedTabs;

  setup(function() {
    store = new bookmarks.TestStore({
      nodes: testTree(createFolder(
          '1',
          [
            createFolder(
                '11',
                [
                  createItem('111', {url: 'http://111/'}),
                ]),
            createItem('12', {url: 'http://12/'}),
            createItem('13', {url: 'http://13/'}),
          ])),
      selectedFolder: '1',
    });
    store.setReducersEnabled(true);
    bookmarks.Store.instance_ = store;

    commandManager = document.createElement('bookmarks-command-manager');

    list = document.createElement('bookmarks-list');
    replaceBody(list);
    document.body.appendChild(commandManager);
    Polymer.dom.flush();

    items = list.root.querySelectorAll('bookmarks-item');

    openedTabs = [];
    chrome.tabs.create = function(createConfig) {
      openedTabs.push(createConfig);
    }
  });

  function assertOpenedTabs(tabs) {
    assertDeepEquals(tabs, openedTabs.map(createConfig => createConfig.url));
  }

  function simulateDoubleClick(element, config) {
    config = config || {};
    customClick(element, config);
    config.detail = 2;
    customClick(element, config);
  }

  test('double click opens folders in bookmark manager', function() {
    simulateDoubleClick(items[0]);
    assertEquals(store.data.selectedFolder, '11');
  });

  test('double click opens items in foreground tab', function() {
    simulateDoubleClick(items[1]);
    assertOpenedTabs(['http://12/']);
  });

  test('shift-double click opens full selection', function() {
    // Shift-double click works because the first click event selects the range
    // of items, then the second doubleclick event opens that whole selection.
    customClick(items[0]);
    simulateDoubleClick(items[1], {shiftKey: true});

    assertOpenedTabs(['http://111/', 'http://12/']);
  });

  test('control-double click opens full selection', function() {
    customClick(items[0]);
    simulateDoubleClick(items[2], {ctrlKey: true});

    assertOpenedTabs(['http://111/', 'http://13/']);
  });
});
