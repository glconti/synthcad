#include "camera_controls.h"

#include <cmath>
#include <iostream>
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
}  // namespace

int main() {
  try {
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
    std::cout << "Camera pan direction, pixel tracking, zoom, resize, and distance checks passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
