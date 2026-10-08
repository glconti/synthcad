## SynthCAD

SynthCAD is a live-reloading CAD workspace for reviewing agent-generated models,
inspecting named components and exporting STL geometry. It uses raylib, Manifold
and QuickJS. The visible product name is SynthCAD; existing `dingcad_viewer`
targets, launch scripts and model APIs remain compatible.

The [v1 product requirements](docs/product/PRD.md) and
[delivery backlog](docs/product/BACKLOG.md) describe the planned agent workflow,
shared review sessions and printing handoff. They distinguish planned features
from the current viewer documented below.

Initialize the geometry and JavaScript dependencies with:

```sh
git submodule update --init --recursive
```

## Windows

The native Windows workflow uses MSVC, Ninja, vcpkg, and the VS Code CMake Tools
extension. In an elevated PowerShell terminal, install the prerequisites with winget:

```powershell
winget install --exact --id Microsoft.VisualStudio.2022.BuildTools --override "--wait --passive --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
winget install --exact --id Kitware.CMake
winget install --exact --id Ninja-build.Ninja
winget install --exact --id Microsoft.VisualStudioCode
```

Bootstrap vcpkg outside this repository, then set `VCPKG_ROOT` for future terminals:

```powershell
git clone https://github.com/microsoft/vcpkg.git $env:USERPROFILE\vcpkg
& "$env:USERPROFILE\vcpkg\bootstrap-vcpkg.bat" -disableMetrics
[Environment]::SetEnvironmentVariable('VCPKG_ROOT', "$env:USERPROFILE\vcpkg", 'User')
```

Open a new terminal after setting `VCPKG_ROOT`. The first configure downloads and
builds the dependencies declared in `vcpkg.json`; later builds reuse the cache.

From PowerShell, run:

```powershell
.\scripts\run-windows.ps1
```

The normal launcher uses the optimized `windows-x64-release` preset. Debug
builds can be much slower for models with many boolean operations. The launcher
configures on first use, then builds incrementally; CMake regenerates when build
inputs change. To refresh dependencies or change toolchain settings explicitly,
run `cmake --preset windows-x64-release` in a configured developer terminal.

For native debugging, use `scripts/run-windows.ps1 -Configuration Debug`, or open
this folder in VS Code, install the recommended extensions, and select the
`windows-x64-debug` preset. The **Debug dingcad** launch configuration builds and
starts the selected CMake target with `scene.js`; **Run dingcad** uses the normal
Release launcher.

`scene.js` retains the upstream example assembly. To run a different scene, pass
its path to the launcher:

```powershell
.\scripts\run-windows.ps1 .\local-scenes\scene.js
```

Keep personal scene scripts, meshes, exports, and design notes in `local-scenes/`.
That directory is ignored by Git and is not part of the public development branch.
Preserve relative imports when moving a scene and its supporting modules there.
Avoid force-adding personal files; `.gitignore` is a safeguard, not access control.

## Linux development builds

The viewer and CLI have been built and tested on Ubuntu 24.04 x64 with GCC 13.3,
CMake 3.28, raylib 6.0, assimp 5.3, TBB and nlohmann_json. Install the development
packages for these dependencies, an OpenGL/X11 environment and DejaVu Sans.
The X11 development package enables native `Xft.dpi` detection; Windows uses
the window's native DPI. The app scales physical rendering and mouse input
itself, without raylib's separate `FLAG_WINDOW_HIGHDPI` transform.
Configure with CMake and build the `dingcad_viewer` and `synthcad` targets.
Set `CMAKE_PREFIX_PATH` when dependency packages are installed outside the system
prefix. Automated CI and distribution packages remain work in progress.

`SYNTHCAD_CUSTOM_FRAME_CONTROL` must match raylib's
`SUPPORT_CUSTOM_FRAME_CONTROL` build setting. It defaults to OFF on Linux and
ON on Windows, matching the tested Linux dependency and Windows vcpkg builds.
See [Batch 3 validation](docs/batch-3-validation.md) for runtime evidence and
the limits of Xvfb/Mesa testing.

## Agent CLI and shared review

Release builds also produce `synthcad` (`synthcad.exe` on Windows). Start with
`synthcad --help`, or pass the same commands to `dingcad_viewer`. The existing
Windows launcher accepts them too:

```powershell
.\scripts\run-windows.ps1 open .\scene.js --session review
.\out\build\windows-x64-release\viewer\synthcad.exe snapshot --session review --json
```

Running `synthcad` with no arguments groups commands and guidance by area.
Read just the instructions you need with `synthcad docs start`,
`synthcad docs modeling`, `synthcad docs print-design` or `synthcad docs api`.
`synthcad docs` lists all areas. Complete guides print to stdout from the CLI's
compiled bundle, without installing skill files or accessing this checkout.
Add `--json` for content and version metadata in a structured response.

The CLI opens or reuses a persistent session. Agents edit model files normally;
the viewer hot-reloads them. Capture a requested revision with `revision`, then
use `wait --revision TOKEN` to acknowledge the loaded result. Highlights, framing,
named project views, selection readback and PNG screenshots share that viewer.
These commands do not edit geometry, generate print layouts or export STL.

For a new assembly with several layouts, export a shared `design` graph:
source parts define geometry once, instances identify physical copies, and
groups/views reference those instances. Plate placements can override assembly
poses without copying the model. `synthcad docs design` prints the full contract;
the [public shared-design fixture](viewer/tests/agent-fixtures/shared-design)
shows four views sharing one entry. Automatic plate validation remains planned.

See the [agent CLI guide](docs/agent-cli.md) for commands, errors and session
lifecycle, and the [project/revision contract](docs/agent-contract.md) for optional
`synthcad.json` projects. Standalone `.js` scenes remain supported. Named sessions
are created through `open`; legacy direct scene launches keep their existing
viewer behavior. The complete review loop also passes on Linux under Xvfb/Mesa;
automated CI and distribution validation remain part of the v1 backlog.

## Viewer controls

- Left-drag to orbit, right-drag to pan (the model follows the mouse), and scroll to zoom.
- Click to select geometry. The bottom-right card switches between Part,
  Surface, Edge and Vertex modes. Surfaces distinguish planar faces from
  tessellated curved patches; edges and corners use an 8-logical-pixel snap
  radius and respect occlusion. Dragging more than 4 logical pixels orbits
  without selecting. Hidden parts cannot be picked.
- **Copy ref** (or **Ctrl+C** outside text entry) copies the selected part or
  feature reference. An agent reads the same context with `synthcad selection`
  or resolves a copied token with `synthcad reference TOKEN`. Geometric IDs are
  revision-local: editing source or changing layouts invalidates old tokens.
  Unsupported topology remains selectable in Part mode with diagnostics in
  `snapshot`. Model files and export flags are unaffected by picking.
- **Space** frames all currently visible parts from the front; **R** reloads the scene manually.
- **M** or the **Dimensions** button cycles Hover, All, and Off. In Hover mode,
  hover the small blue feature markers to see measurements in millimetres.
- **Parts** opens/closes the left tree. Search filters rows only, keeping ancestors.
  Expand groups with the arrow; click a name to select a part or group. A box
  outlines the visible selection. The **eye** icon controls visibility; the **export arrow** controls
  STL inclusion independently. Green icons are on, slashed icons are off, and
  amber icons with a minus badge indicate mixed groups. Hover for the current
  state and action. Group icons affect
  every descendant, including rows hidden by search.
- **Isolate** temporarily shows the selection. **Exit isolation** restores the previous
  visibility (changes made during isolation are temporary). **Show all**
  ends isolation and shows every part; **Frame selection** frames the visible selection.
- **P** or **Export STL** opens the STL dialog: all exportable parts (initial mode,
  including hidden parts) or only visible exportable parts. Edit the suggested
  `Downloads/<scene-name>.stl` path; existing files require **Replace file** confirmation.
  No selected parts means no file is written.

The parts panel is a subtle translucent overlay over the full-window scene.
Its background fits the visible tree rows and shrinks when groups collapse or
search narrows the results. Long trees scroll within 75% of the window height;
the uncovered area below remains available for camera interaction.
Opening or closing it preserves the camera projection. Panel clicks, scrolling,
text entry and modal interaction do not operate the camera or viewport shortcuts.
The top-right toolbar provides Fit all, Dimensions and shortcut help. **Ctrl+F**
opens and focuses search. Search/path fields support UTF-8 text, Backspace,
Delete, arrow/Home/End caret movement, Shift selection, Ctrl/Cmd+A and paste.
Escape cancels the export dialog. Controls use logical pixels and fonts are
rasterized for the current display scale. Only the component tree scrolls.

App-authored text is English; authored names, groups and annotations are kept
verbatim. Successful loads are logged, without a permanent viewport message.
Export success appears for four seconds and can be dismissed. Load errors stay
visible with expandable, scrollable diagnostics and Reload until a valid scene
loads. The last valid view stays visible and export remains disabled.

Export preserves the current arrangement, without creating or validating a print
layout. The suggested destination is `Downloads/<scene-name>.stl` (or
`synthcad.stl` for the built-in sample); an edited path is retained for the session.
Visibility, export overrides, selection and group expansion persist across reload
within the running session, using stable IDs; they reset after closing the viewer.
External references should start with `exportable:false`. Hiding them alone does
not exclude them from the default export mode.

Saving the scene or one of its imported modules reloads the model and annotations
together. Dimension mode survives reloads. See [API.md](API.md#dimension-annotations)
to add labels to other scenes.

Scenes can export `displayParts` for separate component colors. The Windows
viewer uses soft lighting and preserves CAD creases instead of smoothing across
sharp edges. With `displayParts`, the listed solids are also the source for
selective STL export. Scenes without this list retain the original single-solid
export. Invalid part metadata keeps the last valid view but disables export until
corrected. See [parts and colors](API.md#parts-groups-and-display-colors) for the
contract and PNG preview commands.

The viewer targets 60 FPS while focused and 15 FPS in the background. When
minimized, it skips drawing and processes events at 5 Hz. Scene files are checked
every 250 ms (up to roughly 400 ms between checks when minimized); **R** requests
an immediate reload. Frame pacing sleeps instead of keeping a CPU core busy.

For a window-free scene validation, run the built viewer with
`--check-scene scene.js`; it reports the bounding dimensions and valid annotations.




## Viewer checks

The agent review loop has [focused tests and a live acceptance script](docs/batch-1-validation.md),
including Windows/Linux transport checks. Its CLI and project contracts are
documented separately from the geometry API.
Geometric selection and native scaling have
[focused and native-window acceptance coverage](docs/batch-4-validation.md).

Build `dingcad_appearance_tests`, `dingcad_parts_tests`, `dingcad_camera_tests`
and `dingcad_dimension_tests` with CMake, then run the executables in the viewer
build directory. The parts suite covers nested trees, tri-state controls, search,
isolation, reload/reorder/add/remove, input capture and selective STL file safety.
Use `viewer/tests/parts_scene.js` with `--ui-preview` for visual checks of the
panel, selection, dimensions, export dialog, hidden geometry and resized viewport.

To diagnose scene startup without opening a window or exporting files:

```powershell
.\scripts\run-windows.ps1 --profile-scene .\local-scenes\scene.js
```

The output separates scene loading (including JavaScript/model evaluation) from
display mesh conversion, and reports part/triangle counts. These timings exclude
window creation and GPU upload; the interactive viewer logs those mesh/upload
costs separately. Use the same scene and build configuration when comparing
changes, without other heavy builds running at the same time. The profiler does
not cache, simplify or change geometry. An already-running Debug viewer needs to
be relaunched through the Release launcher to benefit from compiler optimization.

### Identity assets

`viewer/resources/synthcad.svg` is the editable layered-S icon source. Run
`python scripts/generate-icons.py` with Pillow installed to regenerate the PNG
sizes, multi-resolution Windows ICO and embedded runtime data. Normal builds do
not require Python or Pillow. Windows executables embed the ICO as a resource;
runtime icons and the panel mark use embedded data and work outside the repo.

### UI regression previews

`dingcad_viewer --ui-preview scene.js preview.png [mode]` captures the actual UI
in a hidden window. Additional modes include `empty`, `error` (simulated error
on the last valid model), `export-edit` (focused destination selection), `toast`,
`scale150` and `scale200`. Combine scale suffixes, for example
`export-scale200` or `small-scale150`. Scaling modes exercise the same logical
layout, font rasterization and hit-coordinate conversion as monitor DPI changes;
they do not change Windows display settings. `watch` leaves the hidden viewer
running for reload integration tests before taking its final screenshot.

On Windows, run `python scripts/test-windows-ui.py <path-to-dingcad_viewer.exe>`
for the embedded/runtime icon, window-title and real file-watch error/recovery
smoke test. It launches a hidden viewer from a temporary directory outside the
repository and removes only its own temporary fixture. An optional second path
chooses the temporary parent directory. It does not modify models or exports.
