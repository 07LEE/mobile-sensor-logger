import 'package:flutter_test/flutter_test.dart';
import 'package:mobile_sensor_logger/core/axis_converter.dart';

void main() {
  group('AxisConverter.phoneToRos', () {
    test('maps phone axes onto REP-103', () {
      final result = AxisConverter.phoneToRos(const Vector3(1, 2, 3));

      expect(result.x, 2);
      expect(result.y, -1);
      expect(result.z, 3);
    });

    test('phone forward (+Y) becomes ROS forward (+X)', () {
      final result = AxisConverter.phoneToRos(const Vector3(0, 1, 0));

      expect(result.x, 1);
      expect(result.y, 0);
      expect(result.z, 0);
    });

    test('phone right (+X) becomes ROS right (-Y)', () {
      final result = AxisConverter.phoneToRos(const Vector3(1, 0, 0));

      expect(result.x, 0);
      expect(result.y, -1);
      expect(result.z, 0);
    });
  });
}
