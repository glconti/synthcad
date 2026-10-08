#include "camera_controls.h"
#include "rlgl.h"

#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void Require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}

void CheckDrag(Camera3D camera, Vector2 drag, int width, int height) {
  const Vector3 anchor = camera.target;
  const Vector2 before = GetWorldToScreenEx(anchor, camera, width, height);
  const float distance = Vector3Distance(camera.position, camera.target);
  const Vector3 offset = dingcad::PanCameraOffset(camera, drag, height);
  camera.position = Vector3Add(camera.position, offset);
  camera.target = Vector3Add(camera.target, offset);
  const Vector2 after = GetWorldToScreenEx(anchor, camera, width, height);
  const Vector2 movement = Vector2Subtract(after, before);
  if (drag.x != 0) Require(movement.x * drag.x > 0, "Horizontal pan must follow the mouse");
  else Require(std::abs(movement.x) < 0.01f, "Vertical pan must not move horizontally");
  if (drag.y != 0) Require(movement.y * drag.y > 0, "Vertical pan must follow the mouse");
  else Require(std::abs(movement.y) < 0.01f, "Horizontal pan must not move vertically");
  Require(std::abs(movement.x - drag.x) < 0.02f && std::abs(movement.y - drag.y) < 0.02f,
          "Pan must match mouse pixels at the target depth, across zoom and resize");
  Require(std::abs(Vector3Distance(camera.position, camera.target) - distance) < 0.001f,
          "Pan must preserve camera distance");
}

void CheckZoom() {
  using dingcad::ZoomCameraDistance;
  Require(ZoomCameraDistance(50, -1) > 50, "Zoom out must pass the old distance cap");
  Require(ZoomCameraDistance(4000, -1) > 4000, "Zoom out must pass the default clipping distance");
  Require(std::abs(ZoomCameraDistance(10, -1) - 11) < 0.0001f, "One step must scale by 1.1");
  const float fractional = ZoomCameraDistance(10, -0.25f);
  Require(fractional > 10 && fractional < 11, "Fractional scroll must produce fractional zoom");
  Require(std::abs(ZoomCameraDistance(fractional, 0.25f) - 10) < 0.0001f,
          "Opposite scroll steps must undo each other");
  Require(ZoomCameraDistance(10, 20) >= 1, "Fast zoom in must stay positive");
  Require(ZoomCameraDistance(10, 10000) == 1, "Fast zoom in must stop at the minimum");
  Require(ZoomCameraDistance(1, 1) == 1 && ZoomCameraDistance(1, -1) > 1,
          "Minimum distance must allow zooming back out");
  Require(ZoomCameraDistance(10, 0) == 10, "Zero scroll must preserve distance");
  Require(ZoomCameraDistance(10, -10000) == 10, "Overflowing scroll must preserve distance");
  for (float wheel : {std::numeric_limits<float>::infinity(),
                      -std::numeric_limits<float>::infinity(),
                      std::numeric_limits<float>::quiet_NaN()}) {
    Require(ZoomCameraDistance(10, wheel) == 10, "Non-finite scroll must preserve distance");
  }
  float distance = 10;
  for (int i = 0; i < 100; ++i) distance = ZoomCameraDistance(distance, -1);
  Require(distance > 100000 && std::isfinite(distance), "Repeated zoom out must remain uncapped and finite");
  const Camera3D camera{{distance * 0.5f, distance * 0.5f, distance * 0.70710678f},
                        {0, 0, 0}, {0, 1, 0}, 45, CAMERA_PERSPECTIVE};
  const Vector2 projected = GetWorldToScreenEx(camera.target, camera, 1280, 720);
  Require(std::isfinite(projected.x) && std::isfinite(projected.y),
          "Distant camera projection must remain finite");
  for (int i = 0; i < 100; ++i) distance = ZoomCameraDistance(distance, 1);
  Require(std::abs(distance - 10) < 0.001f, "Repeated zoom in must return to the original distance");
}

void CheckFraming() {
  for (const BoundingBox bounds : {BoundingBox{{0,-2,-4},{10,0.4f,0}},
                                  BoundingBox{{-25,-5,-13},{25,33,0}},
                                  BoundingBox{{100,200,300},{101,204,302}}}) {
    for (const Vector2 size : {Vector2{1280,720},Vector2{600,1000}}) {
      const auto camera=dingcad::FrameScene(bounds,static_cast<int>(size.x),static_cast<int>(size.y));
      Require(camera.position.z<camera.target.z,"Frame from front, not behind wall");
      const auto center=GetWorldToScreenEx(camera.target,camera,static_cast<int>(size.x),static_cast<int>(size.y));
      Require(std::abs(center.x-size.x/2)<0.01f&&std::abs(center.y-size.y/2)<0.01f,"Center translated models");
      for(float x:{bounds.min.x,bounds.max.x})for(float y:{bounds.min.y,bounds.max.y})for(float z:{bounds.min.z,bounds.max.z}) {
        const auto point=GetWorldToScreenEx({x,y,z},camera,static_cast<int>(size.x),static_cast<int>(size.y));
        Require(point.x>0&&point.x<size.x&&point.y>0&&point.y<size.y,"All model corners fit landscape and portrait views");
      }
    }
  }
}

void CheckClipBounds() {
  constexpr double defaultFar = 4000;
  const BoundingBox bounds{{-20, -0.1f, -20}, {20, 2, 20}};
  Require(dingcad::CameraFarClip({4, 4, 4}, bounds, defaultFar) == defaultFar,
          "Normal viewing must preserve the default far distance");
  // The latter box represents larger, offset geometry loaded on a scene reload.
  for (BoundingBox scene : {bounds, BoundingBox{{-1000, -200, 6000}, {7000, 3000, 8000}}}) {
    for (Vector3 position : {Vector3{0, 0, 5000}, Vector3{10000, -4000, 20000},
                             Vector3{-15000, 8000, -5000}}) {
      const float far = dingcad::CameraFarClip(position, scene, defaultFar);
      Require(std::isfinite(far) && far >= defaultFar, "Far plane must be finite and retain its baseline");
      for (float x : {scene.min.x, scene.max.x}) {
        for (float y : {scene.min.y, scene.max.y}) {
          for (float z : {scene.min.z, scene.max.z}) {
            const double distance = std::hypot(double(position.x) - x,
                                                double(position.y) - y,
                                                double(position.z) - z);
            Require(far >= distance * 1.099, "Far plane must contain every corner with a 10% margin");
          }
        }
      }
    }
  }
  Require(std::isfinite(dingcad::CameraFarClip({1e20f, 1e20f, 1e20f}, bounds, defaultFar)),
          "Far plane computation must not overflow when squaring large coordinates");
}

// Exercise actual OpenGL clipping beyond the old far plane, without input injection.
void RenderSnapshots(const std::filesystem::path &directory) {
  SetConfigFlags(FLAG_WINDOW_HIDDEN);
  InitWindow(640, 360, "DingCAD zoom render test");
  const float near = static_cast<float>(rlGetCullDistanceNear());
  const double defaultFar = rlGetCullDistanceFar();
  const BoundingBox bounds{{-600, -600, -600}, {600, 600, 600}};
  const Camera3D camera{{0, 0, 6000}, {0, 0, 0}, {0, 1, 0}, 45, CAMERA_PERSPECTIVE};
  const RenderTexture2D target = LoadRenderTexture(640, 360);
  std::filesystem::create_directories(directory);
  for (bool adaptive : {false, true}) {
    rlSetClipPlanes(near, adaptive ? dingcad::CameraFarClip(camera.position, bounds, defaultFar) : defaultFar);
    BeginTextureMode(target);
    ClearBackground(RAYWHITE);
    BeginMode3D(camera);
    DrawCube({0, 0, 0}, 1200, 1200, 1200, RED);
    EndMode3D();
    EndTextureMode();
    Image image = LoadImageFromTexture(target.texture);
    const Color center = GetImageColor(image, 320, 180);
    Require(adaptive ? center.r == RED.r && center.g == RED.g : center.r == RAYWHITE.r && center.g == RAYWHITE.g,
            "Distant model must be clipped with the default far plane and visible with adaptive clipping");
    ImageFlipVertical(&image);
    const bool saved = ExportImage(image, (directory / (adaptive ? "adaptive.png" : "default.png")).string().c_str());
    UnloadImage(image);
    Require(saved, "Zoom render snapshot must save successfully");
  }
  rlSetClipPlanes(near, defaultFar);
  UnloadRenderTexture(target);
  CloseWindow();
}
}  // namespace

int main(int argc, char **argv) {
  try {
    CheckZoom();
    CheckFraming();
    CheckClipBounds();
    const Vector3 views[] = {{0, 0, 10}, {10, 8, 10}, {-10, 8, 10},
                             {-10, -8, -10}, {10, 20, -10}, {1, 50, 1}};
    const Vector2 drags[] = {{8, 0}, {-8, 0}, {0, 8}, {0, -8}, {8, -8}, {120, 75}, {0, 0}};
    const int sizes[][2] = {{1280, 720}, {640, 360}, {800, 1200}, {2560, 1440}};
    for (const Vector3 position : views) {
      for (const int projection : {CAMERA_PERSPECTIVE, CAMERA_ORTHOGRAPHIC}) {
        for (const float zoom : {0.25f, 1.0f, 3.0f}) {
          for (const float fovy : {30.0f, 45.0f, 70.0f}) {
            const Camera3D camera{Vector3Scale(position, zoom), {0, 0, 0}, {0, 1, 0}, fovy, projection};
            for (const auto &size : sizes) {
              for (const Vector2 drag : drags) CheckDrag(camera, drag, size[0], size[1]);
            }
          }
        }
      }
    }
    if (argc == 3 && std::string(argv[1]) == "--render") RenderSnapshots(argv[2]);
    std::cout << "Camera framing, zoom, clipping, pan direction, pixel tracking, resize, and distance checks passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
