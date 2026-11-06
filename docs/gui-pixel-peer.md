# GUI CPU publisher verification

The macOS CTest target `nozzle_tester_gui_pixel_peer` exercises the same
`copy_gui_image_to_mapping` helper used by the real GUI publisher. An independent
native core receiver checks actual Metal storage without the GUI receiver,
CLI conversion or pattern-verification code. It requires Metal and fails on an
unexpected native format; no backend SKIP is permitted in this lane.

Eight native cases cover RGBA8, BGRA8, RGBA16F and RGBA32F at 65x3 and 641x3.
Byte channels and alpha are asymmetric; floating patterns carry low mantissa
bits. Floating cases require the original storage type and byte-for-byte
identity, not approximate equality or a conversion to lower precision.

One supplemental CPU-mapping contract case covers both byte permutation
directions, bottom-left origin, negative padded strides, untouched padding,
short source buffers, invalid strides and rejected float32-to-float16 mapping.
These synthetic cases do not claim the native backend produced such mappings.

The production helper copies identical formats unchanged and uses core swizzle
only for RGBA8/BGRA8 permutations. Unsupported mismatches fail rather than
silently changing precision. GUI publication checks copy and mapping-unlock
errors, discards failed frames, and only advances its preview/count after commit.

The initial negative baseline is preserved in PR history and recorded in
[nozzle-dev#223](https://github.com/nozzle-io/nozzle-dev/issues/223): two RGBA8
native mismatches while all six BGRA/float controls passed.

## Boundaries

- Same-process native peer plus production GUI copy helper, not GUI-window,
  control interaction or cross-process smoke.
- The universal GUI builds in CI; execution here is on the runner's native
  architecture, not proof that both architecture slices ran.
- Windows/Linux build/package evidence is separate from this macOS pixel test.
- Sender commit/preview status is not independent receiver verification. The
  existing GUI evidence-model issue remains tracked in
  [nozzle-dev#224](https://github.com/nozzle-io/nozzle-dev/issues/224).
- CLI and GUI receive behavior are not repaired or broadly validated by this
  sender-copy test. The proposed public core API in #222 is not implemented.
