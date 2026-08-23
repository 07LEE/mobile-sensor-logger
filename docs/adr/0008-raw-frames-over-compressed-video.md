# 8. Raw Frames Over Compressed Video

Date: 2026-08-23

## Status

Accepted.

## Context

`retention=all` was measured losing frames upstream of `Record()` under sustained capture — a spare-buffer-pool exhaustion that forces a page-faulting cold allocation once the disk's burst cache runs out, which stalls the main loop long enough for the camera's four-buffer pool to overflow. The spare pool was widened as a mitigation, but the ceiling itself is a property of writing ~19MB of uncompressed YUV per frame; nothing short of writing less data removes it.

A phone's stock camera app records the same or higher resolution for minutes without this failure mode. It does so by encoding through a hardware H.264/H.265 encoder fed directly from the camera's buffer, which cuts the data actually written by roughly two orders of magnitude and skips the CPU-side memory copy this app's YUV_420_888 path requires for sharpness scoring and file writes.

The saving is not free. A video codec's P/B frames encode a motion-compensated difference from another frame rather than an independent image, and rate control spends fewer bits on exactly the content that is hardest to predict — motion-blurred or fast-changing regions — which is the same class of frame a reconstruction already has the least margin on. `frames_lost_upstream` on this codebase's own account shows the failure mode: a fragmented reconstruction traced to a handful of frames lost during a fast turn. Trading that for a codec that specifically economizes on detail during motion risks reintroducing the same failure by a different path.

## Decision

Keep per-frame, uncompressed `YUV_420_888` capture, written as individual files. Do not adopt video-encoded capture as a fix for the `retention=all` storage ceiling.

## Consequences

The storage-throughput ceiling under `retention=all` remains a real limit, addressed by mitigation (a larger spare buffer pool, a possible sharpness gate) rather than removed. Long high-resolution `all` sessions still have a bounded safe duration on a given device.

What is kept is detail with no compression loss at all beyond the sensor's own chroma subsampling — every frame stands on its own, independent of how the scene around it moved.

If the storage ceiling turns out not to be survivable through mitigation alone, video remains open to revisit — but only after a direct comparison of reconstruction quality against raw capture on the same scene, since the loss a codec introduces is concentrated exactly where a capture already has the least room to lose it.
