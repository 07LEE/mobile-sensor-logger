import 'dart:async';

import 'package:geolocator/geolocator.dart';

import '../../core/sensor_engine.dart';
import 'gps_sample.dart';

/// Publishes GPS fixes shaped like sensor_msgs/NavSatFix.
///
/// Accuracy values are kept in their raw metre form so a downstream consumer
/// can derive position_covariance as accuracy^2, matching gps_bridge.py.
class GpsEngine implements SensorEngine<GpsSample> {
  final _controller = StreamController<GpsSample>.broadcast();

  StreamSubscription<Position>? _subscription;
  bool _isRunning = false;

  @override
  Stream<GpsSample> get stream => _controller.stream;

  @override
  Future<void> start() async {
    if (_isRunning) return;

    if (!await _ensurePermission()) {
      throw StateError('Location permission denied');
    }
    _isRunning = true;

    _subscription = Geolocator.getPositionStream(
      locationSettings: const LocationSettings(
        accuracy: LocationAccuracy.best,
      ),
    ).listen((position) {
      _controller.add(
        GpsSample(
          timestampUs: position.timestamp.microsecondsSinceEpoch,
          latitude: position.latitude,
          longitude: position.longitude,
          altitude: position.altitude,
          horizontalAccuracy: position.accuracy,
          verticalAccuracy: position.altitudeAccuracy,
          status: position.accuracy > 0
              ? GpsFixStatus.fix
              : GpsFixStatus.noFix,
        ),
      );
    });
  }

  Future<bool> _ensurePermission() async {
    if (!await Geolocator.isLocationServiceEnabled()) return false;

    var permission = await Geolocator.checkPermission();
    if (permission == LocationPermission.denied) {
      permission = await Geolocator.requestPermission();
    }
    return permission == LocationPermission.always ||
        permission == LocationPermission.whileInUse;
  }

  @override
  Future<void> stop() async {
    if (!_isRunning) return;
    _isRunning = false;

    await _subscription?.cancel();
    _subscription = null;
  }

  Future<void> dispose() async {
    await stop();
    await _controller.close();
  }
}
