import 'dart:async';
import 'dart:convert';
import 'dart:math';
import 'dart:typed_data';

import 'package:flutter/foundation.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';

import '../util/crc32.dart';

/// UUIDs from docs/ble-protocol.md.
class BandUuids {
  static String _u(String n) => '9e1d$n-5b2c-4f7a-8a3e-6c7d2e1f0b0a';
  static final service = Guid(_u('0001'));
  static final info = Guid(_u('0002'));
  static final manifest = Guid(_u('0003'));
  static final fileCtrl = Guid(_u('0004'));
  static final fileData = Guid(_u('0005'));
  static final settings = Guid(_u('0006'));
  static final findMe = Guid(_u('0007'));
}

class BandException implements Exception {
  BandException(this.code, [this.op]);
  final String code;
  final String? op;

  /// Plain-language message for staff.
  String get message => switch (code) {
        'invalid_board' => 'The band rejected the board. Check every button has a picture and a label.',
        'crc' || 'size' || 'seq' => 'Part of the transfer was lost. Move closer to the band and try again.',
        'io' => 'The band could not save the files. Try again; if it keeps happening, restart the band.',
        'timeout' => 'The band stopped answering. Make sure it is nearby and switched on for Bluetooth.',
        _ => 'Something went wrong ($code).',
      };

  @override
  String toString() => 'BandException($code, op: $op)';
}

/// Talks to one band over Bluetooth LE.
class BandClient {
  BandClient(this.device);

  final BluetoothDevice device;
  late BluetoothCharacteristic _info, _manifest, _fileCtrl, _fileData, _settings, _findMe;
  StreamSubscription<List<int>>? _sub;
  final _responses = StreamController<Map<String, dynamic>>.broadcast();

  /// Scan for nearby bands (they only advertise on the charger or after a
  /// 3-second hold of the page key).
  static Stream<List<ScanResult>> scan({Duration timeout = const Duration(seconds: 10)}) {
    FlutterBluePlus.startScan(withServices: [BandUuids.service], timeout: timeout);
    return FlutterBluePlus.scanResults;
  }

  Future<void> connect() async {
    await device.connect(timeout: const Duration(seconds: 15));
    if (!kIsWeb && defaultTargetPlatform == TargetPlatform.android) {
      await device.requestMtu(517);
    }
    final services = await device.discoverServices();
    final svc = services.firstWhere(
      (s) => s.uuid == BandUuids.service,
      orElse: () => throw BandException('not_a_band'),
    );
    BluetoothCharacteristic find(Guid g) => svc.characteristics.firstWhere((c) => c.uuid == g);
    _info = find(BandUuids.info);
    _manifest = find(BandUuids.manifest);
    _fileCtrl = find(BandUuids.fileCtrl);
    _fileData = find(BandUuids.fileData);
    _settings = find(BandUuids.settings);
    _findMe = find(BandUuids.findMe);

    // Reading an encrypted characteristic starts pairing the first time. The
    // band shows a 6-digit code; the phone's system dialog asks for it.
    await _info.read();

    await _fileCtrl.setNotifyValue(true);
    _sub = _fileCtrl.onValueReceived.listen((value) {
      try {
        _responses.add(jsonDecode(utf8.decode(value)) as Map<String, dynamic>);
      } catch (_) {
        // ignore malformed notifications
      }
    });
  }

  Future<void> disconnect() async {
    await _sub?.cancel();
    await device.disconnect();
  }

  Future<Map<String, dynamic>> readInfo() async =>
      jsonDecode(utf8.decode(await _info.read())) as Map<String, dynamic>;

  Future<Map<String, int>> readManifest() async {
    final json = jsonDecode(utf8.decode(await _manifest.read())) as Map<String, dynamic>;
    return {
      for (final f in (json['files'] as List).cast<Map<String, dynamic>>())
        f['path'] as String: (f['crc32'] as num).toInt(),
    };
  }

  Future<void> findMe({int seconds = 3}) => _findMe.write([seconds.clamp(1, 10)]);

  Future<void> writeSettings(Map<String, Object?> settings) =>
      _settings.write(utf8.encode(jsonEncode(settings)));

  Future<Map<String, dynamic>> _command(Map<String, Object?> cmd) async {
    final op = cmd['op'];
    final reply = _responses.stream
        .firstWhere((r) => r['op'] == op || r['op'] == 'data')
        .timeout(const Duration(seconds: 30), onTimeout: () => throw BandException('timeout', op as String?));
    await _fileCtrl.write(utf8.encode(jsonEncode(cmd)));
    final r = await reply;
    if (r['ok'] != true) {
      throw BandException((r['err'] as String?) ?? 'unknown', r['op'] as String?);
    }
    return r;
  }

  /// Send a board, transferring only files that differ from what the band has.
  /// [onProgress] gets 0.0-1.0.
  Future<void> sendBoard(Map<String, Uint8List> files, {void Function(double)? onProgress}) async {
    final live = await readManifest();
    final changed = files.entries.where((e) => live[e.key] != Crc32.compute(e.value)).toList();
    final deletes = live.keys.where((p) => !files.containsKey(p)).toList();
    if (changed.isEmpty && deletes.isEmpty) {
      onProgress?.call(1);
      return;
    }

    await _command({'op': 'session', 'mode': 'patch'});
    try {
      final total = max(1, changed.fold<int>(0, (s, e) => s + e.value.length));
      var sent = 0;
      final chunkSize = max(20, device.mtuNow - 5);  // ATT header 3 + our seq 2

      for (final e in changed) {
        final data = e.value;
        await _command({'op': 'begin', 'path': e.key, 'size': data.length, 'crc32': Crc32.compute(data)});
        var seq = 0;
        for (var off = 0; off < data.length; off += chunkSize) {
          final end = min(off + chunkSize, data.length);
          final packet = Uint8List(2 + end - off)
            ..[0] = seq & 0xFF
            ..[1] = (seq >> 8) & 0xFF
            ..setRange(2, 2 + end - off, data, off);
          await _fileData.write(packet, withoutResponse: true);
          seq++;
          sent += end - off;
          onProgress?.call(sent / total);
        }
        await _command({'op': 'end'});
      }
      for (final path in deletes) {
        await _command({'op': 'delete', 'path': path});
      }
      await _command({'op': 'commit'});
    } catch (_) {
      // Leave the band on its last good board.
      try {
        await _command({'op': 'abort'});
      } catch (_) {}
      rethrow;
    }
  }
}
