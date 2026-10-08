#include "part_tree.h"
#include <algorithm>
#include <cctype>
#include <functional>

namespace dingcad {
void PartTree::Reload(std::vector<DisplayPart> next) {
  parts=std::move(next);nodes.clear();roots_.clear();
  std::unordered_map<std::string,PartFlags> retained;
  std::unordered_map<std::string,size_t> groups;
  for(size_t i=0;i<parts.size();++i){
    const auto &p=parts[i];
    auto found=state.flags.find(p.id);
    auto flags=found==state.flags.end()?PartFlags{true,p.exportable,false}:found->second;
    if(!flags.exportOverride)flags.exportable=p.exportable;
    retained[p.id]=flags;
    if(state.isolated && found==state.flags.end()) {
      state.beforeIsolation[p.id]=true;retained[p.id].visible=false;
    }
    std::string key="g:";int parent=-1;
    for(const auto &label:p.group){
      key+=std::to_string(label.size())+":"+label;
      auto g=groups.find(key);size_t index;
      if(g==groups.end()){
        index=nodes.size();groups[key]=index;nodes.push_back({key,label,true,parent,{},{}});
        if(parent<0)roots_.push_back(index);else nodes[parent].children.push_back(index);
      }else index=g->second;
      nodes[index].parts.push_back(i);parent=static_cast<int>(index);
    }
    size_t leaf=nodes.size();nodes.push_back({"p:"+p.id,p.name,false,parent,{}, {i}});
    if(parent<0)roots_.push_back(leaf);else nodes[parent].children.push_back(leaf);
  }
  state.flags=std::move(retained);
  std::unordered_set<std::string> valid;
  for(const auto &n:nodes)valid.insert(n.key);
  for(auto it=state.collapsed.begin();it!=state.collapsed.end();)
    if(!valid.count(*it))it=state.collapsed.erase(it);else ++it;
  for(auto it=state.beforeIsolation.begin();it!=state.beforeIsolation.end();)
    if(!state.flags.count(it->first))it=state.beforeIsolation.erase(it);else ++it;
  if(!valid.count(state.selected)){
    state.selected.clear();
    if(state.isolated)Isolate(); // restore when the isolated selection disappeared
  }
}
std::vector<TreeRow> PartTree::Rows(const std::string &query) const {
  auto lower=[](std::string s){for(char &c:s)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));return s;};
  const auto needle=lower(query);
  std::vector<bool> matches(nodes.size(),false);
  std::function<bool(size_t)> match=[&](size_t n){
    bool result=needle.empty()||lower(nodes[n].name).find(needle)!=std::string::npos;
    for(auto c:nodes[n].children)result=match(c)||result;
    return matches[n]=result;
  };
  for(auto r:roots_)match(r);
  std::vector<TreeRow> rows;
  std::function<void(size_t,int,bool)> walk=[&](size_t n,int depth,bool ancestor){
    if(!matches[n]&&!ancestor)return;
    rows.push_back({n,depth});
    const bool ownMatch=!needle.empty()&&lower(nodes[n].name).find(needle)!=std::string::npos;
    if(!needle.empty()||!state.collapsed.count(nodes[n].key))
      for(auto c:nodes[n].children)walk(c,depth+1,ancestor||ownMatch);
  };
  for(auto r:roots_)walk(r,0,false);
  return rows;
}
CheckState PartTree::Checked(size_t node,bool exporting) const {
  size_t yes=0;
  for(auto i:nodes.at(node).parts){const auto &f=state.flags.at(parts[i].id);yes+=exporting?f.exportable:f.visible;}
  return yes==0?CheckState::None:(yes==nodes[node].parts.size()?CheckState::All:CheckState::Mixed);
}
void PartTree::Toggle(size_t node,bool exporting){
  const bool value=Checked(node,exporting)!=CheckState::All;
  for(auto i:nodes.at(node).parts){auto &f=state.flags.at(parts[i].id);
    if(exporting){f.exportable=value;f.exportOverride=true;}else f.visible=value;}
}
void PartTree::Select(size_t node){state.selected=nodes.at(node).key;}
std::optional<size_t> PartTree::Selection() const {
  for(size_t i=0;i<nodes.size();++i)if(nodes[i].key==state.selected)return i;
  return {};
}
void PartTree::Isolate(){
  if(state.isolated){
    for(auto &[id,f]:state.flags){auto old=state.beforeIsolation.find(id);if(old!=state.beforeIsolation.end())f.visible=old->second;}
    state.beforeIsolation.clear();state.isolated=false;return;
  }
  const auto selected=Selection();if(!selected)return;
  state.beforeIsolation.clear();
  for(auto &[id,f]:state.flags){state.beforeIsolation[id]=f.visible;f.visible=false;}
  for(auto i:nodes[*selected].parts)state.flags.at(parts[i].id).visible=true;
  state.isolated=true;
}
void PartTree::ShowAll(){state.isolated=false;state.beforeIsolation.clear();for(auto &[_,f]:state.flags)f.visible=true;}
bool PartTree::Visible(size_t part) const{return state.flags.at(parts.at(part).id).visible;}
std::vector<size_t> PartTree::ExportIndices(bool visibleOnly) const {
  std::vector<size_t> result;
  for(size_t i=0;i<parts.size();++i){const auto &f=state.flags.at(parts[i].id);if(f.exportable&&(!visibleOnly||f.visible))result.push_back(i);}
  return result;
}
std::optional<manifold::Manifold> PartTree::ExportSolid(bool visibleOnly) const {
  std::vector<manifold::Manifold> solids;
  for(auto i:ExportIndices(visibleOnly))solids.push_back(*parts[i].solid);
  if(solids.empty())return {};
  return manifold::Manifold::Compose(solids);
}
}
