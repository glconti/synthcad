# SynthCAD product requirements

Status: Draft for review. Updated: 2026-10-08.

SynthCAD helps people design better printable objects with an external AI agent.
The agent authors local model files; a shared CAD viewer reloads those files and
lets the person and agent inspect the same design. The finished deliverable is
an STL or a prepared Bambu Studio 3MF project, with an explicit distinction
between geometric checks, printing recommendations and physical evidence.

This document defines v1. [BACKLOG.md](BACKLOG.md) breaks it into deliverable work.
The [GitHub v1 milestone](https://github.com/glconti/synthcad/milestone/1) tracks
the 22 release stories and their live status; deferred ideas have no milestone.
Requirements below describe intended behavior, not capabilities already shipped.
[API.md](../../API.md) remains the contract for the implemented model API.

## User and problem

The primary user is a maker with access to a 3D printer and a local coding agent.
They can explain the object they want, supply measurements and review a model,
but should not need to learn a traditional CAD editing workflow.

Today, an agent can create geometry and manually prepare printing scenes. The
user still has to coordinate files, viewer windows, selected parts, printer
constraints and export versions. Design reasoning and test notes remain largely
in chat or project notes. A convincing assembly view does not establish that its
parts fit a print bed, can be assembled, or will carry a requested load.

SynthCAD should make those relationships visible and inspectable while keeping
ordinary file editing as the modeling workflow. Its primary value is better
printable designs; reducing coordination overhead supports that outcome.

## Product decisions

| Decision | v1 commitment |
| --- | --- |
| Design authoring | The external agent edits source files directly. Hot reload updates the viewer. No geometry-editing command language in the CLI. |
| Agent conversation | Stays in the user's chosen agent. SynthCAD provides review context and guided selection, without an embedded chat or bundled AI model. |
| Agent interface | A self-documenting CLI connects repeated calls to a persistent local project session and provides all operating and design guidance. No filesystem skill setup is needed. |
| Platforms | Windows and Linux are release targets. Manual build/runtime checks pass on Windows and Ubuntu X11/Mesa; automated CI and distribution validation remain backlog work. |
| Knowledge | Versioned domain guidance is available in full through the CLI. Application checks enforce measurable constraints. |
| Design scope | Any object supported by the available geometry tools, including functional, assembled and decorative designs. |
| Printing | Build-plate preparation is a core user story. Account for printer, nozzle, material, orientation, support access and assembly. |
| Delivery | STL plus Bambu Studio project 3MF first. The user reviews and slices in Bambu Studio. |
| Iteration | Physical samples and test feedback are supported, optional flows. They are not prerequisites for ordinary export. |
| Workspace | One unified view of the current project. Assemblies, groups and special views reference shared source parts through distinct instances. No multi-project library in v1. |

Local refers to project files, geometry evaluation, viewer and communication with
the CLI. The external agent may use a cloud model. SynthCAD does not require a
SynthCAD account or cloud service for its own workflow.

## Main user journey

1. **Start.** Install SynthCAD and tell an agent to use it. Running `synthcad`
   or `synthcad --help` shows commands and guidance grouped by area;
   `synthcad docs AREA` prints that area's complete guidance. The agent creates
   or opens project files and opens the project in SynthCAD. The user can also
   open a project directly. No filesystem skill setup is needed.
2. **Establish constraints.** Capture intended use, dimensions and the printer,
   nozzle and material when known. Record provisional measurements explicitly.
   Missing printer settings should not prevent initial design review.
3. **Design.** The agent writes parametric model files and reload checks report
   whether the new design loaded. The agent considers printing and assembly
   throughout, proposing consequential tradeoffs such as splitting a continuous
   surface into glued parts before making that choice.
4. **Review together.** The agent frames and highlights an element. The user
   selects a part or feature, or answers a guided selection request. Both can
   identify the same geometry and revision without copying filenames or opening
   another viewer for every edit.
5. **Prepare plates.** The agent writes print-layout placements that reference
   the same source parts as the assembly. SynthCAD shows the plate boundaries,
   orientations, quantities, warnings and reasons for important choices.
6. **Export.** Review included parts and profile, then export STL or a prepared
   Bambu Studio 3MF. The user opens the latter in Bambu Studio to inspect and slice.
   Report warnings and allow an explicit export-anyway choice when a valid file
   can still be produced.
7. **Optionally iterate physically.** Print samples or full parts, provide fit or
   load-test feedback to the agent, and revise. The current-project overview
   records that evidence and identifies affected parts and outdated exports.

## Current baseline

This inventory reflects the initial PRD draft, before the delivery batches.
For implemented capabilities and platform evidence, see
[Batch 1](../batch-1-validation.md), [Batch 2](../batch-2-validation.md) and
[Batch 3](../batch-3-validation.md) and [Batch 4](../batch-4-validation.md).
It is not a current platform or physical test report.

| Capability | Current state and implication |
| --- | --- |
| Model evaluation and hot reload | Implemented through QuickJS and Manifold, with imported-file watching. Preserve the existing authoring contract. |
| Review workspace | Named parts, groups, colors, visibility, isolation, framing, annotations and English controls are implemented. Authored data stays in its original language. |
| Recovery | A failed reload retains the last valid view and disables export. Extend this distinction to CLI responses. |
| Selection | Part/group selection exists in the tree. Geometric picking and agent-readable selection need work. |
| STL export | Selective export, reference exclusions, current-coordinate preservation and overwrite protection exist. |
| CLI | Scene validation, profiling and preview/render commands exist. A live session protocol and layered command discovery do not. |
| CAD guidance | A repository-local design skill exists. A distributable, discoverable guide bundle needs work. |
| Build plates | Can be authored as separate scenes. There is no unified project/plate workflow or printer-profile contract. |
| Project 3MF | Not implemented. |
| Cross-platform release | Windows build and smoke-test workflows exist. Linux release readiness is unverified. |

See [README.md](../../README.md), [viewer/main.cpp](../../viewer/main.cpp),
[API.md](../../API.md) and the
[existing design skill](../../.agents/skills/dingcad-design/SKILL.md).

## Functional requirements

### R01 Discovery and guidance

An agent with shell and file access can start with `synthcad` or
`synthcad --help`. Both show commands and guidance grouped by area, with a clear
route to command-level help and domain documentation. `synthcad docs AREA`
prints the complete skill/domain text for the requested area to stdout, so the
agent can read all required guidance using the CLI alone. It must not require
installed skills, a source checkout, or filesystem skill setup.

Guides cover the model API, review workflow, printer constraints, orientation,
layer direction, tolerances, joints, assembly, build plates and Bambu handoff.
They identify evidence limits and adapt to the active profile. Command help and
domain text share a versioned source; maintainers may use internal `SKILL.md`
sources without requiring users to install them. Routine responses point to
relevant `synthcad docs AREA` commands instead of repeatedly printing the
complete handbook. Explicit documentation requests return the complete area
text rather than a summary or a path the agent must read separately.

### R02 Project files and overview

Source files remain authoritative for geometry, parameters and print layouts.
A versioned project description links assembly, inspection and plate entry
points, printer profile, assumptions, checks, exports and optional test notes.
The agent can maintain this information using normal file edits. Define the
schema during implementation; do not require a database or import chat history.

The project contract must distinguish a source part's identity and geometry
definition from the identities of its placed instances. Assemblies, groups,
inspection views and all special views, including build plates, reference this
shared source-part graph rather than copying geometry definitions. Groups
reference instances; group membership alone must not create another physical
copy. View and layout transforms are separate from source geometry and from
each other. A shared source edit updates every dependent view at the new
revision, while intentionally repeated quantities remain distinct instance
placements. Core source/instance references work before a printer profile is
configured. This is a future contract; its final schema remains an
implementation decision.

Reject cyclic and dangling references with actionable diagnostics. Resolve
instance membership consistently for review and export: the same instance
reached through several groups is included once, while distinct intended
instances of one source part remain distinct placements.

The viewer presents a current-project overview and switches between its views.
Each export records its source/dependency revision, source parts, included
instances, view/layout transforms and profile revision when applicable.
Changed inputs mark previous checks and exports as outdated without deleting or
silently overwriting them. Existing single-scene files continue to open without
requiring project conversion.

### R03 Persistent sessions and reload acknowledgement

CLI calls open or attach to an existing project session. Reopening the same
project reuses its session; explicit identifiers resolve multiple running
projects. A short-lived CLI process must not terminate the viewer when it exits.
Communication stays local to the user's machine and user session.

The agent can inspect loading state and wait, with a bounded timeout, for an
expected revision of the entry file and its imported dependencies. An unchanged
successful snapshot must never acknowledge a failed or still-pending new edit.
Responses distinguish requested revision, last attempted revision and currently
displayed valid revision, including failure details.

Expose compact human-readable output and a documented structured format with
stable error categories and exit codes. Reports distinguish no session,
ambiguous session, load failure, timeout, cancellation and stale reference.
Connection failure must not create repeated windows or overwrite project files.

### R04 Shared review context

The CLI can read a semantic scene snapshot, part/group selection, annotations,
available measurements, profile and diagnostics; request a screenshot; frame
elements; switch project views; and add, replace or clear agent highlights.

Agent highlights and callouts are separate from human selection, visibility and
export flags. A highlight does not silently change export contents. File reload
preserves camera and review state where identifiers remain valid. The user can
clear agent overlays. The CLI does not create geometry or modify model sources.

### R05 Selection and guided questions

Users can pick parts and geometric faces, edges and vertices and expose that
selection to the agent. A guided pick displays an agent-supplied question,
allowed selection type, Confirm and Cancel. Confirming returns a reference and
useful context such as owning part, position, normal or bounds where applicable.

References identify the scene revision. Stable authored part IDs may survive
reload; geometric feature references must be invalidated unless correspondence
is actually established. Never guess that a triangle or edge index still refers
to the same feature. The mesh-based geometry pipeline requires a feasibility
spike for meaningful face and edge selection before committing its detailed UX.

The CLI can read events since a cursor or wait for a bounded interval. Cancellation,
window closure and incompatible reload resolve pending picks clearly. GUI events
do not independently wake an external agent; that agent owns the read/wait loop.

### R06 Printer and material context

Store printer identity, usable build volume, relevant bed exclusions, nozzle
diameter and material/profile identity per project. Offer concise guided setup
and allow custom profiles. Avoid a global default that silently inherits another
project's printer or material. Show missing or provisional values.

The agent uses this context during design and plate preparation. Separate advice
from settings actually supported by the target slicer. Unknown machine profiles
can support explicit geometric dimensions without being advertised as validated
Bambu presets.

### R07 Design checks and tradeoffs

Report geometry validity, part connectivity, relevant bounds and layout conflicts
where deterministically computable. Identify checks by revision and classify
results as passed, warning, failed or not checked, with their scope and evidence.

Guidance also covers minimum feature size, fit, layer direction, support access,
overhangs, stability and assembly sequence. Mark heuristic assessments as such;
do not present them as sliced results or verified structural capacity. Requested
load capacity remains unverified without appropriate evidence.

The agent asks about consequential changes to appearance, part count, assembly
effort or intended function. It handles routine implementation choices itself.
Printable geometry may still be exported with acknowledged warnings. Invalid
scene state, failed serialization or an empty selection cannot produce a claimed
successful export.

### R08 Build plates

Provide a dedicated plate view with selected printer boundaries, part names,
instance quantities, print orientation and configurable clearance allowances for
brims and supports. Assembly and plate transforms reference shared source parts;
preparing plates must not move the assembly or duplicate its modeling logic.
Plate placements use the project-wide source/instance reference contract from
R02, shared by assemblies, groups and other views. Each intended copy has a
distinct placement identity; reaching one placement through multiple groups
must not multiply its quantity or exported geometry. Source edits propagate to
dependent plate views, with revision-aware checks and export provenance.

The agent authors plate placement in files and explains relevant compromises
between strength, supports, appearance and assembly. SynthCAD checks transformed
bounds, plate contact, overlap and requested clearances. Conservatively estimated
support/brim space is labeled; actual toolpaths remain the slicer's responsibility.

Support multiple plates and flag missing, duplicate or intentionally repeated
parts against the planned quantities. Cyclic or dangling placement references
fail explicitly rather than producing partial layouts or exports. Shared part
references and placement authoring do not require a printer profile;
printer-dependent checks identify missing settings as not checked. Automatic
packing or a mathematically optimal arrangement is not required for v1. The
objective is a reviewable, printer-aware arrangement with explicit tradeoffs.

### R09 Export and slicer handoff

Retain STL export and add Bambu Studio project 3MF with separate named objects,
plate assignments, transforms and supported printer/material/nozzle/process
settings. Map display colors to filament assignments only when explicitly
configured. A colored model alone does not specify multiple filaments.

Pin and document the Bambu Studio versions and profile combinations exercised
by interoperability tests. Preserve supported settings on opening the project;
surface unsupported mappings rather than silently substituting settings. A
generic geometry-only 3MF must not be described as a configured slicer project.

Allow all-exportable and visible-exportable selections, with external references
excluded by default. Distinguish exporting assembly coordinates from prepared
plate coordinates. Show destination, included quantities, profile and warnings.
Preserve overwrite protection, cancellation and empty-export behavior through
both GUI and CLI. Record export provenance only after successful file creation.

Bambu Studio need not be installed for ordinary use or export. SynthCAD does not
execute slicing in v1 and cannot claim verified support generation, toolpaths,
print duration or material consumption from geometry checks alone.

### R10 Optional physical iteration

The agent can add sample scenes and user-reported results tied to a revision and
part. The overview distinguishes proposed, printed, tested and superseded work
without inferring physical actions from an export or a passing digital check.
Users can proceed from design to full export without creating samples or test
records. Compatibility changes identify parts that need reprinting and preserve
previous outputs.

### R11 Delivery and compatibility

Ship usable Windows and Linux packages containing the viewer, CLI and matching
documentation. Preserve existing executable targets, launch scripts, CLI flags
and model APIs, or supply explicit compatible aliases. App-authored UI is English;
authored names, groups, annotations and diagnostics remain verbatim, including
accented text. Retain scaling and input-capture behavior.

Validate installation and launching outside the repository on both platforms.
The exact supported Linux distribution/display matrix and package formats are
release decisions, not established compatibility claims. Licensing terms and
third-party redistribution obligations must be resolved before public release;
no particular license is assumed by this PRD.

## Scope after v1

Defer OrcaSlicer and other slicer adapters, automatic slicer execution, printer
control, direct slicer launching, automatic packing, embedded agent/chat,
agent-specific setup integrations, proactive agent wake-up, cloud collaboration,
the web app and a multi-project dashboard. Do not assume shared slicer ancestry
establishes project-file compatibility.

Full structural simulation, certified strength and permanent topology identities
across arbitrary edits are not promised. The product must explain the limits of
its checks and preserve useful physical evidence when provided.

## Acceptance and success

Release acceptance uses public, non-personal fixtures: a dimensioned fitting,
a multi-part assembled object and a curved decorative object. For each, an agent
must be able to discover the workflow, edit files, confirm reload, review with a
person, prepare plates and export without a custom SynthCAD agent integration.

Required evidence includes:

- A fresh agent session discovers commands and guidance through no-argument
  help or `--help`, reads complete area guidance through `synthcad docs AREA`,
  and completes onboarding with only the CLI, without filesystem skill setup.
- A shared source edit updates assembly, group and special views, including
  plate placements. Distinct instances retain intentional quantities; repeated
  group membership does not duplicate an export. Cyclic/dangling references
  fail explicitly and prior checks/exports become outdated by revision.
- A multi-file edit, broken edit and recovery report the correct displayed
  revision; stale geometry never receives a false success acknowledgement.
- Review tests cover multiple sessions, selection, highlights, guided-pick
  confirmation/cancellation, stale references and independent user controls.
- An oversized part, overlapping placement, missing profile and questionable
  orientation produce appropriately scoped results with actionable context.
- A valid export with acknowledged warnings succeeds; empty, cancelled and
  invalid exports do not alter output files.
- Tested Bambu projects open with expected names, transforms, plates and supported
  settings. The user can review and slice them independently.
- Both the direct-export journey and optional sample/test journey work. No test
  record is fabricated or required just to proceed.
- Windows and the declared Linux matrix pass packaging and regression checks;
  representative authored Italian names remain unchanged.

During pilot sessions, record time to first valid model, context-copying steps,
reload/selection misunderstandings, successful Bambu handoffs and avoidable
reprints attributed to recorded constraints. Establish a baseline before setting
numerical improvement targets. Do not add mandatory telemetry for these studies.

## Decisions to resolve during delivery

| Decision | Required before |
| --- | --- |
| Project schema and dependency-aware revision identity | Shared session implementation |
| Face/edge semantics and performance in the existing mesh pipeline | Full geometric guided picking |
| Initial Bambu Studio version and supported profile matrix | 3MF compatibility commitment |
| Linux support matrix and distribution formats | Cross-platform release candidate |
| License and redistribution terms | Public packaged release |

CLI spelling, local transport and event schema are implementation decisions.
Keep the CLI as a small client of the running workspace; avoid coupling the
product contract to a particular agent or operating-system transport.
