# Authored project overview

Open **Project** beside the scene filename in the parts card to review the
current project's named views, measurements, assumptions, printer context,
checks, exports and optional physical notes. Select a view to load it in the
same viewer. Scroll the overview independently of the camera; Close returns to
the parts tree. Ctrl+F returns to part search.

Use `synthcad-cli overview --json` for the same context, `synthcad-cli profile --json`
for printer settings and missing values, and `synthcad-cli profile --template` for
an incomplete setup fragment. Run `synthcad-cli docs profiles` for the short setup
conversation. Agents author metadata in `synthcad.json` with ordinary file edits.
No project fields are written by these commands.

The overview projects optional manifest metadata into a read-only view.
It does not open export or attachment paths,
inspect geometry, infer print outcomes, or change any authored record.
Standalone scenes pass an empty metadata object and receive empty sections.

To bind a record, copy `modelRevision` from the loaded view in the overview and
`profile.profileRevision` when relevant. For example, add this to the existing
manifest, replacing the token with the CLI result:

```json
{
  "checks": [{
    "id": "clearance-review", "name": "Assembly clearance review",
    "result": "not-checked", "scope": "geometry",
    "details": "Clearance has not been measured yet.",
    "basis": {"view": "assembly", "modelRevision": "TOKEN_FROM_OVERVIEW", "profileRevision": null}
  }],
  "evidence": [{"id": "fit-sample", "text": "Optional fit sample proposed", "stage": "proposed"}]
}
```

`modelRevision` excludes metadata-only manifest changes, so adding a record
does not invalidate its own basis. Entries not loaded this session have unknown
model identity. Source edits and changed profile revisions mark older records
stale without deleting them. Invalid optional records leave valid siblings and
geometry available. A malformed required manifest retains the last valid model
and metadata with `metadataCurrent:false` and disables export until recovery.

The manifest may contain six optional arrays, each limited to 1000 records:

| Section | Required fields | Optional fields |
| --- | --- | --- |
| `measurements` | `id`, `name`, finite numeric `value`, `unit`, `status` (`measured`, `provisional`) | `notes`, `partIds`, `extensions` |
| `assumptions` | `id`, `text`, `status` (`provisional`, `confirmed`) | `partIds`, `extensions` |
| `checks` | `id`, `name`, `result` (`passed`, `warning`, `failed`, `not-checked`), `scope` (`geometry`, `heuristic`, `sliced`, `physical`) | `details`, `basis`, `partIds`, `extensions` |
| `exports` | `id`, `path`, `format` (`stl`, `3mf`) | `createdAt`, `basis`, `partIds`, `extensions` |
| `evidence` | `id`, `text`, `stage` (`proposed`, `printed`, `tested`, `superseded`) | `basis`, `partIds`, `attachments`, `sampleView`, `sourcePartIds`, `observation`, `extensions` |
| `compatibilityChanges` | `id`, `text`, `status` (`compatible`, `requires-reprint`, `unknown`), `basis`, `previousBasis`, nonempty `partIds`, `reprintPartIds` | `evidenceIds`, `extensions` |

IDs are unique within their section. All entries sharing a duplicate ID are
rejected, including the first. Other valid entries survive independently.
Schema string fields outside arbitrary `extensions` data are NUL-free UTF-8
with a maximum of 10000 Unicode scalar values; IDs,
names, text, units, paths, revision strings and array elements are nonempty.
Optional `notes`, check `details`, and observation `conditions` may be empty.
ID arrays and `attachments` contain at most 1000 nonempty strings. Custom fields belong inside an object named
`extensions`; other unknown record or basis fields are errors. Manifest fields
outside these six sections belong to the enclosing project contract and are
not validated by this projection.

A `basis` object requires `view`, `modelRevision` and `profileRevision`.
Revision strings are opaque and compared exactly. Explicit
`profileRevision: null` means the record is independent of the printer profile.
Omitting `profileRevision` from a supplied basis is an error. Basis objects may
also have an `extensions` object.

Checks, exports and evidence receive derived `freshness` and `reason` fields:

| Freshness | Meaning |
| --- | --- |
| `unbound` | No basis was authored. |
| `unknown` | The view is unknown/ambiguous, no model has been loaded in this session, source state is unavailable, or a required complete printer profile is unavailable. |
| `stale` | The source changed, the loaded model revision differs, or the required active profile revision differs. |
| `current` | The loaded model revision and current source state match, and any required profile is complete and matches. |

The caller supplies `views` as an array with `id`, `loaded`, `modelRevision`,
and `sourceCurrent` for each known view (other display fields are allowed).
Unvisited views have `loaded: false` and `modelRevision: null`. A known profile
revision mismatch makes a dependent record stale even before its view is loaded.
Otherwise an unvisited or ambiguous view has unknown freshness. A known source
change or model mismatch also takes precedence over incomplete profile
information. The profile context
provides `status` (`complete`, `incomplete`, `invalid`) and `profileRevision`.

Every accepted record receives `origin: "authored"`. An authored `passed`
physical check remains an authored claim. A proposed item remains proposed;
revision freshness does not promote evidence stages or certify results.
Existing records are retained with stale/unknown freshness so their history
remains visible.

Sample evidence can identify a small test view using `sampleView` and the shared
sources represented by the sample using `sourcePartIds`. A uniquely registered
sample view receives `sampleViewStatus: "available"` even before it has been
loaded, so it can be opened. Missing or ambiguous sample IDs receive `"unknown"`
with `sampleViewReason`; the authored record remains visible. Availability is
separate from revision freshness and physical verification.

An optional `observation` is allowed only when the author explicitly sets
`stage` to `tested` or `superseded`. It requires `kind` (`fit`, `load`, `other`),
`result` (`passed`, `failed`, `inconclusive`), nonempty `reportedBy` and nonempty
`details`. Optional `conditions` is a string, `recordedAt` is a nonempty string,
and `extensions` is an object. Dates are retained as authored text. A proposed
or printed item with an observation is rejected rather than silently promoted.
For example:

```json
{
  "evidence": [{
    "id": "clip-fit-r1", "text": "Clip sample fit trial", "stage": "tested",
    "sampleView": "fit-sample", "sourcePartIds": ["clip"],
    "observation": {
      "kind": "fit", "result": "inconclusive", "reportedBy": "Project owner",
      "details": "Engages by hand; retention under load has not been tested.",
      "conditions": "One PETG sample", "recordedAt": "2026-10-08"
    }
  }],
  "compatibilityChanges": [{
    "id": "clip-interface-r2", "text": "The wider clip needs a matching panel opening.",
    "status": "requires-reprint", "partIds": ["clip", "panel"],
    "reprintPartIds": ["panel"], "evidenceIds": ["clip-fit-r1"],
    "basis": {"view": "assembly", "modelRevision": "TARGET_REVISION", "profileRevision": null},
    "previousBasis": {"view": "assembly", "modelRevision": "PREVIOUS_REVISION", "profileRevision": null}
  }]
}
```

Compatibility records are authored interface/reprint claims. Both `basis` and
`previousBasis` use the same strict basis schema. Freshness compares only the
target `basis`; `previousBasis` is retained verbatim as the older context.
`reprintPartIds` is required, must be a subset of `partIds`, and must be nonempty
for `requires-reprint` or empty for `compatible`. An `unknown` claim may retain
an authored possible reprint list. These IDs do not create geometry copies or
prove a computed compatibility result.

Optional `evidenceIds` resolve only against accepted evidence records.
`evidenceStatus` is `available` when all IDs resolve, including an empty or
omitted list, and `unknown` when any are missing or were rejected. The
`evidenceReason` distinguishes no authored links from resolved links. Unresolved
links remain in the record and produce root `warnings` with `path`, `message`,
`recordId`, and `missingEvidenceIds`; they do not become structural errors or
delete the record. Resolving a link does not make stale evidence current or
verify its claims. Exports never create observations, compatibility changes,
reprint implications, or superseded stages automatically.

The response contains the six validated section arrays, `errors` entries with
`path` and `message`, explanatory `revisionSources`, and `status`: `valid` when
there are no errors, `partial` when valid records coexist with errors, or
`invalid` when errors leave no accepted records. Invalid metadata sections do
not fail geometry evaluation. Model revisions must come from evaluated model
sources independently of these authored overview sections, preventing a check
record update from invalidating its own model basis.
