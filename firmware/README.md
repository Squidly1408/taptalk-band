# Firmware

ESP-IDF project for the ESP32-S3, using the NimBLE Bluetooth stack.

> **Status:** written against the ESP-IDF v5.3 APIs but **not yet compiled or run on hardware**. Expect small fixes on first build. Opus clip playback and charger ship mode are still to do (marked `TODO` in the code).

## Build

```bash
# 1. Generate the default board into the storage image (flashed with the firmware)
python ../tools/make_default_board.py storage_image/live
python ../tools/make_default_board.py storage_image/factory

# 2. Build and flash (ESP-IDF v5.3 or later)
idf.py set-target esp32s3
idf.py build flash monitor
```

## Modules

| File | Does |
| --- | --- |
| `main.c` | Event loop: presses, pages, charger, Bluetooth requests, deep sleep |
| `buttons.c` | Debounced buttons; page-key hold gestures |
| `display.c` | SSD1681 e-paper driver (full and partial refresh) |
| `ui.c` | Built-in screens: pairing passkey, "charge me", "set me up" |
| `board_store.c` | Board files on flash: load, validate, staged transfer, safe commit, factory reset |
| `ble_service.c` | GATT service, pairing rules, advertising windows, firmware update |
| `audio.c` | I2S output; beeps now, Opus clips to do |
| `haptics.c` | Vibration pulses |
| `power.c` | Charger detect, battery gauge |
| `crc32.c` | CRC-32 used to check every received file |

## Page-key gestures

| Hold | Does |
| --- | --- |
| 1-3 s, then release | Next page (unless the page is locked) |
| 3 s | Bluetooth on for 2 minutes |
| 5 s, on the charger | Pairing mode (shows a 6-digit code) |
| Page key + button 1 for 10 s, on the charger | Factory reset: default board, pairings cleared |
| 15 s | Restart |

A production board should also add a hardware long-press reset chip on the page key, so a fully frozen band can be restarted even if the firmware is stuck.

## Notes

- Pairing is refused unless the band is on its charger **and** in pairing mode.
- Writes need an authenticated (passkey) bond; reads need an encrypted link.
- A received board is only swapped in after every file passes its CRC and `board.json` validates.
- New firmware that crashes before `esp_ota_mark_app_valid_cancel_rollback()` is rolled back by the bootloader.
