# Fit and assembly

Run `synthcad docs fit-and-assembly` to print this guide.

## Model the mating conditions

Start from measurements of the real mating parts and identify which surfaces locate, retain or carry load. Mark uncertain dimensions and intended clearances as parameters, with their values and provenance recorded in the project. A fit depends on the printer, material, orientation and process; do not use a clearance copied from a different project as a guaranteed setting.

Keep mating faces and assembly coordinates consistent across the component models. Check the intended insertion path, tool access and order of assembly. A visual overlap can reveal a concern, but it does not prove that parts can be assembled or will remain secure under load.

For an uncertain interface, a small fit coupon or first article can resolve the uncertainty with less material than a full assembly. It is optional evidence: do not make a sample test a prerequisite for initial review or ordinary export. Record the printer, material, orientation and measured result if a physical test is performed.

## Explain design tradeoffs

Separate reversible choices, such as a provisional clearance parameter, from changes that affect the requested result. Before changing the intended appearance, number of parts or function to improve assembly, explain the consequence and get the user's choice. Do not imply that pins, clips, adhesives, fasteners or printed joints have verified strength without evidence for that design and process.

## Report what was checked

The current viewer can display authored parts, dimensions and bounds. It does not automatically calculate a fit, collision, insertion path, retention force or load capacity. Distinguish a geometry review from a slicer preview and from a measured physical assembly. If any of those remain untested, say so and identify the specific uncertainty.
