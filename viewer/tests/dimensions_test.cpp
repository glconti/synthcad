#include "dimensions.h"
#include "raymath.h"

#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace dingcad;

namespace {
void Require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}

DimensionAnnotations Parse(JSContext *ctx, const char *source) {
  JSValue object = JS_Eval(ctx, source, std::char_traits<char>::length(source),
                           "dimensions-test.js", JS_EVAL_TYPE_GLOBAL);
  Require(!JS_IsException(object), "Test fixture must evaluate");
  auto result = ReadDimensions(ctx, object);
  JS_FreeValue(ctx, object);
  Require(!JS_HasException(ctx), "Annotation errors must not leave pending JS exceptions");
  return result;
}

void CheckParser(JSContext *ctx) {
  Require(Parse(ctx, "({})").entries.empty(), "Missing annotations must be supported");
  Require(Parse(ctx, "({dimensions: []})").diagnostics.empty(), "Empty annotations must be valid");
  Require(Parse(ctx, "({dimensions: 'wrong'})").diagnostics.size() == 1, "Reject non-array exports");
  Require(Parse(ctx, "({get dimensions(){throw Error('bad getter')}})").diagnostics.size() == 1,
          "Handle throwing dimensions getter");
  auto valid = Parse(ctx, R"js(({dimensions: [
    {type:'linear',label:'Width',value:120,start:[-60,-48,0],end:[60,-48,0]},
    {type:'diameter',label:'Hole',value:3.2,start:[0,0,2],end:[3.2,0,2],marker:[1.6,0,2]},
    {type:'radius',label:'Corner',value:6,start:[0,0,10],end:[6,0,10]}
  ]}))js");
  Require(valid.entries.size() == 3 && valid.diagnostics.empty(), "Read all dimension types");
  Require(valid.entries[0].marker.x == 0 && valid.entries[0].marker.y == -48,
          "Default marker must be the midpoint");
  Require(valid.entries[1].marker.x == 1.6f, "Explicit marker must be preserved");
  Require(FormatDimension(valid.entries[0]) == "Width: 120 mm", "Trim trailing zeroes");
  Require(FormatDimension(valid.entries[1]) == "Hole: \xc3\x98" "3.2 mm", "Diameter prefix and decimals");
  Require(FormatDimension(valid.entries[2]) == "Corner: R6 mm", "Radius prefix");
  valid.entries[0].value = 12.345;
  Require(FormatDimension(valid.entries[0]) == "Width: 12.35 mm", "Round to two decimals");

  auto invalid = Parse(ctx, R"js((() => {
    const d = {type:'linear',label:'Width',value:10,start:[0,0,0],end:[10,0,0]};
    return {dimensions: [null, {...d,type:'unknown'}, {...d,value:NaN}, {...d,value:Infinity},
      {...d,value:'10'}, {...d,value:-1}, {...d,label:''}, {...d,start:[0,0]},
      {...d,end:[0,0,Infinity]}, {...d,marker:null}, {...d,get value(){throw Error('x')}},
      {...d,get start(){throw Error('x')}}, d]};
  })())js");
  Require(invalid.entries.size() == 1 && invalid.diagnostics.size() == 12,
          "Skip invalid entries while retaining the following valid entry");
}

void CheckProjectionAndHover() {
  const Vector3 converted = CadToWorld({120, 80, 10});
  Require(converted.x == 12 && converted.y == 1 && converted.z == -8, "CAD axes and millimetre scale");
  Camera3D camera{{0, 0, 10}, {0, 0, 0}, {0, 1, 0}, 45, CAMERA_PERSPECTIVE};
  Dimension dimension{DimensionType::Linear, "Test", 10, {-5, 0, 0}, {5, 0, 0}, {0, 0, 0}};
  const auto projected = ProjectDimensions({dimension}, camera, 1280, 720);
  Require(projected.size() == 1 && std::abs(projected[0].marker.x - 640) < 0.1f &&
          std::abs(projected[0].marker.y - 360) < 0.1f, "Project to viewport centre");
  Require(FindHoveredDimension(projected, {650, 360}) == 0, "Include 10 pixel hover boundary");
  Require(!FindHoveredDimension(projected, {650.1f, 360}), "Exclude beyond hover radius");
  auto overlapping = projected;
  overlapping.push_back({7, {}, {}, {643, 360}});
  Require(FindHoveredDimension(overlapping, {644, 360}) == 7, "Nearest overlapping marker wins");
  Require(FindHoveredDimension(overlapping, {640, 360}) == 0, "Select nearest original marker");
  const auto resized = ProjectDimensions({dimension}, camera, 640, 360);
  Require(resized.size() == 1 && std::abs(resized[0].marker.x - 320) < 0.1f, "Projection follows resize");
  dimension.start = dimension.end = dimension.marker = {0, -200, 0};
  Require(ProjectDimensions({dimension}, camera, 1280, 720).empty(), "Hide anchors behind camera");
  dimension.start = dimension.end = dimension.marker = {100000, 0, 0};
  Require(ProjectDimensions({dimension}, camera, 1280, 720).empty(), "Hide offscreen markers");
  Require(NextDimensionMode(DimensionMode::Hover) == DimensionMode::All &&
          NextDimensionMode(DimensionMode::All) == DimensionMode::Off &&
          NextDimensionMode(DimensionMode::Off) == DimensionMode::Hover, "Cycle all modes");
  Require(CheckCollisionPointRec({1200, 700}, DimensionButtonBounds(1280, 720)), "Button hit area");

  DimensionControls controls;
  const auto button = DimensionButtonBounds(1280, 720);
  UpdateDimensionControls(controls, button, {1200, 700}, false, true, true);
  Require(controls.mode == DimensionMode::All && controls.buttonGesture, "Button click cycles and consumes orbit gesture");
  UpdateDimensionControls(controls, button, {500, 400}, false, false, true);
  Require(controls.mode == DimensionMode::All && controls.buttonGesture, "Dragging out of button must still suppress orbit");
  UpdateDimensionControls(controls, button, {500, 400}, false, false, false);
  Require(!controls.buttonGesture, "Releasing clears button gesture");
  UpdateDimensionControls(controls, button, {500, 400}, false, true, true);
  Require(controls.mode == DimensionMode::All && !controls.buttonGesture, "Normal viewport drag preserves mode and orbit");
  UpdateDimensionControls(controls, button, {500, 400}, true, false, false);
  Require(controls.mode == DimensionMode::Off, "Keyboard shortcut cycles mode");
}

// Deterministic visual checks of the actual overlay renderer; no mouse injection.
void RenderSnapshots(const std::filesystem::path &directory) {
  SetConfigFlags(FLAG_WINDOW_HIDDEN);
  InitWindow(1000, 700, "DingCAD dimension render test");
  Camera3D camera{{10, 8, 10}, {0, 0.5f, 0}, {0, 1, 0}, 45, CAMERA_PERSPECTIVE};
  const std::vector<Dimension> dimensions = {
    {DimensionType::Linear, "Width", 120, {-60, -48, 0}, {60, -48, 0}, {0, -48, 0}},
    {DimensionType::Diameter, "Mounting hole 1", 3.2, {-47.6f, -26, 2}, {-44.4f, -26, 2}, {-46, -26, 2}},
    {DimensionType::Radius, "Outside corner", 6, {54, 34, 10}, {58.24f, 38.24f, 10}, {58.24f, 38.24f, 10}},
  };
  const auto projected = ProjectDimensions(dimensions, camera, 1000, 700);
  Require(projected.size() == 3, "All snapshot markers must be visible");
  const Vector2 mouse = projected[1].marker;
  std::filesystem::create_directories(directory);
  for (auto mode : {DimensionMode::Off, DimensionMode::Hover, DimensionMode::All}) {
    BeginDrawing();
    ClearBackground(RAYWHITE);
    BeginMode3D(camera);
    DrawCube({0, 0.5f, 0}, 12, 1, 8, {210, 210, 220, 255});
    DrawCubeWires({0, 0.5f, 0}, 12, 1, 8, GRAY);
    EndMode3D();
    DrawDimensions(dimensions, mode, camera, GetFontDefault(), mouse, false);
    DrawDimensionButton(mode, GetFontDefault());
    EndDrawing();
    Image image = LoadImageFromScreen();
    const bool saved = ExportImage(image,
        (directory / (std::string(DimensionModeName(mode)) + ".png")).string().c_str());
    UnloadImage(image);
    Require(saved, "Overlay snapshot must save successfully");
    SwapScreenBuffer();
    PollInputEvents();
  }
  CloseWindow();
}
}  // namespace

int main(int argc, char **argv) {
  JSRuntime *runtime = JS_NewRuntime();
  JSContext *ctx = JS_NewContext(runtime);
  int result = 0;
  try {
    CheckParser(ctx);
    CheckProjectionAndHover();
    if (argc == 3 && std::string(argv[1]) == "--render") RenderSnapshots(argv[2]);
    std::cout << "Dimension parsing, formatting, projection, hover, and mode checks passed\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    result = 1;
  }
  JS_FreeContext(ctx);
  JS_FreeRuntime(runtime);
  return result;
}
