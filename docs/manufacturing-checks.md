# Manufacturing review

`synthcad checks --session NAME --json` reads the viewer's manufacturing report
for its displayed view. The viewer evaluates checks against the same placed
solids used for display and export. The report does not introduce a second
editable geometry model, arrange parts automatically, or validate a slicer.

Use an agent to edit the shared JavaScript `design` graph. Define each source
part once, give every intended physical copy its own instance ID, and reference
those instances from groups and views. An instance appearing in two tree groups
is one physical object. Two distinct instances of the same source are two
physical copies. A `kind: 'plate'` view applies its own `placements`; its complete
pose replaces the assembly pose without changing other views.

## Profile and plate settings

The active project profile supplies build volume, rectangular bed exclusions,
nozzle and material metadata. The bed origin is `[0, 0]`, with positive X and Y
across the usable bed and positive Z above it. Record the source and certainty
of the supplied machine values. An unknown or provisional profile leaves
dependent checks unresolved; it is not permission to substitute another
project's machine or invent a default material.

Optional manifest settings are keyed by view ID:

```json
{
  "plateSettings": {
    "plate-a": {
      "partGap": 2,
      "brim": 0,
      "support": 0,
      "contactTolerance": 0.0001
    }
  }
}
```

These example numbers are test data, not recommended process settings. Values
must be finite and nonnegative. `partGap`, `brim` and `support` describe authored
allowances in millimetres, not generated slicer geometry. Explicit zero means
the author selected no allowance; omission means unknown. Dependent clearance
checks remain `not-checked` when their inputs are unknown. The default
`contactTolerance` of `0.0001` mm is a numerical comparison tolerance for bed
contact. It does not specify mechanical fit or compensate for a printer.

## Read results by their evidence

The report contains `schemaVersion`, `view`, `kind`, revision `basis`,
`profileStatus`, `settings`, `bed`, placed `parts`, `checks`, and `quantities`.
Each part includes its stable instance ID, source part ID, rotation,
translation and transformed bounds. Each check includes a result, scope,
affected part IDs, method, structured evidence and practical next actions.

| Check | Method and limitation |
| --- | --- |
| Solid validity/emptiness/connectivity | Manifold topology, finite bounds and volume, emptiness, and connected components. Disconnected bodies can be intentional and require review. |
| Bed bounds/height/contact | Transformed bounds against the supplied rectangular volume and numeric bed plane. Contact does not establish stable contact area or adhesion. |
| Exclusions | Conservative XY bounds against supplied excluded rectangles; a possible conflict is a heuristic warning. |
| Part overlap | Actual solid intersection after disjoint-bounds rejection; positive volume is a plate failure or an assembly warning requiring interpretation. |
| Part/brim/support clearance | Conservative XY bounds gap compared with `partGap + 2*(brim + support)`; actual slicer geometry is not generated. |
| Brim/support envelope | Expand each XY bounds footprint by `brim + support` per side and compare with bed edges and exclusions; possible conflicts are heuristic warnings. |
| Plate quantities | Expand plate membership, deduplicate aliases within each plate and compare physical instance placements with source quantity intent. |

Pair and triangle budgets bound the overlap work. Unchecked pairs are reported
in evidence and must not be interpreted as a clean overlap result. Read the
reported numeric thresholds as engine comparison limits, not print tolerances.
Bed XY bounds, maximum height and envelope comparisons use a fixed `0.0001` mm
numerical epsilon reported in evidence. Changing `contactTolerance` changes
only the minimum-Z bed-contact comparison.

`passed`, `warning`, `failed` and `not-checked` describe that check's method and
inputs. A passing bounds test establishes only the reported geometric
condition. Conservative bounds or clearance methods can report a warning for
shapes whose actual curved or concave surfaces need closer inspection. Review
the evidence and highlight the affected IDs before changing the model.

Results distinguish four scopes:

| Scope | Evidence it can establish |
| --- | --- |
| `geometry` | A deterministic calculation against the current model, poses and specified metadata. |
| `heuristic` | A conservative or approximate indicator requiring interpretation. |
| `sliced` | Evidence from an actual slicer/profile, when separately provided. |
| `physical` | Measurements or tests on actual parts, when separately provided. |

The engine does not promote authored checks or proposed evidence into sliced
or physical verification. Print time, material use, support generation, thin
feature survival, strength and fit require the corresponding process evidence.

Quantity review spans authored plate views in the currently loaded shared
design graph and uses stable physical instance identity. It does not evaluate
separate entry files to construct a project-wide inventory. Check that intended
copies appear on the intended plates, that an
instance has not accidentally been assigned to several plates, and that a
source's declared quantity agrees with its physical copies. Group aliases do
not increase quantities. A deliberate additional copy needs a distinct
instance, rather than another group reference to an existing instance.
The separate `quantity-scope` check reports unresolved coverage for independent
manifest entries or for a legacy scene without a shared graph.

## Act on a warning

Move or rotate the affected instance in its plate view, split work across
plates, or correct the supplied metadata. Recheck after the normal source
reload. Preserve the assembly pose and source part identity. Routine reversible
model and layout edits can proceed within the user's request; explain changes
that alter the intended function, appearance or physical part count.

Use `--expect-revision TOKEN` for a check request tied to a reviewed displayed
revision. The report's basis identifies model, source and profile revisions.
Source edits, view switches and profile changes require a fresh report. A
failed reload retains the previous visible geometry and disables export; a
retained image is not evidence that the new source passed. Checks do not change
selection, visibility, export flags or the camera.

After the geometric review, inspect the target slicer's preview for actual
brims, supports, toolpaths, inaccessible support material and process-specific
minimum features. If fit or loading remains uncertain, offer a targeted coupon
or first article and record its machine, material, orientation and measured
outcome. Physical testing is optional evidence for ordinary modeling/export;
do not call an untested joint or load path verified.
