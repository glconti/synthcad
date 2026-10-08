#include "brand.h"
#include "resources/icon_data.h"
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#define NOUSER
#include <windows.h>
#endif
namespace dingcad {
namespace { Texture2D mark{}; }
void SetApplicationIdentity(){
#if defined(_WIN32)
  HMODULE shell=LoadLibraryW(L"shell32.dll");
  if(shell){
    using SetId=HRESULT (WINAPI *)(PCWSTR);
    auto setId=reinterpret_cast<SetId>(GetProcAddress(shell,"SetCurrentProcessExplicitAppUserModelID"));
    if(setId)setId(L"SynthCAD.Viewer");
    FreeLibrary(shell);
  }
#endif
}
void SetApplicationIcons(){
  Image source=LoadImageFromMemory(".png",kSynthCadIconPng,sizeof(kSynthCadIconPng));
  const int sizes[]={16,24,32,48,64,128,256};Image icons[7]{};
  if(!source.data)return;
  for(int i=0;i<7;++i){icons[i]=ImageCopy(source);ImageResize(&icons[i],sizes[i],sizes[i]);}
  SetWindowIcons(icons,7);
  mark=LoadTextureFromImage(source);
  SetTextureFilter(mark,TEXTURE_FILTER_BILINEAR);
  for(auto &icon:icons)UnloadImage(icon);
  UnloadImage(source);
}
void UnloadApplicationIcons(){if(mark.id)UnloadTexture(mark);mark={};}
void DrawBrandMark(Rectangle r){
 if(mark.id)DrawTexturePro(mark,{0,0,float(mark.width),float(mark.height)},r,{0,0},0,WHITE);
}
}
