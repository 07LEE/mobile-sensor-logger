#include "pro_panel.h"

#include <cstring>

#include "gl_quad.h"

namespace sensor_logger {

bool ProPanel::Contains(const Rect& r, float x, float y) {
  return x >= r.left && x <= r.right && y >= r.top && y <= r.bottom;
}

bool ProPanel::CloseTouched(float x, float y) const {
  return Contains(close_, x, y);
}

bool ProPanel::ShutterTouched(float x, float y) const {
  return Contains(shutter_, x, y);
}

bool ProPanel::FpsTouched(float x, float y) const {
  return Contains(fps_, x, y);
}

bool ProPanel::MainsTouched(float x, float y) const {
  return Contains(mains_, x, y);
}

bool ProPanel::ShiftTouched(float x, float y) const {
  return Contains(shift_, x, y);
}

bool ProPanel::ResidualTouched(float x, float y) const {
  return Contains(residual_, x, y);
}

bool ProPanel::ResetTouched(float x, float y) const {
  return Contains(reset_, x, y);
}

void ProPanel::Draw(
    GLuint quad_program, GLuint white_texture, GLuint text_texture,
    GLint quad_color_location, GLuint vbo, const std::string& shutter_label,
    const std::string& fps_label, const std::string& mains_label,
    const std::string& shift_label, const std::string& residual_label,
    int viewport_width, int viewport_height,
    const std::function<void(const std::vector<std::string>&, int)>&
        rasterize_text_fn) {
  if (viewport_width <= 0 || viewport_height <= 0) return;

  const float vp_w = static_cast<float>(viewport_width);
  const float vp_h = static_cast<float>(viewport_height);

  // Taller than a 3-row panel needs, now that shift and residual make five
  // rows above RESET. close_bottom is measured up from kDialogBottom (see
  // below), so growing this only ever adds space above the row stack.
  constexpr float kDialogTop = 0.90f;
  constexpr float kDialogBottom = -0.90f;
  constexpr float kDialogLeft = -0.85f;
  constexpr float kDialogRight = 0.85f;

  constexpr float kRowH = 0.11f;
  constexpr float kRowGap = 0.020f;
  constexpr float kBtnH = 0.10f;

  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  static constexpr float kFullUvs[8] = {0.0f, 1.0f, 1.0f, 1.0f,
                                        0.0f, 0.0f, 1.0f, 0.0f};

  glActiveTexture(GL_TEXTURE0);
  glUseProgram(quad_program);
  glUniform1i(glGetUniformLocation(quad_program, "u_texture"), 0);

  // Dark semi-transparent background, full screen.
  glBindTexture(GL_TEXTURE_2D, white_texture);
  glUniform4f(quad_color_location, 0.02f, 0.02f, 0.04f, 0.92f);
  DrawQuad(quad_program, vbo, -1.0f, -1.0f, 1.0f, 1.0f, kFullUvs);

  // Inner dialog box.
  glUniform4f(quad_color_location, 0.12f, 0.14f, 0.20f, 0.98f);
  DrawQuad(quad_program, vbo, kDialogLeft, kDialogBottom, kDialogRight,
           kDialogTop, kFullUvs);

  // Header.
  const char* header = "=== PRO SETTINGS ===";
  const int header_cols = static_cast<int>(std::strlen(header));
  std::vector<std::string> header_lines = {header};
  rasterize_text_fn(header_lines, header_cols);
  glBindTexture(GL_TEXTURE_2D, text_texture);

  const float h_used = static_cast<float>(header_cols) / static_cast<float>(kTextColumns);
  const float h_row = 1.0f / static_cast<float>(kTextRows);
  const float h_uvs[8] = {0.0f, h_row, h_used, h_row, 0.0f, 0.0f, h_used, 0.0f};

  const float text_w_px = vp_w * 0.76f;
  const float h_scale = text_w_px / static_cast<float>(header_cols * kCellWidth);
  const float h_height_px = static_cast<float>(kCellHeight) * h_scale;
  const float h_top = kDialogTop - 0.05f;
  const float h_bottom = h_top - 2.0f * h_height_px / vp_h;

  glUniform4f(quad_color_location, 1.0f, 1.0f, 1.0f, 1.0f);
  DrawQuad(quad_program, vbo, -0.76f, h_bottom, 0.76f, h_top, h_uvs);

  // Five rows: shutter, fps, mains, shift, residual. Each is a full-width
  // tappable button — the whole row cycles its value on tap, there is no
  // separate label vs. control the way a checkbox pairs with its caption.
  const struct {
    const std::string& label;
    Rect* rect;
  } rows[5] = {
      {shutter_label, &shutter_}, {fps_label, &fps_},
      {mains_label, &mains_},     {shift_label, &shift_},
      {residual_label, &residual_},
  };

  float row_top = h_bottom - 0.06f;
  for (const auto& row : rows) {
    const float row_bottom = row_top - kRowH;

    Rect px;
    px.left = (kDialogLeft + 0.03f + 1.0f) * 0.5f * vp_w;
    px.right = (kDialogRight - 0.03f + 1.0f) * 0.5f * vp_w;
    px.top = (1.0f - row_top) * 0.5f * vp_h;
    px.bottom = (1.0f - row_bottom) * 0.5f * vp_h;
    *row.rect = px;

    glBindTexture(GL_TEXTURE_2D, white_texture);
    glUniform4f(quad_color_location, 0.16f, 0.20f, 0.30f, 0.95f);
    DrawQuad(quad_program, vbo, kDialogLeft + 0.03f, row_bottom,
             kDialogRight - 0.03f, row_top, kFullUvs);

    const int cols = static_cast<int>(row.label.size());
    const float cy = (row_top + row_bottom) * 0.5f;
    DrawScaledLabel(quad_program, vbo, text_texture, quad_color_location,
                    row.label, cols, 26, vp_w * (kDialogRight - kDialogLeft - 0.06f) * 0.5f,
                    vp_h * kRowH * 0.5f, 0.92f, 0.60f, LabelAnchor::kLeft,
                    kDialogLeft + 0.06f, cy, vp_w, vp_h, 1.0f, 1.0f, 1.0f, 1.0f,
                    rasterize_text_fn);

    row_top = row_bottom - kRowGap;
  }

  // RESET and CLOSE share the bottom row, side by side, rather than RESET
  // taking a full row of its own above it — it is one action among two here,
  // not a value in the list above it. Bottom margin measured up from the
  // dialog's edge rather than the top measured down from it: putting the top
  // a fixed distance above the dialog bottom and then dropping a full kBtnH
  // below that previously put the button's bottom edge outside the dialog.
  constexpr float kBottomMargin = 0.06f;
  constexpr float kGapBetween = 0.04f;
  const float bottom_row_bottom = kDialogBottom + kBottomMargin;
  const float bottom_row_top = bottom_row_bottom + kBtnH;
  const float half_left = kDialogLeft + 0.03f;
  const float half_right = kDialogRight - 0.03f;
  const float mid_left = (half_left + half_right) * 0.5f - kGapBetween * 0.5f;
  const float mid_right = (half_left + half_right) * 0.5f + kGapBetween * 0.5f;

  reset_.left = (half_left + 1.0f) * 0.5f * vp_w;
  reset_.right = (mid_left + 1.0f) * 0.5f * vp_w;
  reset_.top = (1.0f - bottom_row_top) * 0.5f * vp_h;
  reset_.bottom = (1.0f - bottom_row_bottom) * 0.5f * vp_h;

  close_.left = (mid_right + 1.0f) * 0.5f * vp_w;
  close_.right = (half_right + 1.0f) * 0.5f * vp_w;
  close_.top = (1.0f - bottom_row_top) * 0.5f * vp_h;
  close_.bottom = (1.0f - bottom_row_bottom) * 0.5f * vp_h;

  const float btn_box_w = vp_w * (mid_left - half_left) * 0.5f;
  const float btn_box_h = vp_h * kBtnH * 0.5f;
  const float btn_cy = (bottom_row_top + bottom_row_bottom) * 0.5f;

  glBindTexture(GL_TEXTURE_2D, white_texture);
  glUniform4f(quad_color_location, 0.35f, 0.20f, 0.16f, 0.95f);
  DrawQuad(quad_program, vbo, half_left, bottom_row_bottom, mid_left,
           bottom_row_top, kFullUvs);
  DrawScaledLabel(quad_program, vbo, text_texture, quad_color_location,
                  "RESET", 5, 9, btn_box_w, btn_box_h, 0.85f, 0.65f,
                  LabelAnchor::kCenter, (half_left + mid_left) * 0.5f, btn_cy,
                  vp_w, vp_h, 1.0f, 1.0f, 1.0f, 1.0f, rasterize_text_fn);

  // DrawScaledLabel above leaves text_texture bound (its last GL call); the
  // background quad below is untextured colour, not glyphs, and without this
  // rebind it samples whatever RESET's label last rasterized instead of the
  // plain white texture — a small, wrong-tinted ghost of RESET's own label
  // squeezed into CLOSE's box in place of a solid background.
  glBindTexture(GL_TEXTURE_2D, white_texture);
  glUniform4f(quad_color_location, 0.25f, 0.28f, 0.35f, 1.0f);
  DrawQuad(quad_program, vbo, mid_right, bottom_row_bottom, half_right,
           bottom_row_top, kFullUvs);
  DrawScaledLabel(quad_program, vbo, text_texture, quad_color_location,
                  "[ CLOSE ]", 9, 9, btn_box_w, btn_box_h, 0.85f, 0.65f,
                  LabelAnchor::kCenter, (mid_right + half_right) * 0.5f,
                  btn_cy, vp_w, vp_h, 1.0f, 1.0f, 1.0f, 1.0f,
                  rasterize_text_fn);

  glDisable(GL_BLEND);
}

}  // namespace sensor_logger
