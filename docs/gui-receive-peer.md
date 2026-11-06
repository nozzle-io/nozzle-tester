# GUI receive copied-layout investigation

The macOS-only native peer calls the same `copy_gui_received_image` used by
`receive_one_frame`. This helper is a behavior-preserving extraction: allocation
and image interpretation still use `NozzleFrameInfo.format`, not the returned
copy format. No speculative R/B swap or precision conversion is introduced.
The returned metadata points at the owned image bytes, not the helper's temporary
vector. Frame ownership remains with the caller.

Pinned core `f3a31335bec3d714bf0dcdac39307ebba89fa266`:

- `src/c_api/nozzle_c.cpp::nozzle_frame_copy_pixels_with_origin` maps, copies and
  checks unlock. `copy_mapped_pixels_to_buffer` packs rows according to the actual
  CPU layout, retains the mapped format, and does not convert channels or precision.
- The Metal mapping in `src/common/pixel_access.cpp` uses resolved storage format
  (falling back to frame-info format only if unknown). Receiver frame info comes
  from publication metadata; agreement must be checked, not assumed from the names.

The native producer writes Metal BGRA8 / RGBA16F / RGBA32F lanes directly using
the public C API. It does not call the GUI sender helper, CLI conversion or pattern
generator. Requested RGBA8 and BGRA8, half and float are covered at 65x3/641x3,
with both producer mapping origins. Every received component and image-format
interpretation is checked, including alpha, asymmetric rows, negative/HDR floats
and low float32 bits. A real R8 frame verifies explicit unsupported rejection.
Logs show requested/native/frame-info/semantic/copied formats and native/copied
strides. Missing Metal or setup failures fail, with no SKIP.

This is an investigation baseline, not a claimed layout fix. If the existing
construction passes, report the bounded agreement instead of inventing a failing
reproduction. If it fails numerically, retain that CI evidence before any repair.
No all-backend invariant follows from these cases: external/native publication,
sRGB, other formats, Windows/Linux GPU runtime, cross-process transport and GUI
window interaction are not covered. Both producer origins still use a top-left
receiver copy. The existing CLI and GUI sender tests remain separate.

The target is registered only on Apple platforms; test binaries are not packaged.
The existing GUI build on all three CI operating systems checks helper integration,
not native receive runtime on non-macOS. GUI evidence truthfulness is separate
nozzle-dev#224; this test does not validate its displayed PASS or capture records.
