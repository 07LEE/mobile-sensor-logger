#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>

#include "upstream_loss.h"

namespace {

using sensor_logger::UpstreamLoss;
using sensor_logger::UpstreamLossSnapshot;

void Require(bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

void TestNoLossReportsZero() {
  UpstreamLoss loss;
  for (int64_t t = 1; t <= 5; ++t) {
    loss.ObserveResult(t);
    loss.ObserveImage(t);
  }
  const UpstreamLossSnapshot snapshot = loss.Snapshot();
  Require(snapshot.lost_in_range == 0, "matched streams lose nothing");
  Require(snapshot.results_outside_range == 0, "matched streams have no edge");
}

void TestResultWithoutImageInsideRangeIsLost() {
  UpstreamLoss loss;
  for (int64_t t = 1; t <= 5; ++t) loss.ObserveResult(t);
  for (int64_t t : {1, 2, 4, 5}) loss.ObserveImage(t);
  const UpstreamLossSnapshot snapshot = loss.Snapshot();
  Require(snapshot.lost_in_range == 1, "result 3 has no image");
  Require(snapshot.results_outside_range == 0, "nothing outside the range");
}

void TestBoundaryResultsAreNotLoss() {
  UpstreamLoss loss;
  // A result before the first image and one after the last: recording began
  // and ended between a result and its image.
  for (int64_t t : {1, 2, 3, 4, 5, 6}) loss.ObserveResult(t);
  for (int64_t t : {2, 3, 4, 5}) loss.ObserveImage(t);
  const UpstreamLossSnapshot snapshot = loss.Snapshot();
  Require(snapshot.lost_in_range == 0, "edge results are not loss");
  Require(snapshot.results_outside_range == 2, "both edge results counted");
}

void TestImageWithoutResultIsNotLoss() {
  UpstreamLoss loss;
  loss.ObserveResult(1);
  loss.ObserveResult(3);
  for (int64_t t : {1, 2, 3}) loss.ObserveImage(t);
  Require(loss.Snapshot().lost_in_range == 0, "extra image is not a loss");
}

void TestNoImagesPutsEveryResultOutside() {
  UpstreamLoss loss;
  loss.ObserveResult(1);
  loss.ObserveResult(2);
  const UpstreamLossSnapshot snapshot = loss.Snapshot();
  Require(snapshot.lost_in_range == 0, "no images, nothing to lose");
  Require(snapshot.results_outside_range == 2, "results are outside");
}

void TestOutOfOrderArrivalIsHandled() {
  UpstreamLoss loss;
  for (int64_t t : {3, 1, 2}) loss.ObserveImage(t);
  for (int64_t t : {2, 3, 1}) loss.ObserveResult(t);
  Require(loss.Snapshot().lost_in_range == 0, "order does not matter");
}

void TestResetClearsBothStreams() {
  UpstreamLoss loss;
  loss.ObserveResult(1);
  loss.ObserveResult(2);
  loss.ObserveImage(1);
  loss.ObserveImage(3);
  loss.Reset();
  const UpstreamLossSnapshot snapshot = loss.Snapshot();
  Require(snapshot.lost_in_range == 0 && snapshot.results_outside_range == 0,
          "reset starts a clean session");
}

}  // namespace

int main() {
  TestNoLossReportsZero();
  TestResultWithoutImageInsideRangeIsLost();
  TestBoundaryResultsAreNotLoss();
  TestImageWithoutResultIsNotLoss();
  TestNoImagesPutsEveryResultOutside();
  TestOutOfOrderArrivalIsHandled();
  TestResetClearsBothStreams();
  std::cout << "upstream_loss_tests passed\n";
  return 0;
}
