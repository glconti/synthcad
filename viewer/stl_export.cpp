#include "stl_export.h"
#include <fstream>
#include <array>
#include <cmath>
#include <cstring>
#include <algorithm>
namespace dingcad {
namespace {
struct Vec3f {
  float x;
  float y;
  float z;
};

Vec3f FetchVertex(const manifold::MeshGL &mesh, uint32_t index) {
  const size_t offset = static_cast<size_t>(index) * mesh.numProp;
  return {
      static_cast<float>(mesh.vertProperties[offset + 0]),
      static_cast<float>(mesh.vertProperties[offset + 1]),
      static_cast<float>(mesh.vertProperties[offset + 2])
  };
}

Vec3f Subtract(const Vec3f &a, const Vec3f &b) {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Vec3f Cross(const Vec3f &a, const Vec3f &b) {
  return {a.y * b.z - a.z * b.y,
          a.z * b.x - a.x * b.z,
          a.x * b.y - a.y * b.x};
}

Vec3f Normalize(const Vec3f &v) {
  const float lenSq = v.x * v.x + v.y * v.y + v.z * v.z;
  if (lenSq <= 0.0f) return {0.0f, 0.0f, 0.0f};
  const float invLen = 1.0f / std::sqrt(lenSq);
  return {v.x * invLen, v.y * invLen, v.z * invLen};
}

bool WriteMeshAsBinaryStl(const manifold::MeshGL &mesh,
                          const std::filesystem::path &path,
                          std::string &error) {
  const uint32_t triCount = static_cast<uint32_t>(mesh.NumTri());
  if (triCount == 0) {
    error = "Export failed: mesh is empty";
    return false;
  }

  std::ofstream out(path, std::ios::binary);
  if (!out) {
    error = "Export failed: cannot open " + path.string();
    return false;
  }

  std::array<char, 80> header{};
  constexpr const char kHeader[] = "dingcad export";
  std::memcpy(header.data(), kHeader, std::min(header.size(), std::strlen(kHeader)));
  out.write(header.data(), header.size());
  out.write(reinterpret_cast<const char *>(&triCount), sizeof(uint32_t));

  for (uint32_t tri = 0; tri < triCount; ++tri) {
    const uint32_t i0 = mesh.triVerts[tri * 3 + 0];
    const uint32_t i1 = mesh.triVerts[tri * 3 + 1];
    const uint32_t i2 = mesh.triVerts[tri * 3 + 2];

    const Vec3f v0 = FetchVertex(mesh, i0);
    const Vec3f v1 = FetchVertex(mesh, i1);
    const Vec3f v2 = FetchVertex(mesh, i2);

    const Vec3f normal = Normalize(Cross(Subtract(v1, v0), Subtract(v2, v0)));

    out.write(reinterpret_cast<const char *>(&normal), sizeof(Vec3f));
    out.write(reinterpret_cast<const char *>(&v0), sizeof(Vec3f));
    out.write(reinterpret_cast<const char *>(&v1), sizeof(Vec3f));
    out.write(reinterpret_cast<const char *>(&v2), sizeof(Vec3f));
    const uint16_t attr = 0;
    out.write(reinterpret_cast<const char *>(&attr), sizeof(uint16_t));
  }

  if (!out) {
    error = "Export failed: write error";
    return false;
  }

  return true;
}

}
ExportResult ExportParts(const PartTree &tree,bool visibleOnly,bool valid,
                         const std::filesystem::path &path,bool overwrite,std::string &error){
  error.clear();
  if(!valid)return ExportResult::Invalid;
  if(tree.ExportIndices(visibleOnly).empty())return ExportResult::Empty;
  std::error_code ec;
  const bool exists=std::filesystem::exists(path,ec);
  if(ec){error=ec.message();return ExportResult::Failed;}
  if(exists&&!overwrite)return ExportResult::ConfirmOverwrite;
  const auto solid=tree.ExportSolid(visibleOnly);
  const auto mesh=solid->GetMeshGL();
  if(mesh.NumTri()==0)return ExportResult::Empty;
  if(!path.parent_path().empty())std::filesystem::create_directories(path.parent_path(),ec);
  if(ec){error=ec.message();return ExportResult::Failed;}
  return WriteMeshAsBinaryStl(mesh,path,error)?ExportResult::Saved:ExportResult::Failed;
}
}
