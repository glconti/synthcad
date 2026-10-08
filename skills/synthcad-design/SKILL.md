---
name: synthcad-design
description: Create and refine parametric CAD models and project scenes for SynthCAD, including assemblies, fit decisions, print layouts and STL handoff. Use for model authoring, not viewer implementation.
---

# SynthCAD design

Use this skill when authoring or reviewing geometry and scene files for SynthCAD. Run `synthcad docs` to list topics or `synthcad docs skill` to print this workflow. Before changing a model, run `synthcad docs api`; it prints the technical geometry contract. Use `synthcad docs projects` for manifests and named views, and `synthcad docs cli` for local viewer review behavior.

Start with `synthcad docs` to list the available topics, then print only the guides needed for the current work:

- `synthcad docs start` for the workflow entry point.
- `synthcad docs modeling` for scene structure and parametric geometry.
- `synthcad docs print-design` for orientation and process decisions.
- `synthcad docs fit-and-assembly` for mating parts and assembly sequence.
- `synthcad docs build-plates` for authored print-layout views.
- `synthcad docs bambu-handoff` for the current STL workflow and its limits.

The CLI prints guide text to standard output, so no local guide files are needed at runtime.

## Work in the user's project

Use the project or directory the user chose. In a SynthCAD checkout, keep personal models and generated artifacts under its ignored `local-scenes/` directory. Keep project-specific dimensions, decisions and evidence with that project. Do not copy printer, nozzle, material or tolerance assumptions from another model. Record unknown values as unknown or provisional; missing printer details do not prevent an initial geometry review.

Preserve authored names and languages. Keep measured values distinct from derived and provisional values. Make geometry parameters the source for both solids and any authored dimension annotations.

## Author and review

Edit the JavaScript scene sources with the available file editor. The `synthcad` CLI opens and reviews projects; it does not author geometry or export models. Keep `scene` valid and include every intended viewer/export component in `displayParts`, using stable IDs and explicit `exportable` flags for designed parts and external context. Treat display colors as visual aids, not material assignments.

After editing, capture the requested revision and wait for that revision to load successfully. Use the returned **displayed** revision for later `--expect-revision` review commands. A successful load and a plausible image establish that the edited source was evaluated and rendered; they do not establish fit, wall strength, printability, a successful slice or physical performance. Run `synthcad docs modeling` for checks the current files can support, and state what still needs slicer or physical evidence.

When a design tradeoff would change the intended appearance, part count or function, explain the impact and get the user's choice before making that change. Physical samples are optional evidence when fit or performance is uncertain; they are not a prerequisite for an ordinary geometry review or export.
