#!/usr/bin/env python3
import json
import sys
from pathlib import Path

REQUIRED_TOP_LEVEL = [
    "schema_version",
    "tool",
    "tool_version",
    "repo_sha",
    "nozzle_core_sha",
    "os",
    "backend",
    "role",
    "test_case_id",
    "format",
    "native_texture_format",
    "cpu_evidence_format",
    "dimensions",
    "frame",
    "checks",
    "verdict",
    "failure_reasons",
    "covered_failure_reasons",
    "artifacts",
    "artifact_paths",
]

REQUIRED_CHECKS = [
    "orientation",
    "channel_order",
    "alpha",
    "dimensions",
    "stale_frame",
    "stride_or_stretch",
]

REQUIRED_SELF_TEST_REASONS = {
    "vertical_flip",
    "rb_swap",
    "alpha_mismatch",
    "stale_frame",
}


def fail(message: str) -> int:
    print(f"validate-evidence: {message}", file=sys.stderr)
    return 1


def main() -> int:
    if len(sys.argv) != 2:
        return fail("usage: validate-evidence.py PATH")

    path = Path(sys.argv[1])
    try:
        data = json.loads(path.read_text())
    except Exception as exception:
        return fail(f"failed to parse JSON: {exception}")

    for key in REQUIRED_TOP_LEVEL:
        if key not in data:
            return fail(f"missing top-level field: {key}")

    if data["schema_version"] != "0.1.0":
        return fail("unexpected schema_version")
    if data["tool"] != "nozzle-tester":
        return fail("unexpected tool")
    if data["verdict"] != "PASS":
        return fail("expected PASS verdict")
    if data["repo_sha"] == "unknown" or data["nozzle_core_sha"] == "unknown":
        return fail("build metadata SHA fields must not be unknown")

    dimensions = data["dimensions"]
    for key in ["expected_width", "expected_height", "observed_width", "observed_height"]:
        if key not in dimensions or not isinstance(dimensions[key], int) or dimensions[key] < 1:
            return fail(f"invalid dimensions.{key}")

    frame = data["frame"]
    for key in ["expected_index", "observed_index", "observed_count", "changed_across_observations"]:
        if key not in frame:
            return fail(f"missing frame.{key}")

    checks = data["checks"]
    for key in REQUIRED_CHECKS:
        if checks.get(key) not in {"PASS", "FAIL", "INCONCLUSIVE"}:
            return fail(f"invalid checks.{key}")

    covered = set(data["covered_failure_reasons"])
    missing = REQUIRED_SELF_TEST_REASONS - covered
    if missing:
        return fail("missing covered failure reasons: " + ",".join(sorted(missing)))

    artifacts = data["artifacts"]
    if not isinstance(artifacts, list) or not artifacts:
        return fail("artifacts must be a non-empty list")
    for artifact in artifacts:
        if not isinstance(artifact, dict) or not artifact.get("role") or not artifact.get("path"):
            return fail("each artifact must have role and path")

    artifact_paths = data["artifact_paths"]
    if not isinstance(artifact_paths, list) or not artifact_paths:
        return fail("artifact_paths must be a non-empty list")

    print(f"validate-evidence: PASS {path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
