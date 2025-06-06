# Flipper Zero ESP32 Tool

This simple Flipper Zero application lets you send UART commands to a connected ESP32. It provides a menu with `Scan` and `Attack` actions.

## Usage

When launched, the app displays a menu with `Scan` and `Attack`. Use **UP**/**DOWN** to select an option and **OK** to enter it.

### Scan screen

* **OK** – send `scan` command.
* **BACK** – stop with `scanstop` or return to the menu.

### Attack screen

* **OK** – send `attack` command.
* **BACK** – stop with `attackstop` or return to the menu.

The application communicates over UART using the default Flipper settings.
Any pending console output is cleared on start so no stray characters are sent to the ESP32.

## Building

1. Clone the [official firmware repository](https://github.com/flipperdevices/flipperzero-firmware) and install `ufbt` as described in the [fbt documentation](https://github.com/flipperdevices/flipperzero-firmware/blob/dev/documentation/fbt.md).
2. Copy this `FliperApp` folder into the firmware's `applications_user` directory or create a symlink.
3. From the firmware directory run:

   ```bash
   ./fbt fap_scan_app
   ```

   The resulting `.fap` file will appear in `dist/apps/`.
4. Copy the `.fap` to your Flipper's `apps/` directory using qFlipper or USB mass storage.

For details on the manifest format see [App Manifests](https://github.com/flipperdevices/flipperzero-firmware/blob/dev/documentation/AppManifests.md). API documentation can be found in the [Flipper Doxygen pages](https://developer.flipper.net/flipperzero/doxygen/).

The `furi_hal_serial.h` header in this folder is a minimal stub providing only
the few functions used by this example so it can be built outside the firmware
tree.

For a more advanced example of UART usage see the
[uart_demo](https://github.com/jamisonderek/flipper-zero-tutorials/tree/main/gpio/uart_demo)
project referenced in Flipper Zero community tutorials.
