class Vector3 {
  final double x;
  final double y;
  final double z;

  const Vector3(this.x, this.y, this.z);
}

class AxisConverter {
  const AxisConverter._();

  /// Converts a phone-frame vector (X: right, Y: up, Z: out of screen) to
  /// ROS REP-103 (X: forward, Y: left, Z: up), matching web-ros-collector's
  /// imu_bridge.py convention.
  static Vector3 phoneToRos(Vector3 phone) {
    return Vector3(phone.y, -phone.x, phone.z);
  }
}
