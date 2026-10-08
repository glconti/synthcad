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
  auto center=[](Rectangle r){return Click(r.x+r.width/2,r.y+r.height/2);};
  Require(panel.Viewport(1280,720).width==1280&&panel.Bounds(tree,1280,720).width==360,"Overlay preserves full viewport");
  auto expandedHeight=panel.Bounds(tree,1280,720).height;
  tree.state.collapsed.insert(tree.nodes[Node(tree,"Plates")].key);
  Require(panel.Bounds(tree,1280,720).height<expandedHeight,"Content-sized collapsed tree");tree.state.collapsed.clear();
  PanelInput below;below.mouse={100,expandedHeight+30};below.wheel=-1;panel.Update(tree,below,1280,720);
  Require(!panel.CapturesMouse(tree,below,1280,720)&&panel.scroll==0,"Below card passes through");
  Require(panel.Bounds(tree,720,400).height==300,"Height capped at 75 percent");
  for(float scale:{1.f,1.5f,2.f}){
    auto search=panel.Layout(tree,1280,720).search;auto physical=center(search);physical.mouse.x*=scale;physical.mouse.y*=scale;
    panel.Update(tree,LogicalInput(physical,scale),1280,720);
    Require(panel.searchFocus,"DPI-scaled hit target");
  }
  PanelInput typed;typed.text="Right";panel.Update(tree,typed,1280,720);
  Require(panel.search=="Right"&&tree.Visible(1),"Search doesn't hide geometry");
  auto l=panel.Layout(tree,1280,720);
  panel.Update(tree,Click(l.exportX+12,l.list.y+15),1280,720);
  Require(tree.ExportIndices(false).empty(),"Filtered group toggles all descendants");
  PanelInput drag;drag.mouse={600,650};drag.leftDown=true;
  Require(panel.CapturesMouse(tree,drag,1280,720),"Panel gesture captures dragged mouse");
  drag.leftDown=false;panel.Update(tree,drag,1280,720);Require(!panel.CapturesMouse(tree,drag,1280,720),"Released gesture clears capture");
  panel.Update(tree,center(panel.Layout(tree,1280,720).collapse),1280,720);
  Require(!panel.open&&!panel.searchFocus&&panel.Viewport(1280,720).width==1280,"Collapsed preserves projection");
  PanelInput find;find.find=true;panel.Update(tree,find,720,480);
  Require(panel.open&&panel.searchFocus,"Ctrl+F opens and focuses search");
  Require(panel.Layout(tree,640,400).card.x+panel.Layout(tree,640,400).card.width<WorkspaceUi{}.Toolbar(640).x,"Small window controls do not overlap");
  std::string accented="Parete più";TextEdit edit;edit.Focus(accented);PanelInput back;back.backspace=true;edit.Update(accented,back);
  Require(accented=="Parete pi","UTF-8 backspace removes a codepoint");
  PanelInput all;all.selectAll=true;edit.Update(accented,all);PanelInput replace;replace.text="Flangia";edit.Update(accented,replace);
  Require(accented=="Flangia"&&edit.anchor==7,"Selection replacement");
  PanelInput left;left.left=true;edit.Update(accented,left);PanelInput del;del.deleteKey=true;edit.Update(accented,del);Require(accented=="Flangi","Caret delete");
  WorkspaceUi ui;ui.Saved("assembly.stl",10);Require(ui.ToastVisible(13.99)&&!ui.ToastVisible(14),"Four-second export feedback");
  ui.Failed("Errore: parte più larga\nFull diagnostic");ui.Update(PanelInput{},1280,720,99);Require(!ui.loadError.empty(),"Errors persist beyond toast lifetime");
  auto errorCard=ui.ErrorCard(1280,720);Require(ui.Update(Click(errorCard.x+errorCard.width-50,errorCard.y+28),1280,720,99).reload,"Reload action");
  ui.Loaded();Require(ui.loadError.empty()&&!ui.details,"Valid reload clears error");
  ui.gesture=false;ui.Update(Click(1242,686),1280,720,11);Require(!ui.ToastVisible(11),"Toast can be dismissed");
  ui.Failed(std::string(3000,'W'));auto errorBounds=ui.ErrorCard(640,400);
  ui.Update(Click(errorBounds.x+30,errorBounds.y+85),640,400,20);Require(ui.details,"Diagnostics expand");
  PanelInput errorScroll;auto expandedError=ui.ErrorCard(640,400);errorScroll.mouse={expandedError.x+30,expandedError.y+130};errorScroll.wheel=-100;
  ui.Update(errorScroll,640,400,20);Require(ui.detailScroll>0&&ui.CapturesMouse(errorScroll,640,400,20),"Long diagnostics scroll and capture input");
  PartTree longTree;std::vector<DisplayPart> many;for(int n=0;n<100;++n){auto part=a;part.id="long"+std::to_string(n);part.name=part.id;many.push_back(part);}longTree.Reload(many);
  PartsPanel longPanel;auto longLayout=longPanel.Layout(longTree,640,400);PanelInput wheel;wheel.mouse={longLayout.list.x+30,longLayout.list.y+20};wheel.wheel=-100;
  longPanel.Update(longTree,wheel,640,400);Require(longPanel.scroll>0,"Long tree scrolls inside capped panel");
  longPanel.search="missing";longPanel.Update(longTree,PanelInput{},640,400);Require(longPanel.scroll==0,"Empty search resets scroll bounds");

  Require(SuggestedExportPath("home","a/assembly.js").filename()=="assembly.stl"&&SuggestedExportPath("home",{}).filename()=="synthcad.stl","Scene-based destination");
  ExportDialog dialog;dialog.Open("unused.stl");auto save=center(ExportDialog::Layout(1280,720).save);
  Require(!dialog.Update(save,1280,720,0,true).save,"Empty export blocked");Require(!dialog.Update(save,1280,720,2,false).save,"Invalid export blocked");
  Require(dialog.Update(save,1280,720,2,true).save,"Valid export request");
  dialog.overwrite=true;dialog.Update(center(ExportDialog::Layout(1280,720).visible),1280,720,2,true);
  Require(dialog.visibleOnly&&!dialog.overwrite,"Mode resets confirmation");
  dialog.path="custom.stl";PanelInput esc;esc.escape=true;Require(!dialog.Update(esc,1280,720,2,true).save&&!dialog.open,"Cancel does not save");
  dialog.Open("another.stl");Require(dialog.path=="custom.stl","Edited destination persists");

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
