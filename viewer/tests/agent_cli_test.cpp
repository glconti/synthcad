#include "agent_cli.h"

#include <iostream>
#include <stdexcept>

namespace {
void Require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
void CheckParsing() {
  using synthcad::ParseCli;
  auto open = ParseCli({"--json", "-s", "bracket", "open", "My Project/scene.js", "--hidden"});
  Require(bool(open), "open with global flags must parse");
  Require(open.options.session == "bracket" && open.options.jsonOutput, "global values missing");
  Require(open.options.arguments.at("path") == "My Project/scene.js", "path must retain spaces");
  Require(open.options.arguments.at("hidden") == true, "hidden flag missing");
  auto wait = ParseCli({"wait", "--revision=abc", "--timeout", "0", "--session=bracket"});
  Require(bool(wait) && wait.options.timeoutMs == 0, "zero-time wait must parse");
  Require(wait.options.arguments.at("revision") == "abc", "revision token missing");
  Require(ParseCli({"state", "--timeout", "300000"}).options.timeoutMs == 300000, "maximum timeout must parse");
  auto frame = ParseCli({"frame", "--expect-revision", "abc", "base", "lid"});
  Require(bool(frame) && frame.options.arguments.at("partIds").size() == 2, "part IDs missing");
  Require(frame.options.expectRevision == "abc", "review revision guard missing");
  auto reference = ParseCli({"reference", "scsel1.1234", "--expect-revision", "abc"});
  Require(bool(reference) && reference.options.arguments.at("reference") == "scsel1.1234" &&
          reference.options.expectRevision == "abc", "reference resolution arguments missing");
  Require(!ParseCli({"reference"}) && !ParseCli({"reference", ""}) &&
          !ParseCli({"reference", "a", "b"}), "invalid reference arguments accepted");
  auto literal = ParseCli({"open", "--", "--scene.js"});
  Require(bool(literal) && literal.options.arguments.at("path") == "--scene.js", "literal path missing");
  for (const auto& args : std::vector<std::vector<std::string>>{
      {"export"}, {"open"}, {"open", "a", "b"}, {"wait"},
      {"wait", "--revision"}, {"wait", "--revision", "a", "--timeout", "-1"},
      {"wait", "--revision", "a", "--timeout", "12x"},
      {"state", "--timeout", "300001"},
      {"wait", "--revision", "a", "--timeout", "99999999999999"},
      {"snapshot", "--unknown"}, {"snapshot", "--hidden"},
      {"snapshot", "--json=true"}, {"snapshot", "-s", "a", "--session", "b"},
      {"frame", "base", "--selection"}, {"highlight", "base", "--clear"},
      {"highlight"}, {"open", "a", "--expect-revision", "b"},
      {"screenshot", ""}, {"--version", "snapshot"}, {"--session", "a"}}) {
    Require(!ParseCli(args), "invalid CLI input accepted");
  }
  Require(ParseCli({}).options.help, "empty invocation must show help");
  Require(ParseCli({"open", "--help"}).options.help, "command help must omit required positionals");
  Require(ParseCli({"help", "wait"}).options.command == "wait", "help command target missing");
  Require(ParseCli({"--version", "--json"}).options.command == "version", "version discovery missing");
  Require(bool(ParseCli({"highlight", "--clear"})), "highlight clear must parse");
  Require(bool(ParseCli({"frame"})), "whole-scene framing must parse");
  Require(bool(ParseCli({"docs"})), "guidance discovery must parse");
  auto docs = ParseCli({"docs", "print-design", "--json"});
  Require(bool(docs) && docs.options.arguments.at("topic") == "print-design", "guide topic missing");
  Require(!ParseCli({"docs", "a", "b"}) && !ParseCli({"docs", ""}) &&
          !ParseCli({"docs", "start", "--replace"}), "invalid guidance arguments accepted");
}
void CheckResponses() {
  auto success = synthcad::Success("snapshot", {{"parts", nlohmann::json::array()}}, "bracket", "abc");
  Require(success.at("protocolVersion") == 1 && success.at("ok") == true, "success envelope invalid");
  Require(success.at("session") == "bracket" && success.at("revision") == "abc", "response identity missing");
  Require(!success.contains("error"), "success must not contain an error");
  auto failure = synthcad::Error("wait", "superseded", "A newer revision exists", {{"requested", "abc"}});
  Require(failure.at("ok") == false && failure.at("error").at("details").at("requested") == "abc",
          "error details missing");
  Require(!failure.contains("data") && !failure.contains("session"), "optional envelope fields leaked");
  auto encoded = synthcad::FormatResponse(success, true);
  Require(nlohmann::json::parse(encoded) == success && encoded.back() == '\n', "JSON stdout is not one complete response");
  Require(synthcad::FormatResponse(failure, false).find("superseded: ") == 0, "human error category missing");
  const std::vector<std::string> codes = {"invalid_argument", "no_session", "ambiguous_session", "load_failed",
      "timeout", "stale_revision", "superseded", "cancelled", "io_error", "busy", "not_found"};
  for (size_t i = 0; i < codes.size(); ++i)
    Require(synthcad::ExitCode(codes[i]) == int(i) + 2, "exit code contract changed");
  Require(synthcad::ExitCode("") == 0 && synthcad::ExitCode("unrecognized") != 0, "unknown error must fail");
  Require(synthcad::Help().find("synthcad wait") != std::string::npos, "help needs wait example");
  Require(synthcad::Help("screenshot").find("--replace") != std::string::npos, "command help missing options");
  Require(synthcad::Capabilities().at("geometryEditing") == false, "discovery must not promise geometry edits");
  Require(synthcad::Capabilities().at("export") == false, "discovery must not promise exports");
  Require(synthcad::Capabilities().at("selectionReferences") == true &&
          synthcad::Help("reference").find("reference TOKEN") != std::string::npos,
          "reference discovery missing");
  Require(synthcad::Capabilities().at("bundledGuidance") == true, "guidance discovery missing");
  Require(synthcad::Help().find("Printing & assembly") != std::string::npos &&
          synthcad::Help("docs").find("bambu-handoff") != std::string::npos, "area help missing");
  const auto guide = synthcad::Success("docs", {{"content", u8"# Pièce\n\nExact instructions.\n"}});
  Require(synthcad::FormatResponse(guide, false) == u8"# Pièce\n\nExact instructions.\n", "guide stdout must be raw text");
  Require(nlohmann::json::parse(synthcad::FormatResponse(guide, true)) == guide, "guide JSON envelope changed");
}
}

int main() {
  try {
    CheckParsing();
    CheckResponses();
    std::cout << "Agent CLI tests passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
