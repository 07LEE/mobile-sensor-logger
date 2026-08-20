#ifndef SENSOR_LOGGER_SHARPNESS_H
#define SENSOR_LOGGER_SHARPNESS_H

#include <cstdint>

namespace sensor_logger {

// Relative sharpness of a luma plane: the variance of its Laplacian.
//
// Blur suppresses high spatial frequencies, so a defocused or motion-smeared
// frame produces small second derivatives and a low variance. The number has no
// absolute meaning — it shifts with scene content and exposure — so it is only
// worth comparing between frames of the same scene taken moments apart, which
// is exactly the comparison the capture needs.
//
// Only the luma plane is read: chroma is subsampled and contributes little
// edge detail, and at capture resolutions the work has to stay cheap enough to
// run on every frame.
//
// `stride` is the plane's row stride, which is not the same as `width`.
// `step` subsamples both axes; 1 reads every pixel.
float LumaSharpness(const uint8_t* luma, int32_t width, int32_t height,
                    int32_t stride, int32_t step);

}  // namespace sensor_logger

#endif  // SENSOR_LOGGER_SHARPNESS_H
