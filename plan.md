# Plan to deprecate BluetoothRemoteGATTCharacteristic.writeValue()

This plan outlines the steps to deprecate the `writeValue` method on the `BluetoothRemoteGATTCharacteristic` interface and provide a console message to developers.

The method `writeValue` is being deprecated in favor of `writeValueWithResponse()` and `writeValueWithoutResponse()`.

-   [writeValue() specification](https://webbluetoothcg.github.io/web-bluetooth/#dom-bluetoothremotegattcharacteristic-writevalue)
-   [writeValueWithResponse() specification](https://webbluetoothcg.github.io/web-bluetooth/#dom-bluetoothremotegattcharacteristic-writevaluewithresponse)
-   [writeValueWithoutResponse() specification](https://webbluetoothcg.github.io/web-bluetooth/#dom-bluetoothremotegattcharacteristic-writevaluewithoutresponse)

## 1. Add a Use Counter (DONE)

-   **File:** `third_party/blink/public/mojom/use_counter/metrics/web_feature.mojom`
-   **Action:** Add a new enum value: `kBluetoothRemoteGATTCharacteristicWriteValue`. This will be used to track the usage of the deprecated method.

## 2. Modify the IDL file (DONE)

-   **File:** `third_party/blink/renderer/modules/bluetooth/bluetooth_remote_gatt_characteristic.idl`
-   **Action:** Add the `[DeprecateAs=BluetoothRemoteGATTCharacteristicWriteValue]` extended attribute to the `writeValue` method. This will trigger a warning in the developer console and count usage of the feature.

## 3. Add deprecation metadata (DONE)

-   **File:** `third_party/blink/renderer/core/frame/deprecation/deprecation.json5`
-   **Action:** Add a new entry for `BluetoothRemoteGATTCharacteristicWriteValue` to provide a descriptive message to developers in the DevTools console. The message should recommend using `writeValueWithResponse()` or `writeValueWithoutResponse()` instead.

## 4. Verification (IN PROGRESS)

**Build Chromium (DONE)**

### 4.1. Manual Test Page for Deprecation Warning (DONE)

A manual test page has been created to verify the deprecation warning. This test will use a virtual Heart Rate Service peripheral, which can be simulated using the "nRF Toolbox for Bluetooth LE" Android application.
...
-   **File Location:** `testing/web-bluetooth/manual/writeValue-deprecation.html` (created).
...
#### Manual Testing Steps: (TODO)
...
### 4.2. Update Web Bluetooth Samples and Verify (IN PROGRESS)

To provide developers with up-to-date examples and to verify that the new methods do not trigger deprecation warnings, we will update the official Web Bluetooth samples.

#### Sample Update Plan:

1.  **Clone the Samples Repository (DONE):**
    -   **Action:** Clone the `gh-pages` branch of your fork of the `googlechrome/samples` repository to the `out/googlechrome-samples` directory.
    -   **Command:** `git clone https://github.com/scheib/googlechrome-samples.git --branch gh-pages out/googlechrome-samples`
2.  **Navigate to Directory (DONE):**
    -   **Action:** Change into the newly cloned repository's directory.
    -   **Command:** `cd out/googlechrome-samples`
3.  **Create New Sample Files (DONE):**
    -   **Action:** Create two new HTML files in the `web-bluetooth` directory.
    -   **File 1:** `out/googlechrome-samples/web-bluetooth/writeValueWithResponse-sample.html`
    -   **File 2:** `out/googlechrome-samples/web-bluetooth/writeValueWithoutResponse-sample.html`
4.  **Update the Sample Index (DONE):**
    -   **Action:** Add links to the two new sample files in the main `index.html` for the web-bluetooth section.
    -   **File:** `out/googlechrome-samples/web-bluetooth/index.html`
5.  **Verify the New Samples (TODO):**
    -   **Action:** Open the new local sample files in the modified Chromium build.
    -   **Links to run tests:**
        -   [writeValueWithResponse-sample.html](file:///usr/local/google/home/scheib/c/src/out/googlechrome-samples/web-bluetooth/writeValueWithResponse-sample.html)
        -   [writeValueWithoutResponse-sample.html](file:///usr/local/google/home/scheib/c/src/out/googlechrome-samples/web-bluetooth/writeValueWithoutResponse-sample.html)
    -   **Steps:** Connect to the virtual peripheral and click the test buttons on each page.
    -   **Expected Result:** Verify that **no deprecation warning** appears in the DevTools console and that the write operations succeed.
6.  **Commit Local Changes (TODO):**
    -   **Action:** Stage the new and modified files and commit them locally to the `gh-pages` branch.
    -   **Commands:**
        ```bash
        git add web-bluetooth/writeValueWithResponse-sample.html web-bluetooth/writeValueWithoutResponse-sample.html web-bluetooth/index.html
        git commit -m "Add Web Bluetooth samples for writeValueWithResponse and writeValueWithoutResponse"
        ```
7.  **Manual Review and Push (TODO):**
    -   **Action:** The local changes are now committed on the `gh-pages` branch. **Do not push these changes yet.** A human should first review the changes locally to ensure they are correct.
    -   **To review:** Use `git show` to see the commit or `git diff HEAD~1` to see the changes.
    -   **When ready, the human operator should manually push the changes to the remote repository.**
    -   **Manual Push Command:** `git push origin gh-pages`
8.  **Create a Pull Request (TODO):**
    -   **Action:** After manually pushing the changes, go to your forked repository on GitHub (`https://github.com/scheib/googlechrome-samples`) and open a pull request from the `gh-pages` branch to the `gh-pages` branch of the `googlechrome/samples` repository.
### 4.3. Automated Tests (FAILED)

-   **Action:** Run existing web bluetooth tests to ensure that the change doesn't introduce any regressions. The command `third_party/blink/tools/run_web_tests.py --build-directory=out/Default external/wpt/bluetooth` was used to run the relevant tests.
-   **Result:** The tests failed unexpectedly. This appears to be unrelated to the deprecation change, as the failures are widespread and not specific to `writeValue`. This will require separate investigation.