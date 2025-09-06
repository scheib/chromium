# Plan to deprecate BluetoothRemoteGATTCharacteristic.writeValue()

This plan outlines the steps to deprecate the `writeValue` method on the `BluetoothRemoteGATTCharacteristic` interface and provide a console message to developers.

The method `writeValue` is being deprecated in favor of `writeValueWithResponse()` and `writeValueWithoutResponse()`.

-   [writeValue() specification](https://webbluetoothcg.github.io/web-bluetooth/#dom-bluetoothremotegattcharacteristic-writevalue)
-   [writeValueWithResponse() specification](https://webbluetoothcg.github.io/web-bluetooth/#dom-bluetoothremotegattcharacteristic-writevaluewithresponse)
-   [writeValueWithoutResponse() specification](https://webbluetoothcg.github.io/web-bluetooth/#dom-bluetoothremotegattcharacteristic-writevaluewithoutresponse)

## 1. Add a Use Counter

-   **File:** `third_party/blink/public/mojom/use_counter/metrics/web_feature.mojom`
-   **Action:** Add a new enum value: `kBluetoothRemoteGATTCharacteristicWriteValue`. This will be used to track the usage of the deprecated method.

## 2. Modify the IDL file

-   **File:** `third_party/blink/renderer/modules/bluetooth/bluetooth_remote_gatt_characteristic.idl`
-   **Action:** Add the `[DeprecateAs=BluetoothRemoteGATTCharacteristicWriteValue]` extended attribute to the `writeValue` method. This will trigger a warning in the developer console and count usage of the feature.

## 3. Add deprecation metadata

-   **File:** `third_party/blink/renderer/core/frame/deprecation/deprecation.json5`
-   **Action:** Add a new entry for `BluetoothRemoteGATTCharacteristicWriteValue` to provide a descriptive message to developers in the DevTools console. The message should recommend using `writeValueWithResponse()` or `writeValueWithoutResponse()` instead.

## 4. Verification

### 4.1. Manual Test Page for Deprecation Warning

A manual test page will be created to verify the deprecation warning. This test will use a virtual Heart Rate Service peripheral, which can be simulated using the "nRF Toolbox for Bluetooth LE" Android application.

-   **nRF Toolbox App:** [Google Play Store](https://play.google.com/store/apps/details?id=no.nordicsemi.android.nrftoolbox)

#### Test Page Plan:

-   **File Location:** Create a new file at `testing/web-bluetooth/manual/writeValue-deprecation.html`.
-   **HTML Structure:**
    -   A "Connect to Heart Rate Monitor" button.
    -   A "Test deprecated writeValue()" button.
    -   A status log area (`<pre>` tag) to display progress.
-   **JavaScript Logic:**
    1.  **Connect:**
        -   On "Connect" button click, call `navigator.bluetooth.requestDevice` with a filter for the `heart_rate` service.
        -   Connect to the device's GATT server.
        -   Get the `heart_rate` primary service.
        -   Get the `heart_rate_control_point` characteristic. This characteristic is writeable and will be used for the test.
        -   Log status updates to the page.
    2.  **Test `writeValue`:**
        -   On "Test" button click, call `characteristic.writeValue(new Uint8Array([1]))`. The value `1` corresponds to the "Reset Energy Expended" command for the Heart Rate Control Point, which is a safe operation.
        -   Log that the function was called.

#### Manual Testing Steps:

1.  **Prepare Peripheral:**
    -   On an Android phone, install and open the "nRF Toolbox" app.
    -   Select the "Heart Rate Monitor" utility.
    -   The app will now be advertising a virtual Bluetooth LE Heart Rate peripheral.
2.  **Execute Test:**
    -   Build and run the modified version of Chromium.
    -   Open the test page (`testing/web-bluetooth/manual/writeValue-deprecation.html`).
    -   Open DevTools and select the "Console" tab.
    -   Click the "Connect to Heart Rate Monitor" button.
    -   Select the device from the Bluetooth device chooser dialog (it should be named "Heart Rate").
    -   Wait for the status log on the page to confirm a successful connection and that the characteristic is ready.
    -   Click the "Test deprecated writeValue()" button.
3.  **Verify Result:**
    -   **Primary Check:** A deprecation warning message must appear in the DevTools console. The message should state that `writeValue()` is deprecated and recommend using `writeValueWithResponse()` or `writeValueWithoutResponse()`.
    -   **Secondary Check:** The `writeValue` operation should still complete successfully without errors (aside from the deprecation warning).

### 4.2. Update Web Bluetooth Samples and Verify

To provide developers with up-to-date examples and to verify that the new methods do not trigger deprecation warnings, we will update the official Web Bluetooth samples.

#### Sample Update Plan:

1.  **Clone the Samples Repository:**
    -   **Action:** Clone the `gh-pages` branch of your fork of the `googlechrome/samples` repository to the `out/googlechrome-samples` directory.
    -   **Command:** `git clone https://github.com/scheib/googlechrome-samples.git --branch gh-pages out/googlechrome-samples`
2.  **Navigate to Directory:**
    -   **Action:** Change into the newly cloned repository's directory.
    -   **Command:** `cd out/googlechrome-samples`
3.  **Create New Sample Files:**
    -   **Action:** Create two new HTML files in the `web-bluetooth` directory.
    -   **File 1:** `out/googlechrome-samples/web-bluetooth/writeValueWithResponse-sample.html`
        -   **Content:** This page will demonstrate the use of `writeValueWithResponse()`. It will connect to the virtual Heart Rate Monitor and provide a button to call the method, logging the result.
    -   **File 2:** `out/googlechrome-samples/web-bluetooth/writeValueWithoutResponse-sample.html`
        -   **Content:** This page will demonstrate the use of `writeValueWithoutResponse()`. It will have similar functionality to the other sample.
4.  **Update the Sample Index:**
    -   **Action:** Add links to the two new sample files in the main `index.html` for the web-bluetooth section.
    -   **File:** `out/googlechrome-samples/web-bluetooth/index.html`
5.  **Verify the New Samples:**
    -   **Action:** Open the new local sample files in the modified Chromium build.
    -   **Steps:** Connect to the virtual peripheral and click the test buttons on each page.
    -   **Expected Result:** Verify that **no deprecation warning** appears in the DevTools console and that the write operations succeed.
6.  **Commit Local Changes:**
    -   **Action:** Stage the new and modified files and commit them locally to the `gh-pages` branch.
    -   **Commands:**
        ```bash
        git add web-bluetooth/writeValueWithResponse-sample.html web-bluetooth/writeValueWithoutResponse-sample.html web-bluetooth/index.html
        git commit -m "Add Web Bluetooth samples for writeValueWithResponse and writeValueWithoutResponse"
        ```
7.  **Manual Review and Push:**
    -   **Action:** The local changes are now committed on the `gh-pages` branch. **Do not push these changes yet.** A human should first review the changes locally to ensure they are correct.
    -   **To review:** Use `git show` to see the commit or `git diff HEAD~1` to see the changes.
    -   **When ready, the human operator should manually push the changes to the remote repository.**
    -   **Manual Push Command:** `git push origin gh-pages`
8.  **Create a Pull Request:**
    -   **Action:** After manually pushing the changes, go to your forked repository on GitHub (`https://github.com/scheib/googlechrome-samples`) and open a pull request from the `gh-pages` branch to the `gh-pages` branch of the `googlechrome/samples` repository.

### 4.3. Automated Tests

-   **Action:** Run existing web bluetooth tests to ensure that the change doesn't introduce any regressions. The command `third_party/blink/tools/run_web_tests.py http/tests/bluetooth` can be used to run the relevant tests.

This plan is based on the information in the [Deprecation README](https://chromium.googlesource.com/chromium/src/+/refs/heads/main/third_party/blink/renderer/core/frame/deprecation/README.md) and the [IDL Extended Attributes documentation](https://chromium.googlesource.com/chromium/src/+/HEAD/third_party/blink/renderer/bindings/IDLExtendedAttributes.md).