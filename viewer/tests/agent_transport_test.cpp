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
}
int Child(const std::string& session, const std::string& project) {
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
