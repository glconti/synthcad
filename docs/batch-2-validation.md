# Batch 2: stdout guidance and selection feasibility

SC06 and SC07 deliver the next foundation after the persistent review loop.
The product decision is CLI-only guidance: users do not install skills or keep
guide files beside their projects. Source Markdown remains maintainable in this
repository and is compiled into the CLI.

## Discovery and documentation

```text
synthcad
synthcad --help
synthcad docs
synthcad docs modeling
synthcad docs print-design
synthcad docs skill
synthcad docs api --json
```

No-argument help and `--help` group commands and guidance by area. Ten complete
topics cover startup, the design workflow, modeling, print design, fit/assembly,
plate layouts, Bambu handoff, the implemented API, CLI and project contracts.
Plain output is guide text, with no success prefix; JSON adds content hashes
and a content-derived bundle version. Unknown topics return `not_found`.

The generator embeds the maintained API and command/project documents rather
than separate copies. Changes to source guides regenerate the bundle during
the build. `scripts/test-agent-guidance.py` copies only the CLI executable to an
isolated Unicode directory, with no viewer or repository files, then verifies
all topics, grouped help, output equivalence, hashes, errors and absence of
filesystem writes.

An independent agent read the stdout guides and authored a new two-piece
electronics enclosure with alignment features, shared source geometry and
assembly/print-layout views. Both entries passed `--check-scene` with two named
parts. Dimensions were illustrative and printer/process values remained unknown;
the test establishes onboarding and valid modeling, not verified manufacture.
The disposable model stays in ignored local workspace storage.
An isolated native viewer session also acknowledged both named views through
revision/wait. Guarded snapshots confirmed two named parts in each view, and
both print-layout parts had minimum Z of zero. The test closed only the viewer
process it launched.

## Geometric selection decision

[The selection feasibility report](selection-feasibility.md) documents the
CPU-only prototype, fixtures, performance, limits and SC08 integration decision.
It exposes revision-local planar faces, curved patches, sharp edge chains and
corners without presenting arbitrary triangle IDs as persistent CAD features.
Tests reconstruct complete exploded-mesh faces and closed cylinder rims through
immutable topology accessors. The prototype is not yet wired into viewer picking.

The Windows Release measurement on a 32,768-triangle decorative sphere was
approximately 87 ms to build topology and 0.43 ms per hitting surface ray.
These timings exclude mesh generation and do not predict whole-scene UI latency.
SC08 still needs screen-space snapping, occlusion, hidden-part filtering and
selection/transport integration.

## Verification

Windows x64 Release build and the following passed:

- Four existing appearance, parts/export, camera and dimensions suites.
- Project-contract, CLI, transport, bridge and semantic-scene suites.
- New bundled-guidance and feature-topology suites.
- Standalone stdout-guidance smoke test.
- Windows embedded/runtime icon and watched error/recovery smoke test.
- Full Batch 1 persistent-session acceptance demonstration.
- Portable design skill source `quick_validate.py` validation.

Linux, tested natively through WSL Arch with g++ 16.1:

- CLI compiled with the embedded guidance bundle.
- CLI parsing/response and knowledge-runtime tests passed.
- A copied standalone executable printed guides and grouped help outside the
  checkout; unknown-topic errors returned the expected exit code.

Linux GUI, distribution packaging and geometric-selection interaction remain
future acceptance work. No personal model or released STL is part of this
delivery. There is no slicing, 3MF export or physical validation in this batch.

## Product decisions carried forward

SC12 now specifies shared source-part definitions and distinct placed-instance
IDs across assemblies, groups, inspection and plate views. Group membership
must not create extra printable copies. Views own placement transforms, edits
propagate from shared sources, and invalid references fail explicitly. The
core contract does not depend on a printer profile. This is planned work, not
a new manifest schema shipped by this batch.
