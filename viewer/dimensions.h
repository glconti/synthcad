#pragma once

#include "raylib.h"
extern "C" {
#include "quickjs.h"
}

#include <optional>
#include <string>
#include <vector>

namespace dingcad {

constexpr float kSceneScale = 0.1f;
enum class DimensionType { Linear, Diameter, Radius };
enum class DimensionMode { Off, Hover, All };

struct DimensionControls {
  DimensionMode mode = DimensionMode::Hover;
  bool buttonGesture = false;
  bool overButton = false;
};

struct Dimension {
  DimensionType type = DimensionType::Linear;
  std::string label;
  double value = 0;
  Vector3 start{}, end{}, marker{};  // CAD coordinates, millimetres, Z up
};

struct DimensionAnnotations {
  std::vector<Dimension> entries;
  std::vector<std::string> diagnostics;
};

struct ProjectedDimension {
  size_t index;
  Vector2 start, end, marker;
};

DimensionAnnotations ReadDimensions(JSContext *ctx, JSValueConst moduleNamespace);
Vector3 CadToWorld(Vector3 point);
std::string FormatDimension(const Dimension &dimension);
DimensionMode NextDimensionMode(DimensionMode mode);
const char *DimensionModeName(DimensionMode mode);
Rectangle DimensionButtonBounds(int width, int height);
void UpdateDimensionControls(DimensionControls &controls, Rectangle button,
                             Vector2 mouse, bool cycleKey, bool leftPressed, bool leftDown);
std::vector<ProjectedDimension> ProjectDimensions(
    const std::vector<Dimension> &dimensions, Camera3D camera, int width, int height);
std::optional<size_t> FindHoveredDimension(
    const std::vector<ProjectedDimension> &dimensions, Vector2 mouse);
void DrawDimensions(const std::vector<Dimension> &dimensions, DimensionMode mode,
                    Camera3D camera, Font font, Vector2 mouse, bool suppressHover,
                    int width=-1,int height=-1);
void DrawDimensionButton(DimensionMode mode, Font font,int width=-1,int height=-1,
                         Vector2 mouse={-1,-1});

}  // namespace dingcad
