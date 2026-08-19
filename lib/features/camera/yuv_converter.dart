import 'package:camera/camera.dart';
import 'package:flutter/foundation.dart';
import 'package:image/image.dart' as img;

/// Plane layout of a YUV420 frame, normalised across platforms.
///
/// Android delivers three planes (separate U and V), while iOS delivers NV12:
/// two planes, with U and V interleaved in the second one. Both are expressed
/// here as a U source and a V source plus their starting offsets, so the
/// decoder does not need to branch on platform. For NV12 both sources are the
/// same buffer and the offsets are 0 and 1.
///
/// This is also a plain-data class so it can cross an isolate boundary, which
/// [CameraImage] itself cannot.
@immutable
class YuvFrame {
  final Uint8List yPlane;
  final Uint8List uPlane;
  final Uint8List vPlane;
  final int uOffset;
  final int vOffset;
  final int width;
  final int height;
  final int yRowStride;
  final int uvRowStride;
  final int uvPixelStride;

  const YuvFrame({
    required this.yPlane,
    required this.uPlane,
    required this.vPlane,
    required this.width,
    required this.height,
    required this.yRowStride,
    required this.uvRowStride,
    required this.uvPixelStride,
    this.uOffset = 0,
    this.vOffset = 0,
  });

  factory YuvFrame.fromCameraImage(CameraImage image) {
    final planes = image.planes;

    return switch (planes.length) {
      // iOS NV12: Y plane plus one interleaved UV plane.
      2 => YuvFrame(
          yPlane: Uint8List.fromList(planes[0].bytes),
          uPlane: Uint8List.fromList(planes[1].bytes),
          vPlane: Uint8List.fromList(planes[1].bytes),
          uOffset: 0,
          vOffset: 1,
          width: image.width,
          height: image.height,
          yRowStride: planes[0].bytesPerRow,
          uvRowStride: planes[1].bytesPerRow,
          uvPixelStride: planes[1].bytesPerPixel ?? 2,
        ),
      // Android: separate U and V planes, either planar or semi-planar.
      3 => YuvFrame(
          yPlane: Uint8List.fromList(planes[0].bytes),
          uPlane: Uint8List.fromList(planes[1].bytes),
          vPlane: Uint8List.fromList(planes[2].bytes),
          width: image.width,
          height: image.height,
          yRowStride: planes[0].bytesPerRow,
          uvRowStride: planes[1].bytesPerRow,
          uvPixelStride: planes[1].bytesPerPixel ?? 1,
        ),
      _ => throw UnsupportedError(
          'Unsupported YUV plane count: ${planes.length} '
          '(format ${image.format.group})',
        ),
    };
  }
}

/// Converts [frame] to RGB and encodes it as JPEG.
///
/// Intended to run on a background isolate; at logging frame rates this is far
/// too slow for the UI isolate.
Uint8List encodeYuvToJpeg(YuvFrame frame, {int quality = 85}) {
  final rgb = img.Image(width: frame.width, height: frame.height);

  for (var y = 0; y < frame.height; y++) {
    final yRow = y * frame.yRowStride;
    final uvRow = (y ~/ 2) * frame.uvRowStride;

    for (var x = 0; x < frame.width; x++) {
      final uvColumn = (x ~/ 2) * frame.uvPixelStride;

      final yValue = frame.yPlane[yRow + x];
      final uValue = frame.uPlane[uvRow + uvColumn + frame.uOffset] - 128;
      final vValue = frame.vPlane[uvRow + uvColumn + frame.vOffset] - 128;

      final r = (yValue + 1.402 * vValue).round().clamp(0, 255);
      final g = (yValue - 0.344136 * uValue - 0.714136 * vValue)
          .round()
          .clamp(0, 255);
      final b = (yValue + 1.772 * uValue).round().clamp(0, 255);

      rgb.setPixelRgb(x, y, r, g, b);
    }
  }

  return img.encodeJpg(rgb, quality: quality);
}
