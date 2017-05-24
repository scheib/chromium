// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Handle the bookmark bar and theme change requests from the C++ side.
var ntp = {
  /** @param {string} attached */
  setBookmarkBarAttached: function(attached) {
    document.documentElement.setAttribute('bookmarkbarattached', attached);
  },

  /** @param {!{hasCustomBackground: boolean}} themeData */
  themeChanged: function(themeData) {
    document.documentElement.setAttribute('hascustombackground',
                                          themeData.hasCustomBackground);
    $('incognitothemecss').href =
        'chrome://theme/css/incognito_new_tab_theme.css?' + Date.now();
  },
};

// Let the width of two lists of bulletpoints in a horizontal alignment
// determine the maximum content width.
window.addEventListener('load', function() {
  var bulletpoints = document.querySelectorAll('.bulletpoints');
  var content = document.querySelector('.content');

  // Unless this is the first load of the Incognito NTP in this session and
  // with this font size, we already have the maximum content width determined.
  var fontSize = window.getComputedStyle(document.body).fontSize;
  var maxWidth = localStorage[fontSize] ||
      (bulletpoints[0].offsetWidth + bulletpoints[1].offsetWidth +
           40 /* margin */ + 2 /* offsetWidths may be rounded down */);

  // Save the data for quicker access when the NTP is reloaded. Note that since
  // we're in the Incognito mode, the local storage is ephemeral and the data
  // will be discarded when the session ends.
  localStorage[fontSize] = maxWidth;

  // Limit the maximum width to 600px. That might force the two lists
  // of bulletpoints under each other, in which case we must swap the left
  // and right margin.
  if (maxWidth > 600) {
    maxWidth = 600;

    bulletpoints[1].classList.add('tooWide');
  }

  content.style.maxWidth = maxWidth + "px";
});
