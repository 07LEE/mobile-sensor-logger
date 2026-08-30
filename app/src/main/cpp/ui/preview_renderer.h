#ifndef SENSOR_LOGGER_PREVIEW_RENDERER_H
#define SENSOR_LOGGER_PREVIEW_RENDERER_H

#include <GLES3/gl3.h>

#include <cstdint>
#include <string>
#include <vector>

#include "camera_image.h"
#include "pro_panel.h"
#include "session_item.h"
#include "sessions_overlay.h"
#include "ui_button.h"

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
  // degrees and letterboxed inside the `height_fraction` of the screen starting
  // at `top_fraction`.
  void DrawCamera(int32_t sensor_orientation, float height_fraction,
                  float top_fraction = 0.0f);


  // Where the picture ended up on screen, in pixels from the top left, as of
  // the last DrawCamera. What a touch means depends on what is under it.
  bool CameraRectContains(float x, float y) const;

  // Draws `lines` starting at `top_fraction` down the screen, sized so that
  // `columns` glyphs span its width — fewer columns, larger text. Plus a filled
  // marker that is red while recording and grey otherwise.
  // Returns where it ended, as a fraction of the screen height, so whatever
  // goes under it does not have to guess.
  float DrawStatus(const std::vector<std::string>& lines, bool recording,
                   float top_fraction, int columns);

  // How tall that block will be, for working out where to start it.
  float StatusHeightFraction(int columns, int rows) const;

  // Draws a labelled button at `top_fraction` down the screen and remembers
  // where it landed. `enabled` greys it out.
  void DrawLensButton(const std::string& label, float top_fraction, bool enabled);
  bool LensButtonContains(float x, float y) const;

  void DrawSessionsButton(const std::string& label, float top_fraction, bool enabled);
  bool SessionsButtonContains(float x, float y) const;

  void DrawRetentionButton(const std::string& label, float top_fraction, bool enabled);
  bool RetentionButtonContains(float x, float y) const;

  void DrawLockButton(const std::string& label, float top_fraction, bool enabled,
                      bool pinned);
  bool LockButtonContains(float x, float y) const;

  void DrawProButton(const std::string& label, float top_fraction, bool enabled);
  bool ProButtonContains(float x, float y) const;

  // The panel opened by the PRO button: shutter, fps, mains, shift and
  // residual, each a row that cycles its own value on tap. See ProPanel for
  // why shift/residual step through presets rather than a free value.
  void DrawProPanel(const std::string& shutter_label,
                    const std::string& fps_label,
                    const std::string& mains_label,
                    const std::string& shift_label,
                    const std::string& residual_label,
                    const std::string& calibration_label,
                    bool has_calibration);
  bool ProPanelCloseContains(float x, float y) const;
  bool ProPanelShutterContains(float x, float y) const;
  bool ProPanelFpsContains(float x, float y) const;
  bool ProPanelMainsContains(float x, float y) const;
  bool ProPanelShiftContains(float x, float y) const;
  bool ProPanelResidualContains(float x, float y) const;
  bool ProPanelExtrinsicContains(float x, float y) const;
  bool ProPanelResetContains(float x, float y) const;

  // Render sessions overlay dialog with individual delete buttons and confirm
  // state. `*page` is 0-based and clamped in place; how many sessions fit on
  // one is computed from the viewport (see SessionsOverlay::Draw).
  void DrawSessionsOverlay(const std::vector<SessionItem>& sessions,
                           int pending_delete_index, int* page);
  bool CloseOverlayContains(float x, float y) const;
  bool PrevPageOverlayContains(float x, float y) const;
  bool NextPageOverlayContains(float x, float y) const;
  int ItemDeleteOverlayTouched(float x, float y) const;

 private:
  void RasterizeText(const std::vector<std::string>& lines, int columns);

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

  UiButton lens_button_;
  UiButton sessions_button_;
  UiButton retention_button_;
  UiButton lock_button_;
  UiButton pro_button_;
  SessionsOverlay sessions_overlay_;
  ProPanel pro_panel_;

  std::vector<uint8_t> chroma_pixels_;

  int viewport_width_ = 0;
  int viewport_height_ = 0;

  std::vector<uint8_t> text_pixels_;
};

}  // namespace sensor_logger

#endif  // SENSOR_LOGGER_PREVIEW_RENDERER_H
