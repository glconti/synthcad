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
  std::string id;
  std::string name;
  std::vector<std::string> group;
  bool exportable = true;
};
struct Appearance {
  bool specified=false;
  std::vector<DisplayPart> parts;
  std::string diagnostic;
};
// Optional display/export components. A diagnostic rejects the complete load:
// never fall back to exporting reference geometry after invalid metadata.
Appearance ReadAppearance(JSContext *ctx, JSValueConst moduleNamespace);
// Independent vertices preserve CAD creases and colors across touching parts.
// Properties 0..2 are XYZ (mm), 3..5 are RGB (0..255). Never used for STL export.
manifold::MeshGL DisplayMesh(const std::vector<DisplayPart> &parts);
}
