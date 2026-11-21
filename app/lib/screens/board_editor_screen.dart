import 'dart:ui' as ui;

import 'package:flutter/material.dart';
import 'package:image_picker/image_picker.dart';

import '../ble/band_client.dart';
import '../image/tile_renderer.dart';
import '../models/board.dart';
import '../theme.dart';
import 'tile_preview.dart';

class BoardEditorScreen extends StatefulWidget {
  const BoardEditorScreen({super.key, required this.board, this.client, this.bandName});

  final Board board;
  final BandClient? client;  // null = editing offline
  final String? bandName;

  @override
  State<BoardEditorScreen> createState() => _BoardEditorScreenState();
}

class _BoardEditorScreenState extends State<BoardEditorScreen> {
  int _page = 0;
  double? _progress;

  Board get board => widget.board;

  Future<void> _send() async {
    final client = widget.client;
    if (client == null) return;
    setState(() => _progress = 0);
    try {
      await client.sendBoard(board.toFiles(), onProgress: (p) => setState(() => _progress = p));
      _toast('Sent. The band is showing the new buttons.');
    } on BandException catch (e) {
      _toast(e.message);
    } catch (e) {
      _toast('Sending failed: $e');
    } finally {
      if (mounted) setState(() => _progress = null);
    }
  }

  void _toast(String msg) {
    if (mounted) ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text(msg)));
  }

  Future<void> _editButton(BoardButton button) async {
    final changed = await showModalBottomSheet<bool>(
      context: context,
      isScrollControlled: true,
      builder: (_) => _ButtonEditorSheet(button: button, layout: board.layout),
    );
    if (changed == true) setState(() {});
  }

  @override
  Widget build(BuildContext context) {
    final page = board.pages[_page];
    final tile = board.layout.tileSize;
    return Scaffold(
      appBar: AppBar(
        title: Text(widget.bandName?.isNotEmpty == true ? widget.bandName! : 'Board editor'),
        actions: [
          if (widget.client != null)
            IconButton(
              tooltip: 'Find this band',
              icon: const Icon(Icons.notifications_active),
              onPressed: () => widget.client!.findMe(),
            ),
        ],
      ),
      body: Column(
        children: [
          SizedBox(
            height: 56,
            child: ListView(
              scrollDirection: Axis.horizontal,
              padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
              children: [
                for (var i = 0; i < board.pages.length; i++)
                  Padding(
                    padding: const EdgeInsets.only(right: 8),
                    child: ChoiceChip(
                      label: Text(board.pages[i].title),
                      selected: i == _page,
                      onSelected: (_) => setState(() => _page = i),
                    ),
                  ),
              ],
            ),
          ),
          Expanded(
            child: Center(
              child: ConstrainedBox(
                constraints: const BoxConstraints(maxWidth: 420),
                child: Padding(
                  padding: const EdgeInsets.all(16),
                  // The band face: tiles in their real positions.
                  child: Container(
                    padding: const EdgeInsets.all(12),
                    decoration: BoxDecoration(
                      color: TapTalkColors.sunnyYellow,
                      borderRadius: BorderRadius.circular(32),
                      border: Border.all(color: TapTalkColors.deepOcean, width: 4),
                    ),
                    child: GridView.count(
                      shrinkWrap: true,
                      crossAxisCount: 2,
                      mainAxisSpacing: 8,
                      crossAxisSpacing: 8,
                      childAspectRatio: tile.width / tile.height,
                      children: [
                        for (final b in page.buttons)
                          Semantics(
                            button: true,
                            label: 'Edit button ${b.label}',
                            child: InkWell(
                              onTap: () => _editButton(b),
                              child: TilePreview(
                                bits: b.tile,
                                tileWidth: tile.width.toInt(),
                                tileHeight: tile.height.toInt(),
                              ),
                            ),
                          ),
                      ],
                    ),
                  ),
                ),
              ),
            ),
          ),
          if (_progress != null) LinearProgressIndicator(value: _progress),
          SafeArea(
            child: Padding(
              padding: const EdgeInsets.all(16),
              child: SizedBox(
                width: double.infinity,
                child: FilledButton.icon(
                  onPressed: widget.client == null || _progress != null ? null : _send,
                  icon: const Icon(Icons.send),
                  label: Text(widget.client == null ? 'Connect a band to send' : 'Send to band'),
                ),
              ),
            ),
          ),
        ],
      ),
    );
  }
}

class _ButtonEditorSheet extends StatefulWidget {
  const _ButtonEditorSheet({required this.button, required this.layout});

  final BoardButton button;
  final BoardLayout layout;

  @override
  State<_ButtonEditorSheet> createState() => _ButtonEditorSheetState();
}

class _ButtonEditorSheetState extends State<_ButtonEditorSheet> {
  late final TextEditingController _label = TextEditingController(text: widget.button.label);
  ui.Image? _symbol;
  bool _isPhoto = false;
  bool _saving = false;

  @override
  void dispose() {
    _label.dispose();
    super.dispose();
  }

  Future<void> _pick(ImageSource source) async {
    final file = await ImagePicker().pickImage(source: source, maxWidth: 400, maxHeight: 400);
    if (file == null) return;
    final codec = await ui.instantiateImageCodec(await file.readAsBytes());
    final frame = await codec.getNextFrame();
    setState(() {
      _symbol = frame.image;
      _isPhoto = source == ImageSource.camera;
    });
  }

  Future<void> _save() async {
    final label = _label.text.trim();
    if (label.isEmpty) return;
    setState(() => _saving = true);
    widget.button
      ..label = label
      ..tile = await TileRenderer.render(
        label: label,
        size: widget.layout.tileSize,
        symbol: _symbol,
        isPhoto: _isPhoto,
      );
    if (mounted) Navigator.of(context).pop(true);
  }

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: EdgeInsets.fromLTRB(16, 16, 16, MediaQuery.of(context).viewInsets.bottom + 16),
      child: Column(
        mainAxisSize: MainAxisSize.min,
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          Text('Edit button', style: Theme.of(context).textTheme.titleLarge),
          const SizedBox(height: 12),
          TextField(
            controller: _label,
            maxLength: Board.maxLabelLength,
            decoration: const InputDecoration(labelText: 'Word or phrase', border: OutlineInputBorder()),
          ),
          Row(
            children: [
              Expanded(
                child: OutlinedButton.icon(
                  onPressed: () => _pick(ImageSource.gallery),
                  icon: const Icon(Icons.image),
                  label: const Text('Choose picture'),
                ),
              ),
              const SizedBox(width: 8),
              Expanded(
                child: OutlinedButton.icon(
                  onPressed: () => _pick(ImageSource.camera),
                  icon: const Icon(Icons.photo_camera),
                  label: const Text('Take photo'),
                ),
              ),
            ],
          ),
          if (_symbol != null)
            const Padding(
              padding: EdgeInsets.only(top: 8),
              child: Text('Picture chosen. It will be turned into black and white for the band.'),
            ),
          const SizedBox(height: 8),
          // TODO: record a voice clip (Opus) or generate one with text-to-speech.
          const Text('Voice recording is coming soon. For now the band beeps.'),
          const SizedBox(height: 16),
          FilledButton(
            onPressed: _saving ? null : _save,
            child: Text(_saving ? 'Saving...' : 'Save button'),
          ),
        ],
      ),
    );
  }
}
