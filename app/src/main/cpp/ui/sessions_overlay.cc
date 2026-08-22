#include "sessions_overlay.h"

#include <cstring>

#include "gl_quad.h"

namespace sensor_logger {

bool SessionsOverlay::CloseTouched(float x, float y) const {
  return x >= close_button_left_ && x <= close_button_right_ &&
         y >= close_button_top_ && y <= close_button_bottom_;
}

bool SessionsOverlay::PrevPageTouched(float x, float y) const {
  return prev_enabled_ && x >= prev_button_left_ && x <= prev_button_right_ &&
         y >= prev_button_top_ && y <= prev_button_bottom_;
}

bool SessionsOverlay::NextPageTouched(float x, float y) const {
  return next_enabled_ && x >= next_button_left_ && x <= next_button_right_ &&
         y >= next_button_top_ && y <= next_button_bottom_;
}

int SessionsOverlay::ItemDeleteTouched(float x, float y) const {
  for (size_t row = 0; row < item_delete_rects_.size(); ++row) {
    const auto& r = item_delete_rects_[row];
    if (x >= r.left && x <= r.right && y >= r.top && y <= r.bottom) {
      return page_start_ + static_cast<int>(row);
    }
  }
  return -1;
}

void SessionsOverlay::Draw(
    GLuint quad_program, GLuint white_texture, GLuint text_texture,
    GLint quad_color_location, GLuint vbo,
    const std::vector<SessionItem>& sessions,
    int pending_delete_index, int page, int viewport_width, int viewport_height,
    const std::function<void(const std::vector<std::string>&, int)>& rasterize_text_fn) {
  if (viewport_width <= 0 || viewport_height <= 0) return;

  const int total = static_cast<int>(sessions.size());
  const int page_count = total == 0 ? 1 : (total + kSessionsPerPage - 1) / kSessionsPerPage;
  if (page < 0) page = 0;
  if (page > page_count - 1) page = page_count - 1;
  page_start_ = page * kSessionsPerPage;
  const int page_end = page_start_ + kSessionsPerPage < total
                            ? page_start_ + kSessionsPerPage
                            : total;
  prev_enabled_ = page > 0;
  next_enabled_ = page < page_count - 1;

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
  DrawQuad(quad_program, vbo, -1.0f, -1.0f, 1.0f, 1.0f, kFullUvs);

  // Inner dialog box
  constexpr float kDialogTopFrac = 0.08f;
  constexpr float kDialogBottomFrac = 0.92f;
  const float d_top = 1.0f - 2.0f * kDialogTopFrac;
  const float d_bottom = 1.0f - 2.0f * kDialogBottomFrac;
  const float d_left = -0.92f;
  const float d_right = 0.92f;

  glUniform4f(quad_color_location, 0.12f, 0.14f, 0.20f, 0.98f);
  DrawQuad(quad_program, vbo, d_left, d_bottom, d_right, d_top, kFullUvs);

  item_delete_rects_.clear();

  // Render header, with a page indicator once there is more than one page.
  char header_buf[40];
  if (page_count > 1) {
    std::snprintf(header_buf, sizeof(header_buf), "=== SESSIONS (%d/%d) ===",
                  page + 1, page_count);
  } else {
    std::snprintf(header_buf, sizeof(header_buf), "=== SAVED SESSIONS ===");
  }
  std::vector<std::string> header = {header_buf};
  const int header_cols = static_cast<int>(std::strlen(header_buf));
  rasterize_text_fn(header, header_cols);
  glBindTexture(GL_TEXTURE_2D, text_texture);

  const float h_used = static_cast<float>(header_cols) / static_cast<float>(kTextColumns);
  const float h_row = 1.0f / static_cast<float>(kTextRows);
  const float h_uvs[8] = {0.0f, h_row, h_used, h_row, 0.0f, 0.0f, h_used, 0.0f};

  const float text_w_px = vp_w * 0.84f;
  const float h_scale = text_w_px / static_cast<float>(header_cols * kCellWidth);
  const float h_height_px = static_cast<float>(kCellHeight) * h_scale;
  const float h_top = d_top - 0.04f;
  const float h_bottom = h_top - 2.0f * h_height_px / vp_h;

  glUniform4f(quad_color_location, 1.0f, 1.0f, 1.0f, 1.0f);
  DrawQuad(quad_program, vbo, -0.84f, h_bottom, 0.84f, h_top, h_uvs);

  if (sessions.empty()) {
    std::vector<std::string> empty_msg = {"NO SESSIONS FOUND"};
    rasterize_text_fn(empty_msg, 17);
    glBindTexture(GL_TEXTURE_2D, text_texture);
    const float e_used = 17.0f / static_cast<float>(kTextColumns);
    const float e_uvs[8] = {0.0f, h_row, e_used, h_row, 0.0f, 0.0f, e_used, 0.0f};
    const float e_top = h_bottom - 0.10f;
    const float e_bottom = e_top - 2.0f * h_height_px / vp_h;
    DrawQuad(quad_program, vbo, -0.80f, e_bottom, 0.80f, e_top, e_uvs);
  } else {
    // Render this page's session rows
    float current_y = h_bottom - 0.04f;
    constexpr float kRowH = 0.07f;

    for (int i = page_start_; i < page_end; ++i) {
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
      DrawQuad(quad_program, vbo, s_left, s_cy - s_lh * 0.5f, s_left + s_lw, s_cy + s_lh * 0.5f, s_uvs);

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
      DrawQuad(quad_program, vbo, btn_left, row_bottom, btn_right, row_top, kFullUvs);

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
      DrawQuad(quad_program, vbo, b_cx - b_lw * 0.5f, b_cy - b_lh * 0.5f,
                     b_cx + b_lw * 0.5f, b_cy + b_lh * 0.5f, b_uvs);
    }
  }

  // Close button at bottom center; flanked by PREV/NEXT once paginated.
  constexpr float kBtnH = 0.06f;
  constexpr float kCloseWSingle = 0.50f;
  constexpr float kCloseWPaged = 0.34f;
  constexpr float kSideBtnW = 0.32f;

  const bool paginated = page_count > 1;
  const float close_w = paginated ? kCloseWPaged : kCloseWSingle;
  const float close_top = d_bottom + 0.08f;
  const float close_bottom = close_top - kBtnH;
  const float close_left = -close_w * 0.5f;
  const float close_right = close_w * 0.5f;

  close_button_left_ = (close_left + 1.0f) * 0.5f * vp_w;
  close_button_right_ = (close_right + 1.0f) * 0.5f * vp_w;
  close_button_top_ = (1.0f - close_top) * 0.5f * vp_h;
  close_button_bottom_ = (1.0f - close_bottom) * 0.5f * vp_h;

  glBindTexture(GL_TEXTURE_2D, white_texture);
  glUniform4f(quad_color_location, 0.25f, 0.28f, 0.35f, 1.0f);
  DrawQuad(quad_program, vbo, close_left, close_bottom, close_right, close_top, kFullUvs);

  rasterize_text_fn({"[ CLOSE ]"}, 9);
  glBindTexture(GL_TEXTURE_2D, text_texture);

  const float c_used = 9.0f / static_cast<float>(kTextColumns);
  const float c_row = 1.0f / static_cast<float>(kTextRows);
  const float c_uvs[8] = {0.0f, c_row, c_used, c_row, 0.0f, 0.0f, c_used, 0.0f};

  const float c_box_w = vp_w * close_w * 0.5f;
  const float c_box_h = vp_h * kBtnH * 0.5f;
  const float c_sw = c_box_w * 0.85f / static_cast<float>(9 * kCellWidth);
  const float c_sh = c_box_h * 0.65f / static_cast<float>(kGlyphHeight);
  const float c_sc = c_sw < c_sh ? c_sw : c_sh;

  const float c_lw = 2.0f * static_cast<float>(9 * kCellWidth) * c_sc / vp_w;
  const float c_lh = 2.0f * static_cast<float>(kGlyphHeight) * c_sc / vp_h;
  const float c_cx = (close_left + close_right) * 0.5f;
  const float c_cy = (close_top + close_bottom) * 0.5f;

  glUniform4f(quad_color_location, 1.0f, 1.0f, 1.0f, 1.0f);
  DrawQuad(quad_program, vbo, c_cx - c_lw * 0.5f, c_cy - c_lh * 0.5f,
                 c_cx + c_lw * 0.5f, c_cy + c_lh * 0.5f, c_uvs);

  if (paginated) {
    const float prev_left = -0.90f;
    const float prev_right = prev_left + kSideBtnW;
    const float next_right = 0.90f;
    const float next_left = next_right - kSideBtnW;

    prev_button_left_ = (prev_left + 1.0f) * 0.5f * vp_w;
    prev_button_right_ = (prev_right + 1.0f) * 0.5f * vp_w;
    prev_button_top_ = close_button_top_;
    prev_button_bottom_ = close_button_bottom_;

    next_button_left_ = (next_left + 1.0f) * 0.5f * vp_w;
    next_button_right_ = (next_right + 1.0f) * 0.5f * vp_w;
    next_button_top_ = close_button_top_;
    next_button_bottom_ = close_button_bottom_;

    glBindTexture(GL_TEXTURE_2D, white_texture);
    glUniform4f(quad_color_location, 0.25f, 0.28f, 0.35f,
                prev_enabled_ ? 1.0f : 0.5f);
    DrawQuad(quad_program, vbo, prev_left, close_bottom, prev_right, close_top, kFullUvs);

    glUniform4f(quad_color_location, 0.25f, 0.28f, 0.35f,
                next_enabled_ ? 1.0f : 0.5f);
    DrawQuad(quad_program, vbo, next_left, close_bottom, next_right, close_top, kFullUvs);

    const std::string prev_label = "< PREV";
    const std::string next_label = "NEXT >";
    const int p_cols = static_cast<int>(prev_label.size());
    const int n_cols = static_cast<int>(next_label.size());
    const float side_box_w = vp_w * kSideBtnW * 0.5f;

    rasterize_text_fn({prev_label}, p_cols);
    glBindTexture(GL_TEXTURE_2D, text_texture);
    const float p_used = static_cast<float>(p_cols) / static_cast<float>(kTextColumns);
    const float p_uvs[8] = {0.0f, c_row, p_used, c_row, 0.0f, 0.0f, p_used, 0.0f};
    const float p_sw = side_box_w * 0.85f / static_cast<float>(p_cols * kCellWidth);
    const float p_sh = c_box_h * 0.65f / static_cast<float>(kGlyphHeight);
    const float p_sc = p_sw < p_sh ? p_sw : p_sh;
    const float p_lw = 2.0f * static_cast<float>(p_cols * kCellWidth) * p_sc / vp_w;
    const float p_lh = 2.0f * static_cast<float>(kGlyphHeight) * p_sc / vp_h;
    const float p_cx = (prev_left + prev_right) * 0.5f;

    glUniform4f(quad_color_location, 1.0f, 1.0f, 1.0f, prev_enabled_ ? 1.0f : 0.45f);
    DrawQuad(quad_program, vbo, p_cx - p_lw * 0.5f, c_cy - p_lh * 0.5f,
             p_cx + p_lw * 0.5f, c_cy + p_lh * 0.5f, p_uvs);

    rasterize_text_fn({next_label}, n_cols);
    glBindTexture(GL_TEXTURE_2D, text_texture);
    const float n_used = static_cast<float>(n_cols) / static_cast<float>(kTextColumns);
    const float n_uvs[8] = {0.0f, c_row, n_used, c_row, 0.0f, 0.0f, n_used, 0.0f};
    const float n_sw = side_box_w * 0.85f / static_cast<float>(n_cols * kCellWidth);
    const float n_sh = c_box_h * 0.65f / static_cast<float>(kGlyphHeight);
    const float n_sc = n_sw < n_sh ? n_sw : n_sh;
    const float n_lw = 2.0f * static_cast<float>(n_cols * kCellWidth) * n_sc / vp_w;
    const float n_lh = 2.0f * static_cast<float>(kGlyphHeight) * n_sc / vp_h;
    const float n_cx = (next_left + next_right) * 0.5f;

    glUniform4f(quad_color_location, 1.0f, 1.0f, 1.0f, next_enabled_ ? 1.0f : 0.45f);
    DrawQuad(quad_program, vbo, n_cx - n_lw * 0.5f, c_cy - n_lh * 0.5f,
             n_cx + n_lw * 0.5f, c_cy + n_lh * 0.5f, n_uvs);
  }

  glDisable(GL_BLEND);
}

}  // namespace sensor_logger
