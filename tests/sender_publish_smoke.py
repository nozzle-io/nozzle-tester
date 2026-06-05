#!/usr/bin/env python3
"""Process-level sender publish smoke test for nozzle-tester-cli."""

from __future__ import annotations

import argparse
import subprocess
import sys
import uuid
from pathlib import Path

SKIP_BACKEND_UNAVAILABLE = 77


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cli", required=True, type=Path)
    args = parser.parse_args()

    sender_name = f"nozzle_tester_publish_{uuid.uuid4().hex[:12]}"
    result = subprocess.run(
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
            "1",
            "--delay-ms",
            "0",
        ],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    print(result.stdout, end="")
    print(result.stderr, end="", file=sys.stderr)
    if result.returncode == 0:
        return 0
    if "sender_create_failed: NOZZLE_ERROR_BACKEND_ERROR" in result.stderr:
        print(
            "sender publish smoke skipped: backend device unavailable in this environment",
            file=sys.stderr,
        )
        return SKIP_BACKEND_UNAVAILABLE
    return result.returncode


if __name__ == "__main__":
    raise SystemExit(main())
