#include "model_worker.h"
#include "agent_entry.h"
#include <fstream>
#include <iostream>
#include <thread>
#include <cmath>
using namespace synthcad;
namespace fs=std::filesystem;
using json=nlohmann::json;
void Require(bool b,const char* message){if(!b)throw std::runtime_error(message);}
void Write(const fs::path& p,const std::string& s){std::ofstream(p)<<s;}
ModelResult Wait(ModelWorker& w){for(int i=0;i<1000;++i){if(auto r=w.Poll())return std::move(*r);std::this_thread::sleep_for(std::chrono::milliseconds(10));}throw std::runtime_error("Worker test timeout");}
int main(int argc,char** argv){
  auto args=ProcessArguments(argc,argv);
  if(args.size()==3&&args[1]=="--model-worker"){
    auto folder=fs::u8path(args[2]);std::ifstream input(folder/"request.cbor",std::ios::binary);std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)),{});
    auto packet=json::from_cbor(bytes);auto scene=fs::u8path(packet.at("scene").get<std::string>()).stem().u8string();
    if(scene=="crash")std::_Exit(73);
    if(scene=="hang"){std::this_thread::sleep_for(std::chrono::seconds(30));return 0;}
    if(scene=="malformed"){Write(folder/"result.cbor","invalid");return 0;}
    return RunModelWorker(folder);
  }
  auto root=fs::temp_directory_path()/fs::u8path("synthcad-worker-test-città-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  fs::create_directory(root);
  try{
    ModelWorker w(fs::absolute(fs::u8path(args[0])).u8string());auto scene=root/"design.js";
    Write(scene,"export const scene=cube({size:[20,30,4]});");w.Start(scene,"",std::nullopt);
    auto valid=Wait(w);Require(valid.success,valid.message.c_str());Require(valid.displayMeshes.size()==1,"mesh missing");Require(std::abs(valid.manifold->Volume()-2400)<1e-6,"geometry transfer changed volume");
    auto packet=EncodeModel(valid);auto decoded=DecodeModel(packet);Require(decoded.manifold->NumTri()==valid.manifold->NumTri(),"topology transfer changed");
    packet["meshes"][0]["triangles"][0]=99999999;bool rejected=false;try{DecodeModel(packet);}catch(...){rejected=true;}Require(rejected,"bad indices accepted");
    packet=EncodeModel(valid);packet["failure"]="invalid";rejected=false;try{DecodeModel(packet);}catch(...){rejected=true;}Require(rejected,"invalid diagnostic accepted");
    auto source=std::make_shared<manifold::Manifold>(manifold::Manifold::Cube({3,4,5}));
    valid.appearance.specified=true;valid.appearance.parts.clear();valid.displayMeshes.clear();
    for(int i=0;i<2;++i){dingcad::DisplayPart part;part.id=std::to_string(i);part.solid=source;part.sourceSolid=source;valid.appearance.parts.push_back(part);valid.displayMeshes.push_back(dingcad::DisplayMesh({part}));}
    decoded=DecodeModel(EncodeModel(valid));Require(decoded.appearance.parts[0].sourceSolid==decoded.appearance.parts[1].sourceSolid,"shared source copied");
    for(const auto* name:{"crash","malformed","hang"}){
      auto path=root/(std::string(name)+".js");Write(path,"// fixture");w.Start(path,"",std::nullopt,120);
      auto bad=Wait(w);Require(!bad.success,"worker failure accepted");const std::string expected=std::string(name)=="crash"?"worker_crash":std::string(name)=="hang"?"evaluation_timeout":"invalid_worker_result";
      Require(bad.failure.at("category")==expected,"incorrect worker category");
    }
    w.Start(root/"hang.js","",std::nullopt);auto cancelled=w.Cancel();Require(cancelled.failure["category"]=="cancelled"&&!w.Running(),"cancel failed");
    Write(scene,"import './missing.js'; export const scene=cube({size:[1,1,1]});");w.Start(scene,"",std::nullopt);auto missing=Wait(w);Require(!missing.success&&missing.files.count(CanonicalPath(root/"missing.js")),"failed dependency lost");
    Write(root/"missing.js","throw Error('Misura più grande');");w.Start(scene,"",std::nullopt);auto error=Wait(w);Require(!error.success&&error.message.find("Misura")!=std::string::npos,"JS error lost");
    Write(root/"missing.js","export const value=1;");w.Start(scene,"",std::nullopt);auto recovered=Wait(w);Require(recovered.success,recovered.message.c_str());
    for(const auto& sdf:{std::string("throw Error('callback failed');"),std::string("return NaN;")}){
      Write(scene,"export const scene=levelSet({sdf:p=>{"+sdf+"},bounds:{min:[-5,-5,-5],max:[5,5,5]},edgeLength:2});");
      w.Start(scene,"",std::nullopt);auto r=Wait(w);Require(!r.success&&r.failure["category"]=="model_error","SDF callback must fail gracefully");
    }
    Write(scene,"export const scene=levelSet({sdf:p=>3-Math.hypot(...p),bounds:{min:[-5,-5,-5],max:[5,5,5]},edgeLength:1});");w.Start(scene,"",std::nullopt);auto organic=Wait(w);Require(organic.success,organic.message.c_str());Require(organic.manifold->Volume()>0,"implicit surface empty");
    fs::remove_all(root);std::cout<<"PASS worker isolation, timeout, cancellation, transfer, dependencies, callbacks and recovery\n";return 0;
  }catch(const std::exception& e){std::cerr<<e.what()<<"\nEvidence: "<<root.u8string()<<'\n';return 1;}
}
