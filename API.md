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
- loadMesh{path:string, forceCleanup?:bool}
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

Assign your final solid to `scene` to render, e.g. `scene = cube({...});`.

## Display colors

Export optional `displayParts` to give individual solids opaque colors in the
viewer. This list replaces the visual model, so include everything you want to
see. The `scene` solid still controls STL export and reported CAD bounds.

```javascript
const shelf = cube({size:[100,40,4]});
const support = translate(cube({size:[8,40,20]}),[0,0,-20]);
export const scene = compose(shelf,support);
export const displayParts = [
  {solid:shelf, color:'#478a62'},
  {solid:support, color:'#aab6bb'},
];
```

Colors are `#RRGGBB` strings, not material/filament assignments. Missing or empty
lists use the default gray. Invalid entries warn and fall back to the entire
`scene`, rather than hiding some parts. Color changes reload with the geometry.
Display meshes preserve sharp CAD edges; they do not modify the exported solid.
The Windows view uses soft baked lighting. Space frames the current model from
the front; ordinary reloads preserve your camera position.

Save a PNG without opening a visible window using
`dingcad_viewer --render-scene scene.js preview.png`. This uses the same display
colors and Windows lighting, without the interactive grid or annotations.

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
