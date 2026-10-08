#include "project_overview_ui.h"

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
  for (unsigned char byte : text)
    if ((byte & 0xc0) != 0x80) ++count;
  return count;
}

const ProjectOverviewTextMeasure TestMeasure = [](const std::string &text) {
  return static_cast<float>(Codepoints(text) * 8);
};

PanelInput Click(Rectangle rect) {
  PanelInput input;
  input.mouse = Vector2{rect.x + rect.width / 2.0f,
                        rect.y + rect.height / 2.0f};
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

json EmptyOverview() {
  json readiness = json::object();
  readiness["buildVolume"] = {{"status", "missing"},
                               {"reason", "Volume and exclusions are not set"}};
  readiness["nozzle"] = {{"status", "missing"}, {"reason", "Nozzle is not set"}};
  readiness["material"] = {{"status", "missing"}, {"reason", "Material is not set"}};

  json profile = {{"status", "incomplete"},
                  {"activeProfile", nullptr},
                  {"profile", nullptr},
                  {"missing", {"printer", "buildVolume", "nozzleDiameter", "material"}},
                  {"missingIdentifiers", {"printer.id", "material.id"}},
                  {"provisional", json::array()},
                  {"errors", json::array()},
                  {"checkReadiness", std::move(readiness)},
                  {"slicerPresetVerified", false}};
  return {{"name", "Workshop project"},
          {"path", "C:/models/workshop/synthcad.json"},
          {"standalone", false},
          {"views", json::array()},
          {"activeView", nullptr},
          {"displayedView", nullptr},
          {"geometryStatus", "ready"},
          {"geometryDiagnostic", ""},
          {"profile", std::move(profile)},
          {"measurements", json::array()},
          {"assumptions", json::array()},
          {"checks", json::array()},
          {"exports", json::array()},
          {"evidence", json::array()},
          {"errors", json::array()},
          {"status", "valid"},
          {"modelRevision", "rev-abc123"}};
}

void CheckLayoutAndEmptyProfile() {
  const Font noFont{};
  auto overview = EmptyOverview();
  overview["metadataCurrent"] = false;
  overview["geometryDiagnostic"] =
      "Required manifest field profiles.custom.buildVolume is missing.";
  ProjectOverviewUi ui;
  ui.open = true;
  const auto layout = ui.Layout(overview, 640, 400, noFont, TestMeasure);
  Require(layout.card.x == 12 && layout.card.y == 12 &&
              layout.card.width == 520 && layout.card.height == 300,
          "The overview card uses the specified top-left size and 75 percent height cap");
  Require(Inside(layout.card, {0, 0, 640, 400}),
          "The overview card stays inside the viewport");
  Require(Inside(layout.header, layout.card) && Inside(layout.close, layout.header) &&
              Inside(layout.body, layout.card),
          "Header, Close, and scroll body stay inside the card");
  Require(layout.contentHeight > layout.body.height && layout.maxScroll > 0,
          "Even the empty project overview has scrollable, clearly labeled sections");

  for (const auto [width, height] : {std::pair{1280, 720}, std::pair{320, 180},
                                     std::pair{40, 40}}) {
    const auto small = ui.Layout(overview, width, height, noFont, TestMeasure);
    Require(Inside(small.card, {0, 0, static_cast<float>(width),
                                static_cast<float>(height)}),
            "The overview card remains inside compact viewports");
    Require(small.card.width <= 520.0f && small.card.height <= height * 0.75f + 0.01f,
            "Card width and height respect their maximums");
    Require(Inside(small.header, small.card) && Inside(small.close, small.header) &&
                Inside(small.body, small.card),
            "Compact controls and body remain within the card");
  }

  const auto hasDocumentText = [&](const std::string &text) {
    return std::find(layout.document.begin(), layout.document.end(), text) !=
           layout.document.end();
  };
  for (const auto *heading : {"Project", "Views", "Geometry status",
                              "Printer profile", "Measurements", "Assumptions",
                              "Checks", "Exports", "Evidence", "Metadata errors"})
    Require(hasDocumentText(heading), "Every overview section is labeled");
  Require(std::count(layout.document.begin(), layout.document.end(), "None recorded.") >= 6,
          "Empty view and metadata sections say that nothing was recorded");
  Require(hasDocumentText("Profile setup is incomplete. Ask your agent to run synthcad docs profiles."),
          "An absent profile shows the requested setup guidance");
  Require(hasDocumentText(
              "Diagnostic: Required manifest field profiles.custom.buildVolume is missing."),
          "A nonempty geometry diagnostic appears near geometry status");
  Require(hasDocumentText("Showing last loaded project metadata"),
          "Stale project metadata is identified near its status");
  Require(std::any_of(layout.document.begin(), layout.document.end(),
                      [](const std::string &row) {
                        return row.find("Slicer preset verification: Unknown") != std::string::npos;
                      }),
          "The overview makes unverified slicer preset status explicit");
}

void CheckViewCloseAndGestures() {
  const Font noFont{};
  auto overview = EmptyOverview();
  overview["views"] = {{{"id", "assembly"}, {"name", "Main assembly"},
                        {"kind", "assembly"}, {"entry", "assembly.js"},
                        {"loaded", true}, {"modelRevision", "rev-a"},
                        {"sourceCurrent", true}},
                       {{"id", "inspection"}, {"name", "Inspection"},
                        {"kind", "inspection"}, {"entry", "inspection.js"},
                        {"loaded", false}, {"modelRevision", nullptr},
                        {"sourceCurrent", false}}};
  overview["activeView"] = "assembly";
  overview["displayedView"] = "assembly";

  ProjectOverviewUi ui;
  ui.open = true;
  auto layout = ui.Layout(overview, 800, 600, noFont, TestMeasure);
  Require(layout.viewButtons.size() == 2 && layout.viewButtons[0].first == "assembly" &&
              layout.viewButtons[1].first == "inspection",
          "View buttons keep the stable authored view IDs");
  Require(layout.rows[0].section, "The project heading starts the document");
  const auto activeButton = std::find_if(layout.rows.begin(), layout.rows.end(),
                                         [](const ProjectOverviewRow &row) {
                                           return row.viewButton && row.viewId == "assembly";
                                         });
  Require(activeButton != layout.rows.end() && activeButton->selected,
          "The displayed view has a selected indicator");
  Require(std::any_of(layout.document.begin(), layout.document.end(),
                      [](const std::string &row) {
                        return row.find("not loaded") != std::string::npos;
                      }),
          "An unvisited view is labeled not loaded even when its source changed");
  Require(std::none_of(layout.document.begin(), layout.document.end(),
                       [](const std::string &row) {
                         return row.find("not loaded") == 0 &&
                                row.find("revision") != std::string::npos;
                       }),
          "An unvisited view does not imply that a model revision is loaded");
  const auto action = ui.Update(overview, Click(layout.viewButtons[1].second), noFont,
                                800, 600, TestMeasure);
  Require(action.view == "inspection" && !action.close,
          "Clicking a view returns its stable ID without changing JSON");
  Require(overview["displayedView"] == "assembly",
          "UI input does not mutate the overview data");

  layout = ui.Layout(overview, 800, 600, noFont, TestMeasure);
  const auto close = ui.Update(overview, Click(layout.close), noFont, 800, 600,
                               TestMeasure);
  Require(close.close, "The Close control returns a close action");
  Require(ui.CapturesMouse(Click(layout.card), 800, 600),
          "A pointer inside the open card is captured");

  PanelInput press;
  press.mouse = Vector2{layout.card.x + 80, layout.card.y + 80};
  press.pressed = true;
  press.leftDown = true;
  ui.Update(overview, press, noFont, 800, 600, TestMeasure);
  Require(ui.CapturesMouse(press, 800, 600), "A card press is captured");
  PanelInput drag;
  drag.mouse = Vector2{790, 590};
  drag.leftDown = true;
  ui.Update(overview, drag, noFont, 800, 600, TestMeasure);
  Require(ui.CapturesMouse(drag, 800, 600),
          "A gesture remains captured when dragged outside the card");
  PanelInput release;
  release.mouse = drag.mouse;
  ui.Update(overview, release, noFont, 800, 600, TestMeasure);
  Require(!ui.CapturesMouse(release, 800, 600),
          "Gesture capture ends on release outside the card");
}

void CheckReadableCommonMetadata() {
  const Font noFont{};
  auto overview = EmptyOverview();
  overview["views"] = {{{"id", "assembly-view-long-stable-id"},
                        {"name", "Main assembly"},
                        {"kind", "assembly"},
                        {"loaded", true},
                        {"modelRevision", "view-revision-long-abcdef"},
                        {"sourceCurrent", true}}};
  overview["activeView"] = "assembly-view-long-stable-id";
  overview["displayedView"] = "assembly-view-long-stable-id";
  overview["profile"] = {
      {"status", "complete"},
      {"activeProfile", std::string(64, 'a')},
      {"profileRevision", std::string(64, 'b')},
      {"missing", json::array()},
      {"missingIdentifiers", json::array()},
      {"provisional", json::array()},
      {"errors", json::array()},
      {"slicerPresetVerified", false},
      {"checkReadiness", json::object()},
      {"profile",
       {{"name", "Calibrated workshop printer"},
        {"printer", {{"id", std::string(64, 'c')},
                     {"name", "Workshop printer"}}},
        {"buildVolume", {220, 200, 250}},
        {"exclusions", {{{"name", "Front clip"},
                         {"min", {0, 0}},
                         {"max", {12, 8}}}}},
        {"nozzleDiameter", 0.4},
        {"material", {{"id", std::string(64, 'd')}, {"name", "PLA"}}},
        {"provenance", {{"type", "manufacturer"},
                         {"description", "Dimensions transcribed from the printed assembly guide."},
                         {"source", "Workshop manual, page 14"}}},
        {"slicer", {{"id", "prusa-slicer-production-id-long"},
                    {"version", "2.8.1"},
                    {"presetId", "mk4-preset-long-identifier"}}}}}};
  overview["checks"].push_back(
      {{"id", "check-long-identifier-123456"},
       {"name", "Assembly clearance"},
       {"result", "pass"},
       {"scope", "geometry"},
       {"freshness", "current"},
       {"reason", "Revision matches."},
       {"basis", {{"view", "assembly"},
                   {"modelRevision", std::string(64, 'e')},
                   {"profileRevision", std::string(64, 'f')}}}});
  const std::string mixedIdentifier = "12345678901\xc3\xa9-suffix";
  overview["measurements"].push_back(
      {{"id", mixedIdentifier}, {"name", "Boundary test"},
       {"value", 1}, {"unit", "mm"}, {"status", "measured"}});

  ProjectOverviewUi ui;
  ui.open = true;
  const auto layout = ui.Layout(overview, 800, 600, noFont, TestMeasure);
  const auto has = [&](const std::string &text) {
    return std::find(layout.document.begin(), layout.document.end(), text) !=
           layout.document.end();
  };
  Require(has("Active view: Main assembly") && has("Displayed view: Main assembly"),
          "View summaries use authored names rather than stable IDs");
  Require(has("Active profile: aaaaaaaaaaaa…") &&
              has("Profile identity: Calibrated workshop printer (aaaaaaaaaaaa…)"),
          "Profile identity is readable and long IDs are shortened");
  Require(has("Printer: Workshop printer (cccccccccccc…)"),
          "Printer identity uses its name and a short identifier");
  Require(has("Build volume: 220 × 200 × 250 mm") && has("Nozzle: 0.4 mm") &&
              has("Material: PLA (dddddddddddd…)"),
          "Common profile dimensions and material are shown in readable units");
  Require(has("Provenance: manufacturer") &&
              has("Description: Dimensions transcribed from the printed assembly guide.") &&
              has("Source: Workshop manual, page 14"),
          "Provenance is presented as authored type, description, and source");
  Require(has("Front clip: 0, 0 to 12, 8 mm"),
          "Bed exclusions are presented as named coordinate bounds");
  Require(!std::any_of(layout.document.begin(), layout.document.end(),
                       [](const std::string &row) {
                         return row.find("slicerPresetVerified") != std::string::npos;
                       }) &&
              has("Slicer preset verification: Unknown; a preset has not been verified."),
          "Internal preset verification fields are not serialized into the common profile view");
  Require(has("Basis: view assembly · model eeeeeeeeeeee… · profile ffffffffffff…"),
          "Check basis is shown with shortened model and profile revisions");
  Require(has(mixedIdentifier + " · Boundary test — 1 mm"),
          "A multibyte character after eleven ASCII bytes is retained in a full authored identifier");

  for (const auto &row : layout.rows) {
    if (row.text.rfind("Profile identity:", 0) == 0 ||
        row.text.rfind("Printer:", 0) == 0 ||
        row.text.rfind("Build volume:", 0) == 0 ||
        row.text.rfind("Nozzle:", 0) == 0 ||
        row.text.rfind("Material:", 0) == 0 ||
        row.text.rfind("Provenance:", 0) == 0 ||
        row.text.rfind("Description:", 0) == 0 ||
        row.text.rfind("Source:", 0) == 0 ||
        row.text.rfind("Front clip:", 0) == 0 ||
        row.text.rfind("Basis:", 0) == 0)
      Require(row.text.find('{') == std::string::npos &&
                  row.text.find('[') == std::string::npos,
              "Common profile and check rows do not expose serializer syntax");
  }
}

void CheckUnicodeContentAndScrolling() {
  const Font noFont{};
  auto overview = EmptyOverview();
  const std::string unicode = "\xc3\xa9 \xe6\x9d\xb1\xe4\xba\xac \xf0\x9f\x8c\x8d";
  std::string longNote;
  while (longNote.size() < 6000) longNote += unicode + " authored note ";
  overview["measurements"].push_back(
      {{"id", "measure-1"}, {"name", "Caf\xc3\xa9 \xe6\x9d\xb1\xe4\xba\xac"},
       {"value", 32.5}, {"unit", "mm"}, {"status", "measured"},
       {"notes", longNote}, {"origin", "authored"}});
  overview["assumptions"].push_back(
      {{"id", "assumption-1"}, {"text", "\xe9\x9b\xaa: confirm fit with the mating part"},
       {"status", "provisional"}, {"origin", "authored"}});
  overview["checks"].push_back(
      {{"id", "check-1"}, {"name", "Bed fit"}, {"result", "warning"},
       {"scope", "geometry"}, {"details", "No fit result is authored yet."},
       {"freshness", "unknown"}, {"reason", "No loaded basis is available."},
       {"origin", "authored"}});
  overview["exports"].push_back(
      {{"id", "export-1"}, {"path", "C:/out/part.stl"}, {"format", "stl"},
       {"freshness", "stale"}, {"reason", "Model revision changed."},
       {"origin", "authored"}});
  overview["evidence"].push_back(
      {{"id", "evidence-1"}, {"text", "\xd0\xbf\xd1\x80\xd0\xbe\xd0\xb2\xd0\xb5\xd1\x80\xd0\xba\xd0\xb0"},
       {"stage", "tested"}, {"freshness", "current"},
       {"reason", "Revision matches."}, {"origin", "authored"}});

  ProjectOverviewUi ui;
  ui.open = true;
  auto layout = ui.Layout(overview, 640, 400, noFont, TestMeasure);
  const std::string fullNotes = "Notes: " + longNote;
  const auto noteRow = std::find_if(layout.rows.begin(), layout.rows.end(),
                                    [](const ProjectOverviewRow &row) {
                                      return row.text.rfind("Notes: ", 0) == 0;
                                    });
  Require(noteRow != layout.rows.end() && noteRow->text == fullNotes,
          "Long authored Unicode notes remain complete in the document model");
  Require(std::any_of(layout.document.begin(), layout.document.end(),
                      [](const std::string &row) {
                        return row.find("measure-1 · Caf\xc3\xa9") == 0 &&
                               row.find("32.5 mm") != std::string::npos;
                      }),
          "Measurement values display their authored unit");
  std::string wrapped;
  for (const auto &line : noteRow->lines) wrapped += line;
  Require(wrapped == fullNotes && Codepoints(wrapped) < fullNotes.size(),
          "Measured wrapping retains all Unicode characters without splitting code points");
  for (const auto *needle : {"Caf\xc3\xa9 \xe6\x9d\xb1\xe4\xba\xac", "\xe9\x9b\xaa: confirm fit with the mating part",
                             "Freshness: unknown — No loaded basis is available.",
                             "Freshness: stale — Model revision changed.",
                             "Freshness: current — Revision matches."}) {
    Require(std::any_of(layout.document.begin(), layout.document.end(),
                        [&](const std::string &row) {
                          return row.find(needle) != std::string::npos;
                        }),
            "Authored values and freshness reasons appear in the project sections");
  }

  Require(layout.maxScroll > 0, "Long overview content is scrollable");
  PanelInput wheel;
  wheel.mouse = Vector2{layout.body.x + 16, layout.body.y + 16};
  wheel.wheel = -10000;
  ui.Update(overview, wheel, noFont, 640, 400, TestMeasure);
  layout = ui.Layout(overview, 640, 400, noFont, TestMeasure);
  Require(std::abs(ui.scroll - layout.maxScroll) < 0.01f,
          "Scrolling reaches the last line of the long overview");
  wheel.wheel = 10000;
  ui.Update(overview, wheel, noFont, 640, 400, TestMeasure);
  Require(ui.scroll == 0, "Scrolling is bounded at the first line");
}
}  // namespace

int main() {
  try {
    CheckLayoutAndEmptyProfile();
    CheckViewCloseAndGestures();
    CheckReadableCommonMetadata();
    CheckUnicodeContentAndScrolling();
    std::cout << "PASS project overview layout, content, input, and scrolling\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
