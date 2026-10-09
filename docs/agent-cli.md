# SynthCAD domain CLI

Start with `synthcad-cli --help`. It prints an agent kickstart explaining the
application, human/agent collaboration, authoring, revision verification, review,
and export. Guidance is compiled into the executable and works offline without a
checkout, installed skill, project registry, or running viewer.

## Discover the next task

```text
synthcad-cli
synthcad-cli project --help
synthcad-cli model assembly --help
synthcad-cli review pick --help
synthcad-cli print export --help --json
synthcad-cli --version --json
```

Bare groups and guidance topics show their instructions. Action commands execute;
append `--help` to read their workflow without supplying required arguments.
There is no separate docs, skill, help, or capabilities command. Help teaches when
to use an operation, its context, steps, examples, verification, recovery, and
related instructions. Deeper topics carry detailed contracts instead of making
the root dump all instructions at once.

| Domain | Actions | Additional instruction topics |
| --- | --- | --- |
| `project` | `open PATH`, `list`, `inspect`, `reload`, `cancel-load`, `revision`, `wait`, `events` | `files` |
| `model` | Author geometry through source edits | `assembly`, `api` |
| `review` | `view NAME`, `selection [REFERENCE]`, `highlight`, `frame`, `screenshot PATH`, `pick request`, `pick status ID`, `pick cancel ID` | Review and pick workflow guidance |
| `print` | `profile [--template]`, `checks`, `export PATH`, `history` | `design`, `fit`, `plates`, `handoff`, `feedback` |

These are 20 executable leaves. `project inspect` combines load state, semantic
snapshot, and project overview. `review selection` reads the human selection;
supplying a copied reference resolves it without changing selection. Focused
profile, check, selection, and export-history reads avoid loading a full snapshot.
Geometry editing, automatic packing, slicing, and slicer preset verification are
not implemented CLI operations. Read `model api --help` before authoring geometry.

## Open, author, verify, and review

Use a standalone `.js` file, a project directory containing `synthcad.json`, or
the manifest itself. Read `project files --help` for its contract. For a new simple
design, keep shared geometry in `design.js`; use named assembly and plate views.
Put requested final artifacts in `exports/`, and personal repository work in
ignored `local-scenes/`. Read routine responses in memory and use temporary paths
for review screenshots.

```powershell
$opened = synthcad-cli project open './my-design' --name bracket --json | ConvertFrom-Json
if (-not $opened.ok) { throw $opened.error.message }
synthcad-cli project inspect --project bracket --json
# Edit sources with your normal editor. Initial dependency discovery may be busy.
do {
    $desired = synthcad-cli project revision --project bracket --json | ConvertFrom-Json
    if (-not $desired.ok -and $desired.error.code -eq 'busy') {
        Start-Sleep -Milliseconds 100
    } else { break }
} while ($true)
if (-not $desired.ok) { throw $desired.error.message }
$loaded = synthcad-cli project wait --project bracket --revision $desired.data.revision --json | ConvertFrom-Json
if (-not $loaded.ok) { throw $loaded.error.message }
synthcad-cli review highlight base lid --project bracket --frame --expect-revision $loaded.revision --json
```

Open returns a `project` handle and whether the viewer was reused. Use `--name`
only to name an opening project; `--project` targets later calls. `project list`
lists open projects, not saved projects on disk. With one open project targeting
is automatic; with several, supply its handle. The viewer persists after the CLI
exits; close its window to end it. Reopening the same canonical project reuses the
viewer. Conflicting names are rejected, including simultaneous opens.

The revision command captures the active view and known dependency bytes. Pass
its requested token to wait, then use the wait result's **displayed** revision
for `--expect-revision`. New imports can make these identifiers differ. Do not
infer reload success from a plausible image or old geometry retained after a
failed edit. Inspect `status`, `loadFailure`, `attemptedRevision`,
`displayedRevision`, and `exportValid`. Fix failed source and capture/wait again;
use reload to retry evaluation and cancel-load for stuck evaluation. A CLI
timeout does not cancel the model worker. The evaluation limit defaults to
120000 ms and can be adjusted with `--evaluation-timeout` on open or reload.

Read `review --help` for visual collaboration and `review pick --help` for human
selection requests. Pick receipts are asynchronous; read status or events and
wait for explicit confirmation. Read `review selection --help` for reference
format, revision-local identity, geometry coordinates, and stale references.

## Prepare and export

Read `print --help`, then the focused instructions needed for orientation, fit,
profiles, authored plates, checks, slicer handoff, and optional physical feedback.
Metadata and cached evidence belong to the displayed revision. Profile templates
are incomplete JSON fragments to review and merge through source-file edits.

```text
synthcad-cli print profile --template
synthcad-cli print checks --project bracket --json
synthcad-cli print export ./exports/plate.3mf --project bracket --dry-run --json
synthcad-cli print export ./exports/plate.3mf --project bracket --expect-revision DISPLAYED_TOKEN --json
synthcad-cli print history --project bracket --json
```

Export preserves the selected view's current placement and all exportable parts
unless `--visible-only` is supplied. The destination must have a case-insensitive
`.3mf` or `.stl` extension; explicit `--format` must match it. `--dry-run` performs
viewer-owned validation without writing. Review warnings before acknowledging
with `--allow-warnings`; `--replace` explicitly permits overwriting. Neither
option turns missing, heuristic, sliced, or physical evidence into verification.
A timeout near publication is not proof no file was saved: inspect the destination
and `print history` before retrying. History reads cached receipts, not files.

## Options and response contract

Global options work before, between, or after command words:

- `--project NAME`: target an open project; use `--name` when opening instead.
- `--json`: emit one JSON response on stdout; logs belong on stderr.
- `--timeout MS`: request timeout, 0..300000 ms, default 10000.
- `--help` or `-h`: read instructions without contacting a viewer.
- `--`: treat subsequent action arguments literally, including dash-leading IDs or paths.

`--version` accepts only `--json`. Operation-specific options and bounds appear
in that operation's help. `--expect-revision` requires matching displayed geometry
whose consumed files remain current on disk. It is unavailable for captured-token
waits, events, pick outcomes, and profile templates.

CLI protocol version 2 uses this envelope; the private viewer transport remains
version 1:

```json
{
  "protocolVersion": 2,
  "ok": true,
  "command": "project wait",
  "project": "bracket",
  "revision": "DISPLAYED_REVISION",
  "data": {
    "status": "ready",
    "requestedRevision": "REQUESTED_TOKEN",
    "displayedRevision": "DISPLAYED_REVISION"
  }
}
```

Failures contain `error: {code, message, details?}` instead of data. Project and
revision are optional when unavailable. Action payloads retain their original
meaning; project inspection includes `data.overview`. `print profile --template`
prints standalone JSON by default; `--json` wraps that fragment in the envelope.

Structured help includes path, kind (group/topic/action), summary, usage, immediate
children, arguments, options, requirements, examples with argv arrays, guidance,
and nextSteps. Root help also reports implemented capabilities. Traverse children
to discover narrower instructions. Each page includes a guidance hash, source
metadata, and content-derived bundle version/hash; no guide is read from disk at
runtime. Action examples and help links are checked in the test suite.

| Exit | Error | Recovery |
| --- | --- | --- |
| 0 | Success | Read the result before continuing. |
| 2 | `invalid_argument` | Read the operation's help; correct input or name conflicts. |
| 3 | `no_project` | Open a project or select one from project list. |
| 4 | `ambiguous_project` | Supply --project NAME. |
| 5 | `load_failed` | Inspect diagnostics, fix sources, capture and wait again. |
| 6 | `timeout` | Inspect current state before retrying; evaluation may continue. |
| 7 | `stale_revision` | Capture and wait; refresh references and guards. |
| 8 | `superseded` | Files or view changed; capture a new token. |
| 9 | `cancelled` | Inspect the operation's retained result or reopen the project. |
| 10 | `io_error` | Check destination and execution permissions. |
| 11 | `busy` | Inspect load/project state and retry when ready. |
| 12 | `not_found` | Refresh the missing part, view, reference, or request ID. |
| 13 | `stale_cursor` | Resynchronize state and restart retained events from 0. |
| 14 | `warnings_present` | Review warnings before --allow-warnings. |
| 15 | `destination_exists` | Choose another path or explicitly --replace. |
| 16 | `empty_export` | Review the exportable/visible part set. |

## Local access and migration

Windows uses user-restricted named pipes; Linux uses user-owned Unix sockets.
No cloud account or network service is required. The standalone CLI launches
itself as viewer; development `synthcad` launches its sibling `dingcad_viewer`.
`SYNTHCAD_VIEWER` may override that executable. These executable names and existing
viewer developer flags remain supported. Recognizable positional project paths
open the project; unknown command words return an error rather than launching it.

Keep the same registry after timeouts or access errors and retry with required
execution permissions. `SYNTHCAD_SESSION_DIR` remains an internal/testing setting;
do not change it or create a project-local `.sessions/` as an error workaround.

This is a breaking CLI change. Flat commands, docs/help/capabilities commands,
`--session`, and `-s` are removed with replacement diagnostics. Public identities
and errors use project terminology. Internal transport tests still use sessions
and protocol version 1. Repository scripts and maintained instructions use the
new public surface; historical validation records describe their original builds.
