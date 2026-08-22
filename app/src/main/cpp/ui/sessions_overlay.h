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

  // Rows per page. More than this many sessions and the list pages instead of
  // trying to shrink rows to fit or scroll: every touch target stays exactly
  // as tappable as it is on a single-page list.
  static constexpr int kSessionsPerPage = 8;

  SessionsOverlay() = default;

  // `page` is 0-based and clamped internally to the valid range for
  // `sessions.size()`, so the caller does not have to re-derive the page
  // count just to keep it in bounds.
  void Draw(GLuint quad_program, GLuint white_texture, GLuint text_texture,
            GLint quad_color_location, GLuint vbo,
            const std::vector<SessionItem>& sessions,
            int pending_delete_index, int page, int viewport_width, int viewport_height,
            const std::function<void(const std::vector<std::string>&, int)>& rasterize_text_fn);

  bool CloseTouched(float x, float y) const;
  bool PrevPageTouched(float x, float y) const;
  bool NextPageTouched(float x, float y) const;

  // Returns an index into the `sessions` vector passed to the last Draw call,
  // already offset for the page shown then — not a position within the
  // current page.
  int ItemDeleteTouched(float x, float y) const;

 private:
  float close_button_left_ = 0.0f;
  float close_button_right_ = 0.0f;
  float close_button_top_ = 0.0f;
  float close_button_bottom_ = 0.0f;

  float prev_button_left_ = 0.0f;
  float prev_button_right_ = 0.0f;
  float prev_button_top_ = 0.0f;
  float prev_button_bottom_ = 0.0f;
  bool prev_enabled_ = false;

  float next_button_left_ = 0.0f;
  float next_button_right_ = 0.0f;
  float next_button_top_ = 0.0f;
  float next_button_bottom_ = 0.0f;
  bool next_enabled_ = false;

  // First index of `sessions` shown on the page from the last Draw call, so
  // ItemDeleteTouched can turn a row position back into an absolute index.
  int page_start_ = 0;

  std::vector<ItemRect> item_delete_rects_;
};

}  // namespace sensor_logger

#endif  // SENSOR_LOGGER_SESSIONS_OVERLAY_H
