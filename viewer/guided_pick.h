#pragma once
#include <nlohmann/json.hpp>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>

namespace synthcad {
class GuidedPickError : public std::runtime_error {
 public:
  GuidedPickError(std::string code, const std::string& message)
      : std::runtime_error(message), code(std::move(code)) {}
  std::string code;
};
// Pure state engine. The bridge serializes all access to it.
class GuidedPickState {
 public:
  explicit GuidedPickState(std::string epoch);
  nlohmann::json Begin(const nlohmann::json& args, const std::string& revision);
  std::optional<nlohmann::json> Replay(const nlohmann::json& args) const;
  nlohmann::json Active() const;
  nlohmann::json Status(const std::string& id) const;
  nlohmann::json Cancel(const std::string& id, const std::string& reason = "user_cancelled");
  bool Confirm(const std::string& id, const nlohmann::json& geometry, const std::string& revision);
  void Invalidate(const std::string& reason);
  void Close();
  nlohmann::json Events(const std::string& after) const;
  std::string Cursor() const;
  void LoadEvent(const std::string& type,const nlohmann::json& context);
 private:
  void Emit(const std::string& type, const nlohmann::json& request);
  void Finish(const std::string& status, const std::string& type, const std::string& reason);
  std::string epoch_, active_;
  uint64_t sequence_ = 0;
  bool closed_ = false;
  std::map<std::string, nlohmann::json> requests_;
  std::deque<nlohmann::json> events_;
};
}
