'use strict';

function usb_test(func, name, properties) {
  promise_test(async () => {
    await navigator.usb.test.initialize()
    try {
      await func();
    } finally {
      await navigator.usb.test.reset();
    }
  }, name, properties);
}

// Returns a promise that is resolved when the next USBConnectionEvent of the
// given type is received.
function connectionEventPromise(eventType) {
  return new Promise(resolve => {
    let eventHandler = e => {
      assert_true(e instanceof USBConnectionEvent);
      navigator.usb.removeEventListener(eventType, eventHandler);
      resolve(e.device);
    };
    navigator.usb.addEventListener(eventType, eventHandler);
  });
}

// Creates a fake device and returns a promise that resolves once the
// 'connect' event is fired for the fake device. The promise is resolved with
// an object containing the fake USB device and the corresponding USBDevice.
function getFakeDevice() {
  let promise = connectionEventPromise('connect');
  let fakeDevice = navigator.usb.test.addFakeDevice(fakeDeviceInit);
  return promise.then(device => {
    return { device: device, fakeDevice: fakeDevice };
  });
}

// Disconnects the given device and returns a promise that is resolved when it
// is done.
function waitForDisconnect(fakeDevice) {
  let promise = connectionEventPromise('disconnect');
  fakeDevice.disconnect();
  return promise;
}

function assertRejectsWithError(promise, name, message) {
  return promise.then(() => {
    assert_unreached('expected promise to reject with ' + name);
  }, error => {
    assert_equals(error.name, name);
    if (message !== undefined)
      assert_equals(error.message, message);
  });
}

function assertDeviceInfoEquals(usbDevice, deviceInit) {
  for (var property in deviceInit) {
    if (property == 'activeConfigurationValue') {
      if (deviceInit.activeConfigurationValue == 0) {
        assert_equals(usbDevice.configuration, null);
      } else {
        assert_equals(usbDevice.configuration.configurationValue,
                      deviceInit.activeConfigurationValue);
      }
    } else if (Array.isArray(deviceInit[property])) {
      assert_equals(usbDevice[property].length, deviceInit[property].length);
      for (var i = 0; i < usbDevice[property].length; ++i)
        assertDeviceInfoEquals(usbDevice[property][i], deviceInit[property][i]);
    } else {
      assert_equals(usbDevice[property], deviceInit[property], property);
    }
  }
}

// TODO(reillyg): Remove when jyasskin upstreams this to testharness.js:
// https://crbug.com/509058.
function callWithKeyDown(functionCalledOnKeyPress) {
  return new Promise(resolve => {
    function onKeyPress() {
      document.removeEventListener('keypress', onKeyPress, false);
      resolve(functionCalledOnKeyPress());
    }
    document.addEventListener('keypress', onKeyPress, false);

    eventSender.keyDown(' ', []);
  });
}

function runGarbageCollection() {
  // Run gc() as a promise.
  return new Promise((resolve, reject) => {
    GCController.collect();
    setTimeout(resolve, 0);
  });
}
