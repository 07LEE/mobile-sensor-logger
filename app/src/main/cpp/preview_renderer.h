#ifndef SENSOR_LOGGER_PREVIEW_RENDERER_H
#define SENSOR_LOGGER_PREVIEW_RENDERER_H

#include <GLES3/gl3.h>

#include <cstdint>
#include <string>
#include <vector>

namespace sensor_logger {

// Draws the camera image and a status readout onto the screen.
//
// Without this the screen is black. ARCore renders the camera into an external
// texture whether or not anything samples it, so a capture could be framed at
// the ceiling, or not recording at all, and nothing on the device would say so
// — the only way to know was to read logcat from a workstation, which is not
// available while actually walking around filming.
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

  // Draws the camera texture over the whole screen. `uvs` are the eight texture
  // coordinates ARCore produced for the screen corners.
  void DrawCamera(uint32_t camera_texture, const float* uvs);

  // Draws `lines` at the top of the screen over a dark panel, plus a filled
  // marker that is red while recording and grey otherwise.
  void DrawStatus(const std::vector<std::string>& lines, bool recording);

 private:
  void DrawQuad(GLuint program, float x0, float y0, float x1, float y1,
                const float* uvs);
  void RasterizeText(const std::vector<std::string>& lines);

  GLuint camera_program_ = 0;
  GLuint quad_program_ = 0;
  GLuint vbo_ = 0;
  GLuint text_texture_ = 0;
  GLuint white_texture_ = 0;

  GLint quad_color_location_ = -1;

  int viewport_width_ = 0;
  int viewport_height_ = 0;

  std::vector<uint8_t> text_pixels_;
};

}  // namespace sensor_logger

#endif  // SENSOR_LOGGER_PREVIEW_RENDERER_H
