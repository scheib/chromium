// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

suite('selection state', function() {
  var selection;
  var action;

  function select(items, anchor, clear, toggle) {
    return {
      name: 'select-items',
      clear: clear,
      anchor: anchor,
      items: items,
      toggle: toggle,
    };
  }

  setup(function() {
    selection = {
      anchor: null,
      items: new Set(),
    };
  });

  test('can select an item', function() {
    action = select(['1'], '1', true, false);
    selection = bookmarks.SelectionState.updateSelection(selection, action);

    assertDeepEquals(['1'], normalizeSet(selection.items));
    assertEquals('1', selection.anchor);

    // Replace current selection.
    action = select(['2'], '2', true, false);
    selection = bookmarks.SelectionState.updateSelection(selection, action);
    assertDeepEquals(['2'], normalizeSet(selection.items));
    assertEquals('2', selection.anchor);

    // Add to current selection.
    action = select(['3'], '3', false, false);
    selection = bookmarks.SelectionState.updateSelection(selection, action);
    assertDeepEquals(['2', '3'], normalizeSet(selection.items));
    assertEquals('3', selection.anchor);
  });

  test('can select multiple items', function() {
    action = select(['1', '2', '3'], '3', true, false);
    selection = bookmarks.SelectionState.updateSelection(selection, action);
    assertDeepEquals(['1', '2', '3'], normalizeSet(selection.items));

    action = select(['3', '4'], '4', false, false);
    selection = bookmarks.SelectionState.updateSelection(selection, action);
    assertDeepEquals(['1', '2', '3', '4'], normalizeSet(selection.items));
  });

  test('is cleared when selected folder changes', function() {
    action = select(['1', '2', '3'], '3', true, false);
    selection = bookmarks.SelectionState.updateSelection(selection, action);

    action = bookmarks.actions.selectFolder('2');
    selection = bookmarks.SelectionState.updateSelection(selection, action);
    assertDeepEquals({}, selection.items);
  });

  test('is cleared when search finished', function() {
    action = select(['1', '2', '3'], '3', true, false);
    selection = bookmarks.SelectionState.updateSelection(selection, action);

    action = bookmarks.actions.setSearchResults(['2']);
    selection = bookmarks.SelectionState.updateSelection(selection, action);
    assertDeepEquals({}, selection.items);
  });

  test('is cleared when search cleared', function() {
    action = select(['1', '2', '3'], '3', true, false);
    selection = bookmarks.SelectionState.updateSelection(selection, action);

    action = bookmarks.actions.clearSearch();
    selection = bookmarks.SelectionState.updateSelection(selection, action);
    assertDeepEquals({}, selection.items);
  });

  test('deselect items', function() {
    action = select(['1', '2', '3'], '3', true, false);
    selection = bookmarks.SelectionState.updateSelection(selection, action);

    action = bookmarks.actions.deselectItems();
    selection = bookmarks.SelectionState.updateSelection(selection, action);
    assertDeepEquals({}, selection.items);
  });

  test('toggle an item', function() {
    action = select(['1', '2', '3'], '3', true, false);
    selection = bookmarks.SelectionState.updateSelection(selection, action);

    action = select(['1'], '3', false, true);
    selection = bookmarks.SelectionState.updateSelection(selection, action);
    assertDeepEquals(['2', '3'], normalizeSet(selection.items));
  });

  test('deselects items when they are deleted', function() {
    var nodeMap = testTree(createFolder('0', [
      createFolder(
          '1',
          [
            createItem('2'),
            createItem('3'),
            createItem('4'),
          ]),
      createItem('5'),
    ]));

    action = select(['2', '4', '5'], '4', true, false);
    selection = bookmarks.SelectionState.updateSelection(selection, action);

    action = bookmarks.actions.removeBookmark('1', '0', 0, nodeMap);
    selection = bookmarks.SelectionState.updateSelection(selection, action);

    assertDeepEquals(['5'], normalizeSet(selection.items));
    assertEquals(null, selection.anchor);
  });
});

suite('closed folder state', function() {
  var nodes;
  var closedFolders;
  var action;

  setup(function() {
    nodes = testTree(
        createFolder(
            '1',
            [
              createFolder('2', []),
              createItem('3'),
            ]),
        createFolder('4', []));
    closedFolders = new Set();
  });

  test('toggle folder open state', function() {
    action = bookmarks.actions.changeFolderOpen('2', false);
    closedFolders = bookmarks.ClosedFolderState.updateClosedFolders(
        closedFolders, action, nodes);
    assertFalse(closedFolders.has('1'));
    assertTrue(closedFolders.has('2'));
  });

  test('select folder with closed parent', function() {
    // Close '1'
    action = bookmarks.actions.changeFolderOpen('1', false);
    closedFolders = bookmarks.ClosedFolderState.updateClosedFolders(
        closedFolders, action, nodes);
    assertTrue(closedFolders.has('1'));
    assertFalse(closedFolders.has('2'));

    // Should re-open when '2' is selected.
    action = bookmarks.actions.selectFolder('2');
    closedFolders = bookmarks.ClosedFolderState.updateClosedFolders(
        closedFolders, action, nodes);
    assertFalse(closedFolders.has('1'));
  });

  test('move nodes in a closed folder', function() {
    // Moving bookmark items should not open folders.
    closedFolders = new Set(['1']);
    action = bookmarks.actions.moveBookmark('3', '1', 1, '1', 0);
    closedFolders = bookmarks.ClosedFolderState.updateClosedFolders(
        closedFolders, action, nodes);

    assertTrue(closedFolders.has('1'));

    // Moving folders should open their parents.
    closedFolders = new Set(['1', '2']);
    action = bookmarks.actions.moveBookmark('4', '2', 0, '0', 1);
    closedFolders = bookmarks.ClosedFolderState.updateClosedFolders(
        closedFolders, action, nodes);
    assertFalse(closedFolders.has('1'));
    assertFalse(closedFolders.has('2'));
  });
});

suite('selected folder', function() {
  var nodes;
  var selectedFolder;
  var action;

  setup(function() {
    nodes = testTree(createFolder('1', [
      createFolder(
          '2',
          [
            createFolder('3', []),
            createFolder('4', []),
          ]),
    ]));

    selectedFolder = '1';
  });

  test('updates from selectFolder action', function() {
    action = bookmarks.actions.selectFolder('2');
    selectedFolder = bookmarks.SelectedFolderState.updateSelectedFolder(
        selectedFolder, action, nodes);
    assertEquals('2', selectedFolder);
  });

  test('updates when parent of selected folder is closed', function() {
    action = bookmarks.actions.selectFolder('2');
    selectedFolder = bookmarks.SelectedFolderState.updateSelectedFolder(
        selectedFolder, action, nodes);

    action = bookmarks.actions.changeFolderOpen('1', false);
    selectedFolder = bookmarks.SelectedFolderState.updateSelectedFolder(
        selectedFolder, action, nodes);
    assertEquals('1', selectedFolder);
  });

  test('selects ancestor when selected folder is deleted', function() {
    action = bookmarks.actions.selectFolder('3');
    selectedFolder = bookmarks.SelectedFolderState.updateSelectedFolder(
        selectedFolder, action, nodes);

    // Delete the selected folder:
    action = bookmarks.actions.removeBookmark('3', '2', 0, nodes);
    selectedFolder = bookmarks.SelectedFolderState.updateSelectedFolder(
        selectedFolder, action, nodes);

    assertEquals('2', selectedFolder);

    action = bookmarks.actions.selectFolder('4');
    selectedFolder = bookmarks.SelectedFolderState.updateSelectedFolder(
        selectedFolder, action, nodes);

    // Delete an ancestor of the selected folder:
    action = bookmarks.actions.removeBookmark('2', '1', 0, nodes);
    selectedFolder = bookmarks.SelectedFolderState.updateSelectedFolder(
        selectedFolder, action, nodes);

    assertEquals('1', selectedFolder);
  });
});

suite('node state', function() {
  var nodes;
  var action;

  setup(function() {
    nodes = testTree(
        createFolder(
            '1',
            [
              createItem('2', {title: 'a', url: 'a.com'}),
              createItem('3'),
              createFolder('4', []),
            ]),
        createFolder('5', []));
  });

  test('updates when a node is edited', function() {
    action = bookmarks.actions.editBookmark('2', {title: 'b'});
    nodes = bookmarks.NodeState.updateNodes(nodes, action);

    assertEquals('b', nodes['2'].title);
    assertEquals('a.com', nodes['2'].url);

    action = bookmarks.actions.editBookmark('2', {title: 'c', url: 'c.com'});
    nodes = bookmarks.NodeState.updateNodes(nodes, action);

    assertEquals('c', nodes['2'].title);
    assertEquals('c.com', nodes['2'].url);

    action = bookmarks.actions.editBookmark('4', {title: 'folder'});
    nodes = bookmarks.NodeState.updateNodes(nodes, action);

    assertEquals('folder', nodes['4'].title);
    assertEquals(undefined, nodes['4'].url);

    // Cannot edit URL of a folder:
    action = bookmarks.actions.editBookmark('4', {url: 'folder.com'});
    nodes = bookmarks.NodeState.updateNodes(nodes, action);

    assertEquals('folder', nodes['4'].title);
    assertEquals(undefined, nodes['4'].url);
  });

  test('updates when a node is created', function() {
    // Create a folder.
    var folder = {
      id: '6',
      parentId: '1',
      index: 2,
    };
    action = bookmarks.actions.createBookmark(folder.id, folder);
    nodes = bookmarks.NodeState.updateNodes(nodes, action);

    assertEquals('1', nodes['6'].parentId);
    assertDeepEquals([], nodes['6'].children);
    assertDeepEquals(['2', '3', '6', '4'], nodes['1'].children);

    // Add a new item to that folder.
    var item = {
      id: '7',
      parentId: '6',
      index: 0,
      url: 'https://www.example.com',
    };

    action = bookmarks.actions.createBookmark(item.id, item);
    nodes = bookmarks.NodeState.updateNodes(nodes, action);

    assertEquals('6', nodes['7'].parentId);
    assertEquals(undefined, nodes['7'].children);
    assertDeepEquals(['7'], nodes['6'].children);
  });

  test('updates when a node is deleted', function() {
    action = bookmarks.actions.removeBookmark('3', '1', 1, nodes);
    nodes = bookmarks.NodeState.updateNodes(nodes, action);

    assertDeepEquals(['2', '4'], nodes['1'].children);

    assertDeepEquals(['2', '4'], nodes['1'].children);
    assertEquals(undefined, nodes['3']);
  });

  test('removes all children of deleted nodes', function() {
    action = bookmarks.actions.removeBookmark('1', '0', 0, nodes);
    nodes = bookmarks.NodeState.updateNodes(nodes, action);

    assertDeepEquals(['0', '5'], Object.keys(nodes).sort());
  });

  test('updates when a node is moved', function() {
    // Move within the same folder backwards.
    action = bookmarks.actions.moveBookmark('3', '1', 0, '1', 1);
    nodes = bookmarks.NodeState.updateNodes(nodes, action);

    assertDeepEquals(['3', '2', '4'], nodes['1'].children);

    // Move within the same folder forwards.
    action = bookmarks.actions.moveBookmark('3', '1', 2, '1', 0);
    nodes = bookmarks.NodeState.updateNodes(nodes, action);

    assertDeepEquals(['2', '4', '3'], nodes['1'].children);

    // Move between different folders.
    action = bookmarks.actions.moveBookmark('4', '5', 0, '1', 1);
    nodes = bookmarks.NodeState.updateNodes(nodes, action);

    assertDeepEquals(['2', '3'], nodes['1'].children);
    assertDeepEquals(['4'], nodes['5'].children);
  });

  test('updates when children of a node are reordered', function() {
    action = bookmarks.actions.reorderChildren('1', ['4', '2', '3']);
    nodes = bookmarks.NodeState.updateNodes(nodes, action);

    assertDeepEquals(['4', '2', '3'], nodes['1'].children);
  });
});

suite('search state', function() {
  var state;

  setup(function() {
    // Search touches a few different things, so we test using the entire state.
    state = bookmarks.util.createEmptyState();
    state.nodes = testTree(createFolder('1', [
      createFolder(
          '2',
          [
            createItem('3'),
          ]),
    ]));
  });

  test('updates when search is started and finished', function() {
    var action;

    action = bookmarks.actions.selectFolder('2');
    state = bookmarks.reduceAction(state, action);

    action = bookmarks.actions.setSearchTerm('test');
    state = bookmarks.reduceAction(state, action);

    assertEquals('test', state.search.term);
    assertTrue(state.search.inProgress);

    // UI should not have changed yet.
    assertFalse(bookmarks.util.isShowingSearch(state));
    assertDeepEquals(['3'], bookmarks.util.getDisplayedList(state));

    action = bookmarks.actions.setSearchResults(['2', '3']);
    var searchedState = bookmarks.reduceAction(state, action);

    assertFalse(searchedState.search.inProgress);

    // UI changes once search results arrive.
    assertTrue(bookmarks.util.isShowingSearch(searchedState));
    assertDeepEquals(
        ['2', '3'], bookmarks.util.getDisplayedList(searchedState));

    // Case 1: Clear search by setting an empty search term.
    action = bookmarks.actions.setSearchTerm('');
    var clearedState = bookmarks.reduceAction(searchedState, action);

    // Should go back to displaying the contents of '2', which was shown before
    // the search.
    assertEquals('2', clearedState.selectedFolder);
    assertFalse(bookmarks.util.isShowingSearch(clearedState));
    assertDeepEquals(['3'], bookmarks.util.getDisplayedList(clearedState));
    assertEquals('', clearedState.search.term);
    assertDeepEquals([], clearedState.search.results);

    // Case 2: Clear search by selecting a new folder.
    action = bookmarks.actions.selectFolder('1');
    var selectedState = bookmarks.reduceAction(searchedState, action);

    assertEquals('1', selectedState.selectedFolder);
    assertFalse(bookmarks.util.isShowingSearch(selectedState));
    assertDeepEquals(['2'], bookmarks.util.getDisplayedList(selectedState));
    assertEquals('', selectedState.search.term);
    assertDeepEquals([], selectedState.search.results);
  });

  test('removes deleted nodes', function() {
    var action;

    action = bookmarks.actions.setSearchTerm('test');
    state = bookmarks.reduceAction(state, action);

    action = bookmarks.actions.setSearchResults(['1', '3', '2']);
    state = bookmarks.reduceAction(state, action);

    action = bookmarks.actions.removeBookmark('2', '1', 0, state.nodes);
    state = bookmarks.reduceAction(state, action);

    // 2 and 3 should be removed, since 2 was deleted and 3 was a descendant of
    // 2.
    assertDeepEquals(['1'], state.search.results);
  });
});
