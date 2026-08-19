class CameraFrame {
  final int timestampUs;
  final int frameSeq;
  final String filename;
  final String format;
  final int? exposureTimeUs;
  final int? iso;

  const CameraFrame({
    required this.timestampUs,
    required this.frameSeq,
    required this.filename,
    this.format = 'jpeg',
    this.exposureTimeUs,
    this.iso,
  });

  static const csvHeader = [
    'timestamp_us',
    'frame_seq',
    'filename',
    'format',
    'exposure_time_us',
    'iso',
  ];

  List<Object?> toCsvRow() => [
    timestampUs,
    frameSeq,
    filename,
    format,
    exposureTimeUs,
    iso,
  ];
}
