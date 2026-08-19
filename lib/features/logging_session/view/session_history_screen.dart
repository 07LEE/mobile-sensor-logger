import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

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
              itemBuilder: (context, index) => _SessionTile(
                manifest: manifests[index],
                onDelete: () async {
                  await ref
                      .read(storageManagerProvider)
                      .deleteSession(manifests[index].sessionId);
                  ref.invalidate(sessionListProvider);
                },
              ),
            ),
          );
        },
      ),
    );
  }
}

class _SessionTile extends StatelessWidget {
  final SessionManifest manifest;
  final Future<void> Function() onDelete;

  const _SessionTile({required this.manifest, required this.onDelete});

  @override
  Widget build(BuildContext context) {
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
      trailing: IconButton(
        icon: const Icon(Icons.delete_outline),
        tooltip: 'Delete session',
        onPressed: () async {
          final confirmed = await _confirmDelete(context);
          if (confirmed) await onDelete();
        },
      ),
    );
  }

  Future<bool> _confirmDelete(BuildContext context) async {
    final result = await showDialog<bool>(
      context: context,
      builder: (context) => AlertDialog(
        title: const Text('Delete session?'),
        content: Text(
          '${manifest.sessionId} and all of its recorded data will be '
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
    return result ?? false;
  }

  String _formatDuration(Duration duration) {
    String two(int value) => value.toString().padLeft(2, '0');
    final minutes = two(duration.inMinutes.remainder(60));
    final seconds = two(duration.inSeconds.remainder(60));
    return '${duration.inHours}:$minutes:$seconds';
  }
}
