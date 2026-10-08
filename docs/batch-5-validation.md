# Batch 5: guided questions and event readback

SC09 adds a human-confirmed geometry choice to the existing shared viewer.
Agents continue editing model files; the CLI supplies review context and
outcomes rather than a second geometry engine or a chat interface.

## Workflow

```text
synthcad open viewer/tests/agent-fixtures/shared-design --session brackets
synthcad pick --id mounting-surface-1 --kind surface --question "Which surface should receive the mount?" -s brackets --json
synthcad pick-status mounting-surface-1 -s brackets --json
synthcad events --after 0 --wait 30000 -s brackets --json
synthcad pick-cancel mounting-surface-1 -s brackets --json
```

Use a unique caller-generated ID for each question. The viewer switches to the
requested Part, Surface, Edge or Vertex mode and shows a scrollable question
with Confirm and Cancel. Starting preserves the human's existing selection;
Confirm requires a fresh choice of the allowed kind. Confirmation checks the
displayed revision, current topology, visible owner and source files on disk.
Its result includes instance and source identities and a revision-bound token.
Groups and assembly/plate layouts continue referencing shared source geometry.
They do not create independent editable copies.

Ending a request restores the previous pick mode and leaves the user's selected
geometry intact. Agent highlights, export flags and model files are independent.
Changed revisions and failed loads invalidate pending requests. Unchanged
manual reloads clear the candidate without invalidating the request itself.
Search, modal dialogs, question scrolling and panel gestures retain input capture.

Matching retries replay the original receipt, including terminal results;
conflicting payloads fail and a different active question returns busy. The
session retains 1024 request records without evicting them and a 256-event ring.
Opaque cursors detect expired, future and other-session history. Recovery from
cursor `0` reports truncation. Bounded waits never submit/cancel a pick on timeout.
Closing the viewer wakes outstanding waits with cancellation and final events.
Windows named-pipe shutdown now drains the acknowledgement for up to one second
before disconnecting, preventing loss of those final response bytes.

See `synthcad docs cli` for schemas, limits, errors and examples. The external
agent must explicitly read or wait for outcomes; events do not wake an agent.
Bundled guides use UTF-8-safe literal chunks so growing CLI documentation remains
available from the standalone executable on MSVC as well as GCC.

## Verification

Windows MSVC Release and Ubuntu 24.04/GCC 13.3 Release pass all 18 C++ suites:
the previous 16 suites plus guided-pick state and guided-question UI tests.
The transport suite includes a live close-with-outstanding-wait regression.
CLI tests cover parsing, errors, duplicate flags, revision guards and wait limits.
State tests cover idempotency, conflicts, confirmation kinds, record limits,
event eviction, malformed cursors, timeout, reload and viewer closure.
UI tests cover bounds, gestures, keyboard actions, Unicode wrapping and exact
font-based scrolling through long questions.

The public `scripts/test-guided-pick.py` harness uses native messages directed
only to its own Windows HWND or X11 window. It verifies preservation of a prior
matching selection, fresh-choice gating, explicit confirmation, UI/CLI cancel,
duplicate requests before/after selection, maximum event wait with zero global
timeout, terminal replay, stale guards, long-question scrolling, failed-import
recovery, event retention and graceful-close delivery. Confirmed references
resolve to the intended placed instance and source definition.

Actual native runs cover Windows 125% (1600 × 900), Linux 100%, Linux 150%
(1920 × 1080) and Linux 200% (2560 × 1440). Question and scrolling screenshots
were inspected; the last line remains accessible and scrolling leaves the
camera unchanged. Native geometric picking, persistent sessions, shared-design
reload/layout checks, standalone stdout guidance and Windows icon/reload smoke
remain covered. Temporary fixtures and screenshots stay outside product commits.

These checks do not resolve SC08's outstanding normal-desktop Windows clipboard
acceptance, nor establish native Wayland, distribution packaging, CI automation,
printer validation or physical print performance. Those stories remain open.
