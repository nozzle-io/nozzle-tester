"""Windows real-executable UTF-8 argv regression; no converter-only oracle."""

import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time
import uuid


def run(executable, *arguments, timeout=15):
    result = subprocess.run(
        [str(executable), *arguments], stdout=subprocess.PIPE,
        stderr=subprocess.PIPE, timeout=timeout,
    )
    return result, result.stdout.decode("utf-8"), result.stderr.decode("utf-8")


def check_invalid_utf16(executable):
    for invalid in ("\ud800", "\udc00"):
        result, out, err = run(executable, invalid)
        if result.returncode != 2 or "Invalid UTF-16 command line" not in err:
            raise AssertionError(f"malformed UTF-16 was not rejected: {result.returncode} {out!r} {err!r}")


def stop(process):
    if process.poll() is None:
        process.terminate()
    try:
        out, err = process.communicate(timeout=3)
    except subprocess.TimeoutExpired:
        process.kill()
        out, err = process.communicate(timeout=3)
    if process.returncode != 0:
        print(f"child exit={process.returncode} stdout={out!r} stderr={err!r}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", required=True, type=Path)
    args = parser.parse_args()
    if os.name != "nt":
        raise RuntimeError("this regression must launch the Windows executable")
    check_invalid_utf16(args.exe)
    name = "\u65e5\u672c \U0001f680 \"252\"-" + uuid.uuid4().hex[:8]
    with tempfile.TemporaryDirectory(prefix="nozzle-argv-") as directory:
        unicode_dir = Path(directory) / "\u65e5\u672c-\U0001f680"
        unicode_dir.mkdir()
        raw_path = unicode_dir / "\u753b\U0001f680.rgba"
        for command, path_option, evidence_name in (
            ("pattern", "--output", "\u51fa\U0001f680.json"),
            ("verify", "--input", "\u5165\U0001f680.json"),
        ):
            evidence_path = unicode_dir / evidence_name
            result, out, err = run(args.exe, command, path_option, str(raw_path),
                "--width", "320", "--height", "240", "--format", "rgba8_unorm",
                "--frame", "7", "--evidence", str(evidence_path))
            if result.returncode != 0:
                raise AssertionError(f"Unicode-path {command} failed: {out!r} {err!r}")
            record = json.loads(evidence_path.read_text(encoding="utf-8"))
            if (record["verdict"] != "PASS" or record["role"] != command
                    or record["artifact_paths"] != [str(raw_path)]
                    or record["frame"]["observed_index"] != 7
                    or raw_path.stat().st_size != 320 * 240 * 4):
                raise AssertionError(f"Unicode-path {command} evidence mismatch: {record!r}")
        log_path = Path(directory) / "sender.stderr.log"
        with log_path.open("wb") as log:
            sender = subprocess.Popen([
                str(args.exe), "sender", "--name", name, "--width", "320",
                "--height", "240", "--format", "rgba8_unorm", "--frames", "2",
                "--delay-ms", "0", "--hold-ms", "10000",
            ], stdout=subprocess.PIPE, stderr=log)
            try:
                # Reuse the CLI's successful-publication boundary, not a startup sleep.
                deadline = time.monotonic() + 5
                while b"sender_ready published_count=2 " not in log_path.read_bytes():
                    if sender.poll() is not None or time.monotonic() >= deadline:
                        raise AssertionError(f"sender did not become ready: {log_path.read_bytes()!r}")
                    time.sleep(0.02)
                discovered, out, err = run(args.exe, "discover", "--name", name, "--timeout-ms", "5000")
                if discovered.returncode != 0 or f" name={name} application=nozzle-tester " not in out:
                    raise AssertionError(f"discovery identity mismatch: {out!r} {err!r}")
                received, out, err = run(args.exe, "receiver", "--name", name,
                    "--width", "320", "--height", "240", "--format", "rgba8_unorm",
                    "--frames", "1", "--timeout-ms", "5000")
                if received.returncode != 0:
                    raise AssertionError(f"receiver failed: {out!r} {err!r}")
                record = json.loads(out)
                if record["verdict"] != "PASS" or record["sender_name"] != name:
                    raise AssertionError(f"receiver identity/pixels mismatch: {record!r}")
                out, _ = sender.communicate(timeout=15)
                if sender.returncode != 0:
                    raise AssertionError(f"sender failed: {out!r} {log_path.read_bytes()!r}")
                record = json.loads(out)
                if record["verdict"] != "PASS" or record["sender_name"] != name:
                    raise AssertionError(f"sender identity mismatch: {record!r}")
            finally:
                stop(sender)
                if sender.returncode != 0:
                    print(f"sender stderr={log_path.read_bytes()!r}")
    print("PASS Windows Unicode identity, pixels and input/output/evidence paths; strict rejection")


if __name__ == "__main__":
    main()
