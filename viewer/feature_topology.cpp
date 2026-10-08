#include "feature_topology.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <numeric>
#include <stdexcept>
#include <unordered_map>

namespace dingcad::selection {
namespace {
Vec3 Add(Vec3 a,Vec3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
Vec3 Sub(Vec3 a,Vec3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
Vec3 Mul(Vec3 a,double b){return {a.x*b,a.y*b,a.z*b};}
double Dot(Vec3 a,Vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
Vec3 Cross(Vec3 a,Vec3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
double Length(Vec3 a){return std::sqrt(Dot(a,a));}
Vec3 Unit(Vec3 a){return Mul(a,1/Length(a));}
bool Finite(Vec3 a){return std::isfinite(a.x)&&std::isfinite(a.y)&&std::isfinite(a.z);}
Bounds Empty(){double v=std::numeric_limits<double>::infinity();return {{v,v,v},{-v,-v,-v}};}
void Extend(Bounds& b,Vec3 p){b.min={std::min(b.min.x,p.x),std::min(b.min.y,p.y),std::min(b.min.z,p.z)};b.max={std::max(b.max.x,p.x),std::max(b.max.y,p.y),std::max(b.max.z,p.z)};}
struct Cell {
  int64_t x,y,z;
  bool operator==(const Cell& b)const{return x==b.x&&y==b.y&&z==b.z;}
};
struct CellHash {size_t operator()(Cell c)const{return std::hash<int64_t>{}(c.x)^(std::hash<int64_t>{}(c.y)<<1)^(std::hash<int64_t>{}(c.z)<<2);}};
struct Dsu {
  std::vector<uint32_t> p;
  explicit Dsu(size_t n):p(n){std::iota(p.begin(),p.end(),0);}
  uint32_t Find(uint32_t a){while(a!=p[a]){p[a]=p[p[a]];a=p[a];}return a;}
  void Join(uint32_t a,uint32_t b){a=Find(a);b=Find(b);if(a!=b)p[std::max(a,b)]=std::min(a,b);}
};
using Pair=std::pair<uint32_t,uint32_t>;
Pair Sorted(uint32_t a,uint32_t b){return std::minmax(a,b);}
struct Segment {uint32_t a,b;Pair regions;};
void Hash(uint64_t& h,const void* data,size_t n){const auto* b=static_cast<const unsigned char*>(data);for(size_t i=0;i<n;++i){h^=b[i];h*=1099511628211ull;}}
// Moller-Trumbore, double-sided: the owning viewer determines visible candidates.
bool Hit(Vec3 o,Vec3 d,Vec3 a,Vec3 b,Vec3 c,double& t){
  const auto e1=Sub(b,a),e2=Sub(c,a),p=Cross(d,e2);const double det=Dot(e1,p);
  if(std::abs(det)<1e-14*Length(e1)*Length(e2))return false;
  const double inv=1/det;const auto s=Sub(o,a);const double u=Dot(s,p)*inv;
  if(u<-1e-10||u>1+1e-10)return false;
  const auto q=Cross(s,e1);const double v=Dot(d,q)*inv;
  if(v<-1e-10||u+v>1+1e-10)return false;
  t=Dot(e2,q)*inv;return t>=0;
}
}

Topology Topology::Build(const manifold::MeshGL& mesh,std::string partId,
                         std::string revision,Options options){
  if(partId.empty()||revision.empty())throw std::invalid_argument("Selection needs owning part and displayed revision");
  if(mesh.numProp<3||mesh.vertProperties.size()%mesh.numProp||mesh.triVerts.size()%3)
    throw std::invalid_argument("Malformed MeshGL buffers");
  if(!std::isfinite(options.absoluteTolerance)||!std::isfinite(options.relativeTolerance)||
     options.absoluteTolerance<=0||options.relativeTolerance<0||
     !std::isfinite(options.sharpAngleDegrees)||options.sharpAngleDegrees<=0||options.sharpAngleDegrees>=90)
    throw std::invalid_argument("Invalid selection tolerances");
  Topology out;out.partId_=std::move(partId);out.revision_=std::move(revision);
  Bounds bounds=Empty();
  std::vector<Vec3> raw;
  for(size_t i=0;i<mesh.vertProperties.size();i+=mesh.numProp){
    Vec3 p{mesh.vertProperties[i],mesh.vertProperties[i+1],mesh.vertProperties[i+2]};
    if(!Finite(p))throw std::invalid_argument("Non-finite mesh position");
    raw.push_back(p);Extend(bounds,p);
  }
  out.tolerance_=std::max(options.absoluteTolerance,raw.empty()?0:Length(Sub(bounds.max,bounds.min))*options.relativeTolerance);
  if(!std::isfinite(out.tolerance_))throw std::invalid_argument("Mesh scale exceeds selection tolerance");
  // Identity includes input geometry and options, but ignores RGB/properties.
  // It is an accidental-mismatch guard, not a cryptographic identity or edit correspondence.
  out.key_=14695981039346656037ull;
  for(const auto p:raw){Hash(out.key_,&p.x,sizeof(double));Hash(out.key_,&p.y,sizeof(double));Hash(out.key_,&p.z,sizeof(double));}
  for(const auto i:mesh.triVerts)Hash(out.key_,&i,sizeof(i));
  Hash(out.key_,&out.tolerance_,sizeof(double));Hash(out.key_,&options.sharpAngleDegrees,sizeof(double));
  std::unordered_map<Cell,std::vector<uint32_t>,CellHash> grid;
  std::vector<uint32_t> welded(raw.size());
  for(size_t i=0;i<raw.size();++i){
    // Subtract bounds to keep grid coordinates bounded for translated models.
    const auto p=raw[i],q=Mul(Sub(p,bounds.min),1/out.tolerance_);
    if(std::max({q.x,q.y,q.z})>double(std::numeric_limits<int64_t>::max()/2))
      throw std::invalid_argument("Tolerance too small for mesh scale");
    Cell cell{int64_t(std::floor(q.x)),int64_t(std::floor(q.y)),int64_t(std::floor(q.z))};
    uint32_t match=uint32_t(out.points_.size());
    for(int x=-1;x<=1;++x)for(int y=-1;y<=1;++y)for(int z=-1;z<=1;++z){
      auto found=grid.find({cell.x+x,cell.y+y,cell.z+z});if(found==grid.end())continue;
      for(auto v:found->second)if(Length(Sub(p,out.points_[v]))<=out.tolerance_)match=std::min(match,v);
    }
    if(match==out.points_.size()){out.points_.push_back(p);grid[cell].push_back(match);}
    welded[i]=match;
  }
  std::map<Pair,std::vector<uint32_t>> adjacency;
  for(size_t i=0;i<mesh.triVerts.size();i+=3){
    std::array<uint32_t,3> tri;
    for(int j=0;j<3;++j){if(mesh.triVerts[i+j]>=welded.size())throw std::invalid_argument("Triangle index out of range");tri[j]=welded[mesh.triVerts[i+j]];}
    const auto n=Cross(Sub(out.points_[tri[1]],out.points_[tri[0]]),Sub(out.points_[tri[2]],out.points_[tri[0]]));
    if(Length(n)<=out.tolerance_*out.tolerance_)throw std::invalid_argument("Degenerate triangle after welding");
    const auto ti=uint32_t(out.triangles_.size());out.triangles_.push_back(tri);out.normals_.push_back(Unit(n));
    for(int j=0;j<3;++j){auto& incident=adjacency[Sorted(tri[j],tri[(j+1)%3])];incident.push_back(ti);if(incident.size()>2)throw std::invalid_argument("Nonmanifold welded edge; topology ambiguous");}
  }
  const double cosSharp=std::cos(options.sharpAngleDegrees*3.14159265358979323846/180);
  Dsu regions(out.triangles_.size());
  for(const auto& item:adjacency){const auto& t=item.second;if(t.size()==2&&Dot(out.normals_[t[0]],out.normals_[t[1]])>cosSharp)regions.Join(t[0],t[1]);}
  std::map<uint32_t,uint32_t> regionIds;
  out.triangleFeature_.resize(out.triangles_.size());
  for(uint32_t i=0;i<out.triangles_.size();++i){
    const auto root=regions.Find(i);auto found=regionIds.find(root);
    if(found==regionIds.end()){
      found=regionIds.emplace(root,uint32_t(out.features_.size())).first;
      out.features_.push_back({Kind::PlanarFace,Empty(),{},out.normals_[i]});
    }
    auto id=found->second;out.triangleFeature_[i]=id;auto& f=out.features_[id];f.members.push_back(i);
    for(auto v:out.triangles_[i])Extend(f.bounds,out.points_[v]);
  }
  for(auto& f:out.features_){
    const auto origin=out.points_[out.triangles_[f.members[0]][0]];
    for(auto ti:f.members)for(auto vi:out.triangles_[ti])
      if(std::abs(Dot(f.normal,Sub(out.points_[vi],origin)))>out.tolerance_)f.kind=Kind::CurvedPatch;
    if(f.kind==Kind::CurvedPatch)f.normal={}; // curved patches have no single normal
  }
  std::vector<Segment> segments;
  std::vector<std::vector<uint32_t>> atVertex(out.points_.size());
  constexpr uint32_t exterior=std::numeric_limits<uint32_t>::max();
  for(const auto& item:adjacency){
    const auto& t=item.second;
    if(t.size()==2&&Dot(out.normals_[t[0]],out.normals_[t[1]])>cosSharp)continue;
    Pair pair=Sorted(out.triangleFeature_[t[0]],t.size()==2?out.triangleFeature_[t[1]]:exterior);
    const auto si=uint32_t(segments.size());segments.push_back({item.first.first,item.first.second,pair});
    atVertex[item.first.first].push_back(si);atVertex[item.first.second].push_back(si);
  }
  std::vector<bool> corner(out.points_.size(),false),used(segments.size(),false);
  for(uint32_t v=0;v<atVertex.size();++v){
    const auto& incident=atVertex[v];if(incident.empty())continue;
    bool split=incident.size()!=2;
    if(!split){const auto a=segments[incident[0]],b=segments[incident[1]];
      const auto da=Unit(Sub(out.points_[a.a==v?a.b:a.a],out.points_[v]));
      const auto db=Unit(Sub(out.points_[b.a==v?b.b:b.a],out.points_[v]));
      split=a.regions!=b.regions||Dot(da,db)>-cosSharp;
    }
    corner[v]=split;
  }
  auto chain=[&](uint32_t start,uint32_t segment){
    Feature f{Kind::Edge,Empty(),{start},{}};uint32_t current=start;
    while(!used[segment]){
      used[segment]=true;const auto s=segments[segment];current=s.a==current?s.b:s.a;f.members.push_back(current);
      if(corner[current]||current==start)break;
      const auto& next=atVertex[current];segment=next[0]==segment?next[1]:next[0];
    }
    for(auto v:f.members)Extend(f.bounds,out.points_[v]);out.features_.push_back(std::move(f));
  };
  for(uint32_t v=0;v<corner.size();++v)if(corner[v])for(auto s:atVertex[v])if(!used[s])chain(v,s);
  for(uint32_t s=0;s<segments.size();++s)if(!used[s])chain(segments[s].a,s);
  for(uint32_t v=0;v<corner.size();++v)if(corner[v])out.features_.push_back({Kind::Vertex,{out.points_[v],out.points_[v]},{v},{}});
  return out;
}

bool Topology::Contains(const Reference& r)const{
  return r.partId==partId_&&r.revision==revision_&&r.topologyKey==key_&&r.id<features_.size()&&features_[r.id].kind==r.kind;
}
std::optional<Pick> Topology::FeaturePick(uint32_t id,Vec3 position,double distance)const{
  if(id>=features_.size()||!Finite(position)||!std::isfinite(distance)||distance<0)return {};
  const auto& f=features_[id];
  return Pick{{partId_,revision_,key_,f.kind,id},position,{},false,f.bounds,distance};
}
std::optional<Pick> Topology::RayPick(Vec3 origin,Vec3 direction,PickMode mode,double radius)const{
  if(!Finite(origin)||!Finite(direction)||Length(direction)==0||!std::isfinite(radius)||radius<0)return {};
  direction=Unit(direction);double distance=std::numeric_limits<double>::infinity();uint32_t triangle=0;
  for(uint32_t i=0;i<triangles_.size();++i){const auto t=triangles_[i];double d=0;
    if(Hit(origin,direction,points_[t[0]],points_[t[1]],points_[t[2]],d)&&d<distance){distance=d;triangle=i;}}
  if(!std::isfinite(distance))return {};
  Vec3 position=Add(origin,Mul(direction,distance));uint32_t id=triangleFeature_[triangle];
  if(mode!=PickMode::Surface){
    double best=radius;bool found=false;
    for(uint32_t i=0;i<features_.size();++i){const auto& f=features_[i];
      if((mode==PickMode::Vertex&&f.kind!=Kind::Vertex)||(mode==PickMode::Edge&&f.kind!=Kind::Edge))continue;
      if(f.kind==Kind::Vertex){const auto p=points_[f.members[0]];const double d=Length(Sub(p,position));if(d<=best){best=d;id=i;found=true;}}
      else for(size_t j=1;j<f.members.size();++j){const auto a=points_[f.members[j-1]],b=points_[f.members[j]],ab=Sub(b,a);
        const auto p=Add(a,Mul(ab,std::clamp(Dot(Sub(position,a),ab)/Dot(ab,ab),0.0,1.0)));
        const double d=Length(Sub(p,position));if(d<=best){best=d;id=i;found=true;}}
    }
    if(!found)return {};
    const auto& f=features_[id];Vec3 closest=points_[f.members[0]];double bestPoint=Length(Sub(closest,position));
    for(size_t j=1;j<f.members.size();++j){const auto a=points_[f.members[j-1]],b=points_[f.members[j]],ab=Sub(b,a);
      const auto p=Add(a,Mul(ab,std::clamp(Dot(Sub(position,a),ab)/Dot(ab,ab),0.0,1.0)));const double d=Length(Sub(p,position));if(d<bestPoint){closest=p;bestPoint=d;}}
    position=closest;
  }
  const auto& feature=features_[id];const bool normal=mode==PickMode::Surface;
  return Pick{{partId_,revision_,key_,feature.kind,id},position,normal?normals_[triangle]:Vec3{},normal,feature.bounds,distance};
}
} // namespace dingcad::selection
