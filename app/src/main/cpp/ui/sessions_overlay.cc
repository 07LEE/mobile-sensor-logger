#include "sessions_overlay.h"

#include <cstring>

namespace sensor_logger {

namespace {

constexpr int kGlyphHeight = 7;
constexpr int kCellWidth = 6;
constexpr int kCellHeight = 8;
constexpr int kTextColumns = 40;
constexpr int kTextRows = 8;

void DrawQuadHelper(GLuint program, GLuint vbo, float x0, float y0, float x1,
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

}  // namespace

bool SessionsOverlay::CloseTouched(float x, float y) const {
  return x >= close_button_left_ && x <= close_button_right_ &&
         y >= close_button_top_ && y <= close_button_bottom_;
}

int SessionsOverlay::ItemDeleteTouched(float x, float y) const {
  for (size_t i = 0; i < item_delete_rects_.size(); ++i) {
    const auto& r = item_delete_rects_[i];
    if (x >= r.left && x <= r.right && y >= r.top && y <= r.bottom) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

void SessionsOverlay::Draw(
    GLuint quad_program, GLuint white_texture, GLuint text_texture,
    GLint quad_color_location, GLuint vbo,
    const std::vector<SessionItem>& sessions,
    int pending_delete_index, int viewport_width, int viewport_height,
    const std::function<void(const std::vector<std::string>&, int)>& rasterize_text_fn) {
  if (viewport_width <= 0 || viewport_height <= 0) return;

  const float vp_w = static_cast<float>(viewport_width);
  const float vp_h = static_cast<float>(viewport_height);

  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  static constexpr float kFullUvs[8] = {0.0f, 1.0f, 1.0f, 1.0f,
                                        0.0f, 0.0f, 1.0f, 0.0f};

  glActiveTexture(GL_TEXTURE0);
  glUseProgram(quad_program);
  glUniform1i(glGetUniformLocation(quad_program, "u_texture"), 0);

  // Dark semi-transparent background overlay covering full screen
  glBindTexture(GL_TEXTURE_2D, white_texture);
  glUniform4f(quad_color_location, 0.02f, 0.02f, 0.04f, 0.92f);
  DrawQuadHelper(quad_program, vbo, -1.0f, -1.0f, 1.0f, 1.0f, kFullUvs);

  // Inner dialog box
  constexpr float kDialogTopFrac = 0.08f;
  constexpr float kDialogBottomFrac = 0.92f;
  const float d_top = 1.0f - 2.0f * kDialogTopFrac;
  const float d_bottom = 1.0f - 2.0f * kDialogBottomFrac;
  const float d_left = -0.92f;
  const float d_right = 0.92f;

  glUniform4f(quad_color_location, 0.12f, 0.14f, 0.20f, 0.98f);
  DrawQuadHelper(quad_program, vbo, d_left, d_bottom, d_right, d_top, kFullUvs);

  item_delete_rects_.clear();

  // Render header
  std::vector<std::string> header = {"=== SAVED SESSIONS ==="};
  rasterize_text_fn(header, 22);
  glBindTexture(GL_TEXTURE_2D, text_texture);

  const float h_used = 22.0f / static_cast<float>(kTextColumns);
  const float h_row = 1.0f / static_cast<float>(kTextRows);
  const float h_uvs[8] = {0.0f, h_row, h_used, h_row, 0.0f, 0.0f, h_used, 0.0f};

  const float text_w_px = vp_w * 0.84f;
  const float h_scale = text_w_px / static_cast<float>(22 * kCellWidth);
  const float h_height_px = static_cast<float>(kCellHeight) * h_scale;
  const float h_top = d_top - 0.04f;
  const float h_bottom = h_top - 2.0f * h_height_px / vp_h;

  glUniform4f(quad_color_location, 1.0f, 1.0f, 1.0f, 1.0f);
  DrawQuadHelper(quad_program, vbo, -0.84f, h_bottom, 0.84f, h_top, h_uvs);

  if (sessions.empty()) {
    std::vector<std::string> empty_msg = {"NO SESSIONS FOUND"};
    rasterize_text_fn(empty_msg, 17);
    glBindTexture(GL_TEXTURE_2D, text_texture);
    const float e_used = 17.0f / static_cast<float>(kTextColumns);
    const float e_uvs[8] = {0.0f, h_row, e_used, h_row, 0.0f, 0.0f, e_used, 0.0f};
    const float e_top = h_bottom - 0.10f;
    const float e_bottom = e_top - 2.0f * h_height_px / vp_h;
    DrawQuadHelper(quad_program, vbo, -0.80f, e_bottom, 0.80f, e_top, e_uvs);
  } else {
    // Render individual session rows
    const int max_show = static_cast<int>(sessions.size()) < 8 ? static_cast<int>(sessions.size()) : 8;
    float current_y = h_bottom - 0.04f;
    constexpr float kRowH = 0.07f;

    for (int i = 0; i < max_show; ++i) {
      const auto& sess = sessions[static_cast<size_t>(i)];
      const float row_top = current_y;
      const float row_bottom = row_top - kRowH;
      current_y = row_bottom - 0.015f;

      // Draw session label text with aspect-preserved scaling
      char text_buf[64];
      std::snprintf(text_buf, sizeof(text_buf), "%.16s %.1fMB", sess.name.c_str(), sess.megabytes);
      std::vector<std::string> s_line = {text_buf};
      const int s_cols = static_cast<int>(std::strlen(text_buf));

      rasterize_text_fn(s_line, s_cols);
      glBindTexture(GL_TEXTURE_2D, text_texture);
      const float s_used = static_cast<float>(s_cols) / static_cast<float>(kTextColumns);
      const float s_uvs[8] = {0.0f, h_row, s_used, h_row, 0.0f, 0.0f, s_used, 0.0f};

      const float s_box_w = vp_w * 0.54f;
      const float s_box_h = vp_h * kRowH * 0.5f;
      const float s_sw = s_box_w * 0.95f / static_cast<float>(s_cols * kCellWidth);
      const float s_sh = s_box_h * 0.70f / static_cast<float>(kGlyphHeight);
      const float s_sc = s_sw < s_sh ? s_sw : s_sh;

      const float s_lw = 2.0f * static_cast<float>(s_cols * kCellWidth) * s_sc / vp_w;
      const float s_lh = 2.0f * static_cast<float>(kGlyphHeight) * s_sc / vp_h;
      const float s_left = -0.88f;
      const float s_cy = (row_top + row_bottom) * 0.5f;

      glUniform4f(quad_color_location, 1.0f, 1.0f, 1.0f, 1.0f);
      DrawQuadHelper(quad_program, vbo, s_left, s_cy - s_lh * 0.5f, s_left + s_lw, s_cy + s_lh * 0.5f, s_uvs);

      // Draw individual delete button next to session item
      const bool is_pending = (pending_delete_index == i);
      const std::string btn_label = is_pending ? "[CONFIRM?]" : "[DEL]";
      const float btn_left = 0.48f;
      const float btn_right = 0.88f;

      ItemRect rect;
      rect.left = (btn_left + 1.0f) * 0.5f * vp_w;
      rect.right = (btn_right + 1.0f) * 0.5f * vp_w;
      rect.top = (1.0f - row_top) * 0.5f * vp_h;
      rect.bottom = (1.0f - row_bottom) * 0.5f * vp_h;
      item_delete_rects_.push_back(rect);

      glBindTexture(GL_TEXTURE_2D, white_texture);
      if (is_pending) {
        glUniform4f(quad_color_location, 0.85f, 0.15f, 0.15f, 1.0f);
      } else {
        glUniform4f(quad_color_location, 0.45f, 0.18f, 0.18f, 0.90f);
      }
      DrawQuadHelper(quad_program, vbo, btn_left, row_bottom, btn_right, row_top, kFullUvs);

      const int b_cols = static_cast<int>(btn_label.size());
      rasterize_text_fn({btn_label}, b_cols);
      glBindTexture(GL_TEXTURE_2D, text_texture);
      const float b_used = static_cast<float>(b_cols) / static_cast<float>(kTextColumns);
      const float b_uvs[8] = {0.0f, h_row, b_used, h_row, 0.0f, 0.0f, b_used, 0.0f};

      const float b_box_w = vp_w * (btn_right - btn_left) * 0.5f;
      const float b_box_h = vp_h * kRowH * 0.5f;
      const float b_sw = b_box_w * 0.85f / static_cast<float>(b_cols * kCellWidth);
      const float b_sh = b_box_h * 0.65f / static_cast<float>(kGlyphHeight);
      const float b_sc = b_sw < b_sh ? b_sw : b_sh;

      const float b_lw = 2.0f * static_cast<float>(b_cols * kCellWidth) * b_sc / vp_w;
      const float b_lh = 2.0f * static_cast<float>(kGlyphHeight) * b_sc / vp_h;
      const float b_cx = (btn_left + btn_right) * 0.5f;
      const float b_cy = (row_top + row_bottom) * 0.5f;

      glUniform4f(quad_color_location, 1.0f, 1.0f, 1.0f, 1.0f);
      DrawQuadHelper(quad_program, vbo, b_cx - b_lw * 0.5f, b_cy - b_lh * 0.5f,
                     b_cx + b_lw * 0.5f, b_cy + b_lh * 0.5f, b_uvs);
    }
  }

  // Close Button at bottom center
  constexpr float kBtnW = 0.50f;
  constexpr float kBtnH = 0.06f;

  const float close_top = d_bottom + 0.08f;
  const float close_bottom = close_top - kBtnH;
  const float close_left = -kBtnW * 0.5f;
  const float close_right = kBtnW * 0.5f;

  close_button_left_ = (close_left + 1.0f) * 0.5f * vp_w;
  close_button_right_ = (close_right + 1.0f) * 0.5f * vp_w;
  close_button_top_ = (1.0f - close_top) * 0.5f * vp_h;
  close_button_bottom_ = (1.0f - close_bottom) * 0.5f * vp_h;

  glBindTexture(GL_TEXTURE_2D, white_texture);
  glUniform4f(quad_color_location, 0.25f, 0.28f, 0.35f, 1.0f);
  DrawQuadHelper(quad_program, vbo, close_left, close_bottom, close_right, close_top, kFullUvs);

  rasterize_text_fn({"[ CLOSE ]"}, 9);
  glBindTexture(GL_TEXTURE_2D, text_texture);

  const float c_used = 9.0f / static_cast<float>(kTextColumns);
  const float c_row = 1.0f / static_cast<float>(kTextRows);
  const float c_uvs[8] = {0.0f, c_row, c_used, c_row, 0.0f, 0.0f, c_used, 0.0f};

  const float c_box_w = vp_w * kBtnW * 0.5f;
  const float c_box_h = vp_h * kBtnH * 0.5f;
  const float c_sw = c_box_w * 0.85f / static_cast<float>(9 * kCellWidth);
  const float c_sh = c_box_h * 0.65f / static_cast<float>(kGlyphHeight);
  const float c_sc = c_sw < c_sh ? c_sw : c_sh;

  const float c_lw = 2.0f * static_cast<float>(9 * kCellWidth) * c_sc / vp_w;
  const float c_lh = 2.0f * static_cast<float>(kGlyphHeight) * c_sc / vp_h;
  const float c_cx = (close_left + close_right) * 0.5f;
  const float c_cy = (close_top + close_bottom) * 0.5f;

  glUniform4f(quad_color_location, 1.0f, 1.0f, 1.0f, 1.0f);
  DrawQuadHelper(quad_program, vbo, c_cx - c_lw * 0.5f, c_cy - c_lh * 0.5f,
                 c_cx + c_lw * 0.5f, c_cy + c_lh * 0.5f, c_uvs);

  glDisable(GL_BLEND);
}

}  // namespace sensor_logger
