# Prepare geometry for printing

Use this workflow when the human wants a printable layout or an exported model.
Start from the shared source geometry, intended function, and supplied constraints.
Unknown printer metadata does not prevent initial modeling or geometry export.

1. Read `print design --help` for orientation and process decisions; use
   `print fit --help` for mating surfaces, insertion paths, and tolerances.
2. Inspect `print profile --json`. If context is missing, use
   `print profile --template`, review the fragment, and edit the manifest.
   Read `print profile --help` for the contract. Do not invent bed exclusions,
   nozzle, material, or verified slicer presets.
3. Read `print plates --help` and author a separate plate view of the shared
   design. Placement is a source edit; automatic packing is not implemented.
4. Select the plate with `review view NAME`, capture and wait for its revision,
   and read `print checks`. Interpret currentness, scope, method, and evidence.
5. Validate the export, acknowledge only reviewed warnings, and publish using
   the displayed revision guard:

```text
synthcad-cli print export ./exports/plate.3mf --project bracket --dry-run --json
synthcad-cli print export ./exports/plate.3mf --project bracket --expect-revision DISPLAYED_TOKEN --json
synthcad-cli print history --project bracket --json
```

Use the displayed revision returned by the successful review for DISPLAYED_TOKEN.
Read `print export --help` before acknowledging warnings or replacing a file.
Export preserves current placement: an assembly view remains an assembly.

Read `print handoff --help` to review geometry in the slicer. Bounds and overlap
checks do not generate supports or toolpaths, and a successful export is not proof
of a successful print. If an export times out near publication, inspect its
destination and history before retrying.

Read `print feedback --help` when an optional coupon, first article, or physical
report would resolve uncertainty. Record what was actually tested and its
revision; do not require sample records for an ordinary export.
