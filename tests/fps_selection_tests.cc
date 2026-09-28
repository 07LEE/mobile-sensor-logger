#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "fps_selection.h"

namespace {

using sensor_logger::FpsRange;
using sensor_logger::FpsSelection;
using sensor_logger::SelectFps;

void Require(bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

void TestAutoNeedsNoValidation() {
  const FpsSelection result = SelectFps(0, {}, 0);
  Require(result.request_supported, "auto should remain supported");
  Require(result.applied_fps == 0, "auto should remain auto");
}

void TestExactRangeAndFastOutputAreApplied() {
  const FpsSelection result =
      SelectFps(30, {{15, 30}, {30, 30}}, 33'333'333);
  Require(result.request_supported, "30fps should be supported");
  Require(result.fixed_range_available, "fixed range should be found");
  Require(result.applied_fps == 30, "30fps should be applied");
  Require(result.max_output_fps == 30, "output ceiling should be reported");
}

void TestVariableRangeDoesNotPromiseFixedRate() {
  const FpsSelection result = SelectFps(24, {{15, 30}}, 33'333'333);
  Require(!result.request_supported, "variable range is not fixed 24fps");
  Require(result.applied_fps == 0, "unsupported rate should fall back to auto");
}

void TestSlowOutputRejectsAdvertisedRange() {
  const FpsSelection result = SelectFps(60, {{60, 60}}, 33'333'333);
  Require(result.fixed_range_available, "60fps range should be advertised");
  Require(!result.request_supported, "30fps output cannot deliver 60fps");
  Require(result.applied_fps == 0, "slow output should fall back to auto");
}

void TestMissingDurationRejectsFixedRate() {
  const FpsSelection result = SelectFps(30, {{30, 30}}, 0);
  Require(!result.request_supported, "unknown output speed is not a promise");
  Require(result.max_output_fps == 0, "unknown ceiling should stay unknown");
}

}  // namespace

int main() {
  const std::vector<std::pair<const char*, void (*)()>> tests = {
      {"auto", TestAutoNeedsNoValidation},
      {"supported fixed rate", TestExactRangeAndFastOutputAreApplied},
      {"variable range", TestVariableRangeDoesNotPromiseFixedRate},
      {"slow output", TestSlowOutputRejectsAdvertisedRange},
      {"missing duration", TestMissingDurationRejectsFixedRate},
  };

  for (const auto& test : tests) {
    test.second();
    std::cout << "PASS: " << test.first << '\n';
  }
  return 0;
}
