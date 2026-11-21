import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:flutter/material.dart';

import 'eink_converter.dart';

/// Draws a button tile (symbol + label) and converts it to e-ink bits.
/// The label is burned into the tile, so the band needs no fonts.
class TileRenderer {
  static const labelHeight = 26.0;

  static Future<Uint8List> render({
    required String label,
    required Size size,
    ui.Image? symbol,
    bool isPhoto = false,
  }) async {
    final w = size.width, h = size.height;
    final recorder = ui.PictureRecorder();
    final canvas = Canvas(recorder, Rect.fromLTWH(0, 0, w, h));
    final black = Paint()..color = Colors.black;

    canvas.drawRect(Rect.fromLTWH(0, 0, w, h), Paint()..color = Colors.white);
    canvas.drawRRect(
      RRect.fromRectAndRadius(Rect.fromLTWH(3, 3, w - 6, h - 6), const Radius.circular(12)),
      Paint()
        ..color = Colors.black
        ..style = PaintingStyle.stroke
        ..strokeWidth = 3,
    );

    final symbolBox = Rect.fromLTRB(10, 10, w - 10, h - labelHeight - 6);
    if (symbol != null) {
      paintImage(canvas: canvas, rect: symbolBox, image: symbol, fit: BoxFit.contain);
    } else {
      final initial = label.trim().isEmpty ? '?' : label.trim()[0].toUpperCase();
      _drawText(canvas, initial, symbolBox, symbolBox.height * 0.85);
    }

    canvas.drawLine(Offset(10, h - labelHeight - 4), Offset(w - 10, h - labelHeight - 4), black..strokeWidth = 2);
    _drawText(canvas, label, Rect.fromLTRB(7, h - labelHeight - 2, w - 7, h - 5), 18);

    final image = await recorder.endRecording().toImage(w.toInt(), h.toInt());
    final data = await image.toByteData(format: ui.ImageByteFormat.rawRgba);
    return EinkConverter.pack(data!.buffer.asUint8List(), w.toInt(), h.toInt(), dither: isPhoto);
  }

  /// Draws text centred in [box], shrinking the font until it fits the width.
  static void _drawText(Canvas canvas, String text, Rect box, double startSize) {
    var size = startSize;
    late TextPainter tp;
    while (true) {
      tp = TextPainter(
        text: TextSpan(
          text: text,
          style: TextStyle(color: Colors.black, fontSize: size, fontWeight: FontWeight.w700),
        ),
        textDirection: TextDirection.ltr,
        maxLines: 1,
      )..layout();
      if (tp.width <= box.width || size <= 8) break;
      size -= 1;
    }
    tp.paint(canvas, Offset(box.center.dx - tp.width / 2, box.center.dy - tp.height / 2));
  }
}
