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

}  // namespace sensor_logger
