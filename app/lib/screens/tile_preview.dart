import 'dart:typed_data';

import 'package:flutter/material.dart';

import '../image/eink_converter.dart';

/// Shows a tile exactly as the e-ink will: pure black and white pixels.
class TilePreview extends StatelessWidget {
  const TilePreview({super.key, required this.bits, required this.tileWidth, required this.tileHeight});

  final Uint8List bits;
  final int tileWidth;
  final int tileHeight;

  @override
  Widget build(BuildContext context) {
    return AspectRatio(
      aspectRatio: tileWidth / tileHeight,
      child: CustomPaint(painter: _TilePainter(bits, tileWidth, tileHeight)),
    );
  }
}

class _TilePainter extends CustomPainter {
  _TilePainter(this.bits, this.w, this.h);

  final Uint8List bits;
  final int w, h;

  @override
  void paint(Canvas canvas, Size size) {
    canvas.drawRect(Offset.zero & size, Paint()..color = const Color(0xFFF4F1EA));
    final sx = size.width / w, sy = size.height / h;
    final path = Path();
    for (var y = 0; y < h; y++) {
      var runStart = -1;
      for (var x = 0; x <= w; x++) {
        final black = x < w && !EinkConverter.isWhite(bits, w, x, y);
        if (black && runStart < 0) runStart = x;
        if (!black && runStart >= 0) {
          path.addRect(Rect.fromLTWH(runStart * sx, y * sy, (x - runStart) * sx, sy + 0.5));
          runStart = -1;
        }
      }
    }
    canvas.drawPath(path, Paint()..color = const Color(0xFF111111));
  }

  @override
  bool shouldRepaint(_TilePainter old) => old.bits != bits;
}
