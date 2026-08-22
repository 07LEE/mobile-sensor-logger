#ifndef SENSOR_LOGGER_GL_QUAD_H
#define SENSOR_LOGGER_GL_QUAD_H

#include <GLES3/gl3.h>

#include <functional>
#include <string>
#include <vector>

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

// Where DrawScaledLabel positions the label horizontally: at `anchor_x` sits
// its centre, or its left edge.
enum class LabelAnchor { kCenter, kLeft };

// Rasterises `label` (already known to be `columns` glyphs) via
// `rasterize_text_fn`, then draws it as one quad against `text_texture`,
// scaled by whichever of width or height is tighter to fit within a
// `box_width_px` x `box_height_px` area — each shrunk first by its own fill
// fraction (< 1.0) to leave padding — and vertically centered on `center_y`.
//
// The width side of that fit is computed against `scale_columns` rather than
// `columns` itself. Pass `columns` again there for a one-off label sized to
// its own box; pass a shared constant across a set of buttons instead so a
// short label ("LOCK") isn't blown up larger than a long one ("RETENTION
// SHARP") just because it had more width to spare — every label in the set
// renders at the same glyph size, and a short one simply doesn't fill as
// much of the box.
void DrawScaledLabel(GLuint program, GLuint vbo, GLuint text_texture,
                     GLint quad_color_location, const std::string& label,
                     int columns, int scale_columns, float box_width_px,
                     float box_height_px, float width_fill, float height_fill,
                     LabelAnchor anchor, float anchor_x, float center_y,
                     float viewport_width, float viewport_height, float red,
                     float green, float blue, float alpha,
                     const std::function<void(const std::vector<std::string>&, int)>&
                         rasterize_text_fn);

}  // namespace sensor_logger

#endif  // SENSOR_LOGGER_GL_QUAD_H
