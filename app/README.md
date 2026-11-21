# TapTalk app

Flutter companion app: find a band over Bluetooth, edit its buttons, send them.

> **Status:** a starting point, **not yet compiled or run**. The Bluetooth protocol, tile conversion and editor are in place; expect small fixes on first build.

## Run

```bash
cd app
flutter create . --org org.taptalk --platforms=ios,android,windows,macos   # adds platform folders
flutter pub get
flutter test
flutter run
```

After `flutter create`, add the Bluetooth permissions each platform needs (see the [flutter_blue_plus setup guide](https://pub.dev/packages/flutter_blue_plus)):

- **iOS / macOS:** `NSBluetoothAlwaysUsageDescription` in `Info.plist` ("TapTalk uses Bluetooth to set up children's communication bands.")
- **Android:** `BLUETOOTH_SCAN` (with `neverForLocation`) and `BLUETOOTH_CONNECT` in `AndroidManifest.xml`
- **Camera and photos** for `image_picker`: `NSCameraUsageDescription` and `NSPhotoLibraryUsageDescription` on iOS

## Structure

| File | Does |
| --- | --- |
| `lib/ble/band_client.dart` | Scan, connect, pair, send only changed files, find-my-band ([protocol](../docs/ble-protocol.md)) |
| `lib/models/board.dart` | Board, pages, buttons; builds the files sent to the band ([format](../docs/board-format.md)) |
| `lib/image/tile_renderer.dart` | Draws symbol + label into a tile |
| `lib/image/eink_converter.dart` | Converts pixels to the band's 1-bit format (dithering for photos) |
| `lib/screens/` | Home (find bands), board editor, button editor |
| `lib/theme.dart` | Brand colours and the Paper, Night and High Contrast themes |

## To do

- [ ] Voice clips: record (e.g. the `record` package) and encode to Opus, or text-to-speech
- [ ] Symbol library (Mulberry Symbols / OpenMoji) with search
- [ ] Save and load boards locally; export/import a board file
- [ ] Class-wide send to many bands on the charging tray
- [ ] Settings screen (volume, vibrate-only, page lock, idle name) using `writeSettings`
- [ ] Battery level and last-seen per band
- [ ] Web Bluetooth build for Chromebooks (check flutter_blue_plus web support, or a small separate web app)
- [ ] Bundle the Atkinson Hyperlegible font
