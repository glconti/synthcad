# Work with a human in SynthCAD

Use SynthCAD to create and refine parametric CAD models with a human, review
them together in a persistent local viewer, and export STL or standard 3MF
geometry. You are the authoring agent: edit JavaScript and project files with
your ordinary file tools. The CLI connects your work to the shared viewer;
it does not edit geometry for you.

## Establish intent and shared context

The human supplies the object’s purpose, measurements, constraints, preferences,
and feedback. Inspect existing files before changing them. Ask for missing
information when it affects function or an irreversible design decision; keep
routine editable assumptions explicit. Explain tradeoffs that change appearance,
physical part count, or function. Preserve the human’s names and language.

The human reviews the model visually and can select geometry. Use a focused
selection request when you need them to identify a part, surface, edge, or
vertex; wait for their explicit confirmation. Do not treat an ordinary click
or your own highlight as the human’s answer.

## Start small and read the relevant contract

Work in the user’s chosen project. For a new simple design, start with
`design.js` for shared geometry and `synthcad.json` for named views and context.
Existing standalone `.js` scenes are supported. Read `synthcad-cli model --help`
and `synthcad-cli model api --help` before authoring. Read
`synthcad-cli project files --help` for the manifest contract and
`synthcad-cli model assembly --help` for shared parts, instances, and layouts.

Keep dimensions parametric, external context non-exportable, and assembly and
print placements separate. Record measured, derived, provisional, and unknown
values honestly. Do not inherit printer or tolerance assumptions from another
project. In a SynthCAD checkout, personal designs belong in ignored
`local-scenes/`.

## Author, evaluate, inspect, and iterate

Edit source files, then open the project once. Further edits hot-reload into the
same viewer. These commands illustrate the review loop:

```text
synthcad-cli project open ./my-design --name bracket --json
synthcad-cli project revision --project bracket --json
synthcad-cli project wait --project bracket --revision REQUESTED_TOKEN --json
synthcad-cli project inspect --project bracket --expect-revision DISPLAYED_TOKEN --json
```

Read the `project` handle returned by open and use it with `--project`. Opening
successfully does not mean the model has finished loading. After each edit,
capture `data.revision` from `project revision` and substitute it for
`REQUESTED_TOKEN`. If startup reports `busy`, inspect load state and retry.
Proceed only after `project wait` succeeds. Substitute its envelope `revision`
or `data.displayedRevision` for `DISPLAYED_TOKEN` in guarded review actions.
Requested and displayed revisions are distinct identifiers.

Inspect geometry, names, dimensions, bounds, diagnostics, and project context.
Use highlighting, framing, and screenshots to discuss the design. Use
`synthcad-cli review pick --help` when the next edit depends on a human geometry
choice. Revise the source and repeat the revision loop after each change.

A failed reload can leave the old valid model visible. Read `loadFailure` in
`project inspect`, fix the source, and wait for a fresh revision. A timeout does
not cancel evaluation or prove the viewer closed. Check `project list` and reuse
the same project; do not change `SYNTHCAD_SESSION_DIR` or create `.sessions/`
as an access-error workaround.

## Prepare a deliverable and explain its evidence

For printing, choose orientation deliberately, author a separate plate view,
and inspect `print checks`. Run `print export PATH --dry-run` before publication;
review warnings and use the displayed revision to guard the final export.
Read `synthcad-cli print --help` for the complete preparation workflow.

A rendered model establishes that geometry evaluated and displayed. Computed
checks establish only their stated geometric or heuristic conclusions. Slicer
preview and toolpaths supply different evidence; physical fit and strength need
physical evidence. Never turn a plausible screenshot or successful export into
a claim that the object fits, slices, or performs correctly. Samples are optional
when uncertainty warrants them, not a prerequisite for ordinary export.

Put requested final files in `exports/`, preferring 3MF and adding STL when
requested. Read routine JSON responses in memory. Use temporary screenshots for
review; retain extra reports or previews only when requested. Export receipts
are already maintained by the application under `.synthcad/`.

## Discover only the instructions needed next

- `synthcad-cli project --help`: establish files, open a project, inspect and recover evaluation.
- `synthcad-cli model --help`: author parametric geometry and shared assemblies.
- `synthcad-cli review --help`: inspect and discuss displayed geometry with the human.
- `synthcad-cli print --help`: prepare placements, review evidence, export, and hand off.

Each level teaches its own task and names narrower help pages. Add `--json` for
structured instructions, arguments, examples, and immediate children. Guidance
is bundled in the executable: no checkout, network, skill installation, or
running viewer is required. Use `synthcad-cli --version` to identify the build.
