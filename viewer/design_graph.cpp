#include "design_graph.h"
#include "js_bindings.h"
#include "project_contract.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <map>
#include <set>
#include <stdexcept>

namespace dingcad {
namespace {
using Json = nlohmann::json;
constexpr size_t kLimit = 10000, kPathLimit = 100000;
struct Value {
  JSContext* ctx; JSValue value;
  Value(JSContext* c, JSValue v):ctx(c),value(v){}
  ~Value(){JS_FreeValue(ctx,value);}
  Value(const Value&)=delete;
};
struct Transform { std::array<double,3> rotate{}, translate{}; };
struct Ref { bool group; std::string id; };
struct Part { DisplayPart display; Json metadata; };
struct Instance { std::string id, part, name; Color color{}; bool exportable=true;
  Transform transform; Json metadata; };
struct Group { std::string id,name; std::vector<Ref> members; Json metadata; };
struct View { std::string id,name,kind; std::vector<Ref> members;
  std::map<std::string,Transform> placements; Json metadata; };
class Parser {
 public:
  JSContext* ctx; size_t entries=0;
  std::map<std::string,Part> parts;
  std::map<std::string,Instance> instances;
  std::map<std::string,Group> groups;
  std::map<std::string,View> views;
  explicit Parser(JSContext* c):ctx(c){}
  [[noreturn]] void fail(const std::string& s){throw std::runtime_error(s);}
  JSValue get(JSValueConst object,const char* key){
    auto v=JS_GetPropertyStr(ctx,object,key);
    if(JS_IsException(v)||JS_HasException(ctx)){JS_FreeValue(ctx,v);fail(std::string("getter failed: ")+key);}
    return v;
  }
  void object(JSValueConst v){if(!JS_IsObject(v)||JS_IsNull(v)||JS_IsArray(v)||JS_IsFunction(ctx,v))fail("expected object");}
  std::string text(JSValueConst v,bool id=false){
    if(!JS_IsString(v))fail("expected nonempty string");
    size_t n=0;const char* p=JS_ToCStringLen(ctx,&n,v);
    if(!p)fail("string conversion failed");
    std::string s(p,n);JS_FreeCString(ctx,p);
    if(s.empty()||s.find('\0')!=std::string::npos||(id&&s.rfind("@index:",0)==0))fail("invalid/reserved identifier or label");
    return s;
  }
  std::string field(JSValueConst v,const char* key,const std::string& fallback="",bool id=false){
    Value x(ctx,get(v,key));return JS_IsUndefined(x.value)&&!fallback.empty()?fallback:text(x.value,id);
  }
  uint32_t array(JSValueConst v,bool count=true){
    if(!JS_IsArray(v))fail("expected array");
    Value n(ctx,get(v,"length"));uint32_t size=0;
    if(JS_ToUint32(ctx,&size,n.value)<0||size>kLimit)fail("excessive array length");
    if(count&&(entries+=size)>kLimit)fail("excessive design entries");
    return size;
  }
  template<class F> void each(JSValueConst v,F f,bool count=true){
    const auto n=array(v,count);for(uint32_t i=0;i<n;++i){
      Value x(ctx,JS_GetPropertyUint32(ctx,v,i));
      if(JS_IsException(x.value)||JS_HasException(ctx))fail("array getter failed");f(x.value);
    }
  }
  std::vector<std::string> keys(JSValueConst v){
    JSPropertyEnum* table=nullptr;uint32_t n=0;
    if(JS_GetOwnPropertyNames(ctx,&table,&n,v,JS_GPN_STRING_MASK|JS_GPN_SYMBOL_MASK)<0)fail("property enumeration failed");
    std::vector<std::string> result;
    for(uint32_t i=0;i<n;++i){const char* key=JS_AtomToCString(ctx,table[i].atom);
      if(key){result.emplace_back(key);JS_FreeCString(ctx,key);}else result.emplace_back("<invalid>");
      JS_FreeAtom(ctx,table[i].atom);
    }js_free(ctx,table);return result;
  }
  Transform transform(JSValueConst v){
    Transform t;if(JS_IsUndefined(v))return t;object(v);
    for(const auto& k:keys(v))if(k!="rotate"&&k!="translate")fail("unknown transform field: "+k);
    for(const auto* key:{"rotate","translate"}){Value a(ctx,get(v,key));if(JS_IsUndefined(a.value))continue;
      if(array(a.value,false)!=3)fail("transform requires three coordinates");
      auto& target=std::string(key)=="rotate"?t.rotate:t.translate;
      for(uint32_t i=0;i<3;++i){Value x(ctx,JS_GetPropertyUint32(ctx,a.value,i));double number=0;
        if(!JS_IsNumber(x.value)||JS_ToFloat64(ctx,&number,x.value)<0||!std::isfinite(number))fail("transform coordinate must be finite number");target[i]=number;}
    }return t;
  }
  Json transformJson(const Transform& t){return {{"rotate",t.rotate},{"translate",t.translate}};}
  std::string colorText(Color c){const char* hex="0123456789abcdef";std::string s="#";
    for(auto x:{c.r,c.g,c.b}){s+=hex[x>>4];s+=hex[x&15];}return s;}
  void style(JSValueConst v,DisplayPart& d){
    Value name(ctx,get(v,"name")),color(ctx,get(v,"color")),exp(ctx,get(v,"exportable"));
    if(!JS_IsUndefined(name.value))d.name=text(name.value);
    if(!JS_IsUndefined(color.value)){auto s=text(color.value);if(s.size()!=7||s[0]!='#')fail("color requires #RRGGBB");
      unsigned rgb=0;for(size_t i=1;i<7;++i){char c=s[i];unsigned nibble;
        if(c>='0'&&c<='9')nibble=c-'0';else if(c>='a'&&c<='f')nibble=c-'a'+10;else if(c>='A'&&c<='F')nibble=c-'A'+10;else fail("invalid color");rgb=(rgb<<4)|nibble;}
      d.color={static_cast<unsigned char>(rgb>>16),static_cast<unsigned char>(rgb>>8),static_cast<unsigned char>(rgb),255};}
    if(!JS_IsUndefined(exp.value)){if(!JS_IsBool(exp.value))fail("exportable requires boolean");d.exportable=JS_ToBool(ctx,exp.value)!=0;}
  }
  std::vector<Ref> refs(JSValueConst v){std::vector<Ref> result;each(v,[&](JSValueConst x){object(x);
    auto names=keys(x);if(names.size()!=1||(names[0]!="instance"&&names[0]!="group"))fail("member requires exactly one typed reference");
    result.push_back({names[0]=="group",field(x,names[0].c_str(),"",true)});
  });return result;}
  Json refsJson(const std::vector<Ref>& refs){Json a=Json::array();for(const auto& r:refs)a.push_back({{r.group?"group":"instance",r.id}});return a;}
  void read(JSValueConst design){object(design);
    Value version(ctx,get(design,"schemaVersion"));double v=0;
    if(!JS_IsNumber(version.value)||JS_ToFloat64(ctx,&v,version.value)<0||v!=1)fail("schemaVersion must be 1");
    Value p(ctx,get(design,"parts"));each(p.value,[&](JSValueConst x){object(x);Part p;
      p.display.id=field(x,"id","",true);p.display.name=p.display.id;p.display.color={170,170,170,255};
      Value solid(ctx,get(x,"solid"));p.display.solid=GetManifoldHandle(ctx,solid.value);
      if(!p.display.solid||JS_HasException(ctx))fail("part requires exact manifold solid handle");
      if(p.display.solid->Status()!=manifold::Manifold::Error::NoError)fail("part solid has invalid manifold status");style(x,p.display);
      p.metadata={{"id",p.display.id},{"name",p.display.name},{"color",colorText(p.display.color)},{"exportable",p.display.exportable}};
      Value quantity(ctx,get(x,"quantity"));if(!JS_IsUndefined(quantity.value)){double q=0;
        if(!JS_IsNumber(quantity.value)||JS_ToFloat64(ctx,&q,quantity.value)<0||!std::isfinite(q)||q<1||q!=std::floor(q)||q>9007199254740991.0)fail("quantity requires positive safe integer");p.metadata["quantity"]=static_cast<uint64_t>(q);}
      if(!parts.emplace(p.display.id,p).second)fail("duplicate part id");
    });
    Value i(ctx,get(design,"instances"));each(i.value,[&](JSValueConst x){object(x);Instance i;
      i.id=field(x,"id","",true);i.part=field(x,"part","",true);auto p=parts.find(i.part);if(p==parts.end())fail("unknown source part: "+i.part);
      DisplayPart d=p->second.display;style(x,d);i.name=d.name;i.color=d.color;i.exportable=d.exportable;
      Value t(ctx,get(x,"transform"));i.transform=transform(t.value);
      i.metadata={{"id",i.id},{"part",i.part},{"name",i.name},{"color",colorText(i.color)},{"exportable",i.exportable},{"transform",transformJson(i.transform)}};
      if(!instances.emplace(i.id,i).second)fail("duplicate instance id");
    });
    Value g(ctx,get(design,"groups"));if(!JS_IsUndefined(g.value))each(g.value,[&](JSValueConst x){object(x);Group g;
      g.id=field(x,"id","",true);g.name=field(x,"name",g.id);Value m(ctx,get(x,"members"));g.members=refs(m.value);
      g.metadata={{"id",g.id},{"name",g.name},{"members",refsJson(g.members)}};if(!groups.emplace(g.id,g).second)fail("duplicate group id");
    });
    Value w(ctx,get(design,"views"));each(w.value,[&](JSValueConst x){object(x);View w;
      w.id=field(x,"id","",true);w.name=field(x,"name",w.id);w.kind=field(x,"kind");
      if(w.kind!="assembly"&&w.kind!="inspection"&&w.kind!="plate")fail("invalid view kind");
      Value m(ctx,get(x,"members"));w.members=refs(m.value);Value placements(ctx,get(x,"placements"));Json placementsJson=Json::object();
      if(!JS_IsUndefined(placements.value)){object(placements.value);for(const auto& key:keys(placements.value)){
        if(instances.find(key)==instances.end())fail("unknown placement instance: "+key);Value t(ctx,get(placements.value,key.c_str()));
        if(JS_IsUndefined(t.value))fail("placement requires transform object");w.placements[key]=transform(t.value);placementsJson[key]=transformJson(w.placements[key]);}}
      w.metadata={{"id",w.id},{"name",w.name},{"kind",w.kind},{"members",refsJson(w.members)},{"placements",placementsJson}};
      if(!views.emplace(w.id,w).second)fail("duplicate view id");
    });
  }
  void expand(const std::vector<Ref>& refs,std::vector<GroupLabel> path,std::set<std::string>& visiting,size_t& count,
              const std::function<void(const std::string&,const std::vector<GroupLabel>&)>& emit){
    for(const auto& r:refs){if(++count>kPathLimit)fail("excessive expanded membership paths");
      if(!r.group){if(instances.find(r.id)==instances.end())fail("unknown instance reference: "+r.id);emit(r.id,path);continue;}
      auto g=groups.find(r.id);if(g==groups.end())fail("unknown group reference: "+r.id);
      if(path.size()>=32)fail("excessive group nesting");if(!visiting.insert(r.id).second)fail("group cycle: "+r.id);
      auto nested=path;nested.push_back({g->second.id,g->second.name});expand(g->second.members,nested,visiting,count,emit);visiting.erase(r.id);
    }
  }
};
}
DesignGraphResult ReadDesignGraph(JSContext* ctx,JSValueConst ns,const std::string& requestedView){
  DesignGraphResult result;Parser p(ctx);
  try {
    Value design(ctx,p.get(ns,"design"));if(JS_IsUndefined(design.value))return result;result.specified=true;
    Value legacyScene(ctx,p.get(ns,"scene")),legacyParts(ctx,p.get(ns,"displayParts"));
    if(!JS_IsUndefined(legacyScene.value)||!JS_IsUndefined(legacyParts.value))p.fail("design cannot be mixed with scene/displayParts");
    p.read(design.value);const auto defaultView=p.field(design.value,"defaultView","",true);
    if(p.views.find(defaultView)==p.views.end())p.fail("unknown defaultView");
    const auto active=requestedView.empty()?defaultView:requestedView;
    if(p.views.find(active)==p.views.end())p.fail("unknown requested view: "+active);
    const auto ignore=[](const std::string&,const std::vector<GroupLabel>&){};
    size_t validationCount=0;std::set<std::string> visiting;
    for(const auto& g:p.groups)p.expand({{true,g.first}},{},visiting,validationCount,ignore);
    for(const auto& v:p.views){
      std::set<std::string> included;
      p.expand(v.second.members,{},visiting,validationCount,[&](const std::string& id,const std::vector<GroupLabel>&){included.insert(id);});
      for(const auto& placement:v.second.placements)
        if(!included.count(placement.first))p.fail("placement instance is absent from view "+v.first+": "+placement.first);
    }
    result.appearance.specified=true;std::map<std::string,size_t> resolved;size_t count=0;
    p.expand(p.views.at(active).members,{},visiting,count,[&](const std::string& id,const std::vector<GroupLabel>& path){
      auto found=resolved.find(id);if(found!=resolved.end()){
        auto& paths=result.appearance.parts[found->second].memberships;
        auto same=[&](const std::vector<GroupLabel>& other){if(other.size()!=path.size())return false;for(size_t i=0;i<path.size();++i)if(other[i].id!=path[i].id)return false;return true;};
        if(std::find_if(paths.begin(),paths.end(),same)==paths.end())paths.push_back(path);return;
      }
      const auto& i=p.instances.at(id);DisplayPart d=p.parts.at(i.part).display;d.id=id;d.sourcePartId=i.part;d.name=i.name;d.color=i.color;d.exportable=i.exportable;
      auto t=i.transform;auto placement=p.views.at(active).placements.find(id);if(placement!=p.views.at(active).placements.end())t=placement->second;
      d.rotation=t.rotate;d.translation=t.translate;d.memberships.push_back(path);for(const auto& g:path)d.group.push_back(g.name);
      d.sourceSolid=d.solid;
      d.solid=std::make_shared<manifold::Manifold>(d.solid->Rotate(t.rotate[0],t.rotate[1],t.rotate[2]).Translate({t.translate[0],t.translate[1],t.translate[2]}));
      resolved[id]=result.appearance.parts.size();result.appearance.parts.push_back(std::move(d));
    });
    Json metadata={{"schemaVersion",1},{"defaultView",defaultView},{"activeView",active}};
    for(const auto* key:{"sourceParts","instances","groups","views","resolvedInstances"})metadata[key]=Json::array();
    for(const auto& x:p.parts)metadata["sourceParts"].push_back(x.second.metadata);
    for(const auto& x:p.instances)metadata["instances"].push_back(x.second.metadata);
    for(const auto& x:p.groups)metadata["groups"].push_back(x.second.metadata);
    for(const auto& x:p.views)metadata["views"].push_back(x.second.metadata);
    std::vector<manifold::Manifold> solids;
    for(const auto& d:result.appearance.parts){solids.push_back(*d.solid);metadata["resolvedInstances"].push_back({{"id",d.id},{"sourcePartId",d.sourcePartId},{"transform",{{"rotate",d.rotation},{"translate",d.translation}}}});}
    result.scene=std::make_shared<manifold::Manifold>(manifold::Manifold::Compose(solids));
    metadata["identity"]=synthcad::Sha256(metadata.dump());result.metadata=std::move(metadata);
  }catch(const std::exception& e){if(JS_HasException(ctx))JS_FreeValue(ctx,JS_GetException(ctx));
    result.specified=true;result.scene.reset();result.appearance.parts.clear();result.metadata=Json();
    result.diagnostic=std::string("design: ")+e.what()+"; load rejected, export disabled";result.appearance.diagnostic=result.diagnostic;}
  return result;
}
}
