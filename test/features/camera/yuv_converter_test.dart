import 'package:flutter/foundation.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:image/image.dart' as img;
import 'package:mobile_sensor_logger/features/camera/yuv_converter.dart';

/// Builds a 2x2 planar frame (Android layout: separate U and V planes).
YuvFrame planarFrame({
  required List<int> luma,
  required int u,
  required int v,
}) {
  return YuvFrame(
    yPlane: Uint8List.fromList(luma),
    uPlane: Uint8List.fromList([u]),
    vPlane: Uint8List.fromList([v]),
    width: 2,
    height: 2,
    yRowStride: 2,
    uvRowStride: 1,
    uvPixelStride: 1,
  );
}

/// Builds the same 2x2 frame in NV12 (iOS layout: one interleaved UV plane).
YuvFrame nv12Frame({
  required List<int> luma,
  required int u,
  required int v,
}) {
  final interleaved = Uint8List.fromList([u, v]);

  return YuvFrame(
    yPlane: Uint8List.fromList(luma),
    uPlane: interleaved,
    vPlane: interleaved,
    uOffset: 0,
    vOffset: 1,
    width: 2,
    height: 2,
    yRowStride: 2,
    uvRowStride: 2,
    uvPixelStride: 2,
  );
}

void main() {
  const neutralChroma = 128;
  final grey = [130, 130, 130, 130];

  group('encodeYuvToJpeg', () {
    test('decodes a planar (Android) frame', () {
      final jpeg = encodeYuvToJpeg(
        planarFrame(luma: grey, u: neutralChroma, v: neutralChroma),
      );

      final decoded = img.decodeJpg(jpeg);
      expect(decoded, isNotNull);
      expect(decoded!.width, 2);
      expect(decoded.height, 2);
    });

    test('decodes an NV12 (iOS) frame without overrunning the UV plane', () {
      final jpeg = encodeYuvToJpeg(
        nv12Frame(luma: grey, u: neutralChroma, v: neutralChroma),
      );

      final decoded = img.decodeJpg(jpeg);
      expect(decoded, isNotNull);
      expect(decoded!.width, 2);
      expect(decoded.height, 2);
    });

    test('both layouts describing one frame produce identical pixels', () {
      final luma = [16, 90, 200, 255];
      const u = 90;
      const v = 200;

      final fromPlanar = img.decodeJpg(
        encodeYuvToJpeg(planarFrame(luma: luma, u: u, v: v), quality: 100),
      )!;
      final fromNv12 = img.decodeJpg(
        encodeYuvToJpeg(nv12Frame(luma: luma, u: u, v: v), quality: 100),
      )!;

      for (var y = 0; y < 2; y++) {
        for (var x = 0; x < 2; x++) {
          expect(
            fromNv12.getPixel(x, y),
            fromPlanar.getPixel(x, y),
            reason: 'pixel ($x, $y) differs between plane layouts',
          );
        }
      }
    });

    test('neutral chroma yields a grey image', () {
      final jpeg = encodeYuvToJpeg(
        planarFrame(luma: [130, 130, 130, 130], u: 128, v: 128),
        quality: 100,
      );

      final pixel = img.decodeJpg(jpeg)!.getPixel(0, 0);
      expect(pixel.r, closeTo(130, 3));
      expect(pixel.g, closeTo(130, 3));
      expect(pixel.b, closeTo(130, 3));
    });
  });
}
