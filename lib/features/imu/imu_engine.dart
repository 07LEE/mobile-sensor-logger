import 'dart:async';

import 'package:sensors_plus/sensors_plus.dart';

import '../../core/axis_converter.dart';
import '../../core/constants.dart';
import '../../core/sensor_engine.dart';
import 'imu_sample.dart';

/// Publishes fused accelerometer + gyroscope samples in ROS REP-103 axes.
///
/// sensors_plus already reports m/s^2 and rad/s, so only the phone -> REP-103
/// axis remap is applied here. Gyroscope readings are held and paired with the
/// next accelerometer event, which drives the output rate.
class ImuEngine implements SensorEngine<ImuSample> {
  final _controller = StreamController<ImuSample>.broadcast();

  StreamSubscription<AccelerometerEvent>? _accelSubscription;
  StreamSubscription<GyroscopeEvent>? _gyroSubscription;

  Vector3 _latestGyro = const Vector3(0, 0, 0);
  bool _isRunning = false;

  @override
  Stream<ImuSample> get stream => _controller.stream;

  @override
  Future<void> start() async {
    if (_isRunning) return;
    _isRunning = true;

    final samplingPeriod = Duration(
      microseconds: Duration.microsecondsPerSecond ~/
          SensorConstants.imuSampleRateHz,
    );

    _gyroSubscription = gyroscopeEventStream(samplingPeriod: samplingPeriod)
        .listen((event) {
      _latestGyro = AxisConverter.phoneToRos(
        Vector3(event.x, event.y, event.z),
      );
    });

    _accelSubscription =
        accelerometerEventStream(samplingPeriod: samplingPeriod)
            .listen((event) {
      final accel = AxisConverter.phoneToRos(
        Vector3(event.x, event.y, event.z),
      );
      final gyro = _latestGyro;

      _controller.add(
        ImuSample(
          timestampUs: DateTime.now().microsecondsSinceEpoch,
          angularVelocityX: gyro.x,
          angularVelocityY: gyro.y,
          angularVelocityZ: gyro.z,
          linearAccelerationX: accel.x,
          linearAccelerationY: accel.y,
          linearAccelerationZ: accel.z,
        ),
      );
    });
  }

  @override
  Future<void> stop() async {
    if (!_isRunning) return;
    _isRunning = false;

    await _accelSubscription?.cancel();
    await _gyroSubscription?.cancel();
    _accelSubscription = null;
    _gyroSubscription = null;
  }

  Future<void> dispose() async {
    await stop();
    await _controller.close();
  }
}
