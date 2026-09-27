# 14. Bound Keyframe Selection to a Spacing Window

Date: 2026-09-27

## Status

Accepted.

## Context

`FrameMotion::Accept()` does three things in one call: it measures the current frame against a reference, it decides whether that is enough movement to end the current stretch, and — when it decides yes — it replaces the reference with the current frame. `SessionRecorder::Record()` then separately tracks the sharpest frame seen since the last time `Accept()` returned true, and writes that one out.

The reference `Accept()` picks is whichever frame happened to cross the threshold, not the frame `SessionRecorder` actually goes on to write. The next stretch's motion is measured from a frame nobody kept. Combined with sharpness being compared across the *entire* stretch rather than near where the threshold was crossed, a keyframe can end up almost beside the previous one (if the sharpest frame happened to arrive early, with little real movement yet) or well past the intended spacing (if it arrived late). Neither the 88% overlap the docs describe nor any other fixed figure is actually guaranteed. These are problems 1 and 2 in `.project/SAMPLING_IMPROVEMENT_ROADMAP.md`.

The prior commit, `fix: constrain motion thresholds`, fixed the measurable/configurable shift range and in doing so pinned `FrameMotion::kMaxShift` at search-radius/grid-width ≈ 0.156, versus the existing 0.12 default target. That leaves only about 0.036 of headroom between "target reached" and "as far as the search can measure at all" — too little for a second, independent "max shift" threshold (the roadmap's original rule 3) to do anything a plain time bound can't do better.

## Decision

Separate measurement, spacing, and quality into three narrow pieces:

1. `FrameMotion` measures only. `Accept()` is replaced by `Measure(image)`, which updates `last_shift()`/`last_residual()` against the current reference and never changes that reference itself. The reference is changed only by a new explicit `Commit(grid, grid_height)`, taking the small (`kGridWidth` × `grid_height` bytes, a few KB) downsampled grid a prior `Measure()` call already produced — exposed as `current_grid()`/`current_grid_height()` — rather than a raw camera image. That lets a caller snapshot a candidate's grid the moment it becomes the frontrunner, cheaply, and commit it later without re-decoding whichever frame wins.

2. A new `KeyframeSelector` (`pipeline/`, no Android or image dependencies, buildable by the host test harness the same way `FrameMotion` is) owns the spacing decision. Given one frame's `(timestamp, shift, residual, target_shift, target_residual)` it tracks:
   - A window, open once shift or residual first reaches `kApproachRatio` (0.85) of target, and staying open through `kPostCrossingWindowNs` (200ms, chosen to cover roughly four to six frames at 24-30fps) after target is fully reached. Only frames seen while the window is open compete to be the next keyframe — not the whole stretch — which is what actually fixes problem 2.
   - A stationary ceiling, `kMaxStationaryIntervalNs` (5s), independent of the window: if shift and residual never approach target at all — a tripod, a red light — a keyframe is still confirmed on this timer alone, using whatever was sharpest since the last one.
   - A per-frame verdict: `ConfirmReason::kNone` / `kWindow` / `kStationary`.

3. `SessionRecorder` keeps two candidates instead of one, each a `PendingFrame` paired with the `FrameMotion` grid snapshot taken when it became the leader: a window candidate (sharpest frame seen while the selector's window is open) and a stationary candidate (sharpest frame seen since the last keyframe, unconditionally — the window candidate can be empty if the very first frame past the last keyframe already crosses target). On `kWindow`, the window candidate is written; on `kStationary`, the stationary one is. Either way, `FrameMotion::Commit()` re-baselines to the winner's grid, both candidates clear, and the selector is told a keyframe landed at that timestamp.

4. No separate spatial "max shift" bound. `kPostCrossingWindowNs` already bounds how long selection can run past crossing regardless of how much further the picture moves in that time, and `kMaxShift`'s own ~0.156 ceiling bounds how large a shift can even be reported — between the two, the worst case the roadmap's original rule 3 was for (indefinite drift while waiting for a sharper frame) is already covered.

## Consequences

- The reference is always the last frame actually *written*, which is what fixes roadmap problems 1 and 2 together: spacing is measured from the viewpoint a reconstruction actually receives, not from whichever frame happened to cross a threshold.
- One more ~20MB `PendingFrame`-sized buffer held concurrently in `Retention::kSharpest` mode (window candidate + stationary candidate, versus today's single `pending_`). `Retention::kAll` is unaffected — every frame is still written immediately through a single buffer.
- `FrameMotion::Accept()` and its `rejected()` counter are removed. Neither is used outside `SessionRecorder::Record()` and one host test; nothing in the manifest depends on `rejected()`.
- `candidates.csv`'s `shift`/`residual` columns keep the same meaning (distance from the last confirmed keyframe) but should read more consistently across a session, since the frame they are measured from is now always the one that got written. This also changes `Retention::kAll`, which previously advanced the reference only on a threshold crossing (so its columns measured distance since that, not since the last written frame — every frame, in `kAll`); it now commits on every frame, matching what "last confirmed keyframe" means when every frame is one.
- `tests/sampling_tests.cc` needs a substantial rewrite: the `Accept()`-based `FrameMotion` tests move to `Measure()`/`Commit()`, and new tests cover `KeyframeSelector` directly — window open/close timing, the stationary ceiling, and the approach-ratio boundary.
