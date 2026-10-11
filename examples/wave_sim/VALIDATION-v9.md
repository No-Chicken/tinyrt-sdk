# Wave 9 validation — 2026-10-11

## Configuration and scope

600 independent shaded 3D particles; full 466 x 466 round screen, projected container radius 225 px (8 px edge allowance), depth 64 px, focal distance 480 px. App integer version 9; ABI 1, existing SPRITE_BATCH. No production firmware/API, menu, icon, font or system tick changes.

Density/near-density pressure relaxation, viscosity and corrected-displacement velocity replace the old hard-contact solver. Each 1/30 s update contains two 1/60 s substeps. Stable IDs connect immutable completed snapshots. The density pass caches actual neighbors; overflow falls back to a complete pressure scan. Candidate work is bounded across cooperative calls; splash waits for completion of any in-progress sort.

## Why the previous build felt slow

Submitted or completed display frames do not measure physical movement. A temporary on-screen completed-physics counter exposed about 6 updates/s with 900 particles and a 6 ms callback budget, despite roughly 45 display FPS. Neighbor caching and a 12 ms budget raised 900 particles to about 12 updates/s; 600 particles reached 20 updates/s. A 40 ms maximum work budget and 33/33/34 ms frame cadence now prioritize finishing useful physics rather than repeatedly interpolating an old state. The maximum is a yield limit, not a claim that every callback takes 40 ms.

## S31 hardware measurements

ESP32-S31 COM11, MAC 1c:29:04:d0:08:c4; BLE 1c:29:04:d0:08:c5. Firmware 0.0.6 with temporary test console and performance counters. CPU 320 MHz, PSRAM 100 MHz with XIP, panel QSPI 80 MHz; no clock changes. CPU maximum-frequency lock exists in bsp_power.c. Guest linear-memory placement and native-versus-AOT hardware overhead were not measured.

Each synthetic pose ran 35 seconds; panel counts use complete steady windows. Physics frequency is calculated independently from completed-step counters and application timestamps across each pose.

| Synthetic pose | Completed display FPS | Completed physics updates/s | Transfer errors |
| --- | ---: | ---: | ---: |
| Flat (0,0,1000 mg) | 29.97–30.05 | 29.77 | 0 |
| Tilt (700,0,700 mg) | 29.97–30.07 | 29.92 | 0 |
| Shake (alternating +/-700,+/-500,700 mg) | 29.97–30.07 | 29.89 | 0 |

No replaced frames in retained windows. Each physics update includes two substeps; physical simulated time is approximately real time. Initial settling can be slower: a separate cold-start tilt probe measured roughly 24–26 updates/s before steady-state measurements. Results do not establish a sustained 60 Hz claim.

APP_INFO selected_backend=2, fallback_reason=0, state=3. Only Bird 5 and Wave 9 installed. Allocation failures=0, internal free memory about 163 KiB, no panel errors. Earlier repeated experimental installs encountered DMA exhaustion; this round does not establish its root cause or resolution.

## Stability and regression checks

Final 600-particle native production simulation: fixed tilt (0.7,0,0.7), 80 simulated seconds, measuring the final 20 seconds: RMS displacement 0.0003 px/update, RMS speed 0.0146 px/s. These are native simulation results, not camera measurements. The old hard-contact model measured 8.0380 px/update and 52.7772 px/s in the earlier comparison.

Native physics and app tests at 600/900/1200 particles exercise long shaking, circular/depth bounds, unique IDs, whole/sliced determinism, one 1/30 update versus two 1/60 updates, cached-versus-full neighbor equality, capacity overflow fallback, static-pile RMS, immutable snapshots, projection to screen edges, long press/cancel, and queued splash during interpreter sorting. Assertions and warnings are enabled. AOT S31 RISC-V and S3 Xtensa builds retain bounds, stack and loop-poll safety checks; S3 hardware was not tested.

## Limits and local evidence

True tilt direction, perceived smoothness and tearing require human visual acceptance. The model is independently implemented from standard fluid principles; external solver/render code was not copied. Desktop previews show appearance only.

Local evidence: tmp/s31-wave9-fluid-bench.log, tmp/wave9/fluid-bench-results.json, tmp/wave9-physics-probe.log, tmp/wave9/settle-600.exe. Diagnostic text is disabled in the delivery package; restore formal firmware 0.0.6 after validation.
