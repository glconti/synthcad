# Fit and assembly

Run `synthcad docs fit-and-assembly` to print this guide.

## Model the mating conditions

Start from measurements of the real mating parts and identify which surfaces locate, retain or carry load. Mark uncertain dimensions and intended clearances as parameters, with their values and provenance recorded in the project. A fit depends on the printer, material, orientation and process; do not use a clearance copied from a different project as a guaranteed setting.

Keep mating faces and assembly coordinates consistent across the component models. Check the intended insertion path, tool access and order of assembly. A visual overlap can reveal a concern, but it does not prove that parts can be assembled or will remain secure under load.

For an uncertain interface, a small fit coupon or first article can resolve the uncertainty with less material than a full assembly. It is optional evidence: do not make a sample test a prerequisite for initial review or ordinary export. Record the printer, material, orientation and measured result if a physical test is performed.

## Explain design tradeoffs

Proceed with routine reversible choices, such as an editable provisional
clearance parameter, within the user's requested design. Explain changes to
the intended appearance, physical part count or function and use the user's
stated preferences. Do not imply that pins, clips, adhesives, fasteners or
printed joints have verified strength without evidence for that design and
process.

Choose the joint around its job. Locating pins establish repeatable alignment;
adhesive faces need accessible contact and a way to hold the parts while the
joint forms. Fasteners need tool access and a feasible tightening sequence.
A clip needs an insertion path and a way to flex and release without blocking
nearby features. Check that later parts do not trap an earlier fastener or
make required support material impossible to remove. Keep alignment and
load-carrying functions explicit instead of assuming a small pin performs both.
For snap joints, compare the intended motion and material with the process
limitations in the manufacturer's [snap-fit design guidance](https://formlabs.com/blog/designing-3d-printed-snap-fit-enclosures/).
Its machine/material-specific examples do not establish a universal clearance
or guarantee this project's joint strength.

## Report what was checked

The viewer displays authored dimensions and computes manufacturing review
against placed solids and supplied profile metadata. `synthcad checks --json`
can identify overlap, bounds and layout concerns; each result states its
method, evidence and scope. These checks do not establish a workable insertion
path, printed fit, retention force or load capacity. A geometric overlap may
be intentional contact in an assembly and a placement fault on a print plate.
Interpret it in the selected view's role.

Distinguish deterministic geometry checks, conservative heuristics, an actual
slicer preview and a measured physical assembly. Numeric bed-contact tolerance
is unrelated to the clearance needed by a mechanical joint. When a question
remains untested, name it and suggest the specific measurement, assembly
sequence review or optional coupon that would resolve it.
