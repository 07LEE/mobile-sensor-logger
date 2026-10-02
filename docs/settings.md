# Settings and what they trade

Everything the capture can be told to do differently, and what each choice costs. For the app itself see the [README](../README.md); for the files it writes, [output-format.md](output-format.md).

## capture.conf

Read once at startup from capture.conf in the app's external files directory, which is where `adb push` reaches. Missing file or missing key means the default.

```ini
capture   = max | 1920x1080     # largest the camera offers, or an exact size
retention = sharpest | all      # selected frames, or every frame
lens      = ultrawide | main | <camera id>
shift     = 0.12                # how far the picture may slide before a frame
residual  = 0.06                # how much of it may stop matching
shutter   = auto | 1/120        # longest exposure allowed once locked
mains     = 60 | 50 | off       # how often the lights pulse, for flicker
fps       = auto | 30           # pins the frame rate instead of letting AE pick
```

```bash
adb push capture.conf /sdcard/Android/data/com.sensor.logger/files/
```

`shutter`, `fps`, `mains`, `shift` and `residual` can also be changed in the PRO panel, and each change is written back to this file for the next recording. `shift` accepts values greater than `0` and below `0.15625`; `residual` accepts values greater than `0` and below `1.0`. The panel offers a smaller preset list around the defaults. `capture` is file-only. Retention has its own toolbar button and is saved to the file; Lens has its own toolbar button but a lens selected there applies only until the app restarts unless `capture.conf` is also updated.

Every rear camera is logged at startup with its focal length, so `lens` can name one by id. On a Galaxy S25 Ultra two are offered: `0` at 6.3mm and `2` at 2.2mm.

The default is `ultrawide`, the shortest focal length the device offers. It wins at both ends of the range this is used at: a frame covers far more of a room, which matters when free space is the budget, and it focuses closer than the main lens — 5cm against 10cm on the tested device, enough to beat it on close detail despite the wider field, since detail goes as the reciprocal of distance. The main lens is ahead only in between, by about 1.4x per degree.

`main` is whichever the system lists first. That is usually a logical camera, which chooses a physical lens by zoom ratio and can change it during a capture — and the focal length changes with it, which nothing downstream will expect. session.json records `logical_multi_camera` so a session that came back inconsistent has somewhere to start. `ultrawide` picks the shortest focal length, which on the tested device is a physical camera and cannot change.

A requested size is used only if the chosen camera offers it exactly; otherwise it takes the largest and says so in the log.

## PRO panel actions

`INTRINSIC: OK` means the selected camera published Camera2 intrinsic calibration; `INTRINSIC: NONE` means it did not. This row reports availability and cannot be tapped.

`EXTRINSIC CAPTURE` closes the panel and starts a recording immediately with `retention=all` for that recording only. It is intended for a dense camera-IMU calibration take; stopping the recording restores the previous retention setting.

`RESET` restores the PRO values to `shutter=auto`, `fps=auto`, `mains=60`, `shift=0.12` and `residual=0.06`, then saves them to `capture.conf`.

## Which frames are kept

With `retention=sharpest`, every frame is scored for sharpness — variance of the Laplacian over the luma plane, subsampled — and only selected keyframes are submitted to the writer. With `retention=all`, every acquired frame is submitted. A full writer queue can still drop a submitted frame. The sharpness score is recorded in candidates.csv and frames.csv; it has no absolute meaning and should only compare nearby frames of the same scene.

A stretch ends when the picture has changed enough, measured against the last frame kept:

| Column | Meaning | Default threshold |
| --- | --- | --- |
| `shift` | Best alignment offset, as a fraction of frame width | `0.12` |
| `residual` | Mean difference remaining at that offset, `0`–`1` | `0.06` |

`shift` catches panning and sideways movement; `residual` catches walking forward and rotating about the lens axis, which barely move the offset. Reaching 85% of either target opens a candidate window. Once either target is crossed, the app keeps comparing nearby candidates for 200ms and confirms the sharpest one in that window. If neither target is crossed for five seconds, the sharpest stationary candidate is confirmed instead.

Both thresholds are settings. Neither has been tuned against a reconstruction.

Lower thresholds generally keep frames more densely, but the relationship is scene- and motion-dependent. Fast motion does not make `retention=sharpest` keep every frame: the post-crossing candidate window still bounds each selection decision.

The thresholds do not guarantee a fixed overlap between consecutive keyframes. Scene depth, rotation, residual noise and the sharpness winner inside the candidate window all affect the final spacing. At full resolution, use the `MIN LEFT` estimate on screen to judge the current session's measured storage rate instead of relying on a fixed frame count.

Every scored frame is logged in `candidates.csv`, and its downsampled luma grid is stored in `motion_grid.bin`. Together they let `./scripts/replay_sampling.sh` rerun the production selection rule with different thresholds; `candidates.csv` alone cannot reproduce a new reference chain. See [output-format.md](output-format.md#replaying-the-selection-rule).

## Pinning the frame rate

Left at `auto`, the frame rate is whatever the platform defaults to for the chosen resolution, and that default has turned out to be a range flexible enough to run below the sensor's ceiling in a dim room — auto exposure can trade frame rate for a longer exposure instead of raising sensitivity further.
On the tested device the ceiling is a fixed 30fps in a bright room and a measured 24fps in a dim one, both with `fps` left at `auto`. `fps` pins one rate for the whole session, which makes the frame count a fixed recording time will produce arithmetic instead of a guess.

This is about rate only, not exposure: shutter speed is capped separately by `shutter`, or is whatever the scene metered to, and does not move again once recording starts regardless of what `fps` is set to. Pinning `fps` below the sensor's ceiling does not shorten the exposure on its own — capping `shutter` is still the way to fight motion blur.

A fixed value is applied only when the selected camera advertises that exact fixed range and the chosen capture resolution can produce frames that fast. Unsupported values stay saved as the request but run in auto mode; for example, the panel shows `FPS: 60>AUTO`, the log explains which capability failed, and session.json records the request, applied value and observed result separately.

## Running out of room

A frame at full resolution is around 19MB, and a session writes two or three a second, so plan on 2 to 3GB per minute. The readout carries the measured rate and what is left at it.

`dropped_frames` in session.json counts frames the writer's queue could not keep up with. It should be zero. A run of drops means the capture is asking for more than the device can write.

`frames_lost_upstream` is currently calculated as `camera_completed_captures - considered_frames`. A positive value can indicate images superseded in the reader before `Record` saw them, but it can also include capture-result/image asymmetry at the recording boundary. Treat it as a signal to inspect the timestamps rather than as an exact loss count until that boundary accounting is separated.

Recording stops on its own with 2GB left, and the readout says `STOPPED - DISK FULL`. That margin exists because a session that runs the disk to the last byte cannot write its own manifest — the settings, the counts and the calibration go with it — so a capture that filled a phone ends up unusable anyway. Sessions are never deleted; clearing them is a manual job.
