enum GpsFixStatus { fix, noFix }

class GpsSample {
  final int timestampUs;
  final double latitude;
  final double longitude;
  final double altitude;
  final double horizontalAccuracy;
  final double verticalAccuracy;
  final GpsFixStatus status;

  const GpsSample({
    required this.timestampUs,
    required this.latitude,
    required this.longitude,
    required this.altitude,
    required this.horizontalAccuracy,
    required this.verticalAccuracy,
    required this.status,
  });

  static const csvHeader = [
    'timestamp_us',
    'latitude',
    'longitude',
    'altitude',
    'horizontal_accuracy',
    'vertical_accuracy',
    'status',
  ];

  List<Object> toCsvRow() => [
    timestampUs,
    latitude,
    longitude,
    altitude,
    horizontalAccuracy,
    verticalAccuracy,
    status.name,
  ];
}
