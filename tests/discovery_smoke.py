#!/usr/bin/env python3
"""Process-level sender discovery smoke test for nozzle-tester-cli."""

from __future__ import annotations

import argparse
import subprocess
import sys
import time
import uuid
from pathlib import Path

SKIP_BACKEND_UNAVAILABLE = 77


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cli", required=True, type=Path)
    args = parser.parse_args()

    sender_name = f"nozzle_tester_smoke_{uuid.uuid4().hex[:12]}"
    sender = subprocess.Popen(
        [
            str(args.cli),
            "sender",
            "--name",
            sender_name,
            "--width",
            "320",
            "--height",
            "240",
            "--format",
            "rgba8_unorm",
            "--frames",
            "0",
            "--hold-ms",
            "5000",
            "--delay-ms",
            "0",
        ],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )

    try:
        deadline = time.monotonic() + 8.0
        last_stdout = ""
        last_stderr = ""
        while time.monotonic() < deadline:
            if sender.poll() is not None:
                stdout, stderr = sender.communicate(timeout=1)
                print(stdout, end="")
                print(stderr, end="", file=sys.stderr)
                if (
                    sender.returncode == 1
                    and "sender_create_failed: NOZZLE_ERROR_BACKEND_ERROR" in stderr
                ):
                    print(
                        "discovery smoke skipped: backend device unavailable in this environment",
                        file=sys.stderr,
                    )
                    return SKIP_BACKEND_UNAVAILABLE
                raise SystemExit(f"sender exited before discovery: code={sender.returncode}")

            discover = subprocess.run(
                [str(args.cli), "discover", "--name", sender_name, "--timeout-ms", "250"],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
            )
            last_stdout = discover.stdout
            last_stderr = discover.stderr
            if discover.returncode == 0 and sender_name in discover.stdout:
                print(discover.stdout, end="")
                return 0
            time.sleep(0.1)

        print(last_stdout, end="")
        print(last_stderr, end="", file=sys.stderr)
        raise SystemExit(f"sender was not discovered: {sender_name}")
    finally:
        if sender.poll() is None:
            sender.terminate()
            try:
                sender.communicate(timeout=3)
            except subprocess.TimeoutExpired:
                sender.kill()
                sender.communicate()


if __name__ == "__main__":
    raise SystemExit(main())
