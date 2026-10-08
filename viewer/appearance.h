#pragma once
#include <array>
#include <memory>
#include <string>
#include <vector>
#include "raylib.h"
#include "manifold/manifold.h"
extern "C" {
#include "quickjs.h"
}

namespace dingcad {
struct GroupLabel { std::string id, name; };
struct DisplayPart {
  std::shared_ptr<manifold::Manifold> solid;
  Color color;
  std::string id;
  std::string name;
  std::vector<std::string> group;
  bool exportable = true;
  std::string sourcePartId;
  std::array<double,3> rotation{0,0,0}, translation{0,0,0};
  std::vector<std::vector<GroupLabel>> memberships;
  // Retain one shared, unplaced source for instance-aware checks and exports.
  std::shared_ptr<manifold::Manifold> sourceSolid;
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
