"""Tile image helpers shared by the board tools.

Tile format (docs/board-format.md): raw 1-bit, rows top to bottom, 8 px per byte,
MSB = leftmost pixel, packed continuously with no row padding, 1 = white, 0 = black.
"""
from __future__ import annotations

from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

GRID4_SIZE = (100, 100)
BIG2_SIZE = (100, 200)

FONT_CANDIDATES = [
    "AtkinsonHyperlegible-Bold.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
    "/Library/Fonts/Arial Bold.ttf",
    "C:/Windows/Fonts/arialbd.ttf",
]


def tile_bytes(size: tuple[int, int]) -> int:
    w, h = size
    return (w * h + 7) // 8


def image_to_tile(img: Image.Image) -> bytes:
    """Pack a PIL image (any mode) into tile bytes. Dithers greys to black/white."""
    bw = img.convert("L").convert("1")  # Floyd-Steinberg dither
    w, h = bw.size
    pixels = bw.load()
    out = bytearray(tile_bytes((w, h)))
    for y in range(h):
        for x in range(w):
            if pixels[x, y]:  # non-zero = white
                bit = y * w + x
                out[bit >> 3] |= 0x80 >> (bit & 7)
    return bytes(out)


def tile_to_image(data: bytes, size: tuple[int, int]) -> Image.Image:
    """Unpack tile bytes back into a 1-bit PIL image (for previews and tests)."""
    w, h = size
    if len(data) != tile_bytes(size):
        raise ValueError(f"expected {tile_bytes(size)} bytes, got {len(data)}")
    img = Image.new("1", size, 0)
    pixels = img.load()
    for y in range(h):
        for x in range(w):
            bit = y * w + x
            if data[bit >> 3] & (0x80 >> (bit & 7)):
                pixels[x, y] = 1
    return img


def load_font(size: int) -> ImageFont.ImageFont:
    for candidate in FONT_CANDIDATES:
        try:
            return ImageFont.truetype(candidate, size)
        except OSError:
            continue
    return ImageFont.load_default()


def fit_font(draw: ImageDraw.ImageDraw, text: str, max_width: int, start: int) -> ImageFont.ImageFont:
    size = start
    while size > 8:
        font = load_font(size)
        left, _, right, _ = draw.textbbox((0, 0), text, font=font)
        if right - left <= max_width:
            return font
        size -= 1
    return load_font(8)


def placeholder_tile(label: str, size: tuple[int, int] = GRID4_SIZE) -> Image.Image:
    """A clean placeholder: a rounded frame, the label's initial large, the label below.

    Replace these with real symbols (e.g. Mulberry Symbols or OpenMoji) before a pilot.
    """
    w, h = size
    img = Image.new("L", size, 255)
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([3, 3, w - 4, h - 4], radius=12, outline=0, width=3)
    label_h = 26
    symbol_box = (10, 10, w - 10, h - label_h - 6)
    initial = label.strip()[0].upper() if label.strip() else "?"
    font_big = load_font(int((symbol_box[3] - symbol_box[1]) * 0.9))
    cx = (symbol_box[0] + symbol_box[2]) / 2
    cy = (symbol_box[1] + symbol_box[3]) / 2
    d.text((cx, cy), initial, font=font_big, fill=0, anchor="mm")
    d.line([10, h - label_h - 4, w - 10, h - label_h - 4], fill=0, width=2)
    font = fit_font(d, label, w - 14, 18)
    d.text((w / 2, h - label_h / 2 - 3), label, font=font, fill=0, anchor="mm")
    return img


def write_tile(path: Path, img: Image.Image) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(image_to_tile(img))
