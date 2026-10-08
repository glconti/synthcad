# Standard 3MF/STL handoff and slicer review

Run `synthcad docs bambu-handoff` to print this guide.

## Export authored geometry

Use the viewer's export dialog or `synthcad export` for geometry handoff. Both offer standard Core 3MF and STL through the same review and guarded writer. The dialog defaults to 3MF; the CLI infers the format from the `.3mf` or `.stl` destination, or accepts `--format` with a matching extension.

Choose all exportable parts or visible exportable parts deliberately: all-parts mode includes hidden exportable parts, and external references are excluded by default unless their export flag is enabled. Both formats keep selected solids in the current scene coordinates. Export does not move an assembly onto a bed, rearrange parts, or union overlapping bodies. Export the assembly view when its assembly poses are intended; export a manually authored plate view when those plate placements are intended.

The 3MF output is standard Core 3MF geometry interchange in millimeters, not a configured slicer project. For source-linked parts, it can share mesh resources while keeping each physical placement as a separate named object with its own transform. Instance and source-part IDs are metadata; slicers may not display or preserve them. Object names come from authored part names. It does not choose a Bambu machine, nozzle, material or process profile, or create a Bambu-native multi-plate project. A 3MF exported from one plate view represents that view; other authored plate views are not embedded as Bambu plates. Finish printer/material setup and any multi-plate preparation in the target slicer. See [standard 3MF export](../three-mf-export.md) for package details.

The viewer asks before replacing an existing destination. Confirm the scene, included parts, intended view and coordinates in the dialog before saving.

## Review each plate in the target slicer

Before exporting, author the intended plate views and run the geometry review:

```text
synthcad docs print-design
synthcad docs build-plates
synthcad view plate-a --session NAME --json
synthcad checks --session NAME --json
```

Create plate views by editing the authored scene files using the print-design and build-plates guidance. Open the intended plate view and run the checks against that view. Read the report's basis, profile status, check methods, affected IDs and quantity scope. It checks current geometry against supplied dimensions and allowances; it does not simulate slicing. You can author and geometrically review a plate using a known build volume without selecting a catalog printer or a material profile. Add printer/nozzle/material choices when the actual slicer handoff is being prepared.

Review the export before creating a file:

```text
synthcad export plate-a.3mf --session NAME --dry-run --json
synthcad export plate-a.3mf --session NAME --expect-revision TOKEN --allow-warnings --json
synthcad export-history --session NAME --json
```

Use the intended displayed revision from the loaded viewer for `TOKEN`. Read the dry run's view, quantities, context and risks before acknowledging them with `--allow-warnings`; unchecked strength, supports and slicing remain unchecked after export. The GUI shows the same review with **Export anyway** for outstanding concerns. `--visible-only` selects only currently visible exportable parts; omission includes hidden exportable parts too. Existing destinations require `--replace` or explicit GUI confirmation. Empty, invalid or stale exports leave destination files intact. Dry runs write neither geometry nor history. Each successful export saves a receipt with output hash and source/layout/profile revisions; if history cannot be saved, the output remains usable and the response reports that separately. Local history is stored alongside the project under `.synthcad/exports/`, separately from authored metadata. Older files are not regenerated after edits. If a request times out around file commit, inspect the destination and `export-history` before retrying.

Import the resulting file into the target slicer and verify the object count, authored names, orientations and positions against the intended viewer scene. Slicers can interpret or rearrange imported models differently, so the exported arrangement is not a promise about the slicer's final plate layout.

In the slicer, select the exact printer and nozzle variant, filament/material profile, and process profile for the job. Confirm the loaded model's orientation and placement; do not assume the file's colors identify the chosen filament. Inspect the complete toolpath preview, including first layer, support contacts and removal access, brim/skirt/raft, purge or prime structures, exclusion areas, part-to-part clearance, and any machine-specific headroom or collision warnings. Verify small features and mating surfaces in the sliced preview. Resolve or record every warning that affects the intended function before calling the model ready.

When saving handoff evidence, record the slicer and version, selected machine/nozzle, material and profile names, process profile, plate/view, and the result of the actual slice. Keep slicer output distinct from SynthCAD geometry checks. A profile selection, successful file open, or geometric fit does not establish successful slicing, printability, strength, fit or durability.

## What a handoff does not establish

Neither STL nor standard Core 3MF is a configured Bambu Studio project. SynthCAD does not currently assign Bambu profiles, slice the model or verify resulting toolpaths. Do not report a Bambu-native project, successful slice, print-time estimate, material-use estimate or physical validation as a SynthCAD result. Record slicer checks separately from model checks and physical results. If load or fit matters, a printed test coupon or first article is optional evidence; record its machine, profile, orientation, conditions and measurements, and limit the claim to what that test actually covers.
