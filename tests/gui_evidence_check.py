#!/usr/bin/env python3
"""GUI production evidence helpers; CPU fixtures, not GPU/window smoke."""

import argparse
import json
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cases", required=True)
    args = parser.parse_args()
    failures = 0
    with tempfile.TemporaryDirectory(prefix="nozzle-gui-evidence-") as directory:
        run = subprocess.run([args.cases], cwd=directory, capture_output=True, text=True, timeout=20)
        if run.returncode != 0:
            print(run.stdout, run.stderr)
            return 1
        cases = {}
        for line in run.stdout.splitlines():
            item = json.loads(line)
            item["record"] = json.loads(item["record"])
            cases[item["case"]] = item

        def check(name, condition):
            nonlocal failures
            print(f"GUI_EVIDENCE {name} {'PASS' if condition else 'FAIL'}", flush=True)
            failures += not condition

        sender = cases["sender"]["record"]
        check("sender_unmeasured", sender["verdict"] == "INCONCLUSIVE"
              and all(v == "INCONCLUSIVE" for v in sender["checks"].values())
              and sender["frame"]["observed_count"] is None
              and sender.get("publication", {}).get("succeeded") is True)
        generated = cases["generated_capture"]["record"]
        check("generated_not_received", generated["verdict"] == "INCONCLUSIVE"
              and not any(a["role"] in ("raw_capture", "received_raw") for a in generated["artifacts"])
              and not Path(directory, "source-capture.raw").exists())
        frozen = cases["frozen_capture"]["record"]
        check("frozen_metadata", frozen["role"] == "loopback-receiver"
              and frozen["sender_name"] == "original-channel"
              and frozen["frame"]["observed_index"] == 7
              and frozen["dimensions"]["expected_width"] == 65)
        check("float_capture_exact", cases["frozen_capture"]["raw_matches"]
              and frozen["cpu_evidence_format"] == "rgba32_float")
        stopped = cases["stopped_capture"]["record"]
        check("stop_clears_snapshot", stopped["frame"]["observed_count"] is None
              and not Path(directory, "stopped-capture.raw").exists())
        failed = cases["failed_write"]
        check("write_failure_visible", not failed["capture_success"]
              and failed["record"].get("capture_errors")
              and not failed["record"]["artifacts"])
        partial = cases["raw_write_failure"]
        partial_disk = json.loads(Path(directory, "rawfail-evidence.json").read_text())
        check("partial_write", not partial["capture_success"]
              and partial_disk["capture_errors"]
              and [a["role"] for a in partial_disk["artifacts"]] == ["received_preview"])
        json_failed = cases["json_write_failure"]
        check("json_write", not json_failed["capture_success"]
              and json_failed["record"]["capture_errors"] == ["write_failed: jsonfail-evidence.json"]
              and len(json_failed["record"]["artifacts"]) == 2)
        publication = cases["publication_failure"]["record"]
        check("publication_failure", publication["publication"] == {"succeeded": False, "published_count": 2}
              and publication["role"] == "loopback-sender" and publication["sender_name"] == "failed-sender"
              and publication["verdict"] == "INCONCLUSIVE"
              and all(v == "INCONCLUSIVE" for v in publication["checks"].values()))
        for name in ("bad_received", "bad_capture"):
            record = cases[name]["record"]
            check(name, record["verdict"] == "FAIL" and "rb_swap" in record["failure_reasons"])
        # Validate the actual file, not only the in-memory string.
        disk = json.loads(Path(directory, "frozen-evidence.json").read_text())
        check("gui_schema", disk["schema_version"] == "0.2.0")
        bad = cases["bad_sequence"]
        check("bad_failure_teardown", bad["capture_success"] and bad["raw_matches"]
              and bad["record"]["verdict"] == "FAIL" and "rb_swap" in bad["record"]["failure_reasons"]
              and any(a["role"] == "received_raw" for a in bad["record"]["artifacts"]))
        failed_publish = cases["publish_sequence"]
        pub = failed_publish["record"].get("publication") or {}
        check("publish_failure_teardown", failed_publish["capture_success"] and failed_publish["raw_matches"]
              and pub.get("succeeded") is False and pub.get("error") == "commit_failed"
              and failed_publish["record"]["sender_name"] == "failed-sender")
        mixed = cases["mixed_publication"]["record"]
        pub = mixed.get("publication") or {}
        check("old_receive_latest_publication", mixed["verdict"] == "PASS"
              and mixed["frame"]["observed_index"] == 7
              and pub.get("succeeded") is False and pub.get("error") == "commit_failed"
              and any(a["role"] == "received_raw" for a in mixed["artifacts"]))
        for name, reason in (("dimension_early_return", "dimension_mismatch"),
                             ("format_early_return", "format_mismatch"),
                             ("bytes_early_return", "byte_size_mismatch")):
            record = cases[name]["record"]
            check(name, record["verdict"] == "FAIL" and reason in record["failure_reasons"]
                  and all(record["checks"][k] == "INCONCLUSIVE" for k in
                          ("orientation", "channel_order", "alpha", "stale_frame", "stride_or_stretch"))
                  and record["mismatch_count"] is None)
    print(f"GUI_EVIDENCE failed={failures} total=18")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
