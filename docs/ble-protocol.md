# Bluetooth LE protocol (version 1)

The band is a Bluetooth LE peripheral with one custom GATT service. All writes require an **encrypted, authenticated (passkey) bond**. Control messages are UTF-8 JSON; bulk data is binary.

## When the band is reachable

The band advertises only:

- while it is on a charger, or
- for 2 minutes after the page key is held for 3 seconds.

Advertised name: `TapTalk-XXXX` (last 4 hex digits of the MAC), with the service UUID in the advertising data.

## Pairing

1. The band must be on its charger (otherwise pairing is rejected).
2. Staff hold the page key for 5 seconds; the band enters pairing mode.
3. The app connects and starts pairing. The band shows a 6-digit passkey on its screen (IO capability: display only).
4. Staff type the passkey into the app. LE Secure Connections with MITM protection; the bond is stored. Up to 8 bonds; the oldest is dropped first.

## Service and characteristics

Base UUID: `9e1d0000-5b2c-4f7a-8a3e-6c7d2e1f0b0a`

| Name | UUID | Properties | Payload |
| --- | --- | --- | --- |
| Service | `9e1d0001-…` | | |
| Device info | `9e1d0002-…` | read | JSON |
| Manifest | `9e1d0003-…` | read | JSON |
| File control | `9e1d0004-…` | write, notify | JSON commands / JSON results |
| File data | `9e1d0005-…` | write without response | binary chunks |
| Settings | `9e1d0006-…` | read, write | JSON |
| Find me | `9e1d0007-…` | write | 1 byte (seconds, 1-10) |
| OTA control | `9e1d0008-…` | write, notify | JSON |
| OTA data | `9e1d0009-…` | write without response | binary chunks |

(`…` = `-5b2c-4f7a-8a3e-6c7d2e1f0b0a`)

### Device info

```json
{ "name": "Band 07", "colour": "teal", "fw": "0.1.0", "battery": 82, "charging": true, "format": 1 }
```

### Manifest

Every file in the live board with its size and CRC-32, so the app sends only what changed.

```json
{ "files": [ { "path": "board.json", "size": 2048, "crc32": 3735928559 } ] }
```

### Sending a board

1. `{"op":"session","mode":"patch"}` starts a staging copy of the live board (`"mode":"replace"` starts empty).
2. For each changed file:
   - `{"op":"begin","path":"img/core_help.bin","size":1250,"crc32":1234567}`
   - write chunks to **File data**: `[seq: uint16 little-endian][bytes]`, `seq` starting at 0, each chunk up to MTU - 5 bytes
   - `{"op":"end"}`: the band checks size and CRC-32 and notifies the result
3. Optional: `{"op":"delete","path":"snd/old.opus"}`
4. `{"op":"commit"}`: the band checks `board.json`, swaps the staged board in, and redraws.
5. `{"op":"abort"}` discards the staging copy at any point.

Results are notified on File control:

```json
{ "ok": true, "op": "end", "path": "img/core_help.bin" }
{ "ok": false, "op": "end", "err": "crc" }
```

Error codes: `busy`, `bad_json`, `bad_path`, `no_session`, `no_file`, `seq`, `size`, `crc`, `io`, `invalid_board`.

CRC-32 is the standard IEEE 802.3 CRC (the same as zlib `crc32`).

### Firmware update

1. OTA control: `{"op":"begin","size":1234567}`
2. OTA data: `[seq: uint16 LE][bytes]` chunks
3. OTA control: `{"op":"end"}`. The band verifies the image, sets it as the next boot partition and restarts. If the new firmware crashes or restarts before it has started up cleanly and confirmed itself, the bootloader rolls back to the previous firmware.
