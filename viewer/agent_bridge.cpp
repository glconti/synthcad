#include "agent_bridge.h"
#include "agent_cli.h"
#include <algorithm>

namespace synthcad {
using json=nlohmann::json;
AgentBridge::AgentBridge(std::string session):session_(std::move(session)) {
  snapshot_={{"status","loading"},{"displayedRevision",""},{"attemptedRevision",""},
             {"exportValid",false},{"view",""},{"parts",json::array()},
             {"selection",nullptr},{"diagnostic",""}};
}
void AgentBridge::Publish(json snapshot,FileSnapshot files){
  {std::lock_guard<std::mutex> lock(mutex_);snapshot_=std::move(snapshot);files_=std::move(files);}
  changed_.notify_all();
}
std::vector<std::shared_ptr<AgentAction>> AgentBridge::Drain(){
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<std::shared_ptr<AgentAction>> actions(pending_.begin(),pending_.end());
  pending_.clear();return actions;
}
void AgentBridge::Close(){
  {std::lock_guard<std::mutex> lock(mutex_);closed_=true;
    for(auto& action:pending_)action->result.set_value(Error(action->request.value("command",""),"cancelled","Viewer closed",{},session_));
    pending_.clear();}
  changed_.notify_all();
}
json AgentBridge::Handle(const json& request){
  const auto command=request.value("command","");
  const auto args=request.value("arguments",json::object());
  const auto timeout=std::clamp(request.value("timeoutMs",10000),1,300000);
  const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(timeout);
  std::unique_lock<std::mutex> lock(mutex_);
  auto failure=[&](const std::string& code,const std::string& message){
    return Error(command,code,message,snapshot_,session_,snapshot_.value("displayedRevision",""));};
  if(closed_)return failure("cancelled","Viewer closed");
  const auto guard=request.value("expectRevision","");
  if(!guard.empty()&&(snapshot_.value("status","")!="ready"||guard!=snapshot_.value("displayedRevision","")||!MatchesDisk(files_)))
    return failure("stale_revision","The requested displayed revision is no longer current");
  if(command=="state"||command=="snapshot"||command=="selection"){
    return Success(command,command=="selection"?json{{"selection",snapshot_.value("selection",json(nullptr))}}:snapshot_,session_,snapshot_.value("displayedRevision",""));
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
