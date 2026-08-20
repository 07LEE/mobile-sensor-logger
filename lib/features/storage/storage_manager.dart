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

  /// Lists recorded sessions, newest first.
  ///
  /// Directories without a readable manifest are skipped rather than failing
  /// the whole listing: a session killed mid-write should not hide the others.
  Future<List<SessionManifest>> listSessions() async {
    final root = await getApplicationDocumentsDirectory();
    final sessionsDir = Directory('${root.path}/sessions');
    if (!await sessionsDir.exists()) return [];

    final manifests = <SessionManifest>[];
    await for (final entry in sessionsDir.list()) {
      if (entry is! Directory) continue;

      final manifestFile = File('${entry.path}/session.json');
      if (!await manifestFile.exists()) continue;

      try {
        final json =
            jsonDecode(await manifestFile.readAsString()) as Map<String, dynamic>;
        manifests.add(SessionManifest.fromJson(json));
      } on FormatException {
        continue;
      }
    }

    manifests.sort((a, b) => b.startTime.compareTo(a.startTime));
    return manifests;
  }

  Future<String> sessionDirectoryPath(String sessionId) async {
    final root = await getApplicationDocumentsDirectory();
    return '${root.path}/sessions/$sessionId';
  }

  Future<void> deleteSession(String sessionId) async {
    final root = await getApplicationDocumentsDirectory();
    final sessionDir = Directory('${root.path}/sessions/$sessionId');
    if (await sessionDir.exists()) {
      await sessionDir.delete(recursive: true);
    }
  }
}
