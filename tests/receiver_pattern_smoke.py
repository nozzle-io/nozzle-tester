#!/usr/bin/env python3
"""Real CLI producer/consumer pattern alignment; macOS CI, no backend skip."""

import argparse
import json
import re
import subprocess
import tempfile
import time
import uuid
from pathlib import Path


def run_case(cli: Path, directory: Path, name: str, fmt: str, frames: int) -> bool:
    stderr_path = directory / f"{fmt}-{frames}.stderr.log"
    with stderr_path.open("w") as stderr:
        sender = subprocess.Popen([
            str(cli), "sender", "--name", name, "--width", "65", "--height", "33",
            "--format", fmt, "--frames", str(frames), "--delay-ms", "0", "--hold-ms", "2000",
        ], stdout=subprocess.PIPE, stderr=stderr, text=True)
        try:
            deadline = time.monotonic() + 5
            ready = None
            while time.monotonic() < deadline:
                ready = re.search(r"sender_ready published_count=(\d+) payload_frame_index=(\d+)", stderr_path.read_text())
                if ready or sender.poll() is not None:
                    break
                time.sleep(0.02)
            if ready is None:
                raise RuntimeError("sender never reported successful final commit: " + stderr_path.read_text())
            receiver = subprocess.run([
                str(cli), "receiver", "--name", name, "--width", "65", "--height", "33",
                "--format", fmt, "--frames", "1", "--timeout-ms", "3000",
            ], capture_output=True, text=True, timeout=5)
            sender_output, _ = sender.communicate(timeout=5)
            evidence = json.loads(receiver.stdout)
            sender_evidence = json.loads(sender_output)
            payload_index = int(ready.group(2))
            observed_index = evidence["frame"]["observed_index"]
            passed = (sender.returncode == 0 and receiver.returncode == 0
                      and int(ready.group(1)) == frames and payload_index == observed_index == frames
                      and sender_evidence["frame"]["observed_index"] == payload_index
                      and evidence["verdict"] == "PASS" and evidence["checks"]["format"] == "PASS"
                      and evidence["failure_reasons"] == [])
            print(json.dumps({"format": fmt, "commits": frames, "payload_index": payload_index,
                              "observed_index": observed_index, "receiver_exit": receiver.returncode,
                              "verdict": evidence["verdict"], "reasons": evidence["failure_reasons"],
                              "case_pass": passed}), flush=True)
            return passed
        finally:
            if sender.poll() is None:
                sender.terminate()
                try:
                    sender.communicate(timeout=3)
                except subprocess.TimeoutExpired:
                    sender.kill()
                    sender.communicate()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cli", required=True, type=Path)
    args = parser.parse_args()
    failures = 0
    with tempfile.TemporaryDirectory(prefix="nozzle-233-") as temp:
        for fmt in ("rgba8_unorm", "bgra8_unorm", "rgba16_float", "rgba32_float"):
            name = "cli233-" + uuid.uuid4().hex[:12]
            # Recreate both processes with the same channel; the second run must
            # restart at 1 and finish at 3, not continue the old sender counter.
            for frames in (1, 3):
                try:
                    if not run_case(args.cli, Path(temp), name, fmt, frames):
                        failures += 1
                except (OSError, RuntimeError, ValueError, KeyError, subprocess.TimeoutExpired) as error:
                    print(f"CLI_PATTERN_ERROR format={fmt} commits={frames}: {error}", flush=True)
                    failures += 1
    print(f"CLI_PATTERN_PEER failed={failures} total=8", flush=True)
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
