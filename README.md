# Czech5
This project implements active broadcast deauth frames on selected targets on ESP32 C5.
Based on https://github.com/risinek/esp32-wifi-penetration-tool

# Features 
 - Up to 10 APs can be attacked with channel switching, just select them on the web page
 - Deauth frame has been fixed so now Active DOS attack works
- On the other hand, passive and mixed attack mode and handshake and PMKID attacks have been disabled

## Disclaimer

This project is provided for educational and authorized security testing purposes only. Use it solely on networks and devices you own or have explicit permission to test. The authors are not responsible for any misuse or damage caused by this software.

## RGB LED Status

The ESP32-C5 development board includes a single addressable RGB LED
connected to GPIO 27. The firmware uses this LED to show the current
state of the device:

- **Boot:** the LED slowly breathes orange until initialization finishes.
- **Scanning:** while access points are being scanned the LED blinks
  quickly in green.
- **Attack:** when an attack is in progress the LED flashes red in a
  double-blink pattern.

These patterns require `CONFIG_BLINK_GPIO=27` and
`CONFIG_BLINK_LED_STRIP=y` in the project configuration.
