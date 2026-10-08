# SynthCAD local review CLI

`synthcad` connects short shell calls to a persistent local viewer. Open a project
once, edit its JavaScript files using your normal editor or agent, and inspect
the resulting geometry in the same viewer session. This increment provides
semantic review and reload acknowledgement. It does not create geometry, offer
geometric face/edge picking, or export models through the new CLI.

The existing `dingcad_viewer` executable, launch scripts, standalone scene
arguments, `--check-scene`, `--profile-scene`, `--render-scene` and `--ui-preview`
entry points remain available. The viewer also accepts the new review commands.
Use the `synthcad` executable for discoverable command parsing.

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
`export: false`; they are not a roadmap of planned features.

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
synthcad docs fit-and-assembly
synthcad docs build-plates
synthcad docs bambu-handoff
synthcad docs cli             # This command reference
synthcad docs projects        # Project files and revision semantics
```

`docs AREA` writes the complete guide as plain UTF-8 text to stdout, with no
status prefix. `docs AREA --json` returns the same text in `data.content` with
`topic`, `title`, repository-relative `source`, `hash`, `bundleHash` and
`bundleVersion`. `docs --json` lists topic metadata. Content hashes identify
the instructions shipped with this build; runtime never looks for guide files
on the user's disk. Unknown areas return `not_found` and suggest discovery.
The guides distinguish current functionality from future printer, plate and
3MF features. Reading a guide does not enable a feature absent from capabilities.

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
the full consumed source graph, including any newly discovered imports. They are
distinct identifiers. Use the **displayed** revision for `--expect-revision` on subsequent
review actions. If files change again, request a fresh token and wait again.

`state` and `snapshot` expose `status`, `attemptedRevision`, `displayedRevision`,
`diagnostic` and `exportValid`. An initial load may still be in progress after
`open` succeeds. `revision` returns `busy` (exit 11) until a dependency list is
available. A failed reload keeps the last valid geometry, reports `load_failed`
when waiting for the broken revision, and disables GUI export until recovery.
A newer edit or active-view change produces `superseded` instead of acknowledging
the earlier request.

## Commands and options

| Command | Behavior |
| --- | --- |
| `docs [AREA]` | List guidance areas, or print complete bundled instructions to stdout without a viewer or filesystem setup. |
| `open PATH [--hidden]` | Start or reuse a project session. `--hidden` requests an automation window; a graphics context is still required. |
| `sessions` | List reachable CLI-managed sessions, including session name, project path, process ID and local endpoint. |
| `snapshot` | Read parts, groups, authored annotations, bounds, selection, highlights, camera and load state. |
| `selection` | Read the human's part/group selection, or `null` when absent. |
| `state` | Read published scene and load state, including attempted/displayed revisions and diagnostics. |
| `revision` | Capture a requested revision from the active session's known source files on disk. |
| `wait --revision TOKEN` | Wait for that expected source snapshot to complete successfully, or return a specific failure. |
| `highlight PART_IDS... [--frame]` | Replace agent highlights, optionally framing them. IDs can also be group keys from the snapshot. |
| `highlight --clear` | Clear agent highlights. |
| `frame [PART_IDS...]` | Frame visible parts of the scene, or the given part/group references. |
| `frame --selection` | Frame visible descendants of the current human selection. |
| `view NAME` | Switch to a named project view and evaluate it. Standalone scenes expose the `scene` view. |
| `screenshot PATH [--replace]` | Save a `.png` image. The result contains its path; an existing destination requires `--replace`. |
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

`snapshot`, `selection`, `state`, `highlight`, `frame`, `view` and `screenshot`
accept `--expect-revision TOKEN`. It requires the currently displayed revision
to match and its consumed files to remain current on disk. A stale guard returns
`stale_revision` before acting. Agent highlights are separate from human
selection, visibility and exportability. The human can clear them with Escape;
reload retains highlights only for surviving authored IDs.

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

Unknown internal error categories use exit 1. No successful retained scene is
returned as acknowledgement of a failed current edit.

Windows and Linux transport regression tests cover persistent process launch,
simultaneous opens, Unicode paths, concurrency, timeouts and stale records.
Linux socket testing does not establish full Linux GUI or rendering validation.
