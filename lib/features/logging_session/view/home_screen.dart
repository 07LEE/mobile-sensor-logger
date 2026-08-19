import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../controller/session_controller.dart';
import '../models/session.dart';
import '../models/session_status.dart';
import 'session_history_screen.dart';

class HomeScreen extends ConsumerWidget {
  const HomeScreen({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final sessionState = ref.watch(sessionControllerProvider);
    final controller = ref.read(sessionControllerProvider.notifier);

    final isStarting = sessionState.isLoading;
    final isRecording =
        sessionState.value?.status == SessionStatus.recording;

    ref.listen(sessionControllerProvider, (previous, next) {
      final error = next.error;
      if (error == null) return;

      ScaffoldMessenger.of(context)
        ..hideCurrentSnackBar()
        ..showSnackBar(
          SnackBar(
            content: Text(error.toString()),
            backgroundColor: Theme.of(context).colorScheme.error,
            duration: const Duration(seconds: 6),
          ),
        );
    });

    return Scaffold(
      appBar: AppBar(
        title: const Text('Mobile Sensor Logger'),
        actions: [
          IconButton(
            icon: const Icon(Icons.history),
            tooltip: 'Session history',
            onPressed: () => Navigator.of(context).push(
              MaterialPageRoute<void>(
                builder: (_) => const SessionHistoryScreen(),
              ),
            ),
          ),
        ],
      ),
      body: Center(
        child: _StatusView(
          isStarting: isStarting,
          isRecording: isRecording,
          session: sessionState.value,
          error: sessionState.error,
        ),
      ),
      floatingActionButton: FloatingActionButton(
        onPressed: isStarting
            ? null
            : () => isRecording
                ? controller.stopSession()
                : controller.startSession(),
        backgroundColor: isStarting ? Theme.of(context).disabledColor : null,
        child: Icon(isRecording ? Icons.stop : Icons.fiber_manual_record),
      ),
    );
  }
}

class _StatusView extends StatelessWidget {
  final bool isStarting;
  final bool isRecording;
  final Session? session;
  final Object? error;

  const _StatusView({
    required this.isStarting,
    required this.isRecording,
    required this.session,
    required this.error,
  });

  @override
  Widget build(BuildContext context) {
    if (isStarting) {
      return const Column(
        mainAxisAlignment: MainAxisAlignment.center,
        children: [
          CircularProgressIndicator(),
          SizedBox(height: 16),
          Text('Starting sensors...'),
        ],
      );
    }

    final theme = Theme.of(context);

    return Column(
      mainAxisAlignment: MainAxisAlignment.center,
      children: [
        Icon(
          isRecording ? Icons.sensors : Icons.sensors_off,
          size: 64,
          color: isRecording ? theme.colorScheme.error : theme.disabledColor,
        ),
        const SizedBox(height: 16),
        Text(
          isRecording ? 'Recording' : 'Idle',
          style: theme.textTheme.headlineSmall,
        ),
        if (session != null) ...[
          const SizedBox(height: 8),
          Text(session!.id, style: theme.textTheme.bodySmall),
        ],
        if (error != null) ...[
          const SizedBox(height: 16),
          Padding(
            padding: const EdgeInsets.symmetric(horizontal: 32),
            child: Text(
              error.toString(),
              textAlign: TextAlign.center,
              style: theme.textTheme.bodyMedium
                  ?.copyWith(color: theme.colorScheme.error),
            ),
          ),
        ],
      ],
    );
  }
}
