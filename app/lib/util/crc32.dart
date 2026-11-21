/// Standard CRC-32 (IEEE 802.3, same as zlib), matching firmware/main/crc32.c.
class Crc32 {
  static final List<int> _table = List<int>.generate(256, (i) {
    var c = i;
    for (var k = 0; k < 8; k++) {
      c = (c & 1) != 0 ? (0xEDB88320 ^ (c >>> 1)) : (c >>> 1);
    }
    return c.toUnsigned(32);
  });

  static int compute(List<int> data, [int crc = 0]) {
    var c = (crc ^ 0xFFFFFFFF).toUnsigned(32);
    for (final b in data) {
      c = (_table[(c ^ b) & 0xFF] ^ (c >>> 8)).toUnsigned(32);
    }
    return (c ^ 0xFFFFFFFF).toUnsigned(32);
  }
}
