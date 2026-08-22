# Tested Devices and Hardware Profiles

Hardware parameters and test results for devices tested with Mobile Sensor Logger.

## Device Profiles

| | Galaxy S25 Ultra | Galaxy Z Flip4 |
| --- | --- | --- |
| | SM-S938N, Android 16 | SM-F721N, Android 16 |
| Capture size | 4080x3060, 18.8MB | 4000x3000, 18.1MB |
| Luma row stride | 4096 | 4032 |
| Ultrawide | 2.2mm f/1.9 | 1.7mm f/2.2 |
| Main | 6.3mm f/1.7 (logical) | 5.0mm f/1.8 (logical) |
| Intrinsics fx, fy | 1648, 1650 | 1579, 1578 |
| Distortion k1 | +0.0159 | -0.0073 |
| Hyperfocal | 0.91m | 0.59m |
| Rolling shutter skew | 8.6ms | 32.0ms |
| Chroma layout | `semi_planar_vu` | `semi_planar_vu` |
| Timestamp source | `REALTIME` | `REALTIME` |

Every one of those is read from the device rather than assumed, which is what
the second one was needed to establish. The rolling shutter skew is the pair
worth looking at: nearly four times longer on the Flip4 and about as long as the
gap between frames, so the same movement skews its frames far more.

Both are Samsung and both report semi-planar chroma, so the planar path has
never run.
