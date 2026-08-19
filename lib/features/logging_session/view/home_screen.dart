import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../controller/session_controller.dart';
import '../models/session_status.dart';

class HomeScreen extends ConsumerWidget {
  const HomeScreen({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final session = ref.watch(sessionControllerProvider);
    final controller = ref.read(sessionControllerProvider.notifier);
    final isRecording = session?.status == SessionStatus.recording;

    return Scaffold(
      appBar: AppBar(title: const Text('Mobile Sensor Logger')),
      body: Center(
        child: Column(
          mainAxisAlignment: MainAxisAlignment.center,
          children: [
            Icon(
              isRecording ? Icons.sensors : Icons.sensors_off,
              size: 64,
              color: isRecording ? Colors.red : Colors.grey,
            ),
            const SizedBox(height: 16),
            Text(
              isRecording ? 'Recording' : 'Idle',
              style: Theme.of(context).textTheme.headlineSmall,
            ),
            if (session != null) ...[
              const SizedBox(height: 8),
              Text(session.id, style: Theme.of(context).textTheme.bodySmall),
            ],
          ],
        ),
      ),
      floatingActionButton: FloatingActionButton(
        onPressed: () {
          isRecording ? controller.stopSession() : controller.startSession();
        },
        child: Icon(isRecording ? Icons.stop : Icons.fiber_manual_record),
      ),
    );
  }
}
