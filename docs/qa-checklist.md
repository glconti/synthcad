# SynthCAD Release QA checklist

Use a fresh project folder and the standalone `synthcad-cli.exe`. The executable
contains its CLI guides and viewer; no repository checkout or skill files are
needed to begin. The agent edits JavaScript files, while CLI calls communicate
with the persistent viewer. This is a Release QA snapshot, not a completed v1
release or a slicer.

For the current Windows QA snapshot, the clean destination is
`D:\SynthCAD-QA\synthcad-cli.exe`. Run `./synthcad-cli.exe --help` for discovery,
`./synthcad-cli.exe open . --session qa` after creating a project, or pass a `.js`
path directly to open a legacy scene. No-argument execution prints CLI help.
The existing development viewer's no-argument sample behavior is unchanged.

To reproduce the single-file build from a configured MSVC developer shell:

```powershell
cmake --preset windows-x64-portable-release
cmake --build --preset windows-x64-portable-release
```

Copy `out/build/windows-x64-portable-release/viewer/synthcad-cli.exe` as
`synthcad-cli.exe`. The portable preset links third-party libraries and the C/C++
runtime statically; Windows system libraries and graphics drivers remain OS
dependencies. Its viewer-hosting CLI launches this same executable, including
after renaming or moving it.

## Agent discovery and first model

- Run `./synthcad-cli.exe`, `--help`, `capabilities --json` and `docs` from outside
  the repository. Help should group commands and guides by area.
- Read `docs start`, `docs modeling`, `docs api`, `docs design` and
  `docs print-design` through stdout. No installed skill should be required.
- Ask the agent to create a small parametric two-part design in the fresh
  folder. Give parts stable IDs, meaningful Italian names and shared parameters.
- Open the project with `open . --session qa --json`. Repeated opens and CLI
  calls should reuse that viewer. Closing a CLI call should leave it running.

## Loading, review and human input

- Edit an imported geometry file. Use `revision` and `wait` to confirm the new
  model actually loaded. Then use the displayed revision for guarded commands.
- Introduce a syntax error: the last valid geometry should remain, the error
  should be visible, and export should be disabled. Correct it and verify recovery.
- Inspect the parts tree: search, expand/collapse, select, independent visibility
  and export icons, mixed group states, isolate/exit, show all and frame.
- Verify English UI alongside unchanged authored Italian names and annotations.
  Panel input/scrolling should not move the camera. Try a smaller window and
  your normal Windows display scaling.
- Test geometric part/surface/edge/vertex selection and Copy ref. Let the agent
  read `selection` or resolve `reference`. References must reject stale geometry.
- Ask the agent to use `highlight`, `frame`, `view` and `screenshot` in the same
  session. Agent highlights should preserve human selection and export flags.
- Try an agent `pick` question: select the requested feature, confirm or cancel,
  then read `pick-status` and `events`.

## Shared assembly and printing views

- Define source geometry once, reuse physical instances through groups, and make
  assembly, inspection and plate views. An alias must not duplicate an export;
  separate physical instance IDs should represent separate copies.
- Edit a shared dimension and verify assembly and plate views both update. Plate
  poses should remain independent of assembly poses.
- Supply only your build volume first. Review the bed, quantities, rotations,
  height/contact, spacing, exclusions and brim/support allowance checks.
- Try a long narrow part rotated diagonally. Fit depends on the complete rotated
  footprint and margins, not just its length versus the bed diagonal.
- Try a deliberately out-of-bounds or overlapping placement and inspect `checks`.
  Unknown manufacturing inputs should remain unknown. Placement/orientation are
  authored by the agent; automatic packing and slicer support generation are
  not implemented.
- Inspect `overview` and the GUI project overview for measurements, provisional
  assumptions, profile context, scoped checks and revision freshness.

## Export and optional iteration

- Export STL and standard Core 3MF from both CLI and GUI. Compare all exportable
  versus visible exportable modes; external references should initially be
  excluded. Export preserves the current arrangement.
- Verify dry-run, empty selection, cancellation, warnings, overwrite confirmation
  and stale-revision guards. Inspect `export-history` after success.
- Open the 3MF in Bambu Studio and check geometry, object quantities and placement.
  Select process/printer settings in the slicer; native Bambu presets, automatic
  slicing and printer-profile catalogs are deferred.
- Optionally add a shared sample view and explicit user-reported observation.
  Exporting should not mark it printed/tested. Geometry changes should mark old
  evidence/history stale while retaining prior exports and authored stages.
- Record affected parts and required reprints separately in compatibility notes.
  Physical strength or fit is established through actual user tests, not digital
  checks or metadata.

## Report findings

For each finding, include the command or UI steps, expected/actual behavior,
project files, displayed revision and a screenshot if useful. Keep this review
focused on current behavior; approve a separate batch before further features.
