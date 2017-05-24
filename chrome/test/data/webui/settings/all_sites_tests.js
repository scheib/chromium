// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * An example pref with exceptions with origins and patterns from
 * different providers.
 * @type {SiteSettingsPref}
 */
var prefsMixedProvider = {
  exceptions: {
    geolocation: [
      {
        embeddingOrigin: '',
        origin: 'https://[*.]foo.com',
        setting: 'block',
        source: 'policy',
      },
      {
        embeddingOrigin: '',
        origin: 'https://bar.foo.com',
        setting: 'block',
        source: 'preference',
      },
      {
        embeddingOrigin: '',
        origin: 'https://[*.]foo.com',
        setting: 'block',
        source: 'preference',
      },
    ],
    images: [],
  }
};

/**
 * An example pref with mixed origin and pattern.
 * @type {SiteSettingsPref}
 */
var prefsMixedOriginAndPattern = {
  exceptions: {
    auto_downloads: [],
    background_sync: [],
    camera: [],
    cookies: [],
    geolocation: [
      {
        embeddingOrigin: '',
        origin: 'https://foo.com',
        setting: 'allow',
        source: 'preference',
      },
    ],
    images: [],
    javascript: [
      {
        embeddingOrigin: '',
        origin: 'https://[*.]foo.com',
        setting: 'allow',
        source: 'preference',
      },
    ],
    mic: [],
    notifications: [],
    plugins: [],
    midiDevices: [],
    protectedContent: [],
    popups: [],
    subresource_filter: [],
    unsandboxed_plugins: [],
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

suite('AllSites', function() {
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
    browserProxy.setPrefs(prefsMixedOriginAndPattern);
    testElement = document.createElement('all-sites');
    assertTrue(!!testElement);
    document.body.appendChild(testElement);
  });

  teardown(function() {
    // The code being tested changes the Route. Reset so that state is not
    // leaked across tests.
    settings.resetRouteForTesting();
  });

  /**
   * Configures the test element.
   * @param {Array<dictionary>} prefs The prefs to use.
   */
  function setUpCategory(prefs) {
    browserProxy.setPrefs(prefs);
    // Some route is needed, but the actual route doesn't matter.
    testElement.currentRoute = {
      page: 'dummy',
      section: 'privacy',
      subpage: ['site-settings', 'site-settings-category-location'],
    };
  }

  test('All sites category no action menu', function() {
    setUpCategory(prefsVarious);
    return browserProxy.whenCalled('getExceptionList')
        .then(function(contentType) {
          // Use resolver to ensure that the list container is populated.
          var resolver = new PromiseResolver();
          testElement.async(resolver.resolve);
          return resolver.promise.then(function() {
            var item = testElement.$.listContainer.children[0];
            var name = item.querySelector('#displayName');
            assertTrue(!!name);
          });
        });
  });

  test('All sites category', function() {
    // Prefs: Multiple and overlapping sites.
    setUpCategory(prefsVarious);

    return browserProxy.whenCalled('getExceptionList')
        .then(function(contentType) {
          // Use resolver to ensure asserts bubble up to the framework with
          // meaningful errors.
          var resolver = new PromiseResolver();
          testElement.async(resolver.resolve);
          return resolver.promise.then(function() {
            // All Sites calls getExceptionList for all categories, starting
            // with Cookies.
            assertEquals(settings.ContentSettingsTypes.COOKIES, contentType);

            // Required for firstItem to be found below.
            Polymer.dom.flush();

            // Validate that the sites gets populated from pre-canned prefs.
            assertEquals(
                3, testElement.sites.length,
                'If this fails with 5 instead of the expected 3, then ' +
                    'the de-duping of sites is not working for site_list');
            assertEquals(
                prefsVarious.exceptions.geolocation[1].origin,
                testElement.sites[0].origin);
            assertEquals(
                prefsVarious.exceptions.geolocation[0].origin,
                testElement.sites[1].origin);
            assertEquals(
                prefsVarious.exceptions.notifications[0].origin,
                testElement.sites[2].origin);
            assertEquals(undefined, testElement.selectedOrigin);

            // Validate that the sites are shown in UI and can be selected.
            var firstItem = testElement.$.listContainer.children[1];
            var clickable = firstItem.querySelector('.middle');
            assertNotEquals(undefined, clickable);
            MockInteractions.tap(clickable);
            assertEquals(
                prefsVarious.exceptions.geolocation[0].origin,
                settings.getQueryParameters().get('site'));
          });
        });
  });

  test('All sites mixed pattern and origin', function() {
    // Prefs: One site, represented as origin and pattern.
    setUpCategory(prefsMixedOriginAndPattern);

    return browserProxy.whenCalled('getExceptionList')
        .then(function(contentType) {
          // Use resolver to ensure asserts bubble up to the framework with
          // meaningful errors.
          var resolver = new PromiseResolver();
          testElement.async(resolver.resolve);
          return resolver.promise.then(function() {
            // All Sites calls getExceptionList for all categories, starting
            // with Cookies.
            assertEquals(settings.ContentSettingsTypes.COOKIES, contentType);

            // Required for firstItem to be found below.
            Polymer.dom.flush();

            // Validate that the sites gets populated from pre-canned prefs.
            // TODO(dschuyler): de-duping of sites is under discussion, so
            // it is currently disabled. It should be enabled again or this
            // code should be removed.
            assertEquals(
                2, testElement.sites.length,
                'If this fails with 1 instead of the expected 2, then ' +
                    'the de-duping of sites has been enabled for site_list.');
            if (testElement.sites.length == 1) {
              assertEquals(
                  prefsMixedOriginAndPattern.exceptions.geolocation[0].origin,
                  testElement.sites[0].displayName);
            }

            assertEquals(undefined, testElement.selectedOrigin);
            // Validate that the sites are shown in UI and can be selected.
            var firstItem = testElement.$.listContainer.children[0];
            var clickable = firstItem.querySelector('.middle');
            assertNotEquals(undefined, clickable);
            MockInteractions.tap(clickable);
            if (testElement.sites.length == 1) {
              assertEquals(
                  prefsMixedOriginAndPattern.exceptions.geolocation[0].origin,
                  testElement.sites[0].displayName);
            }
          });
        });
  });
});
