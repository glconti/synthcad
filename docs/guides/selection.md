# Read and resolve geometric selections

Geometric selections preserve the selection object's `key`, `name`, `group`
and `partIds` fields and add a `geometry` object. Its `kind` is `part`,
`planar-face`, `curved-patch`, `edge` or `vertex`; it includes `reference`,
`partId`, `revision`, `sourcePartId`, `instanceId`, `position`, `normal` and
`bounds`. Coordinates and bounds use model millimeters. Legacy parts have
null source/instance context. A clicked position has `positionKind: "hit"`;
resolving a copied reference supplies a point on the current feature with
`positionKind: "representative"`. A part reference uses its bounds center.
The normal is null when the feature has no single normal, including a
resolved curved patch, an edge or a vertex.
If an unusually long authored ID would exceed the token limit, the context
remains available with `reference:null` and `referenceError`; Copy is disabled
but selection and clearing still work. `data.selectionDiagnostics` reports
parts whose meshes do not support semantic feature selection; use Part mode.

Copy the reference from the viewer's selection inspector or read
`data.selection.geometry.reference` from `review selection --json`, then resolve it:

```text
synthcad-cli review selection scsel1.HEX_PAYLOAD --project bracket --json
```

References are portable ASCII strings with prefix `scsel1.` and a hexadecimal
canonical JSON payload, bounded to 16 KiB decoded. The payload contains only
`revision`, `partId`, `kind`, and, for geometric features, `topologyKey` (a
decimal string preserving all 64 bits) and `id`. Source-part and instance
context comes from the current scene; it is never accepted from a token.
References are local to a displayed revision and owning part, with topology
identity checked against the current geometry. Reloading, changing view,
moving an instance or changing its mesh can invalidate a reference. Malformed
tokens return `invalid_argument`; stale revision references return
`stale_revision`; an owner missing from the current view returns `not_found`,
while a mismatched topology or feature returns `stale_revision`. Capture a
fresh selection after the scene changes. Resolving a reference is a read-only
review action and does not change the human's selection.
