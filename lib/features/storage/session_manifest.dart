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

  factory SessionManifest.fromJson(Map<String, dynamic> json) {
    final endTime = json['end_time'] as String?;

    return SessionManifest(
      sessionId: json['session_id'] as String,
      startTime: DateTime.parse(json['start_time'] as String),
      endTime: endTime == null ? null : DateTime.parse(endTime),
      imuSampleRateHz:
          (json['sensor_config'] as Map<String, dynamic>?)?['imu_sample_rate_hz']
                  as int? ??
              0,
    );
  }

  /// Null while a session is still recording, or if it ended abnormally.
  Duration? get duration => endTime?.difference(startTime);

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
