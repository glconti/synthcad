#include "agent_transport.h"
#include "agent_cli.h"
#include "agent_bridge.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <sddl.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {
using nlohmann::json;
namespace fs = std::filesystem;
void Require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
void SetRoot(const fs::path& path) {
#ifdef _WIN32
  _putenv_s("SYNTHCAD_SESSION_DIR", path.u8string().c_str());
#else
  setenv("SYNTHCAD_SESSION_DIR", path.c_str(), 1);
#endif
}
void CheckTransport(const fs::path& root) {
  Require(synthcad::ListSessions().at("data").at("sessions").empty(), "test registry must start empty");
  Require(synthcad::Request("", {{"command", "state"}}).at("error").at("code") == "no_session", "missing session category incorrect");
  synthcad::SessionServer server;
  std::string error;
  bool started = server.Start(u8"review-\u00e8", (root / "a.js").u8string(), [](const json& request) {
    const std::string command = request.at("command");
    if (command == "slow") std::this_thread::sleep_for(std::chrono::milliseconds(250));
    json data = request.value("arguments", json::object());
    data["timeoutMs"] = request.at("timeoutMs");
    return synthcad::Success(command, data, u8"review-\u00e8", "r1");
  }, error);
  if (!started) throw std::runtime_error("server failed to start: " + error);
  auto sessions = synthcad::ListSessions();
  if (sessions.at("data").at("sessions").empty()) {
    std::cerr << "Empty session listing: " << sessions.dump() << "\n";
    for (const auto& file : fs::directory_iterator(root)) {
      std::ifstream contents(file.path());
      std::cerr << file.path() << ": " << contents.rdbuf() << "\n";
    }
  }
  Require(sessions.at("data").at("sessions").size() == 1, "live session discovery failed");
  Require(!sessions.at("data").at("sessions")[0].contains("nonce"), "registry secret leaked into session listing");
  auto echo = synthcad::Request("", {{"command", "echo"}, {"arguments", {{"path", u8"Pi\u00e8ce / \u96f6\u4ef6.js"}}}});
  Require(echo.at("ok") == true && echo.at("data").at("path") == u8"Pi\u00e8ce / \u96f6\u4ef6.js", "Unicode round trip failed");
  Require(echo.at("session") == u8"review-\u00e8" && echo.at("revision") == "r1", "response identity was lost");
  const std::string largePayload(1024 * 1024 + 137, 'x');
  auto largeEcho = synthcad::Request(
      u8"review-\u00e8",
      {{"command", "echo"}, {"arguments", {{"payload", largePayload}}}}, 10000);
  if (!largeEcho.value("ok", false) ||
      !largeEcho.value("data", json::object()).contains("payload") ||
      largeEcho.at("data").at("payload").get<std::string>() != largePayload) {
    std::cerr << "Large transport response: " << largeEcho.value("error", json::object()).dump() << "\n";
  }
  Require(largeEcho.at("ok") == true &&
              largeEcho.at("data").at("payload").get<std::string>() == largePayload,
          "one-megabyte request and response round trip failed");
  auto guardedTimeout = synthcad::Request(u8"review-\u00e8", {{"command", "echo"}, {"timeoutMs", 20}}, 1000);
  Require(guardedTimeout.at("ok") == true && guardedTimeout.at("data").at("timeoutMs") == 20,
          "transport grace must not extend the viewer request deadline");
  Require(synthcad::Request(u8"review-\u00e8", {{"command", "echo"}, {"timeoutMs", 300001}}, 1000).at("error").at("code") == "invalid_argument",
          "out-of-range request timeout must fail");
  Require(synthcad::Request(u8"review-\u00e8", {{"command", "echo"}}, 300501).at("error").at("code") == "invalid_argument",
          "out-of-range transport timeout must fail");
  synthcad::SessionServer duplicate;
  Require(!duplicate.Start(u8"review-\u00e8", "b.js", [](const json&) { return json::object(); }, error), "duplicate session names must fail");
  auto slow = std::async(std::launch::async, [] {
    return synthcad::Request(u8"review-\u00e8", {{"command", "slow"}}, 1000);
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  const auto start = std::chrono::steady_clock::now();
  auto fast = synthcad::Request(u8"review-\u00e8", {{"command", "state"}}, 150);
  Require(fast.at("ok") == true, "waiter must not block concurrent state requests");
  Require(std::chrono::steady_clock::now() - start < std::chrono::milliseconds(200), "state read blocked behind waiter");
  Require(slow.get().at("ok") == true, "concurrent slow request failed");
  auto timeout = synthcad::Request(u8"review-\u00e8", {{"command", "slow"}}, 30);
  Require(timeout.at("error").at("code") == "timeout", "client deadline did not expire");
  synthcad::SessionServer second;
  Require(second.Start("second", (root / "b.js").u8string(), [](const json&) {
    return synthcad::Success("state", {}, "second");
  }, error), "second server failed to start");
  Require(synthcad::Request("", {{"command", "state"}}).at("error").at("code") == "ambiguous_session", "multiple projects must require explicit session");
  Require(synthcad::Request("missing", {{"command", "state"}}).at("error").at("code") == "no_session", "named missing session category incorrect");
  second.Stop();
  server.Stop();
  Require(synthcad::ListSessions().at("data").at("sessions").empty(), "closed sessions were not removed");
  std::ofstream stale(root / "stale.json");
  stale << json({{"protocolVersion", 1}, {"session", "stale"}, {"projectPath", "missing.js"},
      {"pid", 4294967295ul}, {"processIdentity", "old"}, {"endpoint", "missing"}, {"nonce", "old"}}).dump();
  stale.close();
  auto afterStale = synthcad::ListSessions();
  if (fs::exists(root / "stale.json")) {
    std::ifstream contents(root / "stale.json");
    std::cerr << "Stale listing " << afterStale.dump() << " file=" << contents.rdbuf() << "\n";
  }
  Require(afterStale.at("data").at("sessions").empty() && !fs::exists(root / "stale.json"), "stale registry record was not recovered");
#ifndef _WIN32
  struct stat info{};
  Require(!stat(root.c_str(), &info) && (info.st_mode & 0777) == 0700, "session directory must be private");
#endif
}
void CheckClosingEventDelivery(const fs::path& root) {
  synthcad::AgentBridge bridge("closing-events", "transport-close");
  bridge.Publish({{"status","ready"},{"displayedRevision","r1"}}, {});
  const auto receipt=bridge.BeginPick({{"id","closing"},{"kind","part"},{"question","Choose a part"}},"r1");
  std::promise<void> entered;
  auto enteredFuture=entered.get_future();
  synthcad::SessionServer server;
  std::string error;
  Require(server.Start("closing-events",(root/"closing.js").u8string(),[&](const json& request){
    entered.set_value();return bridge.Handle(request);
  },error),"close-delivery server failed to start");
  auto waiter=std::async(std::launch::async,[&]{
    return synthcad::Request("closing-events",{{"command","events"},{"timeoutMs",2000},
      {"arguments",{{"after",receipt.at("eventCursor")},{"waitMs",1500}}}},2500);
  });
  Require(enteredFuture.wait_for(std::chrono::seconds(1))==std::future_status::ready,"event waiter did not reach bridge");
  bridge.Close();server.Stop();
  const auto response=waiter.get();
  Require(!response.at("ok").get<bool>()&&response.at("error").at("code")=="cancelled","shutdown lost final event response");
  const auto events=response.at("error").at("details").at("events");
  Require(events.size()==2&&events[0].at("type")=="pick-cancelled"&&events[1].at("type")=="viewer-closed", "shutdown lost final event payload");
}
#ifdef _WIN32
// A real process DACL, not a simulated timeout: OpenProcess must fail while the
// test retains the creation handle to safely terminate its suspended child.
struct UninspectableProcess {
  PROCESS_INFORMATION process{};
  explicit UninspectableProcess(const std::string& executable) {
    PSECURITY_DESCRIPTOR descriptor=nullptr;
    Require(ConvertStringSecurityDescriptorToSecurityDescriptorW(
        L"D:(D;;0x1000;;;WD)(A;;GA;;;OW)",SDDL_REVISION_1,&descriptor,nullptr)!=0,"process test DACL failed");
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES),descriptor,FALSE};
    STARTUPINFOW startup{};startup.cb=sizeof(startup);
    const auto path=fs::u8path(executable).wstring();
    const bool created=CreateProcessW(path.c_str(),nullptr,&attributes,nullptr,FALSE,
        CREATE_SUSPENDED|CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process)!=0;
    LocalFree(descriptor);
    Require(created,"cannot create test-owned suspended process");
  }
  ~UninspectableProcess(){
    if(process.hProcess){TerminateProcess(process.hProcess,0);WaitForSingleObject(process.hProcess,3000);CloseHandle(process.hProcess);}
    if(process.hThread)CloseHandle(process.hThread);
  }
};
void CheckDeniedInspection(const fs::path& root,const std::string& executable) {
  UninspectableProcess child(executable);
  HANDLE query=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,child.process.dwProcessId);
  const auto code=GetLastError();if(query)CloseHandle(query);
  Require(!query&&code==ERROR_ACCESS_DENIED,"test process inspection must really be denied");
  const auto project=root/"access.js";std::ofstream(project)<<"// access fixture";
  synthcad::SessionServer server;std::string error;
  Require(server.Start("access",project.u8string(),[](const json& request){
    return synthcad::Success(request.at("command"),{},"access");
  },error),"access fixture server failed");
  fs::path recordPath;json record;
  for(const auto& file:fs::directory_iterator(root))if(file.path().extension()==".json"){
    std::ifstream input(file.path());json candidate;input>>candidate;
    if(candidate.value("session","")=="access"){recordPath=file.path();record=std::move(candidate);break;}
  }
  Require(!recordPath.empty(),"access fixture record absent");
  // Retain the real nonce/endpoint but make process inspection unavailable.
  record["pid"]=child.process.dwProcessId;
  const std::string original=record.dump();std::ofstream(recordPath)<<original;
  const auto listed=synthcad::ListSessions();
  Require(listed.at("data").at("sessions").size()==1,"unknown process must remain discoverable");
  const auto entry=listed.at("data").at("sessions")[0];
  Require(entry.at("reachable")==true&&entry.at("processStatus")=="unknown","authenticated handshake should recover denied inspection");
  Require(synthcad::Request("access",{{"command","state"}}).at("ok")==true,"denied process query must not block working RPC");
  const auto reused=synthcad::OpenSession(project.u8string(),"access",true,"must-not-launch.exe",200);
  Require(reused.at("ok")==true&&reused.at("data").at("reused")==true,"unknown responding process must be reused");
  synthcad::SessionServer duplicate;
  Require(!duplicate.Start("access",project.u8string(),[](const json&){return json{};},error),"unknown process must block duplicate registration");
  server.Stop();
  // The same private record now describes an inaccessible, unresponsive process.
  std::ofstream(recordPath)<<original;
  const auto unavailable=synthcad::ListSessions();
  Require(unavailable.at("data").at("sessions").size()==1&&!unavailable.at("data").at("sessions")[0].at("reachable").get<bool>(),"unreachable unknown process must remain listed");
  const auto refused=synthcad::OpenSession(project.u8string(),"access",true,"must-not-launch.exe",100);
  Require(refused.at("error").at("code")=="io_error"&&refused.at("error").contains("details"),"unknown unreachable process needs diagnostics, not a new launch");
  std::ifstream input(recordPath);std::string after((std::istreambuf_iterator<char>(input)),{});input.close();
  Require(after==original,"permission failure modified the session record");
  fs::remove(recordPath);
  const auto launch=synthcad::OpenSession(project.u8string(),"slow",true,executable,20);
  Require(launch.at("error").at("code")=="timeout","slow fixture must leave a pending launch");
  const auto pid=launch.at("error").at("details").at("pid").get<DWORD>();
  HANDLE launched=OpenProcess(PROCESS_TERMINATE|SYNCHRONIZE,FALSE,pid);
  Require(launched!=nullptr,"cannot clean up slow test-owned launch");
  TerminateProcess(launched,0);WaitForSingleObject(launched,3000);CloseHandle(launched);
  fs::path pendingPath;
  for(const auto& file:fs::directory_iterator(root))if(file.path().extension()==".launch")pendingPath=file.path();
  Require(!pendingPath.empty(),"missing pending launch fixture");
  const std::string pending=json({{"pid",child.process.dwProcessId},{"processIdentity","unknown-identity"},{"session","slow"}}).dump();
  std::ofstream(pendingPath)<<pending;
  const auto pendingResult=synthcad::OpenSession(project.u8string(),"slow",true,"must-not-launch.exe",30);
  Require(pendingResult.at("error").at("code")=="io_error"&&pendingResult.at("error").at("details").at("pid")==child.process.dwProcessId,"uncertain pending launch was replaced");
  std::ifstream pendingInput(pendingPath);std::string pendingAfter((std::istreambuf_iterator<char>(pendingInput)),{});pendingInput.close();
  Require(pendingAfter==pending,"uncertain pending launch was modified");
  fs::remove(pendingPath);
}
void CheckDeniedLock(const fs::path& root,const std::string& executable) {
  const auto project=root/"lock.js";std::ofstream(project)<<"// lock fixture";
  const auto path=root/"open.lock";std::ofstream(path).close();
  Require(SetFileAttributesW(path.c_str(),FILE_ATTRIBUTE_READONLY)!=0,"cannot make test lock read-only");
  const auto start=std::chrono::steady_clock::now();
  const auto denied=synthcad::OpenSession(project.u8string(),"lock",true,executable,2000);
  SetFileAttributesW(path.c_str(),FILE_ATTRIBUTE_NORMAL);
  Require(denied.at("error").at("code")=="io_error","access denied must not masquerade as a lock timeout");
  Require(std::chrono::steady_clock::now()-start<std::chrono::seconds(1),"lock access failure must return promptly");
  Require(denied.at("error").at("message").get<std::string>().find("Windows error 5")!=std::string::npos,"lock error lost native diagnostic");
}
#endif
void CheckLaunch(const fs::path& root, const std::string& executable) {
  fs::path project = root / fs::u8path(u8"Project \u00e8.js");
  std::ofstream(project) << "// test project";
  auto first = std::async(std::launch::async, [&] {
    return synthcad::OpenSession(project.u8string(), "launched", true, executable, 5000);
  });
  auto second = std::async(std::launch::async, [&] {
    return synthcad::OpenSession(project.u8string(), "launched", true, executable, 5000);
  });
  auto a = first.get();
  auto b = second.get();
  Require(a.at("ok") == true && b.at("ok") == true, "persistent process launch failed");
  Require(a.at("data").at("pid") == b.at("data").at("pid"), "simultaneous open created duplicate processes");
  Require(a.at("data").at("reused") != b.at("data").at("reused"), "repeat open must report reuse");
  Require(synthcad::Request("launched", {{"command", "state"}}).at("ok") == true, "launched process must survive short-lived open calls");
  Require(synthcad::OpenSession(project.u8string(), "other-name", true, executable).at("error").at("code") == "invalid_argument", "same project must not acquire a second name");
  Require(synthcad::Request("launched", {{"command", "shutdown"}}).at("ok") == true, "test child failed to stop");
  for (int i = 0; i < 100 && !synthcad::ListSessions().at("data").at("sessions").empty(); ++i)
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  Require(synthcad::ListSessions().at("data").at("sessions").empty(), "test child session did not close");
  auto delayed=synthcad::OpenSession(project.u8string(),"slow",true,executable,20);
  Require(delayed.at("error").at("code")=="timeout","delayed startup must preserve pending launch on timeout");
  auto recovered=synthcad::OpenSession(project.u8string(),"slow",true,executable,5000);
  Require(recovered.at("ok")==true&&recovered.at("data").at("reused")==true&&
      recovered.at("data").at("pid")==delayed.at("error").at("details").at("pid"),"retry after slow startup created a duplicate");
  Require(synthcad::Request("slow",{{"command","shutdown"}}).at("ok")==true,"slow child failed to stop");
  for(int i=0;i<100&&!synthcad::ListSessions().at("data").at("sessions").empty();++i)
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  Require(synthcad::ListSessions().at("data").at("sessions").empty(),"slow child did not close");
}
int Child(const std::string& session, const std::string& project) {
  if(session=="slow")std::this_thread::sleep_for(std::chrono::milliseconds(600));
  std::atomic<bool> stop{false};
  synthcad::SessionServer server;
  std::string error;
  if (!server.Start(session, project, [&](const json& request) {
    if (request.at("command") == "shutdown") stop.store(true);
    return synthcad::Success(request.at("command"), {}, session);
  }, error)) return 1;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
  while (!stop.load() && std::chrono::steady_clock::now() < deadline)
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  server.Stop();
  return 0;
}
}

int Run(int argc, char** argv) {
  if (argc >= 5 && std::string(argv[1]) == "--agent-session") return Child(argv[2], argv[4]);
  try {
    fs::path base;
#ifdef _WIN32
    base = fs::current_path() / "local-scenes";
    fs::create_directories(base);
#else
    base = fs::temp_directory_path();
#endif
    fs::path root = base / ("sc-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    SetRoot(root);
    CheckTransport(root);
    CheckClosingEventDelivery(root);
    CheckLaunch(root, fs::absolute(fs::u8path(argv[0])).u8string());
#ifdef _WIN32
    CheckDeniedInspection(root,fs::absolute(fs::u8path(argv[0])).u8string());
    CheckDeniedLock(root,fs::absolute(fs::u8path(argv[0])).u8string());
#endif
    fs::remove_all(root);
    std::cout << "Agent transport tests passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
  std::vector<std::string> strings;
  std::vector<char*> arguments;
  for (int i = 0; i < argc; ++i) {
    int length = int(wcslen(argv[i]));
    int size = WideCharToMultiByte(CP_UTF8, 0, argv[i], length, nullptr, 0, nullptr, nullptr);
    std::string converted(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, argv[i], length, converted.data(), size, nullptr, nullptr);
    strings.push_back(std::move(converted));
  }
  for (auto& value : strings) arguments.push_back(value.data());
  return Run(argc, arguments.data());
}
#else
int main(int argc, char** argv) { return Run(argc, argv); }
#endif
