# Modeling scenes

Run `synthcad docs modeling` to print this guide. `synthcad docs api` defines the supported JavaScript operations and the `scene`, `displayParts` and `dimensions` contracts.

## Make the design legible

Choose a clear assembly coordinate system and keep dimensions in the API's millimetres, XYZ, Z-up convention. Define important dimensions once and derive dependent geometry from them. Label assumptions as measured, derived or provisional in the source comments or project notes.

Separate external references from parts the user intends to make. Give designed parts meaningful names, stable IDs and semantic groups. Set `exportable: false` on context solids and set intended manufactured parts explicitly to `true`. Include every component intended for the tree or STL export in `displayParts`; a solid present only inside `scene` is not automatically listed as a part when that array is exported. Preserve IDs when a component is reordered or its geometry changes.

Use color to distinguish neighboring components in the review view. Color does not set filament, material or slicer assignments. Preserve the user's authored names and language.

## Keep views connected to one design

Show assembly parts in their intended assembled coordinates. Use a separate inspection view for useful sections or exploded arrangements and a separate named view for print-layout coordinates. Reuse parameters and source geometry between views so one change updates every representation. Do not let a print transform silently change the assembly view.

Prefer the shared `design` export for new multi-view objects. Source parts own
geometry; instances identify physical copies; groups and views reference those
instances. Run `synthcad docs design` for that contract. Do not also export
`scene` or `displayParts` in a design-graph entry. The legacy exports remain
valid for existing models. Keep graph definitions and source geometry in shared
modules rather than copying solids or modeling code into each view entry.

Author dimension annotations from the same parameters used to build the solids. They are visual aids; the viewer does not derive dimensions from surfaces or test whether anchors match the intended feature.

## Check the modeled result

After a successful reload, inspect the part tree, names, groups, visibility, export flags, bounds and diagnostics. Use API queries such as `boundingBox`, `volume`, `surfaceArea`, `status` or `minGap` only when their result answers a specific design question, and label the inputs and limits of that check. A rendered solid does not by itself establish non-intersection, minimum wall thickness, fit or structural strength.

The viewer and review CLI display authored geometry and semantic parts. They do not automatically run a slicer, inspect toolpaths, certify tolerances or measure a physical part. The GUI's Export STL uses the model's exportable component solids in their current coordinates; it does not arrange an assembly for a bed. Run `synthcad docs build-plates` and `synthcad docs bambu-handoff` when those decisions matter.
