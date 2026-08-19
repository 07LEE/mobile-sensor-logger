import 'dart:async';

import 'package:battery_plus/battery_plus.dart';

import '../../core/sensor_engine.dart';
import 'battery_sample.dart';

/// Publishes battery state shaped like sensor_msgs/BatteryState.
///
/// Battery level changes slowly, so it is polled on a timer rather than
/// streamed; percentage is normalised to 0.0-1.0 as battery_bridge.py expects.
class BatteryEngine implements SensorEngine<BatterySample> {
  static const _pollInterval = Duration(seconds: 5);

  final Battery _battery = Battery();
  final _controller = StreamController<BatterySample>.broadcast();

  Timer? _timer;
  bool _isRunning = false;

  @override
  Stream<BatterySample> get stream => _controller.stream;

  @override
  Future<void> start() async {
    if (_isRunning) return;
    _isRunning = true;

    await _emitSample();
    _timer = Timer.periodic(_pollInterval, (_) => _emitSample());
  }

  Future<void> _emitSample() async {
    if (!_isRunning) return;

    final level = await _battery.batteryLevel;
    final state = await _battery.batteryState;

    _controller.add(
      BatterySample(
        timestampUs: DateTime.now().microsecondsSinceEpoch,
        percentage: level / 100.0,
        powerSupplyStatus: _toPowerSupplyStatus(state),
      ),
    );
  }

  PowerSupplyStatus _toPowerSupplyStatus(BatteryState state) {
    return switch (state) {
      BatteryState.charging || BatteryState.full => PowerSupplyStatus.charging,
      BatteryState.discharging => PowerSupplyStatus.discharging,
      _ => PowerSupplyStatus.unknown,
    };
  }

  @override
  Future<void> stop() async {
    if (!_isRunning) return;
    _isRunning = false;

    _timer?.cancel();
    _timer = null;
  }

  Future<void> dispose() async {
    await stop();
    await _controller.close();
  }
}
