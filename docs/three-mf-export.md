# Standard 3MF export

This repository note describes standard Core 3MF export through the viewer's Export dialog and `synthcad export`. Both use the same review, selection rules and file writer. The dialog defaults to 3MF; the CLI infers the format from the destination's `.3mf` or `.stl` extension.

## What the exported file contains

The exporter writes a standard 3MF package whose model unit is millimeters. It exports the current viewer scene and its current arrangement. A build-volume fit check is useful before export, but the file is still geometry interchange and does not certify a successful slicer import or print.

For a shared design graph, source-linked parts refer to shared source geometry through Core 3MF component references. Each physical placement remains a separate named object with its own transform, instance ID and source-part ID metadata. The mesh resource is shared by instances with the same source-part ID; if a part has no source-part link, it is exported with its own mesh. Object names come from authored part names. Give physical copies distinct authored names when a slicer's object list should distinguish them; stable IDs are metadata and may not be shown by every importer.

The 3MF build represents the current scene only. Exporting a plate view includes that view's arrangement; other project views are not serialized as separate Bambu plates. The file does not contain Bambu machine, nozzle, material or process selections, and it is not a Bambu-native multi-plate project. Choose profiles and create any slicer-specific plate setup in the target slicer.

The export dialog lets you include all exportable parts or only visible exportable parts. All-parts mode includes hidden exportable parts. External references are excluded unless their export flag is enabled. Confirm the scene, selection mode, destination and overwrite choice before saving.

## Author, check, export, then verify

Use this order for a manually arranged print handoff:

1. Run `synthcad docs print-design` to compare part function, layer/load direction, support access, surface finish and split/joint tradeoffs.
2. Run `synthcad docs build-plates` and edit the authored source files to create the intended named plate view and per-instance placements. A known build volume is enough for geometric bounds review; a printer catalog or material profile is not needed to author a placement.
3. Switch the viewer to the plate view and run the geometry check:

   ```text
   synthcad view plate-a --session NAME --json
   synthcad checks --session NAME --json
   ```

   Read the report's basis, methods, affected IDs and quantity scope. Bounds, exclusions, overlap and authored allowances are geometric evidence; they do not generate brims, supports or toolpaths.

4. Open the viewer's **Export** dialog or run `synthcad export plate-a.3mf --session NAME --dry-run --json`. Review the view, quantities, available context and outstanding checks. Save through the dialog's **Export anyway** action, or acknowledge the CLI risks with `--allow-warnings`. Existing files require **Replace file** or `--replace`. Prefer `--expect-revision TOKEN` for agent-driven exports. Choose all or visible exportable parts deliberately.
5. Open that file in the target slicer and compare object count, names, orientation and positions against the authored scene. Importers can apply their own arrangement or object interpretation; SynthCAD cannot guarantee that another application retains the same placement or component presentation. Verify before slicing. If the slicer changes placement, restore the intended positions there or return to the authored plate view and export again.
6. When preparing an actual print, choose the appropriate printer/nozzle, material and process settings in the slicer. Inspect the sliced first layer, supports, brim, clearances, exclusions and complete toolpaths. Record slicer evidence separately from SynthCAD's geometry report.

The project may contain multiple named plate views, but each export is for the currently displayed scene. If a slicer project with several plates is required, prepare and save that project in the slicer after importing and verifying each layout. Do not describe the Core 3MF export as preserving a Bambu multi-plate setup.

Successful exports retain [local revision-linked receipts](export-history.md).
`synthcad export-history --session NAME --json` reads the published history.
Editing the model does not regenerate or overwrite earlier artifacts.

## Limits of the evidence

Successful file creation proves that the viewer serialized the selected solids. It does not prove that every slicer will preserve instance metadata, component reuse, names, position or orientation. A successful import does not prove the model fits the machine, slices as intended, or has sufficient strength, fit or durability. Review the slicer's actual preview and record any printed test results separately.
