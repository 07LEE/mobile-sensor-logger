#ifndef SENSOR_LOGGER_PREVIEW_RENDERER_H
#define SENSOR_LOGGER_PREVIEW_RENDERER_H

#include <GLES3/gl3.h>

#include <cstdint>
#include <string>
#include <vector>

#include "camera_image.h"

namespace sensor_logger {

// Draws the camera image and a status readout onto the screen.
//
// Without this the screen is black, and a capture could be framed at the
// ceiling, or not recording at all, with nothing on the device saying so — the
// only way to know would be reading logcat from a workstation, which is not
// available while actually walking around filming.
//
// The camera hands over YUV planes, so the conversion happens here: luma and
// chroma go up as two textures and the shader combines them. The chroma is
// packed on the way because the planes arrive in whichever layout the device
// prefers, and a shader that had to handle each of them would be three
// shaders.
//
// The readout is drawn from a five-by-seven bitmap font rasterised on the CPU
// into a small single-channel texture. A real text stack would mean a font
// file, a shaper and a glyph cache to put six short lines of ASCII on screen,
// and the numbers that matter here are counters.
class PreviewRenderer {
 public:
  PreviewRenderer() = default;
  ~PreviewRenderer();

  PreviewRenderer(const PreviewRenderer&) = delete;
  PreviewRenderer& operator=(const PreviewRenderer&) = delete;

  bool Init();
  void Destroy();

  void SetViewport(int width, int height);

  // Uploads one camera frame. Cheap enough per frame at preview resolution;
  // this is not the capture stream.
  bool UploadCamera(const CameraImageView& image);

  // Draws the last uploaded frame, rotated upright by `sensor_orientation`
  // degrees and letterboxed inside the top `height_fraction` of the screen.
  // Pass 1 to fill it.
  void DrawCamera(int32_t sensor_orientation, float height_fraction);

  // Where the picture ended up on screen, in pixels from the top left, as of
  // the last DrawCamera. What a touch means depends on what is under it.
  bool CameraRectContains(float x, float y) const;

  // Draws `lines` starting at `top_fraction` down the screen, sized so that
  // `columns` glyphs span its width — fewer columns, larger text. Plus a filled
  // marker that is red while recording and grey otherwise.
  void DrawStatus(const std::vector<std::string>& lines, bool recording,
                  float top_fraction, int columns);

 private:
  void DrawQuad(GLuint program, float x0, float y0, float x1, float y1,
                const float* uvs);
  void RasterizeText(const std::vector<std::string>& lines);

  GLuint camera_program_ = 0;
  GLuint quad_program_ = 0;
  GLuint vbo_ = 0;
  GLuint luma_texture_ = 0;
  GLuint chroma_texture_ = 0;
  GLuint text_texture_ = 0;
  GLuint white_texture_ = 0;

  GLint quad_color_location_ = -1;

  int32_t camera_width_ = 0;
  int32_t camera_height_ = 0;
  bool camera_uploaded_ = false;
  bool swap_chroma_ = false;

  // Pixels from the top left, set by the last DrawCamera.
  float camera_left_ = 0.0f;
  float camera_top_ = 0.0f;
  float camera_right_ = 0.0f;
  float camera_bottom_ = 0.0f;
  std::vector<uint8_t> chroma_pixels_;

  int viewport_width_ = 0;
  int viewport_height_ = 0;

  std::vector<uint8_t> text_pixels_;
};

}  // namespace sensor_logger

#endif  // SENSOR_LOGGER_PREVIEW_RENDERER_H
