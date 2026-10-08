# SC07: revision-local geometric selection

Decision: proceed to SC08 with **planar faces, curved patches, sharp edge chains,
and geometric corner/junction vertices**. The mesh supports useful review
features, including mating faces and bores. It does not provide analytic CAD
faces or permanent topology correspondence. A curved patch must be named as a
patch in structured results and explained in the UI; it must not be advertised
as an inferred cylinder, sphere, or authored feature.

## Prototype and pipeline

`viewer/feature_topology.{h,cpp}` is a CPU-only prototype using Manifold MeshGL
XYZ and triangle indices, without raylib, a window, or GPU state. Build a
`dingcad::selection::Topology` for each owning part from
`part.solid->GetMeshGL()`. The current `appearance.cpp::DisplayMesh` explodes
vertices to preserve hard display normals and RGB. Tests also feed that buffer
shape to the prototype: RGB and all properties beyond XYZ are ignored, and
duplicated positions are welded for adjacency. Never build one topology from
the flattened display mesh of several parts: it loses ownership and may join
touching parts. Export geometry is not modified.

The existing viewer receives model positions from Manifold, without a separate
per-part display transform. Prototype pick context therefore uses the same
model/scene XYZ, in mm. Future instance transforms must transform rays into
part space and points/normals/bounds back into scene space explicitly.
The immutable `Points()` and `Triangles()` accessors expose the welded snapshot
used by feature member indices, so future overlays need not guess correspondence
with the original property-split mesh.

The public `Topology::Build` takes mesh, owning part ID, **displayed valid
revision**, and options. `RayPick` returns a `Reference` plus hit position,
optional local normal, whole-feature bounds, and distance along the normalized
ray to its first surface hit. `Contains` checks the complete reference. It is
the future transport/UI adapter's responsibility to use the current displayed
revision and visible parts, and to discard old caches when loading a new valid
revision. The prototype does not implement UI, clipboard, events or transport.

## Feature semantics

1. Weld positions within `max(1e-6 mm, bounding-box diagonal * 1e-7)` by a
   neighboring-cell spatial search. Defaults are explicit options, with the
   effective tolerance available to callers. Matching uses distance to an
   existing representative, avoiding an unbounded transitive weld chain.
2. Build undirected triangle adjacency. Join triangles across shared edges
   whose normals differ by **less than 30 degrees**. Point contacts and
   disconnected coplanar solids do not join.
3. Check every vertex in each connected region against its first triangle's
   plane using the positional tolerance. A region entirely in that plane is a
   `PlanarFace`; otherwise it is a `CurvedPatch`. This global check prevents a
   sequence of small normal changes from being mislabeled planar.
4. Edges are sharp or boundary segments, chained while the same adjacent region
   pair continues and no corner/junction occurs. A degree-two turn of at least
   30 degrees splits the chain. A smooth circular rim becomes one closed chain;
   face triangulation diagonals never become edges.
5. Vertices are endpoints, junctions or corners of these chains. Ordinary
   tessellation vertices on a smooth patch or circular rim are suppressed.
   A smooth sphere correctly has one patch and no geometric edges or corners.

A cylinder with 64 radial segments yields two planar cap faces, one curved side
patch, two closed rim edges and no corner vertices. A triangulated box yields
six planar faces, twelve straight edges and eight corners. A bored flat plate
provides a selectable mating plane and a separate curved bore patch.

Surface picking intersects triangles only to locate the first hit; it returns
the containing semantic region, never the triangle ID as a face. Curved hit
normals are the local tessellated surface normal, not an analytic normal or a
single normal assigned to the whole patch. Edge and vertex picks snap within a
specified world-space radius of that surface hit and omit the nonunique normal.
Edge bounds encompass the full chain, face bounds the full region. Surface
bounds and all normals are independent of display colors.

## Reference lifetime

A reference contains owning part ID, displayed revision, topology key, kind and
feature ID. IDs are local indices into the built snapshot, not authored CAD
identities. The topology key hashes input XYZ, triangle indices, effective
tolerance and angular threshold. This guards accidental reuse with different
geometry/options even if a caller reuses a revision string; it is not a
cryptographic identity. Changes to buffer order may change this key and IDs.

`Contains` rejects a different revision even when the mesh is identical. It
also rejects wrong part, geometry/options key, kind, and out-of-range index.
Authored part ID persistence does not establish geometric correspondence.
After reload, old geometric references must return stale-reference rather than
silently pointing at the same numeric index. Failed reload handling should
continue to identify the last valid displayed snapshot while reporting the
failed requested revision separately, as required by the session contract.

## Repeatable evidence

`viewer/tests/feature_topology_test.cpp` generates public, nonpersonal Manifold
fixtures directly. No personal scenes, export, renderer, or physical fit claims
are involved. Tests cover:

- Box face grouping, suppressed coplanar diagonals, twelve sharp edges/eight
  corners; exploded colored vertices and sub-tolerance positional seam noise.
- Cylinder cap/side distinction and complete circular rim chains; sphere patch
  without triangle edges/corners; disconnected coplanar boxes staying separate.
- A 40 x 30 x 5 mm fitting plate with a radius-4 mm bore, with separate planar
  and curved picks.
- First-hit position, local normal, complete face bounds, edge/vertex snapping,
  miss/zero direction, and stale/wrong-owner/wrong-kind/changed-geometry/options
  reference rejection.
- Malformed indices, collapsed triangles, and ambiguous nonmanifold welded
  edges rejected through `std::invalid_argument`.

Measured 2026-10-08 on this Windows x64 workspace, MSVC 14.44, `/O2 /MD`,
against the existing Release Manifold library. The representative decorative
fixture is `Manifold::Sphere(30, 256)`, exploded to simulate display properties:
**32,768 triangles, 98,304 input vertices**, one curved patch. Timers exclude
Manifold generation and exploded-buffer creation. Three complete passing runs:

| Run | Build topology | Mean CPU ray pick (100 hitting rays) |
| --- | ---: | ---: |
| 1 | 86.26 ms | 0.4281 ms |
| 2 | 86.99 ms | 0.4184 ms |
| 3 | 90.42 ms | 0.4264 ms |

These are local observations, not platform guarantees. Rays currently scan all
triangles; adjacency uses ordered maps and spatial welding uses a hash grid.
Build once per part/revision/options rather than per frame. Benchmark larger
real parts and many-part scenes before setting latency limits; use a BVH if
measured picking cost requires it. Edge/vertex scans and multi-part visibility
are not represented by the surface-ray benchmark.

For a reproduction in the configured MSVC x64 developer environment:

```text
cmake --build out/build/windows-x64-release --target synthcad_feature_topology_tests
out/build/windows-x64-release/viewer/synthcad_feature_topology_tests.exe
```

For a standalone build against the existing Release dependencies:

```text
cl /nologo /std:c++17 /EHsc /O2 /MD /W4 /Ivendor/manifold/include /Iout/build/windows-x64-release/vcpkg_installed/x64-windows/include viewer/feature_topology.cpp viewer/tests/feature_topology_test.cpp /Foout/build/windows-x64-release/ /Feout/build/windows-x64-release/feature_topology_test.exe /link out/build/windows-x64-release/vendor/manifold/lib/manifold.lib
```

Run the executable with `out/build/windows-x64-release/vendor/manifold/lib`
on PATH for the existing Manifold runtime and dependencies. It prints its own
counts, measured timing and PASS line. MSVC `/W4` reports the existing vendor
MeshGL `NumVert` size conversion warning; this prototype compiled successfully.

## Boundaries and SC08 implementation decision

- Thresholds are geometric policy. A coarse cylinder with dihedrals above the
  threshold remains faceted; a deliberately faceted decorative object with
  shallow dihedrals can become a curved patch. There is no reliable intent
  inference from triangles alone. Show patch semantics and allow later explicit
  authored feature metadata if that distinction becomes necessary.
- Smoothly joined planar and curved regions (for example a tangent fillet) can
  form a single curved patch. This prototype does not fit analytic primitives,
  discover fillet boundaries or guarantee one selectable region per modeled
  operation. Regions bounded by sharp creases remain separate.
- Welding can erase gaps or small features below tolerance. Coincident shells
  within one part may join or produce ambiguous nonmanifold adjacency. Such
  adjacency and collapsed triangles are rejected, not guessed. Valid oriented
  Manifold output is the intended input; self-intersections and inconsistent
  winding are not repaired or validated here.
- World-space snapping is a feasibility sample. It requires a surface hit and
  cannot pick an edge just outside the silhouette. Within the snap radius it
  can reach a nearby feature on a thin neighboring wall; it does not establish
  pixel-level feature visibility. SC08 must implement screen-space distance,
  occlusion and deterministic cross-part tie handling, and test hidden parts,
  touching/overlapping solids and high DPI.
- Degenerate/ambiguous topology should degrade to part selection with a useful
  diagnostic in the eventual UI. Do not substitute triangle features. No
  fallback or UI integration is implemented by the spike itself.

SC08 can use this semantic cache and revision contract, add screen-space feature
picking/overlays and transport serialization, and keep curved patches explicit.
The measured fitting and curved fixtures support that scope. Requiring analytic
faces, inferred tangent boundaries, or IDs that survive arbitrary edits would
require a separate geometry/provenance design before promising those behaviors.
