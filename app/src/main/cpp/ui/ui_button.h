#ifndef SENSOR_LOGGER_UI_BUTTON_H
#define SENSOR_LOGGER_UI_BUTTON_H

#include <GLES3/gl3.h>

#include <functional>
#include <string>
#include <vector>

namespace sensor_logger {

class UiButton {
 public:
  UiButton() = default;

  void Draw(GLuint quad_program, GLuint white_texture, GLuint text_texture,
            GLint quad_color_location, GLuint vbo, const std::string& label,
            float top_fraction, float width_fraction, float height_fraction,
            float red, float green, float blue, float alpha, bool enabled,
            int viewport_width, int viewport_height,
            const std::function<void(const std::vector<std::string>&, int)>& rasterize_text_fn);

  bool Contains(float x, float y) const;

 private:
  float left_px_ = 0.0f;
  float right_px_ = 0.0f;
  float top_px_ = 0.0f;
  float bottom_px_ = 0.0f;
};

}  // namespace sensor_logger

#endif  // SENSOR_LOGGER_UI_BUTTON_H
