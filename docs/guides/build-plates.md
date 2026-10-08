# Build-plate layouts

Run `synthcad docs build-plates` to print this guide. Plate layouts are authored
views of a shared design graph. A known `buildVolume` is enough to check model
bounds against a rectangular bed; printer identity, a catalog profile, nozzle
and material are not prerequisites for authoring or reviewing placements.
Layouts remain deliberate source edits; automatic packing is not provided.

## Plan and author a separate print view

Start from the part-by-part orientation decisions in [print design](print-design.md). Use the known usable build dimensions for layout work. Printer identity, nozzle and material can be added later when available; do not delay geometry placement to look up a printer catalog or choose a slicer profile. Record unknown dimensions or exclusions and keep only the dependent fit conclusion provisional.

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

Use the user's known usable build area and any known bed exclusions. If the bed size is unknown, continue authoring and inspecting placements but leave bounds fit unresolved; do not infer dimensions from another project. Leave unprovided exclusions and slicer allowances unknown rather than silently treating them as empty or zero.

If a long rectangular part exceeds one bed edge, try an in-plane Z rotation before splitting it. For an L-by-W rectangle rotated by θ, its axis-aligned planning footprint is `X = |L cos θ| + |W sin θ|` and `Y = |L sin θ| + |W cos θ|`. For example, a 280-by-10 mm rectangle at 45° has an approximate 205-by-205 mm footprint on a 220-by-220 mm bed, before spacing or brim/support allowances. A 280-by-40 mm rectangle at 45° needs about 226-by-226 mm, so it does not fit that bed even though the part length (280 mm) is less than the bed diagonal (about 311 mm). These rectangle calculations are a planning aid, not a fit proof for arbitrary geometry; check the whole transformed shape and all margins. A 3D tilt can change footprint and height, but may worsen layer direction, bed stability, support demand, or surface finish. Do not promise that a diagonal orientation will fit until the geometry and slicer preview confirm it.

Make a placement worksheet before editing the plate view. List each physical instance and its source-part ID, intended plate, orientation and functional reason, then note the source revision and supplied limits used. Distinguish a second placement of shared source geometry from a duplicate group reference. Do not put the same physical instance on multiple plates unless that is an intentional choice you will review.

```text
Plate view / intended build:
Build volume, source and revision:
Usable bed and named exclusions:
Physical instances and source quantities:
Orientation and placement choices:
Part gap, brim and support allowances (known or unknown):
Open slicer questions:
```

Leave margins for the actual slicer's brim, supports, skirt/raft, purge or prime structures, and machine-specific keep-outs. The project profile can describe the rectangular bed exclusions it supports; a rectangular bounds approximation is not an exact representation of a curved, moving or conditional keep-out. Record such limits and check them in the slicer. A compact bounds fit does not establish headroom for support structures or toolhead clearance.

## Review placement limits

Inspect each transformed part and the combined layout in the viewer. Run
`synthcad checks --session NAME --json` for bounds, bed contact, overlap,
allowance and quantity review against the current placed solids and supplied
profile. Read each check's method, scope and evidence: deterministic geometry
calculations and conservative warnings support different conclusions. Unknown
or provisional inputs leave dependent results unresolved.

After editing the source files, wait for the normal reload, switch to each plate
view and run the CLI check. For example:

```text
synthcad view plate-a --session NAME --json
synthcad checks --session NAME --json
```

Read `profileStatus`, the report basis, each check's result/method/evidence, and
the quantity rows before acting on a warning. Repeat after any placement,
geometry or supplied-limit change; the old report describes its recorded
revisions only. `synthcad docs checks` prints the full report guide to stdout.

Set per-view `plateSettings` for known `partGap`, `brim` and `support`
allowances; omitted values remain unknown. Explicit zero is an authored choice,
not a default prediction about slicing. `contactTolerance` is a numerical
comparison tolerance, not a fit allowance. Run `synthcad docs profiles` for
project metadata and see [manufacturing checks](../manufacturing-checks.md)
for the report contract. Verify actual support/brim geometry and accessibility
in the target slicer before describing a layout as ready to print.

The bounds check compares the modeled solids with the supplied bed volume. Brim
and support allowances expand an XY envelope for conservative spacing and
exclusion checks; they do not generate support, brim, skirt, purge structures,
or paths. The model-height check does not add possible support height or detect
toolhead/frame collisions. Use real slicer settings to inspect those structures
and their headroom, the first layer, and the toolpaths for every part. A layout
can pass geometric checks and still be unsuitable to slice or print.

The GUI exports standard 3MF (selected by default) or STL and preserves the selected model solids' current coordinates. Exporting an assembly view therefore preserves assembly placement; exporting a manually authored plate view preserves its plate placement. A plate scene is the authored geometry itself, not proof that parts fit or slice correctly. Run `synthcad docs bambu-handoff` for the complete export and slicer-review workflow; the repository's [standard 3MF export note](../three-mf-export.md) describes the package contents in more detail.
