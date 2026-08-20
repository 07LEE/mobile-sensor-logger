import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:share_plus/share_plus.dart';

import '../../../providers/engine_providers.dart';
import '../../storage/session_manifest.dart';

/// Lists sessions already on disk. Re-read on each visit rather than cached,
/// since recording a new session invalidates it immediately.
final sessionListProvider = FutureProvider.autoDispose<List<SessionManifest>>(
  (ref) => ref.read(storageManagerProvider).listSessions(),
);

class SessionHistoryScreen extends ConsumerWidget {
  const SessionHistoryScreen({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final sessions = ref.watch(sessionListProvider);

    return Scaffold(
      appBar: AppBar(title: const Text('Session History')),
      body: sessions.when(
        loading: () => const Center(child: CircularProgressIndicator()),
        error: (error, _) => Center(
          child: Padding(
            padding: const EdgeInsets.all(32),
            child: Text(
              'Could not read sessions: $error',
              textAlign: TextAlign.center,
              style: TextStyle(color: Theme.of(context).colorScheme.error),
            ),
          ),
        ),
        data: (manifests) {
          if (manifests.isEmpty) {
            return const Center(child: Text('No sessions recorded yet'));
          }

          return RefreshIndicator(
            onRefresh: () async => ref.invalidate(sessionListProvider),
            child: ListView.separated(
              itemCount: manifests.length,
              separatorBuilder: (_, _) => const Divider(height: 1),
              itemBuilder: (context, index) =>
                  _SessionTile(manifest: manifests[index]),
            ),
          );
        },
      ),
    );
  }
}

class _SessionTile extends ConsumerStatefulWidget {
  final SessionManifest manifest;

  const _SessionTile({required this.manifest});

  @override
  ConsumerState<_SessionTile> createState() => _SessionTileState();
}

class _SessionTileState extends ConsumerState<_SessionTile> {
  bool _isExporting = false;

  @override
  Widget build(BuildContext context) {
    final manifest = widget.manifest;
    final duration = manifest.duration;

    return ListTile(
      leading: const Icon(Icons.folder_outlined),
      title: Text(manifest.sessionId),
      subtitle: Text(
        duration == null
            ? 'Incomplete — no end time recorded'
            : 'Duration ${_formatDuration(duration)}'
                ' · IMU ${manifest.imuSampleRateHz}Hz',
      ),
      trailing: Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          if (_isExporting)
            const Padding(
              padding: EdgeInsets.all(12),
              child: SizedBox(
                width: 20,
                height: 20,
                child: CircularProgressIndicator(strokeWidth: 2),
              ),
            )
          else
            IconButton(
              icon: const Icon(Icons.ios_share),
              tooltip: 'Export session',
              onPressed: _export,
            ),
          IconButton(
            icon: const Icon(Icons.delete_outline),
            tooltip: 'Delete session',
            onPressed: _isExporting ? null : _confirmAndDelete,
          ),
        ],
      ),
    );
  }

  /// Zips the session and hands it to the system share sheet, where the user
  /// chooses the destination.
  Future<void> _export() async {
    setState(() => _isExporting = true);

    try {
      final sessionId = widget.manifest.sessionId;
      final dirPath =
          await ref.read(storageManagerProvider).sessionDirectoryPath(sessionId);
      final zip = await ref.read(sessionExporterProvider).export(dirPath);

      if (!mounted) return;
      await SharePlus.instance.share(
        ShareParams(
          files: [XFile(zip.path)],
          fileNameOverrides: ['$sessionId.zip'],
        ),
      );
    } catch (error) {
      if (!mounted) return;
      ScaffoldMessenger.of(context)
        ..hideCurrentSnackBar()
        ..showSnackBar(
          SnackBar(
            content: Text('Export failed: $error'),
            backgroundColor: Theme.of(context).colorScheme.error,
          ),
        );
    } finally {
      if (mounted) setState(() => _isExporting = false);
    }
  }

  Future<void> _confirmAndDelete() async {
    final confirmed = await showDialog<bool>(
      context: context,
      builder: (context) => AlertDialog(
        title: const Text('Delete session?'),
        content: Text(
          '${widget.manifest.sessionId} and all of its recorded data will be '
          'permanently removed.',
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.of(context).pop(false),
            child: const Text('Cancel'),
          ),
          TextButton(
            onPressed: () => Navigator.of(context).pop(true),
            child: const Text('Delete'),
          ),
        ],
      ),
    );

    if (confirmed != true) return;

    await ref
        .read(storageManagerProvider)
        .deleteSession(widget.manifest.sessionId);
    ref.invalidate(sessionListProvider);
  }

  String _formatDuration(Duration duration) {
    String two(int value) => value.toString().padLeft(2, '0');
    final minutes = two(duration.inMinutes.remainder(60));
    final seconds = two(duration.inSeconds.remainder(60));
    return '${duration.inHours}:$minutes:$seconds';
  }
}
