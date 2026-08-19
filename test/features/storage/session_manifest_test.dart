import 'package:flutter_test/flutter_test.dart';
import 'package:mobile_sensor_logger/features/storage/session_manifest.dart';

void main() {
  final start = DateTime.utc(2026, 8, 20, 14, 30);
  final end = DateTime.utc(2026, 8, 20, 14, 45, 30);

  group('SessionManifest', () {
    test('survives a JSON round trip', () {
      final original = SessionManifest(
        sessionId: 'session_20260820_143000',
        startTime: start,
        endTime: end,
        imuSampleRateHz: 100,
      );

      final restored = SessionManifest.fromJson(original.toJson());

      expect(restored.sessionId, original.sessionId);
      expect(restored.startTime, original.startTime);
      expect(restored.endTime, original.endTime);
      expect(restored.imuSampleRateHz, original.imuSampleRateHz);
    });

    test('records the ROS unit and axis convention', () {
      final json = SessionManifest(
        sessionId: 'session_20260820_143000',
        startTime: start,
        imuSampleRateHz: 100,
      ).toJson();

      expect(json['convention'], {
        'axis': 'REP-103',
        'angular_velocity_unit': 'rad/s',
        'linear_acceleration_unit': 'm/s^2',
      });
    });

    test('reads back a manifest that has no end time', () {
      final restored = SessionManifest.fromJson(
        SessionManifest(
          sessionId: 'session_20260820_143000',
          startTime: start,
          imuSampleRateHz: 100,
        ).toJson(),
      );

      expect(restored.endTime, isNull);
      expect(restored.duration, isNull);
    });

    test('reports duration once the session has ended', () {
      final manifest = SessionManifest(
        sessionId: 'session_20260820_143000',
        startTime: start,
        endTime: end,
        imuSampleRateHz: 100,
      );

      expect(manifest.duration, const Duration(minutes: 15, seconds: 30));
    });
  });
}
