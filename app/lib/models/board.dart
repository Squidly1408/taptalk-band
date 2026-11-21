import 'dart:convert';
import 'dart:typed_data';
import 'dart:ui';

import '../image/tile_renderer.dart';

enum BoardLayout { grid4, big2 }

extension BoardLayoutInfo on BoardLayout {
  int get buttonsPerPage => this == BoardLayout.grid4 ? 4 : 2;
  Size get tileSize => this == BoardLayout.grid4 ? const Size(100, 100) : const Size(100, 200);
  String get jsonName => name;
}

class BoardButton {
  BoardButton({required this.id, required this.label, required this.tile, this.sound});

  String id;
  String label;
  Uint8List tile;    // packed e-ink bits
  Uint8List? sound;  // Ogg/Opus clip, optional

  String imagePath(String pageId) => 'img/${pageId}_$id.bin';
  String soundPath(String pageId) => 'snd/${pageId}_$id.opus';
}

class BoardPage {
  BoardPage({required this.id, required this.title, required this.buttons});

  String id;
  String title;
  List<BoardButton> buttons;
}

class BoardSettings {
  int volume = 70;
  bool vibrateOnly = false;
  String? pageLock;
  int repeatGuardMs = 600;
  String idleText = '';

  Map<String, Object?> toJson() => {
        'volume': volume,
        'vibrate_only': vibrateOnly,
        'page_lock': pageLock,
        'repeat_guard_ms': repeatGuardMs,
        'idle_text': idleText,
      };
}

class Board {
  Board({required this.name, required this.layout, required this.pages, BoardSettings? settings})
      : settings = settings ?? BoardSettings();

  String name;
  BoardLayout layout;
  List<BoardPage> pages;
  BoardSettings settings;

  static const maxPages = 8;
  static const maxLabelLength = 12;

  Map<String, Object?> toJson() => {
        'format': 1,
        'name': name,
        'layout': layout.jsonName,
        'settings': settings.toJson(),
        'pages': [
          for (final p in pages)
            {
              'id': p.id,
              'title': p.title,
              'buttons': [
                for (final b in p.buttons)
                  {
                    'id': b.id,
                    'label': b.label,
                    'image': b.imagePath(p.id),
                    if (b.sound != null) 'sound': b.soundPath(p.id),
                  }
              ],
            }
        ],
      };

  /// Every file of the board, keyed by its path on the band.
  Map<String, Uint8List> toFiles() {
    final files = <String, Uint8List>{
      'board.json': Uint8List.fromList(utf8.encode(const JsonEncoder.withIndent('  ').convert(toJson()))),
    };
    for (final p in pages) {
      for (final b in p.buttons) {
        files[b.imagePath(p.id)] = b.tile;
        if (b.sound != null) files[b.soundPath(p.id)] = b.sound!;
      }
    }
    return files;
  }

  /// Same 5 pages as tools/make_default_board.py, with placeholder tiles.
  static Future<Board> createDefault() async {
    const pages = [
      ('core', 'Core', ['Help', 'Stop', 'My turn', 'Toilet']),
      ('play', 'Play', ['Go', 'Again', 'Want', 'Finished']),
      ('feelings', 'Feelings', ['Happy', 'Sad', 'Angry', 'Hurt']),
      ('needs', 'Needs', ['Drink', 'Eat', 'Break', 'Hug']),
      ('answer', 'Answer', ['Yes', 'No', "Don't know", 'More']),
    ];
    final result = <BoardPage>[];
    for (final (id, title, labels) in pages) {
      final buttons = <BoardButton>[];
      for (final label in labels) {
        buttons.add(BoardButton(
          id: buttonIdFor(label),
          label: label,
          tile: await TileRenderer.render(label: label, size: BoardLayout.grid4.tileSize),
        ));
      }
      result.add(BoardPage(id: id, title: title, buttons: buttons));
    }
    return Board(name: 'Default', layout: BoardLayout.grid4, pages: result);
  }

  static String buttonIdFor(String label) {
    final id = label.toLowerCase().replaceAll(RegExp(r'[^a-z0-9]+'), '_').replaceAll(RegExp(r'^_+|_+$'), '');
    final safe = id.isEmpty ? 'button' : id;
    return safe.length > 16 ? safe.substring(0, 16) : safe;
  }
}
