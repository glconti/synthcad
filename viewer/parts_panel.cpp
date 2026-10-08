#include "parts_panel.h"
#include "raymath.h"
#include <algorithm>

namespace dingcad {
namespace {
const Color ink{42,57,53,255},muted{107,121,116,255},accent{59,130,94,255},paper{248,249,245,255};
bool Hit(const PanelInput &i,Rectangle r){return i.pressed&&CheckCollisionPointRec(i.mouse,r);}
void EraseLast(std::string &s){if(s.empty())return;size_t pos=s.size()-1;while(pos>0&&(static_cast<unsigned char>(s[pos])&0xc0)==0x80)--pos;s.resize(pos);}
void Edit(std::string &s,bool &selected,const PanelInput &i){
  if(i.selectAll)selected=true;
  if(i.backspace||!i.text.empty()){
    if(selected){s.clear();selected=false;}else if(i.backspace)EraseLast(s);
    for(char c:i.text)if(static_cast<unsigned char>(c)>=32&&c!=127&&s.size()<4096)s+=c;
  }
}
std::string Fit(std::string s,Font f,float size,float width){
  if(MeasureTextEx(f,s.c_str(),size,0).x<=width)return s;
  while(!s.empty()&&MeasureTextEx(f,(s+"...").c_str(),size,0).x>width)EraseLast(s);
  return s.empty()?"":s+"...";
}
void Label(const std::string &text,Font f,float x,float y,float width,float size=16,Color c=ink){
  auto s=Fit(text,f,size,width);DrawTextEx(f,s.c_str(),{x,y},size,0,c);
}
void Button(Rectangle r,const char *label,Font f,bool enabled=true){
  DrawRectangleRounded(r,0.16f,4,enabled?Color{226,234,227,255}:Color{237,239,235,255});
  Label(label,f,r.x+7,r.y+6,r.width-14,13,enabled?ink:muted);
}
void Check(Rectangle r,CheckState s){
  DrawRectangleLinesEx(r,1,muted);
  if(s!=CheckState::None)DrawRectangleRec({r.x+3,r.y+(s==CheckState::Mixed?7:3),r.width-6,s==CheckState::Mixed?3:r.height-6},accent);
}
Rectangle DialogBounds(int width,int height){return {std::max(8.0f,(width-620.0f)/2),std::max(8.0f,(height-300.0f)/2),std::min(620.0f,width-16.0f),300};}
}
PanelInput ReadPanelInput(){
  PanelInput i;i.mouse=GetMousePosition();i.wheel=GetMouseWheelMove();
  i.pressed=IsMouseButtonPressed(MOUSE_BUTTON_LEFT);i.leftDown=IsMouseButtonDown(MOUSE_BUTTON_LEFT);
  i.rightPressed=IsMouseButtonPressed(MOUSE_BUTTON_RIGHT);i.rightDown=IsMouseButtonDown(MOUSE_BUTTON_RIGHT);
  i.backspace=IsKeyPressed(KEY_BACKSPACE)||IsKeyPressedRepeat(KEY_BACKSPACE);
  i.enter=IsKeyPressed(KEY_ENTER);i.escape=IsKeyPressed(KEY_ESCAPE);
  const bool ctrl=IsKeyDown(KEY_LEFT_CONTROL)||IsKeyDown(KEY_RIGHT_CONTROL)||IsKeyDown(KEY_LEFT_SUPER)||IsKeyDown(KEY_RIGHT_SUPER);
  i.selectAll=ctrl&&IsKeyPressed(KEY_A);
  for(int c=GetCharPressed();c;c=GetCharPressed())if(!ctrl){int n=0;const char *text=CodepointToUTF8(c,&n);i.text.append(text,n);}
  if(ctrl&&IsKeyPressed(KEY_V)){const char *text=GetClipboardText();if(text)i.text=text;}
  return i;
}
Rectangle PartsPanel::Viewport(int width,int height) const{
  const float panel=open?std::min(320.0f,std::max(0.0f,width-160.0f)):0;
  return {panel,0,std::max(1.0f,width-panel),static_cast<float>(std::max(1,height))};
}
bool PartsPanel::CapturesMouse(const PanelInput &i,int width,int height) const{
  return gesture||(open?i.mouse.x<Viewport(width,height).x:CheckCollisionPointRec(i.mouse,{8,8,76,30}));
}
PanelActions PartsPanel::Update(PartTree &tree,const PanelInput &i,int width,int height){
  PanelActions a;
  if(i.pressed||i.rightPressed)gesture=CapturesMouse(i,width,height);
  const float w=Viewport(width,height).x;
  if(Hit(i,{8,8,76,30})){open=!open;searchFocus=false;return a;}
  if(!open){searchFocus=false;if(!i.leftDown&&!i.rightDown&&!i.pressed&&!i.rightPressed)gesture=false;return a;}
  if(i.pressed)searchFocus=CheckCollisionPointRec(i.mouse,{8,48,w-16,28});
  if(searchFocus){auto before=search;Edit(search,selectText,i);if(before!=search)scroll=0;if(i.escape||i.enter)searchFocus=false;}
  if(Hit(i,{w-95,8,87,30}))a.openExport=true;
  if(Hit(i,{8,84,w/3-10,29})&&(tree.Selection()||tree.state.isolated))tree.Isolate();
  if(Hit(i,{w/3+3,84,w/3-10,29}))tree.ShowAll();
  if(Hit(i,{2*w/3-2,84,w/3-6,29}))a.frame=true;
  auto rows=tree.Rows(search);
  const float listTop=142,rowHeight=26;
  const float available=std::max(0.0f,height-listTop-8);
  if(i.mouse.x<w&&i.mouse.y>=listTop)scroll-=i.wheel*rowHeight*3;
  scroll=Clamp(scroll,0,std::max(0.0f,rows.size()*rowHeight-available));
  if(i.pressed&&i.mouse.x<w&&i.mouse.y>=listTop&&i.mouse.y<height-8){
    const size_t row=static_cast<size_t>((i.mouse.y-listTop+scroll)/rowHeight);
    if(row<rows.size()){
      const auto &r=rows[row];auto &n=tree.nodes[r.node];
      if(i.mouse.x>=w-30)tree.Toggle(r.node,true);
      else if(i.mouse.x>=w-58)tree.Toggle(r.node,false);
      else if(n.group&&i.mouse.x<25+r.depth*14){
        if(!tree.state.collapsed.erase(n.key))tree.state.collapsed.insert(n.key);
      }else tree.Select(r.node);
    }
  }
  if(!i.leftDown&&!i.rightDown&&!i.pressed&&!i.rightPressed)gesture=false;
  return a;
}
void PartsPanel::Draw(const PartTree &tree,Font f,int width,int height) const{
  if(!open){Button({8,8,76,30},"Parti >",f);return;}
  float w=Viewport(width,height).x;DrawRectangle(0,0,static_cast<int>(w),height,paper);DrawLine(static_cast<int>(w)-1,0,static_cast<int>(w)-1,height,{205,213,205,255});
  Button({8,8,76,30},"< Parti",f);Button({w-95,8,87,30},"Esporta",f);
  DrawRectangleRec({8,48,w-16,28},searchFocus?Color{230,241,231,255}:WHITE);
  Label(search.empty()?"Cerca parti...":search,f,14,54,w-28,16,search.empty()?muted:ink);
  Button({8,84,w/3-10,29},tree.state.isolated?"Esci":"Isola",f,tree.Selection().has_value()||tree.state.isolated);
  Button({w/3+3,84,w/3-10,29},"Mostra tutto",f);
  bool canFrame=false;if(auto n=tree.Selection())for(auto p:tree.nodes[*n].parts)canFrame|=tree.Visible(p);
  Button({2*w/3-2,84,w/3-6,29},"Inquadra",f,canFrame);
  Label(std::to_string(tree.parts.size())+" parti",f,10,122,w-85,13,muted);
  Label("V",f,w-55,122,22,13,muted);Label("STL",f,w-31,122,27,13,muted);
  BeginScissorMode(0,142,static_cast<int>(w),std::max(0,height-150));
  auto rows=tree.Rows(search);
  for(size_t index=0;index<rows.size();++index){
    float y=142+index*26-scroll;if(y+26<142||y>height)continue;
    const auto &r=rows[index];const auto &n=tree.nodes[r.node];
    if(n.key==tree.state.selected)DrawRectangleRec({4,y,w-8,25},{216,231,217,255});
    const float x=9+r.depth*14;
    if(n.group)Label(tree.state.collapsed.count(n.key)&&search.empty()?">":"v",f,x,y+5,15);
    else DrawRectangle(static_cast<int>(x+1),static_cast<int>(y+9),8,8,tree.parts[n.parts.front()].color);
    Label(n.name,f,x+16,y+5,w-82-x,15,tree.Checked(r.node,false)==CheckState::None?muted:ink);
    Check({w-55,y+5,17,17},tree.Checked(r.node,false));Check({w-27,y+5,17,17},tree.Checked(r.node,true));
  }
  EndScissorMode();
  if(rows.empty())Label("Nessun risultato",f,12,154,w-24,16,muted);
  const float available=std::max(0.0f,height-150.0f),total=rows.size()*26.0f;
  if(total>available&&available>0)DrawRectangleRec({w-4,142+scroll/total*available,3,std::max(8.0f,available*available/total)},muted);
}
void ExportDialog::Open(const std::string &defaultPath){open=true;if(path.empty())path=defaultPath;error.clear();overwrite=false;pathFocus=false;}
PanelActions ExportDialog::Update(const PanelInput &i,int width,int height,size_t count,bool valid){
  PanelActions a;if(!open)return a;auto r=DialogBounds(width,height);
  if(i.escape||Hit(i,{r.x+12,r.y+252,100,30})){open=false;overwrite=false;return a;}
  if(Hit(i,{r.x+12,r.y+48,r.width-24,28})){visibleOnly=false;overwrite=false;error.clear();}
  if(Hit(i,{r.x+12,r.y+81,r.width-24,28})){visibleOnly=true;overwrite=false;error.clear();}
  if(i.pressed)pathFocus=CheckCollisionPointRec(i.mouse,{r.x+12,r.y+147,r.width-24,31});
  if(pathFocus){auto before=path;Edit(path,selectText,i);if(path!=before){overwrite=false;error.clear();}}
  if(Hit(i,{r.x+r.width-150,r.y+252,138,30})&&count>0&&valid&&!path.empty())a.save=true;
  return a;
}
void ExportDialog::Draw(Font f,int width,int height,size_t count,bool valid) const{
  if(!open)return;DrawRectangle(0,0,width,height,Fade(BLACK,0.28f));auto r=DialogBounds(width,height);DrawRectangleRec(r,paper);
  Label("Esporta STL",f,r.x+12,r.y+13,r.width-24,21);
  Check({r.x+12,r.y+53,17,17},!visibleOnly?CheckState::All:CheckState::None);
  Label("Tutte le esportabili (anche nascoste)",f,r.x+38,r.y+53,r.width-50);
  Check({r.x+12,r.y+86,17,17},visibleOnly?CheckState::All:CheckState::None);
  Label("Solo esportabili visibili",f,r.x+38,r.y+86,r.width-50);
  Label("Percorso file",f,r.x+12,r.y+124,r.width-24,14,muted);
  DrawRectangleRec({r.x+12,r.y+147,r.width-24,31},pathFocus?Color{230,241,231,255}:WHITE);
  // Keep the end visible while editing long paths.
  std::string tail=path;while(!tail.empty()&&MeasureTextEx(f,tail.c_str(),15,0).x>r.width-38){
    size_t next=1;while(next<tail.size()&&(static_cast<unsigned char>(tail[next])&0xc0)==0x80)++next;
    tail.erase(0,next);
  }
  Label(tail,f,r.x+18,r.y+155,r.width-36,15);
  Label(valid?std::to_string(count)+" parti incluse":"Export disabilitato: correggere la scena",f,r.x+12,r.y+191,r.width-24,16,valid?ink:MAROON);
  Label(overwrite?"Il file esiste: confermare Sostituisci.":error,f,r.x+12,r.y+219,r.width-24,14,MAROON);
  Button({r.x+12,r.y+252,100,30},"Annulla",f);
  Button({r.x+r.width-150,r.y+252,138,30},overwrite?"Sostituisci":"Salva STL",f,count>0&&valid&&!path.empty());
}
}
