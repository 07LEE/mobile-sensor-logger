class ImuSample {
  final int timestampUs;
  final double angularVelocityX;
  final double angularVelocityY;
  final double angularVelocityZ;
  final double linearAccelerationX;
  final double linearAccelerationY;
  final double linearAccelerationZ;

  const ImuSample({
    required this.timestampUs,
    required this.angularVelocityX,
    required this.angularVelocityY,
    required this.angularVelocityZ,
    required this.linearAccelerationX,
    required this.linearAccelerationY,
    required this.linearAccelerationZ,
  });

  static const csvHeader = [
    'timestamp_us',
    'ang_vel_x',
    'ang_vel_y',
    'ang_vel_z',
    'lin_acc_x',
    'lin_acc_y',
    'lin_acc_z',
  ];

  List<Object> toCsvRow() => [
    timestampUs,
    angularVelocityX,
    angularVelocityY,
    angularVelocityZ,
    linearAccelerationX,
    linearAccelerationY,
    linearAccelerationZ,
  ];
}
