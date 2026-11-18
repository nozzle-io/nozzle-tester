# nozzle-tester evidence schema

CLI roles other than receiver use version `0.1.0`. GUI output uses the distinct `0.4.0`
contract below; do not feed it to the CLI self-test acceptance checker.

Top-level fields:

```json
{
  "schema_version": "0.1.0",
  "tool": "nozzle-tester",
  "tool_version": "0.1.0",
  "repo_sha": "build git SHA or explicit unknown",
  "nozzle_core_sha": "recorded deps/nozzle git SHA or explicit unknown",
  "os": "macOS|Windows|Linux|unknown",
  "backend": "cpu|d3d11|metal|opengl|dmabuf|unknown",
  "role": "pattern|verify|self-test|sender|receiver|gui-preview|loopback-sender|loopback-receiver",
  "sender_name": "optional",
  "receiver_name": "optional",
  "test_case_id": "default/641x479/rgba8_unorm/frame7",
  "format": "rgba8_unorm",
  "native_texture_format": "unknown or backend value",
  "cpu_evidence_format": "rgba8_unorm",
  "dimensions": { "expected_width": 641, "expected_height": 479, "observed_width": 641, "observed_height": 479 },
  "frame": { "expected_index": 7, "observed_index": 7, "observed_count": 1, "changed_across_observations": true },
  "checks": {
    "orientation": "PASS|FAIL|INCONCLUSIVE",
    "channel_order": "PASS|FAIL|INCONCLUSIVE",
    "alpha": "PASS|FAIL|INCONCLUSIVE",
    "dimensions": "PASS|FAIL|INCONCLUSIVE",
    "format": "PASS|FAIL|INCONCLUSIVE",
    "stale_frame": "PASS|FAIL|INCONCLUSIVE",
    "stride_or_stretch": "PASS|FAIL|INCONCLUSIVE"
  },
  "verdict": "PASS|FAIL|SKIP|INCONCLUSIVE",
  "failure_reasons": ["vertical_flip", "rb_swap", "alpha_mismatch", "stale_frame", "dimension_mismatch", "format_mismatch", "byte_size_mismatch", "pixel_mismatch", "timeout", "missing_frame"],
  "covered_failure_reasons": ["vertical_flip", "rb_swap", "alpha_mismatch", "stale_frame"],
  "artifacts": [
    { "role": "raw_capture", "path": "capture.raw" },
    { "role": "preview_capture", "path": "capture.rgba" }
  ],
  "artifact_paths": ["capture.raw", "capture.rgba", "summary.json"]
}
```

A missing field must not be silently interpreted as PASS. If a backend cannot expose native format, synchronization, or transfer path, the evidence must say `unknown` or mark the row `INCONCLUSIVE`.

Preview artifacts are for humans only and must not replace raw evidence.

## CLI receiver 0.3.0

See the [receiver migration in the README](../README.md) for frame selection,
index progression and run verdicts. The count includes each call to the pixel
oracle, including a failed evaluation. If dimensions, format or buffer size
preclude comparing pixels, pixel checks and their counters are null. Index
zero has no previous-index comparison. Native format and a dedicated stride
check are unmeasured. Requested dimensions and format remain available when
no observation could be evaluated.

## GUI 0.4.0

The GUI retains identity/build fields, expected configuration, dimensions, frame,
checks, verdict, failure reasons and artifact lists. It deliberately does not
pretend generated pixels are received evidence:

- Without a received snapshot, `provenance` is `no_received_observation`, all
  receiver checks and verdict are `INCONCLUSIVE`, `unmeasured_reason` is
  `no_received_observation`, and observed dimensions/index/count and mismatch
  count are JSON null. A sender's successful commit is a separate
  `publication: {succeeded: true, published_count: N}` fact. Publication failure
  sets succeeded false and records `publication.error`; absent publication is
  null. This object also identifies its own sender_name and role. It represents
  the latest publication attempt, separately from the frozen received frame.
- With a received snapshot, `provenance` is `received`. Its expected settings,
  channel/role, observed dimensions/index/count, verification result and owned
  raw CPU bytes are copied at receive time. Runtime channel/loopback identity
  comes from endpoint creation, not later edits to controls. Capture takes no
  current controls as inputs. A bad received frame keeps its FAIL/reasons.
- Capture writes `received_raw` and `received_preview` only from that snapshot.
  Preview is regenerated from the same frozen bytes, not the current GL display.
  Raw half/float bytes are never narrowed. Generated previews/published source
  patterns are not cached as received images and produce no received artifacts.
  A subsequent preview regeneration does not replace an existing received
  snapshot. Explicit user Stop or starting/restarting clears it and the latest
  publication result; the last on-screen diagnostic may remain
  visible, but a capture after stop has no old received payload or metadata.
  Automatic failure teardown only releases runtime handles: a bad loopback
  frame remains capturable as FAIL, with its raw bytes. Publication failure
  likewise remains capturable. If an older good frame exists, capture preserves
  that received frame/result AND reports the latest failed publication attempt;
  its error is not inserted into that older frame's pixel failure reasons.
- `native_texture_format` stays unknown; the copy's storage format is only
  `cpu_evidence_format`. It is not inferred to be the native texture format.
- Dimension/format mismatch and malformed byte size return before pixel
  comparisons. In those cases the overall verdict remains FAIL with the actual
  reason, dimension/format checks retain their measured results, but orientation,
  channel order, alpha, stale-frame and stride checks are INCONCLUSIVE and
  mismatch_count is null. This does not alter the shared CLI verifier contract.
- Replace the GUI 0.2.0 `changed_across_observations` and `temporal_progress_reason`
  with `frame.index_progression`. The CLI receiver and GUI use the same index
  accumulator: zero or one observation gives `insufficient`, equal indices give
  `repeated`, strictly increasing indices (gaps allowed) give `advanced`,
  and mixed repeats/increases or a decrease give `nonmonotonic`. A pixel
  failure is still an evaluated observation; the frozen verdict and reasons
  describe its pixels independently of the index sequence.
- An acquisition returning `NOZZLE_ERROR_SENDER_CLOSED` ends the sequence.
  The frozen snapshot retains its original count and progression until another
  image is evaluated or the user explicitly stops/restarts. A timeout adds no
  sample and does not reset the sequence. The on-screen total and FPS cover
  the whole runtime since Start; JSON counts evaluated frames per connection.
- `capture_errors` lists failed writes. Only successfully written binary files
  enter artifacts/artifact_paths. Capture returns failure and the UI status
  reports a write error on any failed binary or JSON write. A JSON write failure
  is visible in the in-memory JSON/status (the failed destination cannot record
  its own error). Receiver verdict remains the pixel verdict, independently of
  capture I/O success. Partial bundles must not be treated as complete.
- Output names remain fixed and overwrite on capture. Files left by an older
  capture are not evidence of a newer observation; use the newly written JSON
  manifest and capture status. No atomic multi-file transaction is promised.

The GUI serializer is separate from the CLI receiver and the CLI self-test validator. No reserved
mapped-storage domain or generalized session framework is introduced.

`nozzle_tester_gui_evidence` tests production helpers with CPU fixtures and real
file I/O on CI, including bad pixels, immutable float payload/preview, stop,
source-only capture, and partial/JSON write failure. It is not GUI-window or GPU
transport smoke. Negative baseline `4e12796` produces six failures out of nine
assertions; the repair adds three focused publication/partial-write controls.
Follow-up negative `fe31bbc` adds the production tick/teardown path and fails
six of eighteen assertions. The failure-lifecycle repair adds mixed loopback
teardown and explicit Stop-after-failure controls (twenty assertions total).
Release callbacks are tracked, not native handle destruction in a GUI process;
the GUI uses the same dispatch/teardown functions with its real release callback.

## Build metadata

CI evidence must include concrete `repo_sha` and `nozzle_core_sha` values. Git worktree builds auto-detect those values. Source-archive or vendored builds without `.git` must pass them explicitly at configure time, for example:

```bash
cmake -S . -B build \
  -DNOZZLE_TESTER_GIT_SHA=<nozzle-tester-sha> \
  -DNOZZLE_TESTER_NOZZLE_CORE_SHA=<deps-nozzle-sha>
```

`unknown` is allowed in the schema only to make local ad-hoc builds explicit. `scripts/validate-evidence.py` intentionally rejects `unknown` because issue evidence without source identity is not useful.
