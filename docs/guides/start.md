# Start a SynthCAD design

Run `synthcad-cli --help` to list topics or `synthcad-cli project --help` to print this guide.

## Establish the project

Work in the project directory chosen by the user. For a simple design, start with two authored files: `design.js` for shared geometry and `synthcad.json` for named views and project context. Put requested final outputs under `exports/`. When working inside the SynthCAD repository, personal projects belong in the ignored `local-scenes/` directory. Existing standalone `.js` scenes are also supported.

For example, both views in `synthcad.json` can use the same JavaScript entry point:

```json
{
  "schemaVersion": 1,
  "name": "Small plate",
  "defaultView": "assembly",
  "views": {
    "assembly": "design.js",
    "plate-1": "design.js"
  }
}
```

Keep these named views in the shared design graph below. Split source files only when the model's complexity benefits from it. Run `synthcad-cli project files --help` for manifest and revision details and `synthcad-cli model api --help` for the complete model contract.

Read routine CLI responses directly or parse their JSON in memory. Do not save separate check reports, dry-run reviews or export-response dumps by default: the app already keeps export history in `.synthcad/`. Prefer a final 3MF under `exports/`; add STL when requested. Use OS temporary files for review screenshots and retain previews or print-note documents only when requested.

## Begin with a small parametric scene

This one-piece example uses supported geometry and metadata. Its dimensions are an illustration, not a printer recommendation.

```javascript
const width = 80;
const depth = 30;
const thickness = 4;

const body = cube({
  size: [width, depth, thickness],
  center: false,
});

export const design = {
  schemaVersion: 1,
  defaultView: 'assembly',
  parts: [{id: 'body', name: 'Body', solid: body,
    color: '#628bb5', exportable: true}],
  instances: [{id: 'body-1', part: 'body'}],
  views: [
    {id: 'assembly', kind: 'assembly', members: [{instance: 'body-1'}]},
    {id: 'plate-1', kind: 'plate', members: [{instance: 'body-1'}]},
  ],
};

export const dimensions = [{
  type: 'linear',
  label: 'Width',
  value: width,
  start: [0, -6, 0],
  end: [width, -6, 0],
}];
```

Dimensions are authored annotations: the viewer does not infer them from the mesh, and they do not certify a measurement or a fit. Keep their values and anchors tied to the same parameters as the geometry.

## Open and review edits

Open the standalone entry or project directory:

```text
synthcad-cli project open ./my-design
synthcad-cli project inspect --json
```

`project open` returns the project handle and whether an existing viewer was reused. Use that handle with `--project` for subsequent calls; edits hot-reload without another launch. If startup is slow, check `project list` and retry the same project. If access is denied, use the required execution permissions with the same registry. Do not switch `SYNTHCAD_SESSION_DIR` or create a project-local session directory as a recovery workaround.

After editing a source file, request the current revision, then pass the requested token from its JSON result to `project wait`:

```text
synthcad-cli project revision --json
synthcad-cli project wait --revision REQUESTED_TOKEN --timeout 10000 --json
```

Replace `REQUESTED_TOKEN` with the revision returned by `project revision`. Proceed only when `project wait` succeeds. Its envelope `revision` / `data.displayedRevision` identifies the geometry actually displayed. Use that displayed revision with `--expect-revision` for later snapshot, highlight, frame or screenshot calls. If the files change again, capture and wait for a fresh revision. Run `synthcad-cli review --help` for full command behavior and failure states.

Inspect the part tree, authored annotations, bounds, diagnostic and selected view. A screenshot is useful for shape and placement review, but it does not test clearances, wall thickness, bed fit, supports, toolpaths or strength. Record printer, nozzle and material details only when supplied for this project; unknown details can remain open during initial modeling.

If the user wants a targeted sample before a full print, run `synthcad-cli print feedback --help` for shared sample geometry, explicit user reports and revision-linked reprint decisions. This flow is optional; exporting a file never implies it was printed or tested.


If a model fails, read `project inspect --json`: `loadFailure` explains the processing stage
and available cause. The viewer retains its last successful model and disables
export. Fix the source and wait for its new revision, or use `project reload` to retry.
Use `project cancel-load` for stuck work; `project reload --evaluation-timeout 240000` raises the
open project's two-minute default limit. A CLI wait timeout alone does not cancel it.
