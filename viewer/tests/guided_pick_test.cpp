#include "guided_pick.h"
#include <iostream>
#include <stdexcept>
using namespace synthcad;
using json=nlohmann::json;
namespace {
void Check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
template<class F> void ErrorCode(F f,const char* code){try{f();}catch(const GuidedPickError& e){Check(e.code==code,"wrong error code");return;}throw std::runtime_error("expected error");}
json Args(const std::string& id,const std::string& kind="surface"){return {{"id",id},{"kind",kind},{"question","Choose a face\nThen confirm"}};}
json Geometry(const std::string& kind="planar-face"){return {{"kind",kind},{"revision","r1"},{"reference","opaque-token"},{"partId","a"},{"sourcePartId",nullptr},{"instanceId",nullptr}};}
}
int main(){try{
  GuidedPickState state("epoch");
  Check(state.Events("0")["events"].empty(),"initial events");
  auto receipt=state.Begin(Args("a"),"r1");
  Check(receipt["created"]==true&&state.Active()["status"]=="pending","begin");
  Check(state.Begin(Args("a"),"r1")["created"]==false,"pending replay");
  ErrorCode([&]{state.Begin(Args("b"),"r1");},"busy");
  ErrorCode([&]{state.Begin(Args("a","edge"),"r1");},"invalid_argument");
  Check(!state.Confirm("a",Geometry("edge"),"r1"),"wrong kind ignored");
  auto missing=Geometry();missing["reference"]=nullptr;
  Check(!state.Confirm("a",missing,"r1"),"missing token ignored");
  missing=Geometry();missing.erase("partId");Check(!state.Confirm("a",missing,"r1"),"missing owner ignored");
  missing=Geometry();missing["revision"]="r2";Check(!state.Confirm("a",missing,"r1"),"stale geometry ignored");
  missing=Geometry();missing["sourcePartId"]="source";missing["instanceId"]="other";Check(!state.Confirm("a",missing,"r1"),"cross owner ignored");
  Check(state.Confirm("a",Geometry(),"r1"),"confirm");
  Check(state.Active().is_null()&&state.Status("a")["selection"]["kind"]=="planar-face","confirmed result");
  Check(state.Begin(Args("a"),"r2")["request"]["status"]=="confirmed","confirmed replay after revision change");
  Check(state.Cancel("a")["status"]=="confirmed","cancel cannot overwrite terminal");
  state.Begin(Args("b","part"),"r1");state.Cancel("b");
  Check(state.Begin(Args("b","part"),"r2")["request"]["status"]=="cancelled","cancelled replay");
  state.Begin(Args("c","edge"),"r1");state.Invalidate("revision_changed");
  Check(state.Begin(Args("c","edge"),"r2")["request"]["reason"]=="revision_changed","invalidated replay");
  ErrorCode([&]{state.Events("wrong:0");},"stale_cursor");
  ErrorCode([&]{state.Events("epoch:99999");},"stale_cursor");
  ErrorCode([&]{state.Events("epoch:01");},"stale_cursor");
  ErrorCode([&]{state.Begin(Args(""),"r1");},"invalid_argument");
  auto bad=Args("bad");bad["question"]=std::string(4097,'x');ErrorCode([&]{state.Begin(bad,"r1");},"invalid_argument");
  bad["question"]=std::string("a\0b",3);ErrorCode([&]{state.Begin(bad,"r1");},"invalid_argument");
  bad["question"]=std::string(1,char(0xff));ErrorCode([&]{state.Begin(bad,"r1");},"invalid_argument");
  const auto before=state.Cursor();
  state.Begin(Args("d","vertex"),"r1");state.Close();state.Close();
  auto closing=state.Events(before)["events"];
  Check(closing.size()==3&&closing[1]["reason"]=="viewer_closed"&&closing[2]["type"]=="viewer-closed","close events exactly once");
  GuidedPickState full("full");
  for(int i=0;i<1024;++i){const auto id=std::to_string(i);full.Begin(Args(id),"r1");full.Cancel(id);}
  ErrorCode([&]{full.Begin(Args("overflow"),"r1");},"busy");
  Check(full.Begin(Args("0"),"r2")["request"]["status"]=="cancelled","full state retains oldest receipt");
  Check(full.Events("0")["events"].size()==256&&full.Events("0")["truncated"]==true,"startup cursor starts retained history explicitly");
  ErrorCode([&]{full.Events("full:1");},"stale_cursor");
  Check(full.Events("full:1792")["events"].size()==256,"oldest retained predecessor accepted");
  Check(full.Events(full.Cursor())["events"].empty(),"caught up cursor");
  std::cout<<"Guided pick tests passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
