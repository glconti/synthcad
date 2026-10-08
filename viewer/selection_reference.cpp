#include "selection_reference.h"
#include <algorithm>
#include <charconv>
#include <limits>
#include <stdexcept>

namespace dingcad::selection {
namespace {
using nlohmann::json;
constexpr size_t kMaxPayload = 16 * 1024;
constexpr char kPrefix[] = "scsel1.";
std::string Encode(const json& payload) {
  const auto bytes = payload.dump(-1, ' ', true);
  if (bytes.size() > kMaxPayload) throw std::invalid_argument("Selection reference exceeds 16 KiB");
  std::string token = kPrefix;
  constexpr char hex[] = "0123456789abcdef";
  for (unsigned char c : bytes) { token += hex[c >> 4]; token += hex[c & 15]; }
  return token;
}
json Identity(const std::string& partId, const std::string& revision, const char* kind) {
  if (partId.empty() || revision.empty()) throw std::invalid_argument("Selection identity must not be empty");
  return {{"partId", partId}, {"revision", revision}, {"kind", kind}};
}
int Hex(char c) { return c >= '0' && c <= '9' ? c-'0' : c >= 'a' && c <= 'f' ? c-'a'+10 : -1; }
json Coordinates(Vec3 v) { return json::array({v.x,v.y,v.z}); }
json Context(const std::string& partId, const std::string& revision, const PartTree& tree) {
  const auto p = std::find_if(tree.parts.begin(), tree.parts.end(), [&](const auto& part){return part.id == partId;});
  if (p == tree.parts.end()) return nullptr;
  return {{"partId", partId}, {"revision", revision},
          {"sourcePartId", p->sourcePartId.empty() ? json(nullptr) : json(p->sourcePartId)},
          {"instanceId", p->sourcePartId.empty() ? json(nullptr) : json(p->id)}};
}
}
const char* KindName(Kind kind) {
  switch(kind) {
    case Kind::PlanarFace: return "planar-face";
    case Kind::CurvedPatch: return "curved-patch";
    case Kind::Edge: return "edge";
    case Kind::Vertex: return "vertex";
  }
  throw std::invalid_argument("Unknown selection kind");
}
std::string MakeReference(const Reference& r) {
  auto payload = Identity(r.partId, r.revision, KindName(r.kind));
  payload["topologyKey"] = std::to_string(r.topologyKey);
  payload["id"] = r.id;
  return Encode(payload);
}
std::string MakePartReference(const std::string& partId, const std::string& revision) {
  return Encode(Identity(partId, revision, "part"));
}
std::optional<PortableReference> DecodeReference(const std::string& token) {
  constexpr size_t prefixSize = sizeof(kPrefix)-1;
  if (token.compare(0, prefixSize, kPrefix) != 0 || token.size() <= prefixSize ||
      token.size() > prefixSize + 2*kMaxPayload || (token.size()-prefixSize)%2) return {};
  std::string bytes;
  bytes.reserve((token.size()-prefixSize)/2);
  for(size_t i=prefixSize;i<token.size();i+=2) {
    int a=Hex(token[i]), b=Hex(token[i+1]);
    if(a<0 || b<0) return {};
    bytes += char(a*16+b);
  }
  try {
    const auto p=json::parse(bytes);
    if (!p.is_object() || p.dump(-1,' ',true) != bytes || !p.contains("partId") ||
        !p.contains("revision") || !p.contains("kind") || !p["partId"].is_string() ||
        !p["revision"].is_string() || !p["kind"].is_string()) return {};
    PortableReference result{p["partId"].get<std::string>(),p["revision"].get<std::string>(),{}};
    if(result.partId.empty() || result.revision.empty()) return {};
    const std::string kind=p["kind"];
    if(kind=="part") return p.size()==3 ? std::optional<PortableReference>(result) : std::nullopt;
    if(p.size()!=5 || !p.contains("topologyKey") || !p["topologyKey"].is_string() ||
       !p.contains("id") || !p["id"].is_number_unsigned()) return {};
    Kind featureKind;
    if(kind=="planar-face") featureKind=Kind::PlanarFace;
    else if(kind=="curved-patch") featureKind=Kind::CurvedPatch;
    else if(kind=="edge") featureKind=Kind::Edge;
    else if(kind=="vertex") featureKind=Kind::Vertex;
    else return {};
    const auto key=p["topologyKey"].get<std::string>();
    uint64_t value=0;
    const auto parsed=std::from_chars(key.data(),key.data()+key.size(),value);
    if(key.empty() || parsed.ec!=std::errc() || parsed.ptr!=key.data()+key.size() ||
       std::to_string(value)!=key || p["id"].get<uint64_t>()>std::numeric_limits<uint32_t>::max()) return {};
    result.geometry=Reference{result.partId,result.revision,value,featureKind,p["id"].get<uint32_t>()};
    return result;
  } catch(const json::exception&) { return {}; }
}
std::optional<Pick> ResolveReference(const PortableReference& r, const Topology& topology) {
  if(!r.geometry || r.partId!=r.geometry->partId || r.revision!=r.geometry->revision ||
     !topology.Contains(*r.geometry)) return {};
  const auto& f=topology.Features().at(r.geometry->id);
  if(f.members.empty()) return {};
  Vec3 position;
  if(f.kind==Kind::PlanarFace || f.kind==Kind::CurvedPatch) {
    const auto triangle=topology.Triangles().at(f.members.front());
    for(auto i:triangle) { const auto p=topology.Points().at(i); position.x+=p.x/3;position.y+=p.y/3;position.z+=p.z/3; }
  } else if(f.kind==Kind::Edge && f.members.size()>1) {
    const auto a=topology.Points().at(f.members[0]),b=topology.Points().at(f.members[1]);
    position={(a.x+b.x)/2,(a.y+b.y)/2,(a.z+b.z)/2};
  } else position=topology.Points().at(f.members.front());
  return Pick{*r.geometry,position,f.normal,f.kind==Kind::PlanarFace,f.bounds,0};
}
json GeometryJson(const Pick& pick, const PartTree& tree, bool representative) {
  auto result=Context(pick.reference.partId,pick.reference.revision,tree);
  if(result.is_null()) return result;
  result["kind"]=KindName(pick.reference.kind);
  try { result["reference"]=MakeReference(pick.reference); }
  catch(const std::invalid_argument& error) { result["reference"]=nullptr;result["referenceError"]=error.what(); }
  result["position"]=Coordinates(pick.position);
  result["positionKind"]=representative ? "representative" : "hit";
  result["normal"]=pick.hasNormal ? Coordinates(pick.normal) : json(nullptr);
  result["bounds"]={{"min",Coordinates(pick.bounds.min)},{"max",Coordinates(pick.bounds.max)}};
  result["topologyKey"]=std::to_string(pick.reference.topologyKey);
  result["id"]=pick.reference.id;
  return result;
}
json PartGeometryJson(const std::string& partId, const std::string& revision, const PartTree& tree) {
  auto result=Context(partId,revision,tree);
  if(result.is_null()) return result;
  result["kind"]="part";
  try { result["reference"]=MakePartReference(partId,revision); }
  catch(const std::invalid_argument& error) { result["reference"]=nullptr;result["referenceError"]=error.what(); }
  result["normal"]=nullptr;result["position"]=nullptr;result["bounds"]=nullptr;
  result["positionKind"]="representative";
  const auto p=std::find_if(tree.parts.begin(),tree.parts.end(),[&](const auto& part){return part.id==partId;});
  if(p->solid && !p->solid->IsEmpty()) {
    const auto b=p->solid->BoundingBox();
    result["bounds"]={{"min",json::array({b.min.x,b.min.y,b.min.z})},{"max",json::array({b.max.x,b.max.y,b.max.z})}};
    result["position"]=json::array({(b.min.x+b.max.x)/2,(b.min.y+b.max.y)/2,(b.min.z+b.max.z)/2});
  }
  return result;
}
}
