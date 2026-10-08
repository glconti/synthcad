# SynthCAD delivery backlog

Status: Tracked in GitHub Issues. Updated: 2026-10-08.

This backlog implements [the v1 PRD](PRD.md). The
[v1 release milestone](https://github.com/glconti/synthcad/milestone/1) contains
the release stories below. SC15 is now deferred. The five delivery phases sequence that single release; they are
not separate GitHub milestones. No release date is set.

GitHub Issues is the source of truth for status, discussion and completion.
This document retains the scope and delivery order. All 28 issues were open when
created; existing partial functionality does not make a story complete. SC23–SC28
are tracked as deferred ideas without a release milestone.

P0 establishes the shared review workflow. P1 completes the agreed printing and
release requirements; it is still required for v1. Later items are outside v1.
Dependencies name backlog IDs. PRD references use its R01–R11 requirement IDs.

## GitHub tracking

| Backlog ID | Issue | Release |
| --- | --- | --- |
| SC01 | [#2 Define the project and revision contracts](https://github.com/glconti/synthcad/issues/2) | v1 |
| SC02 | [#3 Build discoverable command help and structured output](https://github.com/glconti/synthcad/issues/3) | v1 |
| SC03 | [#4 Connect CLI calls to persistent local sessions](https://github.com/glconti/synthcad/issues/4) | v1 |
| SC04 | [#5 Acknowledge hot reload and recover from errors](https://github.com/glconti/synthcad/issues/5) | v1 |
| SC05 | [#6 Expose semantic review state and commands](https://github.com/glconti/synthcad/issues/6) | v1 |
| SC06 | [#7 Provide complete domain guidance through the CLI](https://github.com/glconti/synthcad/issues/7) | v1 |
| SC07 | [#8 Prove geometric selection semantics](https://github.com/glconti/synthcad/issues/8) | v1 |
| SC08 | [#9 Add geometric picking and copyable references](https://github.com/glconti/synthcad/issues/9) | v1 |
| SC09 | [#10 Support guided picks and event readback](https://github.com/glconti/synthcad/issues/10) | v1 |
| SC10 | [#11 Present the current project overview](https://github.com/glconti/synthcad/issues/11) | v1 |
| SC11 | [#12 Capture printer, nozzle and material profiles](https://github.com/glconti/synthcad/issues/12) | v1 |
| SC12 | [#13 Define shared source parts, instances and view layouts](https://github.com/glconti/synthcad/issues/13) | v1 |
| SC13 | [#14 Add the build plate view and deterministic layout checks](https://github.com/glconti/synthcad/issues/14) | v1 |
| SC14 | [#15 Add manufacturing guidance and scoped check results](https://github.com/glconti/synthcad/issues/15) | v1 |
| SC15 | [#16 Prove the Bambu Studio project format and profile mapping](https://github.com/glconti/synthcad/issues/16) | Deferred |
| SC16 | [#17 Export the current plate as standard 3MF](https://github.com/glconti/synthcad/issues/17) | v1 |
| SC17 | [#18 Unify export review, CLI results and provenance](https://github.com/glconti/synthcad/issues/18) | v1 |
| SC18 | [#19 Support optional samples and physical feedback](https://github.com/glconti/synthcad/issues/19) | v1 |
| SC19 | [#20 Establish Windows and Linux CI](https://github.com/glconti/synthcad/issues/20) | v1 |
| SC20 | [#21 Resolve licensing and release permissions](https://github.com/glconti/synthcad/issues/21) | v1 |
| SC21 | [#22 Package the viewer, CLI and guides](https://github.com/glconti/synthcad/issues/22) | v1 |
| SC22 | [#23 Run end-to-end release acceptance and publish contributor guidance](https://github.com/glconti/synthcad/issues/23) | v1 |
| SC23 | [#24 OrcaSlicer and other popular slicer adapters](https://github.com/glconti/synthcad/issues/24) | Deferred |
| SC24 | [#25 Optional slicer execution and support/toolpath inspection](https://github.com/glconti/synthcad/issues/25) | Deferred |
| SC25 | [#26 Automatic plate packing and orientation proposals](https://github.com/glconti/synthcad/issues/26) | Deferred |
| SC26 | [#27 Agent-specific integrations and event wake-up](https://github.com/glconti/synthcad/issues/27) | Deferred |
| SC27 | [#28 Direct slicer launch and printer handoff](https://github.com/glconti/synthcad/issues/28) | Deferred |
| SC28 | [#29 Web workspace and multi-project library](https://github.com/glconti/synthcad/issues/29) | Deferred |

## Existing capabilities to preserve

Reuse the model API and file watcher, component tree and isolation, annotations,
selective STL exporter, error recovery and last-valid view, appearance/camera
work, Windows build scripts and regression suites. Extend them through a shared
application interface instead of building a separate CLI copy of viewer behavior.
Personal projects and released printing files remain outside product commits and
must not become public fixtures.

## Delivery phase 1 Shared review through the CLI

Exit: an agent opens a project once, edits its files, waits for the intended
revision, reads the selection and highlights the result in that same window.
This is the first usable increment, not the complete v1 printing release.

### SC01 Define the project and revision contracts

P0 · R02, R03 · Dependencies: none · New

As an agent, I need to identify the exact project and geometry revision I am
discussing, including changes in imported model files.

- Document a minimal versioned project description with named view entry points
  and extension points for profiles, evidence and export records.
- Define revision identity for entry files and dependencies, including source
  changes while evaluation is running; timestamps alone cannot imply success.
- Specify attempted versus displayed revisions and reload wait outcomes.
- Existing standalone scenes open without migration. Malformed project metadata
  yields actionable diagnostics without a destructive rewrite.

### SC02 Build discoverable command help and structured output

P0 · R01, R03, R11 · Dependencies: none · Extends existing CLI

- With no arguments or `--help`, show commands and guidance grouped by area,
  including how to request complete text with `synthcad docs AREA`.
- Add command-level help, examples, version/capability discovery,
  structured output and documented exit/error categories.
- Keep machine-readable output separate from logs; avoid dumping meshes or
  encoded screenshots into routine responses.
- Preserve existing flags and executable entry points. Clearly identify new
  commands in the technical documentation.
- A shell-using agent can find how to open, inspect and wait without a skill.

### SC03 Connect CLI calls to persistent local sessions

P0 · R03 · Dependencies: SC01, SC02 · New

- Start or attach to a project, list sessions and address a specific session.
  Repeated and simultaneous open requests reuse the intended project instance.
- Finishing a CLI call leaves the viewer running. Closing the viewer returns
  actionable errors to later calls; stale session records can be recovered.
- Resolve multi-project ambiguity explicitly and work with spaces and Unicode
  paths on Windows and Linux.
- Use a local transport restricted to the owning user. Requests act through the
  application's shared state rather than UI automation or a second geometry copy.

### SC04 Acknowledge hot reload and recover from errors

P0 · R03, R07 · Dependencies: SC03 · Extends file watcher and diagnostics

- Inspect load state and wait with a timeout for an expected dependency revision.
- Test edits to entry points and imported modules, rapid saves, evaluation
  failure, recovery and a newer edit superseding a pending one.
- Return attempted/displayed revisions and full diagnostic access. A retained
  valid model is never returned as successful loading of the broken revision.
- Keep GUI and CLI export disabled on invalid current scene state.

### SC05 Expose semantic review state and commands

P0 · R04 · Dependencies: SC03, SC04 · Extends tree, camera and rendering

- Read named parts, groups, selection, visibility/export state, annotations,
  bounds and active view with revision identifiers.
- Frame, highlight and clear overlays, switch a project view, and save a
  screenshot with a compact result containing its path.
- Keep agent highlights distinct from user selection and exportability; the
  user can clear them. Preserve camera on reload where applicable.
- Verify requests do not operate on a different session or stale revision.

### SC06 Provide complete domain guidance through the CLI

P0 · R01 · Dependencies: SC02 · Extends repository design guidance

- Provide on-demand guides for source authoring, review, print design and later
  plate/export workflows. Reference the implemented API rather than duplicating it.
- `synthcad docs AREA` prints the complete skill/domain text for that area to
  stdout, including the guidance needed to use its commands. Return full text
  rather than a summary or a filesystem path.
- Generate help and domain guidance from a consistent versioned source.
  Internal `SKILL.md` authoring is allowed; user-side skill installation or
  filesystem setup is not part of the workflow.
- Verify a fresh agent discovers areas from no-argument help and `--help`, reads
  complete guidance, and designs a new object using only the CLI. Do not inherit
  a personal project's dimensions, printer or material.

## Delivery phase 2 Precise discussion and project context

Exit: user and agent can discuss a selected feature, answer a guided pick and see
the current project's views, assumptions and evidence together.

### SC07 Prove geometric selection semantics

P0 · R05 · Dependencies: SC01 · Technical spike

- Evaluate useful face, edge and vertex picking in the existing triangulated
  Manifold pipeline; distinguish surface features from arbitrary tessellation.
- Demonstrate a flat fitting and curved object, reload invalidation and
  coordinate/normal context. Record performance and topology limitations.
- Deliver a short design decision and prototype. If meaningful feature picking
  cannot meet the intended workflow, raise that scope decision before SC08;
  do not silently ship triangle IDs as permanent CAD face identities.

### SC08 Add geometric picking and copyable references

P0 · R05 · Dependencies: SC05, SC07, SC12 · Extends selection

- Pick part, face, edge and vertex using the agreed semantics; expose ownership
  and geometric context in GUI and structured responses.
- Copy a reference usable by the agent. Preserve authored part IDs, but reject
  stale geometric references unless correspondence is established.
- Test overlapping geometry, hidden parts, high DPI and curved fixtures, keeping
  camera gestures separate from selection gestures.

### SC09 Support guided picks and event readback

P0 · R05 · Dependencies: SC08 · New

- Display a question with allowed selection type, Confirm and Cancel; return
  confirmed references without inserting a chat interface into SynthCAD.
- Provide cursor-based event retrieval and bounded waiting, including explicit
  timeout, cancellation, window-close and incompatible-reload outcomes.
- Handle duplicate/retried requests and reject or explicitly replace conflicting
  requests. A timed-out read does not duplicate or silently confirm a pick.
- Document that the external agent must call/read/wait; events do not wake it.

### SC10 Present the current project overview

P1 · R02, R10 · Dependencies: SC01, SC05 · New

- Show assembly, inspection and plate entries, measurements/assumptions, check
  results, revisions, exports and optional physical feedback from project files.
- Hot reload edits to project metadata and report malformed entries separately
  from valid geometry when possible.
- Mark stale checks/exports when their source or profile changes; retain older
  records and artifacts. Preserve user-authored text verbatim.
- Keep standalone scenes usable and avoid introducing a multi-project library.

## Delivery phase 3 Printer-aware design and plates

Exit: an agent prepares explicit printable layouts from shared source parts;
the user can inspect plate constraints and understand remaining uncertainties.

### SC11 Capture printer, nozzle and material profiles

P1 · R06 · Dependencies: SC01 · New

- Start with custom build volume alone. Exclusions, nozzle, material, printer
  identity and provenance are optional refinements; known-printer catalogs are
  deferred. Keep the existing manifest container for compatibility.
- Expose active and missing values to the viewer and CLI. Project switching does
  not silently carry another project's profile into the design.
- Allow review before setup is complete; flag which printing checks cannot run.
- Do not claim a custom geometric profile is a validated slicer preset.

### SC12 Define shared source parts, instances and view layouts

P1 · R02, R08 · Dependencies: SC01 · Extends scene authoring

- Define a project-wide reference contract, with source-part identity
  distinct from placed-instance identity. Assemblies, groups and all special
  views, including inspection and build plates, share source geometry
  definitions rather than copying them. Publish the versioned schema and
  its compatibility behavior through the CLI guidance.
- Groups reference instances. Keep view/layout transforms separate from source
  geometry and from other views, so preparing plates does not move the assembly.
  Editing a shared source updates all dependent views at the new revision.
- Support multiple plates, intended quantities and intentional multiple copies
  as distinct placements of shared source parts. Resolve repeated membership of
  one instance through multiple groups once for export; retain distinct
  intended instances even when they share source geometry.
- Reject cyclic and dangling references explicitly. Expose source/dependency
  revisions, instance membership and view/layout identity for future checks and
  export provenance. Changed inputs invalidate displayed-revision guards;
  persistent check/export records belong to SC14/SC17.
- Keep the core source/instance/group/view contract and placement authoring
  independent of printer setup. Printer-aware boundaries, clearance allowances
  for brims/supports and plate validation belong to SC11/SC13.
- Test shared edits across assembly, groups and special views, independent
  transforms, repeated quantities, duplicate group membership, and invalid
  references before accepting the contract.
- Legacy hand-authored print scenes remain viewable; identify the extra metadata
  needed for validated quantities and plate checks.

### SC13 Add the build plate view and deterministic layout checks

P1 · R07, R08 · Dependencies: SC05, SC11, SC12 · New

- Render actual configured bed boundaries/exclusions, part names and orientation.
- Check transformed bounds, bed contact, overlap, clearance and planned
  quantities. Label conservative approximations and uncomputed results.
- Link warnings to affected parts and revisions for CLI highlighting.
- Exercise out-of-bounds, floating, overlapping, missing and intentionally
  duplicated parts. No automatic packing is required.

### SC14 Add manufacturing guidance and scoped check results

P1 · R01, R07 · Dependencies: SC06, SC11, SC13 · Extends guidance/checks

- Standardize passed/warning/failed/not-checked results with evidence, scope,
  affected elements and source/profile revision.
- Add guides for layer direction, support removal, minimum features, joints,
  tolerances and feasible assembly; distinguish heuristic advice from geometry
  checks and physical validation.
- Explain consequential appearance/strength/assembly compromises for user
  choice; avoid interrupting routine source edits with approval prompts.
- Warnings offer practical next actions and do not imply verified load capacity.

## Delivery phase 4 Reliable printing handoff and optional iteration

Exit: the same reviewed design produces traceable STL files or a standard 3MF
plate. Physical sample workflows are available but never mandatory. Printing
orientation and placement guidance take priority over slicer-specific settings.

### SC15 Prove the Bambu Studio project format and profile mapping

Deferred by user on 2026-10-08 · R09 · Dependencies: SC11, SC12 · Technical spike

- Select and record the initial Bambu Studio version/profile matrix.
- Produce a minimal interoperable multi-object, multi-plate project containing
  transforms and printer/material/nozzle/process configuration.
- Open it in the selected Bambu Studio version and inspect retained settings.
  Document supported fields, unsupported mappings and redistribution constraints.
- Deliver public fixtures and an implementation decision; shared ancestry with
  another slicer does not count as verification.

### SC16 Export the current plate as standard 3MF

P1 · R09 · Dependencies: SC12, SC13 · New

- Export the current arrangement as standard 3MF with named objects, shared
  source meshes, physical instances and transforms, without an installed slicer.
- Keep STL, reference exclusions, visible/all selection and file-safety behavior.
- Validate archive contents, shared references, units and world geometry, with
  a basic import check in an available slicer.
- Printer, filament, nozzle and process settings are selected in the slicer;
  native multi-plate projects and exact preset interop remain deferred in SC15.
- Publish actionable orientation, support, splitting, joint and placement
  guidance through CLI stdout. No slicing engine or G-code is included.

### SC17 Unify export review, CLI results and provenance

P1 · R02, R07, R09 · Dependencies: SC04, SC10, SC14, SC16 · Extends STL export

- Expose GUI/CLI exports through shared selection and validation logic, with
  explicit assembly/plate source, format, quantities, profile and destination.
- Preserve all-exportable versus visible-exportable semantics and reference
  exclusions. Report risks and support explicit export anyway for valid output.
- Empty selection, cancellation, invalid scene and unsuccessful writing leave
  existing files intact; replacement requires explicit intent.
- Record source/profile/layout revision only after success. Older outputs are
  retained and marked outdated rather than regenerated by a model reload.

### SC18 Support optional samples and physical feedback

P1 · R10 · Dependencies: SC10, SC12, SC17 · Formalizes an existing agent workflow

- Link sample scenes and user-reported fit/load observations to parts/revisions.
- Show proposed, printed, tested and superseded states only from explicit records.
- Identify reprint implications of compatibility changes and preserve outputs.
- Verify a full-export flow with no samples or test records and a separate flow
  that revises a design after sample feedback.

## Delivery phase 5 Portable release and onboarding

SC19 and SC20 should begin early while feature work proceeds. Exit: documented
packages run on the declared Windows/Linux matrix and the end-to-end workflow
has repeatable evidence. No release date is assigned yet.

### SC19 Establish Windows and Linux CI

P1 · R11 · Dependencies: none · Extends Windows build/test tooling

- Define the initial Linux distribution, architecture and display support matrix.
- Build Release and run existing appearance, parts, camera and dimensions suites
  on both platforms; retain the Windows icon/reload smoke test.
- Add applicable session, reload, picking and export tests as they land.
- Verify fonts, authored Unicode, input mapping, high DPI and graphics behavior;
  a successful compilation alone does not establish GUI support.

### SC20 Resolve licensing and release permissions

P1 · R11 · Dependencies: none · Release prerequisite

- Obtain the maintainer's intended license terms and confirm the basis for
  redistributing inherited source and bundled dependencies/assets.
- Add the agreed license, notices and packaging obligations after terms are
  supplied. Do not invent a license from informal permission.
- Record completion before advertising distributable open-source releases.

### SC21 Package the viewer, CLI and guides

P1 · R01, R11 · Dependencies: SC03, SC06, SC19, SC20 · New

- Select Windows/Linux distribution formats and ship matching binaries/docs.
- Verify first launch, area-grouped no-argument/`--help` discovery, complete
  `synthcad docs AREA` output and session reuse from outside the repository,
  without developer tools, source checkout or filesystem skill setup.
- Document installation, upgrades and compatibility; preserve existing targets
  and scripts through aliases where needed.
- Test package contents for required runtime assets and absence of personal
  models, screenshots, build leftovers and secrets.

### SC22 Run end-to-end release acceptance and publish contributor guidance

P1 · R01–R11 · Dependencies: SC09, SC14, SC17, SC18, SC21 · New

- Use public fitting, assembled-object and curved-object fixtures for the PRD
  journeys, including CLI-only guidance discovery and optional iteration.
- Record platform and Bambu version results, unresolved limitations and pilot
  measures: time to first valid model, context copying, misunderstandings,
  handoff success and avoidable reprints. Establish a baseline before targets.
- Publish a quick start, architecture/contribution guide, supported versions and
  known limitations. Keep technical references synchronized with behavior.
- Require repeatable acceptance evidence before calling v1 ready; never substitute
  renders or digital checks for physical test claims.

## Later backlog

| ID | Capability | Prerequisite or boundary |
| --- | --- | --- |
| SC23 | OrcaSlicer and other popular slicer adapters | Per-slicer project/settings interoperability tests after Bambu delivery |
| SC24 | Optional slicer execution and support/toolpath inspection | Explicit installed-slicer integration and truthful result provenance |
| SC25 | Automatic plate packing and orientation proposals | Compare strength, supports and assembly costs; retain user review |
| SC26 | Agent-specific integrations and event wake-up | Keep the portable CLI contract sufficient for ordinary use |
| SC27 | Direct slicer launch and printer handoff | Separate user intent and lifecycle from file export |
| SC28 | Web workspace and multi-project library | Reassess demand and local/cloud boundaries after local v1 |

## Recommended next implementation

The shared review loop (SC01–SC05) is delivered; see the
[Batch 1 evidence](../batch-1-validation.md). The CLI guidance and selection
feasibility work (SC06–SC07) are documented in
[Batch 2](../batch-2-validation.md). Shared sources, instances and independent
view layouts (SC12) are implemented and verified on Windows and Linux in
[Batch 3](../batch-3-validation.md). Live acceptance status remains in GitHub.

Geometric picking and revision-bound references (SC08) are implemented; see
[Batch 4](../batch-4-validation.md). Its Windows desktop clipboard verification
remains outstanding, so the issue stays open. **SC09** guided picks and
event readback build on this selection contract; see [Batch 5](../batch-5-validation.md).
**SC10–SC11** project overview and printer profiles are integrated in
[Batch 6](../batch-6-validation.md). **SC13–SC14** build-plate review and scoped
manufacturing results build on those profiles and shared instances; see
[Batch 7](../batch-7-validation.md). The user deferred exact Bambu interop on
2026-10-08. Standard current-plate 3MF in **SC16**, strengthened printing guidance
and build-volume-only checks are verified in [Batch 8](../batch-8-validation.md).
**SC17** export provenance and shared CLI/GUI actions are verified in
[Batch 9](../batch-9-validation.md). Next is **SC18** optional physical feedback,
with no mandatory sample-print step.
Start Linux CI and licensing
resolution in parallel when resources are available; do not leave either until
packaging. The Bambu-format spike should precede promises about exact export
settings.
