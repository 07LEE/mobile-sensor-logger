import 'dart:async';
import 'dart:io';

import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../core/constants.dart';
import '../../../providers/engine_providers.dart';
import '../../battery/battery_sample.dart';
import '../../camera/camera_frame.dart';
import '../../gps/gps_sample.dart';
import '../../imu/imu_sample.dart';
import '../../storage/csv_writer.dart';
import '../../storage/session_manifest.dart';
import '../models/session.dart';
import '../models/session_status.dart';

/// Starts and stops every sensor engine as one unit and fans their streams
/// into the per-session CSV files.
class SessionController extends Notifier<Session?> {
  final List<StreamSubscription<Object>> _subscriptions = [];
  final List<CsvWriter> _writers = [];

  Timer? _flushTimer;
  DateTime _lastFrameSavedAt = DateTime.fromMillisecondsSinceEpoch(0);
  int _frameSeq = 0;

  @override
  Session? build() => null;

  Future<void> startSession() async {
    if (state?.status == SessionStatus.recording) return;

    final startTime = DateTime.now();
    final sessionId = _formatSessionId(startTime);

    final storageManager = ref.read(storageManagerProvider);
    final sessionDir = await storageManager.createSessionDirectory(sessionId);
    final dirPath = sessionDir.path;

    await storageManager.writeManifest(
      dirPath,
      SessionManifest(
        sessionId: sessionId,
        startTime: startTime,
        imuSampleRateHz: SensorConstants.imuSampleRateHz,
      ),
    );

    final imuWriter = await _openWriter(
      '$dirPath/imu.csv',
      ImuSample.csvHeader,
    );
    final gpsWriter = await _openWriter(
      '$dirPath/gps.csv',
      GpsSample.csvHeader,
    );
    final batteryWriter = await _openWriter(
      '$dirPath/battery.csv',
      BatterySample.csvHeader,
    );
    final frameWriter = await _openWriter(
      '$dirPath/frames/frame_index.csv',
      CameraFrame.csvHeader,
    );

    final imuEngine = ref.read(imuEngineProvider);
    final gpsEngine = ref.read(gpsEngineProvider);
    final batteryEngine = ref.read(batteryEngineProvider);
    final cameraEngine = ref.read(cameraEngineProvider);
    final jpegWriter = ref.read(frameWriterProvider);

    _subscriptions.addAll([
      imuEngine.stream.listen((s) => imuWriter.writeRow(s.toCsvRow())),
      gpsEngine.stream.listen((s) => gpsWriter.writeRow(s.toCsvRow())),
      batteryEngine.stream.listen((s) => batteryWriter.writeRow(s.toCsvRow())),
      cameraEngine.frameStream.listen((image) async {
        final now = DateTime.now();
        if (now.difference(_lastFrameSavedAt) <
            SensorConstants.frameSaveInterval) {
          return;
        }
        _lastFrameSavedAt = now;

        final frame = await jpegWriter.writeFrame(
          image: image,
          sessionDirPath: dirPath,
          frameSeq: _frameSeq++,
        );
        frameWriter.writeRow(frame.toCsvRow());
      }),
    ]);

    await Future.wait([
      imuEngine.start(),
      gpsEngine.start(),
      batteryEngine.start(),
      cameraEngine.start(),
    ]);

    _flushTimer = Timer.periodic(
      SensorConstants.csvFlushInterval,
      (_) => _flushAll(),
    );

    state = Session(
      id: sessionId,
      startTime: startTime,
      directoryPath: dirPath,
      status: SessionStatus.recording,
    );
  }

  Future<void> stopSession() async {
    final session = state;
    if (session?.status != SessionStatus.recording) return;

    _flushTimer?.cancel();
    _flushTimer = null;

    await Future.wait([
      ref.read(imuEngineProvider).stop(),
      ref.read(gpsEngineProvider).stop(),
      ref.read(batteryEngineProvider).stop(),
      ref.read(cameraEngineProvider).stop(),
    ]);

    for (final subscription in _subscriptions) {
      await subscription.cancel();
    }
    _subscriptions.clear();

    for (final writer in _writers) {
      await writer.close();
    }
    _writers.clear();

    final endTime = DateTime.now();
    await ref.read(storageManagerProvider).writeManifest(
          session!.directoryPath,
          SessionManifest(
            sessionId: session.id,
            startTime: session.startTime,
            endTime: endTime,
            imuSampleRateHz: SensorConstants.imuSampleRateHz,
          ),
        );

    _frameSeq = 0;
    _lastFrameSavedAt = DateTime.fromMillisecondsSinceEpoch(0);

    state = session.copyWith(
      status: SessionStatus.stopped,
      endTime: endTime,
    );
  }

  Future<CsvWriter> _openWriter(String path, List<String> header) async {
    final writer = CsvWriter(File(path));
    await writer.open(header);
    _writers.add(writer);
    return writer;
  }

  Future<void> _flushAll() async {
    for (final writer in _writers) {
      await writer.flush();
    }
  }

  String _formatSessionId(DateTime time) {
    String two(int value) => value.toString().padLeft(2, '0');
    return 'session_${time.year}${two(time.month)}${two(time.day)}'
        '_${two(time.hour)}${two(time.minute)}${two(time.second)}';
  }
}

final sessionControllerProvider = NotifierProvider<SessionController, Session?>(
  SessionController.new,
);
