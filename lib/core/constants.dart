class SensorConstants {
  const SensorConstants._();

  static const imuSampleRateHz = 100;
  static const csvFlushInterval = Duration(milliseconds: 500);
  static const frameSaveInterval = Duration(milliseconds: 500);
}

class RosFrameIds {
  const RosFrameIds._();

  static const imu = 'phone_imu';
  static const gps = 'gps_link';
  static const battery = 'phone_link';
  static const camera = 'phone_camera';
}
