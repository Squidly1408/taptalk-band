import 'dart:typed_data';

/// Converts images to the band's tile format (docs/board-format.md):
/// raw 1-bit, MSB first, packed with no row padding, 1 = white, 0 = black.
class EinkConverter {
  static int tileBytes(int width, int height) => (width * height + 7) >> 3;

  /// [rgba] is width*height*4 bytes. Transparent pixels count as white.
  /// Use [dither] for photos; leave it off for line-art symbols and text.
  static Uint8List pack(
    Uint8List rgba,
    int width,
    int height, {
    bool dither = false,
    int threshold = 128,
  }) {
    final n = width * height;
    if (rgba.length < n * 4) {
      throw ArgumentError('expected ${n * 4} bytes of RGBA, got ${rgba.length}');
    }
    final lum = Float64List(n);
    for (var i = 0; i < n; i++) {
      final r = rgba[i * 4], g = rgba[i * 4 + 1], b = rgba[i * 4 + 2];
      final a = rgba[i * 4 + 3] / 255.0;
      final l = 0.299 * r + 0.587 * g + 0.114 * b;
      lum[i] = l * a + 255.0 * (1 - a);
    }

    final out = Uint8List(tileBytes(width, height));
    for (var y = 0; y < height; y++) {
      for (var x = 0; x < width; x++) {
        final i = y * width + x;
        final old = lum[i];
        final white = old >= threshold;
        if (white) {
          out[i >> 3] |= 0x80 >> (i & 7);
        }
        if (dither) {
          final err = old - (white ? 255.0 : 0.0);
          if (x + 1 < width) lum[i + 1] += err * 7 / 16;
          if (y + 1 < height) {
            if (x > 0) lum[i + width - 1] += err * 3 / 16;
            lum[i + width] += err * 5 / 16;
            if (x + 1 < width) lum[i + width + 1] += err * 1 / 16;
          }
        }
      }
    }
    return out;
  }

  static bool isWhite(Uint8List bits, int width, int x, int y) {
    final i = y * width + x;
    return (bits[i >> 3] & (0x80 >> (i & 7))) != 0;
  }
}
