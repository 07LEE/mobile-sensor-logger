# Settings and what they trade

Everything the capture can be told to do differently, and what each choice
costs. For the app itself see the [README](../README.md); for the files it
writes, [output-format.md](output-format.md).

## capture.conf

Read once at startup from `capture.conf` in the app's external files directory,
which is where `adb push` reaches. Missing file or missing key means the
default.

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

`shutter`, `fps`, `mains`, `shift` and `residual` can also be changed from the
phone itself, in the PRO panel (see the [README](../README.md)) — a tap
cycles a row through a fixed list of values the same way retention and lens
already do, and the change is written back to this same file, so it is picked
up on the next recording without leaving the app. `shift` and `residual` are
continuous in the file (any value between 0 and 0.5) but step through presets
either side of the default on the panel — a coarser control than the file
allows, in exchange for fitting the same tap-to-cycle pattern as the rest of
the row. `capture`, `retention` and `lens` remain file-only: the latter two
already have their own toolbar buttons, and `capture` is not something worth
changing between takes.

Every rear camera is logged at startup with its focal length, so `lens` can name
one by id. On a Galaxy S25 Ultra two are offered: `0` at 6.3mm and `2` at 2.2mm.

The default is `ultrawide`, the shortest focal length the device offers. It
wins at both ends of the range this is used at: a frame covers far more of a
room, which matters when free space is the budget, and it focuses closer than
the main lens — 5cm against 10cm on the tested device, enough to beat it on
close detail despite the wider field, since detail goes as the reciprocal of
distance. The main lens is ahead only in between, by about 1.4x per degree.

`main` is whichever the system lists first. That is usually a **logical**
camera, which chooses a physical lens by zoom ratio and can change it during a
capture — and the focal length changes with it, which nothing downstream will
expect. `session.json` records `logical_multi_camera` so a session that came
back inconsistent has somewhere to start. `ultrawide` picks the shortest focal
length, which on the tested device is a physical camera and cannot change.

A requested size is used only if the chosen camera offers it exactly; otherwise
it takes the largest and says so in the log.

## Which frames are kept

Every frame is scored for sharpness — variance of the Laplacian over the luma
plane, subsampled — and the sharpest frame of each stretch of movement is
written. The score is in `candidates.csv` and `frames.csv`. It has no absolute
meaning: it moves with scene content and exposure, so it only compares frames of
the same scene taken moments apart.

A stretch ends when the picture has changed enough, measured against the last
frame kept:

| Column | Meaning | Default threshold |
| --- | --- | --- |
| `shift` | Best alignment offset, as a fraction of frame width | `0.12` |
| `residual` | Mean difference remaining at that offset, `0`–`1` | `0.06` |

Either one crossing its threshold ends the stretch. `shift` catches panning and
sideways movement; `residual` catches walking forward and rotating about the lens
axis, which barely move the offset.

Both are settings (see below); the sharpness subsampling step is in
`session_recorder.cc`. Neither has been tuned against a reconstruction.

Halving the thresholds roughly doubles the frames kept, and they only bite while
the camera moves slowly enough for them to — sweep fast enough and every frame
crosses them, at which point the capture is keeping everything and the frame rate
is the only limit left.

The defaults leave about 88% of the picture shared between consecutive kept
frames, which is generous next to the 70 to 80% reconstruction usually asks for.
They sit there because at full resolution the free space is the budget: 105GB
holds roughly 5,500 frames, which these values spend in forty minutes and half
these values spend in six. The `ROOM FOR` line on screen is what says whether a
capture is spending it faster than intended.

Every frame scored is logged whether or not its image was kept, so what a
different threshold would have selected can be worked out from a capture already
taken — see [candidates.csv](output-format.md#files).

## Pinning the frame rate

Left at `auto`, the frame rate is whatever the platform defaults to for the
chosen resolution, and that default has turned out to be a range flexible
enough to run below the sensor's ceiling in a dim room — auto exposure can
trade frame rate for a longer exposure instead of raising sensitivity further.
On the tested device the ceiling is a fixed 30fps in a bright room and a
measured 24fps in a dim one, both with `fps` left at `auto`. `fps` pins one
rate for the whole session, which makes the frame count a fixed recording
time will produce arithmetic instead of a guess.

This is about rate only, not exposure: shutter speed is capped separately by
`shutter`, or is whatever the scene metered to, and does not move again once
recording starts regardless of what `fps` is set to. Pinning `fps` below the
sensor's ceiling does not shorten the exposure on its own — capping `shutter`
is still the way to fight motion blur.

## Running out of room

A frame at full resolution is around 19MB, and a session writes two or three a
second, so plan on **2 to 3GB per minute**. The readout carries the measured
rate and what is left at it.

`dropped_frames` in `session.json` counts frames the writer's queue could not
keep up with. **It should be zero.** A run of drops means the capture is
asking for more than the device can write.

`frames_lost_upstream` counts a different kind of loss that `dropped_frames`
cannot see: frames the camera finished (counted in
`camera_completed_captures`) that `AcquireFrame` never handed to `Record`
because a newer image had already superseded them in the reader's buffer —
which happens once the capture loop falls behind for long enough, `retention
= all` being the mode most likely to. Both should be zero on a capture that
kept up.

Recording stops on its own with 2GB left, and the readout says
`STOPPED - DISK FULL`. That margin exists because a session that runs the disk
to the last byte cannot write its own manifest — the settings, the counts and
the calibration go with it — so a capture that filled a phone ends up unusable
anyway. Sessions are never deleted; clearing them is a manual job.
