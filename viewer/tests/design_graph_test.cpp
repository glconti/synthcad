#include "design_graph.h"
#include "js_bindings.h"
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace {
void Require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
dingcad::DesignGraphResult Parse(JSContext* ctx,const std::string& source,const std::string& view=""){
  JSValue object=JS_Eval(ctx,source.c_str(),source.size(),"design-test.js",JS_EVAL_TYPE_GLOBAL);
  Require(!JS_IsException(object),"fixture evaluates");
  auto result=dingcad::ReadDesignGraph(ctx,object,view);JS_FreeValue(ctx,object);
  Require(!JS_HasException(ctx),"parser clears exceptions");return result;
}
std::string Fixture(int size=2){return std::string("({design:{schemaVersion:1,defaultView:'assembly',parts:[")+
  "{id:'block',name:'Shared block',solid:cube({size:["+std::to_string(size)+",1,1]}),color:'#123456',quantity:2}],"+
  "instances:[{id:'a',part:'block',transform:{translate:[10,0,0]}},{id:'b',part:'block',name:'Second',exportable:false,transform:{rotate:[0,0,90],translate:[20,0,0]}}],"+
  "groups:[{id:'left',name:'Left',members:[{instance:'a'},{instance:'b'}]},{id:'right',name:'Right',members:[{instance:'a'}]}],"+
  "views:[{id:'assembly',kind:'assembly',members:[{group:'left'},{group:'right'},{instance:'a'}]},"+
  "{id:'inspection',kind:'inspection',members:[{instance:'a'}],placements:{a:{translate:[30,0,0]}}},"+
  "{id:'plate',kind:'plate',members:[{instance:'a'},{instance:'b'}],placements:{a:{translate:[0,0,0]},b:{translate:[5,0,0]}}}]}})";
}
std::string Minimal(const std::string& extra="",const std::string& members="[{instance:'a'}]"){
  return "({design:{schemaVersion:1,defaultView:'v',parts:[{id:'p',solid:cube({size:[1,1,1]})}],instances:[{id:'a',part:'p'}],views:[{id:'v',kind:'assembly',members:"+members+"}]"+extra+"}})";
}
void Replace(std::string& s,const std::string& a,const std::string& b){auto at=s.find(a);Require(at!=std::string::npos,"replacement exists");s.replace(at,a.size(),b);}
}
int main(){
  JSRuntime* rt=JS_NewRuntime();EnsureManifoldClass(rt);JSContext* ctx=JS_NewContext(rt);RegisterBindings(ctx);
  try{
    Require(!Parse(ctx,"({scene:cube({size:[1,1,1]})})").specified,"legacy scene remains unspecified");
    auto assembly=Parse(ctx,Fixture());Require(assembly.diagnostic.empty(),"valid graph accepted");
    Require(assembly.appearance.parts.size()==2,"repeated paths deduplicate physical instances");
    const auto& a=assembly.appearance.parts[0];const auto& b=assembly.appearance.parts[1];
    Require(a.sourceSolid&&a.sourceSolid==b.sourceSolid,"instances retain one shared unplaced source handle");
    Require(a.sourceSolid->BoundingBox().min.x==0&&a.sourceSolid->BoundingBox().max.x==2,"retained source is not transformed by instance placement");
    Require(a.id=="a"&&a.sourcePartId=="block"&&a.memberships.size()==3,"source and all alias paths retained");
    Require(a.memberships[0][0].id=="left"&&a.memberships[1][0].id=="right"&&a.memberships[2].empty(),"group identity and root membership retained");
    Require(a.name=="Shared block"&&a.color.r==18&&b.name=="Second"&&!b.exportable,"source style inheritance and instance overrides");
    Require(a.translation[0]==10&&b.translation[0]==20&&b.rotation[2]==90,"independent instance transforms retained");
    const auto rotatedBounds=b.solid->BoundingBox();
    Require(std::abs(rotatedBounds.min.x-19)<1e-6&&std::abs(rotatedBounds.max.x-20)<1e-6&&std::abs(rotatedBounds.max.y-2)<1e-6,"rotation is applied before world translation");
    Require(std::abs(assembly.scene->Volume()-4)<1e-6,"compose contains each physical copy once");
    Require(assembly.metadata["sourceParts"][0]["quantity"]==2,"intended quantity metadata retained");
    auto inspection=Parse(ctx,Fixture(),"inspection"),plate=Parse(ctx,Fixture(),"plate");
    Require(inspection.diagnostic.empty()&&plate.diagnostic.empty(),"all named views resolve");
    Require(inspection.appearance.parts[0].translation[0]==30,"placement replaces instance transform absolutely");
    Require(plate.appearance.parts[1].rotation[2]==0,"omitted override rotation resets to zero");
    Require(assembly.metadata["identity"]!=inspection.metadata["identity"]&&inspection.metadata["identity"]!=plate.metadata["identity"],"active view changes content identity");
    Require(Parse(ctx,Fixture()).metadata["identity"]==assembly.metadata["identity"],"identity is stable across reevaluation");
    for(const auto* view:{"assembly","inspection","plate"}){
      auto changed=Parse(ctx,Fixture(4),view),before=Parse(ctx,Fixture(2),view);
      Require(changed.diagnostic.empty(),"source edit accepted");
      for(size_t i=0;i<changed.appearance.parts.size();++i)Require(std::abs(changed.appearance.parts[i].solid->Volume()-2*before.appearance.parts[i].solid->Volume())<1e-6,"one source edit updates every instance in every view");
    }
    {
      JSValue ns=JS_Eval(ctx,Fixture().c_str(),Fixture().size(),"shared.js",JS_EVAL_TYPE_GLOBAL);
      JSValue design=JS_GetPropertyStr(ctx,ns,"design"),parts=JS_GetPropertyStr(ctx,design,"parts"),part=JS_GetPropertyUint32(ctx,parts,0),solid=JS_GetPropertyStr(ctx,part,"solid");
      auto handle=GetManifoldHandle(ctx,solid);const auto bounds=handle->BoundingBox();auto result=dingcad::ReadDesignGraph(ctx,ns,"inspection");
      Require(result.diagnostic.empty()&&handle->BoundingBox().min==bounds.min&&handle->BoundingBox().max==bounds.max,"shared source handle remains unmodified");
      JS_FreeValue(ctx,solid);JS_FreeValue(ctx,part);JS_FreeValue(ctx,parts);JS_FreeValue(ctx,design);JS_FreeValue(ctx,ns);
    }
    {
      auto duplicate=Minimal("","[{instance:'a'},{instance:'a'}]");auto result=Parse(ctx,duplicate);
      Require(result.appearance.parts.size()==1&&result.appearance.parts[0].memberships.size()==1,"identical membership path deduplicates");
    }
    auto invalid=[&](const std::string& source){auto r=Parse(ctx,source);Require(r.specified&&!r.diagnostic.empty()&&!r.scene&&r.appearance.parts.empty(),"invalid graph rejects complete load");};
    for(const auto& pair:std::initializer_list<std::pair<std::string,std::string>>{
      {"schemaVersion:1","schemaVersion:2"},{"schemaVersion:1","schemaVersion:'1'"},
      {"id:'p'","id:''"},{"id:'p'","id:'@index:0'"},{"id:'p'","id:'bad\\0id'"},
      {"part:'p'","part:'missing'"},{"kind:'assembly'","kind:'unknown'"},
      {"solid:cube({size:[1,1,1]})","solid:{}"},{"solid:cube({size:[1,1,1]})","get solid(){throw Error('x')}"},
      {"id:'p'","get id(){throw Error('x')}"},{"part:'p'","part:'p',transform:null"},
      {"part:'p'","part:'p',transform:{translate:[NaN,0,0]}"},
      {"part:'p'","part:'p',transform:{rotate:[Infinity,0,0]}"},
      {"part:'p'","part:'p',transform:{translate:[1,2]}"},
      {"part:'p'","part:'p',transform:{translate:['1',2,3]}"},
      {"part:'p'","part:'p',transform:{scale:[1,1,1]}"},
      {"part:'p'","part:'p',get transform(){throw Error('x')}"},
      {"part:'p'","part:'p',transform:{get rotate(){throw Error('x')}}"},
      {"solid:cube({size:[1,1,1]})","solid:cube({size:[1,1,1]}),quantity:0"},
      {"solid:cube({size:[1,1,1]})","solid:cube({size:[1,1,1]}),quantity:1.5"},
      {"solid:cube({size:[1,1,1]})","solid:cube({size:[1,1,1]}),exportable:1"},
      {"instances:[{id:'a',part:'p'}]","instances:[{id:'a',part:'p'},{id:'a',part:'p'}]"},
      {"defaultView:'v'","defaultView:'missing'"}}){auto s=Minimal();Replace(s,pair.first,pair.second);invalid(s);}
    invalid("({get design(){throw Error('x')}})");invalid("({design:null})");
    invalid(Minimal("","[{instance:'missing'}]"));invalid(Minimal("","[{group:'missing'}]"));
    invalid(Minimal("","[{instance:'a',group:'g'}]"));invalid(Minimal("","[null]"));
    invalid(Minimal(",groups:[{id:'g',members:[{group:'g'}]}]"));
    invalid(Minimal(",groups:[{id:'g',members:[]},{id:'g',members:[]}]"));
    invalid(Minimal(",groups:[{id:'unused',members:[{instance:'missing'}]}]"));
    auto unused=Minimal();Replace(unused,"views:[","views:[{id:'unused',kind:'plate',members:[{instance:'missing'}]},");invalid(unused);
    auto duplicateView=Minimal();Replace(duplicateView,"views:[","views:[{id:'v',kind:'plate',members:[]},");invalid(duplicateView);
    auto duplicatePart=Minimal();Replace(duplicatePart,"parts:[","parts:[{id:'p',solid:cube({size:[1,1,1]})},");invalid(duplicatePart);
    auto placement=Minimal();Replace(placement,"members:[{instance:'a'}]","members:[{instance:'a'}],placements:{missing:{}}");invalid(placement);
    auto absentPlacement=Fixture();Replace(absentPlacement,"placements:{a:{translate:[30,0,0]}}","placements:{b:{translate:[30,0,0]}}");invalid(absentPlacement);
    for(const auto* name:{"scene","displayParts"}){auto mixed=Minimal();Replace(mixed,"({design:",std::string("({")+name+":[],design:");invalid(mixed);}
    std::string groups=",groups:[";
    for(int i=0;i<33;++i){if(i)groups+=",";groups+="{id:'g"+std::to_string(i)+"',members:["+(i==32?std::string("{instance:'a'}"):"{group:'g"+std::to_string(i+1)+"'}")+"]}";}groups+="]";
    invalid(Minimal(groups,"[{group:'g0'}]"));
    std::string expansion=",groups:[";
    for(int i=0;i<18;++i){if(i)expansion+=",";const auto child=i==17?std::string("{instance:'a'}"):"{group:'g"+std::to_string(i+1)+"'}";
      expansion+="{id:'g"+std::to_string(i)+"',members:["+child+","+child+"]}";}expansion+="]";
    invalid(Minimal(expansion,"[{group:'g0'}]"));
    auto excessive=Minimal();Replace(excessive,"instances:[{id:'a',part:'p'}]","instances:Array(10001).fill({id:'a',part:'p'})");invalid(excessive);
    Require(!Parse(ctx,Minimal(),"missing").diagnostic.empty(),"unknown requested view rejected");
    auto empty=Parse(ctx,"({design:{schemaVersion:1,defaultView:'v',parts:[],instances:[],views:[{id:'v',kind:'plate',members:[]}]}})");
    Require(empty.diagnostic.empty()&&empty.scene&&empty.appearance.parts.empty(),"explicit empty design view accepted");
    JS_FreeContext(ctx);JS_FreeRuntime(rt);std::cout<<"PASS shared design graph validation, aliases, transforms, views and identity\n";
  }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
