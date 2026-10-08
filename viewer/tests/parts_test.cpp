#include "part_tree.h"
#include "parts_panel.h"
#include "stl_export.h"
#include "dimensions.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <cmath>
#include <chrono>
#include <cstring>

void Require(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
using namespace dingcad;
DisplayPart Part(const char *id,const char *name,std::vector<std::string> group,bool exp=true,double x=0){
  return {std::make_shared<manifold::Manifold>(manifold::Manifold::Cube({2,2,2}).Translate({x,10,20})),
          BLUE,id,name,group,exp};
}
size_t Node(const PartTree &t,const char *name){
  for(size_t n=0;n<t.nodes.size();++n)if(t.nodes[n].name==name)return n;
  throw std::runtime_error("Missing node");
}
PanelInput Click(float x,float y){PanelInput i;i.mouse={x,y};i.pressed=i.leftDown=true;return i;}
std::string Read(const std::filesystem::path &p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
int main(){try{
  PartTree tree;
  auto wall=Part("wall","Wall",{"References"},false,-5);
  auto a=Part("a","Left",{"Object","Plates"},true,50);
  auto b=Part("b","Right",{"Object","Plates"},true,51); // overlap intentionally: export must not union
  tree.Reload({wall,a,b});
  Require(tree.nodes.size()==6&&tree.Rows("").size()==6,"Nested tree contains ancestors and parts");
  auto group=Node(tree,"Plates");
  tree.Toggle(Node(tree,"Left"),false);
  Require(tree.Checked(group,false)==CheckState::Mixed&&tree.Checked(group,true)==CheckState::All,"Independent tristates");
  auto rows=tree.Rows("riGHt");
  Require(rows.size()==3&&tree.nodes[rows[0].node].name=="Object","Search retains ancestors, case insensitive");
  Require(tree.ExportIndices(false).size()==2&&tree.ExportIndices(true).size()==1,"Hidden exports only in all mode");
  tree.Toggle(group,false);
  Require(tree.Visible(1)&&tree.Visible(2),"Group action includes filtered-out descendants");
  tree.Toggle(group,true);
  Require(tree.ExportIndices(false).empty()&&tree.Visible(1),"Group export does not hide parts");
  tree.Toggle(group,true);
  tree.Toggle(Node(tree,"Left"),false);
  tree.Select(Node(tree,"Right"));tree.Isolate();
  Require(!tree.Visible(0)&&!tree.Visible(1)&&tree.Visible(2),"Isolation selected leaf");
  tree.Toggle(Node(tree,"Wall"),false); // temporary visibility change
  tree.Isolate();
  Require(tree.Visible(0)&&!tree.Visible(1)&&tree.Visible(2),"Exit restores exact previous visibility");
  tree.state.collapsed.insert(tree.nodes[group].key);
  Require(tree.Rows("").size()==4&&tree.Rows("Right").size()==3,"Search opens filtered path temporarily");
  const auto selection=tree.state.selected;
  tree.Reload({b,wall,a,Part("new","New",{"Object"},false)});
  Require(tree.state.selected==selection&&!tree.Visible(2)&&tree.Visible(0),"Reload reconciles by ID across reorder/add");
  Require(tree.state.collapsed.size()==1&&!tree.state.flags.at("new").exportable,"Group openness retained, new defaults used");
  auto updated=a;updated.exportable=false;
  tree.Reload({wall,updated,b});
  Require(tree.state.flags.at("a").exportable,"User export override persists across source-default changes");
  tree.Select(Node(tree,"Right"));tree.Isolate();
  tree.Reload({wall,updated,b,Part("new","New",{},true)});
  Require(!tree.Visible(3),"New part stays hidden during isolation");
  tree.Reload({wall,updated,Part("new","New",{},true)});
  Require(!tree.state.isolated&&tree.state.selected.empty()&&tree.Visible(0)&&tree.Visible(2),"Removed isolated selection restores visibility");
  Require(!tree.state.flags.count("b"),"Removed IDs pruned");
  tree.ShowAll();for(size_t n=0;n<tree.parts.size();++n)Require(tree.Visible(n),"Show all restores every part");
  PartTree defaults;defaults.Reload({a});defaults.Reload({updated});
  Require(!defaults.state.flags.at("a").exportable,"Unmodified exportability follows model default");

  tree.Reload({wall,a,b});tree.ShowAll();tree.state.collapsed.clear();
  tree.state.flags.at("a").exportable=true;tree.state.flags.at("b").exportable=true;
  const auto solid=tree.ExportSolid(false);
  Require(solid&&std::abs(solid->Volume()-16)<1e-8,"Composed export does not fuse overlapping parts");
  auto bounds=solid->BoundingBox();
  Require(bounds.min.x==50&&bounds.max.x==53&&bounds.min.y==10&&bounds.min.z==20,"Original CAD coordinates, no recentering or scale");
  tree.Toggle(Node(tree,"Wall"),true);
  Require(tree.ExportIndices(false).size()==3,"Explicit reference export opt-in");
  tree.Toggle(Node(tree,"Wall"),true);

  PartsPanel panel;
  Require(panel.Viewport(1280,720).x==0&&panel.Viewport(1280,720).width==1280&&panel.Bounds(1280,720).width==320,"Overlay keeps full scene viewport");
  panel.Update(tree,Click(50,60),1280,720);
  PanelInput typed;typed.mouse={50,60};typed.text="Right";panel.Update(tree,typed,1280,720);
  Require(panel.searchFocus&&panel.search=="Right"&&tree.Visible(1),"Search editing leaves geometry visible");
  panel.Update(tree,Click(300,154),1280,720); // filtered Object group export
  Require(tree.ExportIndices(false).empty(),"Filtered group checkbox affects all descendants");
  PanelInput drag;drag.mouse={600,400};drag.leftDown=true;
  Require(panel.CapturesMouse(drag,1280,720),"Panel drag cannot leak into camera");
  panel.Update(tree,drag,1280,720);
  drag.leftDown=false;panel.Update(tree,drag,1280,720);
  Require(!panel.CapturesMouse(drag,1280,720),"Camera released after panel gesture");
  panel.Update(tree,Click(25,20),1280,720);
  Require(!panel.open&&panel.Viewport(1280,720).width==1280&&!panel.searchFocus,"Collapsed panel preserves viewport and releases focus");
  panel.Update(tree,PanelInput{},720,480);
  Require(panel.Viewport(720,480).height==480,"Resize viewport");
  panel.Update(tree,Click(25,20),720,480);
  Require(panel.open&&panel.Viewport(720,480).width==720&&panel.Bounds(720,480).width==320,"Reopening overlay preserves resized projection");
  DimensionControls dimensions;const auto local=Vector2{700,460};
  UpdateDimensionControls(dimensions,DimensionButtonBounds(720,480),local,false,true,true);
  Require(dimensions.mode==DimensionMode::All&&dimensions.buttonGesture,"Dimension control stays accessible beside overlay");

  ExportDialog dialog;dialog.Open("unused.stl");
  auto save=Click(820,478); // 1280x720 dialog origin (330,210), save (800,462)
  Require(!dialog.Update(save,1280,720,0,true).save,"Empty export button blocked");
  Require(!dialog.Update(save,1280,720,2,false).save,"Invalid scene export blocked");
  Require(dialog.Update(save,1280,720,2,true).save,"Valid export requests save");
  dialog.overwrite=true;dialog.Update(Click(360,300),1280,720,2,true);
  Require(dialog.visibleOnly&&!dialog.overwrite,"Mode change resets overwrite consent");
  PanelInput esc;esc.escape=true;Require(!dialog.Update(esc,1280,720,2,true).save&&!dialog.open,"Cancel does not request a write");

  const auto path=std::filesystem::temp_directory_path()/("dingcad-export-test-"+
      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".stl");
  std::ofstream(path)<<"sentinel";std::string error;
  Require(ExportParts(tree,false,true,path,true,error)==ExportResult::Empty&&Read(path)=="sentinel","Empty export leaves existing file intact");
  tree.Toggle(Node(tree,"Plates"),true);
  Require(ExportParts(tree,false,false,path,true,error)==ExportResult::Invalid&&Read(path)=="sentinel","Invalid scene leaves file intact");
  Require(ExportParts(tree,false,true,path,false,error)==ExportResult::ConfirmOverwrite&&Read(path)=="sentinel","Overwrite requires confirmation before writing");
  tree.Toggle(Node(tree,"Left"),false);
  Require(ExportParts(tree,true,true,path,true,error)==ExportResult::Saved,"Visible export writes selected geometry");
  Require(std::filesystem::file_size(path)==84+12*50,"Visible STL triangle count");
  auto bytes=Read(path);float xyz[3];std::memcpy(xyz,bytes.data()+96,12);
  Require(xyz[0]>=51&&xyz[0]<=53&&xyz[1]>=10&&xyz[2]>=20,"STL retains original coordinates");
  Require(ExportParts(tree,false,true,path,true,error)==ExportResult::Saved&&std::filesystem::file_size(path)==84+24*50,"All export includes hidden part, excludes wall");
  std::filesystem::remove(path);
  tree.Toggle(Node(tree,"Plates"),true);
  Require(ExportParts(tree,false,true,path,false,error)==ExportResult::Empty&&!std::filesystem::exists(path),"Empty export produces no new file");
  tree.Reload({});Require(tree.nodes.empty()&&!tree.ExportSolid(false),"Explicit empty list never exports original scene");
  std::cout<<"PASS tree, reload, input boundaries, isolation, composition and selective STL file safety\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
