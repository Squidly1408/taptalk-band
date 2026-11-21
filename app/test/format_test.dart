import 'dart:typed_data';

import 'package:flutter_test/flutter_test.dart';
import 'package:taptalk/image/eink_converter.dart';
import 'package:taptalk/util/crc32.dart';

void main() {
  test('CRC-32 matches zlib', () {
    final data = 'The quick brown fox jumps over the lazy dog'.codeUnits;
    expect(Crc32.compute(data), 1095738169);  // zlib.crc32 of the same string
    final part = Crc32.compute(data.sublist(0, 10));
    expect(Crc32.compute(data.sublist(10), part), 1095738169);
  });

  test('tile packing: polarity, MSB first, no row padding', () {
    const w = 100, h = 100;
    final rgba = Uint8List(w * h * 4)..fillRange(0, w * h * 4, 255);  // all white
    void black(int x, int y) {
      final i = (y * w + x) * 4;
      rgba[i] = rgba[i + 1] = rgba[i + 2] = 0;
    }

    black(0, 0);
    black(0, 1);  // bit 100
    final bits = EinkConverter.pack(rgba, w, h);
    expect(bits.length, 1250);
    expect(bits[0], 0x7F);
    expect(bits[100 >> 3], 0xFF & ~(0x80 >> (100 & 7)));
    expect(EinkConverter.isWhite(bits, w, 1, 0), isTrue);
    expect(EinkConverter.isWhite(bits, w, 0, 1), isFalse);
  });
}
