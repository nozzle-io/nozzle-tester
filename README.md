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

The published Linux x64 build targets **x86-64-v2** (no AVX, AVX2, FMA or F16C). CI sets this explicitly and verifies the binaries with `deps/nozzle/scripts/check_x86_64_isa.sh`.

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
- `Capture evidence`, which saves the frozen receive-time record as `nozzle-tester-gui-evidence.json`. When a received snapshot exists, it also writes its unchanged raw bytes to `nozzle-tester-gui-capture.raw` and derives `nozzle-tester-gui-preview.rgba` from those same bytes. Without a snapshot it writes JSON only, with receiver checks marked INCONCLUSIVE. Generated previews and successful sends are not receiver verification.

GUI output uses schema `0.2.0`; CLI output remains `0.1.0`. User Stop/restart clears
the received snapshot; automatic failure teardown retains it for diagnosis.
Capture includes the latest publication result even when retaining an older
received frame. A failed artifact or JSON write is reported in capture
status, and failed artifacts are not listed as written. See
[the schema](docs/evidence-schema.md#gui-020) for provenance and partial-write limits.

The GUI is intentionally functional rather than polished. It must not be used as a screenshot-only oracle: report the exported JSON, raw captured payload, and optional preview artifact.


## Source archive / vendored builds

Evidence used in issues must identify both the tester source and the embedded nozzle core revision. Normal git checkouts auto-detect these values. If building from a source archive or another layout without `.git`, pass them explicitly:

```bash
cmake -S . -B build \
  -DNOZZLE_TESTER_GIT_SHA=<nozzle-tester-sha> \
  -DNOZZLE_TESTER_NOZZLE_CORE_SHA=<deps-nozzle-sha>
```

The evidence schema can represent `unknown`, but `scripts/validate-evidence.py` rejects it by design. Do not paste `unknown`-SHA evidence into nozzle-dev issues.

## Evidence boundary

A `PASS` JSON result from this tool is evidence for the specific tested input/capture. It is not a blanket support claim for a binding, host app, backend, or platform.
