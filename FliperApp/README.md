# Flipper Zero Scan App

This simple Flipper Zero application provides a menu to start or stop a scan on a connected device via UART.

## Usage

* **OK** – send `scan` command.
* **BACK** – first press sends `scanstop`, second press exits to the main menu.

The application communicates over UART using the default Flipper settings.

## Building

1. Clone the official Flipper Zero firmware and set up `ufbt` as described in the firmware documentation.
2. Copy this `FliperApp` folder into the firmware source tree or create a symlink.
3. From the firmware directory run:

   ```bash
   ./fbt fap_scan_app
   ```

   The resulting `.fap` file will appear in `dist/apps/`.
4. Copy the `.fap` to your Flipper's `apps/` directory using qFlipper or USB mass storage.
