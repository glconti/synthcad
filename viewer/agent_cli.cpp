#include "agent_cli.h"

#include <algorithm>
#include <charconv>
#include <map>
#include <set>
#include <sstream>

namespace synthcad {
namespace {
using nlohmann::json;
const std::map<std::string, std::string> kUsage = {
    {"docs", "docs [AREA]"},
    {"open", "open PATH [--session NAME] [--hidden]"},
    {"sessions", "sessions"}, {"snapshot", "snapshot"},
    {"selection", "selection"}, {"state", "state"},
    {"revision", "revision"},
    {"wait", "wait --revision TOKEN [--timeout MS]"},
    {"highlight", "highlight [PART_IDS...] [--clear] [--frame]"},
    {"frame", "frame [PART_IDS... | --selection]"},
    {"view", "view NAME"},
    {"screenshot", "screenshot PATH [--replace]"},
    {"capabilities", "capabilities"}, {"version", "version"}};
const std::map<std::string, std::string> kDescriptions = {
    {"docs", "List guidance areas, or print one complete guide to stdout. No viewer or skill installation required."},
    {"open", "Open or reuse a persistent project session. --hidden is for automation."},
    {"sessions", "List live local sessions."},
    {"snapshot", "Read semantic scene state, without meshes."},
    {"selection", "Read the user's current selection."},
    {"state", "Read load state and attempted/displayed revisions."},
    {"revision", "Compute the desired revision from current files on disk."},
    {"wait", "Wait for the requested revision; default timeout is 10000 ms."},
    {"highlight", "Replace agent highlights, or clear them. --frame also frames the result."},
    {"frame", "Frame the whole scene, given parts, or the current selection."},
    {"view", "Switch to a named project view."},
    {"screenshot", "Save a screenshot to PATH; --replace permits overwriting."},
    {"capabilities", "List supported commands and protocol capabilities."},
    {"version", "Print the application and protocol versions."}};
const std::set<std::string> kReview = {
    "snapshot", "selection", "state", "highlight", "frame", "view", "screenshot"};
json Envelope(const std::string& command, const std::string& session,
              const std::string& revision, bool ok) {
  json result = {{"protocolVersion", 1}, {"ok", ok}, {"command", command}};
  if (!session.empty()) result["session"] = session;
  if (!revision.empty()) result["revision"] = revision;
  return result;
}
}  // namespace

CliParseResult ParseCli(const std::vector<std::string>& arguments) {
  CliParseResult result;
  auto& options = result.options;
  std::vector<std::string> positional;
  std::set<std::string> flags;
  bool literal = false;
  auto fail = [&](const std::string& message) -> CliParseResult {
    result.error = message;
    return result;
  };
  for (size_t i = 0; i < arguments.size(); ++i) {
    const std::string& token = arguments[i];
    if (!literal && token == "--") { literal = true; continue; }
    if (!literal && !token.empty() && token[0] == '-') {
      const auto equal = token.find('=');
      std::string flag = token.substr(0, equal);
      if (flag == "-s") flag = "--session";
      if (flag == "-h") flag = "--help";
      if (!flags.insert(flag).second) return fail("Repeated option: " + flag);
      const bool needsValue = flag == "--session" || flag == "--timeout" ||
          flag == "--revision" || flag == "--expect-revision";
      std::string value;
      if (needsValue) {
        if (equal != std::string::npos) value = token.substr(equal + 1);
        else if (i + 1 < arguments.size() && !arguments[i + 1].empty() &&
                 arguments[i + 1][0] != '-') value = arguments[++i];
        if (value.empty()) return fail("Missing value for " + flag);
      } else if (equal != std::string::npos) {
        return fail("Option does not accept a value: " + flag);
      }
      if (flag == "--session") options.session = value;
      else if (flag == "--json") options.jsonOutput = true;
      else if (flag == "--help") options.help = true;
      else if (flag == "--version") options.version = true;
      else if (flag == "--expect-revision") options.expectRevision = value;
      else if (flag == "--timeout") {
        int timeout = 0;
        auto parsed = std::from_chars(value.data(), value.data() + value.size(), timeout);
        if (parsed.ec != std::errc() || parsed.ptr != value.data() + value.size() || timeout < 0 || timeout > 300000)
          return fail("--timeout must be an integer from 0 to 300000 milliseconds");
        options.timeoutMs = timeout;
      } else if (flag == "--revision") options.arguments["revision"] = value;
      else if (flag == "--hidden") options.arguments["hidden"] = true;
      else if (flag == "--clear") options.arguments["clear"] = true;
      else if (flag == "--frame") options.arguments["frame"] = true;
      else if (flag == "--selection") options.arguments["selection"] = true;
      else if (flag == "--replace") options.arguments["replace"] = true;
      else return fail("Unknown option: " + flag);
      continue;
    }
    if (options.command.empty()) options.command = token;
    else positional.push_back(token);
  }
  if (options.command == "help") {
    options.help = true;
    if (positional.size() > 1) return fail("Usage: synthcad help [COMMAND]");
    options.command = positional.empty() ? "" : positional.front();
    positional.clear();
  }
  if (options.version) {
    if (!options.command.empty() && options.command != "version")
      return fail("--version cannot be combined with a command");
    options.command = "version";
  }
  if (options.command.empty()) {
    if (!options.arguments.empty() || !options.expectRevision.empty() || !options.session.empty() ||
        flags.count("--timeout")) return fail("A command is required");
    options.help = true;
    return result;
  }
  if (!kUsage.count(options.command)) return fail("Unknown command: " + options.command);
  const std::map<std::string, std::string> owners = {
      {"--hidden", "open"}, {"--revision", "wait"}, {"--clear", "highlight"},
      {"--frame", "highlight"}, {"--selection", "frame"}, {"--replace", "screenshot"}};
  for (const auto& entry : owners) {
    if (flags.count(entry.first) && options.command != entry.second)
      return fail(entry.first + " is only valid for " + entry.second);
  }
  if (!options.expectRevision.empty() && !kReview.count(options.command))
    return fail("--expect-revision is only valid for review commands");
  if (options.help) return result;
  if (options.command == "open" || options.command == "view" || options.command == "screenshot") {
    if (positional.size() != 1 || positional.front().empty())
      return fail("Usage: synthcad " + kUsage.at(options.command));
    options.arguments[options.command == "view" ? "name" : "path"] = positional.front();
  } else if (options.command == "docs") {
    if (positional.size() > 1 || (!positional.empty() && positional.front().empty()))
      return fail("Usage: synthcad docs [AREA]");
    if (!positional.empty()) options.arguments["topic"] = positional.front();
  } else if (options.command == "highlight" || options.command == "frame") {
    if (options.command == "highlight" && options.arguments.value("clear", false) && !positional.empty())
      return fail("--clear cannot be combined with part IDs");
    if (options.command == "highlight" && positional.empty() && !options.arguments.value("clear", false))
      return fail("highlight requires part IDs or --clear");
    if (options.command == "frame" && options.arguments.value("selection", false) && !positional.empty())
      return fail("--selection cannot be combined with part IDs");
    if (std::any_of(positional.begin(), positional.end(), [](const std::string& id) { return id.empty(); }))
      return fail("Part IDs must not be empty");
    options.arguments["partIds"] = positional;
  } else if (!positional.empty()) return fail("Unexpected argument: " + positional.front());
  if (options.command == "wait" && !options.arguments.contains("revision"))
    return fail("wait requires --revision TOKEN");
  return result;
}

std::string Help(const std::string& command) {
  std::ostringstream out;
  const auto guidance = [&]() {
    out << "Guidance on demand: synthcad docs AREA\n"
        << "  Getting started     start, skill\n"
        << "  Modeling            modeling, api, design\n"
        << "  Printing & assembly print-design, fit-and-assembly\n"
        << "  Plates & handoff     build-plates, bambu-handoff\n"
        << "  Agent review        cli, projects\n"
        << "Prints complete instructions to stdout; no skill files to install.\n"
        << "Use synthcad docs to list guides and their current bundle version.\n";
  };
  if (!command.empty()) {
    auto usage = kUsage.find(command);
    if (usage == kUsage.end()) return "Unknown command: " + command + "\n";
    out << "Usage: synthcad " << usage->second << "\n\n" << kDescriptions.at(command) << "\n";
    if (command == "docs") { out << "\n"; guidance(); }
    if (kReview.count(command)) out << "  --expect-revision TOKEN  Reject a stale displayed revision.\n";
  } else {
    out << "SynthCAD — design with an agent, review in a persistent local viewer\n\nUsage: synthcad COMMAND [OPTIONS]\n\n";
    guidance();
    for (const auto& area : std::vector<std::pair<std::string, std::vector<std::string>>>{
        {"Discovery", {"docs", "capabilities", "version"}},
        {"Projects & sessions", {"open", "sessions", "view"}},
        {"Reload & revision checks", {"state", "revision", "wait"}},
        {"Shared review", {"snapshot", "selection", "highlight", "frame", "screenshot"}}}) {
      out << "\n" << area.first << ":\n";
      for (const auto& name : area.second) out << "  " << kUsage.at(name) << "\n";
    }
    out << "\n  help [COMMAND]\n\nExamples:\n"
        << "  synthcad docs start\n"
        << "  synthcad docs print-design\n"
        << "  synthcad open \"My Project/assembly.js\" --session bracket\n"
        << "  synthcad --session bracket --json snapshot\n"
        << "  synthcad revision -s bracket --json\n"
        << "  synthcad wait -s bracket --revision TOKEN --timeout 10000\n"
        << "  synthcad highlight base lid -s bracket --frame --expect-revision TOKEN\n";
  }
  out << "\nGlobal options (before or after the command):\n"
      << "  --session, -s NAME  Address a session; required when several are running.\n"
      << "  --json             One JSON response on stdout; logs go to stderr.\n"
      << "  --timeout MS       Timeout from 0 to 300000 milliseconds (default 10000).\n"
      << "  --help, -h         Show help without starting a viewer.\n"
      << "  --version          Show version without starting a viewer.\n"
      << "  --                 Treat remaining arguments as literal values.\n\n"
      << "Exit codes: 0 success; 2 invalid_argument; 3 no_session; 4 ambiguous_session;\n"
      << "5 load_failed; 6 timeout; 7 stale_revision; 8 superseded; 9 cancelled;\n"
      << "10 io_error; 11 busy; 12 not_found.\n";
  return out.str();
}

json Capabilities() {
  json commands = json::array();
  for (const auto& entry : kUsage) commands.push_back(entry.first);
  return {{"protocolVersion", 1}, {"commands", commands},
          {"persistentSessions", true}, {"semanticSnapshots", true},
          {"revisionWait", true}, {"agentHighlights", true},
          {"screenshots", true}, {"bundledGuidance", true}, {"geometryEditing", false}, {"export", false}};
}

json Success(const std::string& command, const json& data,
             const std::string& session, const std::string& revision) {
  json response = Envelope(command, session, revision, true);
  if (!data.is_null()) response["data"] = data;
  return response;
}

json Error(const std::string& command, const std::string& code,
           const std::string& message, const json& details,
           const std::string& session, const std::string& revision) {
  json response = Envelope(command, session, revision, false);
  response["error"] = {{"code", code}, {"message", message}};
  if (!details.is_null() && !details.empty()) response["error"]["details"] = details;
  return response;
}

int ExitCode(const std::string& code) {
  static const std::map<std::string, int> codes = {
      {"invalid_argument", 2}, {"no_session", 3}, {"ambiguous_session", 4},
      {"load_failed", 5}, {"timeout", 6}, {"stale_revision", 7},
      {"superseded", 8}, {"cancelled", 9}, {"io_error", 10},
      {"busy", 11}, {"not_found", 12}};
  if (code.empty() || code == "ok") return 0;
  const auto found = codes.find(code);
  return found == codes.end() ? 1 : found->second;
}

std::string FormatResponse(const json& response, bool jsonOutput) {
  if (jsonOutput) return response.dump() + "\n";
  if (!response.value("ok", false)) {
    const auto& error = response.at("error");
    return error.value("code", "error") + ": " + error.value("message", "Request failed") + "\n";
  }
  const auto data = response.find("data");
  if (response.value("command", "") == "docs" && data != response.end()) {
    if (data->contains("content")) return data->at("content").get<std::string>();
    std::ostringstream docs;
    docs << "SynthCAD guidance — print an area with synthcad docs AREA\n\n";
    for (const auto& topic : data->at("topics"))
      docs << "  " << topic.at("topic").get<std::string>() << " — " << topic.at("title").get<std::string>() << "\n";
    docs << "\nBundle: " << data->at("bundleVersion").get<std::string>() << "\n";
    return docs.str();
  }
  if (data != response.end() && data->is_string()) return data->get<std::string>() + "\n";
  std::ostringstream out;
  out << response.value("command", "request") << ": ok";
  if (response.contains("session")) out << " (session " << response.at("session").get<std::string>() << ")";
  if (response.contains("revision")) out << " revision " << response.at("revision").get<std::string>();
  out << "\n";
  if (data != response.end() && !data->empty()) out << data->dump(2) << "\n";
  return out.str();
}

}  // namespace synthcad
