#include "agent_entry.h"
#include "agent_cli.h"
#include "agent_transport.h"
#include "project_contract.h"
#include <filesystem>
#include <iostream>
#include <set>
#include <fstream>
#include <chrono>
#include <stdexcept>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#else
#include <unistd.h>
#endif
namespace synthcad {
void WriteOutputFile(const std::filesystem::path& path,const std::string& bytes,bool replace){
  auto temporary=path;
  temporary+=std::string(".tmp-")+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
  try{
    {std::ofstream file(temporary,std::ios::binary);file.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));file.close();
      if(!file)throw std::runtime_error("Cannot write screenshot destination");}
#ifdef _WIN32
    if(!MoveFileExW(temporary.c_str(),path.c_str(),replace?MOVEFILE_REPLACE_EXISTING:0))
      throw std::runtime_error("Cannot publish screenshot; check destination and --replace");
#else
    if(replace)std::filesystem::rename(temporary,path);
    else {if(link(temporary.c_str(),path.c_str())!=0)throw std::runtime_error("Cannot publish screenshot; check destination and --replace");std::filesystem::remove(temporary);}
#endif
  }catch(...){std::error_code error;std::filesystem::remove(temporary,error);throw;}
}
std::vector<std::string> ProcessArguments(int argc,char** argv){
  std::vector<std::string> result;
#ifdef _WIN32
  int count=0;auto wide=CommandLineToArgvW(GetCommandLineW(),&count);
  if(wide){for(int i=0;i<count;++i){
    std::wstring module(32768,L'\0');
    const auto length=i==0?GetModuleFileNameW(nullptr,module.data(),static_cast<DWORD>(module.size())):0;
    if(length)module.resize(length);
    const auto value=length?module.c_str():wide[i];
    int n=WideCharToMultiByte(CP_UTF8,0,value,-1,nullptr,0,nullptr,nullptr);
    std::string text(n,'\0');WideCharToMultiByte(CP_UTF8,0,value,-1,text.data(),n,nullptr,nullptr);
    text.pop_back();result.push_back(text);
  }LocalFree(wide);return result;}
#endif
  for(int i=0;i<argc;++i)result.emplace_back(argv[i]);
#ifdef __linux__
  std::error_code error;const auto executable=std::filesystem::read_symlink("/proc/self/exe",error);
  if(!error&&!result.empty())result[0]=executable.u8string();
#endif
  return result;
}
bool IsAgentCommand(const std::vector<std::string>& arguments){
  if(arguments.size()<2)return false;
  const std::set<std::string> commands={"open","sessions","snapshot","selection","state","revision","wait","highlight","frame","view","screenshot","capabilities","version","help","--help","-h","--version","--json","--session","-s"};
  if(commands.count(arguments[1]))return true;
  return arguments[1].rfind("--",0)==0&&arguments[1]!="--render-scene"&&arguments[1]!="--profile-scene"&&arguments[1]!="--check-scene"&&arguments[1]!="--ui-preview"&&arguments[1]!="--agent-session";
}
int RunAgentCli(const std::vector<std::string>& arguments,const std::string& executable){
  auto parsed=ParseCli(arguments);
  auto& options=parsed.options;
  if(!parsed){auto response=Error(options.command,"invalid_argument",parsed.error);std::cout<<FormatResponse(response,options.jsonOutput);return 2;}
  if(options.help){
    if(options.jsonOutput)std::cout<<FormatResponse(Success("help",{{"text",Help(options.command)}}),true);
    else std::cout<<Help(options.command);
    return 0;
  }
  nlohmann::json response;
  try{
    if(options.version||options.command=="version")response=Success("version",{{"product","SynthCAD"},{"version","0.1.0"},{"protocolVersion",1}});
    else if(options.command=="capabilities")response=Success("capabilities",Capabilities());
    else if(options.command=="sessions")response=ListSessions();
    else if(options.command=="open"){
      const auto project=LoadProject(std::filesystem::u8path(options.arguments.at("path").get<std::string>()));
      auto viewer=std::filesystem::u8path(executable).parent_path()/
#ifdef _WIN32
        "dingcad_viewer.exe";
#else
        "dingcad_viewer";
#endif
      if(const char* configured=std::getenv("SYNTHCAD_VIEWER"))viewer=std::filesystem::u8path(configured);
      response=OpenSession(project.path.u8string(),options.session,options.arguments.value("hidden",false),std::filesystem::absolute(viewer).u8string(),options.timeoutMs);
    }else{
      if(options.command=="screenshot")options.arguments["path"]=std::filesystem::absolute(std::filesystem::u8path(options.arguments.at("path").get<std::string>())).u8string();
      response=Request(options.session,{{"protocolVersion",1},{"command",options.command},{"arguments",options.arguments},{"expectRevision",options.expectRevision},{"timeoutMs",options.timeoutMs}},options.timeoutMs+500);
    }
  }catch(const std::exception& error){response=Error(options.command,"invalid_argument",error.what());}
  std::cout<<FormatResponse(response,options.jsonOutput);
  return response.value("ok",false)?0:ExitCode(response.value("error",nlohmann::json::object()).value("code","io_error"));
}
}
