// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/** @fileoverview Suite of tests for site-list. */

/**
 * An example pref with 2 blocked location items and 2 allowed. This pref
 * is also used for the All Sites category and therefore needs values for
 * all types, even though some might be blank.
 * @type {SiteSettingsPref}
 */
var prefs = {
  exceptions: {
    auto_downloads: [],
    background_sync: [],
    camera: [],
    cookies: [],
    geolocation: [
      {
        embeddingOrigin: 'https://bar-allow.com:443',
        origin: 'https://bar-allow.com:443',
        setting: 'allow',
        source: 'preference',
      },
      {
        embeddingOrigin: 'https://foo-allow.com:443',
        origin: 'https://foo-allow.com:443',
        setting: 'allow',
        source: 'preference',
      },
      {
        embeddingOrigin: 'https://bar-block.com:443',
        origin: 'https://bar-block.com:443',
        setting: 'block',
        source: 'preference',
      },
      {
        embeddingOrigin: 'https://foo-block.com:443',
        origin: 'https://foo-block.com:443',
        setting: 'block',
        source: 'preference',
      },
    ],
    images: [],
    javascript: [],
    mic: [],
    midiDevices: [],
    notifications: [],
    plugins: [],
    protectedContent: [],
    popups: [],
    subresource_filter: [],
    unsandboxed_plugins: [],
  }
};

/**
 * An example of prefs controlledBy policy.
 * @type {SiteSettingsPref}
 */
var prefsControlled = {
  exceptions: {
    plugins: [
      {
        embeddingOrigin: 'http://foo-block.com',
        origin: 'http://foo-block.com',
        setting: 'block',
        source: 'policy',
      },
    ]
  }
};

/**
 * An example pref with mixed schemes (present and absent).
 * @type {SiteSettingsPref}
 */
var prefsMixedSchemes = {
  exceptions: {
    geolocation: [
      {
        embeddingOrigin: 'https://foo-allow.com',
        origin: 'https://foo-allow.com',
        setting: 'allow',
        source: 'preference',
      },
      {
        embeddingOrigin: 'bar-allow.com',
        origin: 'bar-allow.com',
        setting: 'allow',
        source: 'preference',
      },
    ]
  }
};


/**
 * An example pref with exceptions with origins and patterns from
 * different providers.
 * @type {SiteSettingsPref}
 */
var prefsMixedProvider = {
  exceptions: {
    geolocation: [
      {
        embeddingOrigin: 'https://[*.]foo.com',
        origin: 'https://[*.]foo.com',
        setting: 'block',
        source: 'policy',
      },
      {
        embeddingOrigin: 'https://bar.foo.com',
        origin: 'https://bar.foo.com',
        setting: 'block',
        source: 'preference',
      },
      {
        embeddingOrigin: 'https://[*.]foo.com',
        origin: 'https://[*.]foo.com',
        setting: 'block',
        source: 'preference',
      },
    ]
  }
};

/**
 * An example pref with with and without embeddingOrigin.
 * @type {SiteSettingsPref}
 */
var prefsMixedEmbeddingOrigin = {
  exceptions: {
    images: [
      {
        origin: 'https://foo.com',
        embeddingOrigin: 'https://example.com',
        setting: 'allow',
        source: 'preference',
      },
      {
        origin: 'https://bar.com',
        embeddingOrigin: '',
        setting: 'allow',
        source: 'preference',
      },
    ],
  }
};

/**
 * An example pref with multiple categories and multiple allow/block
 * state.
 * @type {SiteSettingsPref}
 */
var prefsVarious = {
  exceptions: {
    auto_downloads: [],
    background_sync: [],
    camera: [],
    cookies: [],
    geolocation: [
      {
        embeddingOrigin: '',
        incognito: false,
        origin: 'https://foo.com',
        setting: 'allow',
        source: 'preference',
      },
      {
        embeddingOrigin: '',
        incognito: false,
        origin: 'https://bar.com',
        setting: 'block',
        source: 'preference',
      },
    ],
    images: [],
    javascript: [],
    mic: [],
    midiDevices: [],
    notifications: [
      {
        embeddingOrigin: '',
        incognito: false,
        origin: 'https://google.com',
        setting: 'block',
        source: 'preference',
      },
      {
        embeddingOrigin: '',
        incognito: false,
        origin: 'https://bar.com',
        setting: 'block',
        source: 'preference',
      },
      {
        embeddingOrigin: '',
        incognito: false,
        origin: 'https://foo.com',
        setting: 'block',
        source: 'preference',
      },
    ],
    plugins: [],
    protectedContent: [],
    popups: [],
    subresource_filter: [],
    unsandboxed_plugins: [],
  }
};

/**
 * An example pref with 1 allowed location item.
 * @type {SiteSettingsPref}
 */
var prefsOneEnabled = {
  exceptions: {
    geolocation: [
      {
        embeddingOrigin: '',
        incognito: false,
        origin: 'https://foo-allow.com:443',
        setting: 'allow',
        source: 'preference',
      },
    ]
  }
};

/**
 * An example pref with 1 blocked location item.
 * @type {SiteSettingsPref}
 */
var prefsOneDisabled = {
  exceptions: {
    geolocation: [
      {
        embeddingOrigin: '',
        incognito: false,
        origin: 'https://foo-block.com:443',
        setting: 'block',
        source: 'preference',
      },
    ]
  }
};

/**
 * An example Cookies pref with 1 in each of the three categories.
 * @type {SiteSettingsPref}
 */
var prefsSessionOnly = {
  exceptions: {
    cookies: [
      {
        embeddingOrigin: '',
        incognito: false,
        origin: 'http://foo-block.com',
        setting: 'block',
        source: 'preference',
      },
      {
        embeddingOrigin: '',
        incognito: false,
        origin: 'http://foo-allow.com',
        setting: 'allow',
        source: 'preference',
      },
      {
        embeddingOrigin: '',
        incognito: false,
        origin: 'http://foo-session.com',
        setting: 'session_only',
        source: 'preference',
      },
    ]
  }
};

/**
 * An example Cookies pref with mixed incognito and regular settings.
 * @type {SiteSettingsPref}
 */
var prefsIncognito = {
  exceptions: {
    cookies: [
      // foo.com is blocked for regular sessions.
      {
        embeddingOrigin: '',
        incognito: false,
        origin: 'http://foo.com',
        setting: 'block',
        source: 'preference',
      },
      // bar.com is an allowed incognito item.
      {
        embeddingOrigin: '',
        incognito: true,
        origin: 'http://bar.com',
        setting: 'allow',
        source: 'preference',
      },
      // foo.com is allowed in incognito (overridden).
      {
        embeddingOrigin: '',
        incognito: true,
        origin: 'http://foo.com',
        setting: 'allow',
        source: 'preference',
      },

    ]
  }
};

/**
 * An example Javascript pref with a chrome-extension:// scheme.
 * @type {SiteSettingsPref}
 */
var prefsChromeExtension = {
  exceptions: {
    javascript: [
      {
        embeddingOrigin: '',
        incognito: false,
        origin: 'chrome-extension://cfhgfbfpcbnnbibfphagcjmgjfjmojfa/',
        setting: 'block',
        source: 'preference',
      },
    ]
  }
};


suite('SiteList', function() {
  /**
   * A site list element created before each test.
   * @type {SiteList}
   */
  var testElement;

  /**
   * The mock proxy object to use during test.
   * @type {TestSiteSettingsPrefsBrowserProxy}
   */
  var browserProxy = null;


  suiteSetup(function() {
    CrSettingsPrefs.setInitialized();
  });

  suiteTeardown(function() {
    CrSettingsPrefs.resetForTesting();
  });

  // Initialize a site-list before each test.
  setup(function() {
    browserProxy = new TestSiteSettingsPrefsBrowserProxy();
    settings.SiteSettingsPrefsBrowserProxyImpl.instance_ = browserProxy;
    PolymerTest.clearBody();
    testElement = document.createElement('site-list');
    document.body.appendChild(testElement);
  });

  teardown(function() {
    closeActionMenu();
    // The code being tested changes the Route. Reset so that state is not
    // leaked across tests.
    settings.resetRouteForTesting();
  });

  /**
   * Opens the action menu for a particular element in the list.
   * @param {number} index The index of the child element (which site) to
   *     open the action menu for.
   */
  function openActionMenu(index) {
    var item = testElement.$.listContainer.children[index];
    var dots = item.querySelector('#actionMenuButton');
    MockInteractions.tap(dots);
    Polymer.dom.flush();
  }

  /** Closes the action menu. */
  function closeActionMenu() {
    var menu = testElement.$$('dialog[is=cr-action-menu]');
    if (menu.open)
      menu.close();
  }

  /**
   * Asserts the menu looks as expected.
   * @param {Array<string>} items The items expected to show in the menu.
   */
  function assertMenu(items) {
    var menu = testElement.$$('dialog[is=cr-action-menu]');
    assertTrue(!!menu);
    var menuItems = menu.querySelectorAll('button:not([hidden])');
    assertEquals(items.length, menuItems.length);
    for (var i = 0; i < items.length; i++)
      assertEquals(items[i], menuItems[i].textContent.trim());
  }

  /**
   * @param {HTMLElement} listContainer Node with the exceptions listed.
   * @return {boolean} Whether the entry is incognito only.
   */
  function hasAnIncognito(listContainer) {
    var descriptions = listContainer.querySelectorAll('#siteDescription');
    for (var i = 0; i < descriptions.length; ++i) {
      if (descriptions[i].textContent == 'Current incognito session')
        return true;
    }
    return false;
  }

  /**
   * Configures the test element for a particular category.
   * @param {settings.ContentSettingsTypes} category The category to set up.
   * @param {settings.PermissionValues} subtype Type of list to use.
   * @param {Array<dictionary>} prefs The prefs to use.
   */
  function setUpCategory(category, subtype, prefs) {
    browserProxy.setPrefs(prefs);
    testElement.categorySubtype = subtype;
    // Some route is needed, but the actual route doesn't matter.
    testElement.currentRoute = {
      page: 'dummy',
      section: 'privacy',
      subpage: ['site-settings', 'site-settings-category-location'],
    };
    testElement.category = category;
  }

  test('read-only attribute', function() {
    setUpCategory(
        settings.ContentSettingsTypes.GEOLOCATION,
        settings.PermissionValues.ALLOW, prefsVarious);
    return browserProxy.whenCalled('getExceptionList')
        .then(function(contentType) {
          // Flush to be sure list container is populated.
          Polymer.dom.flush();
          var dotsMenu =
              testElement.$.listContainer.querySelector('#actionMenuButton');
          assertFalse(dotsMenu.hidden);
          testElement.setAttribute('read-only-list', true);
          Polymer.dom.flush();
          assertTrue(dotsMenu.hidden);
          testElement.removeAttribute('read-only-list');
          Polymer.dom.flush();
          assertFalse(dotsMenu.hidden);
        });
  });

  test('getExceptionList API used', function() {
    setUpCategory(
        settings.ContentSettingsTypes.GEOLOCATION,
        settings.PermissionValues.ALLOW, prefsEmpty);
    return browserProxy.whenCalled('getExceptionList')
        .then(function(contentType) {
          assertEquals(settings.ContentSettingsTypes.GEOLOCATION, contentType);
        });
  });

  test('Empty list', function() {
    setUpCategory(
        settings.ContentSettingsTypes.GEOLOCATION,
        settings.PermissionValues.ALLOW, prefsEmpty);
    return browserProxy.whenCalled('getExceptionList')
        .then(function(contentType) {
          assertEquals(settings.ContentSettingsTypes.GEOLOCATION, contentType);

          assertEquals(0, testElement.sites.length);

          assertEquals(
              settings.PermissionValues.ALLOW, testElement.categorySubtype);

          assertFalse(testElement.$.category.hidden);
        });
  });

  test('initial ALLOW state is correct', function() {
    setUpCategory(
        settings.ContentSettingsTypes.GEOLOCATION,
        settings.PermissionValues.ALLOW, prefs);
    return browserProxy.whenCalled('getExceptionList')
        .then(function(contentType) {
          assertEquals(settings.ContentSettingsTypes.GEOLOCATION, contentType);

          assertEquals(2, testElement.sites.length);
          assertEquals(
              prefs.exceptions.geolocation[0].origin,
              testElement.sites[0].origin);
          assertEquals(
              prefs.exceptions.geolocation[1].origin,
              testElement.sites[1].origin);
          assertEquals(
              settings.PermissionValues.ALLOW, testElement.categorySubtype);
          Polymer.dom.flush();  // Populates action menu.
          openActionMenu(0);
          assertMenu(['Block', 'Edit', 'Remove'], testElement);

          assertFalse(testElement.$.category.hidden);
        });
  });

  test('action menu closes when list changes', function() {
    setUpCategory(
        settings.ContentSettingsTypes.GEOLOCATION,
        settings.PermissionValues.ALLOW, prefs);
    var actionMenu = testElement.$$('dialog[is=cr-action-menu]');
    return browserProxy.whenCalled('getExceptionList')
        .then(function(contentType) {
          Polymer.dom.flush();  // Populates action menu.
          openActionMenu(0);
          assertTrue(actionMenu.open);

          browserProxy.resetResolver('getExceptionList');
          // Simulate a change in the underlying model.
          cr.webUIListenerCallback(
              'contentSettingSitePermissionChanged',
              settings.ContentSettingsTypes.GEOLOCATION);
          return browserProxy.whenCalled('getExceptionList');
        })
        .then(function() {
          // Check that the action menu was closed.
          assertFalse(actionMenu.open);
        });
  });

  test('exceptions are not reordered in non-ALL_SITES', function() {
    setUpCategory(
        settings.ContentSettingsTypes.GEOLOCATION,
        settings.PermissionValues.BLOCK, prefsMixedProvider);
    return browserProxy.whenCalled('getExceptionList')
        .then(function(contentType) {
          assertEquals(settings.ContentSettingsTypes.GEOLOCATION, contentType);

          assertEquals(3, testElement.sites.length);
          for (var i = 0; i < 3; i++) {
            assertEquals(
                prefsMixedProvider.exceptions.geolocation[0].origin,
                testElement.sites[0].origin);
            assertEquals(
                kControlledByLookup[prefsMixedProvider.exceptions.geolocation[0]
                                        .source],
                testElement.sites[0].controlledBy);
          }
        });
  });

  test('initial BLOCK state is correct', function() {
    var contentType = settings.ContentSettingsTypes.GEOLOCATION;
    var categorySubtype = settings.PermissionValues.BLOCK;
    setUpCategory(contentType, categorySubtype, prefs);
    return browserProxy.whenCalled('getExceptionList')
        .then(function(actualContentType) {
          assertEquals(contentType, actualContentType);
          assertEquals(categorySubtype, testElement.categorySubtype);

          assertEquals(2, testElement.sites.length);
          assertEquals(
              prefs.exceptions.geolocation[2].origin,
              testElement.sites[0].origin);
          assertEquals(
              prefs.exceptions.geolocation[3].origin,
              testElement.sites[1].origin);
          Polymer.dom.flush();  // Populates action menu.
          openActionMenu(0);
          assertMenu(['Allow', 'Edit', 'Remove'], testElement);

          assertFalse(testElement.$.category.hidden);
        });
  });

  test('initial SESSION ONLY state is correct', function() {
    var contentType = settings.ContentSettingsTypes.COOKIES;
    var categorySubtype = settings.PermissionValues.SESSION_ONLY;
    setUpCategory(contentType, categorySubtype, prefsSessionOnly);
    return browserProxy.whenCalled('getExceptionList')
        .then(function(actualContentType) {
          assertEquals(contentType, actualContentType);
          assertEquals(categorySubtype, testElement.categorySubtype);

          assertEquals(1, testElement.sites.length);
          assertEquals(
              prefsSessionOnly.exceptions.cookies[2].origin,
              testElement.sites[0].origin);

          Polymer.dom.flush();  // Populates action menu.
          openActionMenu(0);
          assertMenu(['Allow', 'Block', 'Edit', 'Remove'], testElement);

          assertFalse(testElement.$.category.hidden);
        });
  });

  test('update lists for incognito', function() {
    var contentType = settings.ContentSettingsTypes.PLUGINS;
    var categorySubtype = settings.PermissionValues.BLOCK;
    setUpCategory(contentType, categorySubtype, prefsControlled);
    var list = testElement.$.listContainer;
    return browserProxy.whenCalled('getExceptionList')
        .then(function(actualContentType) {
          Polymer.dom.flush();
          assertEquals(1, list.querySelectorAll('.list-item').length);
          assertFalse(hasAnIncognito(list));
          browserProxy.resetResolver('getExceptionList');
          browserProxy.setIncognito(true);
          return browserProxy.whenCalled('getExceptionList');
        })
        .then(function() {
          Polymer.dom.flush();
          assertEquals(2, list.querySelectorAll('.list-item').length);
          assertTrue(hasAnIncognito(list));
          browserProxy.resetResolver('getExceptionList');
          browserProxy.setIncognito(false);
          return browserProxy.whenCalled('getExceptionList');
        })
        .then(function() {
          Polymer.dom.flush();
          assertEquals(1, list.querySelectorAll('.list-item').length);
          assertFalse(hasAnIncognito(list));
          browserProxy.resetResolver('getExceptionList');
          browserProxy.setIncognito(true);
          return browserProxy.whenCalled('getExceptionList');
        })
        .then(function() {
          Polymer.dom.flush();
          assertEquals(2, list.querySelectorAll('.list-item').length);
          assertTrue(hasAnIncognito(list));
        });
  });

  test('initial INCOGNITO BLOCK state is correct', function() {
    var contentType = settings.ContentSettingsTypes.COOKIES;
    var categorySubtype = settings.PermissionValues.BLOCK;
    setUpCategory(contentType, categorySubtype, prefsIncognito);
    return browserProxy.whenCalled('getExceptionList')
        .then(function(actualContentType) {
          assertEquals(contentType, actualContentType);
          assertEquals(categorySubtype, testElement.categorySubtype);

          assertEquals(1, testElement.sites.length);
          assertEquals(
              prefsIncognito.exceptions.cookies[0].origin,
              testElement.sites[0].origin);

          Polymer.dom.flush();  // Populates action menu.
          openActionMenu(0);
          // 'Clear on exit' is visible as this is not an incognito item.
          assertMenu(['Allow', 'Clear on exit', 'Edit', 'Remove'], testElement);

          // Select 'Remove' from menu.
          var remove = testElement.$.reset;
          assertTrue(!!remove);
          MockInteractions.tap(remove);
          return browserProxy.whenCalled('resetCategoryPermissionForOrigin');
        })
        .then(function(args) {
          assertEquals('http://foo.com', args[0]);
          assertEquals('', args[1]);
          assertEquals(contentType, args[2]);
          assertFalse(args[3]);  // Incognito.
        });
  });

  test('initial INCOGNITO ALLOW state is correct', function() {
    var contentType = settings.ContentSettingsTypes.COOKIES;
    var categorySubtype = settings.PermissionValues.ALLOW;
    setUpCategory(contentType, categorySubtype, prefsIncognito);
    return browserProxy.whenCalled('getExceptionList')
        .then(function(actualContentType) {
          assertEquals(contentType, actualContentType);
          assertEquals(categorySubtype, testElement.categorySubtype);

          assertEquals(2, testElement.sites.length);
          assertEquals(
              prefsIncognito.exceptions.cookies[1].origin,
              testElement.sites[0].origin);
          assertEquals(
              prefsIncognito.exceptions.cookies[2].origin,
              testElement.sites[1].origin);

          Polymer.dom.flush();  // Populates action menu.
          openActionMenu(0);
          // 'Clear on exit' is hidden for incognito items.
          assertMenu(['Block', 'Edit', 'Remove'], testElement);
          closeActionMenu();

          // Select 'Remove' from menu on 'foo.com'.
          openActionMenu(1);
          var remove = testElement.$.reset;
          assertTrue(!!remove);
          MockInteractions.tap(remove);
          return browserProxy.whenCalled('resetCategoryPermissionForOrigin');
        })
        .then(function(args) {
          assertEquals('http://foo.com', args[0]);
          assertEquals('', args[1]);
          assertEquals(contentType, args[2]);
          assertTrue(args[3]);  // Incognito.
        });
  });

  test('reset button works for read-only content types', function() {
    testElement.readOnlyList = true;
    Polymer.dom.flush();

    var contentType = settings.ContentSettingsTypes.GEOLOCATION;
    var categorySubtype = settings.PermissionValues.ALLOW;
    setUpCategory(contentType, categorySubtype, prefsOneEnabled);
    return browserProxy.whenCalled('getExceptionList')
        .then(function(actualContentType) {
          assertEquals(contentType, actualContentType);
          assertEquals(categorySubtype, testElement.categorySubtype);

          assertEquals(1, testElement.sites.length);
          assertEquals(
              prefsOneEnabled.exceptions.geolocation[0].origin,
              testElement.sites[0].origin);

          Polymer.dom.flush();

          var item = testElement.$.listContainer.children[0];

          // Assert action button is hidden.
          var dots = item.querySelector('#actionMenuButton');
          assertTrue(!!dots);
          assertTrue(dots.hidden);

          // Assert reset button is visible.
          var resetButton = item.querySelector('#resetSite');
          assertTrue(!!resetButton);
          assertFalse(resetButton.hidden);

          MockInteractions.tap(resetButton);
          return browserProxy.whenCalled('resetCategoryPermissionForOrigin');
        })
        .then(function(args) {
          assertEquals('https://foo-allow.com:443', args[0]);
          assertEquals('', args[1]);
          assertEquals(contentType, args[2]);
        });
  });

  test('edit action menu opens edit exception dialog', function() {
    setUpCategory(
        settings.ContentSettingsTypes.COOKIES,
        settings.PermissionValues.SESSION_ONLY, prefsSessionOnly);

    return browserProxy.whenCalled('getExceptionList').then(function() {
      Polymer.dom.flush();  // Populates action menu.

      openActionMenu(0);
      assertMenu(['Allow', 'Block', 'Edit', 'Remove'], testElement);
      var menu = testElement.$$('dialog[is=cr-action-menu]');
      assertTrue(menu.open);
      var edit = testElement.$.edit;
      assertTrue(!!edit);
      MockInteractions.tap(edit);
      Polymer.dom.flush();
      assertFalse(menu.open);

      assertTrue(!!testElement.$$('settings-edit-exception-dialog'));
    });
  });

  test('list items shown and clickable when data is present', function() {
    var contentType = settings.ContentSettingsTypes.GEOLOCATION;
    setUpCategory(contentType, settings.PermissionValues.ALLOW, prefs);
    return browserProxy.whenCalled('getExceptionList')
        .then(function(actualContentType) {
          assertEquals(contentType, actualContentType);

          // Required for firstItem to be found below.
          Polymer.dom.flush();

          // Validate that the sites gets populated from pre-canned prefs.
          assertEquals(2, testElement.sites.length);
          assertEquals(
              prefs.exceptions.geolocation[0].origin,
              testElement.sites[0].origin);
          assertEquals(
              prefs.exceptions.geolocation[1].origin,
              testElement.sites[1].origin);
          assertFalse(!!testElement.selectedOrigin);

          // Validate that the sites are shown in UI and can be selected.
          var firstItem = testElement.$.listContainer.children[0];
          var clickable = firstItem.querySelector('.middle');
          assertTrue(!!clickable);
          MockInteractions.tap(clickable);
          assertEquals(
              prefs.exceptions.geolocation[0].origin,
              settings.getQueryParameters().get('site'));
        });
  });

  test('Block list open when Allow list is empty', function() {
    // Prefs: One item in Block list, nothing in Allow list.
    var contentType = settings.ContentSettingsTypes.GEOLOCATION;
    setUpCategory(
        contentType, settings.PermissionValues.BLOCK, prefsOneDisabled);
    return browserProxy.whenCalled('getExceptionList')
        .then(function(actualContentType) {
          assertEquals(contentType, actualContentType);
          assertFalse(testElement.$.category.hidden);
        })
        .then(function() {
          assertNotEquals(0, testElement.$.listContainer.offsetHeight);
        });
  });

  test('Block list closed when Allow list is not empty', function() {
    // Prefs: Items in both Block and Allow list.
    var contentType = settings.ContentSettingsTypes.GEOLOCATION;
    setUpCategory(contentType, settings.PermissionValues.BLOCK, prefs);
    return browserProxy.whenCalled('getExceptionList')
        .then(function(actualContentType) {
          assertEquals(contentType, actualContentType);
          assertFalse(testElement.$.category.hidden);
          assertEquals(0, testElement.$.listContainer.offsetHeight);
        });
  });

  test('Allow list is always open (Block list empty)', function() {
    // Prefs: One item in Allow list, nothing in Block list.
    var contentType = settings.ContentSettingsTypes.GEOLOCATION;
    setUpCategory(
        contentType, settings.PermissionValues.ALLOW, prefsOneEnabled);
    return browserProxy.whenCalled('getExceptionList')
        .then(function(actualContentType) {
          assertEquals(contentType, actualContentType);
          assertFalse(testElement.$.category.hidden);
        })
        .then(function() {
          assertNotEquals(0, testElement.$.listContainer.offsetHeight);
        });
  });

  test('Allow list is always open (Block list non-empty)', function() {
    // Prefs: Items in both Block and Allow list.
    var contentType = settings.ContentSettingsTypes.GEOLOCATION;
    setUpCategory(contentType, settings.PermissionValues.ALLOW, prefs);
    return browserProxy.whenCalled('getExceptionList')
        .then(function(actualContentType) {
          assertEquals(contentType, actualContentType);
          assertFalse(testElement.$.category.hidden);
        })
        .then(function() {
          assertNotEquals(0, testElement.$.listContainer.offsetHeight);
        });
  });

  test('Block list not hidden when empty', function() {
    // Prefs: One item in Allow list, nothing in Block list.
    var contentType = settings.ContentSettingsTypes.GEOLOCATION;
    setUpCategory(
        contentType, settings.PermissionValues.BLOCK, prefsOneEnabled);
    return browserProxy.whenCalled('getExceptionList')
        .then(function(actualContentType) {
          assertEquals(contentType, actualContentType);
          assertFalse(testElement.$.category.hidden);
        });
  });

  test('Allow list not hidden when empty', function() {
    // Prefs: One item in Block list, nothing in Allow list.
    var contentType = settings.ContentSettingsTypes.GEOLOCATION;
    setUpCategory(
        contentType, settings.PermissionValues.ALLOW, prefsOneDisabled);
    return browserProxy.whenCalled('getExceptionList')
        .then(function(actualContentType) {
          assertEquals(contentType, actualContentType);
          assertFalse(testElement.$.category.hidden);
        });
  });

  test('Mixed embeddingOrigin', function() {
    setUpCategory(
        settings.ContentSettingsTypes.IMAGES, settings.PermissionValues.ALLOW,
        prefsMixedEmbeddingOrigin);
    return browserProxy.whenCalled('getExceptionList')
        .then(function(contentType) {
          // Required for firstItem to be found below.
          Polymer.dom.flush();
          // Validate that embeddingOrigin sites cannot be edited.
          var firstItem = testElement.$.listContainer.children[0];
          assertTrue(firstItem.querySelector('#actionMenuButton').hidden);
          assertFalse(firstItem.querySelector('#resetSite').hidden);
          // Validate that non-embeddingOrigin sites can be edited.
          var secondItem = testElement.$.listContainer.children[1];
          assertFalse(secondItem.querySelector('#actionMenuButton').hidden);
          assertTrue(secondItem.querySelector('#resetSite').hidden);
        });
  });

  test('Mixed schemes (present and absent)', function() {
    // Prefs: One item with scheme and one without.
    setUpCategory(
        settings.ContentSettingsTypes.GEOLOCATION,
        settings.PermissionValues.ALLOW, prefsMixedSchemes);
    return browserProxy.whenCalled('getExceptionList')
        .then(function(contentType) {
          // No further checks needed. If this fails, it will hang the test.
        });
  });

  test('Select menu item', function() {
    // Test for error: "Cannot read property 'origin' of undefined".
    setUpCategory(
        settings.ContentSettingsTypes.GEOLOCATION,
        settings.PermissionValues.ALLOW, prefs);
    return browserProxy.whenCalled('getExceptionList')
        .then(function(contentType) {
          Polymer.dom.flush();
          openActionMenu(0);
          var allow = testElement.$.allow;
          assertTrue(!!allow);
          MockInteractions.tap(allow);
          return browserProxy.whenCalled('setCategoryPermissionForOrigin');
        });
  });

  test('Chrome Extension scheme', function() {
    setUpCategory(
        settings.ContentSettingsTypes.JAVASCRIPT,
        settings.PermissionValues.BLOCK, prefsChromeExtension);
    return browserProxy.whenCalled('getExceptionList')
        .then(function(contentType) {
          Polymer.dom.flush();
          openActionMenu(0);
          assertMenu(['Allow', 'Edit', 'Remove'], testElement);

          var allow = testElement.$.allow;
          assertTrue(!!allow);
          MockInteractions.tap(allow);
          return browserProxy.whenCalled('setCategoryPermissionForOrigin');
        })
        .then(function(args) {
          assertEquals(
              'chrome-extension://cfhgfbfpcbnnbibfphagcjmgjfjmojfa/', args[0]);
          assertEquals('', args[1]);
          assertEquals(settings.ContentSettingsTypes.JAVASCRIPT, args[2]);
          assertEquals('allow', args[3]);
        });
  });
});

suite('EditExceptionDialog', function() {
  /** @type {SettingsEditExceptionDialogElement} */ var dialog;

  /**
   * The dialog tests don't call |getExceptionList| so the exception needs to
   * be processes as a |SiteSettingsPref|.
   * @type {SiteSettingsPref}
   */
  var cookieException = {
    category: 'cookies',
    embeddingOrigin: 'http://foo.com',
    incognito: false,
    origin: 'http://foo.com',
    setting: 'block',
    enforcement: '',
    controlledBy: 'USER_POLICY',
  };

  setup(function() {
    browserProxy = new TestSiteSettingsPrefsBrowserProxy();
    settings.SiteSettingsPrefsBrowserProxyImpl.instance_ = browserProxy;
    PolymerTest.clearBody();
    dialog = document.createElement('settings-edit-exception-dialog');
    dialog.model = cookieException;
    document.body.appendChild(dialog);
  });

  teardown(function() {
    dialog.remove();
  });

  test('invalid input', function() {
    var input = dialog.$$('paper-input');
    assertTrue(!!input);
    assertFalse(input.invalid);

    var actionButton = dialog.$.actionButton;
    assertTrue(!!actionButton);
    assertFalse(actionButton.disabled);

    // Simulate user input of whitespace only text.
    input.value = '  ';
    input.fire('input');
    Polymer.dom.flush();
    assertTrue(actionButton.disabled);
    assertTrue(input.invalid);

    // Simulate user input of invalid text.
    browserProxy.setIsPatternValid(false);
    var expectedPattern = 'foobarbaz';
    input.value = expectedPattern;
    input.fire('input');

    return browserProxy.whenCalled('isPatternValid').then(function(pattern) {
      assertEquals(expectedPattern, pattern);
      assertTrue(actionButton.disabled);
      assertTrue(input.invalid);
    });
  });

  test('action button calls proxy', function() {
    var input = dialog.$$('paper-input');
    assertTrue(!!input);
    // Simulate user edit.
    var newValue = input.value + ':1234';
    input.value = newValue;

    var actionButton = dialog.$.actionButton;
    assertTrue(!!actionButton);
    assertFalse(actionButton.disabled);

    MockInteractions.tap(actionButton);
    return browserProxy.whenCalled('resetCategoryPermissionForOrigin')
        .then(function(args) {
          assertEquals(cookieException.origin, args[0]);
          assertEquals(cookieException.embeddingOrigin, args[1]);
          assertEquals(settings.ContentSettingsTypes.COOKIES, args[2]);
          assertEquals(cookieException.incognito, args[3]);

          return browserProxy.whenCalled('setCategoryPermissionForOrigin');
        })
        .then(function(args) {
          assertEquals(newValue, args[0]);
          assertEquals(newValue, args[1]);
          assertEquals(settings.ContentSettingsTypes.COOKIES, args[2]);
          assertEquals(cookieException.setting, args[3]);
          assertEquals(cookieException.incognito, args[4]);

          assertFalse(dialog.$.dialog.open);
        });
  });
});

suite('AddExceptionDialog', function() {
  /** @type {AddSiteDialogElement} */ var dialog;

  setup(function() {
    browserProxy = new TestSiteSettingsPrefsBrowserProxy();
    settings.SiteSettingsPrefsBrowserProxyImpl.instance_ = browserProxy;
    PolymerTest.clearBody();
    dialog = document.createElement('add-site-dialog');
    dialog.category = settings.ContentSettingsTypes.GEOLOCATION;
    dialog.contentSetting = settings.PermissionValues.ALLOW;
    document.body.appendChild(dialog);
    dialog.open();
  });

  teardown(function() {
    dialog.remove();
  });

  test('incognito', function() {
    cr.webUIListenerCallback(
        'onIncognitoStatusChanged',
        /*hasIncognito=*/true);
    assertFalse(dialog.$.incognito.checked);
    dialog.$.incognito.checked = true;
    // Changing the incognito status will reset the checkbox.
    cr.webUIListenerCallback(
        'onIncognitoStatusChanged',
        /*hasIncognito=*/false);
    assertFalse(dialog.$.incognito.checked);
  });

  test('invalid input', function() {
    // Initially the action button should be disabled, but the error warning
    // should not be shown for an empty input.
    var input = dialog.$$('paper-input');
    assertTrue(!!input);
    assertFalse(input.invalid);

    var actionButton = dialog.$.add;
    assertTrue(!!actionButton);
    assertTrue(actionButton.disabled);

    // Simulate user input of invalid text.
    browserProxy.setIsPatternValid(false);
    var expectedPattern = 'foobarbaz';
    input.value = expectedPattern;
    input.fire('input');

    return browserProxy.whenCalled('isPatternValid').then(function(pattern) {
      assertEquals(expectedPattern, pattern);
      assertTrue(actionButton.disabled);
      assertTrue(input.invalid);
    });
  });
});
