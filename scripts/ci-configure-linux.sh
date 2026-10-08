#!/usr/bin/env bash
set -euo pipefail
# Run from the checkout root. The caller installs packages listed in docs/ci.md.
build_dir="${1:-out/build/ci-linux}"
dependency_dir="${2:-out/ci/deps}"
raylib_commit=dbc56a87da87d973a9c5baa4e7438a9d20121d28 # official 6.0 tag
mkdir -p "$dependency_dir"
dependency_dir="$(realpath "$dependency_dir")"
raylib_source="$dependency_dir/raylib-source"
if [[ ! -d "$raylib_source/.git" ]]; then
  git init "$raylib_source"
  git -C "$raylib_source" remote add origin https://github.com/raysan5/raylib.git
  git -C "$raylib_source" fetch --depth 1 origin "$raylib_commit"
  git -C "$raylib_source" checkout --detach FETCH_HEAD
fi
[[ "$(git -C "$raylib_source" rev-parse HEAD)" == "$raylib_commit" ]]
cmake -S "$raylib_source" -B "$dependency_dir/raylib-build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_EXAMPLES=OFF -DBUILD_SHARED_LIBS=OFF \
  -DPLATFORM=Desktop -DGLFW_BUILD_WAYLAND=OFF -DGLFW_BUILD_X11=ON \
  -DCMAKE_INSTALL_PREFIX="$dependency_dir/raylib-install"
cmake --build "$dependency_dir/raylib-build" --parallel 2
cmake --install "$dependency_dir/raylib-build"
cmake -S . -B "$build_dir" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$dependency_dir/raylib-install" -DSYNTHCAD_CUSTOM_FRAME_CONTROL=OFF
