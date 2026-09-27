# 15. Record Motion Grids for Offline Replay

Date: 2026-09-27

## Status

Accepted.

## Context

Every threshold and rule choice in the sampling pipeline — `min_shift`, `min_residual`, the keyframe window's approach ratio and post-crossing delay, the stationary ceiling — has so far only been evaluated by shooting a new take with the new numbers and looking at what came out. That is slow, and it is not even a fair comparison: the scene, the hand motion and the lighting are never quite the same twice, so a difference between two takes could be the rule or could just be that take.

`candidates.csv` already logs a timestamp, a sharpness score, and the shift/residual `FrameMotion` measured at the time. But that shift/residual is specific to the reference chain the *actual* thresholds produced during that run — a different `min_shift` would have confirmed a different sequence of keyframes, each becoming the reference for what follows, so the recorded numbers cannot be replayed against a different threshold without already knowing what the new reference chain would have been. Answering "what would `min_shift=0.09` have kept instead" currently means going back and recording it, tripod and all.

`FrameMotion::Measure()` already computes a downsampled luma grid every frame — `current_grid()` — to run its own offset search. It is small (`kGridWidth` × `grid_height` bytes; 64×48 = 3072 bytes at this project's usual 4:3 capture) next to the raw frame it was computed from (multiple megabytes). Recording that grid alongside `candidates.csv` gives a workstation everything `FrameMotion` and `KeyframeSelector` themselves need to re-run the exact same decisions under different thresholds, without the image data at all.

## Decision

1. A new per-session file, `motion_grid.bin`, alongside `candidates.csv`. Binary, little-endian (the only byte order this ever runs on: ARM on the phone, x86-64 on the workstation that replays it) — a fixed 16-byte header, then one fixed-size record per considered frame:

   ```
   header:  magic[4] = "SLMG"   format_version: uint32   grid_width: int32   grid_height: int32
   record:  timestamp_ns: int64   grid: uint8[grid_width * grid_height]
   ```

   `grid_width`/`grid_height` are written once, in the header, because they are constant for a session (the capture resolution and aspect ratio never change mid-recording — same assumption `FrameMotion` itself already makes). The grid layout matches `FrameMotion::Downsample()`'s own indexing: row-major, `grid[y * grid_width + x]`, `y` increasing downward from the top of the frame.

   Fixed record size, rather than a length-prefixed or delimited one, is what makes truncation trivial to detect: a reader checks that the bytes after the header are an exact multiple of the record size, and reports a clear error otherwise instead of silently misreading a partial trailing record. This covers the realistic failure mode on a phone (the process dying mid-write, storage filling up) rather than in-place bit corruption, which nothing here detects — a magic number and a size check catch what actually happens; a per-record checksum would be solving a problem this app has not seen.

2. `SessionRecorder` opens `motion_grid.bin` in `Start()` next to the other logs, and appends one record per considered frame in `Record()`, right after `motion_.Measure()` — the same call site `WriteCandidate()` already reads `last_shift()`/`last_residual()` from. Written for every retention mode, `kAll` included, since the grid is the replay input regardless of which frames were kept as images.

3. `candidates.csv`'s role is now specifically the *observation* log: sharpness (which never depends on any threshold) and the shift/residual the on-device run actually measured against its own reference chain. `motion_grid.bin` is the *replay* input: everything needed to recompute shift/residual against a different reference chain, for different thresholds. Sharpness itself is not re-derived offline — it is not threshold-dependent, so `candidates.csv`'s column is reused as-is by the replay tool rather than recomputed from the grid (the grid is far too downsampled for `LumaSharpness` to give a meaningful score).

4. A new host-buildable CLI, `tools/replay_sampling.cc`, linked against the same `frame_motion.cc`/`keyframe_selector.cc` production code (not a reimplementation): given a session directory and a `min_shift`/`min_residual` pair, it reads `motion_grid.bin` and `candidates.csv`'s sharpness column, drives `FrameMotion`/`KeyframeSelector` exactly as `SessionRecorder::Record()` does, and prints the resulting keyframe timestamps. Run with the thresholds the session actually used, its output should match `frames.csv` exactly — that comparison is the feature's own acceptance test. Run with different thresholds, it answers the "what would a different number have kept" question directly from data already on disk.

5. Recorded unconditionally, not behind a debug flag. At 3080 bytes/frame (8-byte timestamp + a 64×48 grid) and roughly 30 considered fps, a 40-minute session adds about 3080 × 30 × 2400 ≈ 211 MiB — small next to what the kept frames themselves cost (a single raw frame is already ~19 MB) and in keeping with `candidates.csv` already being unconditional. Making it optional would mean the one session that turns out to need re-evaluating is, most of the time, the one it was not recorded for.

## Consequences

- Re-evaluating a threshold or the keyframe window's constants against an already-shot session no longer requires reshooting it.
- Every session on disk grows by roughly 3 KB per considered frame — about 211 MiB for a 40-minute session at 30fps, alongside whatever the kept raw frames already cost.
- `candidates.csv` and `motion_grid.bin` are now both required for a full replay; `candidates.csv` alone (as before) still describes what happened on-device, but no longer doubles as replay input on its own.
- `docs/output-format.md` needs a new section once this lands, and the manifest's `sharpness_metric`/`selection` notes should point at it.
