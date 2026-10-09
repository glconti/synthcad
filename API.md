# DingCAD JavaScript API Cheat Sheet

- cube{options: {size:[x,y,z], center:bool}}
- sphere{options: {radius:number}}
- cylinder{options: {height:number, radius:number, radiusTop:number, center:bool}}
- tetrahedron{}
- compose{...manifolds | manifolds[]}
- decompose{manifold}
- union{...manifolds}
- difference{...manifolds}
- intersection{...manifolds}
- boolean{a,b,op:"add"|"subtract"|"intersect"}
- batchBoolean{op:"add"|"subtract"|"intersect", manifolds[] | ...manifolds}
- translate{manifold,[dx,dy,dz]}
- scale{manifold, factor|[sx,sy,sz]}
- rotate{manifold,[rx,ry,rz]}
- mirror{manifold,[nx,ny,nz]}
- transform{manifold,[m00,m01,m02,m03,...,m22,m23]}
- trimByPlane{manifold,[nx,ny,nz],offset}
- hull{...manifolds | manifolds[]}
- hullPoints{[[x,y,z],...]}
- compose polygons as [[x,y],...] loops grouped like [loop0, loop1,...]
- extrude{polygons, options:{height:number, divisions?:int, twistDegrees?:number, scaleTop?:number|[sx,sy]}}
- revolve{polygons, options?:{segments?:int, degrees?:number}}
- slice{manifold, height?:number}
- project{manifold}
- levelSet{options:{sdf(point:[x,y,z])=>number, bounds:{min:[x,y,z], max:[x,y,z]}, edgeLength:number, level?:number, tolerance?:number}}

  The SDF must return a finite number. Bounds must be finite and strictly ordered;
  `edgeLength` must be positive and no larger than the smallest bounds extent.
  Requests above 16 million estimated sampling cells are rejected; increase
  `edgeLength` or reduce bounds. JavaScript callbacks run sequentially.
  Callback exceptions preserve their message and stack in the load diagnostic.

- loadMesh{path:string, forceCleanup?:bool}

  The viewer tracks the primary mesh file for reloads, revision guards, and export
  receipts, including missing files so restoration can recover a failed load.
  It compares binary bytes before and after import and rejects detected changes;
  the importer opens the file independently, so this is not an atomic guarantee
  of every byte consumed. Auxiliary files opened internally by an import library
  (such as material files referenced by OBJ) are not tracked.

- setTolerance{manifold, tolerance}
- getTolerance{manifold}
- simplify{manifold, tolerance?}
- refine{manifold, iterations}
- refineToLength{manifold, length}
- refineToTolerance{manifold, tolerance}
- smoothByNormals{manifold, normalIdx}
- smoothOut{manifold, minSharpAngle?, minSmoothness?}
- calculateNormals{manifold, normalIdx, minSharpAngle?}
- calculateCurvature{manifold, gaussianIdx, meanIdx}
- asOriginal{manifold}
- originalId{manifold}
- reserveIds{count}
- surfaceArea{manifold}
- volume{manifold}
- boundingBox{manifold} // returns {min:[x,y,z], max:[x,y,z]}
- minGap{manifoldA, manifoldB, searchLength}
- isEmpty{manifold}
- status{manifold}
- numTriangles{manifold}
- numVertices{manifold}
- numEdges{manifold}
- numProperties{manifold}
- numPropertyVertices{manifold}
- genus{manifold}
- decompose polygons back to JS with slice/project return [[x,y],...] loops

Export your final solid as `scene` to render, e.g. `export const scene = cube({...});`.
Alternatively export the shared `design` graph below.

## Shared source parts and instances

Export `design` for assemblies, groups and multiple layouts that reference the
same source parts. Do not also export `scene` or `displayParts`. A version-1
design has `defaultView`, `parts`, `instances`, optional `groups`, and `views`.

```javascript
const body = cube({size:[20,10,3]});
export const design = {
  schemaVersion: 1, defaultView: 'assembly',
  parts: [{id:'body', solid:body, quantity:2}],
  instances: [{id:'left', part:'body'},
              {id:'right', part:'body', transform:{translate:[30,0,0]}}],
  groups: [{id:'pair', members:[{instance:'left'}, {instance:'right'}]}],
  views: [
    {id:'assembly', kind:'assembly', members:[{group:'pair'}]},
    {id:'plate', kind:'plate', members:[{group:'pair'}],
     placements:{right:{translate:[25,0,0]}}},
  ],
};
```

Each source solid is defined once; physical copies have distinct instance IDs.
Groups contain typed references to instances or other groups. Repeated membership
creates tree aliases, never extra export copies. Names, colors and exportability
inherit from the source and may be overridden per instance. Layout transforms
rotate XYZ in degrees then translate in mm; a view override replaces the entire
instance pose. They do not scale the source or move another view. Quantity is
declared intent, not an instruction to generate copies or a plate check.

Run `synthcad-cli docs design` for the complete schema, limits, defaults and reference
validation contract ([source](docs/design-graph.md)). All groups and views are
validated, including unused ones; invalid references/cycles disable export and
retain the previous valid view. A manifest maps named views to the shared entry:
`"views":{"assembly":"design.js","plate":"design.js"}`. Standalone checks
use the design's default view. Existing `scene`/`displayParts` files still work.

## Parts, groups and display colors

The optional `displayParts` export defines **both** the viewer's components and
its selective export source. Include every component that should be viewed
or offered for export; solids present only in `scene` are not added automatically.
With this legacy `displayParts` contract, the module must also export a valid
`scene` (used for CLI CAD bounds). The shared `design` contract above replaces
both exports. Use original assembly coordinates or deliberate print-layout coordinates.

```javascript
const panel = cube({size:[100,40,4]});
const wall = translate(cube({size:[120,4,70]}),[-10,-4,0]);
export const scene = compose(panel,wall);
export const displayParts = [
  {id:'wall', name:'Muro', group:['Riferimenti esterni'],
   solid:wall, color:'#ddd8cd', exportable:false},
  {id:'panel', name:'Piastra', group:['Oggetto progettato','Ripiani'],
   solid:panel, color:'#628bb5', exportable:true},
];
```

Each entry requires a manifold `solid` and an opaque `#RRGGBB` `color` string.
Colors are display aids, not filament assignments. Optional metadata:

| Field | Contract / default |
| --- | --- |
| `id` | Unique nonempty string, stable across reload and reordering. Omitted: index-based identity. The `@index:` prefix is reserved. |
| `name` | Nonempty string. Omitted: `Part 1`, `Part 2`, etc. |
| `group` | Array of nonempty strings, outermost group first, at most 32 levels. Omitted: root-level part. Groups with the same path merge. |
| `exportable` | Boolean, default `true`. Set `false` for external context. |

Strings cannot contain NUL. The maximum list size is 10,000 entries. A missing
`displayParts` gives one gray **Scene** node and exports the original `scene`
solid. An explicitly empty array gives an empty tree and no exportable geometry.
Invalid entries, duplicate IDs or throwing metadata getters reject the whole
reload: the last valid view remains and export is disabled until a valid reload.
There is no fallback export of the entire scene on a metadata error.

Tree state is held only for the running session. Visibility, user overrides of
exportability, selection and expanded groups survive reload by stable part ID or
group path. Newly added IDs use model defaults; removed IDs are pruned. Without
explicit IDs, reordered parts inherit state by index. Unmodified exportability
follows the model's latest default. Reload preserves the camera. Separate GPU
meshes remain resident; tree visibility does not reevaluate JavaScript, booleans
or triangulation.

The **Export** button and **P** open the same dialog, with standard **3MF**
(initial format) or **STL**:

- **All exportable parts** (initial mode) includes hidden exportable parts.
- **Visible exportable parts** includes only currently visible exportable parts,
  including isolation's temporary visibility.

The dialog shows the part count, starts at `Downloads/<scene-stem>.3mf`
(`synthcad.3mf` for the built-in sample), accepts an edited
path retained during the session and requires **Replace file** confirmation before replacing an existing file. Cancel, an
empty selection and invalid metadata produce no file and do not overwrite one.
A non-exportable reference is included only after the user enables its export flag.
Selected source solids are composed in their original CAD coordinates, without
translation, scaling or automatic boolean union. Overlapping bodies remain
separate; the tree does not arrange print plates. STL contains no colors or
annotations. Export from a print-layout scene when a bed arrangement is needed.

3MF preserves separate named instances, millimetre units and the current
placements, with shared source mesh resources for shared design parts. It
contains one current arrangement, not a native multi-plate slicer project or
printer/material presets. Choose those settings in the slicer. Switching format
updates the filename extension; the destination must match the selected format.
See [standard 3MF export](docs/three-mf-export.md).

The `synthcad-cli export` command uses the same viewer-owned export service. A dry
run returns the included instances, source quantities, revision basis and
cached checks without creating files. Actual exports require explicit warning
and replacement acknowledgement when applicable. Committed files receive
[local export receipts](docs/export-history.md); edits never regenerate them.
See [CLI export options](docs/agent-cli.md) for revision guards and responses.

The Windows view uses soft baked lighting and sharp CAD creases. Save a PNG
without opening a visible window with
`dingcad_viewer --render-scene scene.js preview.png` (no UI or annotations).
For viewer UI QA, `--ui-preview scene.js preview.png [mode]` captures three frames
of the actual hidden viewer. Modes: `export`, `hidden`, `closed`, `small`,
`selected`, `isolated`, `dimensions`; selection modes use the `Oggetto progettato`
group in the fixture. Additional UI/scaling modes are documented in README.md.
These commands do not export STL. The product UI is English; names, groups,
annotations and model-generated diagnostic text remain exactly as authored.

## Dimension annotations

Modules can also export an optional `dimensions` array. Coordinates use the same
millimetres and Z-up XYZ axes as the geometry. The viewer does not infer values
from the mesh; use your geometry parameters for both values and anchors.

```javascript
const width = 120;
export const scene = cube({ size: [width, 80, 10], center: false });
export const dimensions = [{
  type: 'linear', // 'linear', 'diameter', or 'radius'
  label: 'Width',
  value: width,   // positive finite number, millimetres
  start: [0, -8, 0],
  end: [width, -8, 0],
  marker: [width / 2, -8, 0], // optional; defaults to the endpoints' midpoint
}];
```

Labels are nonempty strings; anchors must contain exactly three finite numbers.
Malformed entries are skipped with console diagnostics. A missing export means
no annotations. Diameters display with `Ø`, radii with `R`, and values with at
most two decimal places. Dimensions are informational and are not exported to STL.

The **Dimensions** button or **M** cycles **Hover → All → Off → Hover**. Hover is
the startup mode: move within 10 pixels of a small marker to see its measurement.
The closest marker wins when several overlap. All displays every annotation whose
anchors are in front of the camera and whose marker is within the viewport. The
overlays do not perform surface occlusion tests, so markers can describe holes
and hidden features. Off hides the overlays. The mode is retained during reload;
successful reloads replace annotations, and failed scene loads retain the last
valid geometry and annotations.
