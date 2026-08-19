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
///
/// State is an [AsyncValue] so the UI can distinguish "starting up" from
/// "recording", and so a failure to start — a denied permission being the
/// common one — surfaces instead of being swallowed.
class SessionController extends AsyncNotifier<Session?> {
  final List<StreamSubscription<Object>> _subscriptions = [];
  final List<CsvWriter> _writers = [];

  Timer? _flushTimer;
  DateTime _lastFrameSavedAt = DateTime.fromMillisecondsSinceEpoch(0);
  int _frameSeq = 0;

  // Returned synchronously so the app opens in the idle state rather than
  // flashing a loading indicator before any session exists.
  @override
  FutureOr<Session?> build() => null;

  Future<void> startSession() async {
    if (state.value?.status == SessionStatus.recording) return;

    state = const AsyncValue.loading();

    try {
      final session = await _openSession();
      state = AsyncValue.data(session);
    } catch (error, stackTrace) {
      // Engines may have started before the failing one threw; leaving them
      // running would keep the sensors powered with nothing consuming them.
      await _teardown();
      state = AsyncValue.error(error, stackTrace);
    }
  }

  Future<Session> _openSession() async {
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

    final imuWriter = await _openWriter('$dirPath/imu.csv', ImuSample.csvHeader);
    final gpsWriter = await _openWriter('$dirPath/gps.csv', GpsSample.csvHeader);
    final batteryWriter = await _openWriter(
      '$dirPath/battery.csv',
      BatterySample.csvHeader,
    );
    final frameIndexWriter = await _openWriter(
      '$dirPath/frames/frame_index.csv',
      CameraFrame.csvHeader,
    );

    final imuEngine = ref.read(imuEngineProvider);
    final gpsEngine = ref.read(gpsEngineProvider);
    final batteryEngine = ref.read(batteryEngineProvider);
    final cameraEngine = ref.read(cameraEngineProvider);
    final frameWriter = ref.read(frameWriterProvider);

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

        final frame = await frameWriter.writeFrame(
          image: image,
          sessionDirPath: dirPath,
          frameSeq: _frameSeq++,
        );
        frameIndexWriter.writeRow(frame.toCsvRow());
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

    return Session(
      id: sessionId,
      startTime: startTime,
      directoryPath: dirPath,
      status: SessionStatus.recording,
    );
  }

  Future<void> stopSession() async {
    final session = state.value;
    if (session?.status != SessionStatus.recording) return;

    await _teardown();

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

    state = AsyncValue.data(
      session.copyWith(status: SessionStatus.stopped, endTime: endTime),
    );
  }

  /// Releases everything [_openSession] acquired. Safe to call when only part
  /// of the startup sequence completed, so it doubles as failure cleanup.
  Future<void> _teardown() async {
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

    _frameSeq = 0;
    _lastFrameSavedAt = DateTime.fromMillisecondsSinceEpoch(0);
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

final sessionControllerProvider =
    AsyncNotifierProvider<SessionController, Session?>(SessionController.new);
