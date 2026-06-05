# nozzle-tester

`nozzle-tester` is the conformance tool for nozzle runtime smoke evidence. It is not a sample app. Its job is to make weak texture-sharing evidence hard to submit.

The first implementation includes both:

- `nozzle-tester-cli` — CLI pattern generation, verification, native sender/receiver entry points, and JSON evidence output.
- `nozzle-tester` — GUI pattern preview plus sender / receiver / loopback modes using the same generator/oracle code.

## What this proves today

The current tool proves the shared pattern/oracle/evidence contract and can intentionally fail bad captures. It does **not** by itself prove that any integration supports nozzle runtime interop. Live sender/receiver evidence must still include the generated JSON result and any captured artifacts.

## Build

```bash
git submodule update --init --recursive
cmake -S . -B build -DNOZZLE_TESTER_BUILD_GUI=ON -DNOZZLE_TESTER_BUILD_TESTS=ON
cmake --build build --config Release
ctest --test-dir build --output-on-failure
```

Linux GUI builds require OpenGL/X11 development packages for GLFW plus nozzle's Linux backend dependencies.

## CLI examples

Generate a hostile non-symmetric pattern:

```bash
build/nozzle-tester-cli pattern \
  --width 641 --height 479 --format rgba8_unorm --frame 7 \
  --output build/pattern-641x479-rgba8.raw \
  --evidence build/pattern-641x479-rgba8.json
```

Verify a captured frame against the oracle:

```bash
build/nozzle-tester-cli verify \
  --input build/pattern-641x479-rgba8.raw \
  --width 641 --height 479 --format rgba8_unorm --expected-frame 7 \
  --evidence build/verify-641x479-rgba8.json
```

Run built-in bad-capture fixtures:

```bash
build/nozzle-tester-cli self-test --evidence build/self-test.json
```

Start a CPU writable-frame sender path:

```bash
build/nozzle-tester-cli sender --name nozzle_tester --width 320 --height 240 --format rgba8_unorm --frames 60 --evidence build/sender.json
```

Hold a registered sender open without publishing frames. This isolates process-level sender discovery from frame allocation/upload failures:

```bash
build/nozzle-tester-cli sender --name nozzle_tester_discovery --width 320 --height 240 --format rgba8_unorm --frames 0 --hold-ms 5000
```

List currently discoverable senders, or wait for a named sender:

```bash
build/nozzle-tester-cli discover --timeout-ms 0
build/nozzle-tester-cli discover --name nozzle_tester --timeout-ms 2000
```

Receive and verify frames:

```bash
build/nozzle-tester-cli receiver --name nozzle_tester --frames 10 --expected-width 320 --expected-height 240 --format rgba8_unorm --evidence build/receiver.json
```

## GUI

Run:

```bash
build/nozzle-tester
```

On macOS, the GUI build product is an app bundle:

```bash
open build/nozzle-tester.app
```

The GUI provides:

- mode selector: pattern preview, sender, receiver, loopback;
- channel name, dimensions, format, timeout, and frame controls;
- live preview, frame counters, FPS estimate, dropped-frame counter, and backend/path evidence fields;
- `Capture evidence`, which re-runs the oracle on the last raw observed payload and writes `nozzle-tester-gui-evidence.json`, `nozzle-tester-gui-capture.raw`, and `nozzle-tester-gui-preview.rgba`.

The GUI is intentionally functional rather than polished. It must not be used as a screenshot-only oracle: report the exported JSON, raw captured payload, and optional preview artifact.

## Evidence boundary

A `PASS` JSON result from this tool is evidence for the specific tested input/capture. It is not a blanket support claim for a binding, host app, backend, or platform.
