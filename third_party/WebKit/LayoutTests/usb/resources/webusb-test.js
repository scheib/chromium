'use strict';

// This polyfill library implements the WebUSB Test API as specified here:
// https://wicg.github.io/webusb/test/

(() => {

// The global mojo object contains the Mojo JS binding modules loaded during
// initialization.
let mojo = null;

// These variables are logically members of the USBTest class but are defined
// here to hide them from being visible as fields of navigator.usb.test.
let g_initializePromise = null;
let g_chooserService = null;
let g_deviceManager = null;

function fakeDeviceInitToDeviceInfo(guid, init) {
  let deviceInfo = {
    guid: guid + "",
    usb_version_major: init.usbVersionMajor,
    usb_version_minor: init.usbVersionMinor,
    usb_version_subminor: init.usbVersionSubminor,
    class_code: init.deviceClass,
    subclass_code: init.deviceSubclass,
    protocol_code: init.deviceProtocol,
    vendor_id: init.vendorId,
    product_id: init.productId,
    device_version_major: init.deviceVersionMajor,
    device_version_minor: init.deviceVersionMinor,
    device_version_subminor: init.deviceVersionSubminor,
    manufacturer_name: init.manufacturerName,
    product_name: init.productName,
    serial_number: init.serialNumber,
    active_configuration: init.activeConfigurationValue,
    configurations: []
  };
  init.configurations.forEach(config => {
    var configInfo = {
      configuration_value: config.configurationValue,
      configuration_name: config.configurationName,
      interfaces: []
    };
    config.interfaces.forEach(iface => {
      var interfaceInfo = {
        interface_number: iface.interfaceNumber,
        alternates: []
      };
      iface.alternates.forEach(alternate => {
        var alternateInfo = {
          alternate_setting: alternate.alternateSetting,
          class_code: alternate.interfaceClass,
          subclass_code: alternate.interfaceSubclass,
          protocol_code: alternate.interfaceProtocol,
          interface_name: alternate.interfaceName,
          endpoints: []
        };
        alternate.endpoints.forEach(endpoint => {
          var endpointInfo = {
            endpoint_number: endpoint.endpointNumber,
            packet_size: endpoint.packetSize,
          };
          switch (endpoint.direction) {
          case "in":
            endpointInfo.direction = mojo.device.UsbTransferDirection.INBOUND;
            break;
          case "out":
            endpointInfo.direction = mojo.device.UsbTransferDirection.OUTBOUND;
            break;
          }
          switch (endpoint.type) {
          case "bulk":
            endpointInfo.type = mojo.device.UsbTransferType.BULK;
            break;
          case "interrupt":
            endpointInfo.type = mojo.device.UsbTransferType.INTERRUPT;
            break;
          case "isochronous":
            endpointInfo.type = mojo.device.UsbTransferType.ISOCHRONOUS;
            break;
          }
          alternateInfo.endpoints.push(endpointInfo);
        });
        interfaceInfo.alternates.push(alternateInfo);
      });
      configInfo.interfaces.push(interfaceInfo);
    });
    deviceInfo.configurations.push(configInfo);
  });
  return deviceInfo;
}

function convertMojoDeviceFilters(input) {
  let output = [];
  input.forEach(filter => {
    output.push(convertMojoDeviceFilter(filter));
  });
  return output;
}

function convertMojoDeviceFilter(input) {
  let output = {};
  if (input.has_vendor_id)
    output.vendorId = input.vendor_id;
  if (input.has_product_id)
    output.productId = input.product_id;
  if (input.has_class_code)
    output.classCode = input.class_code;
  if (input.has_subclass_code)
    output.subclassCode = input.subclass_code;
  if (input.has_protocol_code)
    output.protocolCode = input.protocol_code;
  if (input.serial_number)
    output.serialNumber = input.serial_number;
  return output;
}

class FakeDevice {
  constructor(deviceInit) {
    this.info_ = deviceInit;
    this.opened_ = false;
    this.currentConfiguration_ = null;
    this.claimedInterfaces_ = new Map();
  }

  getConfiguration() {
    if (this.currentConfiguration_) {
      return Promise.resolve({
          value: this.currentConfiguration_.configuration_value });
    } else {
      return Promise.resolve({ value: 0 });
    }
  }

  open() {
    assert_false(this.opened_);
    this.opened_ = true;
    return Promise.resolve({ error: mojo.device.UsbOpenDeviceError.OK });
  }

  close() {
    assert_true(this.opened_);
    this.opened_ = false;
    return Promise.resolve();
  }

  setConfiguration(value) {
    assert_true(this.opened_);

    let selected_configuration = this.info_.configurations.find(
        configuration => configuration.configurationValue == value);
    // Blink should never request an invalid configuration.
    assert_false(selected_configuration == undefined);
    this.currentConfiguration_ = selected_configuration;
    return Promise.resolve({ success: true });
  }

  claimInterface(interfaceNumber) {
    assert_true(this.opened_);
    assert_false(this.currentConfiguration_ == null, 'device configured');
    assert_false(this.claimedInterfaces_.has(interfaceNumber),
                 'interface already claimed');

    // Blink should never request an invalid interface.
    assert_true(this.currentConfiguration_.interfaces.some(
            iface => iface.interfaceNumber == interfaceNumber));
    this.claimedInterfaces_.set(interfaceNumber, 0);
    return Promise.resolve({ success: true });
  }

  releaseInterface(interfaceNumber) {
    assert_true(this.opened_);
    assert_false(this.currentConfiguration_ == null, 'device configured');
    assert_true(this.claimedInterfaces_.has(interfaceNumber));
    this.claimedInterfaces_.delete(interfaceNumber);
    return Promise.resolve({ success: true });
  }

  setInterfaceAlternateSetting(interfaceNumber, alternateSetting) {
    assert_true(this.opened_);
    assert_false(this.currentConfiguration_ == null, 'device configured');
    assert_true(this.claimedInterfaces_.has(interfaceNumber));

    let iface = this.currentConfiguration_.interfaces.find(
        iface => iface.interfaceNumber == interfaceNumber);
    // Blink should never request an invalid interface or alternate.
    assert_false(iface == undefined);
    assert_true(iface.alternates.some(
        x => x.alternateSetting == alternateSetting));
    this.claimedInterfaces_.set(interfaceNumber, alternateSetting);
    return Promise.resolve({ success: true });
  }

  reset() {
    assert_true(this.opened_);
    return Promise.resolve({ success: true });
  }

  clearHalt(endpoint) {
    assert_true(this.opened_);
    assert_false(this.currentConfiguration_ == null, 'device configured');
    // TODO(reillyg): Assert that endpoint is valid.
    return Promise.resolve({ success: true });
  }

  controlTransferIn(params, length, timeout) {
    assert_true(this.opened_);
    assert_false(this.currentConfiguration_ == null, 'device configured');
    return Promise.resolve({
      status: mojo.device.UsbTransferStatus.OK,
      data: [length >> 8, length & 0xff, params.request, params.value >> 8,
             params.value & 0xff, params.index >> 8, params.index & 0xff]
    });
  }

  controlTransferOut(params, data, timeout) {
    assert_true(this.opened_);
    assert_false(this.currentConfiguration_ == null, 'device configured');
    return Promise.resolve({
      status: mojo.device.UsbTransferStatus.OK,
      bytesWritten: data.byteLength
    });
  }

  genericTransferIn(endpointNumber, length, timeout) {
    assert_true(this.opened_);
    assert_false(this.currentConfiguration_ == null, 'device configured');
    // TODO(reillyg): Assert that endpoint is valid.
    let data = new Array(length);
    for (let i = 0; i < length; ++i)
      data[i] = i & 0xff;
    return Promise.resolve({
      status: mojo.device.UsbTransferStatus.OK,
      data: data
    });
  }

  genericTransferOut(endpointNumber, data, timeout) {
    assert_true(this.opened_);
    assert_false(this.currentConfiguration_ == null, 'device configured');
    // TODO(reillyg): Assert that endpoint is valid.
    return Promise.resolve({
      status: mojo.device.UsbTransferStatus.OK,
      bytesWritten: data.byteLength
    });
  }

  isochronousTransferIn(endpointNumber, packetLengths, timeout) {
    assert_true(this.opened_);
    assert_false(this.currentConfiguration_ == null, 'device configured');
    // TODO(reillyg): Assert that endpoint is valid.
    let data = new Array(packetLengths.reduce((a, b) => a + b, 0));
    let dataOffset = 0;
    let packets = new Array(packetLengths.length);
    for (let i = 0; i < packetLengths.length; ++i) {
      for (let j = 0; j < packetLengths[i]; ++j)
        data[dataOffset++] = j & 0xff;
      packets[i] = {
        length: packetLengths[i],
        transferred_length: packetLengths[i],
        status: mojo.device.UsbTransferStatus.OK
      };
    }
    return Promise.resolve({ data: data, packets: packets });
  }

  isochronousTransferOut(endpointNumber, data, packetLengths, timeout) {
    assert_true(this.opened_);
    assert_false(this.currentConfiguration_ == null, 'device configured');
    // TODO(reillyg): Assert that endpoint is valid.
    let packets = new Array(packetLengths.length);
    for (let i = 0; i < packetLengths.length; ++i) {
      packets[i] = {
        length: packetLengths[i],
        transferred_length: packetLengths[i],
        status: mojo.device.UsbTransferStatus.OK
      };
    }
    return Promise.resolve({ packets: packets });
  }
}

class FakeDeviceManager {
  constructor() {
    this.bindingSet_ =
        new mojo.bindings.BindingSet(mojo.deviceManager.UsbDeviceManager);
    this.devices_ = new Map();
    this.devicesByGuid_ = new Map();
    this.client_ = null;
    this.nextGuid_ = 0;
  }

  addBinding(handle) {
    this.bindingSet_.addBinding(this, handle);
  }

  addDevice(fakeDevice, info) {
    let device = {
      fakeDevice: fakeDevice,
      guid: (this.nextGuid_++).toString(),
      info: info,
      bindingArray: []
    };
    this.devices_.set(fakeDevice, device);
    this.devicesByGuid_.set(device.guid, device);
    if (this.client_)
      this.client_.onDeviceAdded(fakeDeviceInitToDeviceInfo(device.guid, info));
  }

  removeDevice(fakeDevice) {
    let device = this.devices_.get(fakeDevice);
    if (!device)
      throw new Error('Cannot remove unknown device.');

    for (var binding of device.bindingArray)
      binding.close();
    this.devices_.delete(device.fakeDevice);
    this.devicesByGuid_.delete(device.guid);
    if (this.client_) {
      this.client_.onDeviceRemoved(
          fakeDeviceInitToDeviceInfo(device.guid, device.info));
    }
  }

  removeAllDevices() {
    this.devices_.forEach(device => {
      for (var binding of device.bindingArray)
        binding.close();
      this.client_.onDeviceRemoved(
          fakeDeviceInitToDeviceInfo(device.guid, device.info));
    });
    this.devices_.clear();
    this.devicesByGuid_.clear();
  }

  getDevices(options) {
    let devices = [];
    this.devices_.forEach(device => {
      devices.push(fakeDeviceInitToDeviceInfo(device.guid, device.info));
    });
    return Promise.resolve({ results: devices });
  }

  getDevice(guid, request) {
    let device = this.devicesByGuid_.get(guid);
    if (device) {
      let binding = new mojo.bindings.Binding(
          mojo.device.UsbDevice, new FakeDevice(device.info), request);
      binding.setConnectionErrorHandler(() => {
        if (device.fakeDevice.onclose)
          device.fakeDevice.onclose();
      });
      device.bindingArray.push(binding);
    } else {
      request.close();
    }
  }

  setClient(client) {
    this.client_ = client;
  }
}

class FakeChooserService {
  constructor() {
    this.bindingSet_ = new mojo.bindings.BindingSet(
        mojo.chooserService.UsbChooserService);
    this.chosenDevice_ = null;
    this.lastFilters_ = null;
  }

  addBinding(handle) {
    this.bindingSet_.addBinding(this, handle);
  }

  setChosenDevice(fakeDevice) {
    this.chosenDevice_ = fakeDevice;
  }

  getPermission(deviceFilters) {
    this.lastFilters_ = convertMojoDeviceFilters(deviceFilters);
    let device = g_deviceManager.devices_.get(this.chosenDevice_);
    if (device) {
      return Promise.resolve({
        result: fakeDeviceInitToDeviceInfo(device.guid, device.info)
      });
    } else {
      return Promise.resolve({ result: null });
    }
  }
}

// Unlike FakeDevice this class is exported to callers of USBTest.addFakeDevice.
class FakeUSBDevice {
  constructor() {
    this.onclose = null;
  }

  disconnect() {
    setTimeout(() => g_deviceManager.removeDevice(this), 0);
  }
}

class USBTest {
  constructor() {}

  initialize() {
    if (!g_initializePromise) {
      g_initializePromise = new Promise(resolve => {
        window.define = gin.define;  // Mojo modules expect this.

        gin.define('WebUSB Test Mocks', [
            'content/public/renderer/frame_interfaces',
            'device/usb/public/interfaces/chooser_service.mojom',
            'device/usb/public/interfaces/device_manager.mojom',
            'device/usb/public/interfaces/device.mojom',
            'mojo/public/js/bindings',
            'mojo/public/js/core',
            'mojo/public/js/router',
            'mojo/public/js/support',
        ], (frameInterfaces, chooserService, deviceManager, device,
            bindings, core, router, support) => {
          delete window.define;  // Clean up.

          mojo = {
            frameInterfaces: frameInterfaces,
            chooserService: chooserService,
            deviceManager: deviceManager,
            device: device,
            bindings: bindings,
            core: core,
            router: router,
            support: support
          };

          g_deviceManager = new FakeDeviceManager();
          mojo.frameInterfaces.addInterfaceOverrideForTesting(
              mojo.deviceManager.UsbDeviceManager.name,
              handle => g_deviceManager.addBinding(handle));

          g_chooserService = new FakeChooserService();
          mojo.frameInterfaces.addInterfaceOverrideForTesting(
              mojo.chooserService.UsbChooserService.name,
              handle => g_chooserService.addBinding(handle));

          addEventListener('unload', () => {
            mojo.frameInterfaces.clearInterfaceOverridesForTesting();
          });

          resolve();
        });
      });
    }

    return g_initializePromise;
  }

  attachToWindow(otherWindow) {
    if (!g_deviceManager || !g_chooserService)
      throw new Error('Call initialize() before attachToWindow().');

    return new Promise(resolve => {
      otherWindow.gin.define(
      'WebUSB Test Frame Attach', [
        'content/public/renderer/frame_interfaces'
      ], frameInterfaces => {
        frameInterfaces.addInterfaceOverrideForTesting(
            mojo.deviceManager.UsbDeviceManager.name,
            handle => g_deviceManager.addBinding(handle));
        frameInterfaces.addInterfaceOverrideForTesting(
            mojo.chooserService.UsbChooserService.name,
            handle => g_chooserService.addBinding(handle));
        resolve();
      });
    });
  }

  addFakeDevice(deviceInit) {
    if (!g_deviceManager)
      throw new Error('Call initialize() before addFakeDevice().');

    // |addDevice| and |removeDevice| are called in a setTimeout callback so
    // that tests do not rely on the device being immediately available which
    // may not be true for all implementations of this test API.
    let fakeDevice = new FakeUSBDevice();
    setTimeout(() => g_deviceManager.addDevice(fakeDevice, deviceInit), 0);
    return fakeDevice;
  }

  set chosenDevice(fakeDevice) {
    if (!g_chooserService)
      throw new Error('Call initialize() before setting chosenDevice.');

    g_chooserService.setChosenDevice(fakeDevice);
  }

  get lastFilters() {
    if (!g_chooserService)
      throw new Error('Call initialize() before getting lastFilters.');

    return g_chooserService.lastFilters_;
  }

  reset() {
    if (!g_deviceManager || !g_chooserService)
      throw new Error('Call initialize() before reset().');

    // Reset the mocks in a setTimeout callback so that tests do not rely on
    // the fact that this polyfill can do this synchronously.
    return new Promise(resolve => {
      setTimeout(() => {
        g_deviceManager.removeAllDevices();
        g_chooserService.setChosenDevice(null);
        resolve();
      }, 0);
    });
  }
}

navigator.usb.test = new USBTest();

})();
