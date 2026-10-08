# Release validation CI

`.github/workflows/ci.yml` builds the viewer and CLI in Release on Windows 2022
x64/MSVC and Ubuntu 24.04 x64/GCC. Pushes, pull requests and manual dispatch run
both jobs. Each builds the canonical `synthcad_tests` CMake target, then runs
CTest sequentially with assertions enabled in test targets. CTest is the test
inventory; `scripts/ci-test.py` rejects an inventory with fewer than 26 suites
or missing the appearance, parts, camera or dimensions legacy suites.

Both jobs require standalone embedded guidance and the public session, shared
design, geometric selection, guided pick, project overview, manufacturing
review, export workflow, mesh reload and physical feedback acceptance scripts.
These copy public fixtures into temporary projects and own their viewer
processes. They do not use personal scenes, a printer, cloud accounts or an
installed slicer. Windows additionally checks the embedded seven-size icon and
runs the native icon/watched-source reload smoke test. Native failures fail the
job; no native suite is silently optional.

Linux native tests run on Xvfb at 1920×1080 with Mesa software rendering and
`LIBGL_ALWAYS_SOFTWARE=1`. This validates the X11 input path, not native Wayland.
An additional required Xvfb gate uses a 3000×2000 display and sets actual X11
`Xft.dpi` resources to 144 and 192. Geometric-selection and guided-pick native
input tests both assert viewer scale 1.5 at 144 DPI and scale 2 at 192 DPI.
The runner restores the original Xresources in a `finally` block. This tests
input mapping at 150% and 200%; physical-monitor DPI remains unverified.
Windows native tests require an interactive desktop and usable OpenGL context;
whether the hosted Windows image provides the needed OpenGL support remains a
first-run question. A hosted graphics failure must be investigated and recorded,
not replaced with a passing skipped test. Existing local Windows and isolated
Ubuntu/Xvfb results are evidence for those environments, not successful GitHub
Actions runs. SC19 remains open until the actual workflow is run and reviewed.

## Dependencies and pins

The checkout includes the exact Manifold and QuickJS submodule revisions. Windows
uses the registry baseline in `vcpkg-configuration.json` for both the vcpkg tool
checkout and its ports, with the `x64-windows` triplet. Its Visual Studio generator
selects MSVC without changing an existing developer shell or local build tree.
The default dedicated build directory is `out/build/ci-windows`.

Linux builds static raylib 6.0 from official commit
[`dbc56a87da87d973a9c5baa4e7438a9d20121d28`](https://github.com/raysan5/raylib/tree/dbc56a87da87d973a9c5baa4e7438a9d20121d28),
with X11 enabled and Wayland disabled. Source, build and installation live under
`out/ci/deps`, and the viewer builds under `out/build/ci-linux`.
`SYNTHCAD_CUSTOM_FRAME_CONTROL=OFF` matches this default raylib build. Ubuntu
system dependencies come from the Ubuntu 24.04 apt repositories: these package
versions and the hosted image patch level can change. This is a pinned source
and dependency-baseline build, not a claim of byte-for-byte reproducibility.

Actions are pinned to official commits, verified against their official tag refs:

| Action | Release | Commit |
| --- | --- | --- |
| [actions/checkout](https://github.com/actions/checkout/tree/11bd71901bbe5b1630ceea73d27597364c9af683) | v4.2.2 | `11bd71901bbe5b1630ceea73d27597364c9af683` |
| [actions/upload-artifact](https://github.com/actions/upload-artifact/tree/ea165f8d65b6e75b540449e92b4886f43607fa02) | v4.6.2 | `ea165f8d65b6e75b540449e92b4886f43607fa02` |
| [actions/setup-python](https://github.com/actions/setup-python/tree/a26af69be951a213d495a4c3e4e4022e16d87065) | v5.6.0 | `a26af69be951a213d495a4c3e4e4022e16d87065` |

Windows selects Python 3.12; its patch version can change with the runtime
catalog. Ubuntu uses its system Python 3.12.

The workflow grants only repository read permission, disables persisted checkout
credentials, uses no project secrets and uploads only text test logs plus the
CTest inventory for 14 days. It does not upload binaries, dependency packages,
exports or release archives. Binary distribution awaits the repository's license
decision and is outside this workflow.

## Run locally

From a public checkout with initialized submodules on Ubuntu 24.04:

```sh
sudo apt-get update
sudo apt-get install -y build-essential cmake ninja-build pkg-config git python3 \
  libtbb-dev libassimp-dev nlohmann-json3-dev libasound2-dev libgl1-mesa-dev \
  libx11-dev libxrandr-dev libxi-dev libxcursor-dev libxinerama-dev \
  xvfb xauth xdotool xclip x11-xserver-utils mesa-utils fonts-dejavu-core tini
bash scripts/ci-configure-linux.sh
python3 scripts/ci-test.py --build-dir out/build/ci-linux --phase build
python3 scripts/ci-test.py --build-dir out/build/ci-linux --phase unit
python3 scripts/ci-test.py --build-dir out/build/ci-linux --phase guidance
LIBGL_ALWAYS_SOFTWARE=1 tini -s -- xvfb-run -a -s '-screen 0 1920x1080x24 -noreset' \
  python3 scripts/ci-test.py --build-dir out/build/ci-linux --phase native
LIBGL_ALWAYS_SOFTWARE=1 tini -s -- xvfb-run -a -s '-screen 0 3000x2000x24 -noreset' \
  python3 scripts/ci-test.py --build-dir out/build/ci-linux --phase dpi
```

On Windows with Visual Studio 2022 C++ desktop tools, CMake, Git and Python 3.10+
available, use PowerShell from the checkout root:

```powershell
./scripts/ci-configure-windows.ps1
python scripts/ci-test.py --build-dir out/build/ci-windows --phase build
python scripts/ci-test.py --build-dir out/build/ci-windows --phase unit
python scripts/ci-test.py --build-dir out/build/ci-windows --phase guidance
python scripts/ci-test.py --build-dir out/build/ci-windows --phase native
```

Each command stops on failure. Build and test output is saved under
`out/ci/logs`; native harnesses run one at a time to avoid input contention.
The scripts accept separate build/dependency/log paths for isolated local runs.
They require no ignored helper scripts or pre-existing local dependency builds.
