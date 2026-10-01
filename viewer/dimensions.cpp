#include "dimensions.h"
#include "raymath.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>

namespace dingcad {
namespace {
struct JsValue {
  JSContext *ctx;
  JSValue value;
  ~JsValue() { JS_FreeValue(ctx, value); }
  JsValue(const JsValue &) = delete;
  JsValue &operator=(const JsValue &) = delete;
  JsValue(JSContext *context, JSValue owned) : ctx(context), value(owned) {}
};

void ClearException(JSContext *ctx) {
  JS_FreeValue(ctx, JS_GetException(ctx));
}

bool ReadString(JSContext *ctx, JSValueConst object, const char *key, std::string &out) {
  JsValue property(ctx, JS_GetPropertyStr(ctx, object, key));
  if (!JS_IsString(property.value)) return false;
  const char *text = JS_ToCString(ctx, property.value);
  if (!text) return false;
  out = text;
  JS_FreeCString(ctx, text);
  return true;
}

bool ReadNumber(JSContext *ctx, JSValueConst value, double &out) {
  return JS_IsNumber(value) && JS_ToFloat64(ctx, &out, value) == 0 && std::isfinite(out);
}

bool ReadPoint(JSContext *ctx, JSValueConst value, Vector3 &out) {
  if (!JS_IsArray(value)) return false;
  JsValue length(ctx, JS_GetPropertyStr(ctx, value, "length"));
  double count;
  if (!ReadNumber(ctx, length.value, count) || count != 3) return false;
  float components[3];
  for (uint32_t i = 0; i < 3; ++i) {
    JsValue item(ctx, JS_GetPropertyUint32(ctx, value, i));
    double number;
    if (!ReadNumber(ctx, item.value, number) ||
        std::abs(number) > std::numeric_limits<float>::max()) return false;
    components[i] = static_cast<float>(number);
  }
  out = {components[0], components[1], components[2]};
  return true;
}

bool ReadDimension(JSContext *ctx, JSValueConst object, Dimension &out) {
  if (!JS_IsObject(object) || JS_IsArray(object)) return false;
  std::string type;
  if (!ReadString(ctx, object, "type", type) ||
      !ReadString(ctx, object, "label", out.label) || out.label.empty()) return false;
  if (type == "linear") out.type = DimensionType::Linear;
  else if (type == "diameter") out.type = DimensionType::Diameter;
  else if (type == "radius") out.type = DimensionType::Radius;
  else return false;

  JsValue value(ctx, JS_GetPropertyStr(ctx, object, "value"));
  if (!ReadNumber(ctx, value.value, out.value) || out.value <= 0) return false;
  JsValue start(ctx, JS_GetPropertyStr(ctx, object, "start"));
  if (!ReadPoint(ctx, start.value, out.start)) return false;
  JsValue end(ctx, JS_GetPropertyStr(ctx, object, "end"));
  if (!ReadPoint(ctx, end.value, out.end)) return false;
  JsValue marker(ctx, JS_GetPropertyStr(ctx, object, "marker"));
  if (JS_IsUndefined(marker.value)) {
    out.marker = Vector3Add(Vector3Scale(out.start, 0.5f), Vector3Scale(out.end, 0.5f));
  } else if (!ReadPoint(ctx, marker.value, out.marker)) {
    return false;
  }
  return true;
}

bool Finite(Vector2 point) { return std::isfinite(point.x) && std::isfinite(point.y); }

// Fit UTF-8 text to a small/resized viewport without splitting a codepoint.
std::string FitLabel(std::string text, Font font, float size, float width) {
  if (MeasureTextEx(font, text.c_str(), size, 1).x <= width) return text;
  while (!text.empty()) {
    size_t last = text.size() - 1;
    while (last > 0 && (static_cast<unsigned char>(text[last]) & 0xc0) == 0x80) --last;
    text.resize(last);
    if (MeasureTextEx(font, (text + "...").c_str(), size, 1).x <= width) return text + "...";
  }
  return {};
}
}  // namespace

DimensionAnnotations ReadDimensions(JSContext *ctx, JSValueConst moduleNamespace) {
  DimensionAnnotations result;
  JsValue list(ctx, JS_GetPropertyStr(ctx, moduleNamespace, "dimensions"));
  if (JS_IsUndefined(list.value)) return result;
  if (!JS_IsArray(list.value)) {
    ClearException(ctx);
    result.diagnostics.push_back("dimensions must be an array; annotations skipped");
    return result;
  }
  JsValue length(ctx, JS_GetPropertyStr(ctx, list.value, "length"));
  uint32_t count = 0;
  if (JS_ToUint32(ctx, &count, length.value) < 0) {
    ClearException(ctx);
    result.diagnostics.push_back("Unable to read dimensions; annotations skipped");
    return result;
  }
  for (uint32_t i = 0; i < count; ++i) {
    JsValue item(ctx, JS_GetPropertyUint32(ctx, list.value, i));
    Dimension dimension;
    if (ReadDimension(ctx, item.value, dimension)) {
      result.entries.push_back(std::move(dimension));
    } else {
      ClearException(ctx);
      result.diagnostics.push_back("dimensions[" + std::to_string(i) +
          "]: expected type, nonempty label, positive finite value, and finite XYZ anchors; skipped");
    }
  }
  return result;
}

Vector3 CadToWorld(Vector3 point) {
  return {point.x * kSceneScale, point.z * kSceneScale, -point.y * kSceneScale};
}

std::string FormatDimension(const Dimension &dimension) {
  std::ostringstream number;
  number.imbue(std::locale::classic());
  number << std::fixed << std::setprecision(2) << dimension.value;
  std::string value = number.str();
  while (value.size() > 1 && value.back() == '0') value.pop_back();
  if (value.back() == '.') value.pop_back();
  const char *prefix = dimension.type == DimensionType::Diameter ? "\xc3\x98" :
                       dimension.type == DimensionType::Radius ? "R" : "";
  return dimension.label + ": " + prefix + value + " mm";
}

DimensionMode NextDimensionMode(DimensionMode mode) {
  switch (mode) {
    case DimensionMode::Off: return DimensionMode::Hover;
    case DimensionMode::Hover: return DimensionMode::All;
    default: return DimensionMode::Off;
  }
}

const char *DimensionModeName(DimensionMode mode) {
  switch (mode) {
    case DimensionMode::Off: return "Off";
    case DimensionMode::Hover: return "Hover";
    default: return "All";
  }
}

Rectangle DimensionButtonBounds(int width, int height) {
  const float buttonWidth = std::min(246.0f, std::max(0.0f, width - 16.0f));
  return {8, std::max(0.0f, height - 42.0f), buttonWidth, std::min(34.0f, float(height))};
}

void UpdateDimensionControls(DimensionControls &controls, Rectangle button,
                             Vector2 mouse, bool cycleKey, bool leftPressed, bool leftDown) {
  controls.overButton = CheckCollisionPointRec(mouse, button);
  if (leftPressed) controls.buttonGesture = controls.overButton;
  if (cycleKey || (leftPressed && controls.overButton)) controls.mode = NextDimensionMode(controls.mode);
  if (!leftDown) controls.buttonGesture = false;
}

std::vector<ProjectedDimension> ProjectDimensions(
    const std::vector<Dimension> &dimensions, Camera3D camera, int width, int height) {
  std::vector<ProjectedDimension> result;
  if (width <= 0 || height <= 0) return result;
  const Vector3 forward = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
  for (size_t i = 0; i < dimensions.size(); ++i) {
    Vector2 projected[3];
    const Vector3 points[] = {dimensions[i].start, dimensions[i].end, dimensions[i].marker};
    bool visible = true;
    for (int j = 0; j < 3; ++j) {
      const Vector3 world = CadToWorld(points[j]);
      if (Vector3DotProduct(Vector3Subtract(world, camera.position), forward) <= 0.001f) {
        visible = false;
        break;
      }
      projected[j] = GetWorldToScreenEx(world, camera, width, height);
      if (!Finite(projected[j])) { visible = false; break; }
    }
    if (visible && CheckCollisionPointRec(projected[2], {0, 0, float(width), float(height)})) {
      result.push_back({i, projected[0], projected[1], projected[2]});
    }
  }
  return result;
}

std::optional<size_t> FindHoveredDimension(
    const std::vector<ProjectedDimension> &dimensions, Vector2 mouse) {
  std::optional<size_t> closest;
  float best = 100.0f;
  for (const auto &dimension : dimensions) {
    const float distance = Vector2DistanceSqr(mouse, dimension.marker);
    if (distance <= 100 && (!closest || distance < best)) {
      closest = dimension.index;
      best = distance;
    }
  }
  return closest;
}

void DrawDimensions(const std::vector<Dimension> &dimensions, DimensionMode mode,
                    Camera3D camera, Font font, Vector2 mouse, bool suppressHover) {
  if (mode == DimensionMode::Off) return;
  const int width = GetScreenWidth(), height = GetScreenHeight();
  if (width < 32 || height < 32) return;
  const auto projected = ProjectDimensions(dimensions, camera, width, height);
  const auto hovered = suppressHover ? std::optional<size_t>{} :
      FindHoveredDimension(projected, mouse);
  const Color ink = {30, 105, 155, 255};
  const Rectangle button = DimensionButtonBounds(width, height);
  std::vector<Rectangle> occupied = {button};
  for (const auto &dimension : projected) {
    const bool active = mode == DimensionMode::All || hovered == dimension.index;
    DrawCircleV(dimension.marker, active ? 5 : 3, Fade(ink, active ? 1.0f : 0.55f));
    DrawCircleLines(int(dimension.marker.x), int(dimension.marker.y), active ? 7 : 5, Fade(WHITE, 0.9f));
    if (!active) continue;

    DrawLineEx(dimension.start, dimension.end, 1.5f, ink);
    const Vector2 direction = Vector2Normalize(Vector2Subtract(dimension.end, dimension.start));
    const Vector2 tick = {-direction.y * 5, direction.x * 5};
    DrawLineEx(Vector2Subtract(dimension.start, tick), Vector2Add(dimension.start, tick), 1.5f, ink);
    DrawLineEx(Vector2Subtract(dimension.end, tick), Vector2Add(dimension.end, tick), 1.5f, ink);

    const float fontSize = 18;
    const std::string text = FitLabel(FormatDimension(dimensions[dimension.index]), font, fontSize, width - 28.0f);
    const Vector2 size = MeasureTextEx(font, text.c_str(), fontSize, 1);
    Rectangle label = {dimension.marker.x + 12, dimension.marker.y - size.y - 12, size.x + 12, size.y + 10};
    label.x = Clamp(label.x, 8, std::max(8.0f, width - label.width - 8));
    label.y = Clamp(label.y, 8, std::max(8.0f, height - label.height - 8));
    // Move overlapping labels into the nearest free row in All mode.
    for (int attempt = 0; attempt < height / int(label.height + 4); ++attempt) {
      const bool collision = std::any_of(occupied.begin(), occupied.end(),
          [&](Rectangle other) { return CheckCollisionRecs(label, other); });
      if (!collision) break;
      label.y += label.height + 4;
      if (label.y + label.height > height - 8) label.y = 8;
    }
    occupied.push_back(label);
    const Vector2 leader = {Clamp(dimension.marker.x, label.x, label.x + label.width),
                           Clamp(dimension.marker.y, label.y, label.y + label.height)};
    DrawLineEx(dimension.marker, leader, 1, Fade(ink, 0.7f));
    DrawRectangleRec(label, Fade(RAYWHITE, 0.95f));
    DrawRectangleLinesEx(label, 1, Fade(ink, 0.4f));
    DrawTextEx(font, text.c_str(), {label.x + 6, label.y + 5}, fontSize, 1, ink);
  }
}

void DrawDimensionButton(DimensionMode mode, Font font) {
  const Rectangle bounds = DimensionButtonBounds(GetScreenWidth(), GetScreenHeight());
  const bool hovered = CheckCollisionPointRec(GetMousePosition(), bounds);
  DrawRectangleRec(bounds, hovered ? Color{220, 233, 241, 255} : Color{237, 242, 246, 255});
  DrawRectangleLinesEx(bounds, 1, Color{100, 130, 150, 255});
  const std::string text = FitLabel(std::string("Dimensions: ") + DimensionModeName(mode) + "  [M]",
                                  font, 18, std::max(0.0f, bounds.width - 16));
  DrawTextEx(font, text.c_str(), {bounds.x + 8, bounds.y + 8}, 18, 1, DARKGRAY);
}

}  // namespace dingcad
