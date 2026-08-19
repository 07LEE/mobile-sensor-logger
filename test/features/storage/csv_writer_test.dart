import 'dart:io';

import 'package:flutter_test/flutter_test.dart';
import 'package:mobile_sensor_logger/features/storage/csv_writer.dart';

void main() {
  late Directory tempDir;

  setUp(() async {
    tempDir = await Directory.systemTemp.createTemp('csv_writer_test');
  });

  tearDown(() async {
    await tempDir.delete(recursive: true);
  });

  test('writes header and buffered rows', () async {
    final file = File('${tempDir.path}/out.csv');
    final writer = CsvWriter(file);

    await writer.open(['a', 'b']);
    writer.writeRow([1, 2]);
    writer.writeRow([3, 4]);
    await writer.close();

    expect(await file.readAsLines(), ['a,b', '1,2', '3,4']);
  });

  test('renders null cells as empty', () async {
    final file = File('${tempDir.path}/nulls.csv');
    final writer = CsvWriter(file);

    await writer.open(['a', 'b']);
    writer.writeRow([1, null]);
    await writer.close();

    expect(await file.readAsLines(), ['a,b', '1,']);
  });

  test('flush is a no-op when nothing is buffered', () async {
    final file = File('${tempDir.path}/empty.csv');
    final writer = CsvWriter(file);

    await writer.open(['a']);
    await writer.flush();
    await writer.close();

    expect(await file.readAsLines(), ['a']);
  });
}
