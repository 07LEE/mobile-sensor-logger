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
                                 float height_fraction,
                                 float top_fraction) {
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

  // The area the picture is fitted into: bounded by top_fraction and height_fraction.
  if (height_fraction > 1.0f) height_fraction = 1.0f;
  if (height_fraction < 0.05f) height_fraction = 0.05f;
  if (top_fraction < 0.0f) top_fraction = 0.0f;
  if (top_fraction > 0.95f) top_fraction = 0.95f;

  const float area_aspect = screen_aspect / height_fraction;

  float half_width = 1.0f;
  float half_height = 1.0f;
  if (image_aspect > area_aspect) {
    half_height = area_aspect / image_aspect;
  } else {
    half_width = image_aspect / area_aspect;
  }

  // Letterboxed inside the band, positioned at top_fraction down the screen.
  const float band_top = 1.0f - 2.0f * top_fraction;
  const float band_bottom = 1.0f - 2.0f * (top_fraction + height_fraction);
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
  return lens_button_.Contains(x, y);
}

bool PreviewRenderer::SessionsButtonContains(float x, float y) const {
  return sessions_button_.Contains(x, y);
}

bool PreviewRenderer::CloseOverlayContains(float x, float y) const {
  return sessions_overlay_.CloseTouched(x, y);
}

int PreviewRenderer::ItemDeleteOverlayTouched(float x, float y) const {
  return sessions_overlay_.ItemDeleteTouched(x, y);
}

void PreviewRenderer::DrawLensButton(const std::string& label, float top_fraction,
                                     bool enabled) {
  lens_button_.Draw(
      quad_program_, white_texture_, text_texture_, quad_color_location_, vbo_,
      label, top_fraction, 0.56f, 0.045f, 0.16f, 0.16f, 0.18f, 0.95f,
      enabled, viewport_width_, viewport_height_,
      [this](const std::vector<std::string>& lines, int columns) {
        RasterizeText(lines, columns);
      });
}

void PreviewRenderer::DrawSessionsButton(const std::string& label,
                                         float top_fraction, bool enabled) {
  sessions_button_.Draw(
      quad_program_, white_texture_, text_texture_, quad_color_location_, vbo_,
      label, top_fraction, 0.56f, 0.045f, 0.16f, 0.22f, 0.35f, 0.95f,
      enabled, viewport_width_, viewport_height_,
      [this](const std::vector<std::string>& lines, int columns) {
        RasterizeText(lines, columns);
      });
}

void PreviewRenderer::DrawSessionsOverlay(
    const std::vector<SessionItem>& sessions,
    int pending_delete_index) {
  sessions_overlay_.Draw(
      quad_program_, white_texture_, text_texture_, quad_color_location_, vbo_,
      sessions, pending_delete_index, viewport_width_, viewport_height_,
      [this](const std::vector<std::string>& lines, int columns) {
        RasterizeText(lines, columns);
      });
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
