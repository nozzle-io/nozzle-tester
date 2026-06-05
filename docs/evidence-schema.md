# nozzle-tester evidence schema

The schema is intentionally stable and text-friendly so issue comments can paste summaries without rewriting results by hand.

Top-level fields:

```json
{
  "schema_version": "0.1.0",
  "tool": "nozzle-tester",
  "tool_version": "0.1.0",
  "repo_sha": "unknown",
  "nozzle_core_sha": "unknown",
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
    "stale_frame": "PASS|FAIL|INCONCLUSIVE",
    "stride_or_stretch": "PASS|FAIL|INCONCLUSIVE"
  },
  "verdict": "PASS|FAIL|SKIP|INCONCLUSIVE",
  "failure_reasons": ["vertical_flip", "rb_swap", "alpha_mismatch", "stale_frame", "dimension_mismatch", "pixel_mismatch", "timeout", "missing_frame"],
  "artifact_paths": ["capture.raw", "capture.png", "summary.json"]
}
```

A missing field must not be silently interpreted as PASS. If a backend cannot expose native format, synchronization, or transfer path, the evidence must say `unknown` or mark the row `INCONCLUSIVE`.
