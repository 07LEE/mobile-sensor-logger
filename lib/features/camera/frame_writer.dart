import 'dart:io';

import 'package:camera/camera.dart';
import 'package:flutter/foundation.dart';
import 'package:image/image.dart' as img;

import 'camera_frame.dart';

/// Plain-data snapshot of a CameraImage, safe to hand to a background isolate.
///
/// CameraImage itself wraps platform handles and cannot cross an isolate
/// boundary, so the planes are copied out on the caller's side first.
@immutable
class _YuvFrame {
  final Uint8List yPlane;
  final Uint8List uPlane;
  final Uint8List vPlane;
  final int width;
  final int height;
  final int yRowStride;
  final int uvRowStride;
  final int uvPixelStride;

  const _YuvFrame({
    required this.yPlane,
    required this.uPlane,
    required this.vPlane,
    required this.width,
    required this.height,
    required this.yRowStride,
    required this.uvRowStride,
    required this.uvPixelStride,
  });
}

/// Encodes preview frames to JPEG and writes them into the session directory.
///
/// Encoding runs on a background isolate via [compute] because doing it on the
/// UI isolate stalls the preview at the rates this logger runs at.
class FrameWriter {
  Future<CameraFrame> writeFrame({
    required CameraImage image,
    required String sessionDirPath,
    required int frameSeq,
  }) async {
    final timestampUs = DateTime.now().microsecondsSinceEpoch;
    final jpegBytes = await compute(_encodeYuvToJpeg, _toYuvFrame(image));

    final filename =
        '${frameSeq.toString().padLeft(6, '0')}_$timestampUs.jpg';
    final file = File('$sessionDirPath/frames/$filename');
    await file.writeAsBytes(jpegBytes);

    return CameraFrame(
      timestampUs: timestampUs,
      frameSeq: frameSeq,
      filename: filename,
    );
  }

  _YuvFrame _toYuvFrame(CameraImage image) {
    return _YuvFrame(
      yPlane: Uint8List.fromList(image.planes[0].bytes),
      uPlane: Uint8List.fromList(image.planes[1].bytes),
      vPlane: Uint8List.fromList(image.planes[2].bytes),
      width: image.width,
      height: image.height,
      yRowStride: image.planes[0].bytesPerRow,
      uvRowStride: image.planes[1].bytesPerRow,
      uvPixelStride: image.planes[1].bytesPerPixel ?? 1,
    );
  }
}

Uint8List _encodeYuvToJpeg(_YuvFrame frame) {
  final rgb = img.Image(width: frame.width, height: frame.height);

  for (var y = 0; y < frame.height; y++) {
    final uvRow = (y ~/ 2) * frame.uvRowStride;
    final yRow = y * frame.yRowStride;

    for (var x = 0; x < frame.width; x++) {
      final uvIndex = uvRow + (x ~/ 2) * frame.uvPixelStride;

      final yValue = frame.yPlane[yRow + x];
      final uValue = frame.uPlane[uvIndex] - 128;
      final vValue = frame.vPlane[uvIndex] - 128;

      final r = (yValue + 1.402 * vValue).round().clamp(0, 255);
      final g = (yValue - 0.344136 * uValue - 0.714136 * vValue)
          .round()
          .clamp(0, 255);
      final b = (yValue + 1.772 * uValue).round().clamp(0, 255);

      rgb.setPixelRgb(x, y, r, g, b);
    }
  }

  return img.encodeJpg(rgb, quality: 85);
}
