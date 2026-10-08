# Bambu Studio handoff

Run `synthcad docs bambu-handoff` to print this guide.

## Current handoff: STL, then review and slice in Bambu Studio

The current SynthCAD GUI exports STL. The `synthcad` review CLI reports `export: false`; it does not create an STL or Bambu Studio project. To export, use the viewer's **Export STL** dialog on the current scene. Choose all exportable parts or visible exportable parts deliberately: the all-parts mode includes hidden exportable parts. External references are excluded by default unless their export flag is enabled.

STL export keeps the selected solids in the scene's current coordinates. It does not translate an assembly onto a build plate, union overlapping bodies, or include colors and dimension annotations. Open the resulting STL in Bambu Studio, choose the intended machine and profile there, then inspect its orientation, supports and sliced preview before printing. An assembly-coordinate export and a hand-authored plate-view export have different placement by design.

The viewer asks before replacing an existing destination. Confirm the scene, included parts and coordinates in the dialog before saving. Use a named plate view when the STL should contain the manually authored print arrangement.

## What a handoff does not establish

An STL is geometry, not a configured slicer project. SynthCAD does not currently generate a Bambu Studio project 3MF, assign Bambu profiles, slice the model or verify resulting toolpaths. Do not report 3MF handoff, a successful slice, print-time estimates or material-use estimates as SynthCAD results. Record slicer checks separately from model checks and physical results.
