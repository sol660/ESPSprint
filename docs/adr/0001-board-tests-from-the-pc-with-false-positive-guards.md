# Board tests run from the PC, with false-positive guards

Status: accepted (2026-10-16, spec #14)

## Context

We wanted a passing set of tests to accompany every feature, so that a break can be traced to the feature that caused it. The PC logic tests (fakes) and the on-board Unity tests (one driver each) never run the real firmware the way the installer uses it. A test that passes when the board is unplugged, or that matches stale serial text, is worse than no test.

## Decision

The main test seam is the **whole firmware over USB serial**. A pytest + pyserial suite on the PC resets the plugged-in board, reads its output, sends installer commands and checks the replies. The two existing seams stay: wake-cycle logic on the PC for timing-heavy cases, and on-board Unity tests for one-driver fault-finding.

Replies the tests depend on are one line, `@tag key=value ...`. All other lines are ignored. Keys are only added, never renamed.

Guards against false positives:

- No board found, or a silent board, is a **FAIL**, never a skip.
- The serial input is cleared before the board is reset. Only replies after the reset count.
- Command replies echo a random number chosen by this test run (`ping <n>` → `@pong <n>`).
- Upload and alert tests match a record carrying this run's random tag that arrived after the test started. For uploads, there must be exactly one such row in Supabase, with values matching the printed reading.
- A PC-only check runs the hello test against a fake silent port and requires it to FAIL.

Slow behaviour (deep sleep, timer wake) is tested on a separate build environment that differs only in config timings.

## Consequences

- Each feature that touches the board adds at least one serial-seam test. The firmware must print tagged lines for anything a test checks.
- Board tests can't run in CI. CI runs the PC logic tests and the firmware build, and the board suites are run locally before merging.
- Test rows build up in Supabase because the device key is insert-only. They are marked with `test_tag`, left out of real views, and deleted by hand before real use.
- Test alerts go to the real ntfy topic, with a clear "TEST — not a real alert" line.
