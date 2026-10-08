#pragma once
#include "feature_topology.h"
#include "part_tree.h"

namespace dingcad::selection {
enum class ReviewMode { Part, Surface, Edge, Vertex };
struct Hit {
  size_t partIndex;
  std::optional<Pick> feature;
  Vec3 position;
  Bounds bounds;
  double rayDistance;
};
// Inputs are logical viewport coordinates; results are scene/model XYZ in mm.
// The cache is built only from placed per-instance solids, never sourceSolid.
class GeometryPicker {
 public:
  void Reload(const PartTree&, const std::string& displayedRevision);
  std::optional<Hit> PickAt(const PartTree&, const Camera3D&, Vector2 logicalMouse,
                          int logicalWidth, int logicalHeight, ReviewMode,
                          double radiusPixels=8) const;
  const Topology* Get(size_t partIndex) const;
  const std::vector<std::string>& Diagnostics() const { return diagnostics_; }
 private:
  struct CachedPart {
    std::string id;
    std::shared_ptr<manifold::Manifold> solid;
    std::vector<Vec3> points;
    std::vector<std::array<uint32_t,3>> triangles;
    Bounds bounds;
    std::optional<Topology> topology;
  };
  std::vector<CachedPart> parts_;
  std::vector<std::string> diagnostics_;
};
} // namespace dingcad::selection
