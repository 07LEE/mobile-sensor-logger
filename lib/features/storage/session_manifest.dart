class SessionManifest {
  final String sessionId;
  final DateTime startTime;
  final DateTime? endTime;
  final int imuSampleRateHz;

  const SessionManifest({
    required this.sessionId,
    required this.startTime,
    this.endTime,
    required this.imuSampleRateHz,
  });

  Map<String, dynamic> toJson() => {
    'session_id': sessionId,
    'start_time': startTime.toIso8601String(),
    'end_time': endTime?.toIso8601String(),
    'sensor_config': {'imu_sample_rate_hz': imuSampleRateHz},
    'convention': {
      'axis': 'REP-103',
      'angular_velocity_unit': 'rad/s',
      'linear_acceleration_unit': 'm/s^2',
    },
  };
}
