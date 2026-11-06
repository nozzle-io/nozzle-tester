# Published pattern index contract

The normal GUI and CLI senders generate a frame-dependent pattern. Receivers
verify against the frame index in transport metadata, not their local loop count.
Core `f3a31335bec3d714bf0dcdac39307ebba89fa266` starts a new sender at counter0
and increments before publishing; its first successful frame is1.

## Failing-first coverage

The initial regression commit leaves the producer's zero-based generation intact.
The GUI's non-UI generation/acquire/map/copy/unlock/commit path is extracted into
`publish_gui_pattern` without changing that index. Tests use this same helper,
then the real native receive/copy/verifier. They require content index to equal
metadata and PASS, and require a deliberately wrong expected index to FAIL.

Native macOS cases cover four formats, widths65/641, first and second frames,
and fresh sender/receiver sessions recreated with the same channel. Public API
controls precede publication: unsupported-format acquire, acquired-frame discard,
and commit rejected while a CPU mapping remains active. The first real publication
must still have index1. Missing Metal and setup errors fail, never SKIP.

The separate CLI test launches actual sender and receiver processes. It waits for
`sender_ready` written only after the final successful commit, not a blind startup
sleep. It checks first and third committed frames and recreates both processes
with the same channel, using all four formats. JSON is parsed for actual receiver
index, verdict and failure reasons. The current pinned receiver acquires the latest
committed frame when connecting, so a receiver started after ready observes N.

## Failure and scope boundary

Acquire and discard do not advance the core counter. The tested active-mapping
commit rejection is before the counter increment. A later backend signal failure
can occur after increment; this test does not inject that failure or establish
counter recovery for it. The GUI stops/destroys runtime on publication failure;
the CLI breaks and destroys its sender. A fresh session resets both native counter
and local successful-publication count. Never continue a failed session using a
success-only count as if it were the native counter, and do not mix other publish
APIs into that same sender session.

This is not a fix for core#184: tests recreate the receiver too, rather than
keeping an old connected receiver across in-process sender replacement. There is
no GUI-window test or non-Metal pixel-runtime claim. CLI evidence aggregation and
schema concerns remain #234; GUI capture truthfulness remains #224, and editable
controls/content-freshness policy remains #231. Receiver expectations must not be
globally decremented to compensate for a producer defect.
