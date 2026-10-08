# Batch 7: build-plate review and scoped manufacturing evidence

SC13 and SC14 add a dedicated review of manually authored plate layouts, reusing
the shared source/instance graph and project-local printer context. They do not
automatically arrange geometry or run a slicer.

```text
synthcad open viewer/tests/agent-fixtures/plate-review --session plates
synthcad view plate-a -s plates
synthcad frame -s plates
synthcad checks -s plates --json
synthcad docs checks
synthcad docs build-plates
```

The **Checks** button opens the manufacturing review card. It shows printer
context, allowances, named parts and rotations, intended quantities and results
grouped by evidence scope. Result counts appear first; failed results precede
warnings within each scope. Affected-part buttons highlight and frame visible
parts without changing human selection or export flags. IDs belonging to another
view are identified rather than offering an ineffective highlight action.

Plate views draw the configured bed boundary, height and excluded regions as
viewer aids. Those lines do not enter the source solids, picker or STL export.
Explicit **Fit all** and CLI `frame` include the configured volume; opening a
panel does not move the camera. Assemblies retain their original transforms and
do not display the plate boundary.

## Evidence contract

`checks` and the snapshot's `manufacturing` field share the cached report from
the successful model load. Every check has a result, scope, affected IDs,
method, evidence, next actions and source/model/profile revision basis. The
overview lists generated results separately from authored records. Later edits
or load failure mark retained results not current; guarded CLI reads reject
stale revisions. Source files are rechecked after intersection calculations
before acknowledging the loaded revision.

Checks cover topology validity, empty and disconnected parts, transformed bed
bounds, maximum height and minimum-Z bed contact. Actual solid intersections
detect positive-volume overlaps. Conservative XY bounding boxes review excluded
regions, separation and explicit brim/support envelopes. Possible conflicts
from those approximations are warnings, not claims of exact surface collision.
Known provisional machine dimensions also prevent an unqualified passing fit.

The contact tolerance applies only to the bed-contact calculation; a large
authored tolerance cannot hide XY or height violations. Fixed numeric bounds
epsilon and intersection volume thresholds are reported separately. Unknown
allowances or unavailable profile inputs leave dependent checks not checked.

Quantity checks traverse the current shared graph's plate memberships without
evaluating another geometry copy. Aliases deduplicate within a plate; different
physical instance IDs remain intentional copies. Missing instances, repeated
assignment of the same instance across plates and source-quantity mismatches
are reported. Independent manifest entry modules are explicitly outside that
graph's quantity coverage. Safe-integer source quantities are compared without
instantiating additional geometry.

Checks use authored exportable-part roles, not temporary visibility or user
export-checkbox overrides. Reviewing a manufacturing report therefore does not
certify a particular later selective export. Unified export provenance remains
SC17. Pair and triangle budgets produce explicit unchecked results rather than
an all-clear result. Minimum features, anisotropy, support removal, toolpaths and
load capacity remain scoped uncomputed results with practical next actions.

## Verification

Windows MSVC Release and Ubuntu 24.04/GCC Release exercise 23 C++ suites. New
engine tests use actual Manifold solids, including a ring with a peg in its hole:
overlapping bounding boxes produce a clearance warning while the exact overlap
test correctly passes. Other cases cover rotation, floating/below-bed parts,
height, exclusion envelopes, invalid inputs, quantity aliases and duplicates,
very large declared quantities, contact/bounds tolerance separation and bounded
pair/triangle work. UI tests cover wrapping, result order, scrolling, currentness,
missing profiles, active/inactive IDs and non-mutating highlight actions.
Final review also added regressions for consistent stale-result readback through
all CLI projections, independent CSG/footprint computation budgets, and numeric
bed dimensions too large for float rendering. Such dimensions remain visible
as profile/check data, with an explicit rendering-range warning; they cannot
poison the camera or clipping bounds.

`scripts/test-manufacturing-review.py` copies the public shared-source fixture to
a Unicode temporary path and edits only that copy. Native acceptance verifies
the CLI and Checks card, bed view, warning actions, missing/provisional profiles,
quantity problems, guarded results, file-revision changes and failed-load
recovery. It verifies that panel interaction preserves camera/selection/export
state, while an explicit affected-part action frames only its targets.
Native workflows run on Windows at 125% and Linux at 100% and actual 200%
(2560 × 1440, UI scale 2.0). The final overflow and recovery case is additionally
exercised through the live CLI/viewer on both platforms after the edge fixes.

The existing session, shared-graph, geometric-selection, guided-pick and overview
harnesses remain regression gates, alongside Windows icon/reload smoke and the
fourteen-topic standalone stdout-guidance check. Public test fixtures contain
synthetic printer dimensions, not validated machine presets. Personal models,
screenshots and build artifacts remain excluded; all nine released V5 STL hashes
are checked unchanged.

This batch supplies geometry review and evidence limits, not a validated slice
or physical print. Bambu Studio interoperability, prepared 3MF, distribution/CI
and SC08's normal-desktop Windows clipboard check remain separate open work.
