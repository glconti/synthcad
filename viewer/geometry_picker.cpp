#include "geometry_picker.h"
#include "dimensions.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <tuple>

namespace dingcad::selection {
namespace {
Vec3 Add(Vec3 a,Vec3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
Vec3 Sub(Vec3 a,Vec3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
Vec3 Mul(Vec3 a,double s){return {a.x*s,a.y*s,a.z*s};}
double Dot(Vec3 a,Vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
Vec3 Cross(Vec3 a,Vec3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
double Length(Vec3 a){return std::sqrt(Dot(a,a));}
Vec3 Unit(Vec3 a){const auto l=Length(a);return l>0?Mul(a,1/l):Vec3{};}
Vec3 V(Vector3 a){return {a.x,a.y,a.z};}
Vec3 World(Vec3 a){return {a.x*kSceneScale,a.z*kSceneScale,-a.y*kSceneScale};}
Vec3 Model(Vec3 a){return {a.x/kSceneScale,-a.z/kSceneScale,a.y/kSceneScale};}
bool Finite(Vec3 a){return std::isfinite(a.x)&&std::isfinite(a.y)&&std::isfinite(a.z);}
struct Ray { Vec3 origin,direction; };
bool Intersect(Ray ray,Vec3 a,Vec3 b,Vec3 c,double& distance){
  const auto ab=Sub(b,a),ac=Sub(c,a),p=Cross(ray.direction,ac);
  const double determinant=Dot(ab,p);
  if(std::abs(determinant)<1e-14)return false;
  const double inverse=1/determinant;const auto t=Sub(ray.origin,a);
  const double u=Dot(t,p)*inverse;if(u< -1e-9||u>1+1e-9)return false;
  const auto q=Cross(t,ab);const double v=Dot(ray.direction,q)*inverse;
  if(v< -1e-9||u+v>1+1e-9)return false;
  distance=Dot(ac,q)*inverse;return distance>=0;
}
struct View {
  Vec3 eye,forward,right,up;
  double width,height,halfHeight;
  bool perspective;
  double Depth(Vec3 model)const{return Dot(Sub(World(model),eye),forward);}
  Vector2 Project(Vec3 model)const{
    const auto relative=Sub(World(model),eye);const double z=Dot(relative,forward);
    const double scale=height/(2*halfHeight*(perspective?z:1));
    return {float(width/2+Dot(relative,right)*scale),float(height/2-Dot(relative,up)*scale)};
  }
  Ray At(Vector2 pixel)const{
    const double x=(pixel.x-width/2)*2*halfHeight/height;
    const double y=(height/2-pixel.y)*2*halfHeight/height;
    const auto offset=Add(Mul(right,x),Mul(up,y));
    const auto worldOrigin=perspective?eye:Add(eye,offset);
    const auto worldDirection=perspective?Unit(Add(forward,offset)):forward;
    return {Model(worldOrigin),Unit(Model(worldDirection))};
  }
};
constexpr double nearClip=0.01;
}

void GeometryPicker::Reload(const PartTree& tree,const std::string& revision){
  parts_.clear();diagnostics_.clear();parts_.reserve(tree.parts.size());
  for(const auto& part:tree.parts){
    CachedPart cached;cached.id=part.id;cached.solid=part.solid;
    try {
      if(!part.solid)throw std::invalid_argument("missing placed solid");
      const auto mesh=part.solid->GetMeshGL();
      const double inf=std::numeric_limits<double>::infinity();cached.bounds={{inf,inf,inf},{-inf,-inf,-inf}};
      if(mesh.numProp<3)throw std::invalid_argument("mesh has no XYZ positions");
      for(size_t i=0;i+2<mesh.vertProperties.size();i+=mesh.numProp){
        const Vec3 p{mesh.vertProperties[i],mesh.vertProperties[i+1],mesh.vertProperties[i+2]};
        if(!Finite(p))throw std::invalid_argument("mesh has nonfinite positions");
        cached.points.push_back(p);
        cached.bounds.min={std::min(cached.bounds.min.x,p.x),std::min(cached.bounds.min.y,p.y),std::min(cached.bounds.min.z,p.z)};
        cached.bounds.max={std::max(cached.bounds.max.x,p.x),std::max(cached.bounds.max.y,p.y),std::max(cached.bounds.max.z,p.z)};
      }
      for(size_t i=0;i+2<mesh.triVerts.size();i+=3){
        std::array<uint32_t,3> t{mesh.triVerts[i],mesh.triVerts[i+1],mesh.triVerts[i+2]};
        if(t[0]<cached.points.size()&&t[1]<cached.points.size()&&t[2]<cached.points.size())cached.triangles.push_back(t);
      }
      cached.topology=Topology::Build(mesh,part.id,revision);
    } catch(const std::exception& error){diagnostics_.push_back(part.id+": feature selection unavailable: "+error.what());}
    parts_.push_back(std::move(cached));
  }
}
const Topology* GeometryPicker::Get(size_t i)const{return i<parts_.size()&&parts_[i].topology?&*parts_[i].topology:nullptr;}

std::optional<Hit> GeometryPicker::PickAt(const PartTree& tree,const Camera3D& camera,Vector2 mouse,
                                         int width,int height,ReviewMode mode,double radius)const{
  if(width<=0||height<=0||!std::isfinite(mouse.x)||!std::isfinite(mouse.y)||!std::isfinite(radius)||radius<0)return {};
  View view{V(camera.position),Unit(Sub(V(camera.target),V(camera.position))),{},{},double(width),double(height),
            camera.projection==CAMERA_PERSPECTIVE?std::tan(camera.fovy*3.14159265358979323846/360):camera.fovy/2,
            camera.projection==CAMERA_PERSPECTIVE};
  view.right=Unit(Cross(view.forward,V(camera.up)));view.up=Cross(view.right,view.forward);
  if(!Finite(view.eye)||Length(view.forward)==0||Length(view.right)==0||!std::isfinite(view.halfHeight)||view.halfHeight<=0)return {};
  auto active=[&](size_t i){return i<tree.parts.size()&&tree.parts[i].id==parts_[i].id&&tree.parts[i].solid==parts_[i].solid&&tree.Visible(i);};
  auto nearest=[&](Ray ray){
    double best=std::numeric_limits<double>::infinity();
    for(size_t i=0;i<parts_.size();++i)if(active(i))for(const auto& t:parts_[i].triangles){
      double d;const auto& p=parts_[i].points;
      if(Intersect(ray,p[t[0]],p[t[1]],p[t[2]],d)&&view.Depth(Add(ray.origin,Mul(ray.direction,d)))>=nearClip)best=std::min(best,d);
    }
    return best;
  };
  const auto mouseRay=view.At(mouse);
  std::optional<Hit> result;double bestPixels=std::numeric_limits<double>::infinity();
  auto better=[&](double pixels,double distance,size_t i,uint32_t feature){
    if(!result)return true;
    if(pixels<bestPixels-1e-7)return true;if(pixels>bestPixels+1e-7)return false;
    if(distance<result->rayDistance-1e-6)return true;if(distance>result->rayDistance+1e-6)return false;
    const auto current=result->feature?result->feature->reference.id:0;
    return std::tie(parts_[i].id,feature)<std::tie(parts_[result->partIndex].id,current);
  };
  for(size_t i=0;i<parts_.size();++i){
    if(!active(i))continue;const auto& part=parts_[i];
    if(mode==ReviewMode::Part||mode==ReviewMode::Surface){
      double distance=std::numeric_limits<double>::infinity();
      for(const auto& t:part.triangles){double d;if(Intersect(mouseRay,part.points[t[0]],part.points[t[1]],part.points[t[2]],d)&&
          view.Depth(Add(mouseRay.origin,Mul(mouseRay.direction,d)))>=nearClip)distance=std::min(distance,d);}
      if(!std::isfinite(distance))continue;
      const auto position=Add(mouseRay.origin,Mul(mouseRay.direction,distance));
      std::optional<Pick> feature;
      if(mode==ReviewMode::Surface){
        if(!part.topology)continue;
        const double depthRate=Dot(World(mouseRay.direction),view.forward);
        const double start=std::max(0.0,(nearClip-view.Depth(mouseRay.origin))/depthRate);
        feature=part.topology->RayPick(Add(mouseRay.origin,Mul(mouseRay.direction,start)),mouseRay.direction);
        if(!feature||view.Depth(feature->position)<nearClip)continue;
        feature->rayDistance+=start;
      }
      // A topology failure may still occlude a valid feature on a different part.
      if(mode==ReviewMode::Surface&&distance>nearest(mouseRay)+1e-5)continue;
      if(better(0,distance,i,feature?feature->reference.id:0)){
        result=Hit{i,feature,feature?feature->position:position,feature?feature->bounds:part.bounds,distance};bestPixels=0;
      }
      continue;
    }
    if(!part.topology)continue;
    const auto& topology=*part.topology;const auto& points=topology.Points();
    auto candidate=[&](uint32_t id,Vec3 point,double pixels){
      if(!std::isfinite(pixels)||!Finite(point)||pixels>radius||view.Depth(point)<nearClip)return;
      const auto ray=view.At(view.Project(point));
      const double distance=Dot(Sub(point,ray.origin),ray.direction);
      const double tolerance=std::max(1e-5,topology.Tolerance()*4);
      if(distance>nearest(ray)+tolerance)return;
      if(better(pixels,distance,i,id)){
        auto pick=topology.FeaturePick(id,point,distance);
        if(!pick)return;
        result=Hit{i,pick,point,pick->bounds,distance};bestPixels=pixels;
      }
    };
    for(uint32_t id=0;id<topology.Features().size();++id){const auto& f=topology.Features()[id];
      if(mode==ReviewMode::Vertex&&f.kind==Kind::Vertex){
        const auto p=points[f.members[0]];if(view.Depth(p)<nearClip)continue;const auto screen=view.Project(p);
        candidate(id,p,std::hypot(screen.x-mouse.x,screen.y-mouse.y));
      }
      if(mode!=ReviewMode::Edge||f.kind!=Kind::Edge)continue;
      for(size_t j=1;j<f.members.size();++j){
        auto a=points[f.members[j-1]],b=points[f.members[j]];double za=view.Depth(a),zb=view.Depth(b);
        if(za<nearClip&&zb<nearClip)continue;
        if(za<nearClip){a=Add(a,Mul(Sub(b,a),(nearClip-za)/(zb-za)));za=nearClip;}
        if(zb<nearClip){b=Add(b,Mul(Sub(a,b),(nearClip-zb)/(za-zb)));zb=nearClip;}
        const auto pa=view.Project(a),pb=view.Project(b);const double dx=pb.x-pa.x,dy=pb.y-pa.y;
        const double length=dx*dx+dy*dy;
        const double t=length>0?std::clamp(((mouse.x-pa.x)*dx+(mouse.y-pa.y)*dy)/length,0.0,1.0):0;
        const double pixels=std::hypot(pa.x+t*dx-mouse.x,pa.y+t*dy-mouse.y);
        // Interpolate reciprocal depth to recover the point on the 3D segment.
        const double worldT=view.perspective?(t/zb)/((1-t)/za+t/zb):t;
        candidate(id,Add(a,Mul(Sub(b,a),worldT)),pixels);
      }
    }
  }
  return result;
}
} // namespace dingcad::selection
