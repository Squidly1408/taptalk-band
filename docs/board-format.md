# Board format (version 1)

A **board** is everything a band shows and says: its pages, buttons, pictures, voice clips and settings. On the band it lives in a folder; the app builds the same folder and sends it over Bluetooth.

```
board.json
img/<page>_<button>.bin     one tile image per button
snd/<page>_<button>.opus    one voice clip per button (optional)
```

## board.json

```json
{
  "format": 1,
  "name": "Default",
  "layout": "grid4",
  "settings": {
    "volume": 70,
    "vibrate_only": false,
    "page_lock": null,
    "repeat_guard_ms": 600,
    "idle_text": ""
  },
  "pages": [
    {
      "id": "core",
      "title": "Core",
      "buttons": [
        { "id": "help", "label": "Help", "image": "img/core_help.bin", "sound": "snd/core_help.opus" }
      ]
    }
  ]
}
```

| Field | Rules |
| --- | --- |
| `format` | Always `1` for this version |
| `layout` | `grid4` (4 buttons per page) or `big2` (2 tall buttons per page) |
| `settings.volume` | 0-100 |
| `settings.page_lock` | `null` or a page `id` the band stays on |
| `settings.repeat_guard_ms` | Presses of the same button closer than this are ignored (0-5000) |
| `settings.idle_text` | Up to 16 characters, e.g. the child's first name |
| `pages` | 1 to 8 pages |
| `page.id`, `button.id` | Lowercase letters, digits and `_`, up to 16 characters, unique |
| `buttons` | Exactly 4 per page for `grid4`, exactly 2 for `big2` |
| `button.label` | 1 to 12 characters (used for text-to-speech and accessibility) |
| `button.image` | Path to a tile image (required) |
| `button.sound` | Path to an Opus clip (optional; the band beeps if missing) |

Paths must be relative, use only `a-z 0-9 _ - . /`, and never contain `..`.

## Tile images (`.bin`)

Raw 1-bit bitmaps, already sized for the panel, so the band needs no image decoder:

| Layout | Tile size | File size |
| --- | --- | --- |
| `grid4` | 100 x 100 px | 1,250 bytes |
| `big2` | 100 x 200 px | 2,500 bytes |

Rows top to bottom, 8 pixels per byte, most significant bit = leftmost pixel, packed continuously with **no padding at the end of a row** (100 px rows don't end on a byte boundary). **1 = white, 0 = black** (the SSD1681 panel's native polarity). The app draws the symbol **and the label text** into the tile, so the band needs no fonts.

## Voice clips (`.opus`)

Mono, 16 kHz, Opus in an Ogg container, up to 3 seconds (about 6 KB at 16 kbit/s).

## Validating

```bash
python tools/validate_board.py path/to/board-folder
```
