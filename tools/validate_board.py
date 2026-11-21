"""Validate a TapTalk board folder against docs/board-format.md.

Usage:
    python tools/validate_board.py BOARD_DIR

Exits 0 if the board is valid, 1 otherwise. Mirrors the checks in
firmware/main/board_store.c so problems are caught before a board reaches a band.
"""
from __future__ import annotations

import json
import re
import sys
from pathlib import Path

ID_RE = re.compile(r"^[a-z0-9_]{1,16}$")
PATH_RE = re.compile(r"^[a-z0-9_./-]+$")
PATH_MAX = 47
TILE_BYTES = {"grid4": 1250, "big2": 2500}
BUTTONS_PER_PAGE = {"grid4": 4, "big2": 2}


def path_ok(p: object) -> bool:
    return (
        isinstance(p, str)
        and 0 < len(p) <= PATH_MAX
        and not p.startswith("/")
        and ".." not in p
        and bool(PATH_RE.match(p))
    )


def validate(board_dir: Path) -> list[str]:
    errors: list[str] = []
    board_file = board_dir / "board.json"
    if not board_file.is_file():
        return ["missing board.json"]
    try:
        board = json.loads(board_file.read_text(encoding="utf-8"))
    except (json.JSONDecodeError, UnicodeDecodeError) as exc:
        return [f"board.json is not valid JSON: {exc}"]
    if len(board_file.read_bytes()) > 16 * 1024:
        errors.append("board.json is larger than 16 KB")

    if board.get("format") != 1:
        errors.append("format must be 1")
    layout = board.get("layout")
    if layout not in TILE_BYTES:
        errors.append("layout must be 'grid4' or 'big2'")
        return errors

    pages = board.get("pages")
    if not isinstance(pages, list) or not 1 <= len(pages) <= 8:
        errors.append("pages must be a list of 1 to 8 pages")
        return errors

    page_ids: set[str] = set()
    for p, page in enumerate(pages):
        pid = page.get("id") if isinstance(page, dict) else None
        where = f"page {pid or p}"
        if not isinstance(pid, str) or not ID_RE.match(pid):
            errors.append(f"{where}: id must be 1-16 of a-z, 0-9, _")
        elif pid in page_ids:
            errors.append(f"{where}: duplicate page id")
        else:
            page_ids.add(pid)

        buttons = page.get("buttons") if isinstance(page, dict) else None
        if not isinstance(buttons, list) or len(buttons) != BUTTONS_PER_PAGE[layout]:
            errors.append(f"{where}: needs exactly {BUTTONS_PER_PAGE[layout]} buttons for layout {layout}")
            continue

        button_ids: set[str] = set()
        for b, button in enumerate(buttons):
            bid = button.get("id") if isinstance(button, dict) else None
            bw = f"{where} button {bid or b}"
            if not isinstance(bid, str) or not ID_RE.match(bid):
                errors.append(f"{bw}: id must be 1-16 of a-z, 0-9, _")
            elif bid in button_ids:
                errors.append(f"{bw}: duplicate button id on this page")
            else:
                button_ids.add(bid)

            label = button.get("label")
            if not isinstance(label, str) or not 1 <= len(label) <= 12:
                errors.append(f"{bw}: label must be 1-12 characters")

            image = button.get("image")
            if not path_ok(image):
                errors.append(f"{bw}: bad image path {image!r}")
            else:
                f = board_dir / image
                if not f.is_file():
                    errors.append(f"{bw}: image file missing ({image})")
                elif f.stat().st_size != TILE_BYTES[layout]:
                    errors.append(f"{bw}: image must be {TILE_BYTES[layout]} bytes, is {f.stat().st_size}")

            sound = button.get("sound")
            if sound is not None:
                if not path_ok(sound):
                    errors.append(f"{bw}: bad sound path {sound!r}")
                elif not (board_dir / sound).is_file():
                    errors.append(f"{bw}: sound file missing ({sound})")

    settings = board.get("settings", {})
    if not isinstance(settings, dict):
        errors.append("settings must be an object")
    else:
        vol = settings.get("volume", 70)
        if not isinstance(vol, int) or not 0 <= vol <= 100:
            errors.append("settings.volume must be 0-100")
        guard = settings.get("repeat_guard_ms", 600)
        if not isinstance(guard, int) or not 0 <= guard <= 5000:
            errors.append("settings.repeat_guard_ms must be 0-5000")
        lock = settings.get("page_lock")
        if lock is not None and lock not in page_ids:
            errors.append("settings.page_lock must be null or an existing page id")
        idle = settings.get("idle_text", "")
        if not isinstance(idle, str) or len(idle) > 16:
            errors.append("settings.idle_text must be up to 16 characters")
    return errors


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    errors = validate(Path(sys.argv[1]))
    if errors:
        print("Board is NOT valid:")
        for e in errors:
            print(f"  - {e}")
        return 1
    print("Board is valid.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
