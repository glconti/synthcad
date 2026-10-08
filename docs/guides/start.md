# Start a SynthCAD design

Run `synthcad docs` to list topics or `synthcad docs start` to print this guide.

## Establish the project

Work in the project directory chosen by the user. When working inside the SynthCAD repository, personal scenes and generated review or export files belong in the ignored `local-scenes/` directory. A standalone `.js` scene can be opened directly. Use a project manifest when the design needs named assembly, inspection or print-layout views.

For example, `synthcad.json` can map each view to its own JavaScript entry point:

```json
{
  "schemaVersion": 1,
  "name": "Small plate",
  "defaultView": "assembly",
  "views": {
    "assembly": "assembly.js",
    "plate-1": "plate-1.js"
  }
}
```

Each view can use shared source files for dimensions and part geometry. Run `synthcad docs projects` for manifest and revision details and `synthcad docs api` for the complete model contract.

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

export const scene = body;
export const displayParts = [{
  id: 'body',
  name: 'Body',
  group: ['Designed object'],
  solid: body,
  color: '#628bb5',
  exportable: true,
}];

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
synthcad open ./my-design
synthcad snapshot --json
```

After editing a source file, request the current revision, then pass the requested token from its JSON result to `wait`:

```text
synthcad revision --json
synthcad wait --revision REQUESTED_TOKEN --timeout 10000 --json
```

Replace `REQUESTED_TOKEN` with the revision returned by `revision`. Proceed only when `wait` succeeds. Its envelope `revision` / `data.displayedRevision` identifies the geometry actually displayed. Use that displayed revision with `--expect-revision` for later snapshot, highlight, frame or screenshot calls. If the files change again, capture and wait for a fresh revision. Run `synthcad docs cli` for full command behavior and failure states.

Inspect the part tree, authored annotations, bounds, diagnostic and selected view. A screenshot is useful for shape and placement review, but it does not test clearances, wall thickness, bed fit, supports, toolpaths or strength. Record printer, nozzle and material details only when supplied for this project; unknown details can remain open during initial modeling.

If the user wants a targeted sample before a full print, run `synthcad docs physical-feedback` for shared sample geometry, explicit user reports and revision-linked reprint decisions. This flow is optional; exporting a file never implies it was printed or tested.
