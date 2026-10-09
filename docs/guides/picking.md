# Ask the human to select geometry

Use a caller-generated request ID when an agent needs a specific human choice:

```text
synthcad-cli review pick request --id choose-mount-1 --kind surface --question "Which surface should receive the mount?" --project bracket --expect-revision DISPLAYED_REVISION --json
synthcad-cli review pick status choose-mount-1 --project bracket --json
synthcad-cli project events --after 0 --wait 30000 --project bracket --json
synthcad-cli review pick cancel choose-mount-1 --project bracket --json
```

`review pick request` accepts only `part`, `surface`, `edge` or `vertex`. A surface can resolve
to a planar face or curved patch. IDs contain 1–128 UTF-8 bytes and questions
contain 1–4096 UTF-8 bytes; neither permits NUL. The question supplies human
context and the pick result supplies geometry context. These commands do not
edit geometry. The viewer displays the question and permits explicit human
confirmation or cancellation; ordinary selection does not submit the answer.
The existing selection is preserved when a question starts. Confirm remains
disabled until the user makes a fresh selection of the allowed kind. The
question scrolls independently of the camera; Escape cancels outside text
entry and modal dialogs. Ending a request restores the previous pick mode.

A successful start returns `data: {created, request, eventCursor}`. The request
contains `id`, `kind`, `question`, its bound displayed `revision`, and `status`.
Status is `pending`, `confirmed`, `cancelled` or `invalidated`. A confirmed
request also contains `selection`, the geometry object described above
(`data.selection.geometry`), rather than the outer selection wrapper.
Cancellation and invalidation may include `reason`. `review pick status` and
`review pick cancel` return `data.request` and `data.eventCursor`, including terminal
outcomes. Snapshot exposes `guidedPick` (the active request or null) and
`eventCursor`.

Repeating the same ID with the same kind and question replays the existing
receipt, including a terminal result, with `created: false`; it creates no
additional UI event. The request keeps its original bound revision. Reusing an
ID with a different payload returns `invalid_argument`. A different request
while one is active returns `busy`. Completed request records remain available
for the running viewer, up to 1024 requests; reaching that limit returns `busy` and
requires closing and reopening the project. Cancellation is idempotent. Changed source/view
revisions and failed loads invalidate a pending request so that old geometry
cannot become an answer. A reload of unchanged valid source does not invalidate
the request, but clears its candidate and requires a fresh selection.
An explicit stale `--expect-revision` guard still fails on a retried `review pick request`;
omit the guard or use `review pick status` when recovering its original receipt.

`project events` requires `--after`, an opaque cursor string, or `0` to start at the
beginning of retained history. It returns `data: {events: [...], cursor}`.
Each event contains `cursor`, `type`, `requestId` and `revision`, with
`selection` or `reason` when relevant. Types are `pick-started`,
`pick-confirmed`, `pick-cancelled`, `pick-invalidated` and `viewer-closed`.
Persist the returned cursor after processing a successful response, and pass
it to the next call. Using a cursor captured before starting a pick allows
that call to observe its start and subsequent outcome.

The viewer retains at most 256 events. Cursors belong to one running viewer
and reset when it ends. A cursor from another viewer or older than retained
history returns `stale_cursor` (exit 13); inspect `review pick status` for your request
and use `project events --after 0` to recover retained history. This recovery response
includes `truncated: true` when earlier events have been discarded. Do not parse cursors or
assume that every prior event is still retained.

`--wait MS` is an integer from 0 to 300000, default 0. Zero reads immediately;
a positive value waits when no newer event exists. This wait is separate from
global `--timeout`: for events the effective request deadline is
`max(timeout, wait + 1000)` milliseconds, plus the normal transport delivery
grace. A wait that expires returns `timeout` with `error.details.cursor`; it
does not cancel a pick or advance the caller's cursor. Closing the viewer
wakes a live wait with `cancelled` and final-event details. Calls after the
project has closed follow the ordinary no_project error behavior.

`review pick status`, `review pick cancel` and `project events` reject `--expect-revision`: an agent
must still be able to recover an outcome after the scene reloads. An external
agent must explicitly call status or read/wait for events and process the
response. Viewer events do not automatically wake or message an agent.
