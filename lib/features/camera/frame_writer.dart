import 'dart:io';

import 'package:camera/camera.dart';
import 'package:flutter/foundation.dart';

import 'camera_frame.dart';
import 'yuv_converter.dart';

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
    final frame = YuvFrame.fromCameraImage(image);
    final jpegBytes = await compute(encodeYuvToJpeg, frame);

    final filename = '${frameSeq.toString().padLeft(6, '0')}_$timestampUs.jpg';
    final file = File('$sessionDirPath/frames/$filename');
    await file.writeAsBytes(jpegBytes);

    return CameraFrame(
      timestampUs: timestampUs,
      frameSeq: frameSeq,
      filename: filename,
    );
  }
}
