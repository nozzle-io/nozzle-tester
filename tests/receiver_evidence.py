"""Hosted Metal. Parse JSON emitted by the production receiver, including errors."""
import json
import subprocess
import sys
import time
import traceback
import uuid

cli, publisher = sys.argv[1:]


def run(case, frames):
    name = "ev234-" + uuid.uuid4().hex[:12]
    with subprocess.Popen([publisher, name, case], stdin=subprocess.PIPE,
                          stdout=subprocess.PIPE, text=True) as peer:
        assert peer.stdout.readline().strip() == "ready", "publisher exited before first commit"
        with subprocess.Popen([cli, "receiver", "--name", name, "--width", "65",
                               "--height", "33", "--format", "rgba8_unorm",
                               "--frames", str(frames), "--timeout-ms", "1000"],
                              stdout=subprocess.PIPE, text=True) as receiver:
            if case == "advanced":
                deadline = time.monotonic() + 4
                while receiver.poll() is None and time.monotonic() < deadline:
                    time.sleep(0.05)
                    peer.stdin.write("p")
                    peer.stdin.flush()
            output, _ = receiver.communicate(timeout=6)
        peer.stdin.close()
        assert peer.wait(timeout=3) == 0
    e = json.loads(output)
    print(case, receiver.returncode, json.dumps(e), flush=True)
    f = e["frame"]
    idx = f["observed_index"]
    assert idx == f["expected_index"] and e["test_case_id"].endswith(f"/frame{idx}")
    assert e["schema_version"] == "0.3.0"
    assert f["observed_count"] == (2 if case == "advanced" else 1)
    assert f["index_progression"] == ("advanced" if case == "advanced" else "insufficient")
    assert idx >= 2 if case == "advanced" else idx == 1
    assert receiver.returncode == (1 if case in ("partial", "wrong") else 0)
    assert e["verdict"] == ("FAIL" if receiver.returncode else "PASS")
    assert e["frame_verdict"] == ("FAIL" if case == "wrong" else "PASS")
    if case == "wrong":
        assert "stale_frame" in e["failure_reasons"] and e["checks"]["stale_frame"] == "FAIL"
        assert e["mismatch_count"] > 0 and e["stale_previous_mismatch_count"] == 0
    else:
        assert e["mismatch_count"] == 0 and e["stale_previous_mismatch_count"] > 0
        assert e["checks"]["stale_frame"] == "PASS"
    assert e["transport_error"] == ("acquire_timeout" if case == "partial" else None)
    if case != "wrong":
        assert e["failure_reasons"] == (["acquire_timeout"] if case == "partial" else [])


failed = []
for case, frames in (("correct", 1), ("wrong", 2), ("partial", 2), ("advanced", 2)):
    try:
        run(case, frames)
    except (AssertionError, KeyError, ValueError) as error:
        traceback.print_exc()
        failed.append(case)
        print("failed", case, repr(error), flush=True)
missing = subprocess.run([cli, "receiver", "--name", "missing-" + uuid.uuid4().hex[:12],
                          "--frames", "1", "--timeout-ms", "100"],
                         capture_output=True, text=True, timeout=6)
empty = json.loads(missing.stdout)
assert missing.returncode == 1 and empty["verdict"] == "FAIL" and empty["transport_error"]
assert empty["failure_reasons"] == [empty["transport_error"]]
assert empty["frame_verdict"] is None and empty["test_case_id"] is None
assert empty["frame"] == {"expected_index": None, "observed_index": None,
                          "observed_count": 0, "index_progression": "insufficient"}
assert empty["mismatch_count"] is None and all(value is None for value in empty["checks"].values())
print("zero observations", missing.returncode, missing.stdout, flush=True)
if failed:
    sys.exit("receiver evidence failures: " + ", ".join(failed))
