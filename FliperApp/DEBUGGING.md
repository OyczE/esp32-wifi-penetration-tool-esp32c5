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


## Troubleshooting

If `ufbt debug` prints "Remote communication error" or "Cannot access memory", the SWD connection failed to attach. Verify that the debug adapter (ST-Link or Flipper debug board) is properly connected and recognized by the OS. Try powering the Flipper off and on again before starting `ufbt debug`.

You should run debugging commands from the firmware root and keep the USB cable connected for both power and ST-Link. If OpenOCD still reports errors, check the `openocd.log` file in the `scripts/debug` directory for more detail.
