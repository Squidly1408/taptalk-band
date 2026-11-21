"""Generate the default TapTalk board (5 pages x 4 buttons) as a board folder.

Usage:
    python tools/make_default_board.py OUT_DIR [--preview preview.png]

The tiles are placeholders (initial + label). Swap in real symbol artwork before a pilot.
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
from tiles import GRID4_SIZE, placeholder_tile, write_tile  # noqa: E402

DEFAULT_PAGES = [
    ("core", "Core", ["Help", "Stop", "My turn", "Toilet"]),
    ("play", "Play", ["Go", "Again", "Want", "Finished"]),
    ("feelings", "Feelings", ["Happy", "Sad", "Angry", "Hurt"]),
    ("needs", "Needs", ["Drink", "Eat", "Break", "Hug"]),
    ("answer", "Answer", ["Yes", "No", "Don't know", "More"]),
]


def button_id(label: str) -> str:
    return "".join(c if c.isalnum() else "_" for c in label.lower()).strip("_")[:16]


def build(out_dir: Path) -> dict:
    pages = []
    for page_id, title, labels in DEFAULT_PAGES:
        buttons = []
        for label in labels:
            bid = button_id(label)
            image_rel = f"img/{page_id}_{bid}.bin"
            write_tile(out_dir / image_rel, placeholder_tile(label, GRID4_SIZE))
            buttons.append({"id": bid, "label": label, "image": image_rel})
        pages.append({"id": page_id, "title": title, "buttons": buttons})

    board = {
        "format": 1,
        "name": "Default",
        "layout": "grid4",
        "settings": {
            "volume": 70,
            "vibrate_only": False,
            "page_lock": None,
            "repeat_guard_ms": 600,
            "idle_text": "",
        },
        "pages": pages,
    }
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / "board.json").write_text(json.dumps(board, indent=2) + "\n", encoding="utf-8")
    return board


def preview(board: dict, out_dir: Path, preview_path: Path) -> None:
    """Draw every page side by side, as the e-ink will show them."""
    from tiles import tile_to_image

    gap = 16
    pages = board["pages"]
    sheet = Image.new("L", (len(pages) * (200 + gap) + gap, 200 + 2 * gap), 200)
    for p, page in enumerate(pages):
        ox = gap + p * (200 + gap)
        for i, button in enumerate(page["buttons"]):
            tile = tile_to_image((out_dir / button["image"]).read_bytes(), GRID4_SIZE)
            sheet.paste(tile.convert("L"), (ox + (i % 2) * 100, gap + (i // 2) * 100))
    preview_path.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(preview_path)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("out_dir", type=Path)
    parser.add_argument("--preview", type=Path, help="also write a PNG of all pages")
    args = parser.parse_args()
    board = build(args.out_dir)
    if args.preview:
        preview(board, args.out_dir, args.preview)
    print(f"Wrote default board to {args.out_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
