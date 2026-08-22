#ifndef SENSOR_LOGGER_SESSIONS_OVERLAY_H
#define SENSOR_LOGGER_SESSIONS_OVERLAY_H

#include <GLES3/gl3.h>

#include <functional>
#include <string>
#include <vector>

#include "session_item.h"

namespace sensor_logger {

class SessionsOverlay {
 public:
  struct ItemRect {
    float left = 0.0f;
    float right = 0.0f;
    float top = 0.0f;
    float bottom = 0.0f;
  };

  SessionsOverlay() = default;

  void Draw(GLuint quad_program, GLuint white_texture, GLuint text_texture,
            GLint quad_color_location, GLuint vbo,
            const std::vector<SessionItem>& sessions,
            int pending_delete_index, int viewport_width, int viewport_height,
            const std::function<void(const std::vector<std::string>&, int)>& rasterize_text_fn);

  bool CloseTouched(float x, float y) const;
  int ItemDeleteTouched(float x, float y) const;

 private:
  float close_button_left_ = 0.0f;
  float close_button_right_ = 0.0f;
  float close_button_top_ = 0.0f;
  float close_button_bottom_ = 0.0f;

  std::vector<ItemRect> item_delete_rects_;
};

}  // namespace sensor_logger

#endif  // SENSOR_LOGGER_SESSIONS_OVERLAY_H
