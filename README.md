# CYD 4.3" FN Tester

Starting-point firmware for a Sunton **ESP32-8048S043C** ("CYD", 4.3" 800×480
capacitive touch, ESP32-S3, 16MB flash / 8MB PSRAM, microSD, USB-C). Forked
from the C64-CYD companion panel project, with everything specific to the C64
Ultimate stripped out - this is meant as a clean base for a new device.

## What's included

- **Boot splash** — full-screen ShackMate logo shown for a few seconds at
  boot while Wi-Fi comes up (`ui_boot_splash.*`).
- **Boot recovery menu** — hold the screen within ~2s of boot to reach
  Restore Defaults / Erase SD Card / Exit (`ui_boot_menu.*`).
- **Top bar / menu icon** — hamburger menu opening a drawer (Wi-Fi, ESP-NOW,
  Setup), plus live Wi-Fi signal bars (`ui_shell.*`).
- **Wi-Fi setup** — scan, join, and remember networks (`ui_wifi_setup.*`).
- **ESP-NOW** — ShackMate device-discovery protocol: broadcasts a heartbeat,
  discovers other ShackMate devices on the same channel, and lists them
  (`espnow_protocol.h`, `espnow_state.*`, `ui_espnow.*`). See
  `ESPNOW_PROTOCOL.md` for the wire format.
- **Setup** — backlight screensaver settings (`ui_setup.*`).
- **Home tab** — placeholder tab showing Wi-Fi status; replace with whatever
  this device actually does (`ui_home.*`).

Not included (dropped from the source project): anything talking to a C64
Ultimate's REST API, the SD-card file browser/mounter, and the Ultimate
settings/status screens.

## Before you start building

A few things are left as placeholders and should be revisited once this
project has an actual identity:

- `espnow_state.cpp`'s `fill_local_identity()` sets `deviceType` to
  `SM_DEVICE_GENERIC` — pick (or add) the right `SM_DeviceType` in
  `espnow_protocol.h`.
- `config.h`'s default `espnow_friendly_name` ("FN Tester - 01") and
  `ui_shell.cpp`'s `kAppTitle` ("FN Tester") are placeholder names.
- `main.cpp`'s `ArduinoOTA.setHostname("cydfn")` / OTA env's `upload_port`
  in `platformio.ini` should get a project-specific hostname.
- The NVS namespace in `config.cpp` is `"fntester"`.

## Hardware

Board: Sunton **ESP32-8048S043C** (ESP32-S3, RGB parallel 800×480 IPS, GT911
capacitive touch, microSD over SPI).

| Function | Pins |
|---|---|
| RGB565 data | R: 8,3,46,9,1  G: 5,6,7,15,16,4  B: 45,48,47,21,14 |
| RGB control | HSYNC 39, VSYNC 41, DE 40, PCLK 42 |
| Backlight | GPIO 2 |
| GT911 touch (I2C) | SDA 19, SCL 20, RST 38, INT 18 (needs R17 solder-bridge for hw interrupt; firmware polls, so this is optional), I2C address 0x5D |
| microSD (SPI) | CS 10, MOSI 11, SCK 12, MISO 13 |
| USB-C (UART0) | TX 43, RX 44 |

## Software stack

- PlatformIO, `framework = arduino`, `board = esp32-8048S043C` (custom board
  JSON in `boards/`, from
  [rzeldent/platformio-espressif32-sunton](https://github.com/rzeldent/platformio-espressif32-sunton),
  picked up automatically from the project's `boards/` directory)
- LovyanGFX for the RGB panel + GT911 touch driver (`include/lgfx_config.h`)
- LVGL v9.5 (config pinned in `include/lv_conf.h`) for the UI
- `WiFi.h` for networking, `esp_now.h` for ESP-NOW, `SD.h` for the microSD
  card, `Preferences.h` (NVS) for settings

## Build & flash

```sh
pio run                    # compile
pio run -t upload          # flash over USB-C
pio device monitor         # serial monitor (115200 baud)
```

First boot: hold the touchscreen within ~2s of power-on to reach the
recovery menu if needed, otherwise open the hamburger menu -> **Wi-Fi** to
join a network.

OTA updates: `pio run -e esp32-8048S043C-ota -t upload` (see
`platformio.ini` for hostname/auth notes).
