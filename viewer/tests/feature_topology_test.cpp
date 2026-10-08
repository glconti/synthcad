#include "../feature_topology.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>
#include <type_traits>
#include <utility>

using namespace dingcad::selection;
using manifold::Manifold;
void Require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
size_t Count(const Topology& t,Kind kind){size_t n=0;for(const auto& f:t.Features())n+=f.kind==kind;return n;}
static_assert(std::is_same_v<decltype(std::declval<Topology&>().Points()),const std::vector<Vec3>&>,"Points must be immutable");
static_assert(std::is_same_v<decltype(std::declval<Topology&>().Triangles()),const std::vector<std::array<uint32_t,3>>&>,"Triangles must be immutable");
bool Near(double a,double b){return std::abs(a-b)<1e-5;}
Bounds PointBounds(const Topology& topology,const std::set<uint32_t>& members){
  const auto inf=std::numeric_limits<double>::infinity();Bounds bounds{{inf,inf,inf},{-inf,-inf,-inf}};
  for(const auto member:members){const auto p=topology.Points().at(member);
    bounds.min={std::min(bounds.min.x,p.x),std::min(bounds.min.y,p.y),std::min(bounds.min.z,p.z)};
    bounds.max={std::max(bounds.max.x,p.x),std::max(bounds.max.y,p.y),std::max(bounds.max.z,p.z)};}
  return bounds;
}
void SameBounds(Bounds actual,Bounds expected){
  Require(Near(actual.min.x,expected.min.x)&&Near(actual.min.y,expected.min.y)&&Near(actual.min.z,expected.min.z)&&
          Near(actual.max.x,expected.max.x)&&Near(actual.max.y,expected.max.y)&&Near(actual.max.z,expected.max.z),"Reconstructed geometry must match full feature bounds");
}
void CheckExplodedFace(const Topology& topology){
  Require(topology.Points().size()==8&&topology.Triangles().size()==12,"Exploded cube exposes welded snapshot geometry");
  const auto pick=topology.RayPick({10,5,20},{0,0,-1});
  Require(pick&&pick->reference.kind==Kind::PlanarFace,"Exploded face must be selectable");
  const auto& face=topology.Features().at(pick->reference.id);
  std::set<uint32_t> vertices;double area=0;
  for(const auto member:face.members){const auto triangle=topology.Triangles().at(member);
    const auto a=topology.Points().at(triangle[0]),b=topology.Points().at(triangle[1]),c=topology.Points().at(triangle[2]);
    Require(Near(a.z,4)&&Near(b.z,4)&&Near(c.z,4),"Face reconstruction must use top-plane points");
    area+=std::abs((b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x))*0.5;
    vertices.insert(triangle.begin(),triangle.end());}
  Require(face.members.size()==2&&vertices.size()==4&&Near(area,200),"Reconstructed face must cover both triangles and all four corners");
  SameBounds(PointBounds(topology,vertices),{{0,0,4},{20,10,4}});
  SameBounds(PointBounds(topology,vertices),face.bounds);
}
void CheckCylinderRims(const Topology& topology){
  std::set<int> heights;
  for(const auto& rim:topology.Features())if(rim.kind==Kind::Edge){
    Require(rim.members.size()==65&&rim.members.front()==rim.members.back(),"Rim must close after all 64 segments");
    const std::set<uint32_t> vertices(rim.members.begin(),rim.members.end());
    Require(vertices.size()==64,"Closed rim must not repeat an interior point");
    const auto z=topology.Points().at(rim.members.front()).z;
    Require(Near(z,0)||Near(z,20),"Rim must belong to a cylinder cap");heights.insert(Near(z,0)?0:20);
    std::set<uint32_t> expected;
    for(uint32_t i=0;i<topology.Points().size();++i){const auto p=topology.Points()[i];
      if(Near(p.z,z)&&Near(std::hypot(p.x,p.y),10))expected.insert(i);}
    Require(vertices==expected,"Rim must include every cap-boundary point");
    const double segmentLength=20*std::sin(3.14159265358979323846/64);
    for(size_t i=1;i<rim.members.size();++i){const auto a=topology.Points().at(rim.members[i-1]),b=topology.Points().at(rim.members[i]);
      Require(Near(a.z,z)&&Near(b.z,z)&&Near(std::hypot(b.x-a.x,b.y-a.y),segmentLength),"Rim chain must connect consecutive circumference points");}
    const auto bounds=PointBounds(topology,vertices);
    SameBounds(bounds,{{-10,-10,z},{10,10,z}});SameBounds(bounds,rim.bounds);
  }
  Require(heights==std::set<int>({0,20}),"Both complete cylinder rims must be reconstructed");
}
manifold::MeshGL Explode(const manifold::MeshGL& source){
  manifold::MeshGL result;result.numProp=6;
  for(auto i:source.triVerts){result.triVerts.push_back(uint32_t(result.triVerts.size()));
    for(int j=0;j<3;++j)result.vertProperties.push_back(source.vertProperties[i*source.numProp+j]);
    result.vertProperties.insert(result.vertProperties.end(),{255,0,127});}
  return result;
}
template<class F>void Reject(F f,const char* message){bool rejected=false;try{f();}catch(const std::invalid_argument&){rejected=true;}Require(rejected,message);}
int main(){try{
  const auto mesh=Manifold::Cube({20,10,4}).GetMeshGL();
  const auto cube=Topology::Build(mesh,"fitting","revision-a");
  Require(Count(cube,Kind::PlanarFace)==6&&Count(cube,Kind::CurvedPatch)==0,"Cube has six meaningful planes");
  Require(Count(cube,Kind::Edge)==12&&Count(cube,Kind::Vertex)==8,"Cube has twelve edges and eight corners");
  for(const auto& f:cube.Features())if(f.kind==Kind::PlanarFace)Require(f.members.size()==2,"Coplanar diagonals are internal");
  const auto exploded=Topology::Build(Explode(mesh),"fitting","revision-a");
  Require(Count(exploded,Kind::PlanarFace)==6&&Count(exploded,Kind::Edge)==12,"Independent color vertices weld geometrically");
  CheckExplodedFace(exploded);
  const auto hit=cube.RayPick({10,5,20},{0,0,-2});
  Require(hit&&hit->reference.kind==Kind::PlanarFace&&cube.Contains(hit->reference),"Surface reference resolves");
  Require(std::abs(hit->position.z-4)<1e-9&&hit->normal.z>0.99&&hit->hasNormal,"Hit has model XYZ and outward triangle normal");
  Require(hit->bounds.max.x==20&&hit->bounds.max.y==10&&hit->bounds.min.z==4,"Face bounds span the complete plane");
  Require(!cube.RayPick({10,5,20},{0,0,-1},PickMode::Edge,0.2),"Triangle diagonal is not an edge");
  const auto edge=cube.RayPick({0.1,5,20},{0,0,-1},PickMode::Edge,0.2);
  Require(edge&&edge->reference.kind==Kind::Edge&&!edge->hasNormal&&edge->position.x==0,"Edge snaps with no invented unique normal");
  const auto vertex=cube.RayPick({0.1,0.1,20},{0,0,-1},PickMode::Vertex,0.2);
  Require(vertex&&vertex->reference.kind==Kind::Vertex&&vertex->position.x==0&&vertex->position.y==0,"Corner vertex pick");
  Require(!cube.RayPick({30,5,20},{0,0,-1}),"Miss remains empty");
  Require(!cube.RayPick({0,0,0},{0,0,0}),"Zero direction rejected");
  Require(!Topology::Build(mesh,"fitting","revision-b").Contains(hit->reference),"Reload revision invalidates feature ID");
  Require(!Topology::Build(mesh,"other-part","revision-a").Contains(hit->reference),"Owning part checked");
  Require(!Topology::Build(Manifold::Cube({21,10,4}).GetMeshGL(),"fitting","revision-a").Contains(hit->reference),"Geometry mismatch rejected even with reused revision");
  Options changed;changed.sharpAngleDegrees=25;
  Require(!Topology::Build(mesh,"fitting","revision-a",changed).Contains(hit->reference),"Topology options are part of identity");
  auto wrong=hit->reference;wrong.kind=Kind::Vertex;Require(!cube.Contains(wrong),"Feature kind checked");

  const auto cylinder=Topology::Build(Manifold::Cylinder(20,10,-1,64).GetMeshGL(),"curved","r1");
  Require(Count(cylinder,Kind::PlanarFace)==2&&Count(cylinder,Kind::CurvedPatch)==1,"Cylinder has caps and one connected curved side patch");
  Require(Count(cylinder,Kind::Edge)==2&&Count(cylinder,Kind::Vertex)==0,"Circular rims are closed chains, tessellation vertices suppressed");
  CheckCylinderRims(cylinder);
  const auto side=cylinder.RayPick({30,0,10},{-1,0,0});
  Require(side&&side->reference.kind==Kind::CurvedPatch&&side->hasNormal&&side->normal.x>0.99,"Curved hit returns local mesh normal");
  const auto sphere=Topology::Build(Manifold::Sphere(10,64).GetMeshGL(),"decoration","r1");
  Require(Count(sphere,Kind::CurvedPatch)==1&&Count(sphere,Kind::Edge)==0&&Count(sphere,Kind::Vertex)==0,"Smooth sphere is a patch without artificial edges/corners");
  const auto disconnected=Topology::Build(Manifold::Compose({Manifold::Cube({2,2,2}),Manifold::Cube({2,2,2}).Translate({4,0,0})}).GetMeshGL(),"two-solids","r1");
  Require(Count(disconnected,Kind::PlanarFace)==12&&Count(disconnected,Kind::Edge)==24&&Count(disconnected,Kind::Vertex)==16,"Disconnected coplanar solids stay separate");
  const auto fitting=Topology::Build((Manifold::Cube({40,30,5})-Manifold::Cylinder(7,4,-1,64).Translate({20,15,-1})).GetMeshGL(),"bored-plate","r1");
  const auto top=fitting.RayPick({5,5,20},{0,0,-1});
  const auto bore=fitting.RayPick({20,15,2.5},{1,0,0});
  Require(top&&top->reference.kind==Kind::PlanarFace&&bore&&bore->reference.kind==Kind::CurvedPatch,"Fitting exposes planar mating face and curved bore separately");

  // Vertex perturbation below tolerance still removes display triangulation.
  auto noisy=Explode(mesh);noisy.vertProperties[0]+=float(cube.Tolerance()*0.25);
  const auto tolerant=Topology::Build(noisy,"tolerant","r1");
  Require(Count(tolerant,Kind::PlanarFace)==6&&Count(tolerant,Kind::Edge)==12,"Explicit positional tolerance handles tiny property-seam noise");
  auto bad=mesh;bad.triVerts[0]=uint32_t(bad.NumVert()+3);
  Reject([&]{Topology::Build(bad,"bad","r");},"Bad indices rejected");
  bad=mesh;bad.triVerts[1]=bad.triVerts[0];
  Reject([&]{Topology::Build(bad,"bad","r");},"Collapsed triangles rejected");
  bad=mesh;bad.triVerts.insert(bad.triVerts.end(),mesh.triVerts.begin(),mesh.triVerts.begin()+3);
  Reject([&]{Topology::Build(bad,"bad","r");},"Ambiguous nonmanifold weld rejected");

  const auto large=Explode(Manifold::Sphere(30,256).GetMeshGL());
  using Clock=std::chrono::steady_clock;
  const auto before=Clock::now();const auto dense=Topology::Build(large,"dense-decoration","r1");const auto built=Clock::now();
  Require(Count(dense,Kind::CurvedPatch)==1,"Representative decorative mesh groups as one patch");
  size_t picks=0;for(int i=0;i<100;++i)picks+=bool(dense.RayPick({double(i%10)-5,double(i/10)-5,100},{0,0,-1}));const auto picked=Clock::now();
  Require(picks==100,"Measured rays all hit representative mesh");
  std::cout<<"BENCH triangles="<<dense.TriangleCount()<<" input_vertices="<<large.NumVert()
    <<" build_ms="<<std::chrono::duration<double,std::milli>(built-before).count()
    <<" ray_mean_ms="<<std::chrono::duration<double,std::milli>(picked-built).count()/100<<" (100 rays)\n";
  std::cout<<"PASS planar fitting, curved patches, sharp chains/corners, property seams, ray context, stale references and invalid input\n";
  return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
