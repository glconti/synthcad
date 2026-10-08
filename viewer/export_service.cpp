#include "export_service.h"
#include "project_contract.h"
#include "three_mf_export.h"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <map>
#include <random>
#include <sstream>
#include <stdexcept>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#define NOUSER
#include <windows.h>
#else
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <unistd.h>
#endif

namespace dingcad {
using nlohmann::json;
namespace {
json Result(bool okay,const std::string &code,const std::string &message,const json &review=json(nullptr),const json &record=json(nullptr)) {
  const auto canonical=code=="invalid_context" || code=="invalid_format" || code=="invalid_path" || code=="invalid_options" ? "invalid_argument" : code=="write_failed" ? "io_error" : code;
  return {{"ok",okay},{"code",canonical},{"message",message},{"review",review},{"record",record}};
}
std::string Text(const json &object,const char *key) {
  const auto it=object.find(key); return it!=object.end() && it->is_string() ? it->get<std::string>() : std::string();
}
bool Boolean(const json &object,const char *key,bool fallback=false) {
  return object.contains(key) ? object.at(key).get<bool>() : fallback;
}
struct Stage {
  std::filesystem::path directory;
  ~Stage() {std::error_code ignored; if(!directory.empty())std::filesystem::remove_all(directory,ignored);}
};
bool CreateStage(const std::filesystem::path &parent,Stage &stage,std::string &error) {
  static std::atomic<unsigned long long> sequence{0};
  for(int attempt=0;attempt<16;++attempt) {
    const auto candidate=parent/(".synthcad-export-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+std::to_string(sequence++));
    std::error_code ec;
    if(std::filesystem::create_directory(candidate,ec)) {stage.directory=candidate;return true;}
    if(ec) {error=ec.message();return false;}
  }
  error="Could not reserve export staging directory"; return false;
}
std::string Commit(const std::filesystem::path &stage,const std::filesystem::path &destination,bool replace,std::string &error) {
#ifdef _WIN32
  if(MoveFileExW(stage.c_str(),destination.c_str(),replace?MOVEFILE_REPLACE_EXISTING:0))return {};
  const auto failure=GetLastError();
  if(!replace && (failure==ERROR_FILE_EXISTS || failure==ERROR_ALREADY_EXISTS))return "destination_exists";
  error="Atomic export commit failed (Windows error "+std::to_string(failure)+")";
#else
  if(replace ? ::rename(stage.c_str(),destination.c_str())==0 : ::link(stage.c_str(),destination.c_str())==0)return {};
  const auto failure=errno;
  if(!replace && failure==EEXIST)return "destination_exists";
  error=std::strerror(failure);
#endif
  return "write_failed";
}
}

json ReviewExport(const PartTree &tree,const json &context,const json &options) {
  try {
    if(!context.is_object() || !options.is_object())return Result(false,"invalid_context","Export context and options must be objects.");
    if(!context.contains("current") || !context["current"].is_boolean())return Result(false,"invalid_context","Export requires a known current geometry context.");
    if(!context["current"].get<bool>())return Result(false,"stale_revision","Displayed geometry is not current.");
    for(const char *key:{"modelRevision","sourceRevision","view"})
      if(Text(context,key).empty())return Result(false,"invalid_context",std::string("Missing export basis: ")+key);
    for(const char *key:{"visibleOnly","replace","allowWarnings","dryRun"})
      if(options.contains(key) && !options[key].is_boolean())return Result(false,"invalid_options",std::string("Expected boolean option: ")+key);
    const auto format=Text(options,"format");
    if(format!="3mf" && format!="stl")return Result(false,"invalid_format","Export format must be 3mf or stl.");
    const auto pathText=Text(options,"path");
    if(pathText.empty() || pathText.find('\0')!=std::string::npos)return Result(false,"invalid_path","Export requires an absolute destination path.");
    // JSON serialization rejects malformed UTF-8 without touching the filesystem.
    json(pathText).dump();
    const auto path=std::filesystem::u8path(pathText).lexically_normal();
    if(!path.is_absolute() || path.filename().empty() || path.filename()=="." || path.filename()=="..")return Result(false,"invalid_path","Export requires an absolute file path.");
    auto extension=path.extension().u8string();
    std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
    if(extension!="."+format)return Result(false,"invalid_path","Destination extension must match the selected export format.");
    const bool visibleOnly=Boolean(options,"visibleOnly");
    const auto selected=tree.ExportIndices(visibleOnly);
    if(selected.empty())return Result(false,"empty_export","No exportable parts are selected.");
    json ids=json::array(),quantities=json::array(),risks=json::array();
    std::map<std::string,size_t> counts;
    for(auto index:selected) {
      const auto &part=tree.parts.at(index);
      if(!part.solid || part.solid->Status()!=manifold::Manifold::Error::NoError)return Result(false,"load_failed","A selected part has no valid evaluated solid.");
      if(part.solid->IsEmpty())return Result(false,"empty_export","A selected exportable part is empty.");
      const auto bounds=part.solid->BoundingBox();
      for(int axis=0;axis<3;++axis)
        if(!std::isfinite(bounds.min[axis]) || !std::isfinite(bounds.max[axis]) || bounds.max[axis]<bounds.min[axis])return Result(false,"load_failed","A selected solid has invalid bounds.");
      const auto volume=part.solid->Volume();
      if(!std::isfinite(volume) || volume<=0)return Result(false,"load_failed","A selected solid has no positive finite volume.");
      ids.push_back(part.id); ++counts[part.sourcePartId.empty()?part.id:part.sourcePartId];
    }
    for(const auto &[id,count]:counts)quantities.push_back({{"sourcePartId",id},{"count",count}});
    json checks=context.value("checks",json::array());
    if(!checks.is_array())return Result(false,"invalid_context","Cached manufacturing checks must be an array.");
    if(checks.empty())checks.push_back({{"id","manufacturing-review"},{"result","not-checked"},{"scope","geometry"},{"partIds",ids},{"method","Cached manufacturing review unavailable"},{"nextActions",json::array({"Review printer limits, placements and clearances before printing."})}});
    for(const auto &check:checks) {
      if(!check.is_object())return Result(false,"invalid_context","Cached check record is malformed.");
      const auto outcome=Text(check,"result");
      if(outcome!="passed" && outcome!="warning" && outcome!="failed" && outcome!="not-checked")return Result(false,"invalid_context","Cached check outcome is unknown.");
      if(outcome!="passed")risks.push_back(check);
    }
    json basis=json::object();
    for(const char *key:{"view","kind","modelRevision","sourceRevision","layoutRevision","profileRevision","revision"})basis[key]=context.value(key,json(nullptr));
    if(context.contains("dependencies")) {
      if(!context["dependencies"].is_object())return Result(false,"invalid_context","Consumed dependencies must be a path-to-digest object.");
      for(const auto &entry:context["dependencies"].items())
        if(!entry.value().is_string())return Result(false,"invalid_context","Consumed dependency digest must be a string.");
    }
    json review={{"path",path.u8string()},{"format",format},{"visibleOnly",visibleOnly},
      {"replace",Boolean(options,"replace")},{"allowWarnings",Boolean(options,"allowWarnings")},{"dryRun",Boolean(options,"dryRun")},
      {"projectPath",context.value("projectPath",json(nullptr))},{"partIds",ids},{"quantities",quantities},{"basis",basis},
      {"profile",context.value("profile",json(nullptr))},{"checks",checks},{"risks",risks},{"hasWarnings",!risks.empty()},
      {"checkScope","Cached checks cover authored exportable geometry; the chosen export subset is not separately validated."}};
    return Result(true,"reviewed","Export review is ready.",review);
  } catch(const std::exception &error) {return Result(false,"invalid_context",error.what());}
}

json ExecuteExport(const PartTree &tree,const json &context,const json &options,const std::function<std::string()> &guard) {
  auto result=ReviewExport(tree,context,options);
  if(!result["ok"].get<bool>())return result;
  const auto review=result["review"];
  if(review["dryRun"].get<bool>())return Result(true,"dry_run","Export review completed without writing files.",review);
  if(review["hasWarnings"].get<bool>() && !review["allowWarnings"].get<bool>())return Result(false,"warnings_present","Review the risks and explicitly allow warnings before exporting.",review);
  try {
    const auto path=std::filesystem::u8path(review["path"].get<std::string>());
    const bool replace=review["replace"].get<bool>(); std::error_code ec;
    const bool exists=std::filesystem::exists(path,ec);
    if(ec)return Result(false,"write_failed",ec.message(),review);
    if(exists && !replace)return Result(false,"destination_exists","Destination already exists.",review);
    if(guard) {
      const auto blocked=guard();
      if(!blocked.empty())return Result(false,blocked,"Export was cancelled before staging.",review);
    }
    std::filesystem::create_directories(path.parent_path(),ec);
    if(ec)return Result(false,"write_failed",ec.message(),review);
    Stage stage; std::string error;
    if(!CreateStage(path.parent_path(),stage,error))return Result(false,"write_failed",error,review);
    const auto staged=stage.directory/(review["format"].get<std::string>()=="3mf"?"payload.3mf":"payload.stl");
    const bool visibleOnly=review["visibleOnly"].get<bool>();
    const auto saved=review["format"]=="3mf"?ExportParts3mf(tree,visibleOnly,true,staged,false,error):ExportParts(tree,visibleOnly,true,staged,false,error);
    if(saved!=ExportResult::Saved)return Result(false,saved==ExportResult::Empty?"empty_export":"write_failed",error.empty()?"Export serialization failed.":error,review);
    std::ifstream input(staged,std::ios::binary);
    if(!input)return Result(false,"write_failed","Cannot read staged export for its receipt.",review);
    const std::string bytes{std::istreambuf_iterator<char>(input),{}};
    if(input.bad())return Result(false,"write_failed","Cannot finish reading staged export.",review);
    input.close();
    if(review["format"]=="stl") {
      if(bytes.size()<84)return Result(false,"write_failed","Staged STL is truncated.",review);
      uint32_t triangles=0;
      for(unsigned i=0;i<4;++i)triangles|=uint32_t(static_cast<unsigned char>(bytes[80+i]))<<(8*i);
      if(bytes.size()!=84ull+50ull*triangles)return Result(false,"write_failed","Staged STL length does not match its triangle count.",review);
    }
    json record={{"schemaVersion",1},{"path",review["path"]},{"format",review["format"]},{"basis",review["basis"]},
      {"projectPath",review["projectPath"]},{"partIds",review["partIds"]},{"quantities",review["quantities"]},
      {"checks",review["checks"]},{"profile",review["profile"]},{"risks",review["risks"]},{"allowWarnings",review["allowWarnings"]},
      {"dependencies",context.value("dependencies",json::object())},{"sha256",synthcad::Sha256(bytes)},{"sizeBytes",bytes.size()}};
    const auto receiptEntropy=std::to_string(std::random_device{}());
    if(context.contains("dependencies")) {
      const auto dependencies=context["dependencies"].get<synthcad::FileSnapshot>();
      if(!synthcad::MatchesDisk(dependencies))return Result(false,"stale_revision","Source dependencies changed before export commit.",review);
    }
    // Allocate the complete success response before the final guard/commit.
    // The timestamp describes this commit attempt; no receipt is returned until
    // the destination is committed, and no JSON allocation follows success.
    const auto now=std::chrono::system_clock::now();
    const auto time=std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc,&time);
#else
    gmtime_r(&time,&utc);
#endif
    std::ostringstream stamp;stamp<<std::put_time(&utc,"%Y-%m-%dT%H:%M:%SZ");
    record["createdAt"]=stamp.str();
    static std::atomic<unsigned long long> receiptSequence{0};
    record["id"]=synthcad::Sha256(record["sha256"].get<std::string>()+std::to_string(now.time_since_epoch().count())+"-"+std::to_string(receiptSequence++)+"-"+receiptEntropy);
    auto success=Result(true,"saved","Export saved.",review,record);
    if(guard) {
      const auto blocked=guard();
      if(!blocked.empty())return Result(false,blocked,"Export was cancelled before committing the destination.",review);
    }
    // No serialization, hashing, dependency reads, or JSON allocation runs
    // between the final guard and atomic filesystem operation.
    const auto failed=Commit(staged,path,replace,error);
    if(!failed.empty())return Result(false,failed,error.empty()?"Destination already exists.":error,review);
    return success;
  } catch(const std::exception &error) {return Result(false,"write_failed",error.what(),review);}
}
}
