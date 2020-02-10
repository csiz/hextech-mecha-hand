Requires:

* [esp-idf-v3.2.3](https://github.com/espressif/esp-idf/releases/tag/v3.2.3) install via the esp tools installer.
* [arduino-esp32-v1.0.4](https://github.com/espressif/arduino-esp32/releases/tag/1.0.4) install from readme instructions.
* [Adafruit_SSD1306](https://github.com/adafruit/Adafruit_SSD1306), extract to `components/arduino/libraries`.
* [Adafruit-GFX-Library](https://github.com/adafruit/Adafruit-GFX-Library), extract to `components/arduino/libraries`.
* [ESPAsyncWebServer](https://github.com/me-no-dev/ESPAsyncWebServer), extract to `components`.
* [AsyncTCP](https://github.com/me-no-dev/AsyncTCP), extract to `components`.
* Make sure to configure AsyncTCP to run on core 0.
* Also configure CONFIG_ARDUINO_EVENT_RUNNING_CORE for core 0. This is used by the WiFi library.
* Install [node js v13 and npm](https://nodejs.org/en/), and update local packages with `npm install`.
* Build with webpack (we need it to tree-shake d3 so we don't include the entire lib) by running `npm run build`.
* Install [mkspiffs](https://github.com/igrr/mkspiffs/releases/download/0.2.3/mkspiffs-0.2.3-arduino-esp32-win32.zip) (or use esp-idf v4's spiffsgen.py tool), and run command to create the spiffs image: `mkspiffs -c web/dist -b 4096 -p 256 -s 0x30000 build/spiffs.bin`.
* Install [esptool](https://github.com/espressif/esptool) and run command to upload web page to flash/spiffs: `python -m esptool --chip esp32 --port COM3 --baud 115200 write_flash -z 0x110000 build/spiffs.bin`.
