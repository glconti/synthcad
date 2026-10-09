# SynthCAD modeling API roadmap

Status: Prioritized follow-up backlog. Updated: 2026-10-09.

Epic: [SC29 / #30](https://github.com/glconti/synthcad/issues/30). GitHub Issues is authoritative for status and completion.
This document retains the scope and delivery order under the [delivery backlog](BACKLOG.md).
These are proposed capabilities, not additions to the currently implemented [API](../../API.md).
Their product outcomes are defined in [PRD requirements M01–M10](PRD.md#modeling-requirements-beyond-v1),
separately from the v1 requirements and release gates.

## Priority and release placement

The current CLI and local-session work remains the active focus. The open v1
release gates observed on 2026-10-09 are geometric-picking acceptance (#9), CI
(#20), licensing (#21), packaging (#22) and end-to-end acceptance (#23).
Those issue states are a dated snapshot; consult GitHub for changes.

Existing P0/P1 labels mean required for v1. New modeling items use:

- **P2:** Next modeling investment after current CLI/session and v1 work. High
  value or an enabling foundation; not a v1 blocker.
- **P3:** Later investigation requiring evidence or coordination before an
  implementation commitment.
- **scope:later:** No release milestone or date. No item is automatically
  promoted to v1 by being a dependency of another modeling item.

The `area:modeling-api` label groups the epic and follow-ups. The epic uses
`type:epic`; SC38/SC39 use `type:spike`. Priority describes investment order,
while prerequisites below describe technical order. Small conformance fixes
can be considered separately when they demonstrably block current work;
the larger roadmap is not pulled into the release implicitly.

## Delivery order and rationale

| Order | Issue | Priority | Value / decision |
| --- | --- | --- | --- |
| A — baseline first, then continuous | [SC37 / #31](https://github.com/glconti/synthcad/issues/31) — Benchmark agent modeling and parameter-change reliability | P2 | Independent geometry expectations and repeated parameter-change tasks establish the baseline and guide the rest of the roadmap. |
| A — start first | [SC30 / #32](https://github.com/glconti/synthcad/issues/32) — Define a typed and versioned JavaScript modeling contract | P2 | Typed signatures, units/defaults, validation, compatibility and executable documentation reduce mistakes before evaluation. |
| B — foundational geometry | [SC31 / #33](https://github.com/glconti/synthcad/issues/33) — Add first-class 2D profiles, offsets and workplanes | P2 | Profiles, 2D Booleans/offsets and oriented workplanes remove repeated geometry construction. |
| B — foundational placement | [SC32 / #34](https://github.com/glconti/synthcad/issues/34) — Add named reference frames and deterministic part attachments | P2 | Named local frames and deterministic attachments reduce placement errors while preserving source/instance/view separation. |
| B — numerical foundation | [SC36 / #35](https://github.com/glconti/synthcad/issues/35) — Define consistent tessellation quality and approximation contracts | P2 | Consistent tessellation/error semantics make checks and export reproducible; separate approximation, numerical tolerance and physical allowance. |
| C — practical authoring library | [SC33 / #36](https://github.com/glconti/synthcad/issues/36) — Add reusable holes, patterns and mechanical feature helpers | P2 | Tested holes, patterns, bosses and ribs return reusable construction references and explicit fit conventions. |
| D — semantic authoring | [SC34 / #37](https://github.com/glconti/synthcad/issues/37) — Preserve named parameters, features and modeling provenance | P2 | Named parameters and features retain inputs, references and source context; authored identity remains separate from output topology. |
| E — verifiable design intent | [SC35 / #38](https://github.com/glconti/synthcad/issues/38) — Add geometric queries and executable design checks | P2 | Measurements and model-specific assertions return geometric evidence and explicit missing/ambiguous outcomes. |
| Later — evidence-gated capability study | [SC38 / #39](https://github.com/glconti/synthcad/issues/39) — Evaluate sweeps, lofts, fillets and shelling against modeling tasks | P3 | Use failed benchmark tasks to choose between specialized helpers, mesh methods and possible B-rep capabilities. |
| Later — coordinate with CLI/session work | [SC39 / #40](https://github.com/glconti/synthcad/issues/40) — Assess bounded and cancellable model evaluation | P3 | Study bounded/cancellable evaluation with the session owner; a CLI wait timeout is not native computation cancellation. |

Start SC37 and SC30 together: measure current failures while stabilizing the
contract. SC31, SC32 and SC36 can then be designed together around consistent
frames and quality semantics. SC33 provides immediate authoring benefits.
SC34 and SC35 add inspectable design intent and verifiable revisions.
SC34 need not wait for every library helper, and SC37 continues throughout.

Keep the existing low-level geometry API available. Add profiles, frames,
named features and checks above Manifold through compatible contracts. A
general constraint solver, persistent topological naming, arbitrary solid
shelling and a kernel replacement are not assumed prerequisites.

## Scoped follow-ups

### SC37 Benchmark agent modeling and parameter-change reliability

P2 · [SC37 / #31](https://github.com/glconti/synthcad/issues/31) · Prerequisites: none.

Initial renders and successful evaluation do not measure whether agents can revise a design while preserving its requirements. Establish evidence before selecting more expensive kernel work.

- Create public, synthetic tasks for an enclosure/lid, rotated mount, repeated-instance assembly and curved profile; include explicit expected geometry and parameter-change requests.
- Include edits that enlarge an enclosure while preserving walls, change mounting patterns while preserving alignment, rotate a component while preserving clearance, and change hardware dimensions.
- Separate deterministic geometry/parameter-sweep regression checks from repeated end-to-end agent trials; record model/version, guidance, runtime/build and quality settings for trials.
- Record requirement pass rate, repair attempts, elapsed evaluation time, agent effort and failure categories; establish a baseline before choosing improvement targets.
- Use the same corpus to accept SC30–SC36 and to decide SC38 scope. Add cases when a reproducible failure reveals a capability gap.

**Acceptance evidence:** Benchmark checks must be capable of failing a deliberately broken model; use independent geometric expectations rather than echoing source parameters. Include parameter boundary cases and report unverified physical claims separately.

**Boundary:** This is modeling-specific follow-up to SC22 release acceptance, not a new blocker for v1 or a duplicate CLI/session acceptance harness. Personal scenes and private measurements are excluded.

### SC30 Define a typed and versioned JavaScript modeling contract

P2 · [SC30 / #32](https://github.com/glconti/synthcad/issues/32) · Prerequisites: none.

The manually maintained global bindings and API cheat sheet can drift; argument conventions, available options and failure behavior are not captured in a machine-checkable authoring contract.

- Inventory the implemented binding surface and establish a versioned contract covering names, signatures, defaults, units, return types and error categories; reconcile cylinder segment documentation and sphere quality limitations.
- Publish TypeScript declarations usable by JavaScript/JSDoc and checkJs. TypeScript execution is not required; retain compatibility with existing globals and imported JavaScript modules.
- Check documentation, declarations and executable examples against real bindings in CI, through a shared specification or conformance checks.
- Define consistent validation and structured diagnostic context for new/versioned APIs, including invalid numbers, unsupported options and source/operation context. Document compatibility before tightening existing calls.
- Provide a documented extension pattern for profiles, frames, features and queries without requiring a new CLI command language or package registry.

**Acceptance evidence:** Run public existing scenes unchanged; type-check good and intentionally invalid authoring examples; verify documented defaults/options against runtime behavior. Invalid input must identify the operation and argument.

**Boundary:** Documentation content may flow through the existing guide bundler. CLI command discovery, binary names, installation and session behavior remain with the active CLI/session work.

### SC31 Add first-class 2D profiles, offsets and workplanes

P2 · [SC31 / #33](https://github.com/glconti/synthcad/issues/33) · Prerequisites: [SC30 / #32](https://github.com/glconti/synthcad/issues/32).

Authors currently construct polygon loops or repeat 3D hull helpers for common profiles. MANIFOLD_CROSS_SECTION is disabled even though the vendored library provides 2D Booleans and offsets.

- Enable and bind the vendored CrossSection capability after checking its dependency/build implications on Windows and Linux.
- Provide rectangles, rounded rectangles, circles, slots and polygon profiles, with 2D Boolean operations and inward/outward offsets; specify winding, holes and join behavior.
- Add explicitly oriented workplanes that map profile coordinates into model millimetres and integrate with existing extrude/revolve operations.
- Provide an initial line/arc path construction contract; record spline and vector-import follow-ups with tested scope rather than imply full sketch-constraint support.
- Report collapsed, empty, split and invalid/self-intersecting profile outcomes explicitly; preserve documented legacy polygon input behavior.

**Acceptance evidence:** Use holed and concave profiles, inward offset collapse/splitting, rounded enclosure walls, non-XY workplanes and equivalent legacy/new constructions; measure geometry and validate topology on both native platforms.

**Boundary:** No general sketch constraint solver or arbitrary 3D shelling. Coordinate the frame representation with SC32 and quality semantics with SC36.

### SC32 Add named reference frames and deterministic part attachments

P2 · [SC32 / #34](https://github.com/glconti/synthcad/issues/34) · Prerequisites: [SC30 / #32](https://github.com/glconti/synthcad/issues/32).

The shared design graph stores instance poses, but authors manually derive rotations and translations for mating parts. A stable part ID does not provide a meaningful mounting frame.

- Let source parts publish named frames with origin and orientation, and resolve frame references through physical instances.
- Define deterministic attachment between explicit frames, including offset and orientation conventions; compose local/world transforms and reject missing, ambiguous or cyclic dependencies.
- Integrate resolved attachments with the existing shared design graph so source geometry stays shared and plate/inspection placements stay independent of assembly poses.
- Expose resolved frames for programmatic measurement and review using existing revision semantics. Authored datum identity must remain distinct from revision-local picked topology.
- Document parameter-edit behavior, invalidation and compatibility; align workplane/frame conventions with SC31.

**Acceptance evidence:** Resize, rotate and mirror relevant source geometry under explicitly documented frame behavior; attach repeated instances; verify mating frame positions/orientations numerically and preserve independent plate placements. Exercise cycles and missing anchors.

**Boundary:** No assembly degrees-of-freedom solver and no promise that arbitrary selected mesh faces keep identity after Booleans. Human-pick transport is existing SC08/SC09 work.

### SC36 Define consistent tessellation quality and approximation contracts

P2 · [SC36 / #35](https://github.com/glconti/synthcad/issues/35) · Prerequisites: [SC30 / #32](https://github.com/glconti/synthcad/issues/32).

Quality controls vary between bindings and can be confused with kernel tolerance or physical clearance. Display, checks and exports must identify the actual evaluated geometry.

- Separate geometric approximation error, kernel numerical tolerance and physical fit allowance in the authoring contract.
- Provide consistent curve/primitive tessellation controls, including the current sphere/cylinder asymmetry, with maximum-deviation semantics where they can be guaranteed.
- Document distinct resolution/error behavior for level sets, smoothing and imported meshes; do not imply subdivision alone restores an analytic shape.
- Define explicit evaluation quality settings and record the effective settings/algorithm version in relevant evaluated identities and evidence so changed geometry invalidates checks and exports.
- Retain existing defaults for legacy scenes or supply an explicit versioned migration. A quality change must not silently alter the geometry approved for export.

**Acceptance evidence:** Measure circle/sphere approximation at several radii, compare preview/final geometry, verify quality-dependent clearances and revision invalidation, and regression-test legacy exports within their existing semantics.

**Boundary:** No printer compensation hidden in numerical settings, no automatic simplification of final exports, and no redesign of the session protocol. Coordinate identity integration with its owner.

### SC33 Add reusable holes, patterns and mechanical feature helpers

P2 · [SC33 / #36](https://github.com/glconti/synthcad/issues/36) · Prerequisites: [SC30 / #32](https://github.com/glconti/synthcad/issues/32), [SC31 / #33](https://github.com/glconti/synthcad/issues/33), [SC32 / #34](https://github.com/glconti/synthcad/issues/34), [SC36 / #35](https://github.com/glconti/synthcad/issues/35).

Each model currently implements its own through-cut extents, rounded shapes, mounting patterns and coordinate placement. Common fit terminology is easy to interpret inconsistently.

- Ship a documented initial library of through/blind holes, counterbores/countersinks, slots, rounded primitives, linear/circular patterns, bosses/standoffs and ribs/gussets.
- Return useful construction references such as hole axes, entry planes and mounting frames, using the shared frame contract.
- Define through-cut extent/direction against the target geometry so users need not guess cutter lengths; handle missed targets and invalid dimensions with actionable diagnostics.
- Keep nominal hardware dimensions, radial/diametral fit allowances and printer compensation distinct; version and cite any hardware tables rather than invent universal fit defaults.
- Track enclosure lips, nut/insert pockets, text/vector profiles, path patterns and specialized joints as scoped library extensions after validating the initial set.

**Acceptance evidence:** Create a public enclosure with mating lid, a rotated bracket and a multi-hole plate; vary dimensions, hole sizes and pattern counts. Validate cuts, connectivity, references and fit allowances numerically, without claiming physical fit.

**Boundary:** Helpers remain ordinary composable modeling code over supported kernel operations. General edge fillets and arbitrary shelling belong to SC38.

### SC34 Preserve named parameters, features and modeling provenance

P2 · [SC34 / #37](https://github.com/glconti/synthcad/issues/37) · Prerequisites: [SC30 / #32](https://github.com/glconti/synthcad/issues/32), [SC32 / #34](https://github.com/glconti/synthcad/issues/34).

An evaluated solid does not retain enough application-level context to identify the authored feature, parameters or construction references responsible for a hole or mounting surface.

- Introduce opt-in named parameter declarations with units, constraints and stable IDs while retaining ordinary JavaScript expressions and functions.
- Introduce named feature records linking input references, parameter values, construction frames, result geometry and source location.
- Expose an inspectable authored dependency/provenance graph without claiming to serialize or cache arbitrary JavaScript execution.
- Preserve source-part/instance/view separation and define feature/parameter identity, duplicate-ID failures and invalidation across edits.
- Distinguish authored feature identity from output topology. Split/merged/deleted output surfaces may be unresolved or ambiguous; never silently reuse an old face index.

**Acceptance evidence:** Change a mounting pattern and enclosure dimensions; trace affected feature inputs/results; diagnose invalid dependent features. Test duplicate IDs, feature removal and Boolean splits/merges with explicit unresolved references.

**Boundary:** No general persistent topological-naming guarantee, automatic reverse engineering of imported meshes or new source-of-truth database. Additive inspection uses existing review/session architecture.

### SC35 Add geometric queries and executable design checks

P2 · [SC35 / #38](https://github.com/glconti/synthcad/issues/38) · Prerequisites: [SC30 / #32](https://github.com/glconti/synthcad/issues/32), [SC32 / #34](https://github.com/glconti/synthcad/issues/34), [SC34 / #37](https://github.com/glconti/synthcad/issues/37).

Bounds, volume and minGap plus plate checks do not express all model-specific requirements such as mounting-axis spacing, mating orientation or local interference.

- Provide measurements between explicit points, axes, planes and solids; reuse existing distance/intersection/connectivity operations with documented mesh approximation limits.
- Add section/ray/nearest-point and geometric selection queries in a scoped, documented set; selectors return cardinality and report missing/ambiguous results explicitly.
- Allow source-authored executable checks evaluated against resulting geometry or resolved references, with expected ranges and numerical tolerances.
- Return measured values, affected IDs, locations and failure context using the existing passed/warning/failed/not-checked evidence conventions and appropriate model/profile revision basis.
- Integrate results with existing review/check presentation without treating asserted parameter values, authored labels or conservative bounds as independent geometric or physical proof.

**Acceptance evidence:** Verify hole-axis spacing, lid/electronics interference, connected bracket bodies, section geometry and ambiguous selections across parameter edits. Confirm stale results and approximations are labeled and failed checks provide reproducible evidence.

**Boundary:** Does not duplicate the implemented plate/manufacturing engine in SC13/SC14 or claim general wall-thickness/strength verification. Coordinate additive result exposure with the CLI/session owner.

### SC38 Evaluate sweeps, lofts, fillets and shelling against modeling tasks

P3 · [SC38 / #39](https://github.com/glconti/synthcad/issues/39) · Prerequisites: [SC31 / #33](https://github.com/glconti/synthcad/issues/33), [SC32 / #34](https://github.com/glconti/synthcad/issues/34), [SC36 / #35](https://github.com/glconti/synthcad/issues/35), [SC37 / #31](https://github.com/glconti/synthcad/issues/31).

The current API lacks general sweeps, lofts, selected-edge fillets/chamfers and solid shelling; some are beyond a thin binding to the vendored kernel.

- Select representative failed tasks from SC37 and compare narrow reliable helpers, mesh algorithms and a B-rep-backed approach for each required operation.
- Demonstrate sweep frame behavior at bends, loft profile correspondence/topology limits, and representative fillet/shell offset failures with explicit approximation/error semantics.
- Distinguish specified-radius fillets from smoothing and constant-thickness shells from subtracting a scaled copy.
- Record a decision covering geometric coverage, correctness, performance, dependency/licensing/distribution implications and interoperability requirements such as analytic STEP where relevant.
- Create focused implementation follow-ups only for justified capabilities. A kernel migration or second backend requires its own explicit decision; this issue does not authorize that migration.

**Acceptance evidence:** Use synthetic curved ducts/adapters and edge/shell stress cases, including self-intersection and collapsing offsets; publish reproducible fixtures and honest unsupported cases.

**Boundary:** General sketch/assembly constraint solving, broad surface modeling and kernel replacement remain research options, not promised scope.

### SC39 Assess bounded and cancellable model evaluation

P3 · [SC39 / #40](https://github.com/glconti/synthcad/issues/40) · Prerequisites: [SC37 / #31](https://github.com/glconti/synthcad/issues/31).

Synchronous JavaScript/native evaluation can block review. A bounded CLI wait is not cancellation of the computation, and a JavaScript interrupt cannot stop an arbitrary in-progress native call.

- Measure representative JavaScript, Boolean, level-set and mesh-processing workloads from the modeling corpus and identify where time/memory/progress can be observed.
- Coordinate with the current CLI/session workstream before designing changes to evaluator ownership, request lifecycle or revision publication.
- Compare cooperative native cancellation where supported with worker/process isolation and bounded evaluation, documenting graphics-thread ownership and native interruption limits.
- Specify cancellation/timeout/supersession/error outcomes, last-valid-result retention and export validity without weakening the existing consumed-source revision contract.
- Record a decision and scoped implementation follow-ups; permit only dependency-aware caching of explicitly pure feature nodes if later provenance supports it, never assume arbitrary JS is cacheable.

**Acceptance evidence:** Design experiments covering an infinite JavaScript loop, expensive native work, source edits during evaluation, cancellation and recovery. No partial result may acknowledge a completed revision.

**Boundary:** No CLI renaming, transport rewrite, session registry changes or implementation competing with the user's active session. This is a future evaluator study.

## Existing work and coordination

- SC12 / [#13](https://github.com/glconti/synthcad/issues/13) already delivers
  shared parts, instances and independent layouts. SC32/SC34 extend that model;
  they do not introduce a parallel assembly representation.
- SC07–SC09 / [#8](https://github.com/glconti/synthcad/issues/8),
  [#9](https://github.com/glconti/synthcad/issues/9) and
  [#10](https://github.com/glconti/synthcad/issues/10) cover picking and guided
  references. Named construction references must not weaken rejection of
  stale geometric selections or claim general face correspondence.
- SC13/SC14 / [#14](https://github.com/glconti/synthcad/issues/14) and
  [#15](https://github.com/glconti/synthcad/issues/15) already define plate
  checks and evidence scopes. SC35 adds executable model-specific requirements
  using those conventions.
- SC22 / [#23](https://github.com/glconti/synthcad/issues/23) owns v1 release
  acceptance. SC37 measures authoring and revision reliability beyond it, with
  reusable public fixtures rather than a second session acceptance harness.
- CLI naming/discovery, guide delivery, transport, registry and session
  lifecycle remain with their current workstream. Coordinate any additive
  inspection/diagnostic exposure through the existing application interface.
  SC39 is explicitly deferred until evaluator ownership is agreed.
- File edits remain authoritative. No embedded agent, geometry-editing CLI
  language, hidden printer defaults or new source-of-truth database is implied.

## Measuring improvement

Accept capabilities using independent geometric checks across parameter
changes, not only successful initial evaluation or screenshots. Track
requirement pass rate, repair attempts, evaluation cost and recorded agent
effort against the baseline. Parameter sweeps and deterministic regression
tests are separate from repeatable agent trials; record the model/runtime
configuration for the latter. Set numerical improvement targets after the
baseline exists. Geometric evidence remains distinct from sliced or physical
verification, and personal models remain outside public fixtures.
