# Batch 3: shared source parts and instance layouts

SC12 adds an optional `design` export alongside the unchanged legacy scene API.
The full contract is available through `synthcad docs design` and
[the reference document](design-graph.md).

## Delivered behavior

- Source parts define geometry once; physical instances retain the same
  unplaced source handle and have distinct stable IDs.
- Groups and views reference instances. Repeated membership creates tree
  aliases with shared visibility/export state, not additional export solids.
- Assembly, inspection and multiple plate layouts use separate absolute poses.
  View overrides replace the instance pose without modifying its source.
- All references are validated, including unused groups/views. Cycles,
  dangling references, duplicate IDs, invalid transforms and placements outside
  a view's membership reject the load and disable export.
- Snapshots expose source and instance IDs, normalized design metadata and
  effective transforms. Source revision, graph/layout identity and displayed
  revision distinguish source edits from view changes. Failed reloads retain
  the previous valid model, graph and displayed view.
- Intended quantities are metadata. Automatic bed/quantity checks, persistent
  check/export records and 3MF serialization remain subsequent stories.

The public fixture has two printable instances of one source part, a separate
external reference, alias groups and four views of a single entry. Try:

```text
synthcad docs design
synthcad open viewer/tests/agent-fixtures/shared-design --session shared
synthcad snapshot --session shared --json
synthcad view plate-1 --session shared
synthcad view assembly --session shared
```

Edit `parts.js` normally, then use `revision` and `wait` before inspecting the
result. No printer profile is needed to author or review the reference graph.

## Windows verification

Native MSVC x64 Release build passed. The four existing viewer regression
suites and the project, CLI, transport, bridge, knowledge and scene suites
passed. New graph tests verify immutable shared handles, aliases, composition
counts/volume, transforms, limits and rejection paths. Tree tests cover alias
toggles, mixed state, filtering, isolation and stable group IDs after renaming.

`scripts/test-shared-design.py` passed against the real viewer. It edits an
imported source, checks geometry across all four views, checks stable instance
poses, exercises stale guards and verifies failed unused references/cycles,
export invalidation and recovery. The Batch 1 live session demonstration,
stdout guidance test and Windows icon/reload smoke also passed.

## Linux verification

The actual viewer and CLI built in an isolated Ubuntu 24.04 x64 container using
GCC 13.3, CMake 3.28.3, raylib 6.0, assimp 5.3.1, TBB 2021.11 and JSON 3.11.3.
The repository and raylib source were mounted read-only; generated artifacts
were kept outside tracked source files. No upgrade of the host WSL distribution
was needed.

All twelve C++ suites passed. The existing agent-session and new shared-design
live harnesses passed under Xvfb and Mesa llvmpipe OpenGL 4.5, including cleanup.
The container used a subreaper (`tini -s`) so detached viewers were reaped after
test termination. Dimension rendering and normal/150%/200% UI previews passed;
the shared-design screenshot was visually inspected for geometry, tree aliases,
unique part count, external-reference flags and readable text.

Linux build fixes include QuickJS's POSIX feature definitions/libraries,
DejaVu font discovery and an explicit `SYNTHCAD_CUSTOM_FRAME_CONTROL` option
that must match the linked raylib build. The Linux test raylib used custom frame
control OFF, as did the viewer; the Windows vcpkg build uses ON.

This establishes manual Linux build/runtime evidence, not completed SC19 CI or
packaging. Native Wayland, interactive high-DPI input, packaged dependency
discovery and release distributions remain to be verified in their stories.
Forced-scale previews establish layout/rendering only.

The acceptance harnesses now preserve their fixture directories after exit when
`--keep-temp` is requested; ordinary runs continue to clean up their own files.
No personal models or released STL files were included or regenerated.
