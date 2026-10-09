# Review geometry with the human

Use this workflow after authoring or when the human asks about the displayed
model. Open the project once, then capture and wait for a revision after edits.
Read `project inspect --json`: verify load status and displayed revision before
interpreting the geometry, authored annotations, selection, or cached evidence.

Use part and group IDs from inspection to focus the discussion:

```text
synthcad-cli review highlight base lid --frame --project bracket --expect-revision DISPLAYED_TOKEN --json
synthcad-cli review selection --project bracket --json
synthcad-cli review frame --selection --project bracket --json
```

Replace DISPLAYED_TOKEN with the successful wait result’s displayed revision.
Highlights belong to the agent and do not change human selection or export flags.
Names and annotations are authored context, not independently measured facts.

When the human’s intent depends on a particular feature, read
`synthcad-cli review pick --help`, ask one focused question, and wait for explicit
confirmation. A copied selection reference can be resolved with
`review selection REFERENCE`; stale references require a fresh selection.

Switch authored layouts with `review view NAME`, then capture and wait for that
view before reviewing it. Save a temporary PNG with `review screenshot PATH`
when an image helps; it establishes visual appearance, not manufacturability.

If a revision guard fails, inspect current state and repeat the relevant review
against a fresh revision. If load failed, fix the source first: retained geometry
belongs to the previous successful load. Read `print checks --help` for the
separate manufacturing evidence and its limits.
