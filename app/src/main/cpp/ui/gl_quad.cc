#include "gl_quad.h"

namespace sensor_logger {

void DrawQuad(GLuint program, GLuint vbo, float x0, float y0, float x1,
              float y1, const float* uvs) {
  const float vertices[16] = {
      x0, y0, uvs[0], uvs[1], x1, y0, uvs[2], uvs[3],
      x0, y1, uvs[4], uvs[5], x1, y1, uvs[6], uvs[7],
  };

  glUseProgram(program);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);

  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                        reinterpret_cast<void*>(2 * sizeof(float)));

  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void DrawScaledLabel(GLuint program, GLuint vbo, GLuint text_texture,
                     GLint quad_color_location, const std::string& label,
                     int columns, float box_width_px, float box_height_px,
                     float width_fill, float height_fill, LabelAnchor anchor,
                     float anchor_x, float center_y, float viewport_width,
                     float viewport_height, float red, float green, float blue,
                     float alpha,
                     const std::function<void(const std::vector<std::string>&, int)>&
                         rasterize_text_fn) {
  rasterize_text_fn({label}, columns);
  glBindTexture(GL_TEXTURE_2D, text_texture);

  const float used = static_cast<float>(columns) / static_cast<float>(kTextColumns);
  const float row = 1.0f / static_cast<float>(kTextRows);
  const float uvs[8] = {0.0f, row, used, row, 0.0f, 0.0f, used, 0.0f};

  const float scale_w = box_width_px * width_fill / static_cast<float>(columns * kCellWidth);
  const float scale_h = box_height_px * height_fill / static_cast<float>(kGlyphHeight);
  const float scale = scale_w < scale_h ? scale_w : scale_h;

  const float label_w = 2.0f * static_cast<float>(columns * kCellWidth) * scale / viewport_width;
  const float label_h = 2.0f * static_cast<float>(kGlyphHeight) * scale / viewport_height;

  const float left = anchor == LabelAnchor::kCenter ? anchor_x - label_w * 0.5f : anchor_x;

  glUniform4f(quad_color_location, red, green, blue, alpha);
  DrawQuad(program, vbo, left, center_y - label_h * 0.5f, left + label_w,
           center_y + label_h * 0.5f, uvs);
}

}  // namespace sensor_logger
