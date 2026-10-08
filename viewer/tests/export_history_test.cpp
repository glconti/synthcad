#include "export_history.h"

#include "project_contract.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {
void Check(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}

void Write(const fs::path &path, const std::string &bytes) {
  fs::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  if (!output) throw std::runtime_error("test fixture write failed");
}

std::string Read(const fs::path &path) {
  std::ifstream input(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(input),
                     std::istreambuf_iterator<char>());
}

json Receipt(const fs::path &project, const fs::path &artifact,
             const fs::path &dependency, const std::string &artifactBytes,
             const std::string &dependencyBytes) {
  return {
      {"schemaVersion", 1},
      {"id", "export-test-1"},
      {"createdAt", "2026-10-08T12:34:56Z"},
      {"path", artifact.generic_u8string()},
      {"format", "3mf"},
      {"basis", {{"view", "plate-a"}, {"kind", "plate"},
                 {"modelRevision", "model-a"}, {"sourceRevision", "source-a"},
                 {"layoutRevision", "layout-a"}, {"profileRevision", nullptr},
                 {"revision", "receipt-revision-a"}}},
      {"projectPath", project.generic_u8string()},
      {"partIds", {"part-1", "part-2"}},
      {"quantities", {{{"sourcePartId", "source-1"}, {"count", 2}}}},
      {"checks", {{{"name", "Build-volume bounds"}, {"result", "passed"}}}},
      {"profile", nullptr},
      {"risks", json::array()},
      {"dependencies", {{synthcad::CanonicalPath(dependency),
                          synthcad::Sha256(dependencyBytes)}}},
      {"sha256", synthcad::Sha256(artifactBytes)},
      {"sizeBytes", artifactBytes.size()},
      {"serviceExtension", {{"futureField", true}}}};
}

json Context() {
  return {{"view", "plate-a"}, {"modelRevision", "model-a"},
          {"sourceRevision", "source-a"}, {"layoutRevision", "layout-a"},
          {"profileRevision", nullptr}};
}

void CheckSaveLoadAndFreshness(const fs::path &root) {
  const auto projectDir = root / "project";
  const auto project = projectDir / "synthcad.json";
  const auto dependency = projectDir / "plate-a.js";
  const auto artifact = root / "exports" / "plate-a.3mf";
  Write(project, R"({"schemaVersion":1,"defaultView":"plate-a","views":{"plate-a":"plate-a.js"}})");
  const std::string dependencyBytes = "export const parts = [];\n";
  const std::string artifactBytes = "test 3mf bytes";
  Write(dependency, dependencyBytes);
  Write(artifact, artifactBytes);

  auto receipt = Receipt(project, artifact, dependency, artifactBytes, dependencyBytes);
  receipt["path"] = (artifact.parent_path() / ".." / "exports" / "plate-a.3mf").generic_u8string();
  const auto saved = synthcad::SaveExportRecord(project, receipt);
  Check(saved.value("saved", false), "committed receipt should be persisted");
  const fs::path recordPath = fs::u8path(saved.at("path").get<std::string>());
  Check(fs::exists(recordPath), "save result identifies the immutable record file");
  Check(recordPath.parent_path().filename().u8string() ==
            synthcad::Sha256("synthcad-export-history-v1:" + synthcad::CanonicalPath(project)),
        "project history directory is keyed by canonical project identity");
  const auto persisted = json::parse(Read(recordPath));
  Check(!persisted.contains("artifactStatus") && !persisted.contains("freshness"),
        "derived status is not written into the immutable receipt");
  Check(persisted == receipt,
        "history file preserves the exact committed receipt, including its authored output path");
  Check(persisted.at("serviceExtension").at("futureField") == true,
        "unknown service receipt fields are retained");

  auto history = synthcad::LoadExportHistory(project);
  Check(history.at("records").size() == 1 && history.at("diagnostics").empty(),
        "load returns the valid receipt without diagnostics");
  auto loaded = history.at("records").front();
  Check(loaded.at("artifactStatus") == "current" &&
            loaded.at("dependencyStatus") == "current",
        "unchanged artifact and source dependencies are current");
  Check(loaded.at("freshness") == "unknown",
        "loading alone does not claim a current active context");
  auto refreshed = synthcad::RefreshExportHistory(history, Context());
  Check(refreshed.at("records").front().at("freshness") == "current",
        "equal known revisions and incomplete-independent profile basis are current");
  Check(refreshed.at("records").front().at("freshnessReason").is_string(),
        "freshness includes a concise reason");

  auto changedContext = Context();
  changedContext["layoutRevision"] = "layout-b";
  changedContext.erase("modelRevision");
  refreshed = synthcad::RefreshExportHistory(history, changedContext);
  Check(refreshed.at("records").front().at("freshness") == "stale",
        "a known revision mismatch takes precedence over unavailable context fields");
  changedContext = Context();
  changedContext.erase("sourceRevision");
  refreshed = synthcad::RefreshExportHistory(history, changedContext);
  Check(refreshed.at("records").front().at("freshness") == "unknown",
        "missing current basis data remains explicitly unknown");

  auto otherView = Context();
  otherView["view"] = "assembly";
  otherView["modelRevision"] = "assembly-model";
  otherView["layoutRevision"] = "assembly-layout";
  refreshed = synthcad::RefreshExportHistory(history, otherView);
  Check(refreshed.at("records").front().at("freshness") == "unknown" &&
            refreshed.at("records").front().at("freshnessReason").get<std::string>().find("Another view") != std::string::npos,
        "selecting another view leaves an unchanged receipt unknown, not stale");
  otherView["profileRevision"] = "profile-new";
  refreshed = synthcad::RefreshExportHistory(history, otherView);
  Check(refreshed.at("records").front().at("freshness") == "stale",
        "a known profile change makes a receipt stale even when another view is active");
  auto failedLoad = Context();
  failedLoad["current"] = false;
  refreshed = synthcad::RefreshExportHistory(history, failedLoad);
  Check(refreshed.at("records").front().at("freshness") == "unknown",
        "a retained basis from a failed load is never marked current");

  const auto sameHistory = synthcad::LoadExportHistory(projectDir);
  Check(sameHistory.at("records").size() == 1,
        "project directory and manifest path resolve to the same identity");
  const auto standalone = projectDir / "standalone.js";
  Write(standalone, "export default {};\n");
  Check(synthcad::LoadExportHistory(standalone).at("records").empty(),
        "standalone scenes in the same directory receive separate histories");

  auto duplicate = synthcad::SaveExportRecord(project, receipt);
  Check(!duplicate.value("saved", true) &&
            duplicate.value("error", std::string()).find("already exists") != std::string::npos,
        "an existing receipt ID is never overwritten");
  Check(json::parse(Read(recordPath)).at("createdAt") == receipt.at("createdAt"),
        "duplicate save leaves the original receipt intact");

  Write(artifact, "replaced artifact");
  Write(dependency, "changed source\n");
  history = synthcad::LoadExportHistory(project);
  loaded = history.at("records").front();
  Check(loaded.at("artifactStatus") == "changed" &&
            loaded.at("dependencyStatus") == "stale",
        "replacement output and changed dependencies are reported separately");
  fs::remove(artifact);
  history = synthcad::LoadExportHistory(project);
  Check(history.at("records").front().at("artifactStatus") == "missing",
        "a missing exported artifact has an explicit status");
}

void CheckDiagnosticsAndBounds(const fs::path &root) {
  const auto project = root / "diagnostics" / "synthcad.json";
  const auto artifact = root / "diagnostics" / "part.stl";
  const auto dependency = root / "diagnostics" / "part.js";
  Write(project, "{}");
  Write(artifact, "stl");
  Write(dependency, "source");
  const auto receipt = Receipt(project, artifact, dependency, "stl", "source");
  auto noArtifact = receipt;
  noArtifact["id"] = "missing-artifact";
  noArtifact["path"] = (root / "diagnostics" / "missing.stl").generic_u8string();
  Check(!synthcad::SaveExportRecord(project, noArtifact).value("saved", true),
        "history does not record an export before a committed artifact exists");

  auto oversized = receipt;
  oversized["id"] = "oversized-record";
  oversized["risks"] = std::string(1024 * 1024, 'x');
  Check(!synthcad::SaveExportRecord(project, oversized).value("saved", true),
        "oversized receipts are rejected instead of truncated");

  const auto history = synthcad::LoadExportHistory(project);
  Check(history.at("records").empty(), "failed receipt writes leave no history entry");

  const auto historyDir = fs::u8path(synthcad::SaveExportRecord(
      project, Receipt(project, artifact, dependency, "stl", "source"))
                                        .at("path").get<std::string>()).parent_path();
  Write(historyDir / "damaged.json", "{broken JSON");
  const auto withDamaged = synthcad::LoadExportHistory(project);
  Check(withDamaged.at("records").size() == 1 &&
            withDamaged.at("diagnostics").size() == 1,
        "a malformed record is skipped and reported while good siblings load");

  const auto wrongHistory = synthcad::RefreshExportHistory("bad", "bad");
  Check(wrongHistory.is_object() && wrongHistory.at("records").empty() &&
            wrongHistory.at("diagnostics").is_array(),
        "refresh handles malformed caller data without throwing");
}
} // namespace

int main() {
  const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
  const auto root = fs::temp_directory_path() /
                    fs::u8path("synthcad-export-history-test-" + std::to_string(nonce));
  try {
    fs::create_directories(root);
    CheckSaveLoadAndFreshness(root);
    CheckDiagnosticsAndBounds(root);
    json large={{"records",json::array()},{"diagnostics",json::array()}};
    for(int i=0;i<9;++i)large["records"].push_back({{"id",std::to_string(i)},{"detail",std::string(900000,'x')}});
    for(int i=0;i<200;++i)large["diagnostics"].push_back(std::string(2000,'d'));
    const auto published=synthcad::PublishedExportHistory(large);
    Check(published.dump().size()<=4*1024*1024&&published["records"][0]["id"]=="0"&&
      published["omittedRecords"].get<size_t>()+published["records"].size()==9&&published["omittedDiagnostics"].get<size_t>()>0,
      "published history bounds records and diagnostics explicitly without mutating stored receipts");
    std::error_code ec;
    fs::remove_all(root, ec);
    std::cout << "PASS export history persistence, bounds, and freshness\n";
  } catch (const std::exception &error) {
    std::error_code ec;
    fs::remove_all(root, ec);
    std::cerr << error.what() << '\n';
    return 1;
  }
}
