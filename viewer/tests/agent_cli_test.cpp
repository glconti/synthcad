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
      "timeout", "stale_revision", "superseded", "cancelled", "io_error", "busy", "not_found", "stale_cursor"};
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
  Require(synthcad::Capabilities().at("guidedPicking") == true &&
          synthcad::Capabilities().at("sessionEvents") == true, "guided selection discovery missing");
  Require(synthcad::Help("pick").find("--question") != std::string::npos &&
          synthcad::Help("events").find("stale_cursor") != std::string::npos,
          "guided selection help missing");
  Require(synthcad::Help().find("Printing & assembly") != std::string::npos &&
          synthcad::Help("docs").find("bambu-handoff") != std::string::npos, "area help missing");
  const auto guide = synthcad::Success("docs", {{"content", u8"# Pièce\n\nExact instructions.\n"}});
  Require(synthcad::FormatResponse(guide, false) == u8"# Pièce\n\nExact instructions.\n", "guide stdout must be raw text");
  Require(nlohmann::json::parse(synthcad::FormatResponse(guide, true)) == guide, "guide JSON envelope changed");
}
void CheckGuidedSelectionParsing() {
  using synthcad::ParseCli;
  for (const auto* kind : {"part", "surface", "edge", "vertex"}) {
    auto pick = ParseCli({"-s", "bracket", "pick", "--id", "choose-1", "--kind", kind,
                          "--question", u8"Which pièce should move?", "--expect-revision", "rev", "--json"});
    Require(bool(pick) && pick.options.arguments == nlohmann::json({{"id", "choose-1"},
            {"kind", kind}, {"question", u8"Which pièce should move?"}}), "pick payload changed");
    Require(pick.options.expectRevision == "rev" && pick.options.session == "bracket" &&
            pick.options.jsonOutput, "pick context missing");
  }
  auto boundary = ParseCli({"pick", "--id", std::string(128, 'i'), "--kind", "part",
                            "--question", std::string(4096, 'q')});
  Require(bool(boundary), "maximum pick field lengths must parse");
  std::string unicodeId;
  for (int i = 0; i < 64; ++i) unicodeId += u8"é";
  Require(bool(ParseCli({"pick", "--id", unicodeId, "--kind", "part", "--question", "q"})),
          "UTF-8 ID at byte limit must parse");
  unicodeId += u8"é";
  Require(!ParseCli({"pick", "--id", unicodeId, "--kind", "part", "--question", "q"}),
          "request ID limit must count UTF-8 bytes");
  for (const auto* command : {"pick-status", "pick-cancel"}) {
    auto query = ParseCli({command, "--", "-caller-id"});
    Require(bool(query) && query.options.arguments == nlohmann::json({{"id", "-caller-id"}}),
            "pick query ID missing");
    Require(!ParseCli({command}) && !ParseCli({command, ""}) &&
            !ParseCli({command, "a", "b"}) && !ParseCli({command, "a", "--expect-revision", "rev"}) &&
            !ParseCli({command, std::string(129, 'i')}) &&
            !ParseCli({command, std::string("a\0b", 3)}), "invalid pick query accepted");
  }
  auto events = ParseCli({"events", "--after", "0"});
  Require(bool(events) && events.options.arguments == nlohmann::json({{"after", "0"}, {"waitMs", 0}}),
          "event default payload changed");
  events = ParseCli({"events", "--after=opaque:cursor", "--wait", "300000", "--timeout", "2"});
  Require(bool(events) && events.options.arguments == nlohmann::json({{"after", "opaque:cursor"}, {"waitMs", 300000}}) &&
          events.options.timeoutMs == 2, "event wait and global timeout must remain independent");
  Require(bool(ParseCli({"events", "--after", "cursor", "--wait", "0"})), "zero event wait must parse");
  for (const auto& args : std::vector<std::vector<std::string>>{
      {"pick"}, {"pick", "--id", "i", "--kind", "part"},
      {"pick", "--id", "i", "--question", "q"}, {"pick", "--kind", "part", "--question", "q"},
      {"pick", "--id", "i", "--kind", "face", "--question", "q"},
      {"pick", "--id", "", "--kind", "part", "--question", "q"},
      {"pick", "--id", std::string(129, 'i'), "--kind", "part", "--question", "q"},
      {"pick", "--id", std::string("a\0b", 3), "--kind", "part", "--question", "q"},
      {"pick", "--id", "i", "--kind", "part", "--question", ""},
      {"pick", "--id", "i", "--kind", "part", "--question", std::string(4097, 'q')},
      {"pick", "--id", "i", "--kind", "part", "--question", std::string("a\0b", 3)},
      {"pick", "extra", "--id", "i", "--kind", "part", "--question", "q"},
      {"pick", "--id", "i", "--id", "i", "--kind", "part", "--question", "q"},
      {"events"}, {"events", "--after="}, {"events", "--after", "0", "extra"},
      {"events", "--after", "0", "--wait=-1"}, {"events", "--after", "0", "--wait=300001"},
      {"events", "--after", "0", "--wait=1.0"}, {"events", "--after", "0", "--wait=999999999999"},
      {"events", "--after", "0", "--expect-revision", "rev"},
      {"state", "--wait", "1"}, {"wait", "--revision", "rev", "--after", "0"},
      {"snapshot", "--id", "i"}, {"selection", "--kind", "part"}, {"reference", "token", "--question", "q"}})
    Require(!ParseCli(args), "invalid guided selection options accepted");
  Require(ParseCli({"pick", "--help"}).options.help &&
          ParseCli({"events", "--help"}).options.help, "guided help must omit required fields");
}
}

int main() {
  try {
    CheckParsing();
    CheckGuidedSelectionParsing();
    CheckResponses();
    std::cout << "Agent CLI tests passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
