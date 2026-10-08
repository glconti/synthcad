#pragma once
#include "manifold/manifold.h"
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace dingcad::selection {
struct Vec3 { double x=0, y=0, z=0; };
struct Bounds { Vec3 min, max; };
enum class Kind { PlanarFace, CurvedPatch, Edge, Vertex };
enum class PickMode { Surface, Edge, Vertex };
struct Options {
  // XYZ is in model mm. Effective weld/plane tolerance is the larger value.
  double absoluteTolerance=1e-6;
  double relativeTolerance=1e-7;
  double sharpAngleDegrees=30;
};
struct Reference {
  std::string partId, revision;
  uint64_t topologyKey=0;
  Kind kind=Kind::PlanarFace;
  uint32_t id=0;
};
struct Feature {
  Kind kind=Kind::PlanarFace;
  Bounds bounds;
  // Surfaces index Topology::Triangles(); edges contain Topology::Points()
  // vertex chains; vertices contain one point index. Closed edges repeat their
  // first vertex. These indices do not address the original MeshGL buffers.
  std::vector<uint32_t> members;
  Vec3 normal;
};
struct Pick {
  Reference reference;
  Vec3 position, normal;
  bool hasNormal=false;
  Bounds bounds;
  double rayDistance=0;
};
// CPU-only, revision-local prototype. Build separately for each owning part;
// never weld touching parts or use the flattened multicolor DisplayMesh as one part.
class Topology {
 public:
  static Topology Build(const manifold::MeshGL&, std::string partId,
                        std::string displayedRevision, Options = {});
  bool Contains(const Reference&) const;
  std::optional<Pick> RayPick(Vec3 origin, Vec3 direction,
                              PickMode=PickMode::Surface,
                              double featureRadiusMm=0.5) const;
  const std::vector<Feature>& Features() const { return features_; }
  // Snapshot geometry for feature overlays and screen-space picking. Triangle
  // entries index Points(); both arrays share the lifetime of this Topology.
  const std::vector<Vec3>& Points() const { return points_; }
  const std::vector<std::array<uint32_t,3>>& Triangles() const { return triangles_; }
  double Tolerance() const { return tolerance_; }
  size_t TriangleCount() const { return triangles_.size(); }
 private:
  std::string partId_, revision_;
  uint64_t key_=0;
  double tolerance_=0;
  std::vector<Vec3> points_, normals_;
  std::vector<std::array<uint32_t,3>> triangles_;
  std::vector<uint32_t> triangleFeature_;
  std::vector<Feature> features_;
};
} // namespace dingcad::selection
