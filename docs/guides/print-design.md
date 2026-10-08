# Design for printing

Run `synthcad docs print-design` to print this guide. It frames design choices; it does not supply printer-validated settings.

## Gather project-specific constraints

Use the intended function, appearance, available printer, usable bed area, nozzle, material and slicer profile when the user provides them. Keep measured machine limits separate from assumptions. Never carry a profile or process choice over from another project. Unknown printer or material details do not block an initial geometry review; leave dependent claims provisional until the target process is known.

## Compare orientations by consequence

For each candidate orientation, consider which faces touch the bed, how layer
direction relates to expected loads, where supports would be needed and
removed, which surfaces must look clean, and whether the part can be handled
and assembled afterward. Explain tradeoffs in concrete terms. Proceed with
routine reversible orientation and layout edits within the user's request.
If an alternative changes the intended appearance, physical part count or
function, explain its consequence and use the user's stated preferences.

Treat print-process settings as profile-specific. Do not present guessed layer heights, clearances, temperatures, wall counts or strength limits as validated settings. You can choose provisional, editable dimensions to move a design forward; label their basis and uncertainty. A recommendation should identify its assumptions and what the user must check in the target slicer.

Prefer broad, stable bed contact where it does not compromise the design. Keep
support contact away from precision mating surfaces when an alternative
orientation permits it. Make cavities and support regions accessible for
removal; a closed cavity with required internal supports is a design concern.
Relate thin walls, small holes and locating pins to the supplied nozzle and
process rather than assuming every modeled feature will survive slicing.
Consider the load path across layer boundaries when orienting hooks, clips or
long arms. Splitting a part can reduce supports but introduces alignment,
assembly and joint-strength requirements; compare the complete assembled design.
These review choices are consistent with Prusa's guidance on
[model orientation, minimum features and splitting parts](https://help.prusa3d.com/article/modeling-with-3d-printing-in-mind_164135)
and [support access/removal](https://help.prusa3d.com/article/support-material_1698).
Use that guidance for its stated process; it does not validate this project's
machine, material or chosen dimensions.

## State the evidence honestly

`synthcad checks --json` reports engine calculations for placed-part bounds,
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
