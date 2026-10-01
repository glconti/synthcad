## Dingcad

Dingcad is a live reloading program that is a replacement for openscad. Becuase openscad kind of really sucks. Try ./run.sh and then updating scene.js

This is dingcad. Dependencies: raylib, manifoldcad, and quickjs. Ask an LLM how to set up raylib on your system. For the quickjs and manifoldcad; you can 

```
git submodule update --init --recursive
```

This repository is mostly autonomously written by an LLM that I've lazily prompted while watching youtube and hanging out with my family.

There are no docs. Just read the code.

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
- **Space** resets the camera; **R** reloads the scene manually.
- **M** or the **Dimensions** button cycles Hover, All, and Off. In Hover mode,
  hover the small blue feature markers to see measurements in millimetres.
- **P** exports the solid to `Downloads/ding.stl`, without dimension annotations.

Saving the scene or one of its imported modules reloads the model and annotations
together. Dimension mode survives reloads. See [API.md](API.md#dimension-annotations)
to add labels to other scenes.

The viewer targets 60 FPS while focused and 15 FPS in the background. When
minimized, it skips drawing and processes events at 5 Hz. Scene files are checked
every 250 ms (up to roughly 400 ms between checks when minimized); **R** requests
an immediate reload. Frame pacing sleeps instead of keeping a CPU core busy.

For a window-free scene validation, run the built viewer with
`--check-scene scene.js`; it reports the bounding dimensions and valid annotations.



