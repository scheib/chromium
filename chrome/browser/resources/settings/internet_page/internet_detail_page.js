// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview
 * 'settings-internet-detail' is the settings subpage containing details
 * for a network.
 */
(function() {
'use strict';

/** @const */ var CARRIER_VERIZON = 'Verizon Wireless';

Polymer({
  is: 'settings-internet-detail-page',

  behaviors:
      [CrPolicyNetworkBehavior, settings.RouteObserverBehavior, I18nBehavior],

  properties: {
    /** The network GUID to display details for. */
    guid: String,

    /**
     * The current properties for the network matching |guid|.
     * @type {!CrOnc.NetworkProperties|undefined}
     */
    networkProperties: {
      type: Object,
      observer: 'networkPropertiesChanged_',
    },

    /** Preferences state. */
    prefs: {
      type: Object,
      notify: true,
    },

    /**
     * Whether the user is a secondary user.
     * @private
     */
    isSecondaryUser_: {
      type: Boolean,
      value: function() {
        return loadTimeData.getBoolean('isSecondaryUser');
      },
      readOnly: true,
    },

    /**
     * Email address for the primary user.
     * @private
     */
    primaryUserEmail_: {
      type: String,
      value: function() {
        return loadTimeData.getBoolean('isSecondaryUser') ?
            loadTimeData.getString('primaryUserEmail') :
            '';
      },
      readOnly: true,
    },

    /**
     * Highest priority connected network or null.
     * @type {?CrOnc.NetworkStateProperties}
     */
    defaultNetwork: {
      type: Object,
      value: null,
    },

    /** @type {!chrome.networkingPrivate.GlobalPolicy|undefined} */
    globalPolicy: Object,

    /**
     * Interface for networkingPrivate calls, passed from internet_page.
     * @type {NetworkingPrivate}
     */
    networkingPrivate: Object,

    /**
     * The network AutoConnect state.
     * @private
     */
    autoConnect_: {
      type: Boolean,
      value: false,
      observer: 'autoConnectChanged_',
    },

    /**
     * The network preferred state.
     * @private
     */
    preferNetwork_: {
      type: Boolean,
      value: false,
      observer: 'preferNetworkChanged_',
    },

    /**
     * The network IP Address.
     * @private
     */
    IPAddress_: {
      type: String,
      value: '',
    },

    /** @private */
    advancedExpanded_: Boolean,

    /** @private */
    networkExpanded_: Boolean,

    /** @private */
    proxyExpanded_: Boolean,
  },

  /**
   * Listener function for chrome.networkingPrivate.onNetworksChanged event.
   * @type {?function(!Array<string>)}
   * @private
   */
  networksChangedListener_: null,

  /** @private {boolean} */
  didSetFocus_: false,

  /**
   * Set to true to once the initial properties have been received. This
   * prevents setProperties from being called when setting default properties.
   * @private {boolean}
   */
  networkPropertiesReceived_: false,

  /**
   * Set in currentRouteChanged() if the showConfigure URL query
   * parameter is set to true. The dialog cannot be shown until the
   * network properties have been fetched in networkPropertiesChanged_().
   * @private {boolean}
   */
  shoudlShowConfigureWhenNetworkLoaded_: false,

  /**
   * Whether the previous route was also the network detail page.
   * @private {boolean}
   */
  wasPreviousRouteNetworkDetailPage_: false,

  /**
   * settings.RouteObserverBehavior
   * @param {!settings.Route} route
   * @param {!settings.Route} oldRoute
   * @protected
   */
  currentRouteChanged: function(route, oldRoute) {
    if (route != settings.Route.NETWORK_DETAIL) {
      if (this.networksChangedListener_) {
        this.networkingPrivate.onNetworksChanged.removeListener(
            this.networksChangedListener_);
        this.networksChangedListener_ = null;
      }
      return;
    }
    if (!this.networksChangedListener_) {
      this.networksChangedListener_ = this.onNetworksChangedEvent_.bind(this);
      this.networkingPrivate.onNetworksChanged.addListener(
          this.networksChangedListener_);
    }
    var queryParams = settings.getQueryParameters();
    this.guid = queryParams.get('guid') || '';
    if (!this.guid) {
      console.error('No guid specified for page:' + route);
      this.close_();
    }
    // Set basic networkProperties until they are loaded.
    this.networkPropertiesReceived_ = false;
    var type = /** @type {!chrome.networkingPrivate.NetworkType} */ (
                   queryParams.get('type')) ||
        CrOnc.Type.WI_FI;
    this.shoudlShowConfigureWhenNetworkLoaded_ =
        queryParams.get('showConfigure') == 'true';
    this.wasPreviousRouteNetworkDetailPage_ =
        oldRoute == settings.Route.NETWORK_DETAIL;
    var name = queryParams.get('name') || type;
    this.networkProperties = {
      GUID: this.guid,
      Type: type,
      ConnectionState: CrOnc.ConnectionState.NOT_CONNECTED,
      Name: {Active: name},
    };
    this.didSetFocus_ = false;
    this.getNetworkDetails_();
  },

  /** @private */
  close_: function() {
    // Delay navigating to allow other subpages to load first.
    requestAnimationFrame(function() {
      settings.navigateToPreviousRoute();
    });
  },

  /** @private */
  networkPropertiesChanged_: function() {
    if (!this.networkProperties)
      return;

    // Update autoConnect if it has changed. Default value is false.
    var autoConnect = CrOnc.getAutoConnect(this.networkProperties);
    if (autoConnect != this.autoConnect_)
      this.autoConnect_ = autoConnect;

    // Update preferNetwork if it has changed. Default value is false.
    var priority = /** @type {number} */ (
        CrOnc.getActiveValue(this.networkProperties.Priority) || 0);
    var preferNetwork = priority > 0;
    if (preferNetwork != this.preferNetwork_)
      this.preferNetwork_ = preferNetwork;

    // Set the IPAddress property to the IPV4 Address.
    var ipv4 =
        CrOnc.getIPConfigForType(this.networkProperties, CrOnc.IPType.IPV4);
    this.IPAddress_ = (ipv4 && ipv4.IPAddress) || '';

    // Update the detail page title.
    this.parentNode.pageTitle = CrOnc.getNetworkName(this.networkProperties);

    Polymer.dom.flush();

    if (this.didSetFocus_) {
      // Focus a button once the initial state is set.
      this.didSetFocus_ = true;
      var button = this.$$('#buttonDiv .primary-button:not([hidden])');
      if (!button)
        button = this.$$('#buttonDiv .secondary-button:not([hidden])');
      assert(button);  // At least one button will always be visible.
      button.focus();
    }

    if (this.shoudlShowConfigureWhenNetworkLoaded_
        && this.networkProperties.Tether) {
      this.showTetherDialog_();
    }
  },

  /** @private */
  autoConnectChanged_: function() {
    if (!this.networkProperties || !this.guid)
      return;
    var onc = this.getEmptyNetworkProperties_();
    CrOnc.setTypeProperty(onc, 'AutoConnect', this.autoConnect_);
    this.setNetworkProperties_(onc);
  },

  /** @private */
  preferNetworkChanged_: function() {
    if (!this.networkProperties || !this.guid)
      return;
    var onc = this.getEmptyNetworkProperties_();
    onc.Priority = this.preferNetwork_ ? 1 : 0;
    this.setNetworkProperties_(onc);
  },

  /**
   * networkingPrivate.onNetworksChanged event callback.
   * @param {!Array<string>} networkIds The list of changed network GUIDs.
   * @private
   */
  onNetworksChangedEvent_: function(networkIds) {
    if (networkIds.indexOf(this.guid) != -1)
      this.getNetworkDetails_();
  },

  /**
   * Calls networkingPrivate.getProperties for this.guid.
   * @private
   */
  getNetworkDetails_: function() {
    assert(!!this.guid);
    if (this.isSecondaryUser_) {
      this.networkingPrivate.getState(
          this.guid, this.getStateCallback_.bind(this));
    } else {
      this.networkingPrivate.getManagedProperties(
          this.guid, this.getPropertiesCallback_.bind(this));
    }
  },

  /**
   * networkingPrivate.getProperties callback.
   * @param {!CrOnc.NetworkProperties} properties The network properties.
   * @private
   */
  getPropertiesCallback_: function(properties) {
    if (chrome.runtime.lastError) {
      var message = chrome.runtime.lastError.message;
      if (message == 'Error.InvalidNetworkGuid') {
        console.error('Details page: GUID no longer exists: ' + this.guid);
      } else {
        console.error(
            'Unexpected networkingPrivate.getManagedProperties error: ' +
            message + ' For: ' + this.guid);
      }
      this.close_();
      return;
    }
    if (!properties) {
      console.error('No properties for: ' + this.guid);
      this.close_();
      return;
    }
    this.networkProperties = properties;
    this.networkPropertiesReceived_ = true;
  },

  /**
   * networkingPrivate.getState callback.
   * @param {CrOnc.NetworkStateProperties} state The network state properties.
   * @private
   */
  getStateCallback_: function(state) {
    if (!state) {
      // If |state| is null, the network is no longer visible, close this.
      console.error('Network no longer exists: ' + this.guid);
      this.networkProperties = undefined;
      this.close_();
    }
    this.networkProperties = {
      GUID: state.GUID,
      Type: state.Type,
      Connectable: state.Connectable,
      ConnectionState: state.ConnectionState,
    };
    this.networkPropertiesReceived_ = true;
  },

  /**
   * @param {!chrome.networkingPrivate.NetworkConfigProperties} onc The ONC
   *     network properties.
   * @private
   */
  setNetworkProperties_: function(onc) {
    if (!this.networkPropertiesReceived_)
      return;

    assert(!!this.guid);
    this.networkingPrivate.setProperties(this.guid, onc, function() {
      if (chrome.runtime.lastError) {
        // An error typically indicates invalid input; request the properties
        // to update any invalid fields.
        this.getNetworkDetails_();
      }
    }.bind(this));
  },

  /**
   * @return {!chrome.networkingPrivate.NetworkConfigProperties} An ONC
   *     dictionary with just the Type property set. Used for passing properties
   *     to setNetworkProperties_.
   * @private
   */
  getEmptyNetworkProperties_: function() {
    return {Type: this.networkProperties.Type};
  },

  /**
   * @param {!CrOnc.NetworkProperties} networkProperties
   * @return {string} The text to display for the network connection state.
   * @private
   */
  getStateText_: function(networkProperties) {
    return this.i18n('Onc' + networkProperties.ConnectionState);
  },

  /**
   * @param {!CrOnc.NetworkProperties} networkProperties
   * @return {boolean} True if the network is connected.
   * @private
   */
  isConnectedState_: function(networkProperties) {
    return networkProperties.ConnectionState == CrOnc.ConnectionState.CONNECTED;
  },

  /**
   * @param {!CrOnc.NetworkProperties} networkProperties
   * @return {boolean}
   * @private
   */
  isRemembered_: function(networkProperties) {
    var source = networkProperties.Source;
    return !!source && source != CrOnc.Source.NONE;
  },

  /**
   * @param {!CrOnc.NetworkProperties} networkProperties
   * @return {boolean}
   * @private
   */
  isRememberedOrConnected_: function(networkProperties) {
    return this.isRemembered_(networkProperties) ||
        this.isConnectedState_(networkProperties);
  },

  /**
   * @param {!CrOnc.NetworkProperties} networkProperties
   * @return {boolean}
   * @private
   */
  isCellular_: function(networkProperties) {
    return networkProperties.Type == CrOnc.Type.CELLULAR &&
        !!networkProperties.Cellular;
  },

  /**
   * @param {!CrOnc.NetworkProperties} networkProperties
   * @param {!chrome.networkingPrivate.GlobalPolicy} globalPolicy
   * @return {boolean}
   * @private
   */
  connectNotAllowed_: function(networkProperties, globalPolicy) {
    return networkProperties.Type == CrOnc.Type.WI_FI &&
        !!globalPolicy.AllowOnlyPolicyNetworksToConnect &&
        !this.isPolicySource(networkProperties.Source);
  },

  /**
   * @param {!CrOnc.NetworkProperties} networkProperties
   * @param {!chrome.networkingPrivate.GlobalPolicy} globalPolicy
   * @return {boolean}
   * @private
   */
  showConnect_: function(networkProperties, globalPolicy) {
    if (this.connectNotAllowed_(networkProperties, globalPolicy))
      return false;
    return networkProperties.Type != CrOnc.Type.ETHERNET &&
        networkProperties.ConnectionState ==
        CrOnc.ConnectionState.NOT_CONNECTED;
  },

  /**
   * @param {!CrOnc.NetworkProperties} networkProperties
   * @return {boolean}
   * @private
   */
  showDisconnect_: function(networkProperties) {
    return networkProperties.Type != CrOnc.Type.ETHERNET &&
        networkProperties.ConnectionState !=
        CrOnc.ConnectionState.NOT_CONNECTED;
  },

  /**
   * @param {!CrOnc.NetworkProperties} networkProperties
   * @return {boolean}
   * @private
   */
  showForget_: function(networkProperties) {
    var type = networkProperties.Type;
    if (type != CrOnc.Type.WI_FI && type != CrOnc.Type.VPN)
      return false;
    return this.isRemembered_(networkProperties);
  },

  /**
   * @param {!CrOnc.NetworkProperties} networkProperties
   * @return {boolean}
   * @private
   */
  showActivate_: function(networkProperties) {
    if (!this.isCellular_(networkProperties))
      return false;
    var activation = networkProperties.Cellular.ActivationState;
    return activation == CrOnc.ActivationState.NOT_ACTIVATED ||
        activation == CrOnc.ActivationState.PARTIALLY_ACTIVATED;
  },

  /**
   * @param {!CrOnc.NetworkProperties} networkProperties
   * @param {!chrome.networkingPrivate.GlobalPolicy} globalPolicy
   * @return {boolean}
   * @private
   */
  showConfigure_: function(networkProperties, globalPolicy) {
    if (this.connectNotAllowed_(networkProperties, globalPolicy))
      return false;
    var type = networkProperties.Type;
    if (type == CrOnc.Type.CELLULAR)
      return false;
    if ((type == CrOnc.Type.WI_FI || type == CrOnc.Type.WI_MAX) &&
        networkProperties.ConnectionState !=
            CrOnc.ConnectionState.NOT_CONNECTED) {
      return false;
    }
    return this.isRemembered_(networkProperties);
  },

  /**
   * @param {!CrOnc.NetworkProperties} networkProperties
   * @return {boolean}
   * @private
   */
  showViewAccount_: function(networkProperties) {
    // Show either the 'Activate' or the 'View Account' button (Cellular only).
    if (!this.isCellular_(networkProperties) ||
        this.showActivate_(networkProperties)) {
      return false;
    }

    // Only show if online payment URL is provided or the carrier is Verizon.
    var carrier = CrOnc.getActiveValue(networkProperties.Cellular.Carrier);
    if (carrier != CARRIER_VERIZON) {
      var paymentPortal = networkProperties.Cellular.PaymentPortal;
      if (!paymentPortal || !paymentPortal.Url)
        return false;
    }

    // Only show for connected networks or LTE networks with a valid MDN.
    if (!this.isConnectedState_(networkProperties)) {
      var technology = networkProperties.Cellular.NetworkTechnology;
      if (technology != CrOnc.NetworkTechnology.LTE &&
          technology != CrOnc.NetworkTechnology.LTE_ADVANCED) {
        return false;
      }
      if (!networkProperties.Cellular.MDN)
        return false;
    }

    return true;
  },

  /**
   * @param {!CrOnc.NetworkProperties} networkProperties
   * @param {?CrOnc.NetworkStateProperties} defaultNetwork
   * @param {!chrome.networkingPrivate.GlobalPolicy} globalPolicy
   * @return {boolean} Whether or not to enable the network connect button.
   * @private
   */
  enableConnect_: function(networkProperties, defaultNetwork, globalPolicy) {
    if (!this.showConnect_(networkProperties, globalPolicy))
      return false;
    if (networkProperties.Type == CrOnc.Type.CELLULAR &&
        CrOnc.isSimLocked(networkProperties)) {
      return false;
    }
    if (networkProperties.Type == CrOnc.Type.VPN && !defaultNetwork)
      return false;
    return true;
  },

  /**
   * @return {!TetherConnectionDialogElement}
   * @private
   */
  getTetherDialog_: function() {
    return /** @type {!TetherConnectionDialogElement} */ (this.$.tetherDialog);
  },

  /** @private */
  onConnectTap_: function() {
    this.fire('network-connect', {networkProperties: this.networkProperties});
  },

  /** @private */
  onTetherConnect_: function() {
    this.getTetherDialog_().close();
    this.fire('network-connect',
        {networkProperties: this.networkProperties,
         bypassConnectionDialog: true});
  },

  /** @private */
  onTetherDialogClose_: function() {
    // The tether dialog is opened by specifying "showConfigure=true"
    // in the query params. This may lead to the previous route also
    // being the detail page, in which case we should navigate back to
    // the previous route here so that when the user navigates back
    // they will navigate to the previous non-detail page.
    if (this.wasPreviousRouteNetworkDetailPage_)
      settings.navigateToPreviousRoute();
  },

  /** @private */
  onDisconnectTap_: function() {
    this.networkingPrivate.startDisconnect(this.guid);
  },

  /** @private */
  onForgetTap_: function() {
    this.networkingPrivate.forgetNetwork(this.guid);
    // A forgotten network no longer has a valid GUID, close the subpage.
    this.close_();
  },

  /** @private */
  onActivateTap_: function() {
    this.networkingPrivate.startActivate(this.guid);
  },

  /** @private */
  onConfigureTap_: function() {
    chrome.send('configureNetwork', [this.guid]);
  },

  /** @private */
  onViewAccountTap_: function() {
    // startActivate() will show the account page for activated networks.
    this.networkingPrivate.startActivate(this.guid);
  },

  /** @const {string} */
  CR_EXPAND_BUTTON_TAG: 'CR-EXPAND-BUTTON',

  /** @private */
  showTetherDialog_: function() {
    this.getTetherDialog_().open();
  },

  /**
   * @param {Event} event
   * @private
   */
  toggleAdvancedExpanded_: function(event) {
    if (event.target.tagName == this.CR_EXPAND_BUTTON_TAG)
      return;  // Already handled.
    this.advancedExpanded_ = !this.advancedExpanded_;
  },

  /**
   * @param {Event} event
   * @private
   */
  toggleNetworkExpanded_: function(event) {
    if (event.target.tagName == this.CR_EXPAND_BUTTON_TAG)
      return;  // Already handled.
    this.networkExpanded_ = !this.networkExpanded_;
  },

  /**
   * @param {Event} event
   * @private
   */
  toggleProxyExpanded_: function(event) {
    if (event.target.tagName == this.CR_EXPAND_BUTTON_TAG)
      return;  // Already handled.
    this.proxyExpanded_ = !this.proxyExpanded_;
  },

  /**
   * Event triggered for elements associated with network properties.
   * @param {!{detail: !{field: string, value: (string|!Object)}}} event
   * @private
   */
  onNetworkPropertyChange_: function(event) {
    if (!this.networkProperties)
      return;
    var field = event.detail.field;
    var value = event.detail.value;
    var onc = this.getEmptyNetworkProperties_();
    if (field == 'APN') {
      CrOnc.setTypeProperty(onc, 'APN', value);
    } else if (field == 'SIMLockStatus') {
      CrOnc.setTypeProperty(onc, 'SIMLockStatus', value);
    } else {
      console.error('Unexpected property change event: ' + field);
      return;
    }
    this.setNetworkProperties_(onc);
  },

  /**
   * Event triggered when the IP Config or NameServers element changes.
   * @param {!{detail: !{field: string,
   *                     value: (string|!CrOnc.IPConfigProperties|
   *                             !Array<string>)}}} event
   *     The network-ip-config or network-nameservers change event.
   * @private
   */
  onIPConfigChange_: function(event) {
    if (!this.networkProperties)
      return;
    var field = event.detail.field;
    var value = event.detail.value;
    // Get an empty ONC dictionary and set just the IP Config properties that
    // need to change.
    var onc = this.getEmptyNetworkProperties_();
    var ipConfigType =
        /** @type {chrome.networkingPrivate.IPConfigType|undefined} */ (
            CrOnc.getActiveValue(this.networkProperties.IPAddressConfigType));
    if (field == 'IPAddressConfigType') {
      var newIpConfigType =
          /** @type {chrome.networkingPrivate.IPConfigType} */ (value);
      if (newIpConfigType == ipConfigType)
        return;
      onc.IPAddressConfigType = newIpConfigType;
    } else if (field == 'NameServersConfigType') {
      var nsConfigType =
          /** @type {chrome.networkingPrivate.IPConfigType|undefined} */ (
              CrOnc.getActiveValue(
                  this.networkProperties.NameServersConfigType));
      var newNsConfigType =
          /** @type {chrome.networkingPrivate.IPConfigType} */ (value);
      if (newNsConfigType == nsConfigType)
        return;
      onc.NameServersConfigType = newNsConfigType;
    } else if (field == 'StaticIPConfig') {
      if (ipConfigType == CrOnc.IPConfigType.STATIC) {
        var staticIpConfig = this.networkProperties.StaticIPConfig;
        var ipConfigValue = /** @type {!Object} */ (value);
        if (staticIpConfig &&
            this.allPropertiesMatch_(staticIpConfig, ipConfigValue)) {
          return;
        }
      }
      onc.IPAddressConfigType = CrOnc.IPConfigType.STATIC;
      if (!onc.StaticIPConfig) {
        onc.StaticIPConfig =
            /** @type {!chrome.networkingPrivate.IPConfigProperties} */ ({});
      }
      for (var key in value)
        onc.StaticIPConfig[key] = value[key];
    } else if (field == 'NameServers') {
      // If a StaticIPConfig property is specified and its NameServers value
      // matches the new value, no need to set anything.
      var nameServers = /** @type {!Array<string>} */ (value);
      if (onc.NameServersConfigType == CrOnc.IPConfigType.STATIC &&
          onc.StaticIPConfig && onc.StaticIPConfig.NameServers == nameServers) {
        return;
      }
      onc.NameServersConfigType = CrOnc.IPConfigType.STATIC;
      if (!onc.StaticIPConfig) {
        onc.StaticIPConfig =
            /** @type {!chrome.networkingPrivate.IPConfigProperties} */ ({});
      }
      onc.StaticIPConfig.NameServers = nameServers;
    } else {
      console.error('Unexpected change field: ' + field);
      return;
    }
    // setValidStaticIPConfig will fill in any other properties from
    // networkProperties. This is necessary since we update IP Address and
    // NameServers independently.
    CrOnc.setValidStaticIPConfig(onc, this.networkProperties);
    this.setNetworkProperties_(onc);
  },

  /**
   * Event triggered when the Proxy configuration element changes.
   * @param {!{detail: {field: string, value: !CrOnc.ProxySettings}}} event
   *     The network-proxy change event.
   * @private
   */
  onProxyChange_: function(event) {
    if (!this.networkProperties)
      return;
    var field = event.detail.field;
    var value = event.detail.value;
    if (field != 'ProxySettings')
      return;
    var onc = this.getEmptyNetworkProperties_();
    CrOnc.setProperty(onc, 'ProxySettings', /** @type {!Object} */ (value));
    this.setNetworkProperties_(onc);
  },

  /**
   * @param {!CrOnc.NetworkProperties} networkProperties
   * @return {boolean} True if the shared message should be shown.
   * @private
   */
  showShared_: function(networkProperties) {
    return networkProperties.Source == 'Device' ||
        networkProperties.Source == 'DevicePolicy';
  },

  /**
   * @param {!CrOnc.NetworkProperties} networkProperties
   * @return {boolean} True if the AutoConnect checkbox should be shown.
   * @private
   */
  showAutoConnect_: function(networkProperties) {
    return networkProperties.Type != CrOnc.Type.ETHERNET &&
        this.isRemembered_(networkProperties);
  },

  /**
   * @param {!CrOnc.NetworkProperties} networkProperties
   * @param {!chrome.networkingPrivate.GlobalPolicy} globalPolicy
   * @return {boolean}
   * @private
   */
  enableAutoConnect_: function(networkProperties, globalPolicy) {
    if (networkProperties.Type == CrOnc.Type.WI_FI &&
        !!globalPolicy.AllowOnlyPolicyNetworksToAutoconnect &&
        !this.isPolicySource(networkProperties.Source)) {
      return false;
    }
    return !this.isNetworkPolicyEnforced(
        this.getManagedAutoConnect_(networkProperties));
  },

  /**
   * @param {!CrOnc.NetworkProperties} networkProperties
   * @return {!CrOnc.ManagedProperty|undefined} Managed AutoConnect property.
   * @private
   */
  getManagedAutoConnect_: function(networkProperties) {
    return CrOnc.getManagedAutoConnect(networkProperties);
  },

  /**
   * @param {!CrOnc.NetworkProperties} networkProperties
   * @return {boolean} True if the prefer network checkbox should be shown.
   * @private
   */
  showPreferNetwork_: function(networkProperties) {
    // TODO(stevenjb): Resolve whether or not we want to allow "preferred" for
    // networkProperties.Type == CrOnc.Type.ETHERNET.
    return this.isRemembered_(networkProperties);
  },

  /**
   * @param {!Array<string>} fields
   * @return {boolean}
   * @private
   */
  hasVisibleFields_: function(fields) {
    for (var i = 0; i < fields.length; ++i) {
      var value = this.get(fields[i], this.networkProperties);
      if (value !== undefined && value !== '')
        return true;
    }
    return false;
  },

  /**
   * @return {boolean}
   * @private
   */
  hasInfoFields_: function() {
    return this.hasVisibleFields_(this.getInfoFields_());
  },

  /**
   * @return {!Array<string>} The fields to display in the info section.
   * @private
   */
  getInfoFields_: function() {
    /** @type {!Array<string>} */ var fields = [];
    var type = this.networkProperties.Type;
    if (type == CrOnc.Type.CELLULAR && !!this.networkProperties.Cellular) {
      fields.push(
          'Cellular.ActivationState', 'Cellular.RoamingState',
          'RestrictedConnectivity', 'Cellular.ServingOperator.Name');
    } else if (type == CrOnc.Type.TETHER && !!this.networkProperties.Tether) {
      fields.push(
          'Tether.BatteryPercentage', 'Tether.SignalStrength',
          'Tether.Carrier');
    } else if (type == CrOnc.Type.VPN && !!this.networkProperties.VPN) {
      var vpnType = CrOnc.getActiveValue(this.networkProperties.VPN.Type);
      if (vpnType == 'ThirdPartyVPN') {
        fields.push('VPN.ThirdPartyVPN.ProviderName');
      } else {
        fields.push('VPN.Host', 'VPN.Type');
        if (vpnType == 'OpenVPN')
          fields.push('VPN.OpenVPN.Username');
        else if (vpnType == 'L2TP-IPsec')
          fields.push('VPN.L2TP.Username');
      }
    } else if (type == CrOnc.Type.WI_FI) {
      fields.push('RestrictedConnectivity');
    } else if (type == CrOnc.Type.WI_MAX) {
      fields.push('RestrictedConnectivity', 'WiMAX.EAP.Identity');
    }
    return fields;
  },

  /**
   * @return {!Array<string>} The fields to display in the Advanced section.
   * @private
   */
  getAdvancedFields_: function() {
    /** @type {!Array<string>} */ var fields = [];
    fields.push('MacAddress');
    var type = this.networkProperties.Type;
    if (type == CrOnc.Type.CELLULAR && !!this.networkProperties.Cellular) {
      fields.push(
          'Cellular.Carrier', 'Cellular.Family', 'Cellular.NetworkTechnology',
          'Cellular.ServingOperator.Code');
    } else if (type == CrOnc.Type.WI_FI) {
      fields.push(
          'WiFi.SSID', 'WiFi.BSSID', 'WiFi.SignalStrength', 'WiFi.Security',
          'WiFi.EAP.Outer', 'WiFi.EAP.Inner', 'WiFi.EAP.SubjectMatch',
          'WiFi.EAP.Identity', 'WiFi.EAP.AnonymousIdentity', 'WiFi.Frequency');
    } else if (type == CrOnc.Type.WI_MAX) {
      fields.push('WiFi.SignalStrength');
    }
    return fields;
  },

  /**
   * @return {!Array<string>} The fields to display in the device section.
   * @private
   */
  getDeviceFields_: function() {
    /** @type {!Array<string>} */ var fields = [];
    if (this.networkProperties.Type == CrOnc.Type.CELLULAR) {
      fields.push(
          'Cellular.HomeProvider.Name', 'Cellular.HomeProvider.Country',
          'Cellular.HomeProvider.Code', 'Cellular.Manufacturer',
          'Cellular.ModelID', 'Cellular.FirmwareRevision',
          'Cellular.HardwareRevision', 'Cellular.ESN', 'Cellular.ICCID',
          'Cellular.IMEI', 'Cellular.IMSI', 'Cellular.MDN', 'Cellular.MEID',
          'Cellular.MIN', 'Cellular.PRLVersion');
    }
    return fields;
  },

  /**
   * @param {!CrOnc.NetworkProperties} networkProperties
   * @return {boolean}
   * @private
   */
  showAdvanced_: function(networkProperties) {
    return this.hasAdvancedFields_() || this.hasDeviceFields_() ||
        this.isRememberedOrConnected_(networkProperties);
  },

  /**
   * @return {boolean}
   * @private
   */
  hasAdvancedFields_: function() {
    return this.hasVisibleFields_(this.getAdvancedFields_());
  },

  /**
   * @return {boolean}
   * @private
   */
  hasDeviceFields_: function() {
    return this.hasVisibleFields_(this.getDeviceFields_());
  },

  /**
   * @return {boolean}
   * @private
   */
  hasAdvancedOrDeviceFields_: function() {
    return this.hasAdvancedFields_() || this.hasDeviceFields_();
  },

  /**
   * @param {!CrOnc.NetworkProperties} networkProperties
   * @return {boolean}
   * @private
   */
  hasNetworkSection_: function(networkProperties) {
    if (networkProperties.Type == CrOnc.Type.VPN)
      return false;
    if (networkProperties.Type == CrOnc.Type.CELLULAR)
      return true;
    return this.isRememberedOrConnected_(networkProperties);
  },

  /**
   * @param {!CrOnc.NetworkProperties} networkProperties
   * @return {boolean}
   * @private
   */
  showCellularSim_: function(networkProperties) {
    if (networkProperties.Type != 'Cellular' ||
        !networkProperties.Cellular) {
      return false;
    }
    return networkProperties.Cellular.Family == 'GSM';
  },

  /**
   * @param {!Object} curValue
   * @param {!Object} newValue
   * @return {boolean} True if all properties set in |newValue| are equal to
   *     the corresponding properties in |curValue|. Note: Not all properties
   *     of |curValue| need to be specified in |newValue| for this to return
   *     true.
   * @private
   */
  allPropertiesMatch_: function(curValue, newValue) {
    for (var key in newValue) {
      if (newValue[key] != curValue[key])
        return false;
    }
    return true;
  }
});
})();
