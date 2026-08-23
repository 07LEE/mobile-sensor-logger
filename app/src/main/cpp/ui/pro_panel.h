#ifndef SENSOR_LOGGER_PRO_PANEL_H
#define SENSOR_LOGGER_PRO_PANEL_H

#include <GLES3/gl3.h>

#include <functional>
#include <string>
#include <vector>

namespace sensor_logger {

// Overlay for the capture.conf values that have no toolbar button of their
// own: the shutter cap, the fps pin, the mains frequency, and the shift/
// residual motion thresholds. Unlike the other three, shift and residual are
// continuous rather than a small natural set of choices, so their rows step
// through a fixed list of presets around the default rather than covering
// the range — a coarser control than the file allows, in exchange for one
// that fits the same tap-to-cycle pattern as everything else here.
//
// Each row cycles its setting through a fixed list on tap, the same way the
// Retention and Lens buttons already do. What sets a row here apart is that
// the change is also written straight back to capture.conf, so it survives
// the app being killed and restarted instead of reverting to whatever was in
// the file when this session opened — see CaptureConfig::Save.
class ProPanel {
 public:
  ProPanel() = default;

  void Draw(GLuint quad_program, GLuint white_texture, GLuint text_texture,
            GLint quad_color_location, GLuint vbo,
            const std::string& shutter_label, const std::string& fps_label,
            const std::string& mains_label, const std::string& shift_label,
            const std::string& residual_label, int viewport_width,
            int viewport_height,
            const std::function<void(const std::vector<std::string>&, int)>&
                rasterize_text_fn);

  bool CloseTouched(float x, float y) const;
  bool ShutterTouched(float x, float y) const;
  bool FpsTouched(float x, float y) const;
  bool MainsTouched(float x, float y) const;
  bool ShiftTouched(float x, float y) const;
  bool ResidualTouched(float x, float y) const;
  bool ResetTouched(float x, float y) const;

 private:
  struct Rect {
    float left = 0.0f;
    float right = 0.0f;
    float top = 0.0f;
    float bottom = 0.0f;
  };

  static bool Contains(const Rect& r, float x, float y);

  Rect close_;
  Rect shutter_;
  Rect fps_;
  Rect mains_;
  Rect shift_;
  Rect residual_;
  Rect reset_;
};

}  // namespace sensor_logger

#endif  // SENSOR_LOGGER_PRO_PANEL_H
