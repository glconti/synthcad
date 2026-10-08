# Optional samples and physical feedback

Run `synthcad docs physical-feedback` to read this guide. Samples are optional:
users may proceed directly from design and plate review to a full export.
Do not require a sample, printer profile or physical report just to export.

## Offer a targeted test when it answers a useful question

Explain what a small sample would establish: a particular sliding fit, liner
clearance, glue alignment or support-removal detail. Explain what it would not
establish, such as the strength of a full assembly. Let the user choose whether
to print it. Avoid a compulsory sample step or unnecessary test pieces.

Derive the sample from the full part's shared parameters or source geometry;
do not copy dimensions into an unrelated model. Give it a named view and stable
part/instance IDs. Reference reusable full-part instances where appropriate.
Use a plate view with explicit orientation, bed placement and allowances when
the user intends to print it. The same layer direction, material and process
can matter for transferring a result to the full part. Use `print-design`,
`fit-and-assembly` and `build-plates` guidance for those decisions.

## Preserve the user's report and its scope

Agents edit the optional `evidence` array in `synthcad.json`; `synthcad overview`
and the Project card present it. Capture the basis from the loaded sample
view's `modelRevision` in the overview, not a source or displayed revision.
Set `profileRevision:null` when the note is independent of that context. If a
test depends on a known profile, record its revision and actual test conditions.

```json
{
  "evidence": [{
    "id": "socket-fit-1",
    "text": "User-reported fit check of the socket coupon",
    "stage": "tested",
    "sampleView": "fit-sample",
    "sourcePartIds": ["socket", "pin"],
    "partIds": ["coupon-1", "pin-1"],
    "basis": {"view": "fit-sample", "modelRevision": "COPIED_MODEL_TOKEN", "profileRevision": null},
    "observation": {
      "kind": "fit",
      "result": "failed",
      "reportedBy": "User",
      "details": "The user reports the pin cannot seat by hand.",
      "conditions": "Record supplied orientation, material, machine, process and measurements; mark unknowns."
    }
  }]
}
```

This is a schema example, not a report of a test that occurred. Never copy its
result into a real project without the user's report. Start a planned sample
as `proposed` without an observation. Set `printed` only after the user says it
was printed; set `tested` with an observation only after a reported test. Use
`superseded` only for an explicit replacement decision, retaining the old report.
An export receipt, a render or a passing geometry check never advances stages.
Stale revision freshness and physical stage answer different questions: an old
tested sample remains tested even when its geometry basis becomes stale.

Observation kinds are `fit`, `load` or `other`; results are `passed`, `failed`
or `inconclusive`. Observation objects require `reportedBy` and `details`, and
may include `conditions`, `recordedAt` and custom `extensions`. They belong to
`tested` or `superseded` evidence. Preserve the user's wording and uncertainty.
For a load observation, record load placement, amount, duration and conditions
if known; do not generalize one test into a safe load rating or certification.
Attachment paths are references only; the overview does not open them.

## Revise without losing earlier work

Preserve earlier sources when compatibility changes, retain existing exports,
and use a new filename for a revised output. Compare the affected mating parts
and identify which existing prints can still be used. Do not infer reuse merely
because an ID or name stayed the same. If compatibility is uncertain, say so.

Add an authored `compatibilityChanges` record describing that decision:

```json
{
  "compatibilityChanges": [{
    "id": "socket-clearance-v2",
    "text": "Enlarge the socket; the unchanged pin can be reused for this revision.",
    "status": "requires-reprint",
    "basis": {"view": "plate", "modelRevision": "NEW_MODEL_TOKEN", "profileRevision": null},
    "previousBasis": {"view": "plate", "modelRevision": "OLD_MODEL_TOKEN", "profileRevision": null},
    "partIds": ["socket-1", "pin-1"],
    "reprintPartIds": ["socket-1"],
    "evidenceIds": ["socket-fit-1"]
  }]
}
```

The status is `compatible`, `requires-reprint` or `unknown`. Both revision bases,
nonempty affected `partIds`, and `reprintPartIds` are required. Reprint IDs must
be a subset of affected IDs; `requires-reprint` needs at least one, while
`compatible` needs an empty list. `unknown` is a decision still to resolve, not
permission to assume that unlisted parts can be reused. Missing evidence links
are shown as warnings. These are authored engineering decisions, not automatic
compatibility certification. Run `synthcad docs overview` for exact limits and
validation rules.

Reload, review the changed full part and sample, and run `synthcad checks`.
Use `synthcad export PATH --dry-run` before creating a new artifact and inspect
`synthcad export-history` for older receipts. New digital evidence does not erase
the earlier report or prove that the revised design was physically retested.
