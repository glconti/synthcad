#include "agent_bridge.h"
#include "agent_cli.h"
#include <algorithm>
#include <atomic>
#include <random>

namespace synthcad {
using json=nlohmann::json;
namespace {
std::string EventEpoch() {
  static std::atomic<uint64_t> serial{0};
  return Sha256(std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count()) +
                ":" + std::to_string(++serial) + ":" + std::to_string(std::random_device{}()));
}
std::string PickId(const json& args) {
  if (!args.is_object() || !args.contains("id") || !args["id"].is_string()) throw GuidedPickError("invalid_argument", "Expected string id");
  return args["id"].get<std::string>();
}
}
AgentBridge::AgentBridge(std::string session, std::string eventEpoch):session_(std::move(session)),picks_(eventEpoch.empty()?EventEpoch():std::move(eventEpoch)) {
  snapshot_={{"status","loading"},{"displayedRevision",""},{"attemptedRevision",""},
             {"exportValid",false},{"view",""},{"parts",json::array()},
             {"selection",nullptr},{"diagnostic",""}};
  UpdatePickSnapshot();
}
void AgentBridge::UpdatePickSnapshot() { snapshot_["guidedPick"]=picks_.Active(); snapshot_["eventCursor"]=picks_.Cursor(); }
json AgentBridge::BeginPick(const json& args,const std::string& revision) {
  std::lock_guard<std::mutex> lock(mutex_);
  if(closed_)throw GuidedPickError("cancelled","Viewer closed");
  if(auto existing=picks_.Replay(args))return *existing;
  if(snapshot_.value("status","")!="ready")throw GuidedPickError("busy","The displayed scene is not ready");
  if(revision!=snapshot_.value("displayedRevision","")||!MatchesDisk(files_))throw GuidedPickError("stale_revision","Displayed source files have changed");
  auto result=picks_.Begin(args,revision); UpdatePickSnapshot(); changed_.notify_all(); return result;
}
json AgentBridge::ActivePick() { std::lock_guard<std::mutex> lock(mutex_); return picks_.Active(); }
bool AgentBridge::ConfirmPick(const std::string& id,const json& geometry,const std::string& revision) {
  std::lock_guard<std::mutex> lock(mutex_);
  if(closed_)return false;
  const bool confirmed=picks_.Confirm(id,geometry,revision); UpdatePickSnapshot(); changed_.notify_all(); return confirmed;
}
void AgentBridge::InvalidatePick(const std::string& reason) {
  std::lock_guard<std::mutex> lock(mutex_); picks_.Invalidate(reason); UpdatePickSnapshot(); changed_.notify_all();
}
void AgentBridge::Publish(json snapshot,FileSnapshot files){
  {std::lock_guard<std::mutex> lock(mutex_);
    const auto active=picks_.Active();
    if(!active.is_null()) {
      if(snapshot.value("status","")=="failed")picks_.Invalidate("load_failed");
      else if(snapshot.value("status","")=="ready"&&snapshot.value("displayedRevision","")!=active.value("revision",""))picks_.Invalidate("revision_changed");
    }
    if(snapshot.value("status","")!=snapshot_.value("status","")||snapshot.value("attemptedRevision","")!=snapshot_.value("attemptedRevision",""))
      picks_.LoadEvent("load-"+snapshot.value("status",std::string("loading")),{{"attemptedRevision",snapshot.value("attemptedRevision","")},{"displayedRevision",snapshot.value("displayedRevision","")},{"failure",snapshot.value("loadFailure",json(nullptr))}});
    snapshot_=std::move(snapshot);files_=std::move(files);UpdatePickSnapshot();}
  changed_.notify_all();
}
std::vector<std::shared_ptr<AgentAction>> AgentBridge::Drain(){
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<std::shared_ptr<AgentAction>> actions(pending_.begin(),pending_.end());
  pending_.clear();return actions;
}
void AgentBridge::Close(){
  {std::lock_guard<std::mutex> lock(mutex_);if(closed_)return;closed_=true;picks_.Close();UpdatePickSnapshot();
    for(auto& action:pending_)action->result.set_value(Error(action->request.value("command",""),"cancelled","Viewer closed",{},session_));
    pending_.clear();}
  changed_.notify_all();
}
json AgentBridge::Handle(const json& request){
  if(!request.is_object() || !request.contains("command") || !request["command"].is_string())
    return Error("","invalid_argument","Expected a command string",{},session_);
  const auto command=request.value("command","");
  if((request.contains("arguments")&&!request["arguments"].is_object()) ||
     (request.contains("expectRevision")&&!request["expectRevision"].is_string()) ||
     (request.contains("timeoutMs")&&(!request["timeoutMs"].is_number_integer() || request["timeoutMs"]<0 || request["timeoutMs"]>(command=="events"?301000:300000))))
    return Error(command,"invalid_argument","Expected object arguments, string revision guard and bounded integer timeout",{},session_);
  const auto args=request.value("arguments",json::object());
  if(command=="export") {
    try {
      if(!args.contains("path")||!args["path"].is_string()||!args.contains("format")||!args["format"].is_string())
        return Error(command,"invalid_argument","Expected export path and format strings",{},session_);
      const auto path=args["path"].get<std::string>(),format=args["format"].get<std::string>();
      if(path.empty()||path.size()>4096||path.find('\0')!=std::string::npos||(format!="3mf"&&format!="stl"))
        return Error(command,"invalid_argument","Expected nonempty NUL-free path and 3mf or stl format",{},session_);
      for(auto it=args.begin();it!=args.end();++it) {
        if(it.key()=="path"||it.key()=="format")continue;
        if((it.key()!="visibleOnly"&&it.key()!="replace"&&it.key()!="allowWarnings"&&it.key()!="dryRun")||!it.value().is_boolean())
          return Error(command,"invalid_argument","Export options must be known boolean fields",{},session_);
      }
    }catch(const std::exception& error){return Error(command,"invalid_argument",error.what(),{},session_);}
  }
  if(command=="export-history"&&!args.empty())return Error(command,"invalid_argument","export-history takes no arguments",{},session_);
  const auto timeout=std::clamp(request.value("timeoutMs",10000),1,command=="events"?301000:300000);
  const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(timeout);
  std::unique_lock<std::mutex> lock(mutex_);
  auto failure=[&](const std::string& code,const std::string& message){
    return Error(command,code,message,snapshot_,session_,snapshot_.value("displayedRevision",""));};
  if((command=="events"||command=="pick-status"||command=="pick-cancel")&&!request.value("expectRevision","").empty())
    return failure("invalid_argument","Revision guards are not supported for pick outcomes or events");
  if(command=="events") {
    try {
      if(!args.is_object()||!args.contains("after")||!args["after"].is_string()||
         (args.contains("waitMs")&&!args["waitMs"].is_number_integer()))throw GuidedPickError("invalid_argument","Expected after cursor and integer waitMs");
      const auto after=args["after"].get<std::string>();
      if(args.contains("waitMs")&&
         (args["waitMs"].is_number_unsigned()?args["waitMs"].get<uint64_t>()>300000:
          (args["waitMs"].get<int64_t>()<0||args["waitMs"].get<int64_t>()>300000)))
        throw GuidedPickError("invalid_argument","waitMs must be between 0 and 300000");
      const auto wait=args.value("waitMs",0);
      const auto eventDeadline=std::min(deadline,std::chrono::steady_clock::now()+std::chrono::milliseconds(wait));
      for(;;) {
        auto data=picks_.Events(after);
        if(closed_)return Error(command,"cancelled","Viewer closed",data,session_,snapshot_.value("displayedRevision",""));
        if(!data["events"].empty()||wait==0)return Success(command,data,session_,snapshot_.value("displayedRevision",""));
        if(std::chrono::steady_clock::now()>=eventDeadline)return Error(command,"timeout","Timed out waiting for events",data,session_,snapshot_.value("displayedRevision",""));
        changed_.wait_until(lock,eventDeadline);
      }
    }catch(const GuidedPickError& error){return failure(error.code,error.what());}
  }
  if(closed_)return failure("cancelled","Viewer closed");
  if(command=="pick") {
    const auto pickGuard=request.value("expectRevision","");
    if(!pickGuard.empty()&&(snapshot_.value("status","")!="ready"||pickGuard!=snapshot_.value("displayedRevision","")||!MatchesDisk(files_)))
      return failure("stale_revision","The requested displayed revision is no longer current");
    try {
      if(auto existing=picks_.Replay(args))return Success(command,*existing,session_,existing->at("request").value("revision",""));
    }catch(const GuidedPickError& error){return failure(error.code,error.what());}
  }
  if(command=="pick-status"||command=="pick-cancel") {
    try {
      const auto id=PickId(args);
      const auto result=command=="pick-status"?picks_.Status(id):picks_.Cancel(id);
      UpdatePickSnapshot();changed_.notify_all();
      return Success(command,{{"request",result},{"eventCursor",picks_.Cursor()}},session_,snapshot_.value("displayedRevision",""));
    }catch(const GuidedPickError& error){return failure(error.code,error.what());}
  }
  const auto guard=request.value("expectRevision","");
  if(!guard.empty()&&(snapshot_.value("status","")!="ready"||guard!=snapshot_.value("displayedRevision","")||!MatchesDisk(files_)))
    return failure("stale_revision","The requested displayed revision is no longer current");
  if(command=="export-history"){
    auto history=snapshot_.value("exportHistory",json{{"records",json::array()},{"diagnostics",json::array()}});
    if(!history.is_object())history={{"records",json::array()},{"diagnostics",json::array()}};
    return Success(command,history,session_,snapshot_.value("displayedRevision",""));
  }
  if(command=="checks"){
    auto report=snapshot_.value("manufacturing",json{{"current",false},{"checks",json::array()}});
    if(!report.is_object())report={{"current",false},{"checks",json::array()}};
    if(snapshot_.value("status","")!="ready"||!MatchesDisk(files_)){
      report["current"]=false;report["diagnostic"]="Source changed or loading failed; retained checks are not current.";
    }
    return Success(command,report,session_,snapshot_.value("displayedRevision",""));
  }
  if(command=="overview"||command=="profile"){
    auto overview=snapshot_.value("overview",json(nullptr));
    if(command=="overview"&&overview.is_object()&&overview.contains("generatedChecksCurrent")&&
       (snapshot_.value("status","")!="ready"||!MatchesDisk(files_)))overview["generatedChecksCurrent"]=false;
    return Success(command,command=="profile"&&overview.is_object()?overview.value("profile",json(nullptr)):overview,session_,snapshot_.value("displayedRevision",""));
  }
  if(command=="state"||command=="snapshot"||command=="selection"){
    auto exposed=command=="selection"?json{{"selection",snapshot_.value("selection",json(nullptr))}}:snapshot_;
    if(command!="selection"&&(snapshot_.value("status","")!="ready"||!MatchesDisk(files_))){
      if(exposed.contains("manufacturing")&&exposed["manufacturing"].is_object()){
        exposed["manufacturing"]["current"]=false;
        exposed["manufacturing"]["diagnostic"]="Source changed or loading failed; retained checks are not current.";
      }
      if(exposed.contains("overview")&&exposed["overview"].is_object()&&exposed["overview"].contains("generatedChecksCurrent"))
        exposed["overview"]["generatedChecksCurrent"]=false;
    }
    return Success(command,exposed,session_,snapshot_.value("displayedRevision",""));
  }
  if(command=="revision"){
    std::vector<std::filesystem::path> paths;
    for(const auto& file:files_)paths.push_back(std::filesystem::u8path(file.first));
    if(paths.empty())return failure("busy","The scene dependency list is not available yet");
    auto current=CaptureFiles(paths);
    const auto token=Sha256(snapshot_.value("view","")+":"+Revision(current));
    if(expected_.size()>=256)expected_.erase(expected_.begin());
    expected_[token]={current,snapshot_.value("view","")};
    return Success(command,{{"revision",token},{"files",current},{"view",snapshot_.value("view","")}},session_,token);
  }
  if(command=="wait"){
    const auto token=args.value("revision","");
    auto it=expected_.find(token);
    if(it==expected_.end()){
      if(token.empty()||token!=snapshot_.value("displayedRevision",""))
        return failure("stale_revision","Unknown revision token; request revision after editing the files");
      expected_[token]={files_,snapshot_.value("view","")};it=expected_.find(token);
    }
    const auto expected=it->second;
    for(;;){
      if(closed_)return failure("cancelled","Viewer closed");
      if(snapshot_.value("view","")!=expected.view)return failure("superseded","The active view has changed");
      if(!MatchesDisk(expected.files))return failure("superseded","Files changed after the requested revision was captured");
      const auto status=snapshot_.value("status","");
      if(status=="ready"&&MatchesDisk(files_))
        return Success(command,{{"status","ready"},{"requestedRevision",token},{"displayedRevision",snapshot_.value("displayedRevision","")}},session_,snapshot_.value("displayedRevision",""));
      if(status=="failed"&&MatchesDisk(files_))return failure("load_failed",snapshot_.value("diagnostic","Scene load failed"));
      if(std::chrono::steady_clock::now()>=deadline)return failure("timeout","Timed out waiting for the requested revision");
      changed_.wait_for(lock,std::chrono::milliseconds(25));
    }
  }
  auto action=std::make_shared<AgentAction>();action->request=request;action->deadline=deadline;
  auto future=action->result.get_future();pending_.push_back(action);lock.unlock();
  if(future.wait_until(deadline)!=std::future_status::ready)return Error(command,"timeout","Viewer did not process the request before its deadline",{},session_);
  return future.get();
}
}
