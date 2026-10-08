#include "../selection_reference.h"
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace dingcad::selection;
namespace {
void Require(bool ok,const char* message) { if(!ok) throw std::runtime_error(message); }
std::string RawToken(const std::string& bytes) {
  std::string out="scsel1.";
  constexpr char hex[]="0123456789abcdef";
  for(unsigned char c:bytes) { out+=hex[c>>4];out+=hex[c&15]; }
  return out;
}
std::string Token(const nlohmann::json& j) { return RawToken(j.dump(-1,' ',true)); }
void CheckTokens() {
  Reference r{u8"pièce/固定",u8"révision",std::numeric_limits<uint64_t>::max(),Kind::Edge,123};
  auto token=MakeReference(r);
  for(unsigned char c:token) Require(c<128,"token must be ASCII");
  auto decoded=DecodeReference(token);
  Require(decoded && decoded->geometry && decoded->partId==r.partId && decoded->revision==r.revision &&
          decoded->geometry->topologyKey==r.topologyKey && decoded->geometry->id==123,"Unicode/uint64 round trip failed");
  auto part=DecodeReference(MakePartReference(r.partId,r.revision));
  Require(part && !part->geometry && part->partId==r.partId,"part reference round trip failed");
  Require(!DecodeReference("") && !DecodeReference("scsel2.00") && !DecodeReference("scsel1.0") &&
          !DecodeReference("scsel1.zz") && !DecodeReference("scsel1."+std::string(32770,'0')),
          "invalid token framing accepted");
  Require(!DecodeReference(RawToken("{\"kind\":\"part\",\"partId\":\"p\",\"revision\":\"r\",\"revision\":\"r\"}")) &&
          !DecodeReference(RawToken(" {\"kind\":\"part\",\"partId\":\"p\",\"revision\":\"r\"}")) &&
          !DecodeReference(RawToken("[]")) && !DecodeReference(RawToken("null")),
          "duplicate fields or noncanonical payload accepted");
  nlohmann::json base={{"partId","p"},{"revision","r"},{"kind","edge"},{"topologyKey","123"},{"id",0u}};
  const auto bad=[&](nlohmann::json j){Require(!DecodeReference(Token(j)),"invalid identity accepted");};
  auto p=base;p["position"]={0,0,0};bad(p);
  p=base;p["sourcePartId"]="spoofed";bad(p);
  p=base;p["kind"]="face";bad(p);
  p=base;p["id"]=-1;bad(p);
  p=base;p["id"]=1.5;bad(p);
  p=base;p["id"]=uint64_t(1)<<32;bad(p);
  p=base;p["topologyKey"]=123;bad(p);
  p=base;p["topologyKey"]="18446744073709551616";bad(p);
  p=base;p["topologyKey"]="0123";bad(p);
  p=base;p["partId"]="";bad(p);
  p=base;p.erase("revision");bad(p);
  p=base;p["kind"]="part";bad(p);
  bool oversized=false;
  try { MakePartReference(std::string(16384,'a'),"r"); } catch(const std::invalid_argument&) { oversized=true; }
  Require(oversized,"oversized generated reference accepted");
}
void CheckResolution() {
  auto cube=manifold::Manifold::Cube({10,10,10});
  auto topology=Topology::Build(cube.GetMeshGL(),"instance","r1");
  auto hit=topology.RayPick({2,3,20},{0,0,-1});
  Require(bool(hit),"cube ray missed");
  auto decoded=DecodeReference(MakeReference(hit->reference));
  auto resolved=ResolveReference(*decoded,topology);
  Require(resolved && resolved->hasNormal && resolved->bounds.max.z==10,"current feature failed to resolve");
  auto stale=Topology::Build(cube.GetMeshGL(),"instance","r2");
  auto owner=Topology::Build(cube.GetMeshGL(),"other","r1");
  auto changed=Topology::Build(manifold::Manifold::Cube({20,10,10}).GetMeshGL(),"instance","r1");
  Require(!ResolveReference(*decoded,stale) && !ResolveReference(*decoded,owner) &&
          !ResolveReference(*decoded,changed),"stale/wrong-owner/wrong-geometry reference accepted");
  auto wrong=*decoded;wrong.geometry->kind=Kind::Vertex;
  Require(!ResolveReference(wrong,topology),"wrong feature kind accepted");
  dingcad::PartTree tree;
  dingcad::DisplayPart part;part.id="instance";part.sourcePartId="source";
  part.solid=std::make_shared<manifold::Manifold>(cube);tree.parts.push_back(part);
  const auto context=GeometryJson(*resolved,tree,true);
  dingcad::PartTree oversizedTree=tree;
  oversizedTree.parts[0].id=std::string(16384,'a');
  const auto unavailable=PartGeometryJson(oversizedTree.parts[0].id,"r1",oversizedTree);
  Require(unavailable["reference"].is_null()&&unavailable.contains("referenceError"),"long IDs must retain context without crashing");
  auto oversizedPick=*hit;oversizedPick.reference.partId=oversizedTree.parts[0].id;
  Require(GeometryJson(oversizedPick,oversizedTree)["reference"].is_null(),"long feature owner must not crash");
  Require(context["sourcePartId"]=="source" && context["instanceId"]=="instance" &&
          context["positionKind"]=="representative" && context["topologyKey"].is_string(),"trusted context missing");
  Require(GeometryJson(*hit,tree)["position"]==nlohmann::json::array({2,3,10}) &&
          GeometryJson(*hit,tree)["positionKind"]=="hit","clicked position changed");
  Require(PartGeometryJson("instance","r1",tree)["bounds"]["max"]==nlohmann::json::array({10,10,10}),
          "part bounds missing");
  Require(PartGeometryJson("missing","r1",tree).is_null(),"missing owner context fabricated");
}
}
int main() {
  try { CheckTokens();CheckResolution();std::cout<<"Selection reference tests passed\n";return 0; }
  catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
}
