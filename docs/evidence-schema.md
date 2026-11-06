# nozzle-tester evidence schema

CLI output remains version `0.1.0`. GUI output uses the distinct `0.2.0`
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

## GUI 0.2.0

The GUI retains identity/build fields, expected configuration, dimensions, frame,
checks, verdict, failure reasons and artifact lists. It deliberately does not
pretend generated pixels are received evidence:

- Without a received snapshot, `provenance` is `no_received_observation`, all
  receiver checks and verdict are `INCONCLUSIVE`, `unmeasured_reason` is
  `no_received_observation`, and observed dimensions/index/count and mismatch
  count are JSON null. A sender's successful commit is a separate
  `publication: {succeeded: true, published_count: N}` fact. Publication failure
  sets succeeded false and records its error; absent publication is null.
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
  snapshot. Stop/restart clears it; the last on-screen diagnostic may remain
  visible, but a capture after stop has no old received payload or metadata.
- `native_texture_format` stays unknown; the copy's storage format is only
  `cpu_evidence_format`. It is not inferred to be the native texture format.
- `changed_across_observations` is null and `temporal_progress_reason` is
  `not_measured`. `stale_frame` is the existing single-image pattern comparison
  against the previous index, not proof of temporal progress. No count-based
  freshness claim is made; content progression/control lifecycle remain #231.
- `capture_errors` lists failed writes. Only successfully written binary files
  enter artifacts/artifact_paths. Capture returns failure and the UI status
  reports a write error on any failed binary or JSON write. A JSON write failure
  is visible in the in-memory JSON/status (the failed destination cannot record
  its own error). Receiver verdict remains the pixel verdict, independently of
  capture I/O success. Partial bundles must not be treated as complete.
- Output names remain fixed and overwrite on capture. Files left by an older
  capture are not evidence of a newer observation; use the newly written JSON
  manifest and capture status. No atomic multi-file transaction is promised.

The GUI serializer is separate; the CLI serializer, its `0.1.0` consumer and
its existing aggregation semantics are unchanged (#234 is separate). No reserved
mapped-storage domain or generalized session framework is introduced.

`nozzle_tester_gui_evidence` tests production helpers with CPU fixtures and real
file I/O on CI, including bad pixels, immutable float payload/preview, stop,
source-only capture, and partial/JSON write failure. It is not GUI-window or GPU
transport smoke. Negative baseline `4e12796` produces six failures out of nine
assertions; the repair adds three focused publication/partial-write controls.

## Build metadata

CI evidence must include concrete `repo_sha` and `nozzle_core_sha` values. Git worktree builds auto-detect those values. Source-archive or vendored builds without `.git` must pass them explicitly at configure time, for example:

```bash
cmake -S . -B build \
  -DNOZZLE_TESTER_GIT_SHA=<nozzle-tester-sha> \
  -DNOZZLE_TESTER_NOZZLE_CORE_SHA=<deps-nozzle-sha>
```

`unknown` is allowed in the schema only to make local ad-hoc builds explicit. `scripts/validate-evidence.py` intentionally rejects `unknown` because issue evidence without source identity is not useful.
