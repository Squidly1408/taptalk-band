import json
import sys
import tempfile
import unittest
from pathlib import Path

from PIL import Image, ImageDraw

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from make_default_board import build  # noqa: E402
from tiles import GRID4_SIZE, image_to_tile, tile_to_image  # noqa: E402
from validate_board import validate  # noqa: E402


class TileFormatTest(unittest.TestCase):
    def test_round_trip(self):
        img = Image.new("1", GRID4_SIZE, 1)
        ImageDraw.Draw(img).rectangle([10, 20, 60, 70], fill=0)
        data = image_to_tile(img)
        self.assertEqual(len(data), 1250)
        self.assertEqual(list(tile_to_image(data, GRID4_SIZE).getdata()), list(img.getdata()))

    def test_bit_order_and_polarity(self):
        img = Image.new("1", GRID4_SIZE, 1)  # all white
        img.putpixel((0, 0), 0)               # first pixel black
        img.putpixel((0, 1), 0)               # first pixel of row 2: bit 100
        data = image_to_tile(img)
        self.assertEqual(data[0], 0x7F)       # MSB cleared, rest white
        self.assertEqual(data[100 >> 3], 0xFF & ~(0x80 >> (100 & 7)))  # no row padding


class ValidatorTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)
        build(self.dir)

    def tearDown(self):
        self.tmp.cleanup()

    def edit(self, fn):
        path = self.dir / "board.json"
        board = json.loads(path.read_text())
        fn(board)
        path.write_text(json.dumps(board))

    def test_default_board_is_valid(self):
        self.assertEqual(validate(self.dir), [])

    def test_default_board_shape(self):
        board = json.loads((self.dir / "board.json").read_text())
        self.assertEqual(len(board["pages"]), 5)
        self.assertEqual(board["pages"][0]["buttons"][0]["label"], "Help")
        self.assertEqual(board["pages"][0]["buttons"][1]["label"], "Stop")

    def test_label_too_long(self):
        self.edit(lambda b: b["pages"][0]["buttons"][0].update(label="Thirteen char"))
        self.assertTrue(any("label" in e for e in validate(self.dir)))

    def test_path_traversal_rejected(self):
        self.edit(lambda b: b["pages"][0]["buttons"][0].update(image="../secret.bin"))
        self.assertTrue(any("bad image path" in e for e in validate(self.dir)))

    def test_wrong_tile_size(self):
        board = json.loads((self.dir / "board.json").read_text())
        (self.dir / board["pages"][1]["buttons"][2]["image"]).write_bytes(b"\xff" * 100)
        self.assertTrue(any("1250 bytes" in e for e in validate(self.dir)))

    def test_wrong_button_count(self):
        self.edit(lambda b: b["pages"][0]["buttons"].pop())
        self.assertTrue(any("exactly 4 buttons" in e for e in validate(self.dir)))

    def test_too_many_pages(self):
        self.edit(lambda b: b["pages"].extend([dict(b["pages"][0], id=f"p{i}") for i in range(4)]))
        self.assertTrue(any("1 to 8 pages" in e for e in validate(self.dir)))

    def test_missing_sound(self):
        self.edit(lambda b: b["pages"][0]["buttons"][0].update(sound="snd/help.opus"))
        self.assertTrue(any("sound file missing" in e for e in validate(self.dir)))

    def test_page_lock_must_exist(self):
        self.edit(lambda b: b["settings"].update(page_lock="nope"))
        self.assertTrue(any("page_lock" in e for e in validate(self.dir)))


if __name__ == "__main__":
    unittest.main()
