# Batch 6: project overview and printer context

SC10 and SC11 connect the project's authored context to the live viewer and CLI.
Click **Project** beside the scene filename to inspect named views, measurements,
assumptions, printer context, checks, exports and optional physical evidence.
View buttons load the selected assembly, inspection or plate entry in the same
window. Closing the card returns to the parts tree; opening and scrolling it do
not alter camera, human selection or export flags.

```text
synthcad open viewer/tests/agent-fixtures/shared-design --session brackets
synthcad overview -s brackets --json
synthcad profile -s brackets --json
synthcad profile --template
synthcad docs overview
synthcad docs profiles
```

The template prints an incomplete manifest fragment to stdout, even without a
viewer or source checkout. The thirteen bundled guide topics include the complete
record/profile schema and a short printer/nozzle/material setup conversation.
Agents edit project files normally; these commands do not write project files.

## Model and metadata identity

The existing consumed-source/displayed revisions and reload guards remain intact.
A separate `modelRevision` binds consumed model files, the view and design layout,
excluding manifest bytes used only for routing and metadata. Adding a check to
the manifest therefore does not make its own model basis stale. Model code that
actually consumes the manifest still tracks those bytes as model input.

An authored record can bind a model revision and optionally a printer profile
revision. The overview labels records current, stale, unknown or unbound.
Source/profile changes retain old records with updated freshness. Views that have
not been loaded have unknown model revisions; the viewer does not evaluate a
second copy of their geometry. Repeated views continue to reference SC12 sources
and physical instances.

Optional malformed records produce scoped errors while valid siblings and
geometry remain available. A malformed required manifest retains the last valid
model, labels metadata as last loaded, and disables export until recovery.
Authored results are labeled authored: a passed entry is not an engine result,
and proposed evidence is never promoted to printed or tested.

## Printer setup

Profiles are local to one project and explicitly selected by ID. Context includes
machine/material identity, measured build volume, rectangular bed exclusions,
nozzle diameter, provenance and provisional fields. Unknown values can be omitted
or null. Missing or invalid setup does not prevent geometry review; readiness
indicates which future printing checks lack inputs. An omitted exclusion list
means unknown obstacles, while an empty list explicitly declares none.

Changing profile data changes its revision. No preset is silently inherited from
another project, and a custom geometric profile is never labeled a verified
slicer preset. Deterministic plate checks, generated export provenance, 3MF
mapping and physical test performance remain their separate backlog stories.

## Verification

Windows MSVC Release and Ubuntu 24.04/GCC Release run all 21 C++ suites. New
suites cover printer validation/readiness/revision identity, authored records and
freshness, and overview layout/wrapping/input. Existing CLI and bridge suites
cover overview/profile responses and template parsing; standalone guidance checks
copy only the executable to a Unicode temporary directory and verify all thirteen
topics and the incomplete JSON template without writes or a running viewer.

`scripts/test-project-overview.py` copies a public shared-design fixture to a
temporary Unicode path. It verifies missing and custom profiles, provisional
fields, metadata-only model identity, source/profile staleness, malformed sibling
isolation, unvisited-view uncertainty, project isolation, required manifest
failure/recovery, and native open/close/scroll/view switching. Screenshots show
empty setup, authored metadata, profile context and errors. The fixture also
checks that physical evidence remains proposed and that camera/export state
does not change from overview interactions.

Native Windows acceptance ran at 125% display scaling. Linux acceptance ran at
100% and actual 200% (2560 × 1440, reported UI scale 2.0), including scrolling,
named-view switching and source/profile error recovery. A focused regression
also verifies that a known profile mismatch marks dependent records stale even
when their view has not yet been loaded.

Existing session, shared-design, geometric-selection and guided-pick workflows
remain regression gates, along with Windows icon/reload smoke. Temporary scenes,
screenshots and personal exports are excluded from product commits; the nine
released V5 STL hashes are checked unchanged.

This evidence does not close SC08's normal-desktop Windows clipboard check or
SC19's distribution/display matrix and CI work. Latin/Italian authored text is
preserved; glyph coverage beyond the installed viewer font is not established
by JSON Unicode round trips. No printer or physical object was tested here.
