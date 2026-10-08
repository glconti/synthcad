# Build-plate layouts

Run `synthcad docs build-plates` to print this guide. The current workflow uses manually authored scene views; it does not provide automatic packing or a printer-profile contract.

## Author a separate print view

Keep the assembly in assembly coordinates. Add one named view per intended plate, reusing the same source geometry and parameters while applying deliberate print transforms only in the plate view. Keep all intended pieces visible in that view and give them stable IDs and recognizable names. Put boundary or clearance visuals in a clearly identified, non-exportable group when useful; they are review aids, not automatically checked bed geometry.

Keep each part's geometry definition in a shared JavaScript module and import it
from assembly, inspection and plate entries. Do not paste a second copy of the
modeling code or use an exported mesh as an editable substitute. A deliberate
second physical copy is another placement of the same source part. Source
definitions, placed instances and grouping have different meanings: putting
one instance in a second review group should not create another printable copy.
The current API supports shared imported modules and transformed solids; a
project-wide source/instance reference registry and quantity validation are
planned, not yet implemented. Do not invent manifest fields to imply otherwise.

Use the user's known usable build area and bed exclusions. If these are unknown, leave plate fit provisional and proceed with model review; do not infer a printer from another project. Place parts with room for the slicer choices that are actually known, and state when support or brim allowances have not been included.

## Review placement limits

Inspect each transformed part and the combined layout in the viewer. The current review CLI can report scene and part information, bounds, diagnostics and a screenshot. It does not automatically pack parts or verify bed contact, overlap, clearances, support access, brim room or printer compatibility. Check those conditions explicitly against the chosen machine and slicer before describing a layout as ready to print.

The GUI's Export STL preserves the selected model solids' current coordinates. Exporting an assembly view therefore preserves assembly placement; exporting a manually authored plate view preserves its plate placement. A plate scene is the authored geometry itself, not proof that parts fit or slice correctly. Run `synthcad docs bambu-handoff` for the available export path.
