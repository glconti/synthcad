# Design for printing

Run `synthcad-cli docs print-design` to print this guide. It frames design choices; it does not supply printer-validated settings.

## Gather project-specific constraints

Start with the part's job: what it supports, locates, flexes, seals, slides against or protects; the expected load direction and environment; which faces must mate or look clean; and how many physical copies are needed. Then gather the intended printer, usable bed area and exclusions, nozzle, material, process profile, and slicer version when known. Keep user-provided or measured limits distinct from manufacturer specifications and assumptions. Never carry a profile or process choice over from another project.

Unknown printer or material details do not block an initial geometry review. Record them as unknown or provisional and leave dependent print claims open. Ask targeted questions only where the answer changes a design decision, such as whether a clip is meant to flex repeatedly, whether a face is a locating datum, or whether the part sees heat or outdoor exposure.

## Compare each part's print options

For each source part that will be printed, compare the realistic orientations before choosing one. Keep a short record with:

- The part's function, load path, expected force direction, and any critical joint or mating face.
- Two or more feasible bed orientations, the face touching the bed, resulting height, and layer direction relative to the expected load.
- Which visible, sealing, sliding, or mating surfaces would touch the bed or supports, and how supports could be reached and removed.
- The likely support regions, small contact areas, brim need, and any risk of trapped support or a damaged finish.
- Whether a monolithic part fits the known printer and keeps its load path; if it is split, how the pieces align, transfer load, and can be assembled or repaired.
- The chosen orientation, unresolved assumptions, and the next slicer or physical check.

A useful per-part note can stay compact:

```text
Part / physical instances:
Function and expected load:
Critical mating or visible faces:
Orientation candidates and layer/load tradeoff:
Bed contact, support contact and removal access:
Monolithic vs split consequence:
Decision, uncertainty and next evidence:
```

FFF parts are direction-dependent: consider whether a load would be carried within deposited roads or across layer bonds. A short, low part is not automatically the stronger choice, and a large flat bed face is not automatically the best choice if it puts a critical face against the plate or turns the load across layers. Describe the tradeoff instead of claiming a strength score. Prusa's design guidance likewise warns that FFF strength varies by direction and that orientation affects surface quality. Its advice is process-specific and does not establish a strength value for this project. [Modeling with 3D printing in mind](https://help.prusa3d.com/article/modeling-with-3d-printing-in-mind_164135)

## Apply the choice

Explain the tradeoffs in concrete terms. Proceed with routine reversible
orientation and layout edits within the user's request. If an alternative
changes the intended appearance, physical part count or function, explain its
consequence and use the user's stated preferences.

Treat print-process settings as profile-specific. Do not present guessed layer heights, clearances, temperatures, wall counts or strength limits as validated settings. You can choose provisional, editable dimensions to move a design forward; label their basis and uncertainty. A recommendation should identify its assumptions and what the user must check in the target slicer.

Prefer reliable bed contact when it does not compromise function or surface quality. Keep support touch points off precision mating faces when possible, and make any required support reachable with the expected removal tools. Orient openings and cavities so supports do not become trapped. Relate thin walls, holes and locating pins to the selected nozzle and process; modeled geometry alone does not show whether a slicer will retain those features.

Compare a monolithic print with a split design when the one-piece orientation causes inaccessible support, an unfavorable layer/load relationship, a poor finish, or a footprint outside the usable bed. A split adds its own design work: registration, joint area and load path, fastener or adhesive access, assembly order, and a clearance chosen for the actual printer, material and process. Do not insert a universal fit allowance. Check support generation and removal in the target slicer; support needs depend on geometry and slicer settings. [Prusa support material guidance](https://help.prusa3d.com/article/support-material_1698)

## State the evidence honestly

`synthcad-cli checks --session NAME --json` reports engine calculations for placed-part bounds,
bed contact, overlap, specified allowances and plate quantities. Read the
method and scope: a deterministic bounds calculation is different from a
conservative heuristic warning. Authored overview records remain authored
claims. Neither a passing model check nor a successful reload establishes a
successful slice.

The viewer does not derive actual support demand, toolpaths, print time,
material use, mechanical strength or process-specific minimum feature survival.
For a thin wall, hole or pin, inspect whether the target slicer retains a
continuous feature and whether its orientation supports the intended load and
assembly. Keep a support-removal path open; splitting a cavity can improve
access but introduces an alignment and joint design decision. Check those
consequences on the complete assembly.

Use the slicer's preview to inspect the actual orientation, supports and toolpaths for the chosen printer and profile. Physical fit, load and durability claims require measurements or tests on printed parts. Such samples are optional to ordinary modeling and export; when evidence is absent, name the open question instead of describing the part as verified.

When one uncertain interface or load path matters to the design, a small test coupon or first article can answer that question before committing to the full part. Record the machine, nozzle, material and profile, orientation, measured fit or load result, and test conditions. A coupon is evidence only for the feature and conditions it represents; it does not prove the full assembly or a different process.
