#include "sharpness.h"

namespace sensor_logger {

float LumaSharpness(const uint8_t* luma, int32_t width, int32_t height,
                    int32_t stride, int32_t step, int32_t radius) {
  if (luma == nullptr || step <= 0 || radius <= 0) return 0.0f;
  if (width <= 2 * radius || height <= 2 * radius) return 0.0f;

  // Running sums in double: at capture resolutions the squared term overflows
  // float's precision well before the loop ends.
  double sum = 0.0;
  double sum_squares = 0.0;
  int64_t count = 0;

  // The border skipped is the neighbourhood radius, not the sampling step —
  // the two used to be the same variable, which meant a coarser `step` also
  // widened the Laplacian's neighbour distance.
  for (int32_t y = radius; y < height - radius; y += step) {
    const uint8_t* row = luma + static_cast<int64_t>(y) * stride;
    const uint8_t* above = row - static_cast<int64_t>(radius) * stride;
    const uint8_t* below = row + static_cast<int64_t>(radius) * stride;

    for (int32_t x = radius; x < width - radius; x += step) {
      const int32_t laplacian = static_cast<int32_t>(row[x - radius]) +
                                row[x + radius] + above[x] + below[x] -
                                4 * static_cast<int32_t>(row[x]);

      sum += laplacian;
      sum_squares += static_cast<double>(laplacian) * laplacian;
      ++count;
    }
  }

  if (count < 2) return 0.0f;

  const double mean = sum / static_cast<double>(count);
  const double variance = sum_squares / static_cast<double>(count) - mean * mean;
  return variance > 0.0 ? static_cast<float>(variance) : 0.0f;
}

}  // namespace sensor_logger
