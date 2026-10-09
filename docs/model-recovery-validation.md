# Recoverable model failures: validation

Validated on 2026-10-09 against the uncommitted working tree following
`ac3fdf3`. This report covers the bounded model-recovery batch. No commit,
push, QA executable replacement or milestone expansion was performed.

## Review build

The standalone Windows Release is in
`out/review/model-recovery/synthcad-cli.exe`. The folder contains only that
executable, including its embedded guides, icons and hidden worker entry point.
The executable currently used by the other design session at
`D:/SynthCAD-QA/synthcad-cli.exe` was left untouched.

From the review folder, use a new test project:

```powershell
.\synthcad-cli.exe open PATH_TO_TEST_PROJECT --session recovery-review --json
.\synthcad-cli.exe state --session recovery-review --json
.\synthcad-cli.exe reload --session recovery-review --evaluation-timeout 120000 --json
.\synthcad-cli.exe cancel-load --session recovery-review --json
```

The same executable launches its own hidden worker. Evaluation defaults to
120 seconds. A caller's `--timeout` does not cancel evaluation. Do not switch
session registries or repeatedly launch viewers to recover from model errors.
An already-running viewer continues to use its existing executable until closed.

## Delivered behavior

- JavaScript, geometry evaluation, triangulation and load-time manufacturing
  checks run in an owned process. Windows uses a kill-on-close job; Linux uses
  an owned child with parent-death cleanup. Cancellation, superseding edits,
  timeout and native exit leave the viewer and its session available.
- A versioned binary result carries full mesh properties and precision, shared
  source identities, instance transforms, component metadata, annotations,
  checks and source digests. No STL conversion is used for scene transfer.
  Results are validated and a candidate scene is prepared before replacing the
  displayed model. Only the current unchanged revision may be displayed.
- Reads, including failed imports, are journaled while work proceeds so edits
  can recover failed loads. Temporary worker artifacts are application-owned.
- Loading offers Cancel. Failure offers Details, Copy details and Reload,
  labels retained geometry, and disables export. First-load failure leaves an
  empty workspace. Navigation and inspection remain available.
- `state`, `wait`, overview and revision-linked events expose the same failure
  category, processing stage, attempted/displayed revisions and available
  source, operation, stack and native exit information. Existing `load_failed`
  compatibility is retained. Headless checks use the worker too.
- `levelSet` rejects invalid bounds, nonfinite parameters, nonnumeric or
  nonfinite callback results and excessively large grids. Callback exceptions
  retain their diagnostics. Geometry is not silently replaced.

## Verification

| Check | Result |
| --- | --- |
| Windows portable Release and original 26 suites plus worker suite | 27/27 pass |
| Linux build and the same suites | 27/27 pass |
| Windows/Linux native session lifecycle acceptance | Pass |
| Windows/Linux focused live recovery harness | Pass |
| Controlled worker exit, hang, malformed result and cancellation | Pass |
| Failed imports, callback errors, invalid data and automatic recovery | Pass |
| Responsive state during evaluation; caller timeout leaves work running | Pass |
| First-load failure, retained-model failure, superseded edits and explicit retry | Pass |
| Shared sources, selection, guided picking, overview and manufacturing regressions | Pass |
| Export workflow, mesh reload and physical-feedback regressions | Pass |
| Geometry roundtrip volume/topology/shared identity and STL/3MF workflow | Pass |
| Standalone stdout guidance, 15 topics, no writes | Pass on Windows and Linux |
| Windows embedded/runtime icons, outside-repo launch and watched reload | Pass |
| Whitespace checks | Pass |

Linux native checks ran under Xvfb/Mesa in the existing Linux container. They
verify worker and session behavior, not a new distribution package.

Reproduce the focused acceptance with:

```text
python scripts/test-model-recovery.py --cli PATH_TO_EXECUTABLE --output OUTPUT_DIRECTORY
```

The harness creates its own fixtures and sessions and stops only its own
processes. It deliberately terminates a model worker and verifies the same
viewer PID still responds, retains geometry, reports failure and recovers.
The final Windows run retained viewer PID 47964 through that sequence.

Local evidence is kept under `out/worker-qa/windows/` and
`out/linux-validation/worker-recovery/`. Screenshots include `loading.png`,
`error.png`, `native-error.png`, `first-load-error.png` and `recovered.png`.
Build and acceptance logs are `out/worker-*.log` and
`out/linux-validation/worker-*.log`.

## Snowman investigation and limits

The other design session's history recorded repeated Windows access violations
(`0xc0000005`) during implicit-surface modeling. An isolated copy of its body
field also produced a contained native worker failure during this investigation.
The reduced public mathematical fixture is
`viewer/tests/agent-fixtures/implicit-field.js`; neither the original project nor
its geometry was rewritten by this batch.

The current Release renders both the reduced fixture and the isolated original
field successfully. Linux renders them successfully too. The precise native
cause has not been established, and this report does not claim that the engine
fault itself is fixed. The regression accepts either a successful render or an
honest contained native failure with `levelSet` context and a surviving viewer.
Deterministic worker-fault injection independently verifies crash containment.

The worker packet limit is 512 MiB and `levelSet` grid estimates are capped at
16 million cells. GPU-driver failures and system-wide resource exhaustion are
outside the containment guarantee.

All nine released V5 STL hashes still match
`out/shampoo-qa/wall-fit/stl-before.json`. The installed QA executable still has
SHA-256 `33571DD1B522ECCF477174FD43ED7D3C2FDF24301776157C1FF99FFA965985C0`.
Personal sources were only read; unrelated uncommitted work was preserved.
