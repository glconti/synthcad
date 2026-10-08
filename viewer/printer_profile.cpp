#include "printer_profile.h"
#include "project_contract.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <string>

namespace synthcad {
namespace {
using json = nlohmann::json;
constexpr size_t kMaxProfiles = 256, kMaxExclusions = 4096;
constexpr size_t kMaxTextBytes = 4096, kMaxIdBytes = 128, kMaxNodes = 65536;
bool Present(const json& value, const char* key) {
    return value.is_object() && value.contains(key) && !value[key].is_null();
}
void Error(json& errors, const std::string& path, const std::string& message) {
    if (errors.size() < 64) errors.push_back({{"path", path}, {"message", message}});
}
bool Text(const std::string& text, bool id = false) {
    if ((id && text.empty()) || text.size() > (id ? kMaxIdBytes : kMaxTextBytes) || text.find('\0') != std::string::npos) return false;
    try { json(text).dump(); } catch (const json::exception&) { return false; }
    return true;
}
bool TextTree(const json& value, size_t& remaining, size_t depth = 0) {
    if (!remaining || depth > 32) return false;
    --remaining;
    if (value.is_string()) return Text(value.get_ref<const std::string&>());
    if (value.is_object()) {
        for (auto it = value.begin(); it != value.end(); ++it)
            if (!Text(it.key()) || !TextTree(it.value(), remaining, depth + 1)) return false;
    } else if (value.is_array()) {
        for (const auto& item : value) if (!TextTree(item, remaining, depth + 1)) return false;
    }
    return true;
}
void Keys(const json& value, const std::set<std::string>& allowed,
          const std::string& path, json& errors) {
    for (auto it = value.begin(); it != value.end(); ++it)
        if (!allowed.count(it.key())) Error(errors, path + "." + it.key(), "Unknown field; use extensions for custom metadata");
}
bool Vector(const json& value, size_t size, bool positive) {
    if (!value.is_array() || value.size() != size) return false;
    for (const auto& number : value) {
        if (!number.is_number()) return false;
        const double n = number.get<double>();
        if (!std::isfinite(n) || (positive ? n <= 0 : n < 0)) return false;
    }
    return true;
}
void Strings(const json& value, const std::string& path, json& errors) {
    for (auto it = value.begin(); it != value.end(); ++it) {
        if (!it.value().is_null() && !it.value().is_string())
            Error(errors, path + "." + it.key(), "Expected a string or null");
        else if (it.value().is_string() && (it.key() == "id" || it.key() == "presetId") &&
                 !Text(it.value().get_ref<const std::string&>(), true))
            Error(errors, path + "." + it.key(), "IDs must contain 1..128 UTF-8 bytes without NUL");
    }
}
void Validate(const json& profile, const std::string& path, json& errors) {
    if (!profile.is_object()) { Error(errors, path, "Expected a profile object"); return; }
    size_t remaining = kMaxNodes;
    if (!TextTree(profile, remaining)) {
        Error(errors, path, "Expected NUL-free valid UTF-8 text of at most 4096 bytes; profile limit is 65536 values and 32 nesting levels");
        return;
    }
    Keys(profile, {"name", "printer", "buildVolume", "exclusions", "nozzleDiameter", "material", "provenance", "provisional", "slicer", "extensions"}, path, errors);
    if (Present(profile, "name") && !profile["name"].is_string()) Error(errors, path + ".name", "Expected a string or null");
    for (const auto* key : {"printer", "material", "slicer", "provenance"}) {
        if (!Present(profile, key)) continue;
        const auto& object = profile[key];
        const auto field = path + "." + key;
        if (!object.is_object()) { Error(errors, field, "Expected an object or null"); continue; }
        if (std::string(key) == "slicer") Keys(object, {"id", "version", "presetId"}, field, errors);
        else if (std::string(key) == "provenance") Keys(object, {"type", "description", "source"}, field, errors);
        else Keys(object, {"id", "name"}, field, errors);
        Strings(object, field, errors);
        if (std::string(key) == "provenance" && Present(object, "type") &&
            object["type"] != "user" && object["type"] != "manufacturer" && object["type"] != "slicer")
            Error(errors, field + ".type", "Expected user, manufacturer, or slicer");
    }
    if (Present(profile, "extensions") && !profile["extensions"].is_object()) Error(errors, path + ".extensions", "Expected an object or null");
    const bool bed = Present(profile, "buildVolume") && Vector(profile["buildVolume"], 3, true);
    if (Present(profile, "buildVolume") && !bed) Error(errors, path + ".buildVolume", "Expected three positive finite dimensions in mm");
    if (Present(profile, "nozzleDiameter")) {
        const auto& n = profile["nozzleDiameter"];
        if (!n.is_number() || !std::isfinite(n.get<double>()) || n.get<double>() <= 0)
            Error(errors, path + ".nozzleDiameter", "Expected a positive finite diameter in mm");
    }
    if (Present(profile, "exclusions")) {
        const auto& rectangles = profile["exclusions"];
        if (!rectangles.is_array()) Error(errors, path + ".exclusions", "Expected an array or null");
        else if (rectangles.size() > kMaxExclusions) Error(errors, path + ".exclusions", "At most 4096 exclusion rectangles are supported");
        else for (size_t i = 0; i < rectangles.size(); ++i) {
            const auto& rect = rectangles[i];
            const auto field = path + ".exclusions[" + std::to_string(i) + "]";
            if (!rect.is_object()) { Error(errors, field, "Expected a rectangle object"); continue; }
            Keys(rect, {"name", "min", "max"}, field, errors);
            if (Present(rect, "name") && !rect["name"].is_string()) Error(errors, field + ".name", "Expected a string or null");
            if (!Present(rect, "min") || !Present(rect, "max") || !Vector(rect["min"], 2, false) || !Vector(rect["max"], 2, false)) {
                Error(errors, field, "Expected nonnegative finite min and max [x,y]"); continue;
            }
            for (size_t axis = 0; axis < 2; ++axis) {
                if (rect["max"][axis].get<double>() <= rect["min"][axis].get<double>()) Error(errors, field, "Rectangle max must exceed min on both axes");
                if (bed && rect["max"][axis].get<double>() > profile["buildVolume"][axis].get<double>()) Error(errors, field, "Rectangle exceeds the build volume bed");
            }
        }
    }
    if (Present(profile, "provisional")) {
        const auto& fields = profile["provisional"];
        const std::set<std::string> allowed = {"printer", "buildVolume", "exclusions", "nozzleDiameter", "material", "provenance"};
        std::set<std::string> seen;
        if (!fields.is_array()) Error(errors, path + ".provisional", "Expected an array or null");
        else if (fields.size() > allowed.size()) Error(errors, path + ".provisional", "At most six unique provisional fields are supported");
        else for (const auto& field : fields) {
            if (!field.is_string() || !allowed.count(field.get<std::string>())) Error(errors, path + ".provisional", "Expected a supported field path");
            else if (!seen.insert(field.get<std::string>()).second) Error(errors, path + ".provisional", "Field paths must be unique");
        }
    }
}
json Normalize(const json& value) {
    if (value.is_object()) {
        json result = json::object();
        for (auto it = value.begin(); it != value.end(); ++it) {
            if (it.value().is_null()) continue;
            result[it.key()] = it.key() == "extensions" ? it.value() : Normalize(it.value());
            if (it.key() == "provisional" && result[it.key()].is_array())
                std::sort(result[it.key()].begin(), result[it.key()].end());
        }
        return result;
    }
    if (value.is_array()) {
        json result = json::array();
        for (const auto& item : value) result.push_back(Normalize(item));
        return result;
    }
    if (value.is_number()) return value.get<double>();
    return value;
}
bool Identifier(const json& object, const char* key) {
    return Present(object, key) && object[key].is_string() && !object[key].get_ref<const std::string&>().empty();
}
} // namespace

json PrinterProfileContext(const json& project) {
    json result = {{"status", "incomplete"}, {"activeProfile", nullptr}, {"profile", nullptr},
        {"bedOrigin", {0, 0}}, {"missing", json::array()}, {"missingIdentifiers", json::array()},
        {"provisional", json::array()}, {"errors", json::array()}, {"profileRevision", nullptr},
        {"slicerPresetVerified", false}, {"checkReadiness", json::object()}};
    auto& errors = result["errors"];
    if (!project.is_object()) Error(errors, "project", "Expected a project object");
    const bool hasProfiles = project.is_object() && project.contains("profiles");
    if (hasProfiles && !project["profiles"].is_object()) Error(errors, "profiles", "Expected an object keyed by stable profile IDs");
    if (hasProfiles && project["profiles"].is_object() && project["profiles"].size() > kMaxProfiles)
        Error(errors, "profiles", "At most 256 project profiles are supported");
    else if (hasProfiles && project["profiles"].is_object()) {
        for (auto it = project["profiles"].begin(); it != project["profiles"].end(); ++it) {
            if (!Text(it.key(), true)) Error(errors, "profiles", "Profile IDs must contain 1..128 UTF-8 bytes without NUL");
            else Validate(it.value(), "profiles." + it.key(), errors);
        }
    }
    const bool hasActive = project.is_object() && project.contains("activeProfile");
    bool selected = false;
    std::string id;
    if (hasActive) {
        if (!project["activeProfile"].is_string() || !Text(project["activeProfile"].get_ref<const std::string&>(), true)) Error(errors, "activeProfile", "Expected a profile ID of 1..128 UTF-8 bytes without NUL");
        else {
            id = project["activeProfile"].get<std::string>();
            result["activeProfile"] = id;
            if (!hasProfiles || !project["profiles"].is_object() || !project["profiles"].contains(id)) Error(errors, "activeProfile", "Selected profile does not exist in this project");
            else if (project["profiles"][id].is_object()) selected = true;
        }
    } else result["missing"].push_back("activeProfile");
    json selectedErrors = json::array();
    if (selected) {
        Validate(project["profiles"][id], "profiles." + id, selectedErrors);
        if (!selectedErrors.empty()) selected = false;
    }
    if (selected) {
        const auto profile = Normalize(project["profiles"][id]);
        result["profile"] = profile;
        // Hash only valid selected authored metadata; unrelated profiles do not
        // affect this identity. Validation errors elsewhere remain visible.
        if (selectedErrors.empty()) {
            try { result["profileRevision"] = Sha256(json({{"id", id}, {"profile", profile}}).dump()); }
            catch (const json::exception&) { Error(errors, "profiles." + id, "Profile metadata must contain valid UTF-8 text"); }
        }
        for (const auto* field : {"printer", "buildVolume", "exclusions", "nozzleDiameter", "material", "provenance"})
            if (!Present(profile, field)) result["missing"].push_back(field);
        for (const auto* field : {"printer", "material"})
            if (!Present(profile, field) || !Identifier(profile[field], "id")) result["missingIdentifiers"].push_back(std::string(field) + ".id");
        if (Present(profile, "provenance") && !Identifier(profile["provenance"], "type")) result["missing"].push_back("provenance.type");
        if (Present(profile, "provisional") && profile["provisional"].is_array()) result["provisional"] = profile["provisional"];
    }
    for (const auto* check : {"buildVolume", "exclusions", "nozzle", "material"}) {
        const std::string field = std::string(check) == "nozzle" ? "nozzleDiameter" : check;
        std::string status = "missing", reason = "No active project profile";
        if (!errors.empty()) { status = "invalid"; reason = "Project profile metadata has validation errors"; }
        else if (selected) {
            const auto& profile = result["profile"];
            bool ready = Present(profile, field.c_str());
            if (field == "material") ready = ready && (Identifier(profile["material"], "id") || Identifier(profile["material"], "name"));
            bool provisional = false;
            for (const auto& marked : result["provisional"])
                if (marked == field) provisional = true;
            status = !ready ? "missing" : provisional ? "provisional" : "ready";
            reason = !ready ? "Required metadata is incomplete" : provisional ? "Authored metadata is provisional" : "Metadata available for this check";
        }
        result["checkReadiness"][check] = {{"status", status}, {"reason", reason}};
    }
    if (!errors.empty()) result["status"] = "invalid";
    else if (selected && result["missing"].empty() && result["missingIdentifiers"].empty() && result["provisional"].empty()) result["status"] = "complete";
    return result;
}

json PrinterProfileTemplate() {
    return {{"activeProfile", "custom"}, {"profiles", {{"custom", {
        {"name", "Custom printer"}, {"printer", {{"id", nullptr}, {"name", nullptr}}},
        {"buildVolume", nullptr}, {"exclusions", nullptr}, {"nozzleDiameter", nullptr},
        {"material", {{"id", nullptr}, {"name", nullptr}}},
        {"provenance", {{"type", "user"}, {"description", nullptr}}},
        {"provisional", json::array()}}}}}};
}
} // namespace synthcad
