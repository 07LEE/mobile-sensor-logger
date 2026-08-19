import 'dart:io';

class CsvWriter {
  final File file;
  IOSink? _sink;
  final List<String> _buffer = [];

  CsvWriter(this.file);

  Future<void> open(List<String> header) async {
    _sink = file.openWrite();
    _sink!.writeln(header.join(','));
  }

  void writeRow(List<Object?> row) {
    _buffer.add(row.map((value) => value ?? '').join(','));
  }

  Future<void> flush() async {
    if (_buffer.isEmpty) return;
    _sink?.writeln(_buffer.join('\n'));
    _buffer.clear();
  }

  Future<void> close() async {
    await flush();
    await _sink?.close();
  }
}
