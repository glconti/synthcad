# Project and source revision contract

Contract version 1. The C++ contract is implemented in
[`viewer/project_contract.h`](../viewer/project_contract.h). This document covers
project files and source identity; CLI transport and response envelopes have
their own protocol contract.

## Project files

Open a standalone `.js` entry, a directory containing `synthcad.json`, or that
manifest directly. A standalone scene needs no migration and exposes one named
view, `scene`. A minimal project is:

```json
{
  "schemaVersion": 1,
  "name": "My design",
  "defaultView": "assembly",
  "views": {
    "assembly": "assembly.js",
    "inspection": "inspection.js",
    "plate-1": "plates/plate-1.js"
  }
}
```

`schemaVersion` must be integer `1`. `name` is optional and must be a string if
present; the containing directory name supplies its default. `views` must be a
nonempty object mapping nonempty names to relative `.js` entry filenames.
`defaultView` must name one of those views. Paths resolve against the manifest
directory; relative parent segments are allowed for shared sources. An entry
can be absent when metadata is opened; evaluation reports its missing-file
failure. Authored Unicode names and paths are preserved.

Several view names may map to the same entry file when that file exports a
shared `design` graph. The active manifest name selects the matching graph view;
missing graph views fail rather than silently showing the default. Standalone
files keep their `scene` alias and select the graph's `defaultView`. Run
`synthcad-cli model assembly --help` for source parts, instances and reference-based layouts.

Unknown root fields are permitted for future metadata. `profiles` and
`activeProfile` now supply project-local printer context (`synthcad-cli print profile --help`).
`measurements`, `assumptions`, `checks`, `evidence` and `exports` supply the
overview (`synthcad-cli project inspect --help`). Their validation errors are reported
separately and do not reject otherwise valid geometry. Malformed JSON,
unsupported schema versions and invalid required fields produce actionable
errors including the manifest path. Loading never rewrites project files.

Project identity is SHA-256 of `synthcad-project-v1:` followed by the canonical
UTF-8 absolute manifest or standalone entry path. Directory and direct-manifest
opens identify the same project. Canonicalization resolves existing symbolic
links and normalizes relative segments; Windows paths use invariant lowercase
and forward slashes, while Linux paths retain case. Moving a project changes
its identity. This identifies a local project location, not globally portable
project content. Standalone entries and manifests have distinct identities.
Project and view paths retain authored filename case for presentation; only
identity and dependency-map keys use the case-normalized representation.

## Source revisions

A `FileSnapshot` is a sorted map from canonical UTF-8 absolute source paths to
lowercase SHA-256 digests of the exact bytes read. It stores digests rather than
source text. Binary mode preserves newline and encoding bytes. Track the
manifest, selected entry and every dependency actually requested by evaluation,
including missing dependencies. Imports determine the graph; parsing source
text for import-like strings is not a substitute for the evaluator's reads.

`ReadTrackedFile` returns the bytes and records their digest in the same call.
Evaluation must consume those returned bytes. A separate pre-evaluation disk
scan cannot establish what evaluation actually consumed. Failed reads record
`missing` or `unreadable`; repeated reads of a path with different contents in
one attempt record `changed-during-read`, which cannot match a disk snapshot.

`loadMesh` also tracks its primary mesh file, including missing files, and
compares reads before and after the third-party import. This detects ordinary
mesh edits and triggers reload/recovery without editing JavaScript. Unlike JS
evaluation, the importer opens the file independently: this is not an atomic
proof of every byte it consumed, and auxiliary files resolved internally by
an importer are not tracked. Prefer self-contained mesh assets for reproducible
exports; do not treat a recorded primary digest as provenance for external
materials or other auxiliary assets.

The revision is SHA-256 of a deterministic serialization:

1. Begin with ASCII `synthcad-revision-v1:`.
2. Visit map entries in canonical path byte order.
3. Append each path and digest/sentinel as separate fields: decimal UTF-8 byte
   length, ASCII `:`, then the field bytes.

Length prefixes prevent delimiter collisions. Graph paths and membership affect
identity, as do every file's bytes. Timestamps are never evidence of revision
equality. Identical timestamp/size edits are detected; restoring exact original
bytes restores the same revision for the same dependency graph.

`MatchesDisk` rereads every tracked path and compares digests/sentinels. Check it
after evaluation before publishing success; if any source changed while loading,
the attempt cannot acknowledge the current requested revision. This is a
best-effort local filesystem comparison, not a transaction locking source files;
a subsequent edit requires another reload.

An unsuccessful import must not lose the last known import graph. Watch the
union of last successful dependencies and dependencies requested by the failed
attempt, including missing paths. `RecoverDependencies` recaptures that union
for recovery watching. Its result is a watch baseline, never evidence that the
failed attempt successfully evaluated those bytes. A successful complete
evaluation can replace the dependency graph with the files it actually read.

## Reload state interpretation

Keep requested, attempted and displayed revisions separate. A requested revision
describes the expected disk snapshot. An attempted revision describes actual
bytes consumed by the attempt. A displayed revision describes the last valid
geometry and active layout successfully published. If a new attempt fails, retain the previous
displayed geometry and revision while exposing the new attempted revision and
diagnostics; retained geometry must not acknowledge success for the broken edit.

The revision command captures the files known to the active session at that
instant. Its opaque requested token identifies that expected snapshot and active
view: SHA-256 of the UTF-8 view name, ASCII `:`, and the source snapshot revision.
It differs from the displayed revision even when the graph is unchanged.
A bounded wait succeeds only while those expected files still match disk, the
active view is unchanged, and a valid completed evaluation's entire consumed
graph matches disk. If an edit introduces previously unknown imports, the
completed graph can contain additional files. The requested token and displayed
display identity have distinct meanings and both must be reported. The requested
token does not certify dependencies that were unknown when it was captured.

`sourceRevision` in a snapshot identifies the exact consumed source graph of
the last valid load. `displayedRevision` also includes the active view and the
resolved design layout, so two views of the same JavaScript entry cannot share
a stale-action guard. Its version-1 identity is SHA-256 of
`synthcad-display-v1:`, the decimal UTF-8 byte length of the active view, `:`,
the active view, the 64-character source revision, and the design metadata
identity (empty for legacy scenes). Treat returned revision tokens as opaque.
`displayedView` names the last successful view separately from `view`, which
can name an unsuccessful requested view. Snapshot `design` contains the last
valid normalized reference graph, or null for legacy scenes. This provenance
tuple permits consumers to reject outdated check/export results; persisted
check and export records use the separate artifact basis described below.

## Artifact model revisions

`overview.modelRevision` identifies the displayed model for authored checks,
exports and optional physical evidence. It hashes the evaluator-consumed model
files, view name and normalized design/layout identity. The manifest is excluded
when used only to route views and hold metadata. If model code itself reads the
manifest as an input, its consumed bytes remain model dependencies.

The version-1 token is SHA-256 of `synthcad-model-v1:` followed by three
length-prefixed fields (decimal UTF-8 byte count, `:`, value): view name,
`Revision(modelFiles)`, and design metadata identity (empty for legacy scenes).
Keep the returned token opaque. This separate identity prevents recording a
check in the manifest from making that same check immediately stale. Existing
reload and action-guard tokens continue to include manifest bytes.

The viewer remembers successfully loaded views for the session; it does not
evaluate other layouts behind the scenes. `overview.views` includes each entry's
last loaded `modelRevision` and `sourceCurrent` status. Unvisited views have no
verified current model revision. Referenced source edits or entry-path changes
make prior records stale; unvisited views remain unknown until loaded. Snapshot
refresh uses the existing source polling interval. A removed active view falls
back to the new manifest's default view.

`profileRevision` hashes the normalized selected printer profile and its ID.
Changing nozzle, material, dimensions or provisional markers changes it without
changing model identity. Records explicitly bind a profile revision or use null
for profile-independent evidence. Authored results remain authored; matching
revisions do not prove printability or physical strength.

Pending work, evaluation failure, timeout, cancellation and a superseding edit
are distinct outcomes. A changed expected file or active view supersedes the
request. Retained geometry from an earlier edit cannot succeed because its
consumed snapshot no longer matches disk. Command error categories and exact
wait response fields are defined by the protocol contract, not by project
metadata.
