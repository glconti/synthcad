#include "appearance.h"
#include "js_bindings.h"
#include <iostream>
#include <stdexcept>
#include <cmath>
#include <cstring>
void Require(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
dingcad::Appearance Parse(JSContext *ctx,const char *source){
  JSValue object=JS_Eval(ctx,source,strlen(source),"appearance-test.js",JS_EVAL_TYPE_GLOBAL);
  Require(!JS_IsException(object),"Fixture evaluates");
  auto result=dingcad::ReadAppearance(ctx,object);
  JS_FreeValue(ctx,object);
  Require(!JS_HasException(ctx),"Parser clears exceptions");
  return result;
}
int main(){
  JSRuntime *rt=JS_NewRuntime();EnsureManifoldClass(rt);
  JSContext *ctx=JS_NewContext(rt);RegisterBindings(ctx);
  try {
    Require(Parse(ctx,"({})").parts.empty(),"Legacy scene fallback");
    Require(Parse(ctx,"({displayParts:[]})").diagnostic.empty(),"Empty list accepted");
    for(const char *source:{
      "({displayParts:1})", "({get displayParts(){throw Error('x')}})",
      "({displayParts:[{solid:cube({size:[1,1,1]}),color:'#fff'}]})",
      "({displayParts:[{solid:cube({size:[1,1,1]}),color:'#gggggg'}]})",
      "({displayParts:[{solid:42,color:'#123456'}]})",
      "({displayParts:[{get solid(){throw Error('x')},color:'#123456'}]})",
      "({displayParts:[{solid:cube({size:[1,1,1]}),get color(){throw Error('x')}}]})",
      "({displayParts:[{solid:cube({size:[1,1,1]}),color:'#123456'},null]})"}) {
      auto a=Parse(ctx,source);
      Require(a.parts.empty()&&!a.diagnostic.empty(),"Invalid metadata rejects complete load");
    }
    Require(!Parse(ctx,"({displayParts:[{solid:cube({size:[1,1,1]}),color:'#123456\\0extra'}]})").diagnostic.empty(),"Embedded NUL color rejected");
    for(const char *field:{"id:7", "id:''", "name:null", "group:'Parts'", "group:['']", "group:[7]", "exportable:1", "get id(){throw Error('x')}"}){
      const auto source=std::string("({displayParts:[{solid:cube({size:[1,1,1]}),color:'#112233',")+field+"}]})";
      Require(!Parse(ctx,source.c_str()).diagnostic.empty(),"Invalid metadata rejected");
    }
    Require(!Parse(ctx,"({displayParts:[{id:'same',solid:cube({size:[1,1,1]}),color:'#112233'},{id:'same',solid:cube({size:[1,1,1]}),color:'#112233'}]})").diagnostic.empty(),"Duplicate IDs rejected");
    Require(Parse(ctx,"({displayParts:[]})").specified&&!Parse(ctx,"({})").specified,"Explicit empty list differs from legacy scene");
    {
      auto named=Parse(ctx,"({displayParts:[{id:'wall',name:'Wall',group:['References','Room'],exportable:false,solid:cube({size:[1,1,1]}),color:'#112233'}]})");
      Require(named.parts[0].id=="wall"&&named.parts[0].name=="Wall"&&named.parts[0].group.size()==2&&!named.parts[0].exportable,"Author metadata retained");
    }
    {
      auto a=Parse(ctx,"({displayParts:[{solid:cube({size:[1,1,1]}),color:'#1A7e40'}, {solid:translate(cube({size:[1,1,1]}),[1,0,0]),color:'#aAbBcC'}]})");
      Require(a.parts.size()==2 && a.diagnostic.empty(),"Two touching colored solids");
      Require(a.parts[0].id=="@index:0"&&a.parts[0].name=="Parte 1"&&a.parts[0].exportable,"Metadata defaults");
      auto before=a.parts[0].solid->GetMeshGL();
      const auto mesh=dingcad::DisplayMesh(a.parts);
      Require(mesh.numProp==6 && mesh.NumVert()==mesh.NumTri()*3,"Independent vertices preserve hard edges");
      Require(mesh.NumTri()==before.NumTri()+a.parts[1].solid->NumTri(),"Every face remains visible");
      Require(mesh.vertProperties[3]==26 && mesh.vertProperties[4]==126 && mesh.vertProperties[5]==64,"Mixed-case RGB parsing");
      const size_t second=before.triVerts.size()*6;
      Require(mesh.vertProperties[second+3]==170&&mesh.vertProperties[second+4]==187,"Separate part colors preserved");
      Require(std::abs(a.parts[0].solid->Volume()-1)<1e-8 && before.triVerts==a.parts[0].solid->GetMeshGL().triVerts,"Display conversion does not mutate export solid");
    }
    JS_FreeContext(ctx);JS_FreeRuntime(rt);
    std::cout<<"PASS appearance parsing, strict metadata, crease/color separation and unchanged geometry\n";
  }catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
}
