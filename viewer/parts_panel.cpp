#include "parts_panel.h"
#include "brand.h"
#include "raymath.h"
#include <algorithm>
#include <cmath>

namespace dingcad {
namespace {
constexpr float margin=12,pad=12,rowHeight=30,control=32;
float Indent(int depth){return std::min(depth*14.f,84.f);}
const Color ink{39,52,46,255},muted{91,108,98,255},accent{48,108,78,255};
const Color paper{247,246,239,255},glass{201,211,199,190};
float drawScale=1;
Vector2 Mouse(){auto p=GetMousePosition();return {p.x/drawScale,p.y/drawScale};}
bool Over(Rectangle r){return CheckCollisionPointRec(Mouse(),r);}
bool Hit(const PanelInput &i,Rectangle r){return i.pressed&&CheckCollisionPointRec(i.mouse,r);}
size_t Prev(const std::string &s,size_t p){if(!p)return 0;--p;while(p&&(static_cast<unsigned char>(s[p])&0xc0)==0x80)--p;return p;}
size_t Next(const std::string &s,size_t p){if(p<s.size())++p;while(p<s.size()&&(static_cast<unsigned char>(s[p])&0xc0)==0x80)++p;return p;}
void Clip(Rectangle r){BeginScissorMode(int(r.x*drawScale),int(r.y*drawScale),std::max(0,int(r.width*drawScale)),std::max(0,int(r.height*drawScale)));}
std::string Fit(std::string s,Font f,float size,float width){
 if(MeasureTextEx(f,s.c_str(),size,0).x<=width)return s;
 while(!s.empty()&&MeasureTextEx(f,(s+"...").c_str(),size,0).x>width)s.resize(Prev(s,s.size()));
 return s.empty()?"":s+"...";
}
void Label(const std::string &text,Font f,float x,float y,float width,float size=16,Color c=ink){
 auto s=Fit(text,f,size,width);DrawTextEx(f,s.c_str(),{x,y},size,0,c);
}
void Card(Rectangle r,Color color=glass){DrawRectangleRounded(r,0.045f,8,color);}
void Button(Rectangle r,const char *label,Font f,bool enabled=true,bool primary=false){
 Color bg=primary?accent:Color{234,239,228,180};
 if(Over(r)&&enabled)bg=primary?Color{37,90,63,255}:Color{222,231,214,245};
 if(!enabled)bg={219,224,213,120};
 DrawRectangleRounded(r,0.18f,6,bg);
 Label(label,f,r.x+8,r.y+7,r.width-16,15,enabled?(primary?WHITE:ink):muted);
}
void Cross(Rectangle r,Color c=muted){float x=r.x+r.width/2,y=r.y+r.height/2;DrawLineEx({x-4,y-4},{x+4,y+4},1.5f,c);DrawLineEx({x+4,y-4},{x-4,y+4},1.5f,c);}
void Chevron(float x,float y,bool closed){
 if(closed){DrawLineEx({x-2,y-4},{x+2,y},1.5f,muted);DrawLineEx({x+2,y},{x-2,y+4},1.5f,muted);}
 else{DrawLineEx({x-4,y-2},{x,y+2},1.5f,muted);DrawLineEx({x,y+2},{x+4,y-2},1.5f,muted);}
}
void Tooltip(const std::string &s,Font f,int width,int height){
 if(s.empty())return;
 const float maxWidth=std::min(640.f,width-44.f);std::vector<std::string> lines;std::string line;
 for(size_t p=0;p<s.size();){
  auto next=Next(s,p);auto codepoint=s.substr(p,next-p);
  if(codepoint=="\n"){lines.push_back(line);line.clear();}
  else{
   if(!line.empty()&&MeasureTextEx(f,(line+codepoint).c_str(),14,0).x>maxWidth){lines.push_back(line);line.clear();}
   line+=codepoint;
  }
  p=next;
 }
 lines.push_back(line);float textWidth=0;
 for(const auto &text:lines)textWidth=std::max(textWidth,MeasureTextEx(f,text.c_str(),14,0).x);
 auto m=Mouse();float w=textWidth+20,h=lines.size()*18.f+12;
 float x=Clamp(m.x+12,12,std::max(12.f,width-w-12)),y=Clamp(m.y+22,12,std::max(12.f,height-h-12));
 Card({x,y,w,h},{39,52,46,245});
 for(size_t n=0;n<lines.size();++n)DrawTextEx(f,lines[n].c_str(),{x+10,y+6+n*18},14,0,WHITE);
}

void Field(Rectangle r,const std::string &s,const char *placeholder,const TextEdit &e,bool focus,Font f,float reserve=0){
 DrawRectangleRounded(r,0.15f,5,{250,251,245,220});
 if(focus)DrawRectangleRoundedLinesEx(r,0.15f,5,1.5f,accent);
 Rectangle inside{r.x+8,r.y+4,r.width-16-reserve,r.height-8};Clip(inside);
 const float caretWidth=MeasureTextEx(f,s.substr(0,std::min(e.caret,s.size())).c_str(),16,0).x;
 const float offset=focus?std::max(0.0f,caretWidth-inside.width+3):0;
 if(focus&&e.anchor!=e.caret){
  float a=MeasureTextEx(f,s.substr(0,std::min(e.anchor,s.size())).c_str(),16,0).x;
  DrawRectangleRec({inside.x+std::min(a,caretWidth)-offset,r.y+6,std::abs(a-caretWidth),20},{166,202,177,200});
 }
 DrawTextEx(f,s.empty()?placeholder:s.c_str(),{inside.x-offset,r.y+7},16,0,s.empty()?muted:ink);
 if(focus&&std::fmod(GetTime(),1)<0.6)DrawLineEx({inside.x+caretWidth-offset,r.y+6},{inside.x+caretWidth-offset,r.y+26},1.5f,accent);
 EndScissorMode();
}
void PartIcon(Rectangle r,bool exporting,CheckState state){
 if(Over(r))DrawRectangleRounded(r,0.25f,4,{139,176,155,70});
 Color c=state==CheckState::All?accent:(state==CheckState::Mixed?Color{145,98,26,255}:muted);
 const float x=r.x+r.width/2,y=r.y+r.height/2;
 auto line=[&](float a,float b,float d,float e){DrawLineEx({x+a,y+b},{x+d,y+e},1.5f,c);};
 if(exporting){line(-7,2,-7,7);line(-7,7,7,7);line(7,7,7,2);line(0,3,0,-7);line(0,-7,-4,-3);line(0,-7,4,-3);}
 else{for(int i=0;i<12;++i){float t=i/12.f,u=(i+1)/12.f;float v=4.5f*std::sin(PI*t),w=4.5f*std::sin(PI*u);line(-8+16*t,-v,-8+16*u,-w);line(-8+16*t,v,-8+16*u,w);}DrawCircleV({x,y},2,c);}
 if(state==CheckState::None)line(-8,8,8,-8);
 if(state==CheckState::Mixed){DrawRectangleRec({x+2,y+3,10,8},paper);line(3,7,10,7);}
}
std::string IconHint(bool exp,CheckState s){
 if(exp)return s==CheckState::All?"Included in export - click to exclude":s==CheckState::Mixed?"Mixed export - click to include all":"Excluded from export - click to include";
 return s==CheckState::All?"Visible - click to hide":s==CheckState::Mixed?"Mixed visibility - click to show all":"Hidden - click to show";
}
Rectangle HelpRect(int w){return {std::max(12.f,w-316.f),56,304,222};}
Rectangle ToastRect(int w,int h,float bottom){return {std::max(12.f,w-432.f),std::max(12.f,h-bottom-60.f),std::min(420.f,w-24.f),48};}
std::vector<std::string> DiagnosticLines(const std::string &s,float width){
 std::vector<std::string> lines;std::string line;int n=0,limit=std::max(12,int(width/14));
 for(size_t p=0;p<s.size();){auto q=Next(s,p);if(s[p]=='\n'||n>=limit){lines.push_back(line);line.clear();n=0;}if(s[p]!='\n'){line+=s.substr(p,q-p);++n;}p=q;}
 lines.push_back(line);return lines;
}
}
void SetUiDrawScale(float scale){drawScale=scale;}
PanelInput LogicalInput(PanelInput i,float scale){i.mouse.x/=scale;i.mouse.y/=scale;return i;}
PanelInput ReadPanelInput(){
 PanelInput i;i.mouse=GetMousePosition();i.wheel=GetMouseWheelMove();
 i.pressed=IsMouseButtonPressed(MOUSE_BUTTON_LEFT);i.leftDown=IsMouseButtonDown(MOUSE_BUTTON_LEFT);
 i.rightPressed=IsMouseButtonPressed(MOUSE_BUTTON_RIGHT);i.rightDown=IsMouseButtonDown(MOUSE_BUTTON_RIGHT);
 auto key=[](int k){return IsKeyPressed(k)||IsKeyPressedRepeat(k);};
 i.backspace=key(KEY_BACKSPACE);i.deleteKey=key(KEY_DELETE);i.left=key(KEY_LEFT);i.right=key(KEY_RIGHT);i.home=key(KEY_HOME);i.end=key(KEY_END);
 i.shift=IsKeyDown(KEY_LEFT_SHIFT)||IsKeyDown(KEY_RIGHT_SHIFT);i.enter=IsKeyPressed(KEY_ENTER);i.escape=IsKeyPressed(KEY_ESCAPE);
 bool ctrl=IsKeyDown(KEY_LEFT_CONTROL)||IsKeyDown(KEY_RIGHT_CONTROL)||IsKeyDown(KEY_LEFT_SUPER)||IsKeyDown(KEY_RIGHT_SUPER);
 i.selectAll=ctrl&&IsKeyPressed(KEY_A);i.find=ctrl&&IsKeyPressed(KEY_F);
 for(int c=GetCharPressed();c;c=GetCharPressed())if(!ctrl){int n=0;auto text=CodepointToUTF8(c,&n);i.text.append(text,n);}
 if(ctrl&&IsKeyPressed(KEY_V)){auto text=GetClipboardText();if(text)i.text=text;}
 return i;
}
void TextEdit::Focus(const std::string &s,bool all){initialized=true;caret=s.size();anchor=all?0:caret;}
void TextEdit::Update(std::string &s,const PanelInput &i){
 if(!initialized)Focus(s);caret=std::min(caret,s.size());anchor=std::min(anchor,s.size());
 if(i.selectAll){anchor=0;caret=s.size();}
 if(i.left||i.right||i.home||i.end){
  if(i.home)caret=0;else if(i.end)caret=s.size();
  else if(!i.shift&&anchor!=caret)caret=i.left?std::min(anchor,caret):std::max(anchor,caret);
  else caret=i.left?Prev(s,caret):Next(s,caret);
  if(!i.shift)anchor=caret;
 }
 std::string inserted;for(char c:i.text)if(static_cast<unsigned char>(c)>=32&&c!=127)inserted+=c;
 if(i.backspace||i.deleteKey||!inserted.empty()){
  size_t a=std::min(anchor,caret),b=std::max(anchor,caret);
  if(a==b){if(i.backspace)a=Prev(s,a);else if(i.deleteKey)b=Next(s,b);}
  s.erase(a,b-a);caret=anchor=a;
  if(s.size()+inserted.size()<=4096){s.insert(caret,inserted);caret+=inserted.size();anchor=caret;}
 }
}
Rectangle PartsPanel::Viewport(int w,int h) const{return {0,0,float(std::max(1,w)),float(std::max(1,h))};}
PartsLayout PartsPanel::Layout(const PartTree &tree,int w,int h) const{
 PartsLayout l{};float width=std::min(360.f,std::max(248.f,w-324.f));width=std::min(width,w-24.f);
 l.card={margin,margin,width,std::min(196+rowHeight*std::max(size_t{1},tree.Rows(search).size()),h*0.75f)};
 if(!open){l.card={margin,margin,144,44};l.collapse=l.card;return l;}
 float x=margin+pad,right=margin+width-pad;
 l.collapse={right-24,24,24,control};l.exportButton={right-138,24,106,control};
 l.file={x,65,width-184,24};l.checks={right-156,61,76,32};l.overview={right-76,61,76,32};l.search={x,96,width-24,control};l.clear={right-30,96,30,control};
 float available=width-32;float first=available*0.33f,second=available*0.25f;
 l.isolate={x,138,first,control};l.showAll={x+first+4,138,second,control};l.frame={x+first+second+8,138,available-first-second,control};
 l.list={margin+6,200,width-12,std::max(0.f,l.card.height-196)};
 l.visibilityX=right-52;l.exportX=right-24;
 return l;
}
Rectangle PartsPanel::Bounds(const PartTree &t,int w,int h) const{return Layout(t,w,h).card;}
bool PartsPanel::CapturesMouse(const PartTree &t,const PanelInput &i,int w,int h) const{return gesture||CheckCollisionPointRec(i.mouse,Bounds(t,w,h));}
PanelActions PartsPanel::Update(PartTree &tree,const PanelInput &i,int w,int h){
 PanelActions a;if(i.pressed||i.rightPressed)gesture=CheckCollisionPointRec(i.mouse,Bounds(tree,w,h));
 if(!i.leftDown&&!i.rightDown&&!i.pressed&&!i.rightPressed)gesture=false;
 if(i.find){open=true;searchFocus=true;editor.Focus(search,true);}
 auto l=Layout(tree,w,h);
 if(Hit(i,l.collapse)){open=!open;searchFocus=false;return a;}
 if(!open){searchFocus=false;return a;}
 if(i.pressed){searchFocus=CheckCollisionPointRec(i.mouse,l.search);if(searchFocus)editor.Focus(search);}
 if(Hit(i,l.clear)){search.clear();editor.Focus(search);scroll=0;}
 if(searchFocus){auto before=search;editor.Update(search,i);if(before!=search)scroll=0;if(i.escape||i.enter)searchFocus=false;}
 if(Hit(i,l.exportButton))a.openExport=true;
 if(Hit(i,l.overview)){a.openOverview=true;searchFocus=false;}
 if(Hit(i,l.checks)){a.openChecks=true;searchFocus=false;}
 if(Hit(i,l.isolate)&&(tree.Selection()||tree.state.isolated))tree.Isolate();
 if(Hit(i,l.showAll))tree.ShowAll();
 if(Hit(i,l.frame))a.frame=true;
 l=Layout(tree,w,h);auto rows=tree.Rows(search);
 if(CheckCollisionPointRec(i.mouse,l.list))scroll-=i.wheel*rowHeight*3;
 scroll=Clamp(scroll,0,std::max(0.f,rows.size()*rowHeight-l.list.height));
 if(Hit(i,l.list)){
  size_t index=size_t((i.mouse.y-l.list.y+scroll)/rowHeight);
  if(index<rows.size()){
   auto r=rows[index];auto &n=tree.nodes[r.node];
   if(i.mouse.x>=l.exportX)tree.Toggle(r.node,true);
   else if(i.mouse.x>=l.visibilityX)tree.Toggle(r.node,false);
   else if(n.group&&i.mouse.x<margin+pad+18+Indent(r.depth)){if(!tree.state.collapsed.erase(n.key))tree.state.collapsed.insert(n.key);}
   else {tree.Select(r.node);a.selectionChanged=true;}
  }
 }
 auto after=Layout(tree,w,h);scroll=Clamp(scroll,0,std::max(0.f,tree.Rows(search).size()*rowHeight-after.list.height));return a;
}
void PartsPanel::Draw(const PartTree &tree,Font f,int w,int h) const{
 auto l=Layout(tree,w,h);Card(l.card);
 if(!open){DrawBrandMark({22,22,24,24});Label("Parts",f,55,26,60);Chevron(139,34,true);return;}
 DrawBrandMark({24,26,28,28});Label("SynthCAD",f,60,29,l.exportButton.x-66,18);
 Button(l.exportButton,"Export",f,true,true);if(Over(l.collapse))Card(l.collapse,{223,232,216,255});Chevron(l.collapse.x+12,40,false);
 Label(sceneName,f,l.file.x,l.file.y+3,l.file.width,15,muted);
 Button(l.overview,"Project",f);
 Button(l.checks,"Checks",f);
 Field(l.search,search,"Search parts...",editor,searchFocus,f,24);if(!search.empty())Cross(l.clear);
 Button(l.isolate,tree.state.isolated?"Exit isolation":"Isolate",f,tree.Selection().has_value()||tree.state.isolated);
 Button(l.showAll,"Show all",f);bool canFrame=false;if(auto n=tree.Selection())for(auto p:tree.nodes[*n].parts)canFrame|=tree.Visible(p);
 Button(l.frame,"Frame selection",f,canFrame);
 Label(std::to_string(tree.parts.size())+(tree.parts.size()==1?" part":" parts"),f,24,179,160,14,muted);
 PartIcon({l.visibilityX,173,24,24},false,CheckState::All);PartIcon({l.exportX,173,24,24},true,CheckState::All);
 std::string tip;if(Over(l.file))tip=scenePath.empty()?sceneName:scenePath;
 if(Over(l.frame))tip="Frame selection";if(Over(l.isolate))tip=tree.state.isolated?"Exit isolation":"Isolate selection";
 if(Over(l.collapse))tip="Collapse parts";if(Over(l.clear)&&!search.empty())tip="Clear search";
 if(Over({l.visibilityX,173,24,24}))tip="Visibility";if(Over({l.exportX,173,24,24}))tip="Include in export";
 Clip(l.list);auto rows=tree.Rows(search);auto mouse=Mouse();
 for(size_t n=0;n<rows.size();++n){
  float y=l.list.y+n*rowHeight-scroll;if(y+rowHeight<l.list.y||y>=l.list.y+l.list.height)continue;
  auto row=rows[n];auto &node=tree.nodes[row.node];Rectangle r{l.list.x,y,l.list.width-3,rowHeight};bool over=Over(r)&&CheckCollisionPointRec(mouse,l.list);
  if(node.key==tree.state.selected)Card(r,{142,182,151,110});else if(over)Card(r,{211,224,204,160});
  float x=24+Indent(row.depth);
  if(node.group)Chevron(x+5,y+15,tree.state.collapsed.count(node.key)&&search.empty());
  else DrawRectangleRounded({x+1,y+11,8,8},0.2f,3,tree.parts[node.parts.front()].color);
  float labelWidth=l.visibilityX-x-18;Label(node.name,f,x+17,y+7,labelWidth,16,tree.Checked(row.node,false)==CheckState::None?muted:ink);
  PartIcon({l.visibilityX,y+3,24,24},false,tree.Checked(row.node,false));PartIcon({l.exportX,y+3,24,24},true,tree.Checked(row.node,true));
  if(over){if(mouse.x>=l.exportX)tip=IconHint(true,tree.Checked(row.node,true));else if(mouse.x>=l.visibilityX)tip=IconHint(false,tree.Checked(row.node,false));else if(MeasureTextEx(f,node.name.c_str(),16,0).x>labelWidth)tip=node.name;}
 }
 if(rows.empty())Label(tree.parts.empty()?"This scene has no parts":"No matching parts",f,l.list.x+6,l.list.y+7,l.list.width-12,16,muted);
 EndScissorMode();float total=rows.size()*rowHeight;
 if(total>l.list.height&&l.list.height>0)DrawRectangleRounded({l.card.x+l.card.width-5,l.list.y+scroll/total*l.list.height,3,std::max(8.f,l.list.height*l.list.height/total)},0.5f,4,Fade(muted,.6f));
 Tooltip(tip,f,w,h);
}
ExportLayout ExportDialog::Layout(int w,int h){
 ExportLayout l;auto &r=l.card;const float height=std::max(0.f,std::min(560.f,h-24.f));
 r={std::max(12.f,(w-620.f)/2),std::max(12.f,(h-height)/2),std::max(0.f,std::min(620.f,w-24.f)),height};
 auto row=[&](float y,float width){return Rectangle{r.x+20,r.y+y,width,32};};
 l.threeMf=row(54,(r.width-48)/2);l.stl=l.threeMf;l.stl.x+=l.threeMf.width+8;
 l.all=row(94,r.width-40);l.visible=row(130,r.width-40);l.path=row(194,r.width-40);
 l.review={r.x+20,r.y+232,r.width-40,std::max(0.f,height-308)};
 l.cancel=row(height-44,88);l.save=row(height-44,144);l.save.x=r.x+r.width-164;return l;
}
static bool ExportExtensionMatches(const std::string &path,bool threeMf){
 if(path.empty()||path.find('\0')!=std::string::npos)return false;
 auto extension=std::filesystem::u8path(path).extension().u8string();
 for(auto &c:extension)if(c>='A'&&c<='Z')c=char(c-'A'+'a');
 return extension==(threeMf?".3mf":".stl");
}
static std::string ExportReviewText(const nlohmann::json& result,size_t count,bool valid){
 auto review=result.value("review",nlohmann::json::object());
 if(!review.is_object())review=nlohmann::json::object();
 std::string text=valid?std::to_string(count)+(count==1?" part included":" parts included"):"Export disabled - correct the scene error";
 if(result.contains("ok")&&!result.value("ok",false))text+="\n"+result.value("message","");
 if(review.contains("basis"))text+="\n"+review["basis"].value("kind","scene")+": "+review["basis"].value("view","");
 const auto context=review.value("profile",nlohmann::json::object());
 const auto profile=context.is_object()?context.value("profile",nlohmann::json::object()):nlohmann::json::object();
 if(profile.is_object()&&profile.contains("buildVolume"))text+="\nBuild volume (mm): "+profile["buildVolume"].dump();
 if(profile.is_object()){
   for(const auto* key:{"printer","material"})if(profile.contains(key)&&profile[key].is_object())
     text+=std::string("\n")+(std::string(key)=="printer"?"Printer: ":"Material: ")+profile[key].value("name",profile[key].value("id","unspecified"));
   if(profile.contains("nozzleDiameter")&&profile["nozzleDiameter"].is_number())text+="\nNozzle (mm): "+profile["nozzleDiameter"].dump();
 }
 text+="\nKeeps current placement. Choose print settings in your slicer.";
 if(review.contains("quantities"))for(const auto& item:review["quantities"])text+="\n"+item.value("sourcePartId","")+": "+std::to_string(item.value("count",0));
 const auto risks=review.value("risks",nlohmann::json::array());
 if(!risks.empty())text+="\n\nReview concerns / unchecked items:";
 for(const auto& risk:risks){
   text+="\n"+risk.value("result","warning")+": "+risk.value("name",risk.value("id","Check"));
   for(const auto& action:risk.value("nextActions",nlohmann::json::array()))if(action.is_string())text+="\n  "+action.get<std::string>();
 }
 if(!risks.empty())text+="\nChecks cover the authored scene; this export subset is not a new printability validation.";
 return text;
}
static std::vector<std::string> ExportReviewLines(const std::string& text,float width,Font font){
 std::vector<std::string> lines;std::string line;
 auto measured=[&](const std::string& s){
   if(font.glyphCount>0)return MeasureTextEx(font,s.c_str(),14,0).x;
   float size=0;for(size_t p=0;p<s.size();){auto next=Next(s,p);size+=next-p>2?14.f:7.f;p=next;}return size;
 };
 for(size_t p=0;p<text.size();){
   const auto next=Next(text,p);const auto c=text.substr(p,next-p);p=next;
   if(c=="\n"){lines.push_back(line);line.clear();continue;}
   if(!line.empty()&&measured(line+c)>width){
     const auto space=line.find_last_of(' ');
     if(space!=std::string::npos&&space>0){lines.push_back(line.substr(0,space));line.erase(0,space+1);}
     else{lines.push_back(line);line.clear();}
   }
   line+=c;
 }
 lines.push_back(line);return lines;
}
void ExportDialog::Open(const std::string &p){open=true;if(path.empty())path=p;error.clear();overwrite=false;pathFocus=false;reviewScroll=0;editor.Focus(path);}
PanelActions ExportDialog::Update(const PanelInput &i,int w,int h,size_t count,bool valid,Font font){
 PanelActions a;if(!open)return a;auto l=Layout(w,h);
 if(i.escape||Hit(i,l.cancel)){open=false;overwrite=false;return a;}
 const auto lines=ExportReviewLines(ExportReviewText(review,count,valid),l.review.width-12,font);
 const float maxScroll=std::max(0.f,lines.size()*18.f-l.review.height);
 if(CheckCollisionPointRec(i.mouse,l.review))reviewScroll=Clamp(reviewScroll-i.wheel*40,0,maxScroll);
 else reviewScroll=Clamp(reviewScroll,0,maxScroll);
 if(Hit(i,l.threeMf)||Hit(i,l.stl)){
  const bool format=Hit(i,l.threeMf);
  if(format!=threeMf){threeMf=format;if(!path.empty()){auto p=std::filesystem::u8path(path);p.replace_extension(threeMf?".3mf":".stl");path=p.u8string();}overwrite=false;error.clear();editor.Focus(path);}
  pathFocus=false;return a;
 }
 if(Hit(i,l.all)||Hit(i,l.visible)){visibleOnly=Hit(i,l.visible);overwrite=false;error.clear();return a;}
 if(i.pressed){pathFocus=CheckCollisionPointRec(i.mouse,l.path);if(pathFocus)editor.Focus(path);}
 if(pathFocus){auto before=path;editor.Update(path,i);if(before!=path){overwrite=false;error.clear();}}
 if(Hit(i,l.save)&&count&&valid&&!path.empty()){
  if(ExportExtensionMatches(path,threeMf))a.save=true;
  else {error=threeMf?"Use a .3mf destination for 3MF export.":"Use a .stl destination for STL export.";overwrite=false;}
 }return a;
}
void ExportDialog::Draw(Font f,int w,int h,size_t count,bool valid) const{
 if(!open)return;DrawRectangle(0,0,w,h,{33,43,37,72});auto l=Layout(w,h);auto r=l.card;Card(r,paper);
 Label("Export",f,r.x+20,r.y+18,r.width-40,23);
 auto radio=[&](Rectangle a,bool active,const char *s){if(Over(a))Card(a,{226,234,220,180});DrawCircleLines(int(a.x+10),int(a.y+16),8,accent);if(active)DrawCircleV({a.x+10,a.y+16},4.5f,accent);Label(s,f,a.x+28,a.y+7,a.width-30);};
 radio(l.threeMf,threeMf,"3MF");radio(l.stl,!threeMf,"STL");
 radio(l.all,!visibleOnly,"All exportable parts");radio(l.visible,visibleOnly,"Visible exportable parts");
 Label("Destination",f,r.x+20,r.y+174,r.width-40,14,muted);Field(l.path,path,threeMf?"Choose a .3mf file path":"Choose a .stl file path",editor,pathFocus,f);
 auto lines=ExportReviewLines(ExportReviewText(review,count,valid),l.review.width-12,f);
 Clip(l.review);for(size_t n=0;n<lines.size();++n)DrawTextEx(f,lines[n].c_str(),{l.review.x,l.review.y+n*18-reviewScroll},14,0,ink);EndScissorMode();
 if(lines.size()*18>l.review.height)DrawRectangleRounded({l.review.x+l.review.width-4,l.review.y+reviewScroll/(lines.size()*18)*l.review.height,3,std::max(8.f,l.review.height*l.review.height/(lines.size()*18))},0.5f,4,Fade(muted,.6f));
 Label(overwrite?"This file already exists. Replace it?":error,f,r.x+20,r.y+r.height-69,r.width-40,14,MAROON);
 const auto detail=review.value("review",nlohmann::json::object());
 const bool risks=detail.is_object()&&detail.value("hasWarnings",false);
 Button(l.cancel,"Cancel",f);Button(l.save,overwrite?"Replace file":risks?"Export anyway":"Export",f,count>0&&valid&&!path.empty(),true);
 if(!error.empty()&&Over({r.x+20,r.y+r.height-72,r.width-40,24}))Tooltip(error,f,w,h);
}
std::filesystem::path SuggestedExportPath(const std::filesystem::path &home,const std::filesystem::path &scene,bool threeMf){
 auto name=scene.empty()?std::filesystem::path("synthcad"):scene.stem();name+=threeMf?".3mf":".stl";return home/"Downloads"/name;
}
void WorkspaceUi::Loaded(){loadError.clear();details=false;detailScroll=0;}
void WorkspaceUi::Failed(const std::string &s){if(loadError!=s){details=false;detailScroll=0;}loadError=s;}
void WorkspaceUi::Saved(const std::string &s,double now){toast="Exported "+s;toastUntil=now+4;}
bool WorkspaceUi::ToastVisible(double now) const{return !toast.empty()&&now<toastUntil;}
Rectangle WorkspaceUi::ToastCard(int w,int h) const{return ToastRect(w,h,toastBottom);}
Rectangle WorkspaceUi::Toolbar(int w) const{return {std::max(12.f,w-300.f),12,288,40};}
Rectangle WorkspaceUi::ErrorCard(int w,int h) const{return {std::max(12.f,w-492.f),h-12-(details?std::min(310.f,h-84.f):112.f),std::min(480.f,w-24.f),details?std::min(310.f,h-84.f):112.f};}
bool WorkspaceUi::CapturesMouse(const PanelInput &i,int w,int h,double now) const{
 return gesture||CheckCollisionPointRec(i.mouse,Toolbar(w))||(help&&CheckCollisionPointRec(i.mouse,HelpRect(w)))||(!loadError.empty()&&CheckCollisionPointRec(i.mouse,ErrorCard(w,h)))||(ToastVisible(now)&&CheckCollisionPointRec(i.mouse,ToastCard(w,h)));
}
PanelActions WorkspaceUi::Update(const PanelInput &i,int w,int h,double now){
 PanelActions a;if(i.pressed||i.rightPressed)gesture=CapturesMouse(i,w,h,now);if(!i.leftDown&&!i.rightDown&&!i.pressed&&!i.rightPressed)gesture=false;
 auto r=Toolbar(w);if(Hit(i,{r.x+4,r.y+4,65,32}))a.fitAll=true;if(Hit(i,{r.x+73,r.y+4,175,32}))a.dimensions=true;if(Hit(i,{r.x+252,r.y+4,32,32}))help=!help;
 if(i.escape)help=false;
 if(!loadError.empty()){
  auto e=ErrorCard(w,h);if(Hit(i,{e.x+12,e.y+70,140,30})){details=!details;detailScroll=0;}
  if(Hit(i,{e.x+e.width-92,e.y+16,80,32}))a.reload=true;
  Rectangle content{e.x+12,e.y+112,e.width-24,std::max(0.f,e.height-124)};
  if(details&&CheckCollisionPointRec(i.mouse,content)){detailScroll-=i.wheel*54;detailScroll=Clamp(detailScroll,0,std::max(0.f,DiagnosticLines(loadError,content.width).size()*18-content.height));}
 }
 auto t=ToastCard(w,h);if(ToastVisible(now)&&Hit(i,{t.x+t.width-36,t.y+8,28,32}))toast.clear();return a;
}
void WorkspaceUi::Draw(Font f,int w,int h,DimensionMode mode,double now) const{
 auto r=Toolbar(w);Card(r);Button({r.x+4,r.y+4,65,32},"Fit all",f);std::string dim=std::string("Dimensions: ")+DimensionModeName(mode);Button({r.x+73,r.y+4,175,32},dim.c_str(),f);Button({r.x+252,r.y+4,32,32},"?",f);
 if(help){auto b=HelpRect(w);Card(b,paper);Label("Workspace shortcuts",f,b.x+14,b.y+14,b.width-28,18);const char *lines[]={"Drag: orbit  /  Right-drag: pan","Scroll: zoom  /  Space: fit all","W A S D Q E: move camera","Ctrl+F: search parts","P: export  /  R: reload","M: cycle dimensions"};for(int i=0;i<6;++i)Label(lines[i],f,b.x+14,b.y+48+i*26,b.width-28,15,muted);}
 if(!loadError.empty()){
  auto e=ErrorCard(w,h);Card(e,{251,240,228,250});Label("Scene could not load",f,e.x+12,e.y+16,e.width-120,19,{123,65,34,255});Label("Export is disabled until the scene is corrected.",f,e.x+12,e.y+44,e.width-24,15,muted);
  Button({e.x+e.width-92,e.y+16,80,32},"Reload",f);Button({e.x+12,e.y+70,140,30},details?"Hide details":"Show details",f);
  if(details){Rectangle content{e.x+12,e.y+112,e.width-24,e.height-124};Clip(content);auto lines=DiagnosticLines(loadError,content.width);for(size_t n=0;n<lines.size();++n)DrawTextEx(f,lines[n].c_str(),{content.x,content.y+n*18-detailScroll},14,0,ink);EndScissorMode();if(lines.size()*18>content.height)Label("Scroll for more",f,e.x+e.width-120,e.y+76,108,13,muted);}
 }else if(ToastVisible(now)){auto t=ToastCard(w,h);Card(t,{213,233,211,245});Label(toast,f,t.x+14,t.y+15,t.width-54,16);Cross({t.x+t.width-36,t.y+8,28,32});}
 std::string tip;if(Over({r.x+4,r.y+4,65,32}))tip="Fit all visible parts (Space)";if(Over({r.x+73,r.y+4,175,32}))tip="Cycle dimensions (M)";if(Over({r.x+252,r.y+4,32,32}))tip="Keyboard and mouse shortcuts";Tooltip(tip,f,w,h);
}
}
