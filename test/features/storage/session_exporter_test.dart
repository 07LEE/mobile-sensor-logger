import 'dart:io';

import 'package:archive/archive_io.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:mobile_sensor_logger/features/storage/session_exporter.dart';

void main() {
  late Directory tempDir;
  late Directory sessionDir;

  setUp(() async {
    tempDir = await Directory.systemTemp.createTemp('session_exporter_test');
    sessionDir = Directory('${tempDir.path}/session_20260820_143000');
    await Directory('${sessionDir.path}/frames').create(recursive: true);

    await File('${sessionDir.path}/session.json')
        .writeAsString('{"session_id":"session_20260820_143000"}');
    await File('${sessionDir.path}/imu.csv')
        .writeAsString('timestamp_us,ang_vel_x\n1,0.5\n');
    await File('${sessionDir.path}/frames/000001_1.jpg')
        .writeAsBytes([0xFF, 0xD8, 0xFF, 0xD9]);
  });

  tearDown(() async {
    await tempDir.delete(recursive: true);
  });

  List<String> entryPaths(String zipPath) {
    final archive = ZipDecoder().decodeBytes(File(zipPath).readAsBytesSync());
    return archive.files.where((f) => f.isFile).map((f) => f.name).toList();
  }

  group('zipSessionDirectory', () {
    test('includes every session file, nested ones included', () async {
      final outputPath = '${tempDir.path}/out.zip';

      await zipSessionDirectory(
        sourcePath: sessionDir.path,
        outputPath: outputPath,
      );

      expect(
        entryPaths(outputPath),
        containsAll([
          'session.json',
          'imu.csv',
          'frames/000001_1.jpg',
        ]),
      );
    });

    test('round trips file contents unchanged', () async {
      final outputPath = '${tempDir.path}/out.zip';

      await zipSessionDirectory(
        sourcePath: sessionDir.path,
        outputPath: outputPath,
      );

      final archive =
          ZipDecoder().decodeBytes(File(outputPath).readAsBytesSync());
      final imu = archive.files.firstWhere((f) => f.name == 'imu.csv');

      expect(
        String.fromCharCodes(imu.content as List<int>),
        'timestamp_us,ang_vel_x\n1,0.5\n',
      );
    });

    test('replaces a previous export instead of appending to it', () async {
      final outputPath = '${tempDir.path}/out.zip';

      await zipSessionDirectory(
        sourcePath: sessionDir.path,
        outputPath: outputPath,
      );
      final firstCount = entryPaths(outputPath).length;

      await zipSessionDirectory(
        sourcePath: sessionDir.path,
        outputPath: outputPath,
      );

      expect(entryPaths(outputPath).length, firstCount);
    });

    test('produces an archive without the session directory as a prefix',
        () async {
      final outputPath = '${tempDir.path}/out.zip';

      await zipSessionDirectory(
        sourcePath: sessionDir.path,
        outputPath: outputPath,
      );

      expect(
        entryPaths(outputPath),
        isNot(anyElement(startsWith('session_20260820_143000/'))),
      );
    });
  });

  group('SessionExporter', () {
    test('rejects a session directory that does not exist', () async {
      expect(
        () => SessionExporter().export('${tempDir.path}/missing'),
        throwsArgumentError,
      );
    });
  });
}
