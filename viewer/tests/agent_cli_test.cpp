#include "agent_cli.h"
#include "agent_entry.h"
#include "project_contract.h"

#include <iostream>
#include <set>
#include <stdexcept>

namespace {
using nlohmann::json;
using synthcad::ParseCli;
void Require(bool condition, const std::string& message) {
  if (!condition) throw std::runtime_error(message);
}
std::vector<std::string> Words(const std::string& path) {
  std::vector<std::string> words;
  size_t begin=0;
  while(begin<path.size()) { auto end=path.find(' ',begin); words.push_back(path.substr(begin,end-begin)); if(end==std::string::npos)break; begin=end+1; }
  return words;
}
void CheckDiscovery() {
  std::vector<std::string> pending={""};
  std::set<std::string> seen, operations;
  int actions=0;
  while(!pending.empty()) {
    const auto path=pending.back();pending.pop_back();
    Require(seen.insert(path).second,"Duplicate path: "+path);
    const auto data=synthcad::HelpData(path);
    for(const auto* key:{"path","kind","summary","usage","children","arguments","options","requirements","examples","guidance","nextSteps","hash","bundleVersion","bundleHash"})
      Require(data.contains(key),std::string("Missing help field: ")+key+" at "+path);
    Require(!data["guidance"].get<std::string>().empty(),"Empty guidance: "+path);
    Require(data["hash"]==synthcad::Sha256(data["guidance"]),"Wrong help content hash");
    auto args=Words(path);args.push_back("--help");args.push_back("--json");
    auto parsed=ParseCli(args);
    Require(bool(parsed)&&parsed.options.help&&parsed.options.jsonOutput&&parsed.options.command==path,"Help does not parse: "+path);
    const auto text=synthcad::Help(path);
    Require(text.find(data["guidance"].get<std::string>())!=std::string::npos,"Text/JSON guidance diverged");
    if(data["kind"]=="action") {
      ++actions;
      Require(operations.insert(parsed.options.operation).second,"Duplicate executable operation");
      Require(!data["examples"].empty(),"Action missing example: "+path);
    } else Require(ParseCli(Words(path)).options.help,"Bare topic should show help");
    for(const auto& example:data["examples"]) {
      auto exampleArgs=example["argv"].get<std::vector<std::string>>();
      auto exampleParse=ParseCli(exampleArgs);
      Require(bool(exampleParse),"Invalid example: "+example["command"].get<std::string>()+": "+exampleParse.error);
      Require(exampleParse.options.command==path,"Example belongs to wrong node");
    }
    for(const auto& next:data["nextSteps"]) Require(synthcad::HelpData(next.get<std::string>()).is_object(),"Broken next step");
    for(const auto& child:data["children"])pending.push_back(child["path"]);
  }
  Require(actions==20,"Expected exactly 20 executable leaves");
  const auto root=synthcad::HelpData();
  Require(root["children"].size()==4,"Expected four domains");
  Require(!root["capabilities"]["geometryEditing"].get<bool>() && root["capabilities"]["export"].get<bool>(),"Capability truth changed");
  for(const auto* phrase:{"human","JavaScript","persistent","REQUESTED_TOKEN","DISPLAYED_TOKEN","Slicer","physical","source","--help"})
    Require(root["guidance"].get<std::string>().find(phrase)!=std::string::npos,std::string("Kickstart missing ")+phrase);
}
void CheckParsing() {
  auto parsed=ParseCli({"--json","project","--name","bracket","open","My Project/scene.js","--hidden"});
  Require(bool(parsed)&&parsed.options.command=="project open"&&parsed.options.operation=="open"&&parsed.options.session=="bracket","Open targeting missing");
  Require(parsed.options.arguments==json({{"path","My Project/scene.js"},{"hidden",true}}),"Name leaked into operation payload");
  parsed=ParseCli({"--project=bracket","review","selection","scsel1.token","--expect-revision","rev"});
  Require(bool(parsed)&&parsed.options.operation=="reference"&&parsed.options.command=="review selection"&&parsed.options.arguments["reference"]=="scsel1.token","Reference consolidation failed");
  Require(ParseCli({"review","selection"}).options.operation=="selection","Current selection failed");
  parsed=ParseCli({"project","open","--","--scene.js"});
  Require(bool(parsed)&&parsed.options.arguments["path"]=="--scene.js","Literal path failed");
  parsed=ParseCli({"review","pick","status","--","-request"});
  Require(bool(parsed)&&parsed.options.arguments["id"]=="-request","Literal request ID failed");
  Require(ParseCli({"project","wait","--revision=rev","--timeout","0"}).options.timeoutMs==0,"Zero timeout failed");
  Require(bool(ParseCli({"project","wait","--revision","rev","--timeout","300000"})),"Maximum timeout failed");
  Require(bool(ParseCli({"project","reload","--evaluation-timeout","3600000"})),"Evaluation maximum failed");
  Require(bool(ParseCli({"print","profile","--template"})),"Offline template failed");
  parsed=ParseCli({"project","events","--after","opaque:cursor","--wait","300000"});
  Require(bool(parsed)&&parsed.options.arguments==json({{"after","opaque:cursor"},{"waitMs",300000}}),"Events changed");
  Require(ParseCli({"project","events","--after","0"}).options.arguments["waitMs"]==0,"Event default missing");
  parsed=ParseCli({"print","export",u8"Piatto città 日本.3MF","--visible-only","--replace","--allow-warnings","--dry-run"});
  Require(bool(parsed)&&parsed.options.arguments["format"]=="3mf"&&parsed.options.arguments["dryRun"]==true,"Export flags missing");
  Require(bool(ParseCli({"print","export","destination","--format","3mf"})),"Explicit format must reach viewer guard");
  Require(bool(ParseCli({"review","highlight","--clear"}))&&bool(ParseCli({"review","frame"})),"Highlight/frame modes failed");
  for(const auto* kind:{"part","surface","edge","vertex"})
    Require(bool(ParseCli({"review","pick","request","--id","request","--kind",kind,"--question",u8"Which pièce?"})),"Pick kind failed");
  Require(bool(ParseCli({"review","pick","request","--id",std::string(128,'i'),"--kind","part","--question",std::string(4096,'q')})),"Boundary lengths failed");
  for(const auto& args:std::vector<std::vector<std::string>>{
      {"open","scene.js"},{"snapshot"},{"state"},{"overview"},{"docs"},{"skill"},{"help"},{"capabilities"},{"version"},
      {"project","open"},{"project","open","a","b"},{"project","open","a","--project","p"},
      {"project","inspect","--name","p"},{"project","inspect","--session","p"},{"project","inspect","-s","p"},
      {"project","inspect","--project","p","--project","q"},{"project","inspect","--unknown"},
      {"project","wait"},{"project","wait","--revision"},{"project","wait","--revision","r","--timeout","-1"},
      {"project","inspect","--timeout","300001"},{"project","inspect","--timeout","12x"},
      {"project","inspect","--timeout","99999999999999"},{"project","inspect","--json=true"},
      {"project","reload","--evaluation-timeout","0"},{"project","inspect","--evaluation-timeout","100"},
      {"project","revision","--expect-revision","r"},{"print","profile","--template","--expect-revision","r"},
      {"review","selection",""},{"review","selection","a","b"},{"review","highlight"},
      {"review","highlight","base","--clear"},{"review","frame","base","--selection"},
      {"review","pick","status"},{"review","pick","cancel","id","--expect-revision","r"},
      {"review","pick","request","--id","i","--kind","face","--question","q"},
      {"review","pick","request","--id",std::string(129,'i'),"--kind","part","--question","q"},
      {"review","pick","request","--id","i","--kind","part","--question",std::string(4097,'q')},
      {"review","pick","status",std::string("a\0b",3)},
      {"project","events"},{"project","events","--after","0","--wait","300001"},
      {"print","export"},{"print","export","a.obj"},{"print","export","a.stl","--format","obj"},
      {"print","export",std::string("a\0b.stl",7)},{"print","history","--replace"},
      {"--version","project"},{"--version","--help"},{"model","unknown"}})
    Require(!ParseCli(args),"Invalid input accepted: "+args.front());
  Require(ParseCli({"--version","--json"}).options.version,"Version flag failed");
  Require(ParseCli({"project","wait","--help"}).options.help,"Help requires action options");
  Require(ParseCli({"print","export","--help"}).options.help,"Help requires positionals");
  Require(ParseCli({"wat","--json"}).options.jsonOutput,"Parse errors must respect JSON");
  Require(ParseCli({"snapshot"}).error.find("project inspect")!=std::string::npos,"Missing migration guidance");
  Require(ParseCli({"review","wat"}).error.find("review selection")!=std::string::npos,"Missing sibling suggestion");
  Require(synthcad::IsAgentCommand({"app","wat"})&&!synthcad::IsAgentCommand({"app","./scene.js"}),"Unknown command routed as scene");
  Require(!synthcad::IsAgentCommand({"app","--check-scene","scene.js"}),"Developer viewer flags changed");
}
void CheckProtocol() {
  auto internal=synthcad::Success("snapshot",{{"overview",{{"session","authored"},{"project","also authored"}}}},"bracket","rev");
  auto response=synthcad::CliResponse(internal,"project inspect");
  Require(internal["protocolVersion"]==1&&internal["session"]=="bracket","Internal protocol changed");
  Require(response["protocolVersion"]==2&&response["project"]=="bracket"&&!response.contains("session")&&response["command"]=="project inspect","CLI envelope translation failed");
  Require(response["data"]==internal["data"],"Authored payload changed");
  auto list=synthcad::CliResponse(synthcad::Success("sessions",{{"sessions",json::array({{{"session","p"},{"projectPath","/path"}}})}}),"project list");
  Require(list["data"]["projects"][0]["project"]=="p"&&!list["data"].contains("sessions"),"Project list translation failed");
  auto failure=synthcad::CliResponse(synthcad::Error("snapshot","ambiguous_session","old",{{"sessions",{"a","b"}}}),"project inspect");
  Require(failure["error"]["code"]=="ambiguous_project"&&failure["error"]["details"]["projects"].size()==2,"Ambiguous translation failed");
  Require(synthcad::ExitCode("no_project")==3&&synthcad::ExitCode("ambiguous_project")==4&&synthcad::ExitCode("empty_export")==16,"Exit codes changed");
  auto diagnostic=synthcad::CliResponse(synthcad::Error("wait","load_failed","session is an authored variable",{{"session","authored"}}),"project wait");
  Require(diagnostic["error"]["details"]["session"]=="authored"&&diagnostic["error"]["message"]=="session is an authored variable","Authored diagnostic changed");
  Require(json::parse(synthcad::FormatResponse(response,true))==response,"JSON encoding changed");
  auto fragment=json{{"activeProfile","custom"},{"profiles",{{"custom",json::object()}}}};
  auto templ=synthcad::CliResponse(synthcad::Success("profile",fragment),"print profile");
  Require(json::parse(synthcad::FormatResponse(templ,false))==fragment,"Plain template must remain JSON");
  Require(json::parse(synthcad::FormatResponse(templ,true))==templ,"Template envelope changed");
}
}
int main() {
  try { CheckDiscovery();CheckParsing();CheckProtocol();std::cout<<"Domain CLI tests passed\n";return 0; }
  catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
