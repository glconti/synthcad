#include "../geometry_picker.h"
#include "raymath.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace dingcad;
using namespace dingcad::selection;
using manifold::Manifold;
void Require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
bool Near(double a,double b){return std::abs(a-b)<1e-4;}
DisplayPart Part(std::string id,Manifold solid){
  DisplayPart part;part.id=std::move(id);part.name=part.id;
  part.solid=std::make_shared<Manifold>(std::move(solid));part.sourceSolid=part.solid;return part;
}
Camera3D TopCamera(){return {{0,10,0},{0,0,0},{0,0,-1},4,CAMERA_ORTHOGRAPHIC};}
void Basic(){
  PartTree tree;tree.Reload({Part("box",Manifold::Cube({20,20,20},true))});
  GeometryPicker picker;picker.Reload(tree,"r1");const auto camera=TopCamera();
  auto hit=picker.PickAt(tree,camera,{200,200},400,400,ReviewMode::Surface);
  Require(hit&&hit->feature&&hit->feature->reference.kind==Kind::PlanarFace,"Box surface semantic face");
  Require(Near(hit->position.z,10)&&Near(hit->position.x,0)&&Near(hit->position.y,0),"Renderer Z-up conversion and mm positions");
  Require(Near(hit->rayDistance,90),"Ray distances use model mm");
  const auto reference=hit->feature->reference;
  auto edge=picker.PickAt(tree,camera,{305,200},400,400,ReviewMode::Edge);
  Require(edge&&edge->feature->reference.kind==Kind::Edge,"Silhouette edge outside triangle silhouette");
  Require(Near(edge->position.x,10)&&Near(edge->position.z,10),"Back edge rejected by owner self occlusion");
  Require(!picker.PickAt(tree,camera,{309,200},400,400,ReviewMode::Edge),"Logical pixel radius respected");
  auto vertex=picker.PickAt(tree,camera,{304,304},400,400,ReviewMode::Vertex);
  Require(vertex&&Near(vertex->position.z,10),"Visible corner picked and back corner rejected");
  Require(!picker.PickAt(tree,camera,{200,200},400,400,ReviewMode::Vertex),"No invented center vertex");
  // The engine accepts logical dimensions; a high-DPI framebuffer is deliberately absent.
  auto dpi=picker.PickAt(tree,camera,{605,400},800,800,ReviewMode::Edge);
  Require(dpi&&dpi->feature->reference.id==edge->feature->reference.id,"Logical inputs invariant to framebuffer DPI");
  picker.Reload(tree,"r2");Require(!picker.Get(0)->Contains(reference),"Revision reload invalidates old references");
  tree.state.flags.at("box").visible=false;
  Require(!picker.PickAt(tree,camera,{200,200},400,400,ReviewMode::Part),"Hidden part omitted");
}
void Overlap(){
  const auto cube=Manifold::Cube({20,20,20},true);PartTree tree;
  tree.Reload({Part("z-owner",cube),Part("a-owner",cube)});GeometryPicker picker;picker.Reload(tree,"r");
  auto hit=picker.PickAt(tree,TopCamera(),{200,200},400,400,ReviewMode::Surface);
  Require(hit&&hit->feature->reference.partId=="a-owner","Coincident ties use instance ID");
  auto tiedEdge=picker.PickAt(tree,TopCamera(),{305,200},400,400,ReviewMode::Edge);
  Require(tiedEdge&&tiedEdge->feature->reference.partId=="a-owner","Coincident edge ties use instance ID");
  auto tiedVertex=picker.PickAt(tree,TopCamera(),{304,304},400,400,ReviewMode::Vertex);
  Require(tiedVertex&&tiedVertex->feature->reference.partId=="a-owner","Coincident vertex ties use instance ID");
  std::reverse(tree.parts.begin(),tree.parts.end());picker.Reload(tree,"r");
  hit=picker.PickAt(tree,TopCamera(),{200,200},400,400,ReviewMode::Surface);
  Require(hit&&hit->feature->reference.partId=="a-owner","Tie independent of tree order");
  const auto ownerReference=hit->feature->reference;
  Require(!picker.Get(1)->Contains(ownerReference),"Reference cannot cross owning instances");
  tree.Reload({Part("back",cube),Part("front",Manifold::Cube({24,24,4},true).Translate({0,0,20}))});picker.Reload(tree,"r");
  auto part=picker.PickAt(tree,TopCamera(),{200,200},400,400,ReviewMode::Part);
  Require(part&&tree.parts[part->partIndex].id=="front","Nearest visible part");
  Require(!picker.PickAt(tree,TopCamera(),{300,200},400,400,ReviewMode::Edge),"Cross-part occlusion rejects hidden edge");
  Require(!picker.PickAt(tree,TopCamera(),{300,300},400,400,ReviewMode::Vertex),"Cross-part occlusion rejects hidden vertex");
  tree.state.flags.at("front").visible=false;
  Require(bool(picker.PickAt(tree,TopCamera(),{300,200},400,400,ReviewMode::Edge)),"Hidden occluder omitted");
  tree.parts[0].solid=std::make_shared<Manifold>(cube.Translate({100,0,0}));
  Require(!picker.PickAt(tree,TopCamera(),{200,200},400,400,ReviewMode::Part),"Changed ownership snapshot requires reload");
  tree.Reload({Part("lower",cube),Part("upper",cube.Translate({0,0,20}))});picker.Reload(tree,"touching");
  hit=picker.PickAt(tree,TopCamera(),{200,200},400,400,ReviewMode::Surface);
  Require(hit&&hit->feature->reference.partId=="upper"&&Near(hit->position.z,30),"Touching solids retain separate nearest ownership");
}
void Curves(){
  PartTree tree;tree.Reload({Part("cylinder",Manifold::Cylinder(20,10,10,64,true))});
  GeometryPicker picker;picker.Reload(tree,"r");
  auto rim=picker.PickAt(tree,TopCamera(),{303,200},400,400,ReviewMode::Edge);
  Require(rim&&rim->feature->reference.kind==Kind::Edge,"Cylinder circular rim outside silhouette");
  Require(!picker.PickAt(tree,TopCamera(),{300,200},400,400,ReviewMode::Vertex),"Cylinder tessellation is not corner vertices");
  Camera3D side={{4,0,0},{0,0,0},{0,1,0},4,CAMERA_ORTHOGRAPHIC};
  auto patch=picker.PickAt(tree,side,{200,200},400,400,ReviewMode::Surface);
  Require(patch&&patch->feature->reference.kind==Kind::CurvedPatch,"Cylinder side is curved patch");
  tree.Reload({Part("sphere",Manifold::Sphere(10,64))});picker.Reload(tree,"r");
  patch=picker.PickAt(tree,TopCamera(),{200,200},400,400,ReviewMode::Surface);
  Require(patch&&patch->feature->reference.kind==Kind::CurvedPatch,"Sphere surface is curved patch");
  Require(!picker.PickAt(tree,TopCamera(),{300,200},400,400,ReviewMode::Edge),"Sphere has no fake tessellation edges");
}
void Perspective(){
  PartTree tree;tree.Reload({Part("box",Manifold::Cube({20,20,20},true))});GeometryPicker picker;picker.Reload(tree,"r");
  Camera3D camera={{4,5,6},{0,0,0},{0,1,0},45,CAMERA_PERSPECTIVE};
  auto face=picker.PickAt(tree,camera,{200,200},400,400,ReviewMode::Surface);
  Require(face&&face->feature,"Perspective center ray hits");
  // A wide edge radius must still produce a point on an actual segment, with no normal.
  auto edge=picker.PickAt(tree,camera,{200,200},400,400,ReviewMode::Edge,150);
  Require(edge&&edge->feature&&!edge->feature->hasNormal,"Perspective edge pick");
  const auto p=edge->position;
  const int boundary=int(Near(std::abs(p.x),10))+int(Near(std::abs(p.y),10))+int(Near(std::abs(p.z),10));
  Require(boundary>=2,"Perspective-correct snap lies on box edge");
  const Vector3 expectedWorld={1,-0.3f,1}; // CAD {10,-10,-3}, on a visible edge.
  const auto forward=Vector3Normalize(Vector3Subtract(camera.target,camera.position));
  const auto right=Vector3Normalize(Vector3CrossProduct(forward,camera.up));
  const auto up=Vector3CrossProduct(right,forward);
  const auto delta=Vector3Subtract(expectedWorld,camera.position);
  const float scale=200/(std::tan(camera.fovy*DEG2RAD/2)*Vector3DotProduct(delta,forward));
  const Vector2 pixel={200+Vector3DotProduct(delta,right)*scale,200-Vector3DotProduct(delta,up)*scale};
  edge=picker.PickAt(tree,camera,pixel,400,400,ReviewMode::Edge,1);
  Require(edge&&Near(edge->position.x,10)&&Near(edge->position.y,-10)&&Near(edge->position.z,-3),
          "Perspective interpolation reconstructs projected 3D point, including depth");
  camera.target={4,6,6};
  Require(!picker.PickAt(tree,camera,{200,200},400,400,ReviewMode::Edge,10000),"Geometry behind camera rejected");
  Require(!picker.PickAt(tree,camera,{200,200},0,400,ReviewMode::Part),"Invalid viewport rejected");
}
void Fallback(){
  PartTree tree;tree.Reload({Part("thin",Manifold::Cube({20,20,1e-7},true))});
  const auto source=tree.parts[0].sourceSolid;GeometryPicker picker;picker.Reload(tree,"r");
  Require(!picker.Get(0)&&!picker.Diagnostics().empty(),"Weld-collapsed topology produces diagnostic");
  auto hit=picker.PickAt(tree,TopCamera(),{200,200},400,400,ReviewMode::Part);
  Require(hit&&!hit->feature,"Raw triangle part picking survives topology failure");
  Require(!picker.PickAt(tree,TopCamera(),{200,200},400,400,ReviewMode::Surface),"Topology failure never invents triangle faces");
  Require(tree.parts[0].sourceSolid==source,"Picker preserves source solid handle");
  auto instance=Part("placed-instance",Manifold::Cube({20,20,20},true).Translate({20,0,0}));
  instance.sourceSolid=std::make_shared<Manifold>(Manifold::Cube({20,20,20},true));
  const auto unplaced=instance.sourceSolid;tree.Reload({instance});picker.Reload(tree,"placed");
  auto camera=TopCamera();camera.position.x=2;camera.target.x=2;
  hit=picker.PickAt(tree,camera,{200,200},400,400,ReviewMode::Surface);
  Require(hit&&Near(hit->position.x,20)&&hit->feature->reference.partId=="placed-instance","Placed geometry picked with owning instance identity");
  Require(tree.parts[0].sourceSolid==unplaced,"Unplaced source handle preserved");
}
int main(){try{Basic();Overlap();Curves();Perspective();Fallback();std::cout<<"Geometry picker tests PASS\n";return 0;}
catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
