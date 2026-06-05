# nozzle-tester pattern specification

The default pattern is deliberately hostile to false positives.

Required properties:

- distinct corner markers for top-left, top-right, bottom-left, and bottom-right;
- separated red and blue regions for R/B swap detection;
- a semi-transparent alpha patch over a visible background;
- a visible orientation/size cue through asymmetric gradients and corner positions;
- a frame-dependent moving marker for stale-frame detection;
- enough per-pixel structure to expose crop, stretch, stride, and resize mistakes.

Initial dimensions:

- `1x1`
- `2x2`
- `3x2`
- `2x3`
- `16x16`
- `320x240`
- `641x479`
- `3840x2160` performance case

Initial formats:

- `rgba8_unorm`
- `bgra8_unorm`
- `rgba16_float`
- `rgba32_float`

The pattern generator defines RGBA semantic values first. Storage layout conversion is explicit and format-specific. Native texture format, CPU/evidence layout, and sampling semantics must be recorded separately in evidence.
