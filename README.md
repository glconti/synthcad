## Dingcad

Dingcad is a live reloading program that is a replacement for openscad. Becuase openscad kind of really sucks. Try ./run.sh and then updating scene.js

This is dingcad. Dependencies: raylib, manifoldcad, and quickjs. Ask an LLM how to set up raylib on your system. For the quickjs and manifoldcad; you can 

```
git submodule update --init --recursive
```

This repository is mostly autonomously written by an LLM that I've lazily prompted while watching youtube and hanging out with my family.

See [API.md](API.md) for the JavaScript modelling API and scene metadata.

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

To work in VS Code, open this folder, install the recommended extensions, and select
the `windows-x64-debug` CMake preset. The **Debug dingcad** launch configuration
builds and starts the current CMake target with `scene.js`.

`scene.js` retains the upstream example assembly. To run a different scene, pass
its path to the launcher:

```powershell
.\scripts\run-windows.ps1 .\local-scenes\scene.js
```

Keep personal scene scripts, meshes, exports, and design notes in `local-scenes/`.
That directory is ignored by Git and is not part of the public development branch.
Preserve relative imports when moving a scene and its supporting modules there.
Avoid force-adding personal files; `.gitignore` is a safeguard, not access control.

## Viewer controls

- Left-drag to orbit, right-drag to pan (the model follows the mouse), and scroll to zoom.
- **Space** frames all currently visible parts from the front; **R** reloads the scene manually.
- **M** or the **Dimensions** button cycles Hover, All, and Off. In Hover mode,
  hover the small blue feature markers to see measurements in millimetres.
- **Parti** opens/closes the left tree. Search filters rows only, keeping ancestors.
  Expand groups with the arrow; click a name to select a part or group. A box
  outlines the visible selection. **V** controls visibility; **STL** controls
  exportability independently. Group checkboxes show all/none/mixed and affect
  every descendant, including rows hidden by search.
- **Isola** temporarily shows the selection. **Esci** restores the previous
  visibility (changes made during isolation are temporary). **Mostra tutto**
  ends isolation and shows every part; **Inquadra** frames the visible selection.
- **P** or **Esporta** opens the STL dialog: all exportable parts (initial mode,
  including hidden parts) or only visible exportable parts. Edit the suggested
  `Downloads/ding.stl` path; existing files require **Sostituisci** confirmation.
  No selected parts means no file is written.

The parts panel is a dark translucent overlay over the full-window scene.
Opening or closing it preserves the camera projection. Panel clicks, scrolling,
text entry and modal interaction do not operate the camera or viewport shortcuts.
The Dimensions button stays accessible at the bottom right. Search/path fields
support typing, Backspace, Ctrl/Cmd+A and paste. Escape cancels the export dialog.
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

Build `dingcad_appearance_tests`, `dingcad_parts_tests`, `dingcad_camera_tests`
and `dingcad_dimension_tests` with CMake, then run the executables in the viewer
build directory. The parts suite covers nested trees, tri-state controls, search,
isolation, reload/reorder/add/remove, input capture and selective STL file safety.
Use `viewer/tests/parts_scene.js` with `--ui-preview` for visual checks of the
panel, selection, dimensions, export dialog, hidden geometry and resized viewport.
