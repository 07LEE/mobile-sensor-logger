import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../features/battery/battery_engine.dart';
import '../features/camera/camera_engine.dart';
import '../features/camera/frame_writer.dart';
import '../features/gps/gps_engine.dart';
import '../features/imu/imu_engine.dart';
import '../features/storage/session_exporter.dart';
import '../features/storage/storage_manager.dart';

final imuEngineProvider = Provider<ImuEngine>((ref) {
  final engine = ImuEngine();
  ref.onDispose(engine.dispose);
  return engine;
});

final gpsEngineProvider = Provider<GpsEngine>((ref) {
  final engine = GpsEngine();
  ref.onDispose(engine.dispose);
  return engine;
});

final batteryEngineProvider = Provider<BatteryEngine>((ref) {
  final engine = BatteryEngine();
  ref.onDispose(engine.dispose);
  return engine;
});

final cameraEngineProvider = Provider<CameraEngine>((ref) {
  final engine = CameraEngine();
  ref.onDispose(engine.dispose);
  return engine;
});

final frameWriterProvider = Provider<FrameWriter>((ref) => FrameWriter());

final storageManagerProvider = Provider<StorageManager>(
  (ref) => StorageManager(),
);

final sessionExporterProvider = Provider<SessionExporter>(
  (ref) => SessionExporter(),
);
