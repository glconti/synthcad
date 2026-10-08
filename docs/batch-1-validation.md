# Batch 1 validation

Verified on 2026-10-08 for SC01–SC05, GitHub issues #2–#6. The implementation
provides a persistent local review session while models remain ordinary editable
files. [CLI usage](agent-cli.md) and [project contracts](agent-contract.md)
describe the delivered interface.

## Results

| Check | Environment | Result |
| --- | --- | --- |
| Release viewer and CLI build | Windows x64, MSVC 19.44, C++17 | Passed |
| Project contracts and content revisions | Windows; Arch Linux under WSL, GCC 16.1 | Passed |
| CLI parsing, discovery and responses | Windows | Passed |
| Detached process lifecycle and local transport | Windows named pipes; Linux Unix sockets | Passed |
| Revision waits and queued review commands | Windows; Linux | Passed |
| Semantic scene snapshot and part/group references | Windows | Passed |
| Existing appearance, parts, camera and dimensions suites | Windows | Passed |
| Embedded/runtime icons, external-directory launch and reload recovery | Windows | Passed |
| Live CLI-to-viewer acceptance script | Windows Release viewer | Passed |
| CLI compilation, help and capability discovery | Linux | Passed |

The live acceptance script uses public fixtures copied into a unique temporary
directory. It covers concurrent/repeated opens, a path with spaces and Unicode,
multiple projects, selection readback, independent highlights/export flags,
framing, screenshots with overwrite protection, named views, imported-file edits,
failed reload and recovery, stale guards, superseded requests, timeout and
session cleanup. It terminates only its own test viewer processes.

Focused tests additionally cover source changes with unchanged timestamps,
new/missing dependencies, SHA-256 vectors, identical graphs in distinct views,
process-start identity checks, stale registry records and concurrent waits/read
requests. Filename presentation preserves authored case while project/session
identity uses the shared canonical-path rules.

## Reproduction on Windows

From the configured MSVC developer environment, build Release and the focused
targets:

```powershell
cmake --build --preset windows-x64-release
cmake --build out/build/windows-x64-release --target synthcad_project_contract_tests synthcad_agent_cli_tests synthcad_agent_transport_tests synthcad_agent_bridge_tests synthcad_agent_scene_tests dingcad_appearance_tests dingcad_parts_tests dingcad_camera_tests dingcad_dimension_tests

$bin = 'out/build/windows-x64-release/viewer'
foreach ($name in @('synthcad_project_contract_tests', 'synthcad_agent_cli_tests', 'synthcad_agent_transport_tests', 'synthcad_agent_bridge_tests', 'synthcad_agent_scene_tests', 'dingcad_appearance_tests', 'dingcad_parts_tests', 'dingcad_camera_tests', 'dingcad_dimension_tests')) {
    & "$bin/$name.exe"
    if ($LASTEXITCODE -ne 0) { throw "$name failed" }
}
python scripts/test-windows-ui.py "$bin/dingcad_viewer.exe"
python scripts/test-agent-session.py --cli "$bin/synthcad.exe" --viewer "$bin/dingcad_viewer.exe"
```

Run IPC tests as the ordinary user owning the viewer. A sandbox with a different
restricted token cannot access an owner-only named pipe. `--keep-temp` on the
acceptance script preserves the copied fixtures and raw CLI trace for debugging.
No private project is needed for validation.

## Limits and subsequent work

Linux verification covers the native CLI, local transport, lifecycle and revision
logic. It does not establish Linux graphical rendering, fonts, input scaling or
distribution support. Those remain in SC19 and SC21. Linux tests used a C++17
compiler, pthreads and nlohmann JSON headers without a GPU or a slicing program.

Ordinary legacy scene launches retain their previous behavior; use `open` to
create a CLI-managed session. A hidden viewer still requires a graphics context.
No installed skill, printer profile, geometric feature picking, build-plate
workflow or new export command is implied by this batch.

The requested wait token includes the active view and differs from the displayed
source revision. Use the revision returned by a successful wait to guard later
review operations. File comparisons detect changes on a best-effort filesystem
basis; they do not lock the user's editor.

The test fixtures and test outputs are separate from personal models and released
STL files. This batch performs no printing, slicing or physical validation.
