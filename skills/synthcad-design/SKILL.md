---
name: synthcad-design
description: Create and refine parametric CAD models and project scenes for SynthCAD, including assemblies, fit decisions, print layouts and STL handoff. Use for model authoring, not viewer implementation.
---

# SynthCAD design

Use this skill when authoring or reviewing geometry and scene files for SynthCAD. Run `synthcad-cli docs` to list topics or `synthcad-cli docs skill` to print this workflow. Before changing a model, run `synthcad-cli docs api`; it prints the technical geometry contract. Use `synthcad-cli docs projects` for manifests and named views, and `synthcad-cli docs cli` for local viewer review behavior.

Start with `synthcad-cli docs` to list the available topics, then print only the guides needed for the current work:

- `synthcad-cli docs start` for the workflow entry point.
- `synthcad-cli docs modeling` for scene structure and parametric geometry.
- `synthcad-cli docs design` for shared source parts, instances, groups and layouts.
- `synthcad-cli docs print-design` for orientation and process decisions.
- `synthcad-cli docs fit-and-assembly` for mating parts and assembly sequence.
- `synthcad-cli docs build-plates` for authored print-layout views.
- `synthcad-cli docs bambu-handoff` for STL/3MF export and slicer handoff.

The CLI prints guide text to standard output, so no local guide files are needed at runtime.

## Work in the user's project

Use the project or directory the user chose. In a SynthCAD checkout, keep personal models and generated artifacts under its ignored `local-scenes/` directory. Keep project-specific dimensions, decisions and evidence with that project. Do not copy printer, nozzle, material or tolerance assumptions from another model. Record unknown values as unknown or provisional; missing printer details do not prevent an initial geometry review.

Preserve authored names and languages. Keep measured values distinct from derived and provisional values. Make geometry parameters the source for both solids and any authored dimension annotations.

For a simple project, use `design.js` and `synthcad.json`; assembly and plate views can share that same design file. Put requested final outputs in `exports/`, preferring 3MF and adding STL when requested. Read routine CLI JSON responses in memory rather than saving checks, export reviews or response dumps. Use the OS temporary directory for review screenshots; retain previews or print-note documents only when requested. The app maintains export history under `.synthcad/`. Add source modules only when the model's complexity benefits from them.

## Author and review

Edit the JavaScript scene sources with the available file editor. The `synthcad-cli` executable opens and reviews projects and exports STL/3MF; geometry authoring happens through file edits. For new multi-view projects, use a shared `design` graph: define geometry once, reference physical instances from groups and layouts, and keep assembly and plate transforms separate. For legacy scenes, keep `scene` valid and include every intended viewer/export component in `displayParts`. Do not mix these export styles. Use stable IDs and explicit `exportable` flags for designed parts and external context. Treat display colors as visual aids, not material assignments.

After editing, capture the requested revision and wait for that revision to load successfully. Use the returned **displayed** revision for later `--expect-revision` review commands. A successful load and a plausible image establish that the edited source was evaluated and rendered; they do not establish fit, wall strength, printability, a successful slice or physical performance. Run `synthcad-cli docs modeling` for checks the current files can support, and state what still needs slicer or physical evidence.

When a design tradeoff would change the intended appearance, part count or function, explain the impact and get the user's choice before making that change. Physical samples are optional evidence when fit or performance is uncertain; they are not a prerequisite for an ordinary geometry review or export.

Open the project once and use the returned session name for later calls. A timeout or access error is not proof the viewer closed. Check `sessions` and retry the same project/session with the required execution permissions. Keep the same registry; do not change `SYNTHCAD_SESSION_DIR` or create a project-local `.sessions/` directory to work around a failed call.
