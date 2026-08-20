import 'dart:io';

import 'package:archive/archive_io.dart';
import 'package:flutter/foundation.dart';
import 'package:path_provider/path_provider.dart';

/// Packs a recorded session into a single ZIP for transfer off the device.
///
/// The ADR accepted losing live PC monitoring on the condition that sessions
/// could be exported afterwards; this is that export path.
class SessionExporter {
  /// Writes `<sessionId>.zip` into the cache directory and returns it.
  ///
  /// Exports land in the cache rather than alongside the recordings so a failed
  /// or abandoned share does not grow the session store, and so the OS can
  /// reclaim them under storage pressure.
  Future<File> export(String sessionDirPath) async {
    final sourceDir = Directory(sessionDirPath);
    if (!await sourceDir.exists()) {
      throw ArgumentError('Session directory does not exist: $sessionDirPath');
    }

    final cacheDir = await getTemporaryDirectory();
    final sessionId = sessionDirPath.split(Platform.pathSeparator).last;
    final outputPath = '${cacheDir.path}${Platform.pathSeparator}'
        '$sessionId.zip';

    await compute(_zipSessionEntry, [sessionDirPath, outputPath]);
    return File(outputPath);
  }
}

/// Compressing a session's frames is CPU-bound and long enough to drop frames
/// if run on the UI isolate, so [SessionExporter] hands this to [compute].
Future<void> _zipSessionEntry(List<String> paths) =>
    zipSessionDirectory(sourcePath: paths[0], outputPath: paths[1]);

/// Zips [sourcePath] to [outputPath], replacing any previous export.
///
/// Separate from [SessionExporter] so it can be exercised without the
/// platform channels `getTemporaryDirectory` needs.
@visibleForTesting
Future<void> zipSessionDirectory({
  required String sourcePath,
  required String outputPath,
}) async {
  final output = File(outputPath);
  if (await output.exists()) {
    await output.delete();
  }

  final encoder = ZipFileEncoder();
  await encoder.zipDirectory(Directory(sourcePath), filename: outputPath);
}
