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

  // 2. Draw Text Quad Centered inside Button using exact original formula
  const int columns = static_cast<int>(label.size()) + 2;
  rasterize_text_fn({label}, columns);
  glBindTexture(GL_TEXTURE_2D, text_texture);

  const float by_width = button_width_px * 0.86f / static_cast<float>(columns * kCellWidth);
  const float by_height = height_px * 0.5f / static_cast<float>(kGlyphHeight);
  const float scale = by_width < by_height ? by_width : by_height;

  const float used = static_cast<float>(columns) / static_cast<float>(kTextColumns);
  const float row = 1.0f / static_cast<float>(kTextRows);
  const float label_uvs[8] = {0.0f, row, used, row, 0.0f, 0.0f, used, 0.0f};

  const float label_width = 2.0f * static_cast<float>(columns * kCellWidth) * scale / vp_w;
  const float label_height = 2.0f * static_cast<float>(kGlyphHeight) * scale / vp_h;
  const float centre_x = (left + right) * 0.5f;
  const float label_centre = (top + bottom) * 0.5f;

  glUniform4f(quad_color_location, enabled ? 1.0f : 0.45f,
              enabled ? 1.0f : 0.45f, enabled ? 1.0f : 0.45f, 1.0f);
  DrawQuad(quad_program, vbo, centre_x - label_width * 0.5f,
                 label_centre - label_height * 0.5f, centre_x + label_width * 0.5f,
                 label_centre + label_height * 0.5f, label_uvs);

  glDisable(GL_BLEND);
}

bool UiButton::Contains(float x, float y) const {
  return x >= left_px_ && x <= right_px_ && y >= top_px_ && y <= bottom_px_;
}

}  // namespace sensor_logger
