#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "frame_motion.h"
#include "sharpness.h"

namespace {

using sensor_logger::CameraImageView;
using sensor_logger::FrameMotion;
using sensor_logger::LumaSharpness;

constexpr int32_t kWidth = 128;
constexpr int32_t kHeight = 96;

class TestImage {
 public:
  TestImage(int32_t width, int32_t height)
      : width_(width), height_(height), pixels_(static_cast<size_t>(width) * height) {}

  static TestImage Pattern(int32_t width = kWidth, int32_t height = kHeight) {
    TestImage image(width, height);
    for (int32_t y = 0; y < height; ++y) {
      for (int32_t x = 0; x < width; ++x) {
        const uint32_t hash = static_cast<uint32_t>(x) * 73856093u ^
                              static_cast<uint32_t>(y) * 19349663u ^
                              static_cast<uint32_t>(x * y) * 83492791u;
        image.At(x, y) = static_cast<uint8_t>((hash >> 8) & 0xffu);
      }
    }
    return image;
  }

  CameraImageView View() const {
    CameraImageView view;
    view.valid = true;
    view.width = width_;
    view.height = height_;
    view.num_planes = 1;
    view.planes[0].data = pixels_.data();
    view.planes[0].length = static_cast<int32_t>(pixels_.size());
    view.planes[0].row_stride = width_;
    view.planes[0].pixel_stride = 1;
    return view;
  }

  uint8_t& At(int32_t x, int32_t y) {
    return pixels_[static_cast<size_t>(y) * width_ + x];
  }

  uint8_t At(int32_t x, int32_t y) const {
    if (x < 0 || x >= width_ || y < 0 || y >= height_) return 0;
    return pixels_[static_cast<size_t>(y) * width_ + x];
  }

  int32_t width() const { return width_; }
  int32_t height() const { return height_; }
  const std::vector<uint8_t>& pixels() const { return pixels_; }

 private:
  int32_t width_;
  int32_t height_;
  std::vector<uint8_t> pixels_;
};

TestImage Translate(const TestImage& source, int32_t dx, int32_t dy) {
  TestImage result(source.width(), source.height());
  for (int32_t y = 0; y < result.height(); ++y) {
    for (int32_t x = 0; x < result.width(); ++x) {
      result.At(x, y) = source.At(x + dx, y + dy);
    }
  }
  return result;
}

TestImage AddBrightness(const TestImage& source, int32_t amount) {
  TestImage result(source.width(), source.height());
  for (int32_t y = 0; y < result.height(); ++y) {
    for (int32_t x = 0; x < result.width(); ++x) {
      result.At(x, y) = static_cast<uint8_t>(
          std::clamp(static_cast<int32_t>(source.At(x, y)) + amount, 0, 255));
    }
  }
  return result;
}

TestImage Rotate(const TestImage& source, float degrees) {
  TestImage result(source.width(), source.height());
  const float radians = degrees * 3.14159265358979323846f / 180.0f;
  const float cosine = std::cos(radians);
  const float sine = std::sin(radians);
  const float cx = (source.width() - 1) * 0.5f;
  const float cy = (source.height() - 1) * 0.5f;
  for (int32_t y = 0; y < result.height(); ++y) {
    for (int32_t x = 0; x < result.width(); ++x) {
      const float rx = x - cx;
      const float ry = y - cy;
      const int32_t sx = static_cast<int32_t>(std::lround(cosine * rx + sine * ry + cx));
      const int32_t sy = static_cast<int32_t>(std::lround(-sine * rx + cosine * ry + cy));
      result.At(x, y) = source.At(sx, sy);
    }
  }
  return result;
}

TestImage Scale(const TestImage& source, float factor) {
  TestImage result(source.width(), source.height());
  const float cx = (source.width() - 1) * 0.5f;
  const float cy = (source.height() - 1) * 0.5f;
  for (int32_t y = 0; y < result.height(); ++y) {
    for (int32_t x = 0; x < result.width(); ++x) {
      const int32_t sx = static_cast<int32_t>(std::lround((x - cx) / factor + cx));
      const int32_t sy = static_cast<int32_t>(std::lround((y - cy) / factor + cy));
      result.At(x, y) = source.At(sx, sy);
    }
  }
  return result;
}

TestImage BoxBlur(const TestImage& source, int32_t radius) {
  TestImage result(source.width(), source.height());
  for (int32_t y = 0; y < result.height(); ++y) {
    for (int32_t x = 0; x < result.width(); ++x) {
      int32_t sum = 0;
      int32_t count = 0;
      for (int32_t by = -radius; by <= radius; ++by) {
        for (int32_t bx = -radius; bx <= radius; ++bx) {
          const int32_t sx = std::clamp(x + bx, 0, source.width() - 1);
          const int32_t sy = std::clamp(y + by, 0, source.height() - 1);
          sum += source.At(sx, sy);
          ++count;
        }
      }
      result.At(x, y) = static_cast<uint8_t>(sum / count);
    }
  }
  return result;
}

void Require(bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

void RequireNear(float actual, float expected, float tolerance,
                 const std::string& message) {
  if (std::fabs(actual - expected) > tolerance) {
    std::cerr << "FAIL: " << message << " (actual=" << actual
              << ", expected=" << expected << ")\n";
    std::exit(1);
  }
}

void TestUnchangedFrameIsRejected() {
  const TestImage image = TestImage::Pattern();
  FrameMotion motion;
  Require(motion.Accept(image.View()), "first frame should establish a reference");
  Require(!motion.Accept(image.View()), "unchanged frame should remain in the stretch");
  RequireNear(motion.last_shift(), 0.0f, 1e-6f, "unchanged frame shift");
  RequireNear(motion.last_residual(), 0.0f, 1e-6f, "unchanged frame residual");
  Require(motion.rejected() == 1, "unchanged frame should increment rejected count");
}

void TestHorizontalSearchLimit() {
  const TestImage reference = TestImage::Pattern();
  const TestImage shifted = Translate(reference, 20, 0);
  FrameMotion motion;
  Require(motion.Accept(reference.View()), "first frame should establish a reference");
  Require(motion.Accept(shifted.View()),
          "a shift at the search limit should still close the default stretch");
  RequireNear(motion.last_shift(), FrameMotion::kMaxShift, 1e-6f,
              "horizontal search should saturate at the maximum measurable shift");
  RequireNear(motion.last_residual(), 0.0f, 1e-6f,
              "translated overlap should align exactly at the search limit");
}

void TestDefaultShiftClosesStretch() {
  const TestImage reference = TestImage::Pattern();
  const TestImage shifted = Translate(reference, 16, 0);
  FrameMotion motion;
  Require(motion.Accept(reference.View()), "first frame should establish a reference");
  Require(motion.Accept(shifted.View()), "eight grid columns should cross default shift threshold");
  RequireNear(motion.last_shift(), 8.0f / 64.0f, 1e-6f, "default shift measurement");
}

void TestDiagonalMotionIsMeasured() {
  const TestImage reference = TestImage::Pattern();
  const TestImage shifted = Translate(reference, 12, 16);
  FrameMotion motion;
  Require(motion.Accept(reference.View()), "first frame should establish a reference");
  Require(motion.Accept(shifted.View()),
          "diagonal motion at the search limit should close the default stretch");
  RequireNear(motion.last_shift(), 10.0f / 64.0f, 1e-6f,
              "six-by-eight grid translation should have ten-column magnitude");
}

void TestShiftThresholdBoundaries() {
  Require(FrameMotion::IsValidShiftThreshold(FrameMotion::kMaxShift - 0.001f),
          "a value just below the measurable limit should be valid");
  Require(!FrameMotion::IsValidShiftThreshold(FrameMotion::kMaxShift),
          "a value at the measurable limit should be invalid");
  Require(!FrameMotion::IsValidShiftThreshold(FrameMotion::kMaxShift + 0.001f),
          "a value above the measurable limit should be invalid");
  Require(!FrameMotion::IsValidShiftThreshold(0.0f),
          "zero would close every stretch and should be invalid");
  Require(!FrameMotion::IsValidShiftThreshold(-0.01f),
          "a negative threshold should be invalid");
}

void TestResidualThresholdBoundaries() {
  Require(FrameMotion::IsValidResidualThreshold(FrameMotion::kMaxResidual - 0.001f),
          "a value just below the residual limit should be valid");
  Require(!FrameMotion::IsValidResidualThreshold(FrameMotion::kMaxResidual),
          "a value at the residual limit should be invalid");
  Require(!FrameMotion::IsValidResidualThreshold(0.0f),
          "zero would close every stretch and should be invalid");
}

void TestSetThresholdsRejectsUnreachableValues() {
  FrameMotion motion;
  const float default_shift = motion.min_shift();
  const float default_residual = motion.min_residual();
  Require(!motion.SetThresholds(0.18f, default_residual),
          "an unreachable shift threshold should be rejected");
  RequireNear(motion.min_shift(), default_shift, 1e-6f,
              "a rejected shift threshold should leave the previous value in place");
  Require(motion.SetThresholds(0.1f, default_residual),
          "a reachable shift threshold should be accepted");
  RequireNear(motion.min_shift(), 0.1f, 1e-6f,
              "an accepted shift threshold should take effect");
}

void TestShiftPresetsAreReachable() {
  const TestImage reference = TestImage::Pattern();
  const TestImage shifted = Translate(reference, 20, 0);
  for (float preset : FrameMotion::kShiftPresets) {
    Require(FrameMotion::IsValidShiftThreshold(preset),
            "a PRO panel shift preset must fall within the measurable range");
    FrameMotion motion;
    Require(motion.SetThresholds(preset, FrameMotion::kMaxResidual - 0.001f),
            "a valid preset should be accepted by SetThresholds");
    Require(motion.Accept(reference.View()), "first frame should establish a reference");
    Require(motion.Accept(shifted.View()),
            "a search-limit single-axis shift should close the stretch at every preset");
  }
}

void TestBrightnessChangeTriggersResidual() {
  const TestImage reference = TestImage::Pattern();
  const TestImage brighter = AddBrightness(reference, 24);
  FrameMotion motion;
  Require(motion.Accept(reference.View()), "first frame should establish a reference");
  Require(motion.Accept(brighter.View()), "brightness change should cross current residual threshold");
  Require(motion.last_residual() >= FrameMotion::kDefaultMinResidual,
          "brightness change residual should reach default threshold");
}

void TestRotationTriggersResidual() {
  const TestImage reference = TestImage::Pattern();
  const TestImage rotated = Rotate(reference, 5.0f);
  FrameMotion motion;
  Require(motion.Accept(reference.View()), "first frame should establish a reference");
  Require(motion.Accept(rotated.View()), "rotation should close the current stretch");
  Require(motion.last_residual() >= FrameMotion::kDefaultMinResidual,
          "rotation should be visible in residual");
}

void TestScaleChangeTriggersResidual() {
  const TestImage reference = TestImage::Pattern();
  const TestImage scaled = Scale(reference, 1.08f);
  FrameMotion motion;
  Require(motion.Accept(reference.View()), "first frame should establish a reference");
  Require(motion.Accept(scaled.View()), "scale change should close the current stretch");
  Require(motion.last_residual() >= FrameMotion::kDefaultMinResidual,
          "scale change should be visible in residual");
}

void TestSharpImageOutranksBlurredImage() {
  const TestImage sharp = TestImage::Pattern();
  const TestImage blurred = BoxBlur(sharp, 3);
  const float sharp_score = LumaSharpness(sharp.pixels().data(), sharp.width(),
                                          sharp.height(), sharp.width(), 4);
  const float blurred_score = LumaSharpness(blurred.pixels().data(), blurred.width(),
                                            blurred.height(), blurred.width(), 4);
  Require(sharp_score > blurred_score,
          "sharp synthetic image should outrank its blurred copy");
  Require(blurred_score > 0.0f, "blurred textured image should retain a positive score");
}

}  // namespace

int main() {
  const std::vector<std::pair<std::string, std::function<void()>>> tests = {
      {"unchanged frame", TestUnchangedFrameIsRejected},
      {"horizontal search limit", TestHorizontalSearchLimit},
      {"default shift", TestDefaultShiftClosesStretch},
      {"diagonal motion", TestDiagonalMotionIsMeasured},
      {"shift threshold boundaries", TestShiftThresholdBoundaries},
      {"residual threshold boundaries", TestResidualThresholdBoundaries},
      {"rejects unreachable thresholds", TestSetThresholdsRejectsUnreachableValues},
      {"shift presets reachable", TestShiftPresetsAreReachable},
      {"brightness residual", TestBrightnessChangeTriggersResidual},
      {"rotation residual", TestRotationTriggersResidual},
      {"scale residual", TestScaleChangeTriggersResidual},
      {"sharpness ranking", TestSharpImageOutranksBlurredImage},
  };

  for (const auto& [name, test] : tests) {
    test();
    std::cout << "PASS: " << name << '\n';
  }
  std::cout << tests.size() << " sampling tests passed\n";
  return 0;
}
