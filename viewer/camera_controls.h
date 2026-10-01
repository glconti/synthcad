#pragma once

#include "raylib.h"
#include "raymath.h"

#include <algorithm>
#include <cmath>

namespace dingcad {

inline Vector3 PanCameraOffset(const Camera3D &camera, Vector2 mouseDelta, int viewportHeight) {
  const Vector3 forward = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
  const Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, camera.up));
  const Vector3 up = Vector3CrossProduct(right, forward);
  // Match one mouse pixel to one projected pixel at the camera target's depth.
  const float visibleHeight = camera.projection == CAMERA_ORTHOGRAPHIC
      ? camera.fovy
      : 2.0f * Vector3Distance(camera.position, camera.target) *
            std::tan(camera.fovy * DEG2RAD * 0.5f);
  const float scale = visibleHeight / static_cast<float>(std::max(viewportHeight, 1));
  // Move the camera opposite the drag so the model follows the mouse on screen.
  // Screen Y increases downward, whereas camera up points upward.
  return Vector3Add(Vector3Scale(right, -mouseDelta.x * scale),
                    Vector3Scale(up, mouseDelta.y * scale));
}

}  // namespace dingcad
