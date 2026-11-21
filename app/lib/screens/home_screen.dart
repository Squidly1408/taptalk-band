import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';

import '../ble/band_client.dart';
import '../models/board.dart';
import 'board_editor_screen.dart';

class HomeScreen extends StatefulWidget {
  const HomeScreen({super.key});

  @override
  State<HomeScreen> createState() => _HomeScreenState();
}

class _HomeScreenState extends State<HomeScreen> {
  List<ScanResult> _results = [];
  StreamSubscription<List<ScanResult>>? _scanSub;
  bool _scanning = false;
  String? _busyId;

  @override
  void dispose() {
    _scanSub?.cancel();
    FlutterBluePlus.stopScan();
    super.dispose();
  }

  Future<void> _scan() async {
    setState(() {
      _scanning = true;
      _results = [];
    });
    await _scanSub?.cancel();
    _scanSub = BandClient.scan().listen((r) => setState(() => _results = r));
    await Future<void>.delayed(const Duration(seconds: 10));
    if (mounted) setState(() => _scanning = false);
  }

  Future<void> _open(ScanResult r) async {
    setState(() => _busyId = r.device.remoteId.str);
    final client = BandClient(r.device);
    try {
      await FlutterBluePlus.stopScan();
      await client.connect();
      final board = await Board.createDefault();
      if (!mounted) return;
      await Navigator.of(context).push(MaterialPageRoute<void>(
        builder: (_) => BoardEditorScreen(board: board, client: client, bandName: r.device.platformName),
      ));
      await client.disconnect();
    } catch (e) {
      if (!mounted) return;
      final msg = e is BandException ? e.message : 'Could not connect. Is the band on its charger, or was the page key held for 3 seconds?';
      ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text(msg)));
    } finally {
      if (mounted) setState(() => _busyId = null);
    }
  }

  Future<void> _editOffline() async {
    final board = await Board.createDefault();
    if (!mounted) return;
    await Navigator.of(context).push(MaterialPageRoute<void>(
      builder: (_) => BoardEditorScreen(board: board),
    ));
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: const Text('TapTalk')),
      body: ListView(
        padding: const EdgeInsets.all(16),
        children: [
          Text('Nearby bands', style: Theme.of(context).textTheme.titleLarge),
          const SizedBox(height: 4),
          const Text('Put a band on its charger, or hold its side key for 3 seconds, then tap Find bands.'),
          const SizedBox(height: 16),
          FilledButton.icon(
            onPressed: _scanning ? null : _scan,
            icon: _scanning
                ? const SizedBox(width: 18, height: 18, child: CircularProgressIndicator(strokeWidth: 2))
                : const Icon(Icons.bluetooth_searching),
            label: Text(_scanning ? 'Looking for bands...' : 'Find bands'),
          ),
          const SizedBox(height: 8),
          for (final r in _results)
            Card(
              child: ListTile(
                leading: const Icon(Icons.watch),
                title: Text(r.device.platformName.isEmpty ? 'TapTalk band' : r.device.platformName),
                subtitle: Text('Signal ${r.rssi} dBm'),
                trailing: _busyId == r.device.remoteId.str
                    ? const CircularProgressIndicator()
                    : const Icon(Icons.chevron_right),
                onTap: _busyId == null ? () => _open(r) : null,
              ),
            ),
          if (!_scanning && _results.isEmpty)
            const Padding(
              padding: EdgeInsets.symmetric(vertical: 24),
              child: Text('No bands found yet.', textAlign: TextAlign.center),
            ),
          const Divider(height: 32),
          OutlinedButton.icon(
            onPressed: _editOffline,
            icon: const Icon(Icons.edit),
            label: const Text('Prepare a board without a band'),
          ),
        ],
      ),
    );
  }
}
