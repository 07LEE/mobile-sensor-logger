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

  const float vp_w = static_cast<float>(viewport_width);
  const float vp_h = static_cast<float>(viewport_height);

  // Inner dialog box bounds and row metrics, needed below to work out how
  // many rows actually fit before anything is drawn.
  constexpr float kDialogTopFrac = 0.08f;
  constexpr float kDialogBottomFrac = 0.92f;
  const float d_top = 1.0f - 2.0f * kDialogTopFrac;
  const float d_bottom = 1.0f - 2.0f * kDialogBottomFrac;
  const float d_left = -0.92f;
  const float d_right = 0.92f;

  constexpr float kRowH = 0.07f;
  constexpr float kRowGap = 0.015f;
  constexpr float kBtnH = 0.06f;

  // Rows per page, sized to the real viewport instead of a fixed count: a
  // taller screen shows more sessions per page rather than leaving the
  // dialog half-empty above a NEXT button nobody needed yet. The header
  // height below is estimated from its unpaginated length (22 cols) — the
  // "(n/n)" suffix on a paginated header changes that little enough not to
  // matter for a row count that gets floored anyway.
  const float header_scale = (vp_w * 0.84f) / (22.0f * kCellWidth);
  const float header_height_px = static_cast<float>(kCellHeight) * header_scale;
  const float rows_top = (d_top - 0.04f) - 2.0f * header_height_px / vp_h - 0.04f;
  const float rows_bottom = d_bottom + 0.08f + 0.04f;  // clears the footer row
  int rows_per_page = static_cast<int>((rows_top - rows_bottom + kRowGap) / (kRowH + kRowGap));
  if (rows_per_page < 1) rows_per_page = 1;

  const int total = static_cast<int>(sessions.size());
  const int page_count = total == 0 ? 1 : (total + rows_per_page - 1) / rows_per_page;
  if (page < 0) page = 0;
  if (page > page_count - 1) page = page_count - 1;
  page_start_ = page * rows_per_page;
  const int page_end = page_start_ + rows_per_page < total
                            ? page_start_ + rows_per_page
                            : total;
  prev_enabled_ = page > 0;
  next_enabled_ = page < page_count - 1;

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

    for (int i = page_start_; i < page_end; ++i) {
      const auto& sess = sessions[static_cast<size_t>(i)];
      const float row_top = current_y;
      const float row_bottom = row_top - kRowH;
      current_y = row_bottom - 0.015f;

      // Draw session label text with aspect-preserved scaling
      char text_buf[64];
      std::snprintf(text_buf, sizeof(text_buf), "%.16s %.1fMB", sess.name.c_str(), sess.megabytes);
      const int s_cols = static_cast<int>(std::strlen(text_buf));
      const float s_cy = (row_top + row_bottom) * 0.5f;

      DrawScaledLabel(quad_program, vbo, text_texture, quad_color_location,
                      text_buf, s_cols, s_cols, vp_w * 0.54f, vp_h * kRowH * 0.5f,
                      0.95f, 0.70f, LabelAnchor::kLeft, -0.88f, s_cy, vp_w,
                      vp_h, 1.0f, 1.0f, 1.0f, 1.0f, rasterize_text_fn);

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
      const float b_cx = (btn_left + btn_right) * 0.5f;
      const float b_cy = (row_top + row_bottom) * 0.5f;

      DrawScaledLabel(quad_program, vbo, text_texture, quad_color_location,
                      btn_label, b_cols, b_cols, vp_w * (btn_right - btn_left) * 0.5f,
                      vp_h * kRowH * 0.5f, 0.85f, 0.65f, LabelAnchor::kCenter,
                      b_cx, b_cy, vp_w, vp_h, 1.0f, 1.0f, 1.0f, 1.0f,
                      rasterize_text_fn);
    }
  }

  // Close button at bottom center; flanked by PREV/NEXT once paginated.
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

  const float c_box_h = vp_h * kBtnH * 0.5f;
  const float c_cx = (close_left + close_right) * 0.5f;
  const float c_cy = (close_top + close_bottom) * 0.5f;

  DrawScaledLabel(quad_program, vbo, text_texture, quad_color_location,
                  "[ CLOSE ]", 9, 9, vp_w * close_w * 0.5f, c_box_h, 0.85f, 0.65f,
                  LabelAnchor::kCenter, c_cx, c_cy, vp_w, vp_h, 1.0f, 1.0f,
                  1.0f, 1.0f, rasterize_text_fn);

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

    const float side_box_w = vp_w * kSideBtnW * 0.5f;
    const float p_cx = (prev_left + prev_right) * 0.5f;
    const float n_cx = (next_left + next_right) * 0.5f;

    DrawScaledLabel(quad_program, vbo, text_texture, quad_color_location,
                    "< PREV", 6, 6, side_box_w, c_box_h, 0.85f, 0.65f,
                    LabelAnchor::kCenter, p_cx, c_cy, vp_w, vp_h, 1.0f, 1.0f,
                    1.0f, prev_enabled_ ? 1.0f : 0.45f, rasterize_text_fn);

    DrawScaledLabel(quad_program, vbo, text_texture, quad_color_location,
                    "NEXT >", 6, 6, side_box_w, c_box_h, 0.85f, 0.65f,
                    LabelAnchor::kCenter, n_cx, c_cy, vp_w, vp_h, 1.0f, 1.0f,
                    1.0f, next_enabled_ ? 1.0f : 0.45f, rasterize_text_fn);
  }

  glDisable(GL_BLEND);
}

}  // namespace sensor_logger
