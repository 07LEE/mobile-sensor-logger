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
constexpr int kTextRows = 6;
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
in vec2 v_uv;
out vec4 o_color;
void main() {
  float y = texture(u_luma, v_uv).r;
  vec2 uv = texture(u_chroma, v_uv).rg - vec2(0.5, 0.5);
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
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

  // The row stride is the width of the upload, and the extra columns are
  // trimmed by the texture coordinates rather than by copying every row into a
  // packed buffer first.
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, luma_texture_);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, luma.row_stride, image.height, 0,
               GL_RED, GL_UNSIGNED_BYTE, luma.data);

  // Chroma is packed here rather than uploaded in place. The planes arrive
  // planar on some devices and interleaved on others, in either order, and one
  // packed buffer is cheaper than three shaders. At preview size this is a few
  // hundred kilobytes.
  const int32_t chroma_width = image.width / 2;
  const int32_t chroma_height = image.height / 2;
  chroma_pixels_.resize(static_cast<size_t>(chroma_width) * chroma_height * 2);

  const ImagePlane& u = image.planes[1];
  const ImagePlane& v = image.planes[2];
  for (int32_t y = 0; y < chroma_height; ++y) {
    const uint8_t* u_row = u.data + static_cast<size_t>(y) * u.row_stride;
    const uint8_t* v_row = v.data + static_cast<size_t>(y) * v.row_stride;
    uint8_t* out = &chroma_pixels_[static_cast<size_t>(y) * chroma_width * 2];

    for (int32_t x = 0; x < chroma_width; ++x) {
      out[x * 2] = u_row[static_cast<size_t>(x) * u.pixel_stride];
      out[x * 2 + 1] = v_row[static_cast<size_t>(x) * v.pixel_stride];
    }
  }

  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, chroma_texture_);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RG8, chroma_width, chroma_height, 0, GL_RG,
               GL_UNSIGNED_BYTE, chroma_pixels_.data());

  camera_width_ = image.width;
  camera_height_ = image.height;
  // Luma was uploaded stride-wide, so its right edge is padding.
  luma_edge_ = static_cast<float>(image.width) /
               static_cast<float>(luma.row_stride > 0 ? luma.row_stride : 1);
  camera_uploaded_ = true;
  return true;
}

void PreviewRenderer::DrawCamera(int32_t sensor_orientation) {
  if (!camera_uploaded_ || viewport_width_ <= 0 || viewport_height_ <= 0) return;

  glDisable(GL_DEPTH_TEST);
  glDisable(GL_BLEND);

  // Corners of the image, in the strip order the quad is drawn in, rotated so
  // the sensor's idea of up matches the screen's. Phones mount the sensor on
  // its side, so this is 90 degrees far more often than it is zero.
  const float e = luma_edge_;
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

  float half_width = 1.0f;
  float half_height = 1.0f;
  if (image_aspect > screen_aspect) {
    half_height = screen_aspect / image_aspect;
  } else {
    half_width = image_aspect / screen_aspect;
  }

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, luma_texture_);
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, chroma_texture_);

  glUseProgram(camera_program_);
  glUniform1i(glGetUniformLocation(camera_program_, "u_luma"), 0);
  glUniform1i(glGetUniformLocation(camera_program_, "u_chroma"), 1);

  DrawQuad(camera_program_, -half_width, -half_height, half_width, half_height,
           uvs);
}

void PreviewRenderer::RasterizeText(const std::vector<std::string>& lines) {
  std::memset(text_pixels_.data(), 0, text_pixels_.size());

  const int rows = static_cast<int>(lines.size()) < kTextRows
                       ? static_cast<int>(lines.size())
                       : kTextRows;

  for (int row = 0; row < rows; ++row) {
    const std::string& line = lines[static_cast<size_t>(row)];
    const int columns = static_cast<int>(line.size()) < kTextColumns
                            ? static_cast<int>(line.size())
                            : kTextColumns;

    for (int column = 0; column < columns; ++column) {
      const uint8_t* glyph = FindGlyph(line[static_cast<size_t>(column)]);
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

void PreviewRenderer::DrawStatus(const std::vector<std::string>& lines,
                                 bool recording) {
  if (viewport_width_ <= 0 || viewport_height_ <= 0) return;

  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  // Each glyph cell is scaled so the grid spans the width of the screen, which
  // keeps the readout legible on any display without a size to tune.
  const float scale =
      static_cast<float>(viewport_width_) / static_cast<float>(kTextWidth);
  const float panel_height_px = static_cast<float>(kTextHeight) * scale;
  const float panel_bottom =
      1.0f - 2.0f * panel_height_px / static_cast<float>(viewport_height_);

  static constexpr float kFullUvs[8] = {0.0f, 1.0f, 1.0f, 1.0f,
                                        0.0f, 0.0f, 1.0f, 0.0f};

  glActiveTexture(GL_TEXTURE0);
  glUseProgram(quad_program_);
  glUniform1i(glGetUniformLocation(quad_program_, "u_texture"), 0);

  // A dark panel behind the text: white glyphs over a bright scene are
  // unreadable, which is exactly the scene a capture is usually pointed at.
  glBindTexture(GL_TEXTURE_2D, white_texture_);
  glUniform4f(quad_color_location_, 0.0f, 0.0f, 0.0f, 0.55f);
  DrawQuad(quad_program_, -1.0f, panel_bottom, 1.0f, 1.0f, kFullUvs);

  RasterizeText(lines);
  glUniform4f(quad_color_location_, 1.0f, 1.0f, 1.0f, 1.0f);
  DrawQuad(quad_program_, -1.0f, panel_bottom, 1.0f, 1.0f, kFullUvs);

  // A marker that reads at arm's length without focusing on the text.
  const float marker = 2.0f * 24.0f * scale / static_cast<float>(viewport_width_);
  const float marker_y =
      2.0f * 24.0f * scale / static_cast<float>(viewport_height_);
  glBindTexture(GL_TEXTURE_2D, white_texture_);
  if (recording) {
    glUniform4f(quad_color_location_, 0.9f, 0.1f, 0.1f, 1.0f);
  } else {
    glUniform4f(quad_color_location_, 0.4f, 0.4f, 0.4f, 1.0f);
  }
  DrawQuad(quad_program_, 1.0f - marker - 0.02f, panel_bottom - marker_y - 0.02f,
           1.0f - 0.02f, panel_bottom - 0.02f, kFullUvs);

  glDisable(GL_BLEND);
}

}  // namespace sensor_logger
