import 'dart:convert';
import 'dart:io';

import 'package:path_provider/path_provider.dart';

import 'session_manifest.dart';

class StorageManager {
  Future<Directory> createSessionDirectory(String sessionId) async {
    final root = await getApplicationDocumentsDirectory();
    final sessionDir = Directory('${root.path}/sessions/$sessionId');
    await sessionDir.create(recursive: true);
    await Directory('${sessionDir.path}/frames').create(recursive: true);
    return sessionDir;
  }

  Future<void> writeManifest(
    String sessionDirPath,
    SessionManifest manifest,
  ) async {
    final file = File('$sessionDirPath/session.json');
    await file.writeAsString(jsonEncode(manifest.toJson()));
  }
}
