# Accept 0.5° accuracy before calibration; WATCH at 1.0°

Status: accepted (2026-10-16, spec #14)

## Context

Spec #1 asks for a measurement designed for 0.1° and verified to 0.25°. Random noise averages down to about 0.01°, so noise isn't the limit. The limit is the MPU6050's zero-point drift with temperature, up to about 0.4° over a 20 °C swing. Removing that drift needs a 24–48 h calibration run, which doesn't fit before a working core build is needed.

## Decision

- Aim for 0.25°, and accept 0.5° until temperature compensation is calibrated.
- Temperature compensation stays in the design with zero correction. Readings keep carrying raw pitch and roll, so compensation can be fitted and applied later without changing the data format.
- `WATCH_DEG` is 1.0° (spec: 0.5°) until calibration. Otherwise a day/night swing could raise false WATCH alerts.
- The ALERT rules are unchanged (≥ 2°, or a step of ≥ 0.5° since the previous reading). Temperature drift over one 15-minute interval is far below 0.5°.
- Accuracy is checked later against a few fixed-angle 3D-printed wedges.

## Consequences

- The report can only claim what the wedge check measures.
- Lower `WATCH_DEG` back towards 0.5° once calibration (#11) shows the day/night swing is removed.
