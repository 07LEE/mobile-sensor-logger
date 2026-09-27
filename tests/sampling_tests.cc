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
#include "keyframe_selector.h"
#include "sharpness.h"

namespace {

using sensor_logger::CameraImageView;
using sensor_logger::FrameMotion;
using sensor_logger::KeyframeSelector;
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

// --- FrameMotion: measurement only, see ADR 14 -----------------------------

void TestFirstFrameEstablishesReference() {
  const TestImage image = TestImage::Pattern();
  FrameMotion motion;
  Require(!motion.has_reference(), "a fresh FrameMotion should have no reference yet");
  motion.Measure(image.View());
  Require(motion.has_reference(), "the first Measure() call should establish a reference");
  RequireNear(motion.last_shift(), 0.0f, 1e-6f, "the first frame has nothing to compare against");
  RequireNear(motion.last_residual(), 0.0f, 1e-6f, "the first frame has nothing to compare against");
}

void TestResetClearsReference() {
  const TestImage image = TestImage::Pattern();
  FrameMotion motion;
  motion.Measure(image.View());
  Require(motion.has_reference(), "Measure() should have established a reference");
  motion.Reset();
  Require(!motion.has_reference(), "Reset() should clear the reference");
}

void TestUnchangedFrameMeasuresZeroMotion() {
  const TestImage image = TestImage::Pattern();
  FrameMotion motion;
  motion.Measure(image.View());
  motion.Measure(image.View());
  RequireNear(motion.last_shift(), 0.0f, 1e-6f, "an unchanged frame should measure zero shift");
  RequireNear(motion.last_residual(), 0.0f, 1e-6f, "an unchanged frame should measure zero residual");
}

void TestHorizontalSearchLimit() {
  const TestImage reference = TestImage::Pattern();
  const TestImage shifted = Translate(reference, 20, 0);
  FrameMotion motion;
  motion.Measure(reference.View());
  motion.Measure(shifted.View());
  RequireNear(motion.last_shift(), FrameMotion::kMaxShift, 1e-6f,
              "horizontal search should saturate at the maximum measurable shift");
  RequireNear(motion.last_residual(), 0.0f, 1e-6f,
              "translated overlap should align exactly at the search limit");
}

void TestPartialHorizontalShiftIsMeasured() {
  const TestImage reference = TestImage::Pattern();
  const TestImage shifted = Translate(reference, 16, 0);
  FrameMotion motion;
  motion.Measure(reference.View());
  motion.Measure(shifted.View());
  RequireNear(motion.last_shift(), 8.0f / 64.0f, 1e-6f, "eight grid columns of shift");
}

void TestDiagonalMotionIsMeasured() {
  const TestImage reference = TestImage::Pattern();
  const TestImage shifted = Translate(reference, 12, 16);
  FrameMotion motion;
  motion.Measure(reference.View());
  motion.Measure(shifted.View());
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
  FrameMotion motion;
  motion.Measure(reference.View());
  motion.Measure(shifted.View());
  for (float preset : FrameMotion::kShiftPresets) {
    Require(FrameMotion::IsValidShiftThreshold(preset),
            "a PRO panel shift preset must fall within the measurable range");
    Require(motion.last_shift() > preset,
            "a search-limit single-axis shift should cross every preset");
  }
}

void TestCommitRebasesTheReference() {
  const TestImage a = TestImage::Pattern();
  const TestImage b = Translate(a, 16, 0);
  const TestImage c = Translate(a, 32, 0);

  FrameMotion motion;
  motion.Measure(a.View());

  motion.Measure(b.View());
  const std::vector<uint8_t> grid_b = motion.current_grid();
  const int32_t grid_height_b = motion.current_grid_height();
  RequireNear(motion.last_shift(), 8.0f / 64.0f, 1e-6f, "b is eight grid columns from a");

  motion.Commit(grid_b, grid_height_b);
  RequireNear(motion.last_shift(), 0.0f, 1e-6f, "Commit() should reset the measurement to zero");

  motion.Measure(c.View());
  RequireNear(motion.last_shift(), 8.0f / 64.0f, 1e-6f,
              "c is eight grid columns from b, not sixteen from a — Commit() must have rebased "
              "the reference to b rather than leaving it at a");
}

void TestBrightnessChangeTriggersResidual() {
  const TestImage reference = TestImage::Pattern();
  const TestImage brighter = AddBrightness(reference, 24);
  FrameMotion motion;
  motion.Measure(reference.View());
  motion.Measure(brighter.View());
  Require(motion.last_residual() >= FrameMotion::kDefaultMinResidual,
          "brightness change residual should reach default threshold");
}

void TestRotationTriggersResidual() {
  const TestImage reference = TestImage::Pattern();
  const TestImage rotated = Rotate(reference, 5.0f);
  FrameMotion motion;
  motion.Measure(reference.View());
  motion.Measure(rotated.View());
  Require(motion.last_residual() >= FrameMotion::kDefaultMinResidual,
          "rotation should be visible in residual");
}

void TestScaleChangeTriggersResidual() {
  const TestImage reference = TestImage::Pattern();
  const TestImage scaled = Scale(reference, 1.08f);
  FrameMotion motion;
  motion.Measure(reference.View());
  motion.Measure(scaled.View());
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

// --- KeyframeSelector: spacing decision only, see ADR 14 --------------------

constexpr float kSelectorTargetShift = 0.12f;
constexpr float kSelectorTargetResidual = 0.06f;

void TestSelectorIdleBelowApproachRatio() {
  KeyframeSelector selector;
  selector.Observe(0, 0.05f, 0.02f, kSelectorTargetShift, kSelectorTargetResidual);
  Require(!selector.InWindow(), "progress below the approach ratio should not open the window");
  Require(selector.ShouldConfirm() == KeyframeSelector::ConfirmReason::kNone,
          "nothing should confirm while far from target");
}

void TestSelectorWindowOpensNearTarget() {
  KeyframeSelector selector;
  // 90% of target shift: above kApproachRatio (0.85) but short of target itself.
  selector.Observe(0, kSelectorTargetShift * 0.9f, 0.0f, kSelectorTargetShift,
                   kSelectorTargetResidual);
  Require(selector.InWindow(), "progress above the approach ratio should open the window");
  Require(selector.ShouldConfirm() == KeyframeSelector::ConfirmReason::kNone,
          "the window opening on its own should not confirm anything yet");
}

void TestSelectorConfirmsWindowAfterPostCrossingDelay() {
  KeyframeSelector selector;
  const int64_t crossed_at = 1000;
  selector.Observe(crossed_at, kSelectorTargetShift, 0.0f, kSelectorTargetShift,
                   kSelectorTargetResidual);
  Require(selector.InWindow(), "the crossing frame itself is still inside its own window");
  Require(selector.ShouldConfirm() == KeyframeSelector::ConfirmReason::kNone,
          "crossing should not confirm immediately — see ADR 14");

  const int64_t at_edge = crossed_at + KeyframeSelector::kPostCrossingWindowNs;
  selector.Observe(at_edge, kSelectorTargetShift, 0.0f, kSelectorTargetShift,
                   kSelectorTargetResidual);
  Require(selector.InWindow(), "exactly at the window's edge should still be inside it");
  Require(selector.ShouldConfirm() == KeyframeSelector::ConfirmReason::kNone,
          "the edge itself should not yet confirm");

  const int64_t past_edge = at_edge + 1;
  selector.Observe(past_edge, kSelectorTargetShift, 0.0f, kSelectorTargetShift,
                   kSelectorTargetResidual);
  Require(!selector.InWindow(), "past the window's edge should no longer be inside it");
  Require(selector.ShouldConfirm() == KeyframeSelector::ConfirmReason::kWindow,
          "the first frame past the window should confirm via the window path");
}

void TestSelectorResidualAloneCanCrossAndConfirm() {
  KeyframeSelector selector;
  selector.Observe(0, 0.0f, kSelectorTargetResidual, kSelectorTargetShift,
                   kSelectorTargetResidual);
  Require(selector.InWindow(), "residual alone reaching target should open the window");

  const int64_t past_edge = KeyframeSelector::kPostCrossingWindowNs + 1;
  selector.Observe(past_edge, 0.0f, kSelectorTargetResidual, kSelectorTargetShift,
                   kSelectorTargetResidual);
  Require(selector.ShouldConfirm() == KeyframeSelector::ConfirmReason::kWindow,
          "residual alone should be able to drive a window confirmation");
}

void TestSelectorStationaryTimeoutFires() {
  KeyframeSelector selector;
  selector.Observe(0, 0.0f, 0.0f, kSelectorTargetShift, kSelectorTargetResidual);
  Require(selector.ShouldConfirm() == KeyframeSelector::ConfirmReason::kNone,
          "no movement at all should not confirm before the stationary ceiling");

  const int64_t before_ceiling = KeyframeSelector::kMaxStationaryIntervalNs - 1;
  selector.Observe(before_ceiling, 0.0f, 0.0f, kSelectorTargetShift, kSelectorTargetResidual);
  Require(selector.ShouldConfirm() == KeyframeSelector::ConfirmReason::kNone,
          "just under the stationary ceiling should still wait");

  const int64_t at_ceiling = KeyframeSelector::kMaxStationaryIntervalNs;
  selector.Observe(at_ceiling, 0.0f, 0.0f, kSelectorTargetShift, kSelectorTargetResidual);
  Require(selector.ShouldConfirm() == KeyframeSelector::ConfirmReason::kStationary,
          "the stationary ceiling should confirm on elapsed time alone");
}

void TestSelectorConfirmedRestartsTimers() {
  KeyframeSelector selector;
  selector.Observe(0, kSelectorTargetShift, 0.0f, kSelectorTargetShift, kSelectorTargetResidual);
  selector.Confirmed(5000);

  Require(!selector.InWindow(), "confirming should close the window immediately");
  Require(selector.ShouldConfirm() == KeyframeSelector::ConfirmReason::kNone,
          "confirming should clear the pending reason");

  const int64_t before_next_ceiling = 5000 + KeyframeSelector::kMaxStationaryIntervalNs - 1;
  selector.Observe(before_next_ceiling, 0.0f, 0.0f, kSelectorTargetShift,
                   kSelectorTargetResidual);
  Require(selector.ShouldConfirm() == KeyframeSelector::ConfirmReason::kNone,
          "the stationary ceiling should count from the confirmed timestamp, not from zero");

  const int64_t at_next_ceiling = 5000 + KeyframeSelector::kMaxStationaryIntervalNs;
  selector.Observe(at_next_ceiling, 0.0f, 0.0f, kSelectorTargetShift, kSelectorTargetResidual);
  Require(selector.ShouldConfirm() == KeyframeSelector::ConfirmReason::kStationary,
          "the stationary ceiling should still fire relative to the new confirmation time");
}

void TestSelectorResetClearsState() {
  KeyframeSelector selector;
  selector.Observe(0, kSelectorTargetShift, 0.0f, kSelectorTargetShift, kSelectorTargetResidual);
  selector.Reset();
  Require(!selector.InWindow(), "Reset() should close any open window");
  Require(selector.ShouldConfirm() == KeyframeSelector::ConfirmReason::kNone,
          "Reset() should clear any pending confirmation");

  // The stationary ceiling should count from whenever Observe() next runs, not
  // from the timestamps seen before Reset().
  selector.Observe(1, 0.0f, 0.0f, kSelectorTargetShift, kSelectorTargetResidual);
  Require(selector.ShouldConfirm() == KeyframeSelector::ConfirmReason::kNone,
          "Reset() should restart the stationary ceiling from the next observation");
}

}  // namespace

int main() {
  const std::vector<std::pair<std::string, std::function<void()>>> tests = {
      {"first frame establishes reference", TestFirstFrameEstablishesReference},
      {"reset clears reference", TestResetClearsReference},
      {"unchanged frame", TestUnchangedFrameMeasuresZeroMotion},
      {"horizontal search limit", TestHorizontalSearchLimit},
      {"partial horizontal shift", TestPartialHorizontalShiftIsMeasured},
      {"diagonal motion", TestDiagonalMotionIsMeasured},
      {"shift threshold boundaries", TestShiftThresholdBoundaries},
      {"residual threshold boundaries", TestResidualThresholdBoundaries},
      {"rejects unreachable thresholds", TestSetThresholdsRejectsUnreachableValues},
      {"shift presets reachable", TestShiftPresetsAreReachable},
      {"commit rebases reference", TestCommitRebasesTheReference},
      {"brightness residual", TestBrightnessChangeTriggersResidual},
      {"rotation residual", TestRotationTriggersResidual},
      {"scale residual", TestScaleChangeTriggersResidual},
      {"sharpness ranking", TestSharpImageOutranksBlurredImage},
      {"selector idle below approach ratio", TestSelectorIdleBelowApproachRatio},
      {"selector window opens near target", TestSelectorWindowOpensNearTarget},
      {"selector confirms window after delay", TestSelectorConfirmsWindowAfterPostCrossingDelay},
      {"selector residual alone confirms", TestSelectorResidualAloneCanCrossAndConfirm},
      {"selector stationary timeout fires", TestSelectorStationaryTimeoutFires},
      {"selector confirmed restarts timers", TestSelectorConfirmedRestartsTimers},
      {"selector reset clears state", TestSelectorResetClearsState},
  };

  for (const auto& [name, test] : tests) {
    test();
    std::cout << "PASS: " << name << '\n';
  }
  std::cout << tests.size() << " sampling tests passed\n";
  return 0;
}
