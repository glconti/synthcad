# Build-plate layouts

Run `synthcad docs build-plates` to print this guide. Plate layouts are authored
views of a shared design graph. The project profile supplies explicit machine
metadata; `synthcad checks --json` reports the current manufacturing review.
Layouts remain deliberate source edits; automatic packing is not provided.

## Author a separate print view

Keep the assembly in assembly coordinates. Add one named view per intended plate, reusing the same source geometry and parameters while applying deliberate print transforms only in the plate view. Keep all intended pieces visible in that view and give them stable IDs and recognizable names. Put boundary or clearance visuals in a clearly identified, non-exportable group when useful; they are review aids, not automatically checked bed geometry.

Keep each part's geometry definition in a shared JavaScript module and import it
from assembly, inspection and plate entries. Do not paste a second copy of the
modeling code or use an exported mesh as an editable substitute. A deliberate
second physical copy is another placement of the same source part. Source
definitions, placed instances and grouping have different meanings: putting
one instance in a second review group should not create another printable copy.
For new multi-view projects, export a shared `design` graph: define source parts
once, give each physical copy a distinct instance ID, and reference those IDs
from groups and view members. A plate's `placements` override assembly poses
without changing the source or other views. Run `synthcad docs design` for the
schema. Manufacturing quantity review compares physical instances and declared
source quantities across plate views. Group aliases remain a single copy;
assigning one instance to multiple plates is a review concern.

Use the user's known usable build area and bed exclusions. If these are unknown, leave plate fit provisional and proceed with model review; do not infer a printer from another project. Place parts with room for the slicer choices that are actually known, and state when support or brim allowances have not been included.

## Review placement limits

Inspect each transformed part and the combined layout in the viewer. Run
`synthcad checks --session NAME --json` for bounds, bed contact, overlap,
allowance and quantity review against the current placed solids and supplied
profile. Read each check's method, scope and evidence: deterministic geometry
calculations and conservative warnings support different conclusions. Unknown
or provisional inputs leave dependent results unresolved.

Set per-view `plateSettings` for known `partGap`, `brim` and `support`
allowances; omitted values remain unknown. Explicit zero is an authored choice,
not a default prediction about slicing. `contactTolerance` is a numerical
comparison tolerance, not a fit allowance. Run `synthcad docs profiles` for
project metadata and see [manufacturing checks](../manufacturing-checks.md)
for the report contract. Verify actual support/brim geometry and accessibility
in the target slicer before describing a layout as ready to print.

The GUI's Export STL preserves the selected model solids' current coordinates. Exporting an assembly view therefore preserves assembly placement; exporting a manually authored plate view preserves its plate placement. A plate scene is the authored geometry itself, not proof that parts fit or slice correctly. Run `synthcad docs bambu-handoff` for the available export path.
