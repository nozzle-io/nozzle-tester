# Receiver storage and semantic format checks

The macOS-only native peer calls the same `copy_received_image` used by GUI
`receive_one_frame` and CLI `run_receiver`. Allocation and byte interpretation use
`NozzleFrameInfo.format`; the original 17 checks compare this against returned
copy metadata. No R/B swap or precision conversion is introduced.
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

The verdict regression additionally generates a pattern in actual mapped storage
at 65x33/641x33, publishes it as frame 1 (the core's first committed index), and
calls the real `verify_pattern` with the requested format and received frame index,
as GUI/CLI receivers do. It asserts PASS and exact copied bytes for all four
requested formats. This failing-first extension leaves the production comparison
unchanged in the negative baseline `6e77f47`: CI establishes whether requested
RGBA8 is rejected as BGRA8 storage.
These additional cases use the pattern generator; the original 17 copy cases do
not. Copy agreement alone does not establish user-facing verdict correctness.

The repair retains storage in `image_buffer.format` and received semantics in
`image_buffer.semantic_format`. `verify_pattern` compares expected format with
semantics, while byte size and decoding still use storage. CPU fixtures/raw-file
verification without transport metadata keep their original storage comparison.
Live receive rejects unsupported/unknown semantic metadata instead of inventing
semantics. Stored GUI image copies retain semantics for subsequent capture checks.

Each generated native case also checks genuine format mismatch, channel swap,
vertical flip and alpha corruption rejection. Synthetic missing semantic metadata
is rejected by the receive helper. The native producer remains alive while a real
CLI child receives its committed frame; matching expectations must exit 0 and
wrong semantic precision must exit 1. CLI JSON is printed in the CI log, including
its format check. These child processes exercise the actual CLI receive path,
not tester's normal sender (whose payload/index alignment is the next separate
repair, nozzle-dev#233, before the GUI evidence work).

No all-backend invariant follows: external/native texture publication, sRGB,
other formats, Windows/Linux GPU runtime and GUI window interaction remain
uncovered. Both producer origins use a top-left receiver copy. The original copy
and GUI-helper verdict checks are same-process; only the CLI child checks are
cross-process. No ordinary tester-to-tester loopback success is claimed.

The target is registered only on Apple platforms; test binaries are not packaged.
The existing GUI build on all three CI operating systems checks helper integration,
not native receive runtime on non-macOS. GUI evidence truthfulness is separate
nozzle-dev#224; this test covers the shared verdict computation, not GUI controls,
capture provenance/file I/O, or screenshot evidence.
