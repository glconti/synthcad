#include "plate_review_ui.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

using namespace dingcad;
using json = nlohmann::json;

namespace {
void Require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}

size_t Codepoints(const std::string &text) {
  size_t count = 0;
  for (const unsigned char byte : text)
    if ((byte & 0xc0) != 0x80) ++count;
  return count;
}

const PlateReviewTextMeasure TestMeasure = [](const std::string &text) {
  return static_cast<float>(Codepoints(text) * 8);
};

PanelInput Click(Rectangle rect) {
  PanelInput input;
  input.mouse = {rect.x + rect.width / 2.0f, rect.y + rect.height / 2.0f};
  input.pressed = true;
  input.leftDown = true;
  return input;
}

bool Inside(Rectangle inner, Rectangle outer) {
  constexpr float epsilon = 0.01f;
  return inner.x + epsilon >= outer.x && inner.y + epsilon >= outer.y &&
         inner.x + inner.width <= outer.x + outer.width + epsilon &&
         inner.y + inner.height <= outer.y + outer.height + epsilon;
}

json MinimalReport() {
  return {{"schemaVersion", 1},
          {"view", "plate-main"},
          {"kind", "plate"},
          {"basis", {{"view", "plate-main"},
                      {"modelRevision", std::string(64, 'a')},
                      {"profileRevision", nullptr}}},
          {"profileStatus", "incomplete"},
          {"profile", {{"status", "incomplete"},
                        {"activeProfile", nullptr},
                        {"profile", nullptr},
                        {"missing", {"printer", "buildVolume", "nozzleDiameter", "material"}},
                        {"missingIdentifiers", {"printer.id", "material.id"}},
                        {"provisional", json::array()},
                        {"errors", json::array()},
                        {"checkReadiness", json::object()},
                        {"slicerPresetVerified", false}}},
          {"settings", {{"partGap", {{"value", nullptr}, {"origin", "unknown"}, {"unit", "mm"}}},
                         {"brim", {{"value", nullptr}, {"origin", "unknown"}, {"unit", "mm"}}},
                         {"support", {{"value", nullptr}, {"origin", "unknown"}, {"unit", "mm"}}},
                         {"contactTolerance", {{"value", 0.0001}, {"origin", "numeric-default"}, {"unit", "mm"}}},
                         {"errors", json::array()}}},
          {"bed", nullptr},
          {"parts", json::array()},
          {"checks", json::array()},
          {"quantities", json::array()},
          {"current", true}};
}

json DetailedReport() {
  json report = MinimalReport();
  report["profileStatus"] = "incomplete";
  report["profile"] = {{"status", "incomplete"},
                        {"activeProfile", "workshop-profile"},
                        {"profileRevision", std::string(64, 'b')},
                        {"profile", {{"name", "Workshop profile"},
                                      {"printer", {{"id", "printer-x1"}, {"name", "Workshop printer"}}},
                                      {"buildVolume", {220, 200, 250}},
                                      {"nozzleDiameter", 0.4},
                                      {"material", {{"id", "pla"}, {"name", "PLA"}}}}},
                        {"missing", {"buildVolume"}},
                        {"missingIdentifiers", {"material.id"}},
                        {"provisional", {"material"}},
                        {"errors", json::array()},
                        {"checkReadiness", {{"buildVolume", {{"status", "missing"}, {"reason", "Bed limits are not confirmed."}}},
                                             {"nozzle", {{"status", "ready"}, {"reason", "Authored nozzle diameter is present."}}},
                                             {"material", {{"status", "provisional"}, {"reason", "Material selection remains provisional."}}}}},
                        {"slicerPresetVerified", false}};
  report["basis"] = {{"view", "plate-main"},
                      {"modelRevision", std::string(64, 'a')},
                      {"profileRevision", std::string(64, 'b')}};
  report["settings"] = {{"partGap", {{"value", 0.6}, {"origin", "authored"}, {"unit", "mm"}}},
                         {"brim", {{"value", 0.0}, {"origin", "authored"}, {"unit", "mm"}}},
                         {"support", {{"value", 2.0}, {"origin", "numeric-default"}, {"unit", "mm"}}},
                         {"contactTolerance", {{"value", 0.0001}, {"origin", "numeric-default"}, {"unit", "mm"}}},
                         {"errors", json::array()}};
  report["bed"] = {{"size", {220, 200, 250}},
                   {"origin", {0, 0}},
                   {"exclusions", {{{"name", "Front clips"}, {"min", {0, 0}}, {"max", {15, 8}}}}}};
  report["parts"] = {{{"id", "instance-front"},
                      {"name", "Front bracket — Città 東京 — left cable-guide retention arm for the upstream support assembly"},
                      {"sourcePartId", "bracket-source"},
                      {"rotation", {0, 0, 90}},
                      {"translation", {10, 20, 0}},
                      {"bounds", {{"min", {10, 20, 0}}, {"max", {42, 50, 8}}}},
                      {"exportable", true}}};
  report["quantities"] = {{{"sourcePartId", "bracket-source"},
                           {"expected", 2},
                           {"expectedOrigin", "authored-source-quantity"},
                           {"authoredInstances", 2},
                           {"placedInstances", 1},
                           {"platePlacements", 1},
                           {"instanceIds", {"instance-front"}},
                           {"plates", {{{"view", "plate-main"}, {"instanceId", "instance-front"}}}}}};
  const std::string longAction =
      "Move the bracket inward from the bed clip and inspect the actual footprint in the slicer. ";
  std::string repeatedAction;
  while (repeatedAction.size() < 6000) repeatedAction += longAction;
  report["checks"] = {
      {{"id", "bed-exclusion-check"},
       {"name", "Excluded bed regions"},
       {"result", "warning"},
       {"scope", "heuristic"},
       {"partIds", {"instance-front", "instance-rear"}},
       {"basis", {{"view", "plate-main"},
                  {"modelRevision", std::string(64, 'a')},
                  {"profileRevision", std::string(64, 'b')}}},
       {"method", "Conservative XY bounds compared with named exclusion rectangles."},
       {"evidence", {{"possibleConflicts", {{{"partId", "instance-front"}, {"exclusionIndex", 0}}}},
                     {"text", "Footprint may enter a clip exclusion."},
                     {"profileProvisional", true}}},
       {"nextActions", {repeatedAction}}},
      {{"id", "toolpath-check"},
       {"name", "Sliced toolpaths"},
       {"result", "not-checked"},
       {"scope", "sliced"},
       {"partIds", json::array()},
       {"basis", {{"view", "plate-main"},
                  {"modelRevision", std::string(64, 'a')},
                  {"profileRevision", nullptr}}},
       {"method", "Requires a slicer preview."},
       {"evidence", json::object()},
       {"nextActions", {"Slice with the intended machine and inspect the toolpaths."}}}};
  const auto geometryCheck = [&](const std::string &id, const std::string &name,
                                 const std::string &result) {
    return json{{"id", id}, {"name", name}, {"result", result},
                {"scope", "geometry"}, {"partIds", json::array()},
                {"basis", {{"view", "plate-main"},
                           {"modelRevision", std::string(64, 'a')},
                           {"profileRevision", std::string(64, 'b')}}},
                {"method", "Authored geometry check."},
                {"evidence", json::object()},
                {"nextActions", json::array()}};
  };
  report["checks"].push_back(geometryCheck("pass-check", "Pass after warnings", "passed"));
  report["checks"].push_back(geometryCheck("fail-check", "First failure", "failed"));
  report["checks"].push_back(geometryCheck("warning-check", "First warning", "warning"));
  report["checks"].push_back(geometryCheck("unknown-check", "Not checked geometry", "not-checked"));
  return report;
}

void CheckLayoutAndContent() {
  const Font noFont{};
  const auto report = DetailedReport();
  PlateReviewUi ui;
  ui.open = true;
  const auto layout = ui.Layout(report, 640, 400, noFont, TestMeasure);
  Require(layout.card.x == 12 && layout.card.y == 12 &&
              layout.card.width == 520 && layout.card.height == 300,
          "The plate review card uses the overview size and 75 percent height cap");
  Require(Inside(layout.card, {0, 0, 640, 400}) &&
              Inside(layout.header, layout.card) && Inside(layout.close, layout.header) &&
              Inside(layout.body, layout.card),
          "The plate review card controls stay within the viewport");
  Require(layout.contentHeight > layout.body.height && layout.maxScroll > 0,
          "Long check actions remain accessible through scrolling");

  const auto has = [&](const std::string &text) {
    return std::find(layout.document.begin(), layout.document.end(), text) !=
           layout.document.end();
  };
  for (const auto *text : {"Active plate", "Printer and profile", "Plate check settings",
                           "Build plate", "Placed parts", "Intended quantities",
                           "Checks by scope", "Heuristic checks", "Sliced checks"})
    Require(has(text), "Plate review sections are labeled");
  Require(has("Check results: 1 failed · 2 warning · 2 not checked · 1 passed"),
          "A compact summary brings current failures above verbose part details");
  for (const auto *text : {"View: plate-main", "Kind: plate",
                           "Profile status: incomplete",
                           "Active profile: Workshop profile (workshop-profile)",
                           "Printer: Workshop printer (printer-x1)",
                           "Material: PLA (pla)", "Nozzle: 0.4 mm",
                           "Missing profile fields: buildVolume",
                           "Missing identifiers: material.id",
                           "Provisional profile fields: material",
                           "Slicer preset verification: Not verified.",
                           "Part gap: 0.6 mm (authored)",
                           "Support: 2 mm (numeric default (provisional))",
                           "Bed dimensions: 220, 200, 250 mm",
                           "Bed origin: 0, 0 mm",
                           "Front clips: 0, 0 to 15, 8 mm",
                           "Orientation: 0, 0, 90 degrees",
                           "Placement: 10, 20, 0 mm",
                           "Bounds: 10, 20, 0 to 42, 50, 8 mm",
                           "bracket-source: expected 2",
                           "Expected quantity source: authored source-part quantity",
                           "Authored instances: 2 · placed instances: 1 · plate placements: 1",
                           "Basis: view plate-main · model aaaaaaaaaaaa… · profile bbbbbbbbbbbb…",
                           "Method: Conservative XY bounds compared with named exclusion rectangles.",
                           "Evidence / Text: Footprint may enter a clip exclusion.",
                           "Not in this view: instance-rear — open the corresponding assembly/plate.",
                           "Excluded bed regions — Warning",
                           "Sliced toolpaths — Not checked"})
    Require(has(text), "Readable plate, profile, quantity, and check details are exposed");
  const auto indexOf = [&](const std::string &text) {
    return std::find(layout.document.begin(), layout.document.end(), text) -
           layout.document.begin();
  };
  Require(indexOf("First failure — Failed") < indexOf("First warning — Warning") &&
              indexOf("First warning — Warning") < indexOf("Not checked geometry — Not checked") &&
              indexOf("Not checked geometry — Not checked") < indexOf("Pass after warnings — Passed"),
          "Each scope presents failed, warning, not-checked, then passed checks");

  const auto fullAction = std::find_if(layout.rows.begin(), layout.rows.end(),
                                       [](const PlateReviewRow &row) {
                                         return row.text.rfind("- Move the bracket", 0) == 0;
                                       });
  Require(fullAction != layout.rows.end() && fullAction->text.size() > 6000,
          "Complete next-action text remains in the review document");
  std::string wrapped;
  for (const auto &line : fullAction->lines) wrapped += line;
  Require(wrapped == fullAction->text,
          "Wrapped authored action text does not lose characters");
  Require(std::none_of(layout.document.begin(), layout.document.end(),
                       [](const std::string &row) {
                         return row.find(std::string(64, 'a')) != std::string::npos ||
                                row.find('{') != std::string::npos;
                       }),
          "Check bases shorten hashes and common values avoid raw JSON");
  const auto longName = std::find_if(
      layout.rows.begin(), layout.rows.end(), [](const PlateReviewRow &row) {
        return row.text.rfind("Front bracket — Città 東京 — left cable-guide", 0) == 0;
      });
  Require(longName != layout.rows.end() && longName->lines.size() > 1,
          "Long Unicode part names wrap instead of being clipped");
  std::string wrappedName;
  for (const auto &line : longName->lines) wrappedName += line;
  Require(wrappedName == longName->text,
          "Wrapped part names retain their complete authored text");
}

void CheckActionsCurrentStateAndCapture() {
  const Font noFont{};
  auto report = DetailedReport();
  const auto original = report;
  PlateReviewUi ui;
  ui.open = true;
  auto layout = ui.Layout(report, 900, 700, noFont, TestMeasure);
  auto highlight = std::find_if(
      layout.rows.begin(), layout.rows.end(),
      [](const PlateReviewRow &row) { return row.highlightButton && row.enabled; });
  Require(highlight != layout.rows.end() &&
              highlight->highlightIds == std::vector<std::string>{"instance-front"},
          "Affected-part controls preserve order and include only parts in this view");
  Require(std::find(layout.document.begin(), layout.document.end(),
                    "Not in this view: instance-rear — open the corresponding assembly/plate.") !=
              layout.document.end(),
          "Report IDs absent from this view remain visible as navigation guidance");
  ui.scroll = std::clamp(highlight->bounds.y - layout.body.y - 4.0f,
                         0.0f, layout.maxScroll);
  layout = ui.Layout(report, 900, 700, noFont, TestMeasure);
  highlight = std::find_if(layout.rows.begin(), layout.rows.end(),
                           [](const PlateReviewRow &row) {
                             return row.highlightButton && row.enabled;
                           });
  Require(highlight != layout.rows.end() &&
              Inside(highlight->bounds, layout.body),
          "The affected-part action can be scrolled into the body viewport");
  const auto action = ui.Update(report, Click(highlight->bounds), noFont,
                                900, 700, TestMeasure);
  Require(action.highlight == std::vector<std::string>{"instance-front"} &&
              action.frame,
          "A current affected-part click returns highlight and frame actions");
  Require(report == original,
          "Review actions do not mutate report, human selection, or source metadata");

  layout = ui.Layout(report, 900, 700, noFont, TestMeasure);
  const auto close = ui.Update(report, Click(layout.close), noFont,
                               900, 700, TestMeasure);
  Require(close.close, "Close returns a close action");
  Require(ui.CapturesMouse(Click(layout.card), 900, 700),
          "The open review card captures pointers inside its bounds");

  PanelInput press;
  press.mouse = {layout.card.x + 90, layout.card.y + 80};
  press.pressed = true;
  press.leftDown = true;
  ui.Update(report, press, noFont, 900, 700, TestMeasure);
  PanelInput drag;
  drag.mouse = {890, 690};
  drag.leftDown = true;
  ui.Update(report, drag, noFont, 900, 700, TestMeasure);
  Require(ui.CapturesMouse(drag, 900, 700),
          "A card gesture remains captured when dragged outside the card");
  PanelInput release;
  release.mouse = drag.mouse;
  ui.Update(report, release, noFont, 900, 700, TestMeasure);
  Require(!ui.CapturesMouse(release, 900, 700),
          "Gesture capture ends when released outside the card");

  report["current"] = false;
  report["diagnostic"] = "The latest plate evaluation failed; earlier geometry remains visible.";
  layout = ui.Layout(report, 900, 700, noFont, TestMeasure);
  Require(std::find(layout.document.begin(), layout.document.end(),
                    "NOT CURRENT — displayed geometry was retained from an earlier state.") !=
              layout.document.end() &&
              std::find(layout.document.begin(), layout.document.end(),
                        "Diagnostic: The latest plate evaluation failed; earlier geometry remains visible.") !=
                  layout.document.end(),
          "Retained geometry displays a prominent stale report and full diagnostic");
  Require(std::find(layout.document.begin(), layout.document.end(),
                    "Last check results: 1 failed · 2 warning · 2 not checked · 1 passed (report not current)") !=
              layout.document.end(),
          "A stale report labels its result counts as historical");
  const auto staleButton = std::find_if(
      layout.rows.begin(), layout.rows.end(),
      [](const PlateReviewRow &row) { return row.highlightButton; });
  Require(staleButton != layout.rows.end() && !staleButton->enabled,
          "Stale reports disable affected-part highlight controls");
  const auto staleAction = ui.Update(report, Click(staleButton->bounds), noFont,
                                     900, 700, TestMeasure);
  Require(staleAction.highlight.empty() && !staleAction.frame,
          "Stale reports do not return camera or highlight actions");
}

void CheckEmptyMalformedAndScroll() {
  const Font noFont{};
  auto report = MinimalReport();
  PlateReviewUi ui;
  ui.open = true;
  auto layout = ui.Layout(report, 640, 400, noFont, TestMeasure);
  Require(std::count(layout.document.begin(), layout.document.end(), "None recorded.") >= 3,
          "Empty standalone plate arrays are stated briefly");
  Require(std::find(layout.document.begin(), layout.document.end(),
                    "Bed dimensions: Unknown; printer bed data is not recorded.") !=
              layout.document.end(),
          "A missing printer profile leaves bed dimensions unknown");
  for (const auto *text : {"Printer: Missing from profile",
                           "Nozzle: Missing from profile",
                           "Missing profile fields: printer, buildVolume, nozzleDiameter, material",
                           "Slicer preset verification: Not verified."})
    Require(std::find(layout.document.begin(), layout.document.end(), text) !=
                layout.document.end(),
            "An incomplete profile clearly identifies missing printer setup");

  report["bed"] = {{"size", {220, 200}}, {"exclusions", "malformed"}};
  layout = ui.Layout(report, 640, 400, noFont, TestMeasure);
  Require(std::find(layout.document.begin(), layout.document.end(),
                    "Bed dimensions: Unknown; bed data is incomplete.") !=
              layout.document.end(),
          "Malformed bed dimensions are represented as unknown");
  report["bed"] = {{"size", {220, 220, 250}}, {"exclusions", nullptr}};
  report["current"] = true;
  report["profile"]["profile"] = {{"buildVolume", {220, 220, 250}}};
  layout = ui.Layout(report, 640, 400, noFont, TestMeasure);
  Require(CanDrawPlateBed(report), "Build volume alone provides drawable plate geometry");
  Require(std::find(layout.document.begin(), layout.document.end(),
                    "Bed exclusions: Unknown; exclusion checks are not checked.") != layout.document.end(),
          "Unknown exclusion list is never presented as none recorded");
  report["bed"]["exclusions"] = json::array();
  layout = ui.Layout(report, 640, 400, noFont, TestMeasure);
  Require(std::find(layout.document.begin(), layout.document.end(), "Bed exclusions: None recorded.") != layout.document.end(),
          "Explicit empty exclusion list is distinguished from unknown");
  DrawPlateBed({{"kind", "plate"}});
  DrawPlateBed({{"kind", "plate"}, {"bed", nullptr}, {"current", true}});
  DrawPlateBed({{"kind", "plate"},
                {"bed", {{"size", {220, 200}}, {"exclusions", "malformed"}}},
                {"current", true}});
  const json staleBed{{"bed", {{"size", {220, 200, 250}}}},
                      {"kind", "plate"}, {"current", false}};
  DrawPlateBed(staleBed);
  DrawPlateBed({{"kind", "assembly"},
                {"bed", {{"size", {220, 200, 250}}}},
                {"current", true}});

  auto huge = DetailedReport();
  const std::string note = "Città 東京 — authored check context. ";
  std::string many;
  while (many.size() < 7000) many += note;
  huge["checks"][1]["nextActions"] = {many};
  layout = ui.Layout(huge, 640, 400, noFont, TestMeasure);
  Require(layout.maxScroll > 0, "Long Unicode checks remain scrollable");
  PanelInput wheel;
  wheel.mouse = {layout.body.x + 12, layout.body.y + 12};
  wheel.wheel = -10000;
  ui.Update(huge, wheel, noFont, 640, 400, TestMeasure);
  layout = ui.Layout(huge, 640, 400, noFont, TestMeasure);
  Require(std::abs(ui.scroll - layout.maxScroll) < 0.01f,
          "Plate review scrolling reaches the last action line");
  wheel.wheel = 10000;
  ui.Update(huge, wheel, noFont, 640, 400, TestMeasure);
  Require(ui.scroll == 0, "Plate review scrolling stays bounded at the first line");
}

void CheckBedRenderSafety() {
  auto report = DetailedReport();
  Require(CanDrawPlateBed(report),
          "A current plate with finite translated bed extents can be drawn");

  auto atRangeLimit = report;
  atRangeLimit["bed"]["size"] = {1.0e12, 1.0e12, 1.0e12};
  Require(CanDrawPlateBed(atRangeLimit),
          "The explicit render-coordinate boundary has finite frame distances");

  auto oversized = report;
  oversized["bed"]["size"][0] = 1.0e100;
  Require(!CanDrawPlateBed(oversized),
          "A finite JSON bed dimension that overflows viewer coordinates is rejected");
  PlateReviewUi ui;
  ui.open = true;
  const auto layout = ui.Layout(oversized, 900, 700, Font{}, TestMeasure);
  Require(std::find(layout.document.begin(), layout.document.end(),
                    "Bed dimensions exceed viewer rendering range; numeric check context remains available.") !=
              layout.document.end(),
          "The card explains when valid numeric bed data cannot be rendered");

  auto translatedOutOfRange = report;
  translatedOutOfRange["bed"]["origin"] = {1.0e12, 0.0};
  Require(!CanDrawPlateBed(translatedOutOfRange),
          "Translated bed extents are checked against the viewer range");

  auto stale = report;
  stale["current"] = false;
  Require(!CanDrawPlateBed(stale),
          "Stale bed geometry is never offered for current rendering");
  auto missingCurrent = report;
  missingCurrent.erase("current");
  Require(!CanDrawPlateBed(missingCurrent),
          "Bed geometry is not rendered without an explicit current report state");
  auto assembly = report;
  assembly["kind"] = "assembly";
  Require(!CanDrawPlateBed(assembly),
          "Printer bed context in an assembly is not rendered as plate geometry");
}
}  // namespace

int main() {
  try {
    CheckLayoutAndContent();
    CheckActionsCurrentStateAndCapture();
    CheckEmptyMalformedAndScroll();
    CheckBedRenderSafety();
    std::cout << "PASS plate review layout, detail content, actions, and bed aids\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
