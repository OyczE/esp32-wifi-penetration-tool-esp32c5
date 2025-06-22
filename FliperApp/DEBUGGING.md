# Debugging the Flipper Zero App

This app is built with the standard `fbt`/`ufbt` tooling from the official firmware.
Using these tools you can flash the app and view logs to track down crashes.

## Quick steps

1. Build the app from the firmware directory:
   ```bash
   ./fbt fap_scan_app
   ```
2. Flash it to the device over USB:
   ```bash
   ./fbt flash_usb
   ```
3. Open the log console to watch UART output:
   ```bash
   ./fbt log
   ```
4. For step-by-step debugging attach GDB:
   ```bash
   ./fbt debug
   ```

Refer to the [`fbt` documentation](https://github.com/flipperdevices/flipperzero-firmware/blob/dev/documentation/fbt.md) for more options.

