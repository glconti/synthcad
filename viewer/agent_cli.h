#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace synthcad {

struct CliOptions {
  std::string command;
  nlohmann::json arguments = nlohmann::json::object();
  std::string session;
  bool jsonOutput = false;
  int timeoutMs = 10000;
  bool help = false;
  bool version = false;
  std::string expectRevision;
};

struct CliParseResult {
  CliOptions options;
  std::string error;
  explicit operator bool() const { return error.empty(); }
};

// Arguments exclude the executable name. Parsing never starts a viewer.
CliParseResult ParseCli(const std::vector<std::string>& arguments);
std::string Help(const std::string& command = "");
nlohmann::json Capabilities();
nlohmann::json Success(const std::string& command,
    const nlohmann::json& data = nlohmann::json::object(),
    const std::string& session = "", const std::string& revision = "");
nlohmann::json Error(const std::string& command, const std::string& code,
    const std::string& message,
    const nlohmann::json& details = nlohmann::json::object(),
    const std::string& session = "", const std::string& revision = "");
int ExitCode(const std::string& code);
// The returned string is one response including its trailing newline.
// Callers send this to stdout and diagnostic logs exclusively to stderr.
std::string FormatResponse(const nlohmann::json& response, bool jsonOutput);

}  // namespace synthcad
