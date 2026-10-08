# Design for printing

Run `synthcad docs print-design` to print this guide. It frames design choices; it does not supply printer-validated settings.

## Gather project-specific constraints

Use the intended function, appearance, available printer, usable bed area, nozzle, material and slicer profile when the user provides them. Keep measured machine limits separate from assumptions. Never carry a profile or process choice over from another project. Unknown printer or material details do not block an initial geometry review; leave dependent claims provisional until the target process is known.

## Compare orientations by consequence

For each candidate orientation, consider which faces touch the bed, how layer direction relates to expected loads, where supports would be needed and removed, which surfaces must look clean, and whether the part can be handled and assembled afterward. Explain tradeoffs in concrete terms. If changing orientation or splitting a part would alter the requested appearance, part count or function, get the user's choice first.

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

## State the evidence honestly

SynthCAD can display the authored model and its bounds. An authored check can report a geometric calculation, but the viewer does not automatically calculate wall thickness, support demand, bed compatibility, toolpaths, print time or material use. A successful model reload is not a successful slice.

Use the slicer's preview to inspect the actual orientation, supports and toolpaths for the chosen printer and profile. Physical fit, load and durability claims require measurements or tests on printed parts. Such samples are optional to ordinary modeling and export; when evidence is absent, name the open question instead of describing the part as verified.
