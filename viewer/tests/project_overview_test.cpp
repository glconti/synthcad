#include "project_overview.h"

#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

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
json Observation() {
    return {{"kind","fit"},{"result","inconclusive"},{"reportedBy",u8"Maker città"},
        {"details","Fit needs a second trial."},{"conditions",""},{"recordedAt","2026-10-08"}};
}
json Compatibility() {
    auto previous=Basis(nullptr); previous["modelRevision"]="older-model";
    return {{"id","interface-r2"},{"text","New mating edge needs a replacement panel."},
        {"status","requires-reprint"},{"basis",Basis(nullptr)},{"previousBasis",previous},
        {"partIds",{"panel","clip"}},{"reprintPartIds",{"panel"}},{"evidenceIds",{"fit"}}};
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

        profile={{"status","complete"},{"profileRevision","profile-a"}};
        views=json::array({{{"id","assembly"},{"loaded",true},{"modelRevision","model-a"},{"sourceCurrent",true}}});
        Check(empty["compatibilityChanges"].empty() && empty["warnings"].empty(),"sixth optional section is empty without authoring");
        auto sample=Evidence();sample["stage"]="tested";sample["sampleView"]="assembly";
        sample["sourcePartIds"]={"panel"};sample["observation"]=Observation();
        metadata={{"evidence",json::array({sample})},{"compatibilityChanges",json::array({Compatibility()})}};
        const auto authored=metadata;
        result=ProjectOverview(metadata,profile,views);
        Check(result["status"]=="valid" && metadata==authored,"projection leaves authored sample/compatibility records untouched");
        Check(result["evidence"][0]["observation"]==Observation() && result["evidence"][0]["stage"]=="tested","explicit observation remains authored and stage is retained");
        Check(result["evidence"][0]["sampleViewStatus"]=="available" && result["compatibilityChanges"][0]["evidenceStatus"]=="available","registered sample and accepted evidence links available");
        Check(result["compatibilityChanges"][0]["freshness"]=="current" && result["compatibilityChanges"][0]["previousBasis"]==Compatibility()["previousBasis"],"target basis alone determines freshness; previous basis retained verbatim");
        views[0]["loaded"]=false;views[0]["modelRevision"]=nullptr;
        result=ProjectOverview(metadata,profile,views);
        Check(result["evidence"][0]["sampleViewStatus"]=="available" && result["evidence"][0]["freshness"]=="unknown","registered unloaded sample is openable without claiming current revision");
        result=ProjectOverview(metadata,profile,json::array());
        Check(result["status"]=="valid" && result["evidence"][0]["sampleViewStatus"]=="unknown","foreign sample view does not invalidate physical history");
        auto duplicatedViews=json::array({views[0],views[0]});
        Check(ProjectOverview(metadata,profile,duplicatedViews)["evidence"][0]["sampleViewStatus"]=="unknown","ambiguous sample registry remains unknown");
        Check(ProjectOverview(metadata,profile,nullptr)["evidence"][0]["sampleViewStatus"]=="unknown","invalid sample registry remains unknown");
        views[0]["loaded"]=true;views[0]["modelRevision"]="model-a";
        for(const char *stage:{"proposed","printed"}) {
            auto bad=sample;bad["stage"]=stage;
            Check(ProjectOverview({{"evidence",json::array({bad})}},profile,views)["evidence"].empty(),"observation forbidden before explicitly tested stage");
        }
        auto superseded=sample;superseded["stage"]="superseded";
        Check(ProjectOverview({{"evidence",json::array({superseded})}},profile,views)["evidence"][0]["stage"]=="superseded","superseded observations preserve history");
        for(const json &patch:std::vector<json>{
            {{"kind","strength"}},{{"result","warning"}},{{"reportedBy",""}},{{"details",nullptr}},
            {{"conditions",17}},{{"recordedAt",""}},{{"extensions",json::array()}},{{"automatic",true}},
            {{"reportedBy",std::string("bad\0name",8)}}}) {
            auto bad=sample;bad["observation"].update(patch);
            Check(ProjectOverview({{"evidence",json::array({bad})}},profile,views)["evidence"].empty(),"strict observation fields reject malformed values");
        }
        for(const json &patch:std::vector<json>{{{"sampleView",""}},{{"sourcePartIds",json::array({17})}},{{"observation",nullptr}}}) {
            auto bad=sample;bad.update(patch);
            Check(ProjectOverview({{"evidence",json::array({bad})}},profile,views)["evidence"].empty(),"strict sample fields reject malformed values");
        }
        auto change=Compatibility();change["evidenceIds"]={"unknown-evidence"};
        result=ProjectOverview({{"compatibilityChanges",json::array({change})}},profile,views);
        Check(result["status"]=="valid" && result["compatibilityChanges"].size()==1 && result["compatibilityChanges"][0]["evidenceStatus"]=="unknown" && result["warnings"].size()==1,"missing evidence warns without silently dropping compatibility claim");
        Check(result["warnings"][0]["missingEvidenceIds"]==json::array({"unknown-evidence"}),"unresolved evidence IDs disclosed");
        change=Compatibility();change.erase("evidenceIds");change["status"]="compatible";change["reprintPartIds"]=json::array();
        result=ProjectOverview({{"compatibilityChanges",json::array({change})}},profile,views);
        Check(result["status"]=="valid" && result["compatibilityChanges"][0]["evidenceStatus"]=="available","compatible empty reprint/no evidence flow valid");
        change["status"]="unknown";change["reprintPartIds"]={"clip"};
        Check(ProjectOverview({{"compatibilityChanges",json::array({change})}},profile,views)["status"]=="valid","unknown compatibility can retain authored possible reprint list");
        for(const json &patch:std::vector<json>{
            {{"partIds",json::array()}},{{"reprintPartIds",{"foreign"}}},{{"status","requires-reprint"},{"reprintPartIds",json::array()}},
            {{"status","compatible"}},{{"evidenceIds",json::array({false})}},{{"previousBasis",nullptr}},{{"extra",true}}}) {
            auto bad=change;bad.update(patch);
            Check(ProjectOverview({{"compatibilityChanges",json::array({bad})}},profile,views)["compatibilityChanges"].empty(),"strict compatibility requirements enforced");
        }
        for(const char *key:{"basis","previousBasis","partIds","reprintPartIds"}) {
            auto bad=Compatibility();bad.erase(key);
            Check(ProjectOverview({{"compatibilityChanges",json::array({bad})}},profile,views)["compatibilityChanges"].empty(),"required compatibility field cannot be omitted");
        }
        auto bad=Compatibility();bad["previousBasis"]["unexpected"]=true;
        Check(ProjectOverview({{"compatibilityChanges",json::array({bad})}},profile,views)["compatibilityChanges"].empty(),"previous basis uses same strict schema");
        auto stale=Compatibility();stale["basis"]["modelRevision"]="target-before-edit";
        Check(ProjectOverview({{"compatibilityChanges",json::array({stale})}},profile,views)["compatibilityChanges"][0]["freshness"]=="stale","target revision change stales compatibility claim");
        metadata={{"evidence",json::array({sample,sample})},{"compatibilityChanges",json::array({17,Compatibility()})}};
        result=ProjectOverview(metadata,profile,views);
        Check(result["status"]=="partial" && result["compatibilityChanges"].size()==1 && result["compatibilityChanges"][0]["evidenceStatus"]=="unknown","rejected evidence cannot satisfy cross references; valid sibling compatibility survives");
        Check(result["warnings"][0]["path"]=="$.compatibilityChanges[1].evidenceIds","warning path follows authored index despite rejected sibling");
        metadata={{"compatibilityChanges",json::array()}};
        for(int i=0;i<1001;++i){auto item=change;item["id"]=std::to_string(i);metadata["compatibilityChanges"].push_back(item);}
        result=ProjectOverview(metadata,profile,views);
        Check(result["status"]=="partial" && result["compatibilityChanges"].size()==1000,"compatibility section uses existing bounded valid-prefix behavior");
        std::cout << "project overview tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
