#include "manufacturing_checks.h"
#include "printer_profile.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace dingcad;
using nlohmann::json;
namespace {
void Require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
json Profile() {
    const json selected = {
        {"printer", {{"id", "custom"}}}, {"buildVolume", {100, 100, 100}}, {"exclusions", json::array()},
        {"nozzleDiameter", .4}, {"material", {{"id", "pla"}}}, {"provenance", {{"type", "user"}}}};
    return synthcad::PrinterProfileContext({{"activeProfile", "custom"}, {"profiles", {{"custom", selected}}}});
}
DisplayPart Part(const std::string& id, const manifold::Manifold& solid, bool exportable = true) {
    DisplayPart part;
    part.solid = std::make_shared<manifold::Manifold>(solid); part.color = BLUE;
    part.id = part.name = id; part.exportable = exportable;
    part.sourcePartId = "source"; part.sourceSolid = part.solid;
    return part;
}
json Basis() { return {{"view", "plate"}, {"modelRevision", "model"}, {"sourceRevision", "source"}, {"profileRevision", "profile"}}; }
json Design(const std::vector<std::string>& ids = {"a"}, const std::string& kind = "plate") {
    json instances = json::array(), members = json::array();
    for (const auto& id : ids) { instances.push_back({{"id", id}, {"part", "source"}, {"exportable", true}}); members.push_back({{"instance", id}}); }
    return {{"activeView", "plate"}, {"sourceParts", {{{"id", "source"}, {"exportable", true}}}}, {"instances", instances},
        {"groups", json::array()}, {"views", {{{"id", "plate"}, {"kind", kind}, {"members", members}}}}};
}
json Settings(double gap = 2, double brim = 0, double support = 0) {
    return {{"plateSettings", {{"plate", {{"partGap", gap}, {"brim", brim}, {"support", support}}}}}};
}
const json& Check(const json& report, const std::string& id) {
    for (const auto& check : report.at("checks")) if (check.at("id") == id) return check;
    throw std::runtime_error("Missing check " + id);
}
std::string Result(const json& report, const std::string& id) { return Check(report, id).at("result"); }
json Run(const std::vector<DisplayPart>& parts, json design = Design(), json profile = Profile(), json settings = Settings()) {
    return ManufacturingChecks(parts, design, profile, settings, Basis());
}
}
int main() {
    try {
        const auto cube = manifold::Manifold::Cube({10, 10, 10});
        const auto a = Part("a", cube.Translate({10, 10, 0}));
        const auto b = Part("b", cube.Translate({30, 10, 0}));
        const auto good = Run({a, b}, Design({"a", "b"}));
        Require(good["kind"] == "plate" && good["basis"] == Basis(), "report identity and basis");
        for (const auto* id : {"geometry-validity", "geometry-empty", "geometry-connectivity", "plate-bounds", "plate-height", "plate-bed-contact", "part-overlap", "plate-clearance", "plate-quantities"})
            Require(Result(good, id) == "passed", "separated cubes pass computable checks");
        Require(good["settings"]["contactTolerance"]["origin"] == "numeric-default", "contact default labeled");
        for (const auto& check : good["checks"]) Require(check["basis"] == Basis() && check["evidence"].is_object() && check["nextActions"].is_array(), "check schema");
        for (const auto* id : {"minimum-thickness", "strength", "support-removal", "layer-direction", "toolpath"})
            Require(Result(good, id) == "not-checked" && !Check(good, id)["nextActions"].empty(), "unsupported checks explicit with guidance");
        Require(Result(Run({Part("a", cube.Translate({-1, 10, 0}))}), "plate-bounds") == "failed", "negative XY bounds fail");
        Require(Result(Run({Part("a", cube.Translate({95, 10, 0}))}), "plate-bounds") == "failed", "maximum XY bounds fail");
        Require(Result(Run({Part("a", cube.Translate({10, 10, 95}))}), "plate-height") == "failed", "maximum height fails");
        auto looseContact = Settings(); looseContact["plateSettings"]["plate"]["contactTolerance"] = 1000000;
        const auto looseContactReport = Run({a, Part("bad", cube.Translate({-10, 10, 200}))}, Design({"a", "bad"}), Profile(), looseContact);
        Require(Result(looseContactReport, "plate-bounds") == "failed" && Result(looseContactReport, "plate-height") == "failed" && Result(looseContactReport, "plate-allowance-bounds") == "warning", "contact tolerance cannot hide XY height or envelope errors");
        Require(Check(looseContactReport, "plate-bounds")["partIds"] == json::array({"bad"}) && Check(looseContactReport, "plate-height")["partIds"] == json::array({"bad"}), "bounds failures identify only affected parts");
        Require(Result(Run({Part("a", cube.Translate({10, 10, 1}))}), "plate-bed-contact") == "failed", "floating part fails");
        Require(Result(Run({Part("a", cube.Translate({10, 10, -1}))}), "plate-bed-contact") == "failed", "below-bed part fails");
        Require(Result(Run({Part("a", cube.Translate({10, 10, 0.00001}))}), "plate-bed-contact") == "passed", "numeric contact tolerance applied");
        auto strict = Settings(); strict["plateSettings"]["plate"]["contactTolerance"] = 0;
        Require(Result(Run({Part("a", cube.Translate({10, 10, 0.00001}))}, Design(), Profile(), strict), "plate-bed-contact") == "failed", "authored zero tolerance respected");

        auto rotated = Part("a", manifold::Manifold::Cube({20, 4, 3}).Rotate(0, 0, 90).Translate({10, 10, 0}));
        rotated.rotation = {0, 0, 90}; rotated.translation = {10, 10, 0};
        const auto rotatedReport = Run({rotated});
        Require(std::abs(rotatedReport["parts"][0]["bounds"]["min"][0].get<double>() - 6) < 1e-6 &&
            std::abs(rotatedReport["parts"][0]["bounds"]["max"][1].get<double>() - 30) < 1e-6, "actual transformed bounds used");
        const auto sourceBefore = a.sourceSolid->Volume();
        const auto collision = Run({a, Part("b", cube.Translate({15, 10, 0}))}, Design({"a", "b"}));
        Require(Result(collision, "part-overlap") == "failed", "positive solid intersection fails plate");
        Require(Check(collision, "part-overlap")["evidence"]["overlaps"][0]["intersectionVolume"].get<double>() > 499, "intersection volume evidence");
        const auto affectedOverlap = Run({a, Part("b", cube.Translate({15, 10, 0})), Part("other", cube.Translate({70, 10, 0}))}, Design({"a", "b", "other"}));
        Require(Check(affectedOverlap, "part-overlap")["partIds"] == json::array({"a", "b"}), "overlap record identifies only colliding parts");
        Require(a.sourceSolid->Volume() == sourceBefore, "shared solids untouched");
        Require(Result(Run({a, Part("b", cube.Translate({15, 10, 0}))}, Design({"a", "b"}, "assembly")), "part-overlap") == "warning", "assembly overlap requires intent review");
        Require(Result(Run({a}, Design({"a"}, "inspection")), "plate-bed-contact") == "not-checked", "nonplate checks not applicable");
        Require(Result(Run({a, Part("b", cube.Translate({20, 10, 0}))}, Design({"a", "b"})), "part-overlap") == "passed", "touching zero-volume faces are not positive overlap");
        auto ring = Part("a", (manifold::Manifold::Cylinder(5, 10, 10, 48) - manifold::Manifold::Cylinder(5, 8, 8, 48)).Translate({20, 20, 0}));
        auto inner = Part("b", manifold::Manifold::Cylinder(5, 3, 3, 48).Translate({20, 20, 0}));
        const auto conservative = Run({ring, inner}, Design({"a", "b"}));
        Require(Result(conservative, "part-overlap") == "passed", "overlapping AABBs do not imply solid collision");
        Require(Result(conservative, "plate-clearance") == "warning" && Check(conservative, "plate-clearance")["evidence"]["possibleConflicts"].size() == 1, "AABB clearance explicitly conservative");
        Require(Result(Run({a, b}, Design({"a", "b"}), Profile(), json::object()), "plate-clearance") == "not-checked", "unknown allowances never pass");
        Require(Result(Run({Part("a", cube.Translate({1, 10, 0}))}, Design(), Profile(), Settings(2, 2)), "plate-allowance-bounds") == "warning", "brim envelope beyond bed warns");
        auto excluded = Profile(); excluded["profile"]["exclusions"] = {{{"min", {22, 10}}, {"max", {24, 20}}}};
        const auto envelopeExcluded = Run({a}, Design(), excluded, Settings(2, 3));
        Require(Result(envelopeExcluded, "plate-exclusions") == "passed" && Result(envelopeExcluded, "plate-allowance-bounds") == "warning", "allowance envelope checked against exclusions");
        excluded["profile"]["exclusions"] = {{{"min", {11, 11}}, {"max", {12, 12}}}};
        Require(Result(Run({a}, Design(), excluded), "plate-exclusions") == "warning", "AABB exclusion conflict is possible not proven");

        auto provisional = Profile(); provisional["status"] = "incomplete"; provisional["provisional"] = {"buildVolume"};
        Require(Result(Run({a}, Design(), provisional), "plate-bounds") == "warning", "provisional bed fit not certified");
        auto invalid = Profile(); invalid["status"] = "invalid";
        for (const auto* id : {"plate-bounds", "plate-height", "plate-bed-contact", "plate-clearance"})
            Require(Result(Run({a}, Design(), invalid), id) == "not-checked", "invalid profile dependent checks notchecked");
        Require(Result(Run({a}, Design(), synthcad::PrinterProfileContext(json::object())), "plate-bounds") == "not-checked", "missing profile never inferred");
        const auto volumeOnly = synthcad::PrinterProfileContext({{"activeProfile", "custom"}, {"profiles", {{"custom", {{"buildVolume", {220, 220, 250}}}}}}});
        const auto volumeReport = Run({a}, Design(), volumeOnly);
        Require(volumeReport["profileStatus"] == "incomplete" && volumeReport["bed"]["size"] == json({220, 220, 250}) && volumeReport["bed"]["exclusions"].is_null(), "volume-only context retains drawable bed without inventing exclusions");
        for (const auto* id : {"plate-bounds", "plate-height", "plate-bed-contact"}) Require(Result(volumeReport, id) == "passed", "volume alone enables placement checks");
        Require(Result(volumeReport, "plate-exclusions") == "not-checked" && Check(volumeReport, "plate-exclusions")["evidence"]["exclusionsKnown"] == false, "unknown exclusions never pass");
        Require(Result(volumeReport, "plate-allowance-bounds") == "not-checked" && Check(volumeReport, "plate-allowance-bounds")["evidence"]["bedBoundsResult"] == "passed", "allowance bed bounds remain computed while unknown exclusions remain unchecked");
        const auto outsideEnvelope = Run({Part("a", cube.Translate({1, 10, 0}))}, Design(), volumeOnly, Settings(2, 2));
        Require(Result(outsideEnvelope, "plate-allowance-bounds") == "warning" && Check(outsideEnvelope, "plate-allowance-bounds")["evidence"]["exclusionsKnown"] == false, "known envelope overflow warns even when exclusions unknown");
        auto unrelatedProvisional = Profile();unrelatedProvisional["status"] = "incomplete";unrelatedProvisional["provisional"] = {"printer", "material", "exclusions"};
        Require(Result(Run({a}, Design(), unrelatedProvisional), "plate-bounds") == "passed" && Result(Run({a}, Design(), unrelatedProvisional), "plate-exclusions") == "warning", "only volume uncertainty affects volume checks");
        const auto longBar = manifold::Manifold::Cube({280, 10, 5});
        Require(Result(Run({Part("a", longBar)}, Design(), volumeOnly), "plate-bounds") == "failed", "long bar straight placement exceeds square bed");
        const auto diagonal = Run({Part("a", longBar.Rotate(0, 0, 45).Translate({10, 0, 0}))}, Design(), volumeOnly);
        Require(Result(diagonal, "plate-bounds") == "passed", "actual diagonal placement fits despite long dimension exceeding bed width");
        const auto thickDiagonal = Run({Part("a", manifold::Manifold::Cube({280, 40, 5}).Rotate(0, 0, 45).Translate({29, 0, 0}))}, Design(), volumeOnly);
        Require(Result(thickDiagonal, "plate-bounds") == "failed", "part width can prevent diagonal fit despite long axis below bed diagonal");
        auto invalidSettings = Settings(); invalidSettings["plateSettings"]["plate"]["brim"] = -1;
        Require(Result(Run({a}, Design(), Profile(), invalidSettings), "plate-clearance") == "not-checked", "invalid allowance cannot pass");
        invalidSettings["plateSettings"]["plate"]["brim"] = std::numeric_limits<double>::infinity();
        Require(!Run({a}, Design(), Profile(), invalidSettings)["settings"]["errors"].empty(), "nonfinite setting rejected");
        const auto disconnected = Part("a", manifold::Manifold::Compose({cube.Translate({10, 10, 0}), cube.Translate({30, 10, 0})}));
        Require(Result(Run({disconnected}), "geometry-connectivity") == "warning", "multiple components require review");
        Require(Result(Run({Part("a", manifold::Manifold())}), "geometry-empty") == "failed", "empty source detected");
        auto broken = a; broken.solid.reset();
        Require(Result(Run({broken}), "geometry-validity") == "failed", "missing solid invalid");
        Require(Result(Run({a, Part("reference", cube.Translate({10, 10, 0}), false)}), "part-overlap") == "passed", "nonexportable references excluded");

        auto quantities = Design({"a", "b"});
        quantities["groups"] = {{{"id", "aliases"}, {"members", {{{"instance", "a"}}, {{"instance", "a"}}}}}};
        quantities["views"][0]["members"].push_back({{"group", "aliases"}});
        auto quantityReport = Run({a, b}, quantities);
        Require(quantityReport["quantities"][0]["expected"] == 2 && quantityReport["quantities"][0]["platePlacements"] == 2 && Result(quantityReport, "plate-quantities") == "passed", "aliases dedup and intentional copies count separately");
        quantities["views"].push_back({{"id", "plate2"}, {"kind", "plate"}, {"members", {{{"instance", "a"}}}}});
        Require(Result(Run({a, b}, quantities), "plate-quantities") == "failed", "same instance on two plates detected");
        quantities = Design({"a", "b"}); quantities["views"][0]["members"] = {{{"instance", "a"}}};
        Require(Result(Run({a}, quantities), "plate-quantities") == "failed", "unassigned authored instance detected");
        Require(Check(Run({a}, quantities), "plate-quantities")["partIds"] == json::array({"a", "b"}), "quantity mismatch includes assigned and missing affected instance IDs");
        quantities = Design(); quantities["sourceParts"][0]["quantity"] = 2;
        Require(Result(Run({a}, quantities), "plate-quantities") == "failed" && Run({a}, quantities)["quantities"][0]["expectedOrigin"] == "authored-source-quantity", "explicit source quantity honored");
        quantities["sourceParts"][0]["quantity"] = uint64_t(9007199254740991ULL);
        const auto largeQuantity = Run({a}, quantities);
        Require(largeQuantity["quantities"][0]["expected"] == uint64_t(9007199254740991ULL) && Check(largeQuantity, "plate-quantities")["evidence"]["errors"].empty() && Result(largeQuantity, "plate-quantities") == "failed", "large valid quantity preserved without generating geometry copies");
        quantities["views"][0]["members"] = {{{"instance", "missing"}}};
        Require(Result(Run({a}, quantities), "plate-quantities") == "failed", "missing graph reference detected");
        Require(Result(Run({a}, json::object()), "plate-quantities") == "not-checked" && Result(Run({a}, json::object()), "quantity-scope") == "not-checked", "legacy absent graph never implies quantity verification");
        auto standaloneBasis = Basis(); standaloneBasis["view"] = "scene";
        const auto alias = ManufacturingChecks({a}, Design(), Profile(), Settings(), standaloneBasis);
        Require(alias["view"] == "scene" && alias["kind"] == "plate" && alias["designView"] == "plate" && Result(alias, "plate-clearance") == "passed", "standalone alias resolves active design view for kind/settings");
        auto modules = Settings(); modules["views"] = {{"one", "first.js"}, {"two", "second.js"}};
        Require(Result(Run({a}, Design(), Profile(), modules), "quantity-scope") == "not-checked", "independent modules cannot imply complete project quantities");

        std::vector<DisplayPart> many;
        std::vector<std::string> ids;
        for (size_t i = 0; i < 66; ++i) { ids.push_back("copy-" + std::to_string(i)); many.push_back(Part(ids.back(), cube.Translate({double(i * 20), 10, 0}))); }
        const auto bounded = Run(many, Design(ids));
        Require(Check(bounded, "part-overlap")["evidence"]["uncheckedPairs"].get<size_t>() > 0 && Result(bounded, "part-overlap") == "not-checked", "pair budget reports unchecked remainder");
        const auto detailed = manifold::Manifold::Sphere(5, 320).Translate({20, 20, 5});
        const auto triangleBounded = Run({Part("a", detailed), Part("b", detailed.Translate({1, 0, 0}))}, Design({"a", "b"}));
        Require(Check(triangleBounded, "part-overlap")["evidence"]["uncheckedPairs"].get<size_t>() == 1 && Result(triangleBounded, "part-overlap") == "not-checked", "triangle budget avoids unbounded CSG and reports unchecked pair");
        const auto footprintComputed = Run({Part("a", detailed), Part("b", detailed.Translate({1, 0, 0}))}, Design({"a", "b"}), Profile(), Settings(0, 0, 0));
        Require(Result(footprintComputed, "part-overlap") == "not-checked" && Check(footprintComputed, "part-overlap")["evidence"]["triangleLimitOmissions"] == 1,
            "high-triangle intersecting bounds retain uncomputed exact overlap");
        Require(Result(footprintComputed, "plate-clearance") == "passed" && Check(footprintComputed, "plate-clearance")["evidence"]["uncheckedPairs"] == 0,
            "CSG budget does not mark computed zero-allowance footprint comparison unchecked");
        std::cout << "manufacturing_checks_test passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
