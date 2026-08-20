#include "sharpness.h"

namespace sensor_logger {

float LumaSharpness(const uint8_t* luma, int32_t width, int32_t height,
                    int32_t stride, int32_t step) {
  if (luma == nullptr || width <= 2 || height <= 2 || step <= 0) return 0.0f;

  // Running sums in double: at capture resolutions the squared term overflows
  // float's precision well before the loop ends.
  double sum = 0.0;
  double sum_squares = 0.0;
  int64_t count = 0;

  // The border is skipped because the 4-neighbour kernel needs one pixel on
  // every side.
  for (int32_t y = step; y < height - step; y += step) {
    const uint8_t* row = luma + static_cast<int64_t>(y) * stride;
    const uint8_t* above = row - static_cast<int64_t>(step) * stride;
    const uint8_t* below = row + static_cast<int64_t>(step) * stride;

    for (int32_t x = step; x < width - step; x += step) {
      const int32_t laplacian = static_cast<int32_t>(row[x - step]) +
                                row[x + step] + above[x] + below[x] -
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
