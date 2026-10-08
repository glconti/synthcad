#pragma once
#include <memory>
#include <string>
#include <vector>
#include "raylib.h"
#include "manifold/manifold.h"
extern "C" {
#include "quickjs.h"
}

namespace dingcad {
struct DisplayPart {
  std::shared_ptr<manifold::Manifold> solid;
  Color color;
};
struct Appearance {
  std::vector<DisplayPart> parts;
  std::string diagnostic;
};
// Optional visual-only solid/color pairs. A malformed list falls back as a whole
// to the actual scene, rather than hiding geometry with a partially valid list.
Appearance ReadAppearance(JSContext *ctx, JSValueConst moduleNamespace);
// Independent vertices preserve CAD creases and colors across touching parts.
// Properties 0..2 are XYZ (mm), 3..5 are RGB (0..255). Never used for STL export.
manifold::MeshGL DisplayMesh(const std::vector<DisplayPart> &parts);
}
