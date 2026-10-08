#pragma once
#include "feature_topology.h"
#include "part_tree.h"
#include <nlohmann/json.hpp>

namespace dingcad::selection {
// Tokens contain identity only. Context and geometric coordinates always come
// from the displayed revision's part tree and topology cache.
struct PortableReference {
  std::string partId, revision;
  std::optional<Reference> geometry;
};
const char* KindName(Kind kind);
std::string MakeReference(const Reference& reference);
std::string MakePartReference(const std::string& partId, const std::string& revision);
std::optional<PortableReference> DecodeReference(const std::string& token);
std::optional<Pick> ResolveReference(const PortableReference&, const Topology&);
nlohmann::json GeometryJson(const Pick&, const PartTree&, bool representative = false);
nlohmann::json PartGeometryJson(const std::string& partId,
                                const std::string& revision, const PartTree&);
}
