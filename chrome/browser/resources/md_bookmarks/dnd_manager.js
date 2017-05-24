// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

cr.define('bookmarks', function() {
  /**
   * @param {BookmarkElement} element
   * @return {boolean}
   */
  function isBookmarkItem(element) {
    return element.tagName == 'BOOKMARKS-ITEM';
  }

  /**
   * @param {BookmarkElement} element
   * @return {boolean}
   */
  function isBookmarkFolderNode(element) {
    return element.tagName == 'BOOKMARKS-FOLDER-NODE';
  }

  /**
   * @param {BookmarkElement} element
   * @return {boolean}
   */
  function isBookmarkList(element) {
    return element.tagName == 'BOOKMARKS-LIST';
  }

  /**
   * @param {Array<!Element>|undefined} path
   * @return {BookmarkElement}
   */
  function getBookmarkElement(path) {
    if (!path)
      return null;

    for (var i = 0; i < path.length; i++) {
      if (isBookmarkItem(path[i]) || isBookmarkFolderNode(path[i]) ||
          isBookmarkList(path[i]))
        return path[i];
    }
    return null;
  }

  /**
   * @param {BookmarkElement} bookmarkElement
   * @return {BookmarkNode}
   */
  function getBookmarkNode(bookmarkElement) {
    return bookmarks.Store.getInstance().data.nodes[bookmarkElement.itemId];
  }

  /**
   * Contains and provides utility methods for drag data sent by the
   * bookmarkManagerPrivate API.
   * @constructor
   */
  function DragInfo() {
    /** @type {DragData} */
    this.dragData = null;
  }

  DragInfo.prototype = {
    /** @param {DragData} newDragData */
    handleChromeDragEnter: function(newDragData) {
      this.dragData = newDragData;
    },

    clearDragData: function() {
      this.dragData = null;
    },

    /** @return {boolean} */
    isDragValid: function() {
      return !!this.dragData;
    },

    /** @return {boolean} */
    isSameProfile: function() {
      return !!this.dragData && this.dragData.sameProfile;
    },

    /** @return {boolean} */
    isDraggingFolders: function() {
      return !!this.dragData && this.dragData.elements.some(function(node) {
        return !node.url;
      });
    },

    /** @return {boolean} */
    isDraggingBookmark: function(bookmarkId) {
      return !!this.dragData && this.isSameProfile() &&
          this.dragData.elements.some(function(node) {
            return node.id == bookmarkId;
          });
    },

    /** @return {boolean} */
    isDraggingChildBookmark: function(folderId) {
      return !!this.dragData && this.isSameProfile() &&
          this.dragData.elements.some(function(node) {
            return node.parentId == folderId;
          });
    },

    /** @return {boolean} */
    isDraggingFolderToDescendant: function(itemId, nodes) {
      if (!this.isSameProfile())
        return false;

      var parentId = nodes[itemId].parentId;
      var parents = {};
      while (parentId) {
        parents[parentId] = true;
        parentId = nodes[parentId].parentId;
      }

      return !!this.dragData && this.dragData.elements.some(function(node) {
        return parents[node.id];
      });
    },
  };


  /**
   * Manages auto expanding of sidebar folders on hover while dragging.
   * @constructor
   */
  function AutoExpander() {
    /** @const {number} */
    this.EXPAND_FOLDER_DELAY = 400;

    /** @private {number} */
    this.lastTimestamp_ = 0;

    /** @private {BookmarkElement|null} */
    this.lastElement_ = null;

    /** @private {number} */
    this.testTimestamp_ = 0;
  }

  AutoExpander.prototype = {
    /**
     * @param {Event} e
     * @param {?BookmarkElement} overElement
     */
    update: function(e, overElement) {
      var eventTimestamp = this.testTimestamp_ || e.timeStamp;
      var itemId = overElement ? overElement.itemId : null;
      var store = bookmarks.Store.getInstance();

      // If hovering over the same folder as last update, open the folder after
      // the delay has passed.
      if (overElement && overElement == this.lastElement_) {
        if (eventTimestamp - this.lastTimestamp_ < this.EXPAND_FOLDER_DELAY)
          return;

        var action = bookmarks.actions.changeFolderOpen(itemId, true);
        store.dispatch(action);
      } else if (
          overElement && isBookmarkFolderNode(overElement) &&
          bookmarks.util.hasChildFolders(itemId, store.data.nodes) &&
          store.data.closedFolders.has(itemId)) {
        // Since this is a closed folder node that has children, set the auto
        // expander to this element.
        this.lastTimestamp_ = eventTimestamp;
        this.lastElement_ = overElement;
        return;
      }

      // If the folder has been expanded or we have moved to a different
      // element, reset the auto expander.
      this.lastTimestamp_ = 0;
      this.lastElement_ = null;
    },
  };

  /**
   * Encapsulates the behavior of the drag and drop indicator which puts a line
   * between items or highlights folders which are valid drop targets.
   * @constructor
   */
  function DropIndicator() {
    /**
     * @private {number|null} Timer id used to help minimize flicker.
     */
    this.removeDropIndicatorTimer_ = null;

    /**
     * The element that had a style applied it to indicate the drop location.
     * This is used to easily remove the style when necessary.
     * @private {BookmarkElement|null}
     */
    this.lastIndicatorElement_ = null;

    /**
     * The style that was applied to indicate the drop location.
     * @private {?string|null}
     */
    this.lastIndicatorClassName_ = null;

    /**
     * Used to instantly remove the indicator style in tests.
     * @private {function((Function|null|string), number)}
     */
    this.setTimeout = window.setTimeout.bind(window);
  }

  DropIndicator.prototype = {
    /**
     * Applies the drop indicator style on the target element and stores that
     * information to easily remove the style in the future.
     * @param {HTMLElement} indicatorElement
     * @param {DropPosition} position
     */
    addDropIndicatorStyle: function(indicatorElement, position) {
      var indicatorStyleName = position == DropPosition.ABOVE ?
          'drag-above' :
          position == DropPosition.BELOW ? 'drag-below' : 'drag-on';

      this.lastIndicatorElement_ = indicatorElement;
      this.lastIndicatorClassName_ = indicatorStyleName;

      indicatorElement.classList.add(indicatorStyleName);
    },

    /**
     * Clears the drop indicator style from the last drop target.
     */
    removeDropIndicatorStyle: function() {
      if (!this.lastIndicatorElement_ || !this.lastIndicatorClassName_)
        return;

      this.lastIndicatorElement_.classList.remove(this.lastIndicatorClassName_);
      this.lastIndicatorElement_ = null;
      this.lastIndicatorClassName_ = null;
    },

    /**
     * Displays the drop indicator on the current drop target to give the
     * user feedback on where the drop will occur.
     * @param {DropDestination} dropDest
     */
    update: function(dropDest) {
      window.clearTimeout(this.removeDropIndicatorTimer_);

      var indicatorElement = dropDest.element.getDropTarget();
      var position = dropDest.position;

      this.removeDropIndicatorStyle();
      this.addDropIndicatorStyle(indicatorElement, position);
    },

    /**
     * Stop displaying the drop indicator.
     */
    finish: function() {
      // The use of a timeout is in order to reduce flickering as we move
      // between valid drop targets.
      window.clearTimeout(this.removeDropIndicatorTimer_);
      this.removeDropIndicatorTimer_ = this.setTimeout(function() {
        this.removeDropIndicatorStyle();
      }.bind(this), 100);
    },
  };

  /**
   * Manages drag and drop events for the bookmarks-app.
   * @constructor
   */
  function DNDManager() {
    /** @private {bookmarks.DragInfo} */
    this.dragInfo_ = null;

    /** @private {?DropDestination} */
    this.dropDestination_ = null;

    /** @private {bookmarks.DropIndicator} */
    this.dropIndicator_ = null;

    /** @private {Object<string, function(!Event)>} */
    this.documentListeners_ = null;

    /**
     * Used to instantly clearDragData in tests.
     * @private {function((Function|null|string), number)}
     */
    this.setTimeout_ = window.setTimeout.bind(window);
  }

  DNDManager.prototype = {
    init: function() {
      this.dragInfo_ = new DragInfo();
      this.dropIndicator_ = new DropIndicator();
      this.autoExpander_ = new AutoExpander();

      this.documentListeners_ = {
        'dragstart': this.onDragStart_.bind(this),
        'dragenter': this.onDragEnter_.bind(this),
        'dragover': this.onDragOver_.bind(this),
        'dragleave': this.onDragLeave_.bind(this),
        'drop': this.onDrop_.bind(this),
        'dragend': this.clearDragData_.bind(this),
        'mouseup': this.clearDragData_.bind(this),
        // TODO(calamity): Add touch support.
      };
      for (var event in this.documentListeners_)
        document.addEventListener(event, this.documentListeners_[event]);

      chrome.bookmarkManagerPrivate.onDragEnter.addListener(
          this.dragInfo_.handleChromeDragEnter.bind(this.dragInfo_));
      chrome.bookmarkManagerPrivate.onDragLeave.addListener(
          this.clearDragData_.bind(this));
      chrome.bookmarkManagerPrivate.onDrop.addListener(
          this.clearDragData_.bind(this));
    },

    destroy: function() {
      for (var event in this.documentListeners_)
        document.removeEventListener(event, this.documentListeners_[event]);
    },

    /** @private */
    onDragLeave_: function() {
      this.dropIndicator_.finish();
    },

    /**
     * @private
     * @param {!Event} e
     */
    onDrop_: function(e) {
      if (this.dropDestination_) {
        e.preventDefault();

        var dropInfo = this.calculateDropInfo_(this.dropDestination_);
        if (dropInfo.index != -1)
          chrome.bookmarkManagerPrivate.drop(dropInfo.parentId, dropInfo.index);
        else
          chrome.bookmarkManagerPrivate.drop(dropInfo.parentId);
      }

      this.dropDestination_ = null;
      this.dropIndicator_.finish();
    },

    /**
     * @param {DropDestination} dropDestination
     * @return {{parentId: string, index: number}}
     */
    calculateDropInfo_: function(dropDestination) {
      if (isBookmarkList(dropDestination.element)) {
        return {
          index: 0,
          parentId: bookmarks.Store.getInstance().data.selectedFolder,
        };
      }

      var node = getBookmarkNode(dropDestination.element);
      var position = dropDestination.position;
      var index = -1;
      var parentId = node.id;

      if (position != DropPosition.ON) {
        var state = bookmarks.Store.getInstance().data;

        // Drops between items in the normal list and the sidebar use the drop
        // destination node's parent.
        parentId = assert(node.parentId);
        index = state.nodes[parentId].children.indexOf(node.id);

        if (position == DropPosition.BELOW)
          index++;
      }

      return {
        index: index,
        parentId: parentId,
      };
    },

    /** @private */
    clearDragData_: function() {
      // Defer the clearing of the data so that the bookmark manager API's drop
      // event doesn't clear the drop data before the web drop event has a
      // chance to execute (on Mac).
      this.setTimeout_(function() {
        this.dragInfo_.clearDragData();
        this.dropDestination_ = null;
        this.dropIndicator_.finish();
      }.bind(this), 0);
    },

    /**
     * @private
     * @param {Event} e
     */
    onDragStart_: function(e) {
      var dragElement = getBookmarkElement(e.path);
      if (!dragElement)
        return;

      var store = bookmarks.Store.getInstance();
      var dragId = dragElement.itemId;

      // Determine the selected bookmarks.
      var state = store.data;
      var draggedNodes = Array.from(state.selection.items);

      // Change selection to the dragged node if the node is not part of the
      // existing selection.
      if (isBookmarkFolderNode(dragElement) ||
          draggedNodes.indexOf(dragId) == -1) {
        store.dispatch(bookmarks.actions.deselectItems());
        if (!isBookmarkFolderNode(dragElement)) {
          store.dispatch(bookmarks.actions.selectItem(dragId, state, {
            clear: false,
            range: false,
            toggle: false,
          }));
        }
        draggedNodes = [dragId];
      }

      e.preventDefault();

      // If we are dragging a single link, we can do the *Link* effect.
      // Otherwise, we only allow copy and move.
      if (e.dataTransfer) {
        e.dataTransfer.effectAllowed =
            draggedNodes.length == 1 && state.nodes[draggedNodes[0]].url ?
            'copyLink' :
            'copyMove';
      }

      // TODO(calamity): account for touch.
      chrome.bookmarkManagerPrivate.startDrag(draggedNodes, false);
    },

    /**
     * @private
     * @param {Event} e
     */
    onDragEnter_: function(e) {
      e.preventDefault();
    },

    /**
     * @private
     * @param {Event} e
     */
    onDragOver_: function(e) {
      this.dropDestination_ = null;

      // This is necessary to actually trigger the 'none' effect, even though
      // the event will have this set to 'none' already.
      if (e.dataTransfer)
        e.dataTransfer.dropEffect = 'none';

      // Allow normal DND on text inputs.
      if (e.path[0].tagName == 'INPUT')
        return;

      // The default operation is to allow dropping links etc to do
      // navigation. We never want to do that for the bookmark manager.
      e.preventDefault();

      if (!this.dragInfo_.isDragValid())
        return;

      var overElement = getBookmarkElement(e.path);
      this.autoExpander_.update(e, overElement);
      if (!overElement)
        return;


      // Now we know that we can drop. Determine if we will drop above, on or
      // below based on mouse position etc.
      this.dropDestination_ =
          this.calculateDropDestination_(e.clientY, overElement);
      if (!this.dropDestination_) {
        this.dropIndicator_.finish();
        return;
      }

      if (e.dataTransfer) {
        e.dataTransfer.dropEffect =
            this.dragInfo_.isSameProfile() ? 'move' : 'copy';
      }

      this.dropIndicator_.update(this.dropDestination_);
    },

    /**
     * This function determines where the drop will occur.
     * @private
     * @param {number} elementClientY
     * @param {!BookmarkElement} overElement
     * @return {?DropDestination} If no valid drop position is found, null,
     *   otherwise:
     *       element - The target element that will receive the drop.
     *       position - A |DropPosition| relative to the |element|.
     */
    calculateDropDestination_: function(elementClientY, overElement) {
      var validDropPositions = this.calculateValidDropPositions_(overElement);
      if (validDropPositions == DropPosition.NONE)
        return null;

      var above = validDropPositions & DropPosition.ABOVE;
      var below = validDropPositions & DropPosition.BELOW;
      var on = validDropPositions & DropPosition.ON;
      var rect = overElement.getDropTarget().getBoundingClientRect();
      var yRatio = (elementClientY - rect.top) / rect.height;

      if (above && (yRatio <= .25 || yRatio <= .5 && (!below || !on)))
        return {element: overElement, position: DropPosition.ABOVE};

      if (below && (yRatio > .75 || yRatio > .5 && (!above || !on)))
        return {element: overElement, position: DropPosition.BELOW};

      if (on)
        return {element: overElement, position: DropPosition.ON};

      return null;
    },

    /**
     * Determines the valid drop positions for the given target element.
     * @private
     * @param {!BookmarkElement} overElement The element that we are currently
     *     dragging over.
     * @return {DropPosition} An bit field enumeration of valid drop locations.
     */
    calculateValidDropPositions_: function(overElement) {
      var dragInfo = this.dragInfo_;
      var state = bookmarks.Store.getInstance().data;
      var itemId = overElement.itemId;

      // Drags aren't allowed onto the search result list.
      if ((isBookmarkList(overElement) || isBookmarkItem(overElement)) &&
          bookmarks.util.isShowingSearch(state)) {
        return DropPosition.NONE;
      }

      if (isBookmarkList(overElement))
        itemId = state.selectedFolder;

      // Drags of a bookmark onto itself or of a folder into its children aren't
      // allowed.
      if (dragInfo.isDraggingBookmark(itemId) ||
          dragInfo.isDraggingFolderToDescendant(itemId, state.nodes)) {
        return DropPosition.NONE;
      }

      var validDropPositions = this.calculateDropAboveBelow_(overElement);
      if (this.canDropOn_(overElement))
        validDropPositions |= DropPosition.ON;

      return validDropPositions;
    },

    /**
     * @private
     * @param {BookmarkElement} overElement
     * @return {DropPosition}
     */
    calculateDropAboveBelow_: function(overElement) {
      var dragInfo = this.dragInfo_;
      var state = bookmarks.Store.getInstance().data;

      if (isBookmarkList(overElement))
        return DropPosition.NONE;

      // We cannot drop between Bookmarks bar and Other bookmarks.
      if (getBookmarkNode(overElement).parentId == ROOT_NODE_ID)
        return DropPosition.NONE;

      var isOverFolderNode = isBookmarkFolderNode(overElement);

      // We can only drop between items in the tree if we have any folders.
      if (isOverFolderNode && !dragInfo.isDraggingFolders())
        return DropPosition.NONE;

      var validDropPositions = DropPosition.NONE;

      // Cannot drop above if the item above is already in the drag source.
      var previousElem = overElement.previousElementSibling;
      if (!previousElem || !dragInfo.isDraggingBookmark(previousElem.itemId))
        validDropPositions |= DropPosition.ABOVE;

      // Don't allow dropping below an expanded sidebar folder item since it is
      // confusing to the user anyway.
      if (isOverFolderNode && !state.closedFolders.has(overElement.itemId) &&
          bookmarks.util.hasChildFolders(overElement.itemId, state.nodes)) {
        return validDropPositions;
      }

      var nextElement = overElement.nextElementSibling;

      // Cannot drop below if the item below is already in the drag source.
      if (!nextElement || !dragInfo.isDraggingBookmark(nextElement.itemId))
        validDropPositions |= DropPosition.BELOW;

      return validDropPositions;
    },

    /**
     * Determine whether we can drop the dragged items on the drop target.
     * @private
     * @param {!BookmarkElement} overElement The element that we are currently
     *     dragging over.
     * @return {boolean} Whether we can drop the dragged items on the drop
     *     target.
     */
    canDropOn_: function(overElement) {
      // Allow dragging onto empty bookmark lists.
      if (isBookmarkList(overElement)) {
        var state = bookmarks.Store.getInstance().data;
        return state.selectedFolder &&
            state.nodes[state.selectedFolder].children.length == 0;
      }

      // We can only drop on a folder.
      if (getBookmarkNode(overElement).url)
        return false;

      return !this.dragInfo_.isDraggingChildBookmark(overElement.itemId)
    },

    disableTimeoutsForTesting: function() {
      this.setTimeout_ = function(fn) {
        fn();
      };
      this.dropIndicator_.setTimeout = this.setTimeout_;
    }
  };

  return {
    DNDManager: DNDManager,
    DragInfo: DragInfo,
    DropIndicator: DropIndicator,
  };
});
