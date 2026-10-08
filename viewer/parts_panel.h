#pragma once
#include "part_tree.h"
#include <string>

namespace dingcad {
struct PanelInput {
  Vector2 mouse{};
  float wheel=0;
  bool pressed=false,leftDown=false,rightPressed=false,rightDown=false;
  bool backspace=false,enter=false,escape=false,selectAll=false;
  std::string text;
};
PanelInput ReadPanelInput();
struct PanelActions {bool frame=false,openExport=false,save=false;};
struct PartsPanel {
  bool open=true,searchFocus=false,gesture=false,selectText=false;
  std::string search;
  float scroll=0;
  Rectangle Viewport(int width,int height) const;
  PanelActions Update(PartTree &tree,const PanelInput &input,int width,int height);
  bool CapturesMouse(const PanelInput &input,int width,int height) const;
  void Draw(const PartTree &tree,Font font,int width,int height) const;
};
struct ExportDialog {
  bool open=false,visibleOnly=false,pathFocus=false,overwrite=false,selectText=false;
  std::string path,error;
  void Open(const std::string &defaultPath);
  PanelActions Update(const PanelInput &input,int width,int height,size_t count,bool valid);
  void Draw(Font font,int width,int height,size_t count,bool valid) const;
};
}
