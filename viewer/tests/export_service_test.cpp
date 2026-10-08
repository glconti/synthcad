#include "export_service.h"
#include "project_contract.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace dingcad;
using nlohmann::json;
namespace {
void Require(bool condition,const char *message) {if(!condition)throw std::runtime_error(message);}
std::string Read(const std::filesystem::path &path) {std::ifstream in(path,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
void Write(const std::filesystem::path &path,const std::string &bytes) {std::ofstream out(path,std::ios::binary);out<<bytes;}
DisplayPart Part(const std::string &id,bool exportable=true) {
  DisplayPart p; p.id=id;p.name=id;p.sourcePartId="shared";p.exportable=exportable;
  p.solid=std::make_shared<manifold::Manifold>(manifold::Manifold::Cube({2,3,4}));
  p.sourceSolid=p.solid;return p;
}
json Context() {
  return {{"current",true},{"projectPath","project"},{"view","plate-a"},{"kind","plate"},
    {"modelRevision","model"},{"sourceRevision","source"},{"layoutRevision","layout"},{"profileRevision",nullptr},
    {"revision","displayed"},{"profile",{{"status","incomplete"}}},{"dependencies",json::object()},
    {"checks",json::array({{{"id","geometry-validity"},{"result","passed"},{"scope","geometry"}}})}};
}
json Options(const std::filesystem::path &path,const std::string &format) {return {{"path",path.u8string()},{"format",format}};}
}
int main() {try {
  const auto root=std::filesystem::temp_directory_path()/("synthcad-export-service-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(root);
  PartTree tree;tree.Reload({Part("one"),Part("two"),Part("ref",false)});
  auto context=Context();std::string error;
  const auto future=root/"not-created"/"plate.3mf";
  auto options=Options(future,"3mf");
  auto review=ReviewExport(tree,context,options);
  Require(review["ok"] && review["record"].is_null() && !std::filesystem::exists(future.parent_path()),"Review must not allocate destination files or directories");
  Require(review["review"]["partIds"]==json::array({"one","two"}) && review["review"]["quantities"]==json::array({{{"sourcePartId","shared"},{"count",2}}}),"Review preserves selected distinct quantities and excludes references");
  context["checks"].push_back({{"id","support"},{"scope","physical"},{"result","not-checked"}});
  options["dryRun"]=true;
  auto result=ExecuteExport(tree,context,options);
  Require(result["ok"] && result["code"]=="dry_run" && result["review"]["hasWarnings"] && !std::filesystem::exists(future.parent_path()),"Dry run must disclose warnings without filesystem writes");
  options["dryRun"]=false;
  result=ExecuteExport(tree,context,options);
  Require(!result["ok"] && result["code"]=="warnings_present" && !std::filesystem::exists(future.parent_path()),"Warnings need explicit acknowledgement");
  options["allowWarnings"]=true;
  for(const std::string format:{"3mf","stl"}) {
    const auto path=root/("plate."+format);options=Options(path,format);options["allowWarnings"]=true;
    result=ExecuteExport(tree,context,options);
    Require(result["ok"] && result["code"]=="saved" && !result["record"].is_null(),"Acknowledged export failed");
    const auto original=Read(path);
    Require(result["record"]["sha256"]==synthcad::Sha256(original) && result["record"]["sizeBytes"]==original.size() && result["record"]["basis"]["layoutRevision"]=="layout", "Receipt must describe committed bytes and complete basis");
    Require(result["record"]["checks"]==context["checks"] && result["record"]["dependencies"]==context["dependencies"],"Receipt must retain checks and consumed dependencies");
    Require(result["record"]["id"].get<std::string>().size()==64 && result["record"]["createdAt"].get<std::string>().back()=='Z',"Committed receipt must include unique identity and UTC creation time");
    result=ExecuteExport(tree,context,options);
    Require(!result["ok"] && result["code"]=="destination_exists" && Read(path)==original,"Existing destination needs replace acknowledgement");
    options["replace"]=true;
    int called=0;
    result=ExecuteExport(tree,context,options,[&](){++called;return "stale_revision";});
    Require(called==1 && !result["ok"] && result["code"]=="stale_revision" && result["record"].is_null() && Read(path)==original,"Stale guard must preserve existing destination and emit no receipt");
    called=0;
    result=ExecuteExport(tree,context,options,[&](){return ++called==2?"stale_revision":"";});
    Require(called==2 && !result["ok"] && result["code"]=="stale_revision" && result["record"].is_null() && Read(path)==original,"Final stale guard must run after staging and preserve destination");
    result=ExecuteExport(tree,context,options,[](){return "timeout";});
    Require(!result["ok"] && result["code"]=="timeout" && Read(path)==original,"Expired guard must prevent late commit");
    called=0;
    result=ExecuteExport(tree,context,options,[&](){return ++called==2?"timeout":"";});
    Require(called==2 && !result["ok"] && result["code"]=="timeout" && Read(path)==original,"Deadline expiring during serialization must prevent final commit");
    result=ExecuteExport(tree,context,options,[]()->std::string{throw std::runtime_error("guard failed");});
    Require(!result["ok"] && result["record"].is_null() && Read(path)==original,"Guard exception must preserve destination");
    const auto race=root/("race."+format);options=Options(race,format);options["allowWarnings"]=true;
    int raceGuards=0;
    result=ExecuteExport(tree,context,options,[&](){if(++raceGuards==2)Write(race,"concurrent destination");return std::string();});
    Require(!result["ok"] && result["code"]=="destination_exists" && Read(race)=="concurrent destination","Commit must atomically enforce no overwrite");
  }
  for(const auto &entry:std::filesystem::directory_iterator(root))Require(entry.path().filename().u8string().find(".synthcad-export-")!=0,"Staging files leaked");
  const auto serialization=root/"serialize.3mf";Write(serialization,"prior artifact");
  options=Options(serialization,"3mf");options["replace"]=true;options["allowWarnings"]=true;
  tree.parts[0].name=std::string("bad\0name",8);
  result=ExecuteExport(tree,context,options);
  Require(!result["ok"] && result["code"]=="io_error" && result["record"].is_null() && Read(serialization)=="prior artifact","Failed serialization must not truncate existing destination");
  tree.parts[0].name="one";
  options=Options(root/"changed.3mf","3mf");options["allowWarnings"]=true;
  const auto dependency=root/"source.js";Write(dependency,"before");
  context["dependencies"]={{synthcad::CanonicalPath(dependency),synthcad::Sha256("before")}};
  Write(dependency,"after");
  result=ExecuteExport(tree,context,options);
  Require(!result["ok"] && result["code"]=="stale_revision" && !std::filesystem::exists(root/"changed.3mf"),"Changed consumed source must prevent commit");
  context["dependencies"]=json::object();context["current"]=false;
  Require(ExecuteExport(tree,context,options)["code"]=="stale_revision","Noncurrent load must block acknowledged export");
  context=Context();options["format"]="obj";Require(ReviewExport(tree,context,options)["code"]=="invalid_argument","Unknown format accepted");
  options=Options(root/"wrong.stl","3mf");Require(ReviewExport(tree,context,options)["code"]=="invalid_argument","Mismatched destination extension accepted");
  options=Options(root/"upper.3MF","3mf");Require(ReviewExport(tree,context,options)["ok"],"Extension matching should ignore case");
  options["format"]="3mf";options["path"]="relative.3mf";Require(ReviewExport(tree,context,options)["code"]=="invalid_argument","Relative destination accepted");
  options=Options(root/"missing-checks.3mf","3mf");context.erase("checks");
  Require(ReviewExport(tree,context,options)["review"]["hasWarnings"] && ExecuteExport(tree,context,options)["code"]=="warnings_present","Missing manufacturing review must remain an explicit risk");
  context=Context();context["checks"][0]["result"]="failed";
  Require(ReviewExport(tree,context,options)["ok"],"Unrelated cached authored failure must not invalidate a valid selected subset");
  tree.state.flags.at("ref").exportable=true;
  Require(ReviewExport(tree,context,options)["review"]["partIds"].size()==3,"Explicitly enabled reference must join selected validation/export");
  tree.parts[2].solid.reset();options["allowWarnings"]=true;
  Require(ExecuteExport(tree,context,options)["code"]=="load_failed","Invalid newly enabled reference bypassed selected solid validation");
  tree.state.flags.at("ref").exportable=false;tree.parts[0].solid=std::make_shared<manifold::Manifold>();
  Require(ExecuteExport(tree,context,options)["code"]=="empty_export","Empty selected part accepted");
  tree.Reload({});Require(ExecuteExport(tree,context,options)["code"]=="empty_export","Empty selection accepted");
  std::filesystem::remove_all(root);
  std::cout<<"PASS export review, warnings, staged STL/3MF, atomic races, stale/deadline guards and receipts\n";
  return 0;
}catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}}
