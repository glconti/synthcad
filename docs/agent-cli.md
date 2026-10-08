# SynthCAD local review CLI

`synthcad` connects short shell calls to a persistent local viewer. Open a project
once, edit its JavaScript files using your normal editor or agent, and inspect
the resulting geometry in the same viewer session. This increment provides
semantic review, geometric selection references, guided human selection and
reload acknowledgement, guarded geometry export and retained export history.
Model creation and edits remain ordinary source-file edits.

The existing `dingcad_viewer` executable, launch scripts, standalone scene
arguments, `--check-scene`, `--profile-scene`, `--render-scene` and `--ui-preview`
entry points remain available. The viewer also accepts the new review commands.
Use the `synthcad` executable for discoverable command parsing.

## Export current geometry

```text
synthcad export ./local-scenes/current-plate.3mf -s bracket --dry-run --json
synthcad export ./local-scenes/current-plate.3mf -s bracket --expect-revision TOKEN --json
synthcad export ./local-scenes/current-plate.stl -s bracket --visible-only --allow-warnings --replace --json
synthcad export-history -s bracket --json
```

The format defaults to the destination's case-insensitive `.3mf` or `.stl`
extension. An explicit `--format 3mf` or `--format stl` must match that extension;
missing or mismatched extensions are rejected.
Relative paths become absolute before dispatch. Export defaults to all
exportable parts; `--visible-only` restricts that set to visible parts. Group
aliases do not add physical copies. Standard 3MF preserves current placements
and separate objects; STL contains geometry. Choose printer, nozzle, material
and process settings in the slicer. Exact Bambu interoperability is not required.

The viewer owns both real exports and `--dry-run` validation. Dry runs perform
the same guards and report the proposed result without writing a destination.
Review warnings before acknowledging them with `--allow-warnings`; this does
not turn heuristic, missing, sliced or physical evidence into verification.
Printer and material metadata are optional for geometry export. Build dimensions
enable placement checks when supplied, while unknown exclusions remain unchecked.

`--expect-revision` binds the request to the displayed revision. `--replace`
explicitly permits an existing destination to be replaced. No export command
changes the selected view, geometry, user selection or export flags. Request
timeouts use the existing session and queue; a timeout does not restart the
viewer. The owner checks the request deadline before publishing a file.
If a timeout occurs around the final commit, inspect the destination and
`export-history` before retrying; a timeout is not proof that no file was saved.

`export-history` returns the published cache as an object containing `records`
and `diagnostics`; it does not scan destination files or open attachments.
An optional `--expect-revision` rejects a stale displayed context for this read.

## Discover the interface

```text
synthcad --help
synthcad help wait
synthcad screenshot --help
synthcad --version --json
synthcad capabilities --json
```

Help, version and capability discovery run without opening a GUI. Capabilities
describe this implementation, including `geometryEditing: false` and
`export: true`, `exportFormats: ["3mf", "stl"]` and `exportHistory: true`.

## Load guidance only when needed

Running `synthcad` without arguments, or `synthcad --help`, groups commands and
guidance by area. The instructions are bundled in the CLI: no checkout, network
request, viewer session or skill installation is needed to read them.

```text
synthcad docs                 # List available areas and bundle version
synthcad docs start           # Start a new design/review workflow
synthcad docs skill           # Full portable design workflow instructions
synthcad docs modeling        # Structure parametric models and scenes
synthcad docs api             # Implemented geometry API, from API.md
synthcad docs print-design    # Printing constraints and orientation decisions
synthcad docs profiles        # Printer, bed exclusions, nozzle and material setup
synthcad docs fit-and-assembly
synthcad docs build-plates
synthcad docs checks          # Geometry/plate checks, evidence and limitations
synthcad docs bambu-handoff
synthcad docs cli             # This command reference
synthcad docs projects        # Project files and revision semantics
synthcad docs design          # Shared source parts, instances and layouts
```

`docs AREA` writes the complete guide as plain UTF-8 text to stdout, with no
status prefix. `docs AREA --json` returns the same text in `data.content` with
`topic`, `title`, repository-relative `source`, `hash`, `bundleHash` and
`bundleVersion`. `docs --json` lists topic metadata. Content hashes identify
the instructions shipped with this build; runtime never looks for guide files
on the user's disk. Unknown areas return `not_found` and suggest discovery.
The guides distinguish current functionality from future automatic packing and
printer preset catalogs. Reading a guide does not enable a feature absent from capabilities.

## Open, edit, wait and review

Open a standalone `.js` file, a project directory containing `synthcad.json`,
or the manifest itself. See the [project and revision contract](agent-contract.md)
for named views and the manifest schema. Keep personal scenes and generated
artifacts in the repository's ignored `local-scenes/` directory.

PowerShell, with the built executables on `PATH`:

```powershell
$opened = synthcad open '.\local-scenes\Pièce\assembly.js' --session bracket --json | ConvertFrom-Json
if (-not $opened.ok) { throw $opened.error.message }
synthcad state -s bracket --json
synthcad snapshot -s bracket --json

# Edit the entry or an imported source file using your editor, then capture
# the desired disk revision. During initial startup, revision can return busy.
do {
    $desired = synthcad revision -s bracket --json | ConvertFrom-Json
    if (-not $desired.ok -and $desired.error.code -eq 'busy') {
        Start-Sleep -Milliseconds 100
    } else { break }
} while ($true)
if (-not $desired.ok) { throw $desired.error.message }

$loaded = synthcad wait -s bracket --revision $desired.data.revision --timeout 10000 --json | ConvertFrom-Json
if (-not $loaded.ok) { throw $loaded.error.message }
synthcad highlight base lid -s bracket --frame --expect-revision $loaded.revision --json
synthcad screenshot '.\local-scenes\review.png' -s bracket --expect-revision $loaded.revision --json
```

Linux shell, with `jq` available for extracting JSON fields:

```sh
synthcad open './local-scenes/Pièce/assembly.js' --session bracket --json
# Edit the source files before requesting the revision.
while true; do
    desired=$(synthcad revision -s bracket --json)
    status=$?
    if [ "$status" -eq 0 ]; then break; fi
    if [ "$status" -ne 11 ]; then printf '%s\n' "$desired" >&2; exit "$status"; fi
    sleep 0.1
done
token=$(printf '%s' "$desired" | jq -r '.data.revision')
loaded=$(synthcad wait -s bracket --revision "$token" --timeout 10000 --json)
status=$?
if [ "$status" -ne 0 ]; then printf '%s\n' "$loaded" >&2; exit "$status"; fi
displayed=$(printf '%s' "$loaded" | jq -r '.revision')
synthcad selection -s bracket --json
synthcad frame --selection -s bracket --expect-revision "$displayed" --json
synthcad screenshot './local-scenes/review.png' -s bracket --json
```

The `revision` command captures current bytes of the active view's known
dependencies. It returns a requested token; `wait` checks those bytes, the active
view and the completed evaluation's dependency graph. Do not infer success from
an unchanged screenshot, a timestamp, or the existence of a previous valid model.

A successful `wait` returns both `data.requestedRevision` and
`data.displayedRevision`; the envelope's `revision` is the displayed revision.
The requested token includes the active view; the displayed identifier describes
the full consumed source graph, including any newly discovered imports, plus
the active view and resolved layout. They are
distinct identifiers. Use the **displayed** revision for `--expect-revision` on subsequent
review actions. If files change again, request a fresh token and wait again.

`state` and `snapshot` expose `status`, `attemptedRevision`, `displayedRevision`,
`diagnostic` and `exportValid`. An initial load may still be in progress after
`open` succeeds. `revision` returns `busy` (exit 11) until a dependency list is
available. A failed reload keeps the last valid geometry, reports `load_failed`
when waiting for the broken revision, and disables GUI export until recovery.
A newer edit or active-view change produces `superseded` instead of acknowledging
the earlier request.

Snapshots also report `sourceRevision`, `displayedView` and `design` (null for
legacy scenes). A shared-design graph supplies source-part IDs, physical
instance IDs, groups, views and effective transforms. A tree alias references
the same instance: it does not add another object to `parts` or exports. Each
graph part includes `sourcePartId`, `instanceId` and `transform`; graph groups
include their authored `sourceId`. Read `synthcad docs design` before authoring
an assembly and multiple print views from shared definitions.

## Commands and options

`overview` and `profile` read context stored with the displayed scene revision.
They do not reread a changed manifest independently of scene reload. Use
`--expect-revision TOKEN` when a review depends on a specific displayed revision.
Profile context reports setup status, missing fields and identifiers, provisional
values, scoped validation errors, metadata readiness, and a deterministic
`profileRevision`. Readiness does not mean geometry checks have passed. Printer
context is project-local; previous projects and global defaults are never inherited.
Slicer IDs are recorded as metadata and never reported as verified presets.

Run `synthcad docs profiles` for setup questions and the manifest contract, or
`synthcad profile --template` for an incomplete JSON fragment that can be reviewed
before dimensions, nozzle, and material are known. Template output requires no
viewer or session and cannot be combined with `--expect-revision`.

`checks` reads cached manufacturing results for the displayed geometry and
profile. The viewer computes these once per successful load, including named
plate layouts; reading them does not run another model or slicer. Each result
includes scope, method, evidence, affected instance IDs, next actions and a
source/model/profile revision basis. Use the response's displayed `revision`
with `highlight ... --expect-revision` to discuss affected geometry safely.
After source edits or load failure, unguarded reads retain prior evidence with
`current: false`; guarded reads reject the stale revision. See `docs checks`.

| Command | Behavior |
| --- | --- |
| `docs [AREA]` | List guidance areas, or print complete bundled instructions to stdout without a viewer or filesystem setup. |
| `overview` | Read project overview and profile context stored for the displayed revision; supports `--expect-revision`. |
| `profile` | Read the displayed overview's project-local printer/material context; supports `--expect-revision`. |
| `checks` | Read cached manufacturing/plate evidence, currentness and affected instance IDs; supports `--expect-revision`. |
| `profile --template` | Print an incomplete manifest fragment without a session. Default stdout is standalone JSON; `--json` wraps it in the protocol envelope. |
| `open PATH [--hidden]` | Start or reuse a project session. `--hidden` requests an automation window; a graphics context is still required. |
| `sessions` | List reachable CLI-managed sessions, including session name, project path, process ID and local endpoint. |
| `snapshot` | Read parts, groups, authored annotations, bounds, selection, highlights, camera and load state. |
| `selection` | Read the human's part/group or geometric selection, or `null` when absent. |
| `reference TOKEN` | Resolve a copied selection reference using the current displayed geometry. |
| `pick --id ID --kind part\|surface\|edge\|vertex --question TEXT` | Ask the human to select geometry and immediately return a request receipt. |
| `pick-status ID` | Read the pending or retained terminal request outcome. |
| `pick-cancel ID` | Cancel a request; repeated cancellation is idempotent. |
| `events --after CURSOR [--wait MS]` | Read retained session events after a cursor, optionally waiting for an event. |
| `state` | Read published scene and load state, including attempted/displayed revisions and diagnostics. |
| `revision` | Capture a requested revision from the active session's known source files on disk. |
| `wait --revision TOKEN` | Wait for that expected source snapshot to complete successfully, or return a specific failure. |
| `highlight PART_IDS... [--frame]` | Replace agent highlights, optionally framing them. IDs can also be group keys from the snapshot. |
| `highlight --clear` | Clear agent highlights. |
| `frame [PART_IDS...]` | Frame visible parts of the scene, or the given part/group references. |
| `frame --selection` | Frame visible descendants of the current human selection. |
| `view NAME` | Switch to a named project view and evaluate it. Standalone scenes expose the `scene` view. |
| `screenshot PATH [--replace]` | Save a `.png` image. The result contains its path; an existing destination requires `--replace`. |
| `export PATH [--format 3mf\|stl] [--visible-only] [--replace] [--allow-warnings] [--dry-run]` | Request guarded export of the current placed geometry from the viewer. |
| `export-history` | Read cached export records and diagnostics published by the viewer. |
| `capabilities` | Discover the implemented command list and features. |
| `version`, `--version` | Read application and protocol versions. |
| `help [COMMAND]`, `COMMAND --help` | Discover usage and options without launching a viewer. |

Global options work before or after the command:

- `--session NAME` or `-s NAME`: address a session, or name a newly opened one.
- `--json`: emit a structured command result for automation.
- `--timeout MS`: integer timeout from `0` to `300000` milliseconds; default
  `10000`. The transport allows a small response-delivery grace period after
  the viewer's request deadline.
- `--help` or `-h`: print help.
- `--`: treat remaining positional arguments literally, including paths or IDs
  starting with a dash. Quote paths containing spaces in the shell.

`snapshot`, `selection`, `reference`, `state`, `highlight`, `frame`, `view`, `screenshot`, `export`, `export-history` and `pick`
accept `--expect-revision TOKEN`. It requires the currently displayed revision
to match and its consumed files to remain current on disk. A stale guard returns
`stale_revision` before acting. Agent highlights are separate from human
selection, visibility and exportability. The human can clear them with Escape;
reload retains highlights only for surviving authored IDs.

Geometric selections preserve the selection object's `key`, `name`, `group`
and `partIds` fields and add a `geometry` object. Its `kind` is `part`,
`planar-face`, `curved-patch`, `edge` or `vertex`; it includes `reference`,
`partId`, `revision`, `sourcePartId`, `instanceId`, `position`, `normal` and
`bounds`. Coordinates and bounds use model millimeters. Legacy parts have
null source/instance context. A clicked position has `positionKind: "hit"`;
resolving a copied reference supplies a point on the current feature with
`positionKind: "representative"`. A part reference uses its bounds center.
The normal is null when the feature has no single normal, including a
resolved curved patch, an edge or a vertex.
If an unusually long authored ID would exceed the token limit, the context
remains available with `reference:null` and `referenceError`; Copy is disabled
but selection and clearing still work. `snapshot.selectionDiagnostics` reports
parts whose meshes do not support semantic feature selection; use Part mode.

Copy the reference from the viewer's selection inspector or read
`data.selection.geometry.reference` from `selection --json`, then resolve it:

```text
synthcad reference scsel1.HEX_PAYLOAD -s bracket --json
```

References are portable ASCII strings with prefix `scsel1.` and a hexadecimal
canonical JSON payload, bounded to 16 KiB decoded. The payload contains only
`revision`, `partId`, `kind`, and, for geometric features, `topologyKey` (a
decimal string preserving all 64 bits) and `id`. Source-part and instance
context comes from the current scene; it is never accepted from a token.
References are local to a displayed revision and owning part, with topology
identity checked against the current geometry. Reloading, changing view,
moving an instance or changing its mesh can invalidate a reference. Malformed
tokens return `invalid_argument`; stale revision references return
`stale_revision`; an owner missing from the current view returns `not_found`,
while a mismatched topology or feature returns `stale_revision`. Capture a
fresh selection after the scene changes. Resolving a reference is a read-only
review action and does not change the human's selection.

## Ask the human to select geometry

Use a caller-generated request ID when an agent needs a specific human choice:

```text
synthcad pick --id choose-mount-1 --kind surface --question "Which surface should receive the mount?" -s bracket --expect-revision DISPLAYED_REVISION --json
synthcad pick-status choose-mount-1 -s bracket --json
synthcad events --after 0 --wait 30000 -s bracket --json
synthcad pick-cancel choose-mount-1 -s bracket --json
```

`pick` accepts only `part`, `surface`, `edge` or `vertex`. A surface can resolve
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
(`snapshot.selection.geometry`), rather than the outer selection wrapper.
Cancellation and invalidation may include `reason`. `pick-status` and
`pick-cancel` return `data.request` and `data.eventCursor`, including terminal
outcomes. Snapshot exposes `guidedPick` (the active request or null) and
`eventCursor`.

Repeating the same ID with the same kind and question replays the existing
receipt, including a terminal result, with `created: false`; it creates no
additional UI event. The request keeps its original bound revision. Reusing an
ID with a different payload returns `invalid_argument`. A different request
while one is active returns `busy`. Completed request records remain available
for the session, up to 1024 requests; reaching that limit returns `busy` and
requires a new session. Cancellation is idempotent. Changed source/view
revisions and failed loads invalidate a pending request so that old geometry
cannot become an answer. A reload of unchanged valid source does not invalidate
the request, but clears its candidate and requires a fresh selection.
An explicit stale `--expect-revision` guard still fails on a retried `pick`;
omit the guard or use `pick-status` when recovering its original receipt.

`events` requires `--after`, an opaque cursor string, or `0` to start at the
beginning of retained history. It returns `data: {events: [...], cursor}`.
Each event contains `cursor`, `type`, `requestId` and `revision`, with
`selection` or `reason` when relevant. Types are `pick-started`,
`pick-confirmed`, `pick-cancelled`, `pick-invalidated` and `viewer-closed`.
Persist the returned cursor after processing a successful response, and pass
it to the next call. Using a cursor captured before starting a pick allows
that call to observe its start and subsequent outcome.

The session retains at most 256 events. Cursors belong to one viewer session
and reset when it ends. A cursor from another session or older than retained
history returns `stale_cursor` (exit 13); inspect `pick-status` for your request
and use `events --after 0` to recover retained history. This recovery response
includes `truncated: true` when earlier events have been discarded. Do not parse cursors or
assume that every prior event is still retained.

`--wait MS` is an integer from 0 to 300000, default 0. Zero reads immediately;
a positive value waits when no newer event exists. This wait is separate from
global `--timeout`: for events the effective request deadline is
`max(timeout, wait + 1000)` milliseconds, plus the normal transport delivery
grace. A wait that expires returns `timeout` with `error.details.cursor`; it
does not cancel a pick or advance the caller's cursor. Closing the viewer
wakes a live wait with `cancelled` and final-event details. Calls after the
session has closed follow the ordinary session-error behavior.

`pick-status`, `pick-cancel` and `events` reject `--expect-revision`: an agent
must still be able to recover an outcome after the scene reloads. An external
agent must explicitly call status or read/wait for events and process the
response. Viewer events do not automatically wake or message an agent.

## Session lifecycle and local access

Repeated and simultaneous `open` calls for the same canonical project reuse its
session. A session name already assigned to another project is an error; a
project already opened under a different name is also reported explicitly.
Without `--session`, requests use the sole running session and reject multiple
sessions as ambiguous. Ordinary legacy viewer windows are not automatically
registered as CLI-managed sessions.

The viewer continues running after the CLI exits. Close its window to end the
session. Later calls return an actionable session error; dead process records
are removed after checking the process identity. An `open` timeout can leave a
viewer starting in the background: inspect `sessions` or retry the same `open`.
Pending launch records prevent the retry from launching a duplicate process.
An alive but unresponsive existing project returns `busy` rather than creating
another window.

Windows uses local named pipes restricted to the current user's SID and rejects
remote pipe clients. Linux uses Unix domain sockets in a directory owned by the
current user with mode `0700`; socket permissions are `0600`. Registry records
include process-start identity and an endpoint nonce checked by the server.
There is no network service or cloud-account requirement.

By default the CLI launches `dingcad_viewer` beside its own executable. Override
that location with an absolute `SYNTHCAD_VIEWER` path when testing or using a
custom installation. `SYNTHCAD_SESSION_DIR` overrides the per-user temporary
registry directory for isolated tests; set it consistently for the CLI and
viewer. The directory is secured for the current user. A spawned viewer inherits
these environment settings.

## Structured responses and errors

Protocol version 1 uses one response envelope:

```json
{
  "protocolVersion": 1,
  "ok": true,
  "command": "wait",
  "session": "bracket",
  "revision": "DISPLAYED_REVISION",
  "data": {
    "status": "ready",
    "requestedRevision": "REQUESTED_TOKEN",
    "displayedRevision": "DISPLAYED_REVISION"
  }
}
```

Failures contain `error: {code, message, details?}` instead of `data`. Session,
revision and error details are optional when unavailable. Routine results do
not contain meshes, screenshot bytes or encoded images. Snapshot size grows
with semantic scene size; the transport rejects incoming messages over 16 MiB.

Machine-readable command output goes to stdout. Diagnostics belong on stderr;
viewer output is detached from the CLI response stream. This increment does
not provide a persistent viewer logfile, log rotation, or a CLI log-history
command. Use the structured `diagnostic` field for scene evaluation failures.
Use `capabilities --json` for machine-readable command discovery; `--json`
with help returns the help text in a structured envelope.

| Exit | Error code | Meaning / next step |
| --- | --- | --- |
| 0 | — | Command completed successfully. |
| 2 | `invalid_argument` | Fix unknown/missing options, invalid metadata or conflicting session naming. |
| 3 | `no_session` | Open a project or choose a live session. |
| 4 | `ambiguous_session` | Specify `--session NAME`. |
| 5 | `load_failed` | Inspect diagnostics, fix the source and capture a new revision. |
| 6 | `timeout` | The bounded request expired; inspect state before retrying. |
| 7 | `stale_revision` | Capture a fresh revision, wait, then use its displayed revision. |
| 8 | `superseded` | Expected files or active view changed; request a fresh token. |
| 9 | `cancelled` | The viewer closed while a request was pending. |
| 10 | `io_error` | Check filesystem access, screenshot destination and local transport. |
| 11 | `busy` | Initial dependencies are unavailable, an existing process is unresponsive, or a GUI modal blocks review; retry when ready. |
| 12 | `not_found` | A requested guide, part, group, view or transport-level project path is missing. |
| 13 | `stale_cursor` | The cursor belongs to another session or precedes retained event history; recover via request status and retained history. |
| 14 | `warnings_present` | Review reported check warnings; use `--allow-warnings` to acknowledge them explicitly. |
| 15 | `destination_exists` | Choose another path or use `--replace` after reviewing the existing destination. |
| 16 | `empty_export` | The selected all/visible export set contains no geometry. |

Unknown internal error categories use exit 1. No successful retained scene is
returned as acknowledgement of a failed current edit.

Windows and Linux transport regression tests cover persistent process launch,
simultaneous opens, Unicode paths, concurrency, timeouts and stale records.
Linux socket testing does not establish full Linux GUI or rendering validation.
