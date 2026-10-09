# Shared parts and named views

An entry module may export `design` instead of `scene` or `displayParts`.
Mixing these exports rejects the complete load. Existing modules retain their
legacy behavior when `design` is absent.

```js
const bracket = cube({ size: [20, 10, 3] });
export const design = {
  schemaVersion: 1,
  defaultView: 'assembly',
  parts: [{ id: 'bracket', solid: bracket, name: 'Bracket', quantity: 2 }],
  instances: [
    { id: 'left', part: 'bracket' },
    { id: 'right', part: 'bracket', transform: { translate: [40, 0, 0] } },
  ],
  groups: [{ id: 'pair', name: 'Bracket pair', members: [
    { instance: 'left' }, { instance: 'right' },
  ] }],
  views: [
    { id: 'assembly', kind: 'assembly', members: [{ group: 'pair' }] },
    { id: 'inspection', kind: 'inspection', members: [{ instance: 'right' }] },
    { id: 'plate', kind: 'plate', members: [{ group: 'pair' }], placements: {
      left: { translate: [0, 0, 0] },
      right: { translate: [25, 0, 0] },
    } },
  ],
};
```

`parts`, `instances`, and `views` are required arrays. `groups` is optional.
IDs must be unique within their category, nonempty strings without NUL, and
must not begin with the reserved `@index:` prefix. Typed member references
contain exactly one `instance` or `group` field, so categories may reuse an ID.
All references in all groups and views are checked, including unused groups.
Cycles, more than 32 nested groups, more than 10,000 total definitions and
member references, or more than 100,000 expanded validation paths reject loading.

A source part requires an exact manifold `solid` handle. Its optional `name`
defaults to its ID, `color` to `#aaaaaa`, and `exportable` to `true`. Color uses
six hexadecimal RGB digits. An instance inherits these values and may override
`name`, `color`, and `exportable`. Optional source `quantity` is a positive safe
integer describing the intended total; it is retained in metadata. Quantity
does not create copies or enforce plate completeness.

Every physical copy needs its own instance ID. Repeating an instance through
multiple groups creates aliases in the parts tree while keeping one solid in
the scene and one export component. Repeating the same path also deduplicates
that path. A root member retains an empty membership path. Group IDs identify
tree nodes independently from their visible labels.

Transforms allow only `rotate` and `translate`, each a three-number array of
finite values. Missing components default to `[0, 0, 0]`. Rotation uses XYZ
degrees, followed by translation in millimetres. A view placement replaces the
entire instance transform; it does not compose with it. For example, an override
containing only translation resets rotation to zero. Source solids are never
modified. Scale, matrices, malformed values, and unknown transform fields reject
loading. Getter failures also reject loading and disable export.

Each view requires `id`, `kind`, and `members`; optional `name` defaults to its
ID. Kinds are `assembly`, `inspection`, and `plate`. Optional `placements` maps
instance IDs to absolute transforms. Each placement must target an instance
included in that view's expanded members, including in unselected views.
`defaultView` must identify an existing
view. A requested named view selects that view; otherwise the default applies.
Opening a standalone JavaScript file selects its default design view, exposed
through the legacy `scene` view alias. To switch layouts in one CLI-managed
project, add this manifest beside the shared `design.js` entry:

```json
{
  "schemaVersion": 1,
  "defaultView": "assembly",
  "views": {
    "assembly": "design.js",
    "inspection": "design.js",
    "plate": "design.js"
  }
}
```

Run `synthcad-cli project open . --name brackets`, then
`synthcad-cli review view plate --project brackets`. Manifest names select matching design
view IDs; a missing match fails. The source definition can live in an imported
module, so all entries share the same geometry authoring. Each evaluation still
rebuilds the selected scene; the contract does not promise a cross-view GPU cache.

The parsed result includes normalized JSON `sourceParts`, `instances`, `groups`,
and `views`; `defaultView`; `activeView`; effective `resolvedInstances`; and a
stable SHA-256 `identity`. No JS handles enter this metadata. Identity covers
normalized definitions and the selected layout. The viewer combines it with
the actual consumed source revision and active view to guard displayed state
against stale actions. Geometry edits invalidate that source revision even
when normalized metadata remains unchanged. This contract supplies provenance;
it does not implement a persisted validation or export history.
