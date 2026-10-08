#include "project_overview.h"

#include <iostream>
#include <limits>
#include <stdexcept>

using nlohmann::json;
using synthcad::ProjectOverview;
namespace {
void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
json Basis(json profile = "profile-a") {
    return {{"view", "assembly"}, {"modelRevision", "model-a"}, {"profileRevision", profile}};
}
json Evidence() {
    return {{"id", "fit"}, {"text", u8"Verificare città 日本"}, {"stage", "proposed"},
            {"basis", Basis()}, {"attachments", {"does-not-exist.png"}}};
}
}
int main() {
    try {
        json profile = {{"status", "complete"}, {"profileRevision", "profile-a"}};
        json views = json::array({{{"id", "assembly"}, {"loaded", true},
                                  {"modelRevision", "model-a"}, {"sourceCurrent", true}}});
        auto empty = ProjectOverview(json::object(), json::object(), json::array());
        Check(empty["status"] == "valid" && empty["evidence"].empty() && empty["errors"].empty(), "standalone metadata is empty and valid");
        json metadata = {{"evidence", json::array({Evidence()})}};
        auto result = ProjectOverview(metadata, profile, views);
        Check(result["evidence"][0]["freshness"] == "current", "matching basis current");
        Check(result["evidence"][0]["stage"] == "proposed" && result["evidence"][0]["origin"] == "authored", "proposed evidence never inferred tested");
        Check(result["evidence"][0]["text"] == Evidence()["text"], "Unicode authored text preserved");
        Check(result["evidence"][0]["attachments"] == Evidence()["attachments"], "unopened attachment path preserved");

        views[0]["sourceCurrent"] = false;
        Check(ProjectOverview(metadata, profile, views)["evidence"][0]["freshness"] == "stale", "source change stales record");
        views[0]["sourceCurrent"] = true;
        views[0]["modelRevision"] = "model-b";
        Check(ProjectOverview(metadata, profile, views)["evidence"][0]["freshness"] == "stale", "model change stales record");
        views[0]["modelRevision"] = "model-a";
        profile["profileRevision"] = "profile-b";
        Check(ProjectOverview(metadata, profile, views)["evidence"][0]["freshness"] == "stale", "profile change stales record");
        profile["status"] = "incomplete";
        Check(ProjectOverview(metadata, profile, views)["evidence"][0]["freshness"] == "stale", "known profile mismatch overrides incomplete status");
        Check(ProjectOverview(metadata, profile, json::array())["evidence"][0]["freshness"] == "stale", "known profile mismatch stales unvisited views");
        auto unvisited = views;
        unvisited[0]["loaded"] = false; unvisited[0]["modelRevision"] = nullptr;
        Check(ProjectOverview(metadata, profile, unvisited)["evidence"][0]["freshness"] == "stale", "known profile mismatch does not require geometry evaluation");
        profile["profileRevision"] = "profile-a";
        Check(ProjectOverview(metadata, profile, views)["evidence"][0]["freshness"] == "unknown", "incomplete profile unknown");
        metadata["evidence"][0]["basis"]["profileRevision"] = nullptr;
        Check(ProjectOverview(metadata, profile, views)["evidence"][0]["freshness"] == "current", "explicit null profile-independent basis");
        Check(ProjectOverview(metadata, profile, json::array())["evidence"][0]["freshness"] == "unknown", "unknown view");
        views[0]["loaded"] = false; views[0]["modelRevision"] = nullptr;
        Check(ProjectOverview(metadata, profile, views)["evidence"][0]["freshness"] == "unknown", "unvisited view");
        views[0]["sourceCurrent"] = false;
        Check(ProjectOverview(metadata, profile, views)["evidence"][0]["freshness"] == "unknown", "unvisited source state never supplies loaded revision");
        metadata["evidence"][0].erase("basis");
        Check(ProjectOverview(metadata, profile, views)["evidence"][0]["freshness"] == "unbound", "missing basis unbound");

        json measurement = {{"id", "width"}, {"name", "Width"}, {"value", 20.5}, {"unit", "mm"}, {"status", "provisional"}, {"notes", ""}, {"extensions", {{"instrument", "caliper"}}}};
        metadata = {{"measurements", json::array({measurement, 17, {{"id", "bad"}}})}, {"checks", "bad section"}};
        result = ProjectOverview(metadata, profile, views);
        Check(result["status"] == "partial" && result["measurements"].size() == 1 && result["errors"].size() > 1, "malformed siblings isolated");
        Check(result["measurements"][0]["extensions"] == measurement["extensions"], "extension metadata preserved");
        Check(result["checks"].empty(), "invalid root section excluded");
        metadata["measurements"] = json::array({measurement, measurement});
        result = ProjectOverview(metadata, profile, views);
        Check(result["measurements"].empty(), "all duplicate IDs rejected");
        measurement["id"] = "other";
        metadata["measurements"].push_back(measurement);
        Check(ProjectOverview(metadata, profile, views)["measurements"].size() == 1, "unique sibling retained among duplicates");
        measurement["unexpected"] = true;
        metadata = {{"measurements", json::array({measurement})}};
        Check(ProjectOverview(metadata, profile, views)["measurements"].empty(), "unknown record fields rejected");
        measurement.erase("unexpected");
        measurement["value"] = std::numeric_limits<double>::infinity();
        metadata["measurements"] = json::array({measurement});
        Check(ProjectOverview(metadata, profile, views)["measurements"].empty(), "nonfinite measurements rejected");
        measurement["value"] = 1;
        measurement["name"] = std::string("bad\0name", 8);
        metadata["measurements"] = json::array({measurement});
        Check(ProjectOverview(metadata, profile, views)["measurements"].empty(), "embedded NUL rejected");
        measurement["name"] = std::string("\xc0\xaf", 2);
        metadata["measurements"] = json::array({measurement});
        Check(ProjectOverview(metadata, profile, views)["measurements"].empty(), "invalid UTF-8 rejected");
        metadata = {{"evidence", json::array({Evidence()})}};
        metadata["evidence"][0]["basis"].erase("profileRevision");
        Check(ProjectOverview(metadata, profile, views)["evidence"].empty(), "missing profile basis does not imply independent");
        metadata = {{"evidence", json::array({Evidence()})}};
        metadata["evidence"][0]["basis"]["other"] = "custom";
        Check(ProjectOverview(metadata, profile, views)["evidence"].empty(), "unknown basis field rejected");
        metadata = {{"evidence", json::array({Evidence()})}};
        metadata["evidence"][0]["attachments"] = json::array({17});
        Check(ProjectOverview(metadata, profile, views)["evidence"].empty(), "invalid attachment element rejected");
        metadata = {{"evidence", json::array({Evidence()})}};
        metadata["evidence"][0]["text"] = std::string(10001, 'x');
        Check(ProjectOverview(metadata, profile, views)["evidence"].empty(), "oversized text rejected");
        metadata = {{"evidence", json::array()}};
        for (int i = 0; i < 1001; ++i) {
            auto record = Evidence(); record["id"] = std::to_string(i);
            metadata["evidence"].push_back(record);
        }
        result = ProjectOverview(metadata, profile, views);
        Check(result["status"] == "partial" && result["evidence"].size() == 1000, "section limit retains valid prefix with diagnostic");
        Check(ProjectOverview(nullptr, profile, views)["status"] == "invalid", "invalid metadata root diagnosed");

        metadata = {{"checks", json::array({{{"id", "author-pass"}, {"name", "Fit"}, {"result", "passed"}, {"scope", "physical"}}})},
                    {"exports", json::array({{{"id", "mesh"}, {"path", "missing.stl"}, {"format", "stl"}}})},
                    {"assumptions", json::array({{{"id", "material"}, {"text", "Material provisional"}, {"status", "provisional"}}})}};
        result = ProjectOverview(metadata, profile, views);
        Check(result["status"] == "valid" && result["checks"][0]["origin"] == "authored", "authored passed result not engine verified");
        Check(result["exports"][0]["path"] == "missing.stl", "export path not opened");
        Check(result["assumptions"][0]["status"] == "provisional", "assumption state preserved");
        std::cout << "project overview tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
