#include "manufacturing_checks.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>

namespace dingcad {
namespace {
using json = nlohmann::json;
constexpr size_t kPairLimit = 2048, kPairTriangles = 100000, kTriangleBudget = 2000000;
constexpr size_t kMetadataLimit = 10000, kExpansionLimit = 100000;
constexpr double kContactTolerance = 1e-4;
constexpr double kBoundsEpsilon = 1e-4;
constexpr uint64_t kMaxQuantity = 9007199254740991ULL;
json Array(const json& object, const char* field) {
    if (!object.is_object() || !object.contains(field) || !object[field].is_array()) return json::array();
    return object[field];
}
std::string String(const json& object, const char* field, const std::string& fallback = "") {
    if (!object.is_object() || !object.contains(field) || !object[field].is_string()) return fallback;
    return object[field].get<std::string>();
}
bool Number(const json& value, bool positive = false) {
    return value.is_number() && std::isfinite(value.get<double>()) && (positive ? value.get<double>() > 0 : value.get<double>() >= 0);
}
json Vec(const manifold::vec3& v) { return {v.x, v.y, v.z}; }
bool Bounds(const manifold::Box& box) {
    for (int axis = 0; axis < 3; ++axis)
        if (!std::isfinite(box.min[axis]) || !std::isfinite(box.max[axis]) || box.max[axis] < box.min[axis]) return false;
    return true;
}
bool Overlap3(const manifold::Box& a, const manifold::Box& b) {
    for (int axis = 0; axis < 3; ++axis)
        if (std::min(a.max[axis], b.max[axis]) <= std::max(a.min[axis], b.min[axis])) return false;
    return true;
}
double FootprintGap(const manifold::Box& a, const manifold::Box& b) {
    const double x = std::max({a.min.x - b.max.x, b.min.x - a.max.x, 0.0});
    const double y = std::max({a.min.y - b.max.y, b.min.y - a.max.y, 0.0});
    return std::hypot(x, y);
}
json Check(const std::string& id, const std::string& name, const std::string& result,
           const std::string& scope, const json& ids, const json& basis,
           const std::string& method, const json& evidence, const json& actions = json::array()) {
    return {{"id", id}, {"name", name}, {"result", result}, {"scope", scope}, {"partIds", ids},
        {"basis", basis}, {"method", method}, {"evidence", evidence}, {"nextActions", actions}};
}
json Affected(const json& items) {
    std::set<std::string> ids;
    std::function<void(const json&)> collect = [&](const json& value) {
        if (value.is_string()) ids.insert(value.get<std::string>());
        else if (value.is_array()) for (const auto& item : value) collect(item);
        else if (value.is_object()) {
            for (const auto* key : {"partId", "instanceId", "partIds"})
                if (value.contains(key)) collect(value[key]);
        }
    };
    collect(items);
    return ids;
}
void Quantities(json& output, const json& design, const json& basis) {
    std::map<std::string, json> instances, groups, sources;
    json errors = json::array(), duplicates = json::array(), missing = json::array();
    std::set<std::string> affected;
    const auto read = [&](const char* field, auto& target) {
        const auto list = Array(design, field);
        if (list.size() > kMetadataLimit) errors.push_back(std::string(field) + " exceeds metadata limit");
        for (size_t i = 0; i < std::min(list.size(), kMetadataLimit); ++i) {
            const auto id = String(list[i], "id");
            if (id.empty() || !target.emplace(id, list[i]).second) errors.push_back(std::string(field) + " has missing or duplicate ID");
        }
    };
    read("instances", instances); read("groups", groups); read("sourceParts", sources);
    std::map<std::string, std::set<std::string>> assigned;
    std::map<std::string, size_t> authored;
    for (const auto& [id, instance] : instances) {
        if (instance.value("exportable", true)) {
            const auto source = String(instance, "part");
            if (!sources.count(source)) errors.push_back("Unknown source part for instance " + id);
            ++authored[source];
        }
    }
    for (const auto& [id, source] : sources)
        if (source.contains("quantity") && source.value("exportable", true)) authored.emplace(id, 0);
    size_t plateCount = 0, expanded = 0;
    bool bounded = false;
    const auto views = Array(design, "views");
    for (size_t i = 0; i < std::min(views.size(), kMetadataLimit); ++i) {
        const auto& view = views[i];
        if (String(view, "kind") != "plate") continue;
        ++plateCount;
        const auto viewId = String(view, "id");
        std::set<std::string> included, visiting;
        std::function<void(const json&, size_t)> expand;
        expand = [&](const json& refs, size_t depth) {
            if (!refs.is_array() || depth > 32) { errors.push_back("Invalid or cyclic group membership in plate " + viewId); return; }
            for (const auto& ref : refs) {
                if (++expanded > kExpansionLimit) { bounded = true; return; }
                if (!ref.is_object()) { errors.push_back("Invalid plate member in " + viewId); continue; }
                if (ref.contains("instance")) {
                    const auto id = String(ref, "instance");
                    if (!instances.count(id)) { errors.push_back("Unknown plate instance " + id); affected.insert(id); }
                    else if (instances.at(id).value("exportable", true)) included.insert(id);
                } else if (ref.contains("group")) {
                    const auto id = String(ref, "group");
                    if (!groups.count(id) || !visiting.insert(id).second) errors.push_back("Missing or cyclic plate group " + id);
                    else { expand(Array(groups.at(id), "members"), depth + 1); visiting.erase(id); }
                } else errors.push_back("Unknown plate member in " + viewId);
            }
        };
        expand(Array(view, "members"), 0);
        for (const auto& id : included) assigned[id].insert(viewId);
    }
    if (views.size() > kMetadataLimit || bounded) errors.push_back("Plate quantity expansion limit reached; quantities are incomplete");
    if (plateCount) for (const auto& [id, instance] : instances) {
        if (!instance.value("exportable", true)) continue;
        if (!assigned.count(id)) { missing.push_back(id); affected.insert(id); }
        else if (assigned[id].size() > 1) { duplicates.push_back({{"instanceId", id}, {"plates", assigned[id]}}); affected.insert(id); }
    }
    bool mismatch = false;
    for (const auto& [source, count] : authored) {
        json placedIds = json::array(), plates = json::array();
        size_t placements = 0;
        for (const auto& [id, memberships] : assigned) if (String(instances.at(id), "part") == source) {
            placedIds.push_back(id); placements += memberships.size();
            for (const auto& plate : memberships) plates.push_back({{"view", plate}, {"instanceId", id}});
        }
        uint64_t expected = count;
        std::string origin = "authored-exportable-instance-count";
        if (sources.count(source) && sources.at(source).contains("quantity")) {
            const auto& quantity = sources.at(source)["quantity"];
            if (!quantity.is_number_integer() || !Number(quantity, true) || quantity.get<double>() > double(kMaxQuantity))
                errors.push_back("Source quantity must be a positive integer at most 9007199254740991: " + source);
            else { expected = quantity.get<uint64_t>(); origin = "authored-source-quantity"; }
        }
        mismatch = mismatch || (plateCount && placements != expected);
        if (plateCount && placements != expected)
            for (const auto& [id, instance] : instances)
                if (String(instance, "part") == source && instance.value("exportable", true)) affected.insert(id);
        output["quantities"].push_back({{"sourcePartId", source}, {"expected", expected}, {"expectedOrigin", origin},
            {"authoredInstances", count}, {"placedInstances", placedIds.size()}, {"platePlacements", placements},
            {"instanceIds", placedIds}, {"plates", plates}});
    }
    const auto result = !plateCount ? "not-checked" : (!errors.empty() || !missing.empty() || !duplicates.empty() || mismatch) ? "failed" : "passed";
    output["checks"].push_back(Check("plate-quantities", "Plate instance quantities", result, "geometry", affected, basis,
        "Expand all authored plate groups; deduplicate aliases per plate; count intentional instance IDs independently without evaluating other solids",
        {{"plateViews", plateCount}, {"missingInstances", missing}, {"instancesOnMultiplePlates", duplicates}, {"errors", errors}, {"expansionLimit", kExpansionLimit}},
        result == std::string("passed") ? json::array() : json::array({"Assign each intended exportable instance to one plate; use distinct instance IDs for intentional copies and confirm source quantities."})));
}
} // namespace

json ManufacturingChecks(const std::vector<DisplayPart>& parts, const json& design,
    const json& profile, const json& metadata, const json& basis) {
    const auto view = String(basis, "view", String(design, "activeView", "scene"));
    auto designView = view;
    std::string kind = "scene";
    for (const auto& candidate : Array(design, "views")) if (String(candidate, "id") == view) kind = String(candidate, "kind", "scene");
    if (kind == "scene" && view == "scene" && !String(design, "activeView").empty()) {
        designView = String(design, "activeView");
        for (const auto& candidate : Array(design, "views")) if (String(candidate, "id") == designView) kind = String(candidate, "kind", "scene");
    }
    const bool plate = kind == "plate";
    const auto profileStatus = String(profile, "status", "incomplete");
    json output = {{"schemaVersion", 1}, {"view", view}, {"kind", kind}, {"basis", basis}, {"profileStatus", profileStatus},
        {"designView", designView}, {"settings", json::object()}, {"bed", nullptr}, {"parts", json::array()}, {"checks", json::array()}, {"quantities", json::array()}};
    const auto add = [&](const std::string& id, const std::string& name, const std::string& result,
                         const std::string& scope, const json& ids, const std::string& method,
                         const json& evidence, const json& actions = json::array()) {
        output["checks"].push_back(Check(id, name, result, scope, ids, basis, method, evidence, actions));
    };
    json authored = json::object(), settingsErrors = json::array();
    if (metadata.is_object() && metadata.contains("plateSettings")) {
        if (!metadata["plateSettings"].is_object()) settingsErrors.push_back("plateSettings must be an object keyed by view ID");
        else if (metadata["plateSettings"].contains(designView)) {
            if (!metadata["plateSettings"][designView].is_object()) settingsErrors.push_back("Active plate settings must be an object");
            else authored = metadata["plateSettings"][designView];
        }
    }
    for (auto it = authored.begin(); it != authored.end(); ++it)
        if (it.key() != "partGap" && it.key() != "brim" && it.key() != "support" && it.key() != "contactTolerance") settingsErrors.push_back("Unknown plate setting: " + it.key());
    for (const auto* key : {"partGap", "brim", "support", "contactTolerance"}) {
        const bool present = authored.contains(key), valid = present && Number(authored[key]);
        if (present && !valid) settingsErrors.push_back(std::string(key) + " must be a nonnegative finite number in mm");
        const bool defaulted = !present && std::string(key) == "contactTolerance";
        output["settings"][key] = {{"value", valid ? authored[key] : defaulted ? json(kContactTolerance) : json(nullptr)},
            {"origin", valid ? "authored" : defaulted ? "numeric-default" : "unknown"}, {"unit", "mm"}};
    }
    output["settings"]["errors"] = settingsErrors;
    const double tolerance = output["settings"]["contactTolerance"]["value"].is_number() ? output["settings"]["contactTolerance"]["value"].get<double>() : kContactTolerance;
    const bool contactValid = !authored.contains("contactTolerance") || Number(authored["contactTolerance"]);
    if (!settingsErrors.empty()) add("plate-settings", "Authored plate allowances", "not-checked", "heuristic", json::array(),
        "Validate optional per-view numeric settings without modifying geometry", {{"errors", settingsErrors}}, {"Correct invalid settings and confirm intended clearances with the slicer."});

    json activeIds = json::array(), invalid = json::array(), empty = json::array(), disconnected = json::array();
    struct Solid { const DisplayPart* part; manifold::Box bounds; size_t triangles; };
    std::vector<Solid> solids;
    for (const auto& part : parts) {
        json row = {{"id", part.id}, {"name", part.name}, {"sourcePartId", part.sourcePartId}, {"rotation", part.rotation},
            {"translation", part.translation}, {"exportable", part.exportable}, {"bounds", nullptr}};
        if (part.exportable) activeIds.push_back(part.id);
        try {
            if (!part.solid || part.solid->Status() != manifold::Manifold::Error::NoError) {
                if (part.exportable) invalid.push_back(part.id);
            } else if (part.solid->IsEmpty()) {
                if (part.exportable) empty.push_back(part.id);
            } else {
                const auto box = part.solid->BoundingBox();
                if (!Bounds(box)) { if (part.exportable) invalid.push_back(part.id); }
                else {
                    row["bounds"] = {{"min", Vec(box.min)}, {"max", Vec(box.max)}};
                    if (part.exportable) {
                        const double volume = part.solid->Volume();
                        if (!std::isfinite(volume) || volume <= 0) invalid.push_back(part.id);
                        else {
                            const auto components = part.solid->Decompose().size();
                            if (components != 1) disconnected.push_back({{"partId", part.id}, {"components", components}});
                            solids.push_back({&part, box, part.solid->NumTri()});
                        }
                    }
                }
            }
        } catch (const std::exception& error) {
            if (part.exportable) invalid.push_back({{"partId", part.id}, {"diagnostic", error.what()}});
        }
        output["parts"].push_back(std::move(row));
    }
    add("geometry-validity", "Exportable solid validity", activeIds.empty() ? "not-checked" : invalid.empty() ? "passed" : "failed", "geometry", invalid.empty() ? activeIds : Affected(invalid),
        "Manifold topology status, finite transformed bounds, positive finite solid volume", {{"invalidParts", invalid}},
        invalid.empty() ? json::array() : json::array({"Repair invalid authored solids before export."}));
    add("geometry-empty", "Empty exportable parts", activeIds.empty() || !empty.empty() ? "failed" : !invalid.empty() ? "not-checked" : "passed", "geometry", empty.empty() ? activeIds : Affected(empty),
        "Manifold IsEmpty on each authored exportable part", {{"emptyParts", empty}, {"exportableParts", activeIds.size()}},
        activeIds.empty() || !empty.empty() ? json::array({"Provide nonempty exportable solids; keep reference geometry explicitly non-exportable."}) : json::array());
    add("geometry-connectivity", "Exportable part connectivity", solids.empty() || !invalid.empty() || !empty.empty() ? "not-checked" : disconnected.empty() ? "passed" : "warning", "geometry", disconnected.empty() ? activeIds : Affected(disconnected),
        "Manifold Decompose connected components; disconnected bodies may be intentional", {{"disconnectedParts", disconnected}},
        disconnected.empty() ? json::array() : json::array({"Review disconnected bodies; represent intentionally separate printed parts as separate source parts."}));

    bool bedReady = profileStatus != "invalid";
    json bedSize = nullptr, exclusions = nullptr;
    if (profile.is_object() && profile.contains("profile") && profile["profile"].is_object()) {
        const auto& selected = profile["profile"];
        if (selected.contains("buildVolume") && selected["buildVolume"].is_array() && selected["buildVolume"].size() == 3) {
            bedSize = selected["buildVolume"];
            for (const auto& number : bedSize) if (!Number(number, true)) bedReady = false;
        }
        if (selected.contains("exclusions") && selected["exclusions"].is_array()) exclusions = selected["exclusions"];
    }
    bedReady = bedReady && bedSize.is_array();
    const bool exclusionsKnown = profileStatus != "invalid" && exclusions.is_array();
    const auto provisional = Array(profile, "provisional");
    bool bedProvisional = false, exclusionsProvisional = false;
    for (const auto& field : provisional) {
        if (field == "buildVolume") bedProvisional = true;
        if (field == "exclusions") exclusionsProvisional = true;
    }
    if (bedReady) output["bed"] = {{"size", bedSize}, {"exclusions", exclusions}, {"exclusionsKnown", exclusionsKnown}, {"origin", {0, 0}}};
    json outside = json::array(), tooTall = json::array(), floating = json::array(), below = json::array(), excluded = json::array();
    for (const auto& solid : solids) {
        const auto& box = solid.bounds; const auto& id = solid.part->id;
        if (std::abs(box.min.z) > tolerance) (box.min.z < 0 ? below : floating).push_back({{"partId", id}, {"minZ", box.min.z}});
        if (bedReady) {
            if (box.min.x < -kBoundsEpsilon || box.min.y < -kBoundsEpsilon || box.max.x > bedSize[0].get<double>() + kBoundsEpsilon || box.max.y > bedSize[1].get<double>() + kBoundsEpsilon) outside.push_back(id);
            if (box.max.z > bedSize[2].get<double>() + kBoundsEpsilon) tooTall.push_back(id);
            for (size_t i = 0; exclusionsKnown && i < exclusions.size(); ++i) {
                const auto& rect = exclusions[i];
                if (!rect.is_object() || !rect.contains("min") || !rect.contains("max") || !rect["min"].is_array() || !rect["max"].is_array() || rect["min"].size() != 2 || rect["max"].size() != 2) continue;
                if (std::min(box.max.x, rect["max"][0].get<double>()) > std::max(box.min.x, rect["min"][0].get<double>()) && std::min(box.max.y, rect["max"][1].get<double>()) > std::max(box.min.y, rect["min"][1].get<double>())) excluded.push_back({{"partId", id}, {"exclusionIndex", i}});
            }
        }
    }
    const bool canPlate = plate && bedReady && !solids.empty() && invalid.empty() && empty.empty();
    const auto limitResult = [&](const json& failures) { return !canPlate ? "not-checked" : !failures.empty() ? "failed" : bedProvisional ? "warning" : "passed"; };
    add("plate-bounds", "Transformed bed bounds", limitResult(outside), "geometry", outside.empty() ? activeIds : Affected(outside),
        "Actual placed solid axis-aligned bounds compared to rectangular bed limits", {{"outsideParts", outside}, {"boundsEpsilon", kBoundsEpsilon}, {"profileProvisional", bedProvisional}, {"applicable", plate}, {"bedAvailable", bedReady}},
        canPlate && outside.empty() && !bedProvisional ? json::array() : json::array({"Confirm bed dimensions and place exportable parts inside the usable bed."}));
    add("plate-height", "Build height", limitResult(tooTall), "geometry", tooTall.empty() ? activeIds : Affected(tooTall),
        "Maximum Z of actual placed solid compared to authored build height", {{"tooTallParts", tooTall}, {"boundsEpsilon", kBoundsEpsilon}, {"profileProvisional", bedProvisional}},
        canPlate && tooTall.empty() && !bedProvisional ? json::array() : json::array({"Confirm build height and reorient oversized parts."}));
    add("plate-bed-contact", "Bed contact", !canPlate || !contactValid ? "not-checked" : !floating.empty() || !below.empty() ? "failed" : bedProvisional ? "warning" : "passed", "geometry", floating.empty() && below.empty() ? activeIds : Affected(json::array({floating, below})),
        "Minimum Z within explicit numeric tolerance of bed Z=0; does not prove a stable contact area or adhesion", {{"floatingParts", floating}, {"belowBedParts", below}, {"contactTolerance", tolerance}},
        canPlate && floating.empty() && below.empty() ? json::array() : json::array({"Place parts on Z=0 and inspect stable first-layer contact in the slicer."}));
    add("plate-exclusions", "Excluded bed regions", !canPlate || !exclusionsKnown ? "not-checked" : !excluded.empty() || exclusionsProvisional ? "warning" : "passed", "heuristic", excluded.empty() ? activeIds : Affected(excluded),
        "Conservative XY AABB footprints versus authored excluded rectangles; an unknown exclusion list is not checked; a footprint intersection is a possible conflict, not proven solid contact", {{"possibleConflicts", excluded}, {"exclusionsKnown", exclusionsKnown}, {"profileProvisional", exclusionsProvisional}},
        !exclusionsKnown ? json::array({"Record excluded bed regions, or explicitly confirm an empty exclusions list."}) : excluded.empty() ? json::array() : json::array({"Move parts away from clips/exclusions or inspect the exact footprint before slicing."}));

    size_t considered = 0, skipped = 0, exact = 0, broadDisjoint = 0, trianglesUsed = 0, triangleSkipped = 0;
    json overlaps = json::array(), overlapErrors = json::array(), clearanceConflicts = json::array();
    bool allowances = settingsErrors.empty() && output["settings"]["partGap"]["value"].is_number() && output["settings"]["brim"]["value"].is_number() && output["settings"]["support"]["value"].is_number();
    const double envelope = allowances ? output["settings"]["brim"]["value"].get<double>() + output["settings"]["support"]["value"].get<double>() : 0;
    const double requiredGap = allowances ? output["settings"]["partGap"]["value"].get<double>() + 2 * envelope : 0;
    allowances = allowances && std::isfinite(requiredGap);
    json envelopeOutside = json::array(), envelopeExclusions = json::array();
    if (allowances && bedReady) for (const auto& solid : solids) {
        const auto& box = solid.bounds;
        if (box.min.x - envelope < -kBoundsEpsilon || box.min.y - envelope < -kBoundsEpsilon || box.max.x + envelope > bedSize[0].get<double>() + kBoundsEpsilon || box.max.y + envelope > bedSize[1].get<double>() + kBoundsEpsilon) envelopeOutside.push_back(solid.part->id);
        for (size_t i = 0; exclusionsKnown && i < exclusions.size(); ++i) {
            const auto& rect = exclusions[i];
            if (!rect.is_object() || !rect.contains("min") || !rect.contains("max") || !rect["min"].is_array() || !rect["max"].is_array() || rect["min"].size() != 2 || rect["max"].size() != 2) continue;
            if (std::min(box.max.x + envelope, rect["max"][0].get<double>()) > std::max(box.min.x - envelope, rect["min"][0].get<double>()) && std::min(box.max.y + envelope, rect["max"][1].get<double>()) > std::max(box.min.y - envelope, rect["min"][1].get<double>())) envelopeExclusions.push_back({{"partId", solid.part->id}, {"exclusionIndex", i}});
        }
    }
    add("plate-allowance-bounds", "Brim and support envelope versus usable bed", !canPlate || !allowances ? "not-checked" : !envelopeOutside.empty() || !envelopeExclusions.empty() || bedProvisional || (exclusionsKnown && exclusionsProvisional) ? "warning" : !exclusionsKnown ? "not-checked" : "passed", "heuristic", envelopeOutside.empty() && envelopeExclusions.empty() ? activeIds : Affected(json::array({envelopeOutside, envelopeExclusions})),
        "Conservative XY AABB inflated by brim+support on each side; bed edges are checked from build dimensions; unknown exclusion regions remain unchecked; possible conflicts only",
        {{"envelopePerSide", allowances ? json(envelope) : json(nullptr)}, {"boundsEpsilon", kBoundsEpsilon}, {"possiblyOutsideParts", envelopeOutside}, {"possibleExclusionConflicts", envelopeExclusions}, {"exclusionsKnown", exclusionsKnown}, {"bedBoundsResult", !canPlate || !allowances ? "not-checked" : !envelopeOutside.empty() || bedProvisional ? "warning" : "passed"}},
        {"Confirm slicer-generated brims/supports remain inside the usable bed and clear all excluded regions."});
    const auto totalPairs = solids.empty() ? size_t(0) : solids.size() * (solids.size() - 1) / 2;
    const auto omittedPairs = totalPairs > kPairLimit ? totalPairs - kPairLimit : 0;
    skipped = omittedPairs;
    for (size_t i = 0; i < solids.size() && considered < kPairLimit; ++i) for (size_t j = i + 1; j < solids.size() && considered < kPairLimit; ++j) {
        ++considered;
        const auto& a = solids[i]; const auto& b = solids[j];
        if (plate && allowances && FootprintGap(a.bounds, b.bounds) < requiredGap)
            clearanceConflicts.push_back({{"partIds", {a.part->id, b.part->id}}, {"aabbGap", FootprintGap(a.bounds, b.bounds)}, {"requiredGap", requiredGap}});
        if (!Overlap3(a.bounds, b.bounds)) { ++broadDisjoint; continue; }
        const auto pairTriangles = a.triangles + b.triangles;
        if (pairTriangles > kPairTriangles || trianglesUsed + pairTriangles > kTriangleBudget) { ++skipped; ++triangleSkipped; continue; }
        trianglesUsed += pairTriangles;
        try {
            const auto intersection = *a.part->solid ^ *b.part->solid;
            if (intersection.Status() != manifold::Manifold::Error::NoError) { overlapErrors.push_back({a.part->id, b.part->id}); continue; }
            const double volume = intersection.Volume(); ++exact;
            if (!std::isfinite(volume) || volume < 0) overlapErrors.push_back({a.part->id, b.part->id});
            else if (volume > 1e-9) overlaps.push_back({{"partIds", {a.part->id, b.part->id}}, {"intersectionVolume", volume}, {"unit", "mm^3"}});
        } catch (const std::exception& error) { overlapErrors.push_back({{"partIds", {a.part->id, b.part->id}}, {"diagnostic", error.what()}}); }
    }
    const bool geometryUsable = invalid.empty() && empty.empty() && !solids.empty();
    add("part-overlap", "Positive-volume solid overlaps", !geometryUsable ? "not-checked" : !overlaps.empty() ? plate ? "failed" : "warning" : skipped || !overlapErrors.empty() ? "not-checked" : "passed", "geometry", overlaps.empty() ? activeIds : Affected(overlaps),
        "Exact Manifold intersection after disjoint AABB rejection; overlap threshold 1e-9 mm^3. Assembly intersections require intent review",
        {{"overlaps", overlaps}, {"exactPairs", exact}, {"broadPhaseDisjointPairs", broadDisjoint}, {"uncheckedPairs", skipped}, {"pairLimitOmissions", omittedPairs}, {"triangleLimitOmissions", triangleSkipped}, {"errors", overlapErrors}, {"pairLimit", kPairLimit}, {"pairTriangleLimit", kPairTriangles}, {"triangleBudget", kTriangleBudget}},
        overlaps.empty() && !skipped && overlapErrors.empty() ? json::array() : json::array({"Separate overlapping plate parts; review assembly interfaces; simplify complex solids or check remaining pairs in the slicer."}));
    add("plate-clearance", "Part, brim and support allowance", !plate || !canPlate || !allowances ? "not-checked" : !clearanceConflicts.empty() ? "warning" : omittedPairs ? "not-checked" : bedProvisional ? "warning" : "passed", "heuristic", clearanceConflicts.empty() ? activeIds : Affected(clearanceConflicts),
        "Conservative XY AABB gap; required gap = partGap + 2*(brim+support). Numeric allowances do not verify slicer support or brim generation",
        {{"possibleConflicts", clearanceConflicts}, {"requiredGap", allowances ? json(requiredGap) : json(nullptr)}, {"unknownAllowances", !allowances}, {"uncheckedPairs", omittedPairs}},
        {"Confirm part gap, brim and support allowances; inspect exact footprints and generated paths in the slicer."});
    Quantities(output, design, basis);
    std::set<std::string> manifestEntries;
    if (metadata.is_object() && metadata.contains("views") && metadata["views"].is_object())
        for (const auto& entry : metadata["views"]) if (entry.is_string()) manifestEntries.insert(entry.get<std::string>());
    const bool graphAvailable = design.is_object() && design.contains("instances") && design["instances"].is_array() && design.contains("sourceParts") && design["sourceParts"].is_array() && design.contains("views") && design["views"].is_array();
    add("quantity-scope", "Quantity coverage scope", manifestEntries.size() > 1 || !graphAvailable ? "not-checked" : "passed", "geometry", json::array(),
        "Quantities cover only the currently loaded normalized shared design graph; no independent module is evaluated",
        {{"scope", "current-shared-design-graph"}, {"manifestEntryCount", manifestEntries.size()}, {"otherModulesEvaluated", false}},
        manifestEntries.size() > 1 ? json::array({"Review quantities in each independent manifest entry; this report cannot certify complete-project counts across modules."}) : json::array());
    for (const auto& unsupported : std::vector<std::array<std::string, 4>>{
        {"minimum-thickness", "Minimum wall thickness", "heuristic", "Measure critical walls/clearances and compare with nozzle, line width and material requirements."},
        {"strength", "Mechanical strength", "physical", "Test load-bearing coupons or perform an engineering analysis for the intended material, loads and environment."},
        {"support-removal", "Support generation and removal", "sliced", "Generate supports in the slicer, inspect accessibility, and test removal on a representative sample."},
        {"layer-direction", "Layer direction and anisotropy", "heuristic", "Review layer direction against expected loads; validate critical orientations with a printed sample."},
        {"toolpath", "Sliced toolpaths and machine settings", "sliced", "Slice with a verified machine/material setup and inspect toolpaths, first layer, travel and clearance."}})
        add(unsupported[0], unsupported[1], "not-checked", unsupported[2], activeIds, "Unsupported by solid geometry checks; no verification inferred", json::object(), {unsupported[3]});
    return output;
}
} // namespace dingcad
