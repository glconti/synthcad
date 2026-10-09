# CLI discovery and session QA fix validation

Validated on 2026-10-09 against the working changes following `ac3fdf3`.
This is a bounded QA batch; it does not resume the v1 feature backlog.

## Delivered behavior

- The portable Release is `synthcad-cli.exe`. No arguments and `--help`
  identify a CLI controlling a persistent viewer. Development target names and
  legacy viewer entry points remain compatible.
- Embedded guides default to `design.js` plus `synthcad.json`, with shared
  geometry and named views. Requested outputs go in `exports/`, preferably
  3MF. Routine JSON stays in memory; review screenshots use temporary paths.
  Export history stays under `.synthcad/` and sessions remain app-managed.
- Session discovery distinguishes exited, alive and uninspectable processes.
  Unknown processes retain their records and use an authenticated handshake.
  Failed access cannot trigger another launch. Pending launches receive the
  same protection; lock access errors return promptly with the native code.
- Windows default session storage uses the stable per-user LocalAppData/Temp
  location instead of a caller's redirected temporary directory. Standalone
  positional project/scene launches route through persistent session reuse.

## Evidence

| Check | Result |
| --- | --- |
| Windows portable Release build and all 26 CTest suites | Pass |
| Linux build and all 26 CTest suites | Pass |
| Windows/Linux native agent-session acceptance | Pass |
| Standalone stdout guides, 15 topics, no generated files | Pass on both platforms |
| Windows embedded seven-size icon, runtime icons, outside-repo launch, watched failure/recovery | Pass |
| Skill `quick_validate.py` and whitespace checks | Pass |
| Real denied process inspection with a test-owned Windows process DACL | Pass: authenticated reuse; unreachable record retained |
| Denied launch-lock access | Pass: immediate `io_error`, Windows error 5 |
| Slow startup, concurrent opens, stale records, distinct projects and closure | Pass |
| Minimal project in a path with spaces and accented characters | Pass |
| Copy of the airplane, with only its two authored files | Pass |
| Original airplane and nine released V5 STL hashes | Unchanged |

The minimal workflow used PID 46748 across separate caller processes. The
airplane-copy workflow used PID 14584. Both checks verified one visible viewer
window, repeated `open`, positional manifest reuse, revision acknowledgement
after editing, and 3MF export. Each project's root ended with only `design.js`,
`synthcad.json`, `exports/` and `.synthcad/`. No original project was modified.
The airplane test used the executable alone outside the repository, launching
that same executable as its viewer. Test-owned viewers were stopped afterward.

Linux native acceptance ran under Xvfb/Mesa in the existing validation container.
The harness now recognizes exited zombie processes when container PID 1 has not
reaped them; they are not running viewers. This is transport/lifecycle coverage,
not a new Linux packaging claim.

## Restricted Windows caller limitation

Separate restricted agent calls were exercised against the same default registry.
This execution sandbox denies access to the owner's named pipe and launch lock.
`state` and repeated `open` return `io_error` with Windows error 5; `sessions`
retains the recorded PID. No second viewer is launched. With the required
execution permissions, the same calls reuse the viewer, reload and export.

Process inspection denial alone is recoverable when authenticated pipe access
works, as verified by the focused native test. SynthCAD does not bypass OS access
controls. Retry with the needed execution permissions and the same registry;
changing `SYNTHCAD_SESSION_DIR` is not a recovery procedure.

## Reproduction

Build with `windows-x64-portable-release`, then run CTest and:

```powershell
python scripts/test-agent-guidance.py --cli <path-to-synthcad-cli.exe>
python scripts/test-agent-session.py --cli <path-to-synthcad-cli.exe> --viewer <path-to-synthcad-cli.exe>
python scripts/test-windows-ui.py <path-to-synthcad-cli.exe>
```

For separate tool-call verification, invoke `scripts/test-standalone-workflow.py`
with the same `--cli` and a fresh `--work-dir`, one phase per call:
`setup`, `open`, optional `restricted`, `review`, `export`, `cleanup`.
Use `--source-project` only during setup to copy the two authored files from an
existing project. Test evidence is kept outside that copied project.

QA executable SHA-256:
`33571DD1B522ECCF477174FD43ED7D3C2FDF24301776157C1FF99FFA965985C0`.
The old QA executable is archived outside the user's project.
