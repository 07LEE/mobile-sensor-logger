#ifndef SENSOR_LOGGER_GL_QUAD_H
#define SENSOR_LOGGER_GL_QUAD_H

#include <GLES3/gl3.h>

namespace sensor_logger {

// Layout of the fixed-grid bitmap font texture that PreviewRenderer
// rasterises text into. Every widget that draws a label against that texture
// needs these to convert a glyph count into UVs and screen size.
constexpr int kGlyphHeight = 7;
constexpr int kCellWidth = 6;  // one column of spacing
constexpr int kCellHeight = 8;
constexpr int kTextColumns = 40;
constexpr int kTextRows = 8;

// Uploads one textured quad's vertices to `vbo` and draws it with `program`
// bound. `uvs` is 4 (u, v) pairs matching the vertex order: bottom-left,
// bottom-right, top-left, top-right.
void DrawQuad(GLuint program, GLuint vbo, float x0, float y0, float x1,
              float y1, const float* uvs);

}  // namespace sensor_logger

#endif  // SENSOR_LOGGER_GL_QUAD_H
