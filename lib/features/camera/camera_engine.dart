import 'dart:async';

import 'package:camera/camera.dart';

/// Wraps CameraController's preview image stream.
///
/// Frames arrive at the preview rate (typically 30fps); throttling to the
/// configured save interval is the caller's concern, so this only exposes the
/// raw stream and the controller needed to render a preview.
class CameraEngine {
  final _controller = StreamController<CameraImage>.broadcast();

  CameraController? _cameraController;
  bool _isRunning = false;

  Stream<CameraImage> get frameStream => _controller.stream;

  CameraController? get cameraController => _cameraController;

  Future<void> start() async {
    if (_isRunning) return;

    final cameras = await availableCameras();
    if (cameras.isEmpty) {
      throw StateError('No camera available on this device');
    }

    final controller = CameraController(
      cameras.first,
      ResolutionPreset.medium,
      enableAudio: false,
    );
    await controller.initialize();
    _cameraController = controller;
    _isRunning = true;

    await controller.startImageStream(_controller.add);
  }

  Future<void> stop() async {
    if (!_isRunning) return;
    _isRunning = false;

    final controller = _cameraController;
    _cameraController = null;
    if (controller == null) return;

    if (controller.value.isStreamingImages) {
      await controller.stopImageStream();
    }
    await controller.dispose();
  }

  Future<void> dispose() async {
    await stop();
    await _controller.close();
  }
}
