#pragma once

#include <nlohmann/json.hpp>
#include <functional>
#include <memory>
#include <string>

namespace synthcad {

class SessionServer {
 public:
  SessionServer();
  ~SessionServer();
  SessionServer(const SessionServer&) = delete;
  SessionServer& operator=(const SessionServer&) = delete;
  bool Start(const std::string& session, const std::string& projectPath,
             std::function<nlohmann::json(const nlohmann::json&)> handler,
             std::string& error);
  void Stop();
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// All public client functions return the standard CLI response envelope.
nlohmann::json ListSessions();
nlohmann::json OpenSession(const std::string& projectPath,
                          const std::string& requestedName, bool hidden,
                          const std::string& viewerExe, int timeoutMs = 10000);
nlohmann::json Request(const std::string& session, const nlohmann::json& request,
                       int timeoutMs = 10000);

}  // namespace synthcad
