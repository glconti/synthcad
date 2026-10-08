#include "appearance.h"
#include "js_bindings.h"
#include <cctype>
#include <set>

namespace dingcad {
namespace {
struct Value {
  JSContext *ctx;
  JSValue value;
  Value(JSContext *c, JSValue v):ctx(c),value(v){}
  ~Value(){JS_FreeValue(ctx,value);}
  Value(const Value&)=delete;
};
bool HexColor(JSContext *ctx, JSValueConst value, Color &color) {
  if (!JS_IsString(value)) return false;
  size_t size=0;
  const char *raw=JS_ToCStringLen(ctx,&size,value);
  if (!raw) return false;
  const std::string text(raw,size);
  JS_FreeCString(ctx,raw);
  if (text.size()!=7 || text[0]!='#') return false;
  unsigned rgb=0;
  for (size_t i=1;i<text.size();++i) {
    const unsigned char ch=static_cast<unsigned char>(text[i]);
    if (!std::isxdigit(ch)) return false;
    rgb=(rgb<<4) | (std::isdigit(ch)?ch-'0':std::tolower(ch)-'a'+10);
  }
  color={static_cast<unsigned char>(rgb>>16),static_cast<unsigned char>(rgb>>8),static_cast<unsigned char>(rgb),255};
  return true;
}
bool Text(JSContext *ctx, JSValueConst value, std::string &out) {
  if (!JS_IsString(value)) return false;
  size_t size=0;
  const char *raw=JS_ToCStringLen(ctx,&size,value);
  if (!raw) return false;
  out.assign(raw,size);JS_FreeCString(ctx,raw);
  return !out.empty() && out.find('\0')==std::string::npos;
}
}
Appearance ReadAppearance(JSContext *ctx, JSValueConst ns) {
  Appearance result;
  const auto invalid=[&](const std::string &reason){
    if (JS_HasException(ctx)) JS_FreeValue(ctx,JS_GetException(ctx));
    result.parts.clear();
    result.diagnostic="displayParts: "+reason+"; load rejected, export disabled";
    return result;
  };
  Value list(ctx,JS_GetPropertyStr(ctx,ns,"displayParts"));
  if (JS_IsUndefined(list.value)) return result;
  result.specified=true;
  if (!JS_IsArray(list.value)) return invalid("expected an array");
  Value length(ctx,JS_GetPropertyStr(ctx,list.value,"length"));
  uint32_t count=0;
  if (JS_ToUint32(ctx,&count,length.value)<0 || count>10000) return invalid("invalid or excessive entry count");
  std::set<std::string> ids;
  for (uint32_t i=0;i<count;++i) {
    Value entry(ctx,JS_GetPropertyUint32(ctx,list.value,i));
    if (!JS_IsObject(entry.value)) return invalid("invalid entry "+std::to_string(i));
    Value solid(ctx,JS_GetPropertyStr(ctx,entry.value,"solid"));
    Value color(ctx,JS_GetPropertyStr(ctx,entry.value,"color"));
    Color tint{};
    auto handle=GetManifoldHandle(ctx,solid.value);
    if (!handle || !HexColor(ctx,color.value,tint) || JS_HasException(ctx))
      return invalid("entry "+std::to_string(i)+" needs a manifold solid and #RRGGBB color");
    DisplayPart part{handle,tint};
    Value id(ctx,JS_GetPropertyStr(ctx,entry.value,"id"));
    Value name(ctx,JS_GetPropertyStr(ctx,entry.value,"name"));
    Value group(ctx,JS_GetPropertyStr(ctx,entry.value,"group"));
    Value exportable(ctx,JS_GetPropertyStr(ctx,entry.value,"exportable"));
    if (JS_IsUndefined(id.value)) part.id="@index:"+std::to_string(i);
    else if (!Text(ctx,id.value,part.id) || part.id.rfind("@index:",0)==0)
      return invalid("entry "+std::to_string(i)+" has invalid/reserved id");
    if (!ids.insert(part.id).second) return invalid("duplicate id "+part.id);
    if (JS_IsUndefined(name.value)) part.name="Part "+std::to_string(i+1);
    else if (!Text(ctx,name.value,part.name)) return invalid("invalid name for "+part.id);
    if (!JS_IsUndefined(exportable.value)) {
      if (!JS_IsBool(exportable.value)) return invalid("exportable must be boolean for "+part.id);
      part.exportable=JS_ToBool(ctx,exportable.value)!=0;
    }
    if (!JS_IsUndefined(group.value)) {
      if (!JS_IsArray(group.value)) return invalid("group must be an array for "+part.id);
      Value size(ctx,JS_GetPropertyStr(ctx,group.value,"length"));uint32_t depth=0;
      if (JS_ToUint32(ctx,&depth,size.value)<0 || depth>32) return invalid("invalid group depth for "+part.id);
      for(uint32_t j=0;j<depth;++j){
        Value label(ctx,JS_GetPropertyUint32(ctx,group.value,j));std::string text;
        if(!Text(ctx,label.value,text))return invalid("invalid group label for "+part.id);
        part.group.push_back(text);
      }
    }
    if(JS_HasException(ctx))return invalid("metadata getter failed for "+part.id);
    result.parts.push_back(std::move(part));
  }
  return result;
}
manifold::MeshGL DisplayMesh(const std::vector<DisplayPart> &parts) {
  manifold::MeshGL display;
  display.numProp=6;
  for (const auto &part:parts) {
    const auto source=part.solid->GetMeshGL();
    for (const auto index:source.triVerts) {
      display.triVerts.push_back(static_cast<unsigned int>(display.triVerts.size()));
      for (int axis=0;axis<3;++axis)display.vertProperties.push_back(source.vertProperties[index*source.numProp+axis]);
      display.vertProperties.insert(display.vertProperties.end(),{
        static_cast<float>(part.color.r),static_cast<float>(part.color.g),static_cast<float>(part.color.b)});
    }
  }
  return display;
}
}
