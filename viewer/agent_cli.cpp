#include "agent_cli.h"
#include "agent_knowledge.h"
#include "project_contract.h"

#include <algorithm>
#include <charconv>
#include <filesystem>
#include <map>
#include <set>
#include <sstream>

namespace synthcad {
namespace {
using nlohmann::json;
const json& Registry() {
  static const json registry = json::parse(ReadDoc("cli_commands").at("content").get<std::string>());
  return registry;
}
const json* Node(const std::string& path) {
  for (const auto& node : Registry().at("nodes"))
    if (node.at("path") == path) return &node;
  return nullptr;
}
std::string Parent(const std::string& path) {
  const auto space = path.rfind(' ');
  return space == std::string::npos ? "" : path.substr(0, space);
}
json Children(const std::string& path) {
  json children = json::array();
  for (const auto& node : Registry().at("nodes")) {
    const auto child = node.at("path").get<std::string>();
    if (!child.empty() && Parent(child) == path)
      children.push_back({{"path", child}, {"kind", node.at("kind")}, {"summary", node.at("summary")}});
  }
  return children;
}
std::string Usage(const json& node) {
  std::string usage = "synthcad-cli";
  const auto path = node.at("path").get<std::string>();
  if (!path.empty()) usage += " " + path;
  for (const auto& arg : node.at("arguments")) {
    std::string name = arg.at("name");
    if (arg.at("multiple").get<bool>()) name += "...";
    usage += " " + (arg.at("required").get<bool>() ? name : "[" + name + "]");
  }
  for (const auto& option : node.at("options")) {
    const std::string name = option;
    const auto& spec = Registry().at("options").at(name);
    std::string item = name;
    if (spec.contains("valueName")) item += " " + spec.at("valueName").get<std::string>();
    const auto& required = node.at("requiredOptions");
    const bool mandatory = std::find(required.begin(), required.end(), option) != required.end();
    usage += " " + (mandatory ? item : "[" + item + "]");
  }
  if (node.at("kind") != "action") usage += " [--help]";
  return usage;
}
std::string Unknown(const std::string& path, const std::string& token) {
  if (path.empty() && Registry().at("removed").contains(token))
    return "Removed command: " + token + ". Use synthcad-cli " + Registry().at("removed").at(token).get<std::string>() + ".";
  std::string message = "Unknown command: " + (path.empty() ? "" : path + " ") + token + ". Choose:";
  for (const auto& child : Children(path)) message += " " + child.at("path").get<std::string>() + ";";
  return message + " read synthcad-cli" + (path.empty() ? "" : " " + path) + " --help.";
}
json Envelope(const std::string& command, const std::string& session,
              const std::string& revision, bool ok) {
  json result = {{"protocolVersion", 1}, {"ok", ok}, {"command", command}};
  if (!session.empty()) result["session"] = session;
  if (!revision.empty()) result["revision"] = revision;
  return result;
}
void Rename(json& object, const char* from, const char* to) {
  if (object.is_object() && object.contains(from)) {
    object[to] = object.at(from);
    object.erase(from);
  }
}
}  // namespace

bool IsCliRoot(const std::string& token) {
  return (!token.empty() && Node(token)) || Registry().at("removed").contains(token);
}
bool IsProjectPath(const std::string& token) {
  if (token.empty() || token.front() == '-' || IsCliRoot(token)) return false;
  const auto path = std::filesystem::u8path(token);
  std::error_code error;
  return std::filesystem::exists(path, error) || token.find('/') != std::string::npos ||
      token.find('\\') != std::string::npos || path.extension() == ".js" || path.filename() == "synthcad.json";
}

CliParseResult ParseCli(const std::vector<std::string>& arguments) {
  CliParseResult result;
  auto& options = result.options;
  options.jsonOutput = std::find(arguments.begin(), arguments.end(), "--json") != arguments.end();
  std::vector<std::string> words;
  std::vector<bool> literals;
  std::map<std::string, json> flags;
  bool literal = false;
  auto fail = [&](const std::string& message) { result.error = message; return result; };
  for (size_t i = 0; i < arguments.size(); ++i) {
    const auto& token = arguments[i];
    if (!literal && token == "--") { literal = true; continue; }
    if (!literal && !token.empty() && token.front() == '-') {
      const auto equal = token.find('=');
      std::string name = token.substr(0, equal);
      if (name == "-h") name = "--help";
      if (name == "--session" || name == "-s") return fail("Removed option: " + name + ". Use --project NAME, or project open PATH --name NAME.");
      if (flags.count(name)) return fail("Repeated option: " + name);
      if (name == "--version") {
        if (equal != std::string::npos) return fail("Option does not accept a value: " + name);
        flags[name] = true; options.version = true; continue;
      }
      if (!Registry().at("options").contains(name)) return fail("Unknown option: " + name + ". Read synthcad-cli --help.");
      const auto& spec = Registry().at("options").at(name);
      if (spec.at("type") == "boolean") {
        if (equal != std::string::npos) return fail("Option does not accept a value: " + name);
        flags[name] = true;
      } else {
        std::string value;
        if (equal != std::string::npos) value = token.substr(equal + 1);
        else if (i + 1 < arguments.size() && !arguments[i + 1].empty() && arguments[i + 1].front() != '-') value = arguments[++i];
        if (value.empty()) return fail("Missing value for " + name);
        if (spec.at("type") == "integer") {
          int number = 0;
          const auto parsed = std::from_chars(value.data(), value.data() + value.size(), number);
          if (parsed.ec != std::errc() || parsed.ptr != value.data() + value.size() || number < spec.at("minimum").get<int>() || number > spec.at("maximum").get<int>())
            return fail(name + " must be an integer from " + spec.at("minimum").dump() + " to " + spec.at("maximum").dump());
          flags[name] = number;
        } else {
          if (spec.contains("choices") && std::find(spec.at("choices").begin(), spec.at("choices").end(), value) == spec.at("choices").end())
            return fail("Invalid value for " + name + "; choose " + spec.at("choices").dump());
          flags[name] = value;
        }
      }
    } else { words.push_back(token); literals.push_back(literal); }
  }
  options.jsonOutput = flags.count("--json") != 0;
  options.help = flags.count("--help") != 0;
  if (options.version) {
    options.command = "--version";
    if (!words.empty() || flags.size() > (flags.count("--json") ? 2u : 1u)) return fail("--version only accepts --json.");
    return result;
  }
  const json* node = Node("");
  size_t position = 0;
  while (position < words.size() && node->at("kind") != "action") {
    const auto candidate = options.command.empty() ? words[position] : options.command + " " + words[position];
    const auto* child = literals[position] ? nullptr : Node(candidate);
    if (!child) return fail(Unknown(options.command, words[position]));
    node = child; options.command = candidate; ++position;
  }
  options.operation = node->at("operation");
  for (const auto& entry : flags) {
    const auto& name = entry.first;
    const auto& spec = Registry().at("options").at(name);
    const auto& allowed = node->at("options");
    if (spec.value("scope", "") != "global" && std::find(allowed.begin(), allowed.end(), name) == allowed.end())
      return fail(name + " is not valid for " + (options.command.empty() ? "root help" : options.command) + ". Read its --help.");
    if (name == "--project") options.session = entry.second.get<std::string>();
    else if (name == "--name") options.session = entry.second.get<std::string>();
    else if (name == "--expect-revision") options.expectRevision = entry.second.get<std::string>();
    else if (name == "--timeout") options.timeoutMs = entry.second.get<int>();
    else if (name != "--json" && name != "--help") options.arguments[spec.at("key").get<std::string>()] = entry.second;
  }
  if (options.operation == "open" && flags.count("--project")) return fail("project open takes --name NAME; use --project NAME for subsequent actions.");
  if (options.arguments.value("template", false) && !options.expectRevision.empty()) return fail("--template does not read a displayed revision; omit --expect-revision.");
  if (node->at("kind") != "action" || options.help) { options.help = true; return result; }
  for (const auto& required : node->at("requiredOptions"))
    if (!flags.count(required.get<std::string>())) return fail(options.command + " requires " + required.get<std::string>() + ". Usage: " + Usage(*node));
  for (const auto& arg : node->at("arguments")) {
    const auto key = arg.at("key").get<std::string>();
    if (arg.at("multiple").get<bool>()) {
      options.arguments[key] = json::array();
      while (position < words.size()) {
        if (words[position].empty()) return fail("Arguments must not be empty. Usage: " + Usage(*node));
        options.arguments[key].push_back(words[position++]);
      }
    } else if (position < words.size()) {
      if (words[position].empty()) return fail("Arguments must not be empty. Usage: " + Usage(*node));
      options.arguments[key] = words[position++];
    } else if (arg.at("required").get<bool>()) return fail("Missing " + arg.at("name").get<std::string>() + ". Usage: " + Usage(*node));
  }
  if (position < words.size()) return fail("Unexpected argument: " + words[position] + ". Usage: " + Usage(*node));
  const auto& op = options.operation;
  if (op == "selection" && options.arguments.contains("reference")) options.operation = "reference";
  if (op == "highlight" || op == "frame") {
    const bool hasIds = !options.arguments.at("partIds").empty();
    if (op == "highlight" && options.arguments.value("clear", false) == hasIds) return fail("review highlight requires either part IDs or --clear.");
    if (op == "frame" && options.arguments.value("selection", false) && hasIds) return fail("--selection cannot be combined with part IDs.");
  }
  if (op == "export") {
    const auto path = options.arguments.at("path").get<std::string>();
    if (path.size() > 4096 || path.find('\0') != std::string::npos) return fail("Export path must contain 1..4096 bytes without NUL.");
    if (!options.arguments.contains("format")) {
      auto extension = std::filesystem::u8path(path).extension().u8string();
      for (auto& c : extension) if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
      if (extension != ".3mf" && extension != ".stl") return fail("Use a .3mf or .stl destination, or specify --format.");
      options.arguments["format"] = extension.substr(1);
    }
    for (const auto* key : {"visibleOnly", "replace", "allowWarnings", "dryRun"})
      if (!options.arguments.contains(key)) options.arguments[key] = false;
  }
  if (op == "pick" || op == "pick-status" || op == "pick-cancel") {
    const auto id = options.arguments.at("id").get<std::string>();
    if (id.empty() || id.size() > 128 || id.find('\0') != std::string::npos) return fail("Request ID must contain 1..128 UTF-8 bytes without NUL.");
    if (op == "pick") {
      const auto question = options.arguments.at("question").get<std::string>();
      if (question.empty() || question.size() > 4096 || question.find('\0') != std::string::npos) return fail("--question must contain 1..4096 UTF-8 bytes without NUL.");
    }
  }
  if (op == "events" && !options.arguments.contains("waitMs")) options.arguments["waitMs"] = 0;
  return result;
}

json Capabilities() { return Registry().at("capabilities"); }
json HelpData(const std::string& command) {
  const auto* node = Node(command);
  if (!node) throw KnowledgeError("not_found", "Unknown help path: " + command);
  json data = {{"path",command},{"kind",node->at("kind")},{"summary",node->at("summary")},
    {"usage",Usage(*node)},{"children",Children(command)},{"arguments",node->at("arguments")},
    {"requirements",node->at("requirements")},{"examples",node->at("examples")},{"nextSteps",node->at("nextSteps")},
    {"options",json::array()},{"sources",json::array()}};
  for (const auto& entry : Registry().at("options").items()) {
    const auto& allowed = node->at("options");
    if (entry.value().value("scope", "") != "global" && std::find(allowed.begin(), allowed.end(), entry.key()) == allowed.end()) continue;
    json option = entry.value(); option.erase("key"); option["name"] = entry.key();
    const auto& required = node->at("requiredOptions");
    option["required"] = std::find(required.begin(), required.end(), entry.key()) != required.end();
    data["options"].push_back(option);
  }
  if (command.empty()) {
    data["capabilities"] = Capabilities();
    data["options"].push_back({{"name","--version"},{"type","boolean"},{"description","Read build and CLI protocol versions without a viewer."},{"required",false}});
  }
  std::string guidance = node->at("guidance");
  for (const auto& topic : node->at("guides")) {
    auto guide = ReadDoc(topic.get<std::string>());
    if (!guidance.empty()) guidance += "\n\n";
    guidance += guide.at("content").get<std::string>();
    guide.erase("content"); guide.erase("bundleVersion"); guide.erase("bundleHash");
    data["sources"].push_back(guide);
  }
  const auto bundle = ListDocs();
  data["guidance"] = guidance;
  data["hash"] = Sha256(guidance);
  data["bundleHash"] = bundle.at("bundleHash");
  data["bundleVersion"] = bundle.at("bundleVersion");
  return data;
}
std::string Help(const std::string& command) {
  const auto data = HelpData(command);
  std::ostringstream out;
  out << data.at("guidance").get<std::string>() << "\n\nUsage: " << data.at("usage").get<std::string>() << "\n";
  if (!data.at("examples").empty()) {
    out << "\nExamples:\n";
    for (const auto& example : data.at("examples")) out << "  " << example.at("command").get<std::string>() << "\n";
  }
  if (!data.at("children").empty()) {
    out << "\nExplore next (append --help for instructions):\n";
    for (const auto& child : data.at("children")) out << "  " << child.at("path").get<std::string>() << " — " << child.at("summary").get<std::string>() << "\n";
  }
  if (!data.at("requirements").empty()) {
    out << "\nRequirements:\n";
    for (const auto& requirement : data.at("requirements")) out << "  " << requirement.get<std::string>() << "\n";
  }
  out << "\nOptions (before or after the command):\n";
  for (const auto& option : data.at("options")) {
    out << "  " << option.at("name").get<std::string>();
    if (option.contains("valueName")) out << " " << option.at("valueName").get<std::string>();
    out << "  " << option.at("description").get<std::string>() << "\n";
  }
  if (!data.at("nextSteps").empty()) {
    out << "\nRelated instructions:\n";
    for (const auto& next : data.at("nextSteps")) out << "  synthcad-cli " << next.get<std::string>() << " --help\n";
  }
  return out.str();
}

json Success(const std::string& command, const json& data, const std::string& session, const std::string& revision) {
  json response = Envelope(command, session, revision, true);
  if (!data.is_null()) response["data"] = data;
  return response;
}
json Error(const std::string& command, const std::string& code, const std::string& message,
           const json& details, const std::string& session, const std::string& revision) {
  json response = Envelope(command, session, revision, false);
  response["error"] = {{"code", code}, {"message", message}};
  if (!details.is_null() && !details.empty()) response["error"]["details"] = details;
  return response;
}
json CliResponse(json response, const std::string& command) {
  response["protocolVersion"] = 2;
  response["command"] = command;
  Rename(response, "session", "project");
  // Only these operation results contain transport-owned project identities.
  if (response.contains("data")) {
    auto& data = response["data"];
    if (command == "project open") Rename(data, "session", "project");
    if (command == "project list") {
      Rename(data, "sessions", "projects");
      if (data.contains("projects")) for (auto& record : data["projects"]) Rename(record, "session", "project");
    }
  }
  if (response.contains("error")) {
    auto& error = response["error"];
    const auto code = error.value("code", "");
    if (code == "no_session") {
      error["code"] = "no_project";
      error["message"] = "No matching open project. Run synthcad-cli project list or project open PATH.";
    } else if (code == "ambiguous_session") {
      error["code"] = "ambiguous_project";
      error["message"] = "Several projects are open; use --project NAME from synthcad-cli project list.";
    }
    if (error.contains("details")) {
      auto& details = error["details"];
      if (code == "ambiguous_session") Rename(details, "sessions", "projects");
      if (command == "project open") { Rename(details, "session", "project"); Rename(details, "existingSession", "existingProject"); }
    }
    // Translate transport messages only, leaving evaluation diagnostics and authored strings intact.
    if (command == "project open") {
      auto message = error.value("message", "");
      const std::vector<std::pair<std::string,std::string>> phrases = {
        {"Project is already open as session ","Project is already open as "},
        {"Session name belongs to another project: ","Project handle belongs to another project: "},
        {"Project is already opening as session ","Project is already opening as "},
        {"An existing project session cannot currently be reached.","The existing open project cannot currently be reached."}};
      for (const auto& phrase : phrases) if (message.rfind(phrase.first, 0) == 0) message.replace(0, phrase.first.size(), phrase.second);
      error["message"] = message;
    }
  }
  return response;
}
int ExitCode(const std::string& code) {
  static const std::map<std::string, int> codes = {
      {"invalid_argument",2},{"no_session",3},{"no_project",3},{"ambiguous_session",4},{"ambiguous_project",4},
      {"load_failed",5},{"timeout",6},{"stale_revision",7},{"superseded",8},{"cancelled",9},
      {"io_error",10},{"busy",11},{"not_found",12},{"stale_cursor",13},
      {"warnings_present",14},{"destination_exists",15},{"empty_export",16}};
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
  if (response.value("command", "") == "print profile" && data != response.end() && data->contains("profiles")) return data->dump(2) + "\n";
  if (data != response.end() && data->is_string()) return data->get<std::string>() + "\n";
  std::ostringstream out;
  out << response.value("command", "request") << ": ok";
  if (response.contains("project")) out << " (project " << response.at("project").get<std::string>() << ")";
  if (response.contains("revision")) out << " revision " << response.at("revision").get<std::string>();
  out << "\n";
  if (data != response.end() && !data->empty()) out << data->dump(2) << "\n";
  return out.str();
}
}  // namespace synthcad
