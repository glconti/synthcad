#include "model_worker.h"
#include "js_bindings.h"
#include "design_graph.h"
#include "manufacturing_checks.h"
#include "printer_profile.h"
#include <fstream>
#include <set>
#include <chrono>
#include <thread>
#include <random>
#include <cmath>
#include <iostream>
#include <stdexcept>
#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#define NOUSER
#include <windows.h>
#else
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <sys/prctl.h>
#include <spawn.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
extern char **environ;
#endif
namespace synthcad {
namespace fs=std::filesystem;
using json=nlohmann::json;
namespace {
struct ModuleLoaderData { fs::path baseDir; std::set<fs::path> dependencies; FileSnapshot files; };
ModuleLoaderData g_module_loader_data;
fs::path progressDirectory;
json workerProgress;
void WritePacket(const fs::path& path,const json& value) {
  const auto bytes=json::to_cbor(value);
  auto temporary=path;temporary+=".tmp";
  {std::ofstream out(temporary,std::ios::binary|std::ios::trunc);out.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());if(!out)throw std::runtime_error("Cannot write worker packet");}
#ifdef _WIN32
  for(int attempt=0;;++attempt){
    if(MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING))break;
    const auto error=GetLastError();
    if(attempt>=50||(error!=ERROR_ACCESS_DENIED&&error!=ERROR_SHARING_VIOLATION))throw std::runtime_error("Cannot publish worker packet (Windows error "+std::to_string(error)+"): "+path.u8string());
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
#else
  fs::rename(temporary,path);
#endif
}
json ReadPacket(const fs::path& path) {
  std::vector<uint8_t> bytes;
#ifdef _WIN32
  HANDLE handle=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
  if(handle==INVALID_HANDLE_VALUE)throw std::runtime_error("Worker packet not available");
  LARGE_INTEGER size{};
  if(!GetFileSizeEx(handle,&size)||size.QuadPart<0||size.QuadPart>512ll*1024*1024){CloseHandle(handle);throw std::runtime_error("Invalid worker packet size");}
  bytes.resize(static_cast<size_t>(size.QuadPart));DWORD read=0;
  const bool ok=bytes.empty()||ReadFile(handle,bytes.data(),static_cast<DWORD>(bytes.size()),&read,nullptr);
  CloseHandle(handle);if(!ok||read!=bytes.size())throw std::runtime_error("Incomplete worker packet");
#else
  if(fs::file_size(path)>512ull*1024*1024)throw std::runtime_error("Worker packet exceeds 512 MiB limit");
  std::ifstream in(path,std::ios::binary);bytes.assign(std::istreambuf_iterator<char>(in),{});
#endif
  return json::from_cbor(bytes);
}
void Progress(const std::string& stage,const std::string& operation="") {
  if(progressDirectory.empty())return;
  workerProgress["stage"]=stage;workerProgress["operation"]=operation;
  workerProgress["files"]=g_module_loader_data.files;
  WritePacket(progressDirectory/"progress.cbor",workerProgress);
}
std::optional<std::string> ReadTextFile(const std::filesystem::path &path) {
  auto text=synthcad::ReadTrackedFile(path,g_module_loader_data.files);
  workerProgress["sourcePath"]=path.u8string();Progress("read");return text;
}

char* FilesystemModuleNormalize(JSContext* ctx,const char* base,const char* name,void*){
  try {
    auto path=std::filesystem::u8path(name);
    if(path.is_relative())path=std::filesystem::u8path(base).parent_path()/path;
    const auto normalized=synthcad::CanonicalPath(path);
    return js_strdup(ctx,normalized.c_str());
  }catch(const std::exception& error){JS_ThrowReferenceError(ctx,"%s",error.what());return nullptr;}
}

JSModuleDef *FilesystemModuleLoader(JSContext *ctx, const char *module_name, void *opaque) {
  auto *data = static_cast<ModuleLoaderData *>(opaque);
  std::filesystem::path resolved=std::filesystem::u8path(module_name);
  if (resolved.is_relative()) {
    const std::filesystem::path base = data && !data->baseDir.empty()
                                           ? data->baseDir
                                           : std::filesystem::current_path();
    resolved = base / resolved;
  }
  resolved = std::filesystem::absolute(resolved).lexically_normal();

  if (data) {
    data->dependencies.insert(resolved);
  }

  auto source = ReadTextFile(resolved);
  if (!source) {
    JS_ThrowReferenceError(ctx, "Unable to load module '%s'", resolved.u8string().c_str());
    return nullptr;
  }

  const std::string moduleName = resolved.u8string();
  JSValue funcVal = JS_Eval(ctx, source->c_str(), source->size(), moduleName.c_str(),
                            JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
  if (JS_IsException(funcVal)) {
    return nullptr;
  }

  auto *module = static_cast<JSModuleDef *>(JS_VALUE_GET_PTR(funcVal));
  JS_FreeValue(ctx, funcVal);
  return module;
}

double ElapsedMilliseconds(std::chrono::steady_clock::time_point start) {
  return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
}

ModelResult LoadSceneFromFile(JSRuntime *runtime, const std::filesystem::path &path,
                             const std::string& requestedView = "") {
  ModelResult result;
  const auto loadStarted=std::chrono::steady_clock::now();
  const auto absolutePath = std::filesystem::absolute(path);
  g_module_loader_data.baseDir = absolutePath.parent_path();
  g_module_loader_data.dependencies.clear();
  g_module_loader_data.files.clear();
  g_module_loader_data.dependencies.insert(absolutePath);
  auto sourceOpt = ReadTextFile(absolutePath);
  if (!sourceOpt) {
    result.message = "Unable to read scene file: " + absolutePath.u8string();
    result.dependencies.assign(g_module_loader_data.dependencies.begin(),
                               g_module_loader_data.dependencies.end());
    result.files=g_module_loader_data.files;
    return result;
  }
  JSContext *ctx = JS_NewContext(runtime);
  RegisterBindings(ctx,ReadTextFile);

  auto captureException = [&]() {
    JSValue exc = JS_GetException(ctx);
    JSValue stack = JS_GetPropertyStr(ctx, exc, "stack");
    const char *exceptionStr=JS_ToCString(ctx,exc);
    const char *stackStr=JS_IsUndefined(stack)?nullptr:JS_ToCString(ctx,stack);
    result.message=exceptionStr?exceptionStr:"JavaScript error";
    if(stackStr)result.message+=std::string("\n")+stackStr;
    JS_FreeCString(ctx,exceptionStr);
    JS_FreeCString(ctx, stackStr);
    JS_FreeValue(ctx, stack);
    JS_FreeValue(ctx, exc);
  };
  auto assignDependencies = [&]() {
    result.dependencies.assign(g_module_loader_data.dependencies.begin(),
                               g_module_loader_data.dependencies.end());
    result.files=g_module_loader_data.files;
  };

  Progress("compile");
  JSValue moduleFunc = JS_Eval(ctx, sourceOpt->c_str(), sourceOpt->size(), absolutePath.u8string().c_str(),
                               JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
  if (JS_IsException(moduleFunc)) {
    captureException();
    assignDependencies();
    JS_FreeContext(ctx);
    return result;
  }

  if (JS_ResolveModule(ctx, moduleFunc) < 0) {
    captureException();
    JS_FreeValue(ctx, moduleFunc);
    assignDependencies();
    JS_FreeContext(ctx);
    return result;
  }

  auto *module = static_cast<JSModuleDef *>(JS_VALUE_GET_PTR(moduleFunc));
  const auto evaluationStarted=std::chrono::steady_clock::now();
  Progress("evaluate");
  JSValue evalResult = JS_EvalFunction(ctx, moduleFunc);
  result.evaluationMilliseconds=ElapsedMilliseconds(evaluationStarted);
  if (JS_IsException(evalResult)) {
    captureException();
    assignDependencies();
    JS_FreeContext(ctx);
    return result;
  }
  // QuickJS module evaluation returns a promise, including synchronous throws.
  // Preserve its rejection before touching uninitialized namespace exports.
  if(JS_PromiseState(ctx,evalResult)==JS_PROMISE_REJECTED){
    JS_Throw(ctx,JS_PromiseResult(ctx,evalResult));
    captureException();assignDependencies();
    JS_FreeValue(ctx,evalResult);JS_FreeContext(ctx);return result;
  }
  JS_FreeValue(ctx, evalResult);

  JSValue moduleNamespace = JS_GetModuleNamespace(ctx, module);
  if (JS_IsException(moduleNamespace)) {
    captureException();
    assignDependencies();
    JS_FreeContext(ctx);
    return result;
  }

  Progress("validate");
  auto design = dingcad::ReadDesignGraph(ctx, moduleNamespace, requestedView);
  if (!design.diagnostic.empty()) {
    result.message = design.diagnostic;
    JS_FreeValue(ctx, moduleNamespace);assignDependencies();JS_FreeContext(ctx);
    return result;
  }
  JSValue sceneVal = design.specified ? JS_UNDEFINED : JS_GetPropertyStr(ctx, moduleNamespace, "scene");
  if (JS_IsException(sceneVal)) {
    JS_FreeValue(ctx, moduleNamespace);
    captureException();
    assignDependencies();
    JS_FreeContext(ctx);
    return result;
  }
  auto annotations = dingcad::ReadDimensions(ctx, moduleNamespace);
  result.appearance = design.specified ? std::move(design.appearance) : dingcad::ReadAppearance(ctx, moduleNamespace);
  if (design.specified) result.design = std::move(design.metadata);
  JS_FreeValue(ctx, moduleNamespace);

  if(!result.appearance.diagnostic.empty()){
    result.message=result.appearance.diagnostic;
    JS_FreeValue(ctx,sceneVal);JS_FreeContext(ctx);assignDependencies();return result;
  }

  if (!design.specified && JS_IsUndefined(sceneVal)) {
    JS_FreeValue(ctx, sceneVal);
    JS_FreeContext(ctx);
    result.message = "Scene module must export 'scene'";
    assignDependencies();
    return result;
  }

  auto sceneHandle = design.specified ? design.scene : GetManifoldHandle(ctx, sceneVal);
  if (!sceneHandle) {
    JS_FreeValue(ctx, sceneVal);
    JS_FreeContext(ctx);
    result.message = "Exported 'scene' is not a manifold";
    assignDependencies();
    return result;
  }

  result.manifold = sceneHandle;
  result.success = true;
  result.message = "Loaded " + absolutePath.u8string();
  result.dimensions = std::move(annotations.entries);
  for (const auto &diagnostic : annotations.diagnostics) {
    TraceLog(LOG_WARNING, "%s", diagnostic.c_str());
  }
  if (!annotations.diagnostics.empty()) {
    result.message += " (" + std::to_string(annotations.diagnostics.size()) + " dimension warning(s))";
  }
  assignDependencies();
  JS_FreeValue(ctx, sceneVal);
  JS_FreeContext(ctx);
  result.loadMilliseconds=ElapsedMilliseconds(loadStarted);
  return result;
}


} // namespace

namespace {
template<class M> json MeshPacket(const M& m){return {{"numProp",m.numProp},{"vertices",m.vertProperties},{"triangles",m.triVerts},{"mergeFrom",m.mergeFromVert},{"mergeTo",m.mergeToVert},{"runIndex",m.runIndex},{"runOriginalID",m.runOriginalID},{"runTransform",m.runTransform},{"faceID",m.faceID},{"tangents",m.halfedgeTangent},{"tolerance",m.tolerance}};}
template<class M> M ReadMesh(const json& j){
  M m;
  j.at("numProp").get_to(m.numProp);j.at("vertices").get_to(m.vertProperties);j.at("triangles").get_to(m.triVerts);
  j.at("mergeFrom").get_to(m.mergeFromVert);j.at("mergeTo").get_to(m.mergeToVert);j.at("runIndex").get_to(m.runIndex);
  j.at("runOriginalID").get_to(m.runOriginalID);j.at("runTransform").get_to(m.runTransform);j.at("faceID").get_to(m.faceID);
  j.at("tangents").get_to(m.halfedgeTangent);j.at("tolerance").get_to(m.tolerance);
  auto require=[](bool b){if(!b)throw std::runtime_error("Invalid worker mesh buffers");};
  require(m.numProp>=3&&m.numProp<=1024&&m.vertProperties.size()%m.numProp==0&&m.triVerts.size()%3==0);
  for(auto v:m.vertProperties)require(std::isfinite(v));
  for(auto v:m.runTransform)require(std::isfinite(v));
  for(auto v:m.halfedgeTangent)require(std::isfinite(v));
  require(std::isfinite(m.tolerance)&&m.tolerance>=0);
  for(auto i:m.triVerts)require(i<m.NumVert());
  require(m.mergeFromVert.size()==m.mergeToVert.size());
  for(auto i:m.mergeFromVert)require(i<m.NumVert());for(auto i:m.mergeToVert)require(i<m.NumVert());
  require(m.faceID.empty()||m.faceID.size()==m.NumTri());
  require(m.halfedgeTangent.empty()||m.halfedgeTangent.size()==m.triVerts.size()*4);
  require(m.runTransform.empty()||m.runTransform.size()==m.runOriginalID.size()*12);
  require(m.runIndex.empty()?m.runOriginalID.empty():m.runIndex.size()==m.runOriginalID.size()+1);
  if(!m.runIndex.empty()){
    require(m.runIndex.front()==0&&m.runIndex.back()==m.triVerts.size());
    for(size_t i=0;i<m.runIndex.size();++i)require(m.runIndex[i]%3==0&&(i==0||m.runIndex[i]>=m.runIndex[i-1]));
  }
  return m;
}
json V(Vector3 v){return {v.x,v.y,v.z};}
Vector3 Vec(const json& j){auto a=j.get<std::array<float,3>>();for(auto f:a)if(!std::isfinite(f))throw std::runtime_error("Invalid annotation coordinate");return {a[0],a[1],a[2]};}
}
std::vector<dingcad::DisplayPart> ModelParts(const ModelResult& r){
  if(r.appearance.specified)return r.appearance.parts;
  return {{r.manifold,{210,210,220,255},"@scene","Scene",{},true}};
}
json EncodeModel(const ModelResult& r){
  json j={{"version",1},{"success",r.success},{"message",r.message},{"files",r.files},{"failure",r.failure}};
  if(!r.success)return j;
  j["loadMilliseconds"]=r.loadMilliseconds;j["evaluationMilliseconds"]=r.evaluationMilliseconds;
  j["design"]=r.design;j["checks"]=r.checks;j["specified"]=r.appearance.specified;
  j["solids"]=json::array();std::map<const manifold::Manifold*,size_t> ids;
  auto solid=[&](const std::shared_ptr<manifold::Manifold>& m){
    if(!m)return json(nullptr);
    auto it=ids.find(m.get());if(it!=ids.end())return json(it->second);
    const auto id=ids.size();ids[m.get()]=id;j["solids"].push_back(MeshPacket(m->GetMeshGL64()));return json(id);
  };
  j["scene"]=solid(r.manifold);j["parts"]=json::array();
  for(const auto& p:r.appearance.parts){
    json groups=json::array();for(const auto& path:p.memberships){json g=json::array();for(const auto& a:path)g.push_back({{"id",a.id},{"name",a.name}});groups.push_back(g);}
    j["parts"].push_back({{"solid",solid(p.solid)},{"source",solid(p.sourceSolid)},{"color",{p.color.r,p.color.g,p.color.b,p.color.a}},
      {"id",p.id},{"name",p.name},{"group",p.group},{"exportable",p.exportable},{"sourcePartId",p.sourcePartId},
      {"rotation",p.rotation},{"translation",p.translation},{"memberships",groups}});
  }
  j["dimensions"]=json::array();for(const auto& d:r.dimensions)j["dimensions"].push_back({{"type",int(d.type)},{"label",d.label},{"value",d.value},{"start",V(d.start)},{"end",V(d.end)},{"marker",V(d.marker)}});
  j["meshes"]=json::array();for(const auto& m:r.displayMeshes)j["meshes"].push_back(MeshPacket(m));return j;
}
ModelResult DecodeModel(const json& j){
  if(j.at("version")!=1)throw std::runtime_error("Unsupported worker result version");
  ModelResult r;r.success=j.at("success");r.message=j.at("message");r.files=j.at("files").get<FileSnapshot>();r.failure=j.at("failure");
  if(!r.failure.is_null()&&!r.failure.is_object())throw std::runtime_error("Invalid worker diagnostic");
  if(!r.success)return r;
  r.loadMilliseconds=j.at("loadMilliseconds");r.evaluationMilliseconds=j.at("evaluationMilliseconds");
  r.design=j.at("design");r.checks=j.at("checks");r.appearance.specified=j.at("specified");
  if((!r.design.is_null()&&!r.design.is_object())||!r.checks.is_object())throw std::runtime_error("Invalid worker scene metadata");
  if(j.at("parts").size()>10000||j.at("solids").size()>20001)throw std::runtime_error("Worker scene too large");
  std::vector<std::shared_ptr<manifold::Manifold>> solids;
  for(const auto& m:j.at("solids")){
    auto mesh=ReadMesh<manifold::MeshGL64>(m);auto solid=std::make_shared<manifold::Manifold>(mesh);
    if(solid->Status()!=manifold::Manifold::Error::NoError)throw std::runtime_error("Invalid worker solid");
    solids.push_back(std::move(solid));
  }
  auto get=[&](const json& id){return id.is_null()?std::shared_ptr<manifold::Manifold>{}:solids.at(id.get<size_t>());};
  r.manifold=get(j.at("scene"));if(!r.manifold)throw std::runtime_error("Worker scene missing");
  for(const auto& p:j.at("parts")){
    dingcad::DisplayPart d;d.solid=get(p.at("solid"));d.sourceSolid=get(p.at("source"));
    if(!d.solid)throw std::runtime_error("Worker part solid missing");
    auto c=p.at("color").get<std::array<unsigned char,4>>();d.color={c[0],c[1],c[2],c[3]};
    d.id=p.at("id");d.name=p.at("name");d.group=p.at("group").get<std::vector<std::string>>();d.exportable=p.at("exportable");d.sourcePartId=p.at("sourcePartId");
    d.rotation=p.at("rotation").get<std::array<double,3>>();d.translation=p.at("translation").get<std::array<double,3>>();
    for(const auto& group:p.at("memberships")){std::vector<dingcad::GroupLabel> g;for(const auto& item:group)g.push_back({item.at("id"),item.at("name")});d.memberships.push_back(std::move(g));}
    r.appearance.parts.push_back(std::move(d));
  }
  for(const auto& d:j.at("dimensions")){
    const int type=d.at("type");if(type<0||type>2)throw std::runtime_error("Invalid dimension type");
    r.dimensions.push_back({dingcad::DimensionType(type),d.at("label"),d.at("value"),Vec(d.at("start")),Vec(d.at("end")),Vec(d.at("marker"))});
  }
  for(const auto& m:j.at("meshes"))r.displayMeshes.push_back(ReadMesh<manifold::MeshGL>(m));
  if(r.displayMeshes.size()!=ModelParts(r).size())throw std::runtime_error("Worker display mesh count mismatch");return r;
}
int RunModelWorker(const fs::path& directory){
#ifdef _WIN32
  SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
#else
  prctl(PR_SET_PDEATHSIG,SIGTERM);
  const rlimit coreLimit{0,0};setrlimit(RLIMIT_CORE,&coreLimit);
#endif
  SetTraceLogLevel(LOG_NONE);
  progressDirectory=directory;
  ModelResult result;JSRuntime* runtime=nullptr;bool runtimeHealthy=true;
  try{
    const auto request=ReadPacket(directory/"request.cbor");if(request.at("version")!=1)throw std::runtime_error("Invalid worker request");
    workerProgress={{"stage","read"},{"nonce",request.at("nonce")},{"files",json::object()}};
    runtime=JS_NewRuntime();if(!runtime)throw std::runtime_error("Cannot allocate model runtime");
    EnsureManifoldClass(runtime);JS_SetModuleLoaderFunc(runtime,FilesystemModuleNormalize,FilesystemModuleLoader,&g_module_loader_data);
    SetBindingProgress([](const char* op){Progress("evaluate",op);});
    result=LoadSceneFromFile(runtime,fs::u8path(request.at("scene").get<std::string>()),request.at("view"));
    if(result.success){
      Progress("mesh");const auto parts=ModelParts(result);
      for(const auto& p:parts){
        workerProgress["partId"]=p.id;Progress("mesh");
        if(p.solid->Status()!=manifold::Manifold::Error::NoError)throw std::runtime_error("Invalid geometry in part '"+p.id+"'");
        result.displayMeshes.push_back(dingcad::DisplayMesh({p}));
      }
      workerProgress.erase("partId");Progress("checks");
      const auto metadata=request.at("metadata"),profile=PrinterProfileContext(metadata);
      const std::string identity=result.design.is_object()?result.design.value("identity",""):"";
      auto files=result.files;for(const auto& f:request.at("projectFiles").items())files[f.key()]=f.value().get<std::string>();
      const auto view=request.at("basisView").get<std::string>();
      json basis={{"view",view},{"modelRevision",ModelRevision(result.files,view,identity)},{"sourceRevision",Revision(files)},{"profileRevision",profile.value("profileRevision",json(nullptr))}};
      result.checks=dingcad::ManufacturingChecks(parts,result.design,profile,metadata,basis);
      Progress("transfer");
    }
  }catch(const std::exception& e){runtimeHealthy=false;result.success=false;result.message=e.what();result.files=g_module_loader_data.files;}
  auto publicProgress=workerProgress;publicProgress.erase("nonce");
  if(!result.success)result.failure={{"category","model_error"},{"stage",workerProgress.value("stage","read")},{"message",result.message},{"details",result.message},{"context",publicProgress}};
  if(!result.success){const auto newline=result.message.find('\n');result.failure["stackTrace"]=newline==std::string::npos?json(nullptr):json(result.message.substr(newline+1));}
  try{
    auto packet=EncodeModel(result);packet["nonce"]=workerProgress.value("nonce","");
    WritePacket(directory/"result.cbor",packet);
  }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
  SetBindingProgress({});if(runtime&&runtimeHealthy)JS_FreeRuntime(runtime);return 0;
}

struct ModelWorker::Impl {
  std::string executable,nonce;fs::path directory;json progress;FileSnapshot initial;
  std::chrono::steady_clock::time_point deadline;bool running=false;
#ifdef _WIN32
  HANDLE process=nullptr,job=nullptr;
#else
  pid_t pid=-1;
#endif
  explicit Impl(std::string exe):executable(std::move(exe)){}
  void Stop(){
#ifdef _WIN32
    if(process){if(WaitForSingleObject(process,0)==WAIT_TIMEOUT){TerminateProcess(process,1);WaitForSingleObject(process,3000);}CloseHandle(process);process=nullptr;}
    if(job){CloseHandle(job);job=nullptr;}
#else
    if(pid>0){int status=0;if(waitpid(pid,&status,WNOHANG)==0){kill(pid,SIGKILL);while(waitpid(pid,&status,0)<0&&errno==EINTR){}}pid=-1;}
#endif
    running=false;
  }
  void Cleanup(){Stop();if(!directory.empty()){std::error_code ec;fs::remove_all(directory,ec);directory.clear();}}
  ModelResult Failure(const std::string& category,const std::string& message,json native=nullptr){
    ModelResult r;r.message=message;r.files=initial;
    if(progress.contains("files"))for(const auto& f:progress["files"].items())r.files[f.key()]=f.value().get<std::string>();
    auto context=progress;context.erase("nonce");
    r.failure={{"category",category},{"stage",progress.value("stage","start")},{"message",message},{"details",message},{"nativeExitCode",native},{"context",context}};
    return r;
  }
};
ModelWorker::ModelWorker(std::string executable):impl_(std::make_unique<Impl>(std::move(executable))){}
ModelWorker::~ModelWorker(){impl_->Cleanup();}
bool ModelWorker::Running() const{return impl_->running;}
void ModelWorker::Start(const fs::path& scene,const std::string& view,const std::optional<Project>& project,int timeoutMs){
  auto& p=*impl_;p.Cleanup();p.initial=CaptureFiles({scene});if(project)p.initial.insert(project->files.begin(),project->files.end());
  p.nonce=Sha256(std::to_string(std::random_device{}())+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  const auto directory=fs::temp_directory_path()/("synthcad-model-"+p.nonce);
  if(!fs::create_directory(directory))throw std::runtime_error("Cannot create model worker workspace");
  p.directory=directory;
#ifndef _WIN32
  if(chmod(p.directory.c_str(),0700))throw std::runtime_error("Cannot secure model worker workspace");
#endif
  json request={{"version",1},{"nonce",p.nonce},{"scene",CanonicalPath(scene)},{"view",view},
    {"basisView",view.empty()?"scene":view},{"metadata",project?project->metadata:json::object()},{"projectFiles",project?json(project->files):json::object()}};
  WritePacket(p.directory/"request.cbor",request);
  p.progress={{"stage","start"},{"files",p.initial},{"nonce",p.nonce}};
#ifdef _WIN32
  const auto exe=fs::u8path(p.executable).wstring();
  auto quote=[](const std::wstring& value){std::wstring out=L"\"";size_t slashes=0;for(auto c:value){if(c==L'\\'){++slashes;continue;}if(c==L'\"'){out.append(slashes*2+1,L'\\');out+=c;}else{out.append(slashes,L'\\');out+=c;}slashes=0;}out.append(slashes*2,L'\\');return out+L"\"";};
  auto command=quote(exe)+L" --model-worker "+quote(p.directory.wstring());
  STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
  if(!CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW|CREATE_SUSPENDED,nullptr,nullptr,&startup,&process))throw std::runtime_error("Cannot launch model worker (Windows error "+std::to_string(GetLastError())+")");
  p.process=process.hProcess;
  p.job=CreateJobObjectW(nullptr,nullptr);JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  if(!p.job||!SetInformationJobObject(p.job,JobObjectExtendedLimitInformation,&limits,sizeof(limits))||!AssignProcessToJobObject(p.job,p.process)){
    CloseHandle(process.hThread);p.Stop();throw std::runtime_error("Cannot supervise model worker lifetime");
  }
  const auto resumed=ResumeThread(process.hThread);CloseHandle(process.hThread);
  if(resumed==DWORD(-1)){p.Stop();throw std::runtime_error("Cannot resume model worker");}
#else
  std::string folder=p.directory.u8string();char* argv[]={p.executable.data(),const_cast<char*>("--model-worker"),folder.data(),nullptr};
  posix_spawn_file_actions_t actions;posix_spawn_file_actions_init(&actions);
  posix_spawn_file_actions_addopen(&actions,STDOUT_FILENO,"/dev/null",O_WRONLY,0);
  posix_spawn_file_actions_addopen(&actions,STDERR_FILENO,"/dev/null",O_WRONLY,0);
  const int error=posix_spawn(&p.pid,p.executable.c_str(),&actions,nullptr,argv,environ);posix_spawn_file_actions_destroy(&actions);
  if(error){p.pid=-1;throw std::runtime_error("Cannot launch model worker (errno "+std::to_string(error)+")");}
#endif
  p.deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(timeoutMs);p.running=true;
}
json ModelWorker::Progress(){
  auto& p=*impl_;if(!p.directory.empty())try{auto j=ReadPacket(p.directory/"progress.cbor");
    j.at("stage").get<std::string>();j.at("files").get<FileSnapshot>();
    for(const auto* key:{"operation","sourcePath","partId"})if(j.contains(key))j.at(key).get<std::string>();
    if(j.at("nonce")==p.nonce)p.progress=std::move(j);}catch(const std::exception&){}
  auto result=p.progress;result.erase("nonce");return result;
}
std::optional<ModelResult> ModelWorker::Poll(){
  auto& p=*impl_;if(!p.running)return std::nullopt;Progress();
  bool exited=false;uint64_t native=0;
#ifdef _WIN32
  if(WaitForSingleObject(p.process,0)==WAIT_OBJECT_0){DWORD code=0;GetExitCodeProcess(p.process,&code);native=code;exited=true;}
#else
  int status=0;const auto waited=waitpid(p.pid,&status,WNOHANG);
  if(waited==p.pid){native=WIFSIGNALED(status)?128+WTERMSIG(status):WEXITSTATUS(status);p.pid=-1;exited=true;}
#endif
  if(!exited){if(std::chrono::steady_clock::now()>=p.deadline)return Cancel("evaluation_timeout");return std::nullopt;}
  ModelResult result;
  if(native)result=p.Failure("worker_crash","The model processor stopped unexpectedly. The previous model is unchanged.",native);
  else try{auto packet=ReadPacket(p.directory/"result.cbor");if(packet.at("nonce")!=p.nonce)throw std::runtime_error("Worker result identity mismatch");result=DecodeModel(packet);}
    catch(const std::exception& e){result=p.Failure("invalid_worker_result",std::string("Could not prepare the model: ")+e.what());}
  p.Cleanup();return result;
}
ModelResult ModelWorker::Cancel(const std::string& category){
  Progress();auto result=impl_->Failure(category,category=="evaluation_timeout"?"Model evaluation reached its time limit. Simplify the model or increase --evaluation-timeout and reload.":"Model update cancelled. Edit the source or reload to try again.");impl_->Cleanup();return result;
}
ModelResult CheckModel(const std::string& executable,const fs::path& scene){
  ModelWorker worker(executable);worker.Start(scene,"",std::nullopt);
  while(true){if(auto result=worker.Poll())return std::move(*result);std::this_thread::sleep_for(std::chrono::milliseconds(10));}
}
} // namespace synthcad
