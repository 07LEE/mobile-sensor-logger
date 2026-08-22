#include "ui_button.h"

#include "gl_quad.h"

namespace sensor_logger {

void UiButton::Draw(GLuint quad_program, GLuint white_texture, GLuint text_texture,
                    GLint quad_color_location, GLuint vbo, const std::string& label,
                    float top_fraction, float width_fraction, float height_fraction,
                    float red, float green, float blue, float alpha, bool enabled,
                    int viewport_width, int viewport_height,
                    const std::function<void(const std::vector<std::string>&, int)>& rasterize_text_fn) {
  if (viewport_width <= 0 || viewport_height <= 0) return;

  const float vp_w = static_cast<float>(viewport_width);
  const float vp_h = static_cast<float>(viewport_height);

  const float button_width_px = vp_w * width_fraction;
  const float height_px = vp_h * height_fraction;

  const float left_frac = (1.0f - width_fraction) * 0.5f;
  const float left = -1.0f + 2.0f * left_frac;
  const float right = left + 2.0f * width_fraction;
  const float top = 1.0f - 2.0f * top_fraction;
  const float bottom = top - 2.0f * height_px / vp_h;

  left_px_ = left_frac * vp_w;
  right_px_ = left_px_ + button_width_px;
  top_px_ = top_fraction * vp_h;
  bottom_px_ = top_px_ + height_px;

  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  static constexpr float kFullUvs[8] = {0.0f, 1.0f, 1.0f, 1.0f,
                                        0.0f, 0.0f, 1.0f, 0.0f};

  glActiveTexture(GL_TEXTURE0);
  glUseProgram(quad_program);

  // 1. Draw Background Quad
  glBindTexture(GL_TEXTURE_2D, white_texture);
  if (enabled) {
    glUniform4f(quad_color_location, red, green, blue, alpha);
  } else {
    glUniform4f(quad_color_location, 0.09f, 0.09f, 0.09f, 0.95f);
  }
  DrawQuad(quad_program, vbo, left, bottom, right, top, kFullUvs);

  // 2. Draw Text Quad Centered inside Button
  const int columns = static_cast<int>(label.size()) + 2;
  const float centre_x = (left + right) * 0.5f;
  const float label_centre = (top + bottom) * 0.5f;
  const float shade = enabled ? 1.0f : 0.45f;

  // A fixed reference rather than this label's own length: the on-screen
  // buttons share this Draw(), and sizing each label to its own box made a
  // short one ("LOCK") render noticeably larger than a long one ("RETENTION
  // SHARP") in the same row of buttons. 18 covers the longest label any of
  // them currently uses ("ULTRAWIDE 10.0MM", 16 chars) plus the padding
  // above; a future label longer than that would be the one to shrink
  // instead of blowing the others up to match it.
  constexpr int kScaleColumns = 18;

  DrawScaledLabel(quad_program, vbo, text_texture, quad_color_location, label,
                  columns, kScaleColumns, button_width_px, height_px, 0.86f,
                  0.5f, LabelAnchor::kCenter, centre_x, label_centre, vp_w,
                  vp_h, shade, shade, shade, 1.0f, rasterize_text_fn);

  glDisable(GL_BLEND);
}

bool UiButton::Contains(float x, float y) const {
  return x >= left_px_ && x <= right_px_ && y >= top_px_ && y <= bottom_px_;
}

}  // namespace sensor_logger
