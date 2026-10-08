#pragma once

#include "raylib.h"
#include "raymath.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace dingcad {

// Pass model bounds, not grid bounds: small objects should still fill the view.
inline Camera3D FrameScene(BoundingBox bounds, int width, int height) {
  const Vector3 size = Vector3Subtract(bounds.max, bounds.min);
  const Vector3 target = Vector3Scale(Vector3Add(bounds.min, bounds.max), 0.5f);
  const float halfFov = 22.5f * DEG2RAD;
  const float aspect = static_cast<float>(std::max(1, width)) / std::max(1, height);
  const float limitingFov = std::min(halfFov, atanf(tanf(halfFov) * aspect));
  const float distance = std::max(0.5f, Vector3Length(size) * 0.55f / sinf(limitingFov));
  const Vector3 direction = Vector3Normalize({0.45f, 0.75f, -1.5f});
  return {Vector3Add(target, Vector3Scale(direction, distance)), target, {0,1,0}, 45, CAMERA_PERSPECTIVE};
}

inline float ZoomCameraDistance(float distance, float wheel) {
  if (!std::isfinite(wheel)) return distance;
  // Exponential steps support fractional/fast scrolling without crossing zero.
  const double next = static_cast<double>(distance) * std::pow(1.1, -static_cast<double>(wheel));
  if (!std::isfinite(next) || next > std::numeric_limits<float>::max()) return distance;
  return static_cast<float>(std::max(1.0, next));
}

inline float CameraFarClip(Vector3 position, BoundingBox bounds, double defaultFar) {
  // The farthest AABB corner bounds every point's view depth, even after panning.
  // Work in double precision so squaring distant float coordinates cannot overflow.
  const double x = std::max(std::abs(static_cast<double>(position.x) - bounds.min.x),
                            std::abs(static_cast<double>(position.x) - bounds.max.x));
  const double y = std::max(std::abs(static_cast<double>(position.y) - bounds.min.y),
                            std::abs(static_cast<double>(position.y) - bounds.max.y));
  const double z = std::max(std::abs(static_cast<double>(position.z) - bounds.min.z),
                            std::abs(static_cast<double>(position.z) - bounds.max.z));
  const double far = std::max(defaultFar, 1.1 * std::hypot(x, y, z));
  return static_cast<float>(std::min(far, static_cast<double>(std::numeric_limits<float>::max())));
}

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
