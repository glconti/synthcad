# Batch 8: printing guidance, build volume and standard 3MF

Scope changed by the owner on 2026-10-08: printing knowledge and plate placement
take priority over exact slicer preset interoperability. SC15 is deferred;
SC16 now covers standard Core 3MF for the current arrangement. Known-printer
catalogs are later work. SC17 still owns CLI exports and persistent provenance.

## Delivered behavior

The Export dialog offers 3MF by default and retains STL. Both use the existing
all-exportable/visible-exportable selection and exclude reference parts unless
explicitly enabled. File extensions follow the selected format; cancellation,
empty or invalid scenes and overwrite confirmation retain their semantics.
3MF serialization uses exclusive temporary files and atomic replacement so a
serialization/write failure does not truncate an existing destination.

Core 3MF carries millimetre geometry, separate named instances and their current
poses. Shared design sources become shared mesh resources. Aliases remain one
physical copy, while distinct coincident instances remain distinct. Mesh
property seams use Manifold's topology merge map, not coordinate welding.
No vendor presets, native multi-plate settings or G-code are written.

Only usable build dimensions are needed to display a bed and check rotated
part bounds and height. Unknown bed exclusions remain explicitly unchecked;
printer identity, nozzle and material do not gate those geometric checks.
The existing profile container remains compatible without requiring a known
printer catalog. Bounds use the whole transformed solid, not length alone.

The bundled `print-design`, `build-plates`, `profiles` and `bambu-handoff` guides
explain orientation and layer/load tradeoffs, support access, joints and splits,
diagonal placement, instance quantities and spacing/allowance evidence. Agents
author the placements in source files; this does not add an automatic packing
solver or promise an optimal arrangement.

## Verification

- Windows MSVC Release viewer/CLI and focused export, parts/UI, profile,
  manufacturing, plate UI and guidance tests passed, along with the four
  existing camera/dimension/appearance/parts regression suites.
- Ubuntu 24.04/GCC Release passed all 24 C++ suites and native manufacturing,
  guided-picking, volume-only bed display and actual GUI 3MF export workflows.
- A 280 by 10 mm bar fails straight placement on a 220 mm square bed and passes
  at 45 degrees; a 280 by 40 mm bar at that angle correctly fails. Unknown
  exclusions and optional process context retain separate evidence states.
- Core writer tests cover shared meshes, distinct instances, alias deduplication,
  filtering, arbitrary XYZ rotations compared with Manifold vertices, UTF-8/XML
  escaping, indexed closed topology, ZIP CRCs and destination preservation.
- `scripts/test-three-mf-export.py` independently parses generated ZIP/XML,
  relationship and metadata namespaces, units, references, oriented topology
  and world geometry. Actual GUI plate output has two source meshes and three
  physical instances. Invalid units/references/topology are rejected.
- A basic import/re-export through Bambu Studio 2.8.2.61 on Linux preserved the
  grounded fixture's world geometry at 0.001 mm with arrangement/orientation
  disabled. The slicer dropped custom metadata namespace declarations in its
  saved file; the comparison reports that warning separately. This is not an
  exact preset/native-project compatibility claim. Importers may reposition
  other arrangements, which users must inspect in the slicer.
- The standalone stdout-guidance test passed all 14 topics without a checkout,
  viewer or skill installation. The Windows export-dialog preview was inspected.
- All nine released V5 STL hashes remain unchanged. No personal models, generated
  archives, slicer profiles, screenshots or build products are included.

Evidence is retained locally under ignored `out/plate-export` and
`out/linux-validation/batch8-*`. Geometry checks and export do not establish
slice quality, physical fit, strength or a successful print.
