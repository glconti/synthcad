#pragma once
#include "part_tree.h"
#include "dimensions.h"
#include <filesystem>
#include <string>
namespace dingcad {
// UI coordinates are logical pixels; the viewport and camera remain window-sized.
struct PanelInput {
  Vector2 mouse{};float wheel=0;
  bool pressed=false,leftDown=false,rightPressed=false,rightDown=false;
  bool backspace=false,deleteKey=false,enter=false,escape=false,selectAll=false,find=false;
  bool left=false,right=false,home=false,end=false,shift=false;
  std::string text;
};
PanelInput ReadPanelInput();
PanelInput LogicalInput(PanelInput input,float scale);
void SetUiDrawScale(float scale);
struct TextEdit {
  size_t caret=0,anchor=0;bool initialized=false;
  void Update(std::string &text,const PanelInput &input);
  void Focus(const std::string &text,bool all=false);
};
struct PanelActions {bool frame=false,openExport=false,save=false,fitAll=false,dimensions=false,reload=false,selectionChanged=false;};
struct PartsLayout {
  Rectangle card,collapse,exportButton,file,search,clear,isolate,showAll,frame,list;
  float visibilityX=0,exportX=0;
};
struct PartsPanel {
  bool open=true,searchFocus=false,gesture=false;
  std::string search,sceneName="Built-in sample",scenePath;
  TextEdit editor;float scroll=0;
  Rectangle Viewport(int width,int height) const;
  PartsLayout Layout(const PartTree &tree,int width,int height) const;
  Rectangle Bounds(const PartTree &tree,int width,int height) const;
  PanelActions Update(PartTree &tree,const PanelInput &input,int width,int height);
  bool CapturesMouse(const PartTree &tree,const PanelInput &input,int width,int height) const;
  void Draw(const PartTree &tree,Font font,int width,int height) const;
};
struct ExportLayout {Rectangle card,all,visible,path,cancel,save;};
struct ExportDialog {
  bool open=false,visibleOnly=false,pathFocus=false,overwrite=false;
  std::string path,error;TextEdit editor;
  static ExportLayout Layout(int width,int height);
  void Open(const std::string &defaultPath);
  PanelActions Update(const PanelInput &input,int width,int height,size_t count,bool valid);
  void Draw(Font font,int width,int height,size_t count,bool valid) const;
};
struct WorkspaceUi {
  bool help=false,gesture=false,details=false;float detailScroll=0;
  std::string loadError,toast;double toastUntil=0;
  float toastBottom=128;
  void Loaded();void Failed(const std::string &message);void Saved(const std::string &name,double now);
  bool ToastVisible(double now) const;
  Rectangle Toolbar(int width) const;
  Rectangle ErrorCard(int width,int height) const;
  Rectangle ToastCard(int width,int height) const;
  bool CapturesMouse(const PanelInput &input,int width,int height,double now) const;
  PanelActions Update(const PanelInput &input,int width,int height,double now);
  void Draw(Font font,int width,int height,DimensionMode mode,double now) const;
};
std::filesystem::path SuggestedExportPath(const std::filesystem::path &home,const std::filesystem::path &scene);
}
