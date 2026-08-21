#include "preview_renderer.h"

#include <android/log.h>

#include <cstring>

namespace sensor_logger {
namespace {

constexpr char kTag[] = "sensor_logger";

// Columns of a five-by-seven glyph, least significant bit at the top row. Only
// the characters the status lines use are here; anything else draws as a blank.
constexpr int kGlyphWidth = 5;
constexpr int kGlyphHeight = 7;
constexpr int kCellWidth = 6;  // one column of spacing
constexpr int kCellHeight = 8;

struct Glyph {
  char code;
  uint8_t columns[kGlyphWidth];
};

constexpr Glyph kFont[] = {
    {' ', {0x00, 0x00, 0x00, 0x00, 0x00}},
    {'0', {0x3E, 0x51, 0x49, 0x45, 0x3E}},
    {'1', {0x00, 0x42, 0x7F, 0x40, 0x00}},
    {'2', {0x42, 0x61, 0x51, 0x49, 0x46}},
    {'3', {0x21, 0x41, 0x45, 0x4B, 0x31}},
    {'4', {0x18, 0x14, 0x12, 0x7F, 0x10}},
    {'5', {0x27, 0x45, 0x45, 0x45, 0x39}},
    {'6', {0x3C, 0x4A, 0x49, 0x49, 0x30}},
    {'7', {0x01, 0x71, 0x09, 0x05, 0x03}},
    {'8', {0x36, 0x49, 0x49, 0x49, 0x36}},
    {'9', {0x06, 0x49, 0x49, 0x29, 0x1E}},
    {'A', {0x7E, 0x11, 0x11, 0x11, 0x7E}},
    {'B', {0x7F, 0x49, 0x49, 0x49, 0x36}},
    {'C', {0x3E, 0x41, 0x41, 0x41, 0x22}},
    {'D', {0x7F, 0x41, 0x41, 0x22, 0x1C}},
    {'E', {0x7F, 0x49, 0x49, 0x49, 0x41}},
    {'F', {0x7F, 0x09, 0x09, 0x09, 0x01}},
    {'G', {0x3E, 0x41, 0x49, 0x49, 0x7A}},
    {'H', {0x7F, 0x08, 0x08, 0x08, 0x7F}},
    {'I', {0x00, 0x41, 0x7F, 0x41, 0x00}},
    {'J', {0x20, 0x40, 0x41, 0x3F, 0x01}},
    {'K', {0x7F, 0x08, 0x14, 0x22, 0x41}},
    {'L', {0x7F, 0x40, 0x40, 0x40, 0x40}},
    {'M', {0x7F, 0x02, 0x0C, 0x02, 0x7F}},
    {'N', {0x7F, 0x04, 0x08, 0x10, 0x7F}},
    {'O', {0x3E, 0x41, 0x41, 0x41, 0x3E}},
    {'P', {0x7F, 0x09, 0x09, 0x09, 0x06}},
    {'Q', {0x3E, 0x41, 0x51, 0x21, 0x5E}},
    {'R', {0x7F, 0x09, 0x19, 0x29, 0x46}},
    {'S', {0x46, 0x49, 0x49, 0x49, 0x31}},
    {'T', {0x01, 0x01, 0x7F, 0x01, 0x01}},
    {'U', {0x3F, 0x40, 0x40, 0x40, 0x3F}},
    {'V', {0x1F, 0x20, 0x40, 0x20, 0x1F}},
    {'W', {0x3F, 0x40, 0x38, 0x40, 0x3F}},
    {'X', {0x63, 0x14, 0x08, 0x14, 0x63}},
    {'Y', {0x07, 0x08, 0x70, 0x08, 0x07}},
    {'Z', {0x61, 0x51, 0x49, 0x45, 0x43}},
    {':', {0x00, 0x36, 0x36, 0x00, 0x00}},
    {'/', {0x20, 0x10, 0x08, 0x04, 0x02}},
    {'.', {0x00, 0x60, 0x60, 0x00, 0x00}},
    {'-', {0x08, 0x08, 0x08, 0x08, 0x08}},
    {'%', {0x23, 0x13, 0x08, 0x64, 0x62}},
};

// The readout is rasterised into a fixed grid so the texture never has to be
// reallocated; lines longer than this are cut off.
constexpr int kTextColumns = 40;
constexpr int kTextRows = 8;
constexpr int kTextWidth = kTextColumns * kCellWidth;
constexpr int kTextHeight = kTextRows * kCellHeight;

const uint8_t* FindGlyph(char c) {
  if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
  for (const Glyph& glyph : kFont) {
    if (glyph.code == c) return glyph.columns;
  }
  return nullptr;
}

constexpr char kCameraVertexShader[] = R"(#version 300 es
layout(location = 0) in vec2 a_position;
layout(location = 1) in vec2 a_uv;
out vec2 v_uv;
void main() {
  gl_Position = vec4(a_position, 0.0, 1.0);
  v_uv = a_uv;
}
)";

// BT.601 studio swing, which is what YUV_420_888 carries. Getting this wrong
// shows up as a preview that is merely a bit flat, so it is worth being exact
// rather than eyeballing it.
constexpr char kCameraFragmentShader[] = R"(#version 300 es
precision mediump float;
uniform sampler2D u_luma;
uniform sampler2D u_chroma;
uniform int u_swap_chroma;
in vec2 v_uv;
out vec4 o_color;
void main() {
  float y = texture(u_luma, v_uv).r;
  vec2 c = texture(u_chroma, v_uv).rg;
  vec2 uv = (u_swap_chroma == 1 ? c.gr : c.rg) - vec2(0.5, 0.5);
  y = (y - 0.0625) * 1.164384;
  o_color = vec4(y + 1.596027 * uv.y,
                 y - 0.391762 * uv.x - 0.812968 * uv.y,
                 y + 2.017232 * uv.x,
                 1.0);
}
)";

// One program covers both the solid panels and the text: the text texture is
// single channel and the solid draws bind a one-pixel opaque texture, so the
// colour comes from the uniform either way.
constexpr char kQuadFragmentShader[] = R"(#version 300 es
precision mediump float;
uniform sampler2D u_texture;
uniform vec4 u_color;
in vec2 v_uv;
out vec4 o_color;
void main() {
  float coverage = texture(u_texture, v_uv).r;
  o_color = vec4(u_color.rgb, u_color.a * coverage);
}
)";

GLuint CompileShader(GLenum type, const char* source) {
  const GLuint shader = glCreateShader(type);
  glShaderSource(shader, 1, &source, nullptr);
  glCompileShader(shader);

  GLint compiled = 0;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
  if (compiled == GL_FALSE) {
    char log[512];
    glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
    __android_log_print(ANDROID_LOG_ERROR, kTag, "shader failed: %s", log);
    glDeleteShader(shader);
    return 0;
  }
  return shader;
}

GLuint LinkProgram(const char* vertex_source, const char* fragment_source) {
  const GLuint vertex = CompileShader(GL_VERTEX_SHADER, vertex_source);
  const GLuint fragment = CompileShader(GL_FRAGMENT_SHADER, fragment_source);
  if (vertex == 0 || fragment == 0) return 0;

  const GLuint program = glCreateProgram();
  glAttachShader(program, vertex);
  glAttachShader(program, fragment);
  glLinkProgram(program);
  glDeleteShader(vertex);
  glDeleteShader(fragment);

  GLint linked = 0;
  glGetProgramiv(program, GL_LINK_STATUS, &linked);
  if (linked == GL_FALSE) {
    char log[512];
    glGetProgramInfoLog(program, sizeof(log), nullptr, log);
    __android_log_print(ANDROID_LOG_ERROR, kTag, "link failed: %s", log);
    glDeleteProgram(program);
    return 0;
  }
  return program;
}

}  // namespace

PreviewRenderer::~PreviewRenderer() = default;

bool PreviewRenderer::Init() {
  camera_program_ = LinkProgram(kCameraVertexShader, kCameraFragmentShader);
  quad_program_ = LinkProgram(kCameraVertexShader, kQuadFragmentShader);
  if (camera_program_ == 0 || quad_program_ == 0) return false;

  quad_color_location_ = glGetUniformLocation(quad_program_, "u_color");

  glGenBuffers(1, &vbo_);
  glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  glBufferData(GL_ARRAY_BUFFER, 16 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);

  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

  text_pixels_.assign(static_cast<size_t>(kTextWidth) * kTextHeight, 0);
  glGenTextures(1, &text_texture_);
  glBindTexture(GL_TEXTURE_2D, text_texture_);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, kTextWidth, kTextHeight, 0, GL_RED,
               GL_UNSIGNED_BYTE, text_pixels_.data());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  glGenTextures(1, &luma_texture_);
  glBindTexture(GL_TEXTURE_2D, luma_texture_);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  glGenTextures(1, &chroma_texture_);
  glBindTexture(GL_TEXTURE_2D, chroma_texture_);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  const uint8_t opaque = 0xFF;
  glGenTextures(1, &white_texture_);
  glBindTexture(GL_TEXTURE_2D, white_texture_);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, 1, 1, 0, GL_RED, GL_UNSIGNED_BYTE,
               &opaque);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

  return true;
}

void PreviewRenderer::Destroy() {
  if (vbo_ != 0) glDeleteBuffers(1, &vbo_);
  if (luma_texture_ != 0) glDeleteTextures(1, &luma_texture_);
  if (chroma_texture_ != 0) glDeleteTextures(1, &chroma_texture_);
  if (text_texture_ != 0) glDeleteTextures(1, &text_texture_);
  if (white_texture_ != 0) glDeleteTextures(1, &white_texture_);
  if (camera_program_ != 0) glDeleteProgram(camera_program_);
  if (quad_program_ != 0) glDeleteProgram(quad_program_);

  vbo_ = 0;
  luma_texture_ = 0;
  chroma_texture_ = 0;
  camera_uploaded_ = false;
  text_texture_ = 0;
  white_texture_ = 0;
  camera_program_ = 0;
  quad_program_ = 0;
}

void PreviewRenderer::SetViewport(int width, int height) {
  viewport_width_ = width;
  viewport_height_ = height;
  glViewport(0, 0, width, height);
}

void PreviewRenderer::DrawQuad(GLuint program, float x0, float y0, float x1,
                               float y1, const float* uvs) {
  const float vertices[16] = {
      x0, y0, uvs[0], uvs[1], x1, y0, uvs[2], uvs[3],
      x0, y1, uvs[4], uvs[5], x1, y1, uvs[6], uvs[7],
  };

  glUseProgram(program);
  glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);

  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                        reinterpret_cast<void*>(2 * sizeof(float)));

  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

bool PreviewRenderer::UploadCamera(const CameraImageView& image) {
  if (!image.valid || image.planes[0].data == nullptr) return false;

  const ImagePlane& luma = image.planes[0];
  const ImagePlane& u = image.planes[1];
  const ImagePlane& v = image.planes[2];

  const int32_t chroma_width = image.width / 2;
  const int32_t chroma_height = image.height / 2;

  // Row padding is handled by the unpack row length rather than by copying
  // every row into a packed buffer, and the storage is allocated once rather
  // than on every frame. This runs at camera rate, and the loop it runs in has
  // thirty-three milliseconds for everything.
  const bool resized = image.width != camera_width_ ||
                       image.height != camera_height_;
  camera_width_ = image.width;
  camera_height_ = image.height;

  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, luma_texture_);
  glPixelStorei(GL_UNPACK_ROW_LENGTH, luma.row_stride);
  if (resized) {
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, image.width, image.height, 0, GL_RED,
                 GL_UNSIGNED_BYTE, luma.data);
  } else {
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, image.width, image.height, GL_RED,
                    GL_UNSIGNED_BYTE, luma.data);
  }

  // Semi-planar chroma is already the two-channel layout the shader wants, one
  // byte apart in one buffer, so it goes up untouched and the shader corrects
  // the order. Planar chroma has no such luck and is interleaved here.
  const uint8_t* chroma = nullptr;
  int32_t chroma_row_length = chroma_width;

  if (image.chroma_layout == ChromaLayout::kPlanar) {
    chroma_pixels_.resize(static_cast<size_t>(chroma_width) * chroma_height * 2);
    for (int32_t y = 0; y < chroma_height; ++y) {
      const uint8_t* u_row = u.data + static_cast<size_t>(y) * u.row_stride;
      const uint8_t* v_row = v.data + static_cast<size_t>(y) * v.row_stride;
      uint8_t* out = &chroma_pixels_[static_cast<size_t>(y) * chroma_width * 2];

      for (int32_t x = 0; x < chroma_width; ++x) {
        out[x * 2] = u_row[static_cast<size_t>(x) * u.pixel_stride];
        out[x * 2 + 1] = v_row[static_cast<size_t>(x) * v.pixel_stride];
      }
    }
    chroma = chroma_pixels_.data();
    swap_chroma_ = false;
  } else {
    // Whichever plane is first is where the buffer starts; the shader swaps the
    // channels when that is V.
    swap_chroma_ = image.chroma_layout == ChromaLayout::kSemiPlanarVFirst;
    chroma = swap_chroma_ ? v.data : u.data;
    chroma_row_length = u.row_stride / 2;
  }

  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, chroma_texture_);
  glPixelStorei(GL_UNPACK_ROW_LENGTH, chroma_row_length);
  if (resized) {
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RG8, chroma_width, chroma_height, 0,
                 GL_RG, GL_UNSIGNED_BYTE, chroma);
  } else {
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, chroma_width, chroma_height, GL_RG,
                    GL_UNSIGNED_BYTE, chroma);
  }

  glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

  camera_uploaded_ = true;
  return true;
}

bool PreviewRenderer::CameraRectContains(float x, float y) const {
  return x >= camera_left_ && x <= camera_right_ && y >= camera_top_ &&
         y <= camera_bottom_;
}

void PreviewRenderer::DrawCamera(int32_t sensor_orientation,
                                 float height_fraction) {
  if (!camera_uploaded_ || viewport_width_ <= 0 || viewport_height_ <= 0) return;

  glDisable(GL_DEPTH_TEST);
  glDisable(GL_BLEND);

  // Corners of the image, in the strip order the quad is drawn in, rotated so
  // the sensor's idea of up matches the screen's. Phones mount the sensor on
  // its side, so this is 90 degrees far more often than it is zero.
  constexpr float e = 1.0f;
  const float corners[4][8] = {
      {0, 1, e, 1, 0, 0, e, 0},  // 0
      {e, 1, e, 0, 0, 1, 0, 0},  // 90
      {e, 0, 0, 0, e, 1, 0, 1},  // 180
      {0, 0, 0, 1, e, 0, e, 1},  // 270
  };
  const int index = ((sensor_orientation % 360) + 360) % 360 / 90;
  const float* uvs = corners[index & 3];

  // Letterboxed rather than stretched: a preview that lies about the shape of
  // the frame is worse than one with bars, because framing is the whole point.
  const bool swapped = (index & 1) != 0;
  const float image_width =
      static_cast<float>(swapped ? camera_height_ : camera_width_);
  const float image_height =
      static_cast<float>(swapped ? camera_width_ : camera_height_);

  const float image_aspect = image_width / image_height;
  const float screen_aspect = static_cast<float>(viewport_width_) /
                              static_cast<float>(viewport_height_);

  // The area the picture is fitted into: the whole screen at 1, or a band
  // across the top of it below that.
  if (height_fraction > 1.0f) height_fraction = 1.0f;
  if (height_fraction < 0.05f) height_fraction = 0.05f;

  const float area_aspect = screen_aspect / height_fraction;

  float half_width = 1.0f;
  float half_height = 1.0f;
  if (image_aspect > area_aspect) {
    half_height = area_aspect / image_aspect;
  } else {
    half_width = image_aspect / area_aspect;
  }

  // Letterboxed inside the band, and the band pinned to the top of the screen.
  const float band_top = 1.0f;
  const float band_bottom = 1.0f - 2.0f * height_fraction;
  const float centre = (band_top + band_bottom) * 0.5f;
  const float span = (band_top - band_bottom) * 0.5f;

  const float top = centre + span * half_height;
  const float bottom = centre - span * half_height;

  camera_left_ = (1.0f - half_width) * 0.5f * viewport_width_;
  camera_right_ = (1.0f + half_width) * 0.5f * viewport_width_;
  camera_top_ = (1.0f - top) * 0.5f * viewport_height_;
  camera_bottom_ = (1.0f - bottom) * 0.5f * viewport_height_;

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, luma_texture_);
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, chroma_texture_);

  glUseProgram(camera_program_);
  glUniform1i(glGetUniformLocation(camera_program_, "u_luma"), 0);
  glUniform1i(glGetUniformLocation(camera_program_, "u_chroma"), 1);
  glUniform1i(glGetUniformLocation(camera_program_, "u_swap_chroma"),
              swap_chroma_ ? 1 : 0);

  DrawQuad(camera_program_, -half_width, bottom, half_width, top, uvs);
}

bool PreviewRenderer::LensButtonContains(float x, float y) const {
  return x >= lens_button_left_ && x <= lens_button_right_ &&
         y >= lens_button_top_ && y <= lens_button_bottom_;
}

bool PreviewRenderer::SessionsButtonContains(float x, float y) const {
  return x >= sessions_button_left_ && x <= sessions_button_right_ &&
         y >= sessions_button_top_ && y <= sessions_button_bottom_;
}

bool PreviewRenderer::CloseOverlayContains(float x, float y) const {
  return x >= close_button_left_ && x <= close_button_right_ &&
         y >= close_button_top_ && y <= close_button_bottom_;
}

void PreviewRenderer::DrawLensButton(const std::string& label, float top_fraction,
                                     bool enabled) {
  if (viewport_width_ <= 0 || viewport_height_ <= 0) return;

  constexpr float kWidth = 0.56f;
  constexpr float kLeft = (1.0f - kWidth) * 0.5f;
  constexpr float kHeight = 0.045f;

  const float button_width_px = static_cast<float>(viewport_width_) * kWidth;
  const float height_px = static_cast<float>(viewport_height_) * kHeight;

  const int columns = static_cast<int>(label.size()) + 2;
  const float by_width =
      button_width_px * 0.86f / static_cast<float>(columns * kCellWidth);
  const float by_height =
      height_px * 0.5f / static_cast<float>(kGlyphHeight);
  const float scale = by_width < by_height ? by_width : by_height;

  const float left = -1.0f + 2.0f * kLeft;
  const float right = left + 2.0f * kWidth;
  const float top = 1.0f - 2.0f * top_fraction;
  const float bottom = top - 2.0f * height_px / static_cast<float>(viewport_height_);

  lens_button_left_ = kLeft * viewport_width_;
  lens_button_right_ = lens_button_left_ + button_width_px;
  lens_button_top_ = top_fraction * viewport_height_;
  lens_button_bottom_ = lens_button_top_ + height_px;

  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  static constexpr float kFullUvs[8] = {0.0f, 1.0f, 1.0f, 1.0f,
                                        0.0f, 0.0f, 1.0f, 0.0f};

  glActiveTexture(GL_TEXTURE0);
  glUseProgram(quad_program_);
  glUniform1i(glGetUniformLocation(quad_program_, "u_texture"), 0);

  glBindTexture(GL_TEXTURE_2D, white_texture_);
  if (enabled) {
    glUniform4f(quad_color_location_, 0.16f, 0.16f, 0.18f, 0.95f);
  } else {
    glUniform4f(quad_color_location_, 0.09f, 0.09f, 0.09f, 0.95f);
  }
  DrawQuad(quad_program_, left, bottom, right, top, kFullUvs);

  RasterizeText({label}, columns);
  glBindTexture(GL_TEXTURE_2D, text_texture_);

  const float used = static_cast<float>(columns) / kTextColumns;
  const float row = 1.0f / kTextRows;
  const float label_uvs[8] = {0.0f, row, used, row, 0.0f, 0.0f, used, 0.0f};

  const float label_width =
      2.0f * static_cast<float>(columns * kCellWidth) * scale /
      static_cast<float>(viewport_width_);
  const float label_height =
      2.0f * static_cast<float>(kGlyphHeight) * scale /
      static_cast<float>(viewport_height_);
  const float centre_x = (left + right) * 0.5f;
  const float label_centre = (top + bottom) * 0.5f;

  glUniform4f(quad_color_location_, enabled ? 1.0f : 0.45f,
              enabled ? 1.0f : 0.45f, enabled ? 1.0f : 0.45f, 1.0f);
  DrawQuad(quad_program_, centre_x - label_width * 0.5f,
           label_centre - label_height * 0.5f, centre_x + label_width * 0.5f,
           label_centre + label_height * 0.5f, label_uvs);

  glDisable(GL_BLEND);
}

void PreviewRenderer::DrawSessionsButton(const std::string& label,
                                         float top_fraction, bool enabled) {
  if (viewport_width_ <= 0 || viewport_height_ <= 0) return;

  constexpr float kWidth = 0.56f;
  constexpr float kLeft = (1.0f - kWidth) * 0.5f;
  constexpr float kHeight = 0.045f;

  const float button_width_px = static_cast<float>(viewport_width_) * kWidth;
  const float height_px = static_cast<float>(viewport_height_) * kHeight;

  const int columns = static_cast<int>(label.size()) + 2;
  const float by_width =
      button_width_px * 0.86f / static_cast<float>(columns * kCellWidth);
  const float by_height =
      height_px * 0.5f / static_cast<float>(kGlyphHeight);
  const float scale = by_width < by_height ? by_width : by_height;

  const float left = -1.0f + 2.0f * kLeft;
  const float right = left + 2.0f * kWidth;
  const float top = 1.0f - 2.0f * top_fraction;
  const float bottom = top - 2.0f * height_px / static_cast<float>(viewport_height_);

  sessions_button_left_ = kLeft * viewport_width_;
  sessions_button_right_ = sessions_button_left_ + button_width_px;
  sessions_button_top_ = top_fraction * viewport_height_;
  sessions_button_bottom_ = sessions_button_top_ + height_px;

  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  static constexpr float kFullUvs[8] = {0.0f, 1.0f, 1.0f, 1.0f,
                                        0.0f, 0.0f, 1.0f, 0.0f};

  glActiveTexture(GL_TEXTURE0);
  glUseProgram(quad_program_);
  glUniform1i(glGetUniformLocation(quad_program_, "u_texture"), 0);

  glBindTexture(GL_TEXTURE_2D, white_texture_);
  if (enabled) {
    glUniform4f(quad_color_location_, 0.16f, 0.22f, 0.35f, 0.95f);
  } else {
    glUniform4f(quad_color_location_, 0.09f, 0.09f, 0.09f, 0.95f);
  }
  DrawQuad(quad_program_, left, bottom, right, top, kFullUvs);

  RasterizeText({label}, columns);
  glBindTexture(GL_TEXTURE_2D, text_texture_);

  const float used = static_cast<float>(columns) / kTextColumns;
  const float row = 1.0f / kTextRows;
  const float label_uvs[8] = {0.0f, row, used, row, 0.0f, 0.0f, used, 0.0f};

  const float label_width =
      2.0f * static_cast<float>(columns * kCellWidth) * scale /
      static_cast<float>(viewport_width_);
  const float label_height =
      2.0f * static_cast<float>(kGlyphHeight) * scale /
      static_cast<float>(viewport_height_);
  const float centre_x = (left + right) * 0.5f;
  const float label_centre = (top + bottom) * 0.5f;

  glUniform4f(quad_color_location_, enabled ? 1.0f : 0.45f,
              enabled ? 1.0f : 0.45f, enabled ? 1.0f : 0.45f, 1.0f);
  DrawQuad(quad_program_, centre_x - label_width * 0.5f,
           label_centre - label_height * 0.5f, centre_x + label_width * 0.5f,
           label_centre + label_height * 0.5f, label_uvs);

  glDisable(GL_BLEND);
}

int PreviewRenderer::ItemDeleteOverlayTouched(float x, float y) const {
  for (size_t i = 0; i < item_delete_rects_.size(); ++i) {
    const auto& r = item_delete_rects_[i];
    if (x >= r.left && x <= r.right && y >= r.top && y <= r.bottom) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

void PreviewRenderer::DrawSessionsOverlay(
    const std::vector<SessionRecorder::SessionItem>& sessions,
    int pending_delete_index) {
  if (viewport_width_ <= 0 || viewport_height_ <= 0) return;

  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  static constexpr float kFullUvs[8] = {0.0f, 1.0f, 1.0f, 1.0f,
                                        0.0f, 0.0f, 1.0f, 0.0f};

  glActiveTexture(GL_TEXTURE0);
  glUseProgram(quad_program_);
  glUniform1i(glGetUniformLocation(quad_program_, "u_texture"), 0);

  // Dark semi-transparent background overlay covering full screen
  glBindTexture(GL_TEXTURE_2D, white_texture_);
  glUniform4f(quad_color_location_, 0.02f, 0.02f, 0.04f, 0.92f);
  DrawQuad(quad_program_, -1.0f, -1.0f, 1.0f, 1.0f, kFullUvs);

  // Inner dialog box
  constexpr float kDialogTopFrac = 0.08f;
  constexpr float kDialogBottomFrac = 0.92f;
  const float d_top = 1.0f - 2.0f * kDialogTopFrac;
  const float d_bottom = 1.0f - 2.0f * kDialogBottomFrac;
  const float d_left = -0.92f;
  const float d_right = 0.92f;

  glUniform4f(quad_color_location_, 0.12f, 0.14f, 0.20f, 0.98f);
  DrawQuad(quad_program_, d_left, d_bottom, d_right, d_top, kFullUvs);

  item_delete_rects_.clear();

  // Render header
  std::vector<std::string> header = {"=== SAVED SESSIONS ==="};
  RasterizeText(header, 22);
  glBindTexture(GL_TEXTURE_2D, text_texture_);

  const float h_used = 22.0f / kTextColumns;
  const float h_row = 1.0f / kTextRows;
  const float h_uvs[8] = {0.0f, h_row, h_used, h_row, 0.0f, 0.0f, h_used, 0.0f};

  const float text_w_px = static_cast<float>(viewport_width_) * 0.84f;
  const float h_scale = text_w_px / static_cast<float>(22 * kCellWidth);
  const float h_height_px = static_cast<float>(kCellHeight) * h_scale;
  const float h_top = d_top - 0.04f;
  const float h_bottom = h_top - 2.0f * h_height_px / static_cast<float>(viewport_height_);

  glUniform4f(quad_color_location_, 1.0f, 1.0f, 1.0f, 1.0f);
  DrawQuad(quad_program_, -0.84f, h_bottom, 0.84f, h_top, h_uvs);

  if (sessions.empty()) {
    std::vector<std::string> empty_msg = {"NO SESSIONS FOUND"};
    RasterizeText(empty_msg, 17);
    glBindTexture(GL_TEXTURE_2D, text_texture_);
    const float e_used = 17.0f / kTextColumns;
    const float e_uvs[8] = {0.0f, h_row, e_used, h_row, 0.0f, 0.0f, e_used, 0.0f};
    const float e_top = h_bottom - 0.10f;
    const float e_bottom = e_top - 2.0f * h_height_px / static_cast<float>(viewport_height_);
    DrawQuad(quad_program_, -0.80f, e_bottom, 0.80f, e_top, e_uvs);
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

      RasterizeText(s_line, s_cols);
      glBindTexture(GL_TEXTURE_2D, text_texture_);
      const float s_used = static_cast<float>(s_cols) / kTextColumns;
      const float s_uvs[8] = {0.0f, h_row, s_used, h_row, 0.0f, 0.0f, s_used, 0.0f};

      const float s_box_w = static_cast<float>(viewport_width_) * 0.54f;
      const float s_box_h = static_cast<float>(viewport_height_) * kRowH * 0.5f;
      const float s_sw = s_box_w * 0.95f / static_cast<float>(s_cols * kCellWidth);
      const float s_sh = s_box_h * 0.70f / static_cast<float>(kGlyphHeight);
      const float s_sc = s_sw < s_sh ? s_sw : s_sh;

      const float s_lw = 2.0f * static_cast<float>(s_cols * kCellWidth) * s_sc / static_cast<float>(viewport_width_);
      const float s_lh = 2.0f * static_cast<float>(kGlyphHeight) * s_sc / static_cast<float>(viewport_height_);
      const float s_left = -0.88f;
      const float s_cy = (row_top + row_bottom) * 0.5f;

      glUniform4f(quad_color_location_, 1.0f, 1.0f, 1.0f, 1.0f);
      DrawQuad(quad_program_, s_left, s_cy - s_lh * 0.5f, s_left + s_lw, s_cy + s_lh * 0.5f, s_uvs);

      // Draw individual delete button next to session item
      const bool is_pending = (pending_delete_index == i);
      const std::string btn_label = is_pending ? "[CONFIRM?]" : "[DEL]";
      const float btn_left = 0.25f;
      const float btn_right = 0.88f;

      ItemRect rect;
      rect.left = (btn_left + 1.0f) * 0.5f * viewport_width_;
      rect.right = (btn_right + 1.0f) * 0.5f * viewport_width_;
      rect.top = (1.0f - row_top) * 0.5f * viewport_height_;
      rect.bottom = (1.0f - row_bottom) * 0.5f * viewport_height_;
      item_delete_rects_.push_back(rect);

      glBindTexture(GL_TEXTURE_2D, white_texture_);
      if (is_pending) {
        glUniform4f(quad_color_location_, 0.85f, 0.15f, 0.15f, 1.0f);
      } else {
        glUniform4f(quad_color_location_, 0.45f, 0.18f, 0.18f, 0.90f);
      }
      DrawQuad(quad_program_, btn_left, row_bottom, btn_right, row_top, kFullUvs);

      const int b_cols = static_cast<int>(btn_label.size());
      RasterizeText({btn_label}, b_cols);
      glBindTexture(GL_TEXTURE_2D, text_texture_);
      const float b_used = static_cast<float>(b_cols) / kTextColumns;
      const float b_uvs[8] = {0.0f, h_row, b_used, h_row, 0.0f, 0.0f, b_used, 0.0f};

      const float b_box_w = static_cast<float>(viewport_width_) * (btn_right - btn_left) * 0.5f;
      const float b_box_h = static_cast<float>(viewport_height_) * kRowH * 0.5f;
      const float b_sw = b_box_w * 0.85f / static_cast<float>(b_cols * kCellWidth);
      const float b_sh = b_box_h * 0.65f / static_cast<float>(kGlyphHeight);
      const float b_sc = b_sw < b_sh ? b_sw : b_sh;

      const float b_lw = 2.0f * static_cast<float>(b_cols * kCellWidth) * b_sc / static_cast<float>(viewport_width_);
      const float b_lh = 2.0f * static_cast<float>(kGlyphHeight) * b_sc / static_cast<float>(viewport_height_);
      const float b_cx = (btn_left + btn_right) * 0.5f;
      const float b_cy = (row_top + row_bottom) * 0.5f;

      glUniform4f(quad_color_location_, 1.0f, 1.0f, 1.0f, 1.0f);
      DrawQuad(quad_program_, b_cx - b_lw * 0.5f, b_cy - b_lh * 0.5f,
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

  close_button_left_ = (close_left + 1.0f) * 0.5f * viewport_width_;
  close_button_right_ = (close_right + 1.0f) * 0.5f * viewport_width_;
  close_button_top_ = (1.0f - close_top) * 0.5f * viewport_height_;
  close_button_bottom_ = (1.0f - close_bottom) * 0.5f * viewport_height_;

  glBindTexture(GL_TEXTURE_2D, white_texture_);
  glUniform4f(quad_color_location_, 0.25f, 0.28f, 0.35f, 1.0f);
  DrawQuad(quad_program_, close_left, close_bottom, close_right, close_top, kFullUvs);

  RasterizeText({"[ CLOSE ]"}, 9);
  glBindTexture(GL_TEXTURE_2D, text_texture_);

  const float c_used = 9.0f / kTextColumns;
  const float c_row = 1.0f / kTextRows;
  const float c_uvs[8] = {0.0f, c_row, c_used, c_row, 0.0f, 0.0f, c_used, 0.0f};

  const float c_box_w = static_cast<float>(viewport_width_) * kBtnW * 0.5f;
  const float c_box_h = static_cast<float>(viewport_height_) * kBtnH * 0.5f;
  const float c_sw = c_box_w * 0.85f / static_cast<float>(9 * kCellWidth);
  const float c_sh = c_box_h * 0.65f / static_cast<float>(kGlyphHeight);
  const float c_sc = c_sw < c_sh ? c_sw : c_sh;

  const float c_lw = 2.0f * static_cast<float>(9 * kCellWidth) * c_sc / static_cast<float>(viewport_width_);
  const float c_lh = 2.0f * static_cast<float>(kGlyphHeight) * c_sc / static_cast<float>(viewport_height_);
  const float c_cx = (close_left + close_right) * 0.5f;
  const float c_cy = (close_top + close_bottom) * 0.5f;

  glUniform4f(quad_color_location_, 1.0f, 1.0f, 1.0f, 1.0f);
  DrawQuad(quad_program_, c_cx - c_lw * 0.5f, c_cy - c_lh * 0.5f,
           c_cx + c_lw * 0.5f, c_cy + c_lh * 0.5f, c_uvs);

  glDisable(GL_BLEND);
}

void PreviewRenderer::RasterizeText(const std::vector<std::string>& lines,
                                    int columns) {
  std::memset(text_pixels_.data(), 0, text_pixels_.size());

  const int rows = static_cast<int>(lines.size()) < kTextRows
                       ? static_cast<int>(lines.size())
                       : kTextRows;

  for (int row = 0; row < rows; ++row) {
    const std::string& line = lines[static_cast<size_t>(row)];
    const int length = static_cast<int>(line.size()) < columns
                           ? static_cast<int>(line.size())
                           : columns;

    // Centred within the width being drawn, rather than run up against the
    // left edge. Lines of different lengths otherwise read as ragged.
    const int indent = (columns - length) / 2;

    for (int i = 0; i < length; ++i) {
      const int column = indent + i;
      const uint8_t* glyph = FindGlyph(line[static_cast<size_t>(i)]);
      if (glyph == nullptr) continue;

      for (int gx = 0; gx < kGlyphWidth; ++gx) {
        for (int gy = 0; gy < kGlyphHeight; ++gy) {
          if ((glyph[gx] & (1u << gy)) == 0) continue;
          const int x = column * kCellWidth + gx;
          const int y = row * kCellHeight + gy;
          text_pixels_[static_cast<size_t>(y) * kTextWidth + x] = 0xFF;
        }
      }
    }
  }

  glBindTexture(GL_TEXTURE_2D, text_texture_);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, kTextWidth, kTextHeight, GL_RED,
                  GL_UNSIGNED_BYTE, text_pixels_.data());
}

float PreviewRenderer::StatusHeightFraction(int columns, int rows) const {
  if (viewport_width_ <= 0 || viewport_height_ <= 0) return 0.0f;
  if (columns < 1) columns = 1;
  if (rows > kTextRows) rows = kTextRows;

  const float scale = static_cast<float>(viewport_width_) * 0.96f /
                      static_cast<float>(columns * kCellWidth);
  return static_cast<float>(rows * kCellHeight) * scale /
         static_cast<float>(viewport_height_);
}

float PreviewRenderer::DrawStatus(const std::vector<std::string>& lines,
                                  bool recording, float top_fraction,
                                  int columns) {
  if (viewport_width_ <= 0 || viewport_height_ <= 0) return top_fraction;
  if (columns < 1) columns = 1;
  if (columns > kTextColumns) columns = kTextColumns;

  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  // Fewer columns across the same width means larger glyphs. Only that many are
  // sampled out of the grid, so lines are written to fit rather than scaled to.
  //
  // Inset from the edges: text that starts in the very first column of a phone
  // screen reads as though it has been cut off, and the curved corners of one
  // will eat it.
  constexpr float kInset = 0.04f;
  const float text_width_px =
      static_cast<float>(viewport_width_) * (1.0f - kInset);

  const int rows = static_cast<int>(lines.size()) < kTextRows
                       ? static_cast<int>(lines.size())
                       : kTextRows;

  const float used = static_cast<float>(columns) / kTextColumns;
  const float used_rows = static_cast<float>(rows) / kTextRows;
  const float scale = text_width_px / static_cast<float>(columns * kCellWidth);
  const float panel_height_px =
      static_cast<float>(rows * kCellHeight) * scale;

  const float panel_top = 1.0f - 2.0f * top_fraction;
  const float panel_bottom =
      panel_top - 2.0f * panel_height_px / static_cast<float>(viewport_height_);

  const float left = -1.0f + kInset;
  const float right = 1.0f - kInset;

  const float kFullUvs[8] = {0.0f,      used_rows, used, used_rows,
                             0.0f,      0.0f,      used, 0.0f};

  glActiveTexture(GL_TEXTURE0);
  glUseProgram(quad_program_);
  glUniform1i(glGetUniformLocation(quad_program_, "u_texture"), 0);

  // A dark panel behind the text: white glyphs over a bright scene are
  // unreadable, which is exactly the scene a capture is usually pointed at.
  glBindTexture(GL_TEXTURE_2D, white_texture_);
  glUniform4f(quad_color_location_, 0.0f, 0.0f, 0.0f, 0.55f);
  DrawQuad(quad_program_, -1.0f, panel_bottom, 1.0f, panel_top, kFullUvs);

  RasterizeText(lines, columns);
  glUniform4f(quad_color_location_, 1.0f, 1.0f, 1.0f, 1.0f);
  DrawQuad(quad_program_, left, panel_bottom, right, panel_top, kFullUvs);

  // A marker that reads at arm's length without focusing on the text.
  // On the first line, at its right edge, so it reads as part of the state
  // rather than as something floating on its own.
  const float line_px = static_cast<float>(kCellHeight) * scale;
  const float marker_w = 2.0f * line_px * 0.7f / static_cast<float>(viewport_width_);
  const float marker_h = 2.0f * line_px * 0.7f / static_cast<float>(viewport_height_);
  const float line_bottom =
      panel_top - 2.0f * line_px / static_cast<float>(viewport_height_);

  glBindTexture(GL_TEXTURE_2D, white_texture_);
  if (recording) {
    glUniform4f(quad_color_location_, 0.9f, 0.1f, 0.1f, 1.0f);
  } else {
    glUniform4f(quad_color_location_, 0.3f, 0.3f, 0.3f, 1.0f);
  }
  DrawQuad(quad_program_, right - marker_w, line_bottom, right,
           line_bottom + marker_h, kFullUvs);

  glDisable(GL_BLEND);

  return (1.0f - panel_bottom) * 0.5f;
}

}  // namespace sensor_logger
