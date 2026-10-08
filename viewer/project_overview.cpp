#include "project_overview.h"

#include <cmath>
#include <map>
#include <set>
#include <string>

namespace synthcad {
namespace {
using json = nlohmann::json;
constexpr std::size_t MaxRecords = 1000;
constexpr std::size_t MaxCharacters = 10000;

// Count Unicode scalar values, rejecting malformed UTF-8 and embedded NUL.
bool Text(const json& value, bool nonempty = true) {
    if (!value.is_string()) return false;
    const auto& s = value.get_ref<const std::string&>();
    if (nonempty && s.empty()) return false;
    std::size_t count = 0;
    for (std::size_t i = 0; i < s.size();) {
        const auto c = static_cast<unsigned char>(s[i++]);
        unsigned scalar = c;
        unsigned continuation = 0;
        unsigned minimum = 0;
        if (c == 0) return false;
        if (c < 0x80) {}
        else if (c >= 0xc2 && c <= 0xdf) { continuation = 1; scalar = c & 31; minimum = 0x80; }
        else if (c >= 0xe0 && c <= 0xef) { continuation = 2; scalar = c & 15; minimum = 0x800; }
        else if (c >= 0xf0 && c <= 0xf4) { continuation = 3; scalar = c & 7; minimum = 0x10000; }
        else return false;
        while (continuation--) {
            if (i == s.size()) return false;
            const auto next = static_cast<unsigned char>(s[i++]);
            if ((next & 0xc0) != 0x80) return false;
            scalar = (scalar << 6) | (next & 63);
        }
        if (scalar < minimum || scalar > 0x10ffff || (scalar >= 0xd800 && scalar <= 0xdfff)) return false;
        if (++count > MaxCharacters) return false;
    }
    return true;
}

struct Validator {
    json& errors;
    void Error(const std::string& path, const std::string& message) {
        errors.push_back({{"path", path}, {"message", message}});
    }
    void String(const json& object, const std::string& key, const std::string& path,
                bool required = true, bool nonempty = true) {
        if (!object.contains(key)) {
            if (required) Error(path + "." + key, "required field is missing");
        } else if (!Text(object[key], nonempty)) {
            Error(path + "." + key, "expected NUL-free UTF-8 string of at most 10000 characters" +
                  std::string(nonempty ? " (nonempty)" : ""));
        }
    }
    void Enum(const json& object, const std::string& key, const std::string& path,
              const std::set<std::string>& values) {
        if (!object.contains(key) || !object[key].is_string() ||
            !values.count(object[key].get<std::string>()))
            Error(path + "." + key, "missing or unsupported value");
    }
    void Fields(const json& object, const std::set<std::string>& allowed, const std::string& path) {
        for (auto it = object.begin(); it != object.end(); ++it)
            if (!allowed.count(it.key())) Error(path + "." + it.key(), "unknown field; use extensions for custom metadata");
        if (object.contains("extensions") && !object["extensions"].is_object())
            Error(path + ".extensions", "expected object");
    }
    void Strings(const json& object, const std::string& key, const std::string& path) {
        if (!object.contains(key)) return;
        const auto& array = object[key];
        if (!array.is_array() || array.size() > MaxRecords) {
            Error(path + "." + key, "expected array of at most 1000 strings");
            return;
        }
        for (std::size_t i = 0; i < array.size(); ++i)
            if (!Text(array[i])) Error(path + "." + key + "[" + std::to_string(i) + "]", "expected nonempty NUL-free UTF-8 string");
    }
    void Basis(const json& object, const std::string& path) {
        if (!object.contains("basis")) return;
        const auto& basis = object["basis"];
        const auto bp = path + ".basis";
        if (!basis.is_object()) { Error(bp, "expected object"); return; }
        Fields(basis, {"view", "modelRevision", "profileRevision", "extensions"}, bp);
        String(basis, "view", bp);
        String(basis, "modelRevision", bp);
        if (!basis.contains("profileRevision")) Error(bp + ".profileRevision", "required string or explicit null is missing");
        else if (!basis["profileRevision"].is_null()) String(basis, "profileRevision", bp);
    }
    bool Record(const json& record, const std::string& section, const std::string& path) {
        const auto before = errors.size();
        if (!record.is_object()) { Error(path, "expected object"); return false; }
        std::set<std::string> fields = {"id", "partIds", "extensions"};
        String(record, "id", path);
        Strings(record, "partIds", path);
        if (section == "measurements") {
            fields.insert({"name", "value", "unit", "status", "notes"});
            String(record, "name", path); String(record, "unit", path);
            String(record, "notes", path, false, false);
            Enum(record, "status", path, {"measured", "provisional"});
            if (!record.contains("value") || !record["value"].is_number() ||
                !std::isfinite(record["value"].get<double>())) Error(path + ".value", "expected finite number");
        } else if (section == "assumptions") {
            fields.insert({"text", "status"});
            String(record, "text", path);
            Enum(record, "status", path, {"provisional", "confirmed"});
        } else {
            fields.insert("basis"); Basis(record, path);
            if (section == "checks") {
                fields.insert({"name", "result", "scope", "details"});
                String(record, "name", path); String(record, "details", path, false, false);
                Enum(record, "result", path, {"passed", "warning", "failed", "not-checked"});
                Enum(record, "scope", path, {"geometry", "heuristic", "sliced", "physical"});
            } else if (section == "exports") {
                fields.insert({"path", "format", "createdAt"});
                String(record, "path", path); String(record, "createdAt", path, false);
                Enum(record, "format", path, {"stl", "3mf"});
            } else {
                fields.insert({"text", "stage", "attachments"});
                String(record, "text", path); Strings(record, "attachments", path);
                Enum(record, "stage", path, {"proposed", "printed", "tested", "superseded"});
            }
        }
        Fields(record, fields, path);
        return before == errors.size();
    }
};

void Freshness(json& record, const json& profile, const json& views) {
    auto set = [&](const char* freshness, const char* reason) {
        record["freshness"] = freshness; record["reason"] = reason;
    };
    if (!record.contains("basis")) { set("unbound", "No revision basis was authored."); return; }
    const auto& basis = record["basis"];
    if (!basis["profileRevision"].is_null() && profile.is_object() &&
        profile.contains("profileRevision") && Text(profile["profileRevision"]) &&
        profile["profileRevision"] != basis["profileRevision"]) {
        set("stale", "The printer profile revision differs from the authored basis."); return;
    }
    const json* view = nullptr;
    if (views.is_array()) for (const auto& candidate : views) {
        if (candidate.is_object() && candidate.contains("id") && candidate["id"] == basis["view"]) {
            if (view) { set("unknown", "The referenced view is ambiguous."); return; }
            view = &candidate;
        }
    }
    if (!view) { set("unknown", "The referenced view is unknown."); return; }
    if (!view->contains("loaded") || (*view)["loaded"] != true ||
        !view->contains("modelRevision") || !Text((*view)["modelRevision"])) {
        set("unknown", "The referenced view has no loaded model revision in this session."); return;
    }
    if (view->contains("sourceCurrent") && (*view)["sourceCurrent"] == false) {
        set("stale", "The referenced view source has changed."); return;
    }
    if ((*view)["modelRevision"] != basis["modelRevision"]) {
        set("stale", "The model revision differs from the authored basis."); return;
    }
    if (!view->contains("sourceCurrent") || (*view)["sourceCurrent"] != true) {
        set("unknown", "Current view source state is unavailable."); return;
    }
    if (!basis["profileRevision"].is_null()) {
        if (!profile.is_object() || !profile.contains("status") || profile["status"] != "complete" ||
            !profile.contains("profileRevision") || !Text(profile["profileRevision"])) {
            set("unknown", "A complete active printer profile revision is unavailable."); return;
        }
    }
    set("current", "The authored basis matches the loaded model and required profile revisions.");
}
} // namespace

nlohmann::json ProjectOverview(const json& metadata, const json& profile, const json& views) {
    json output = {{"errors", json::array()}, {"revisionSources", {
        {"modelRevision", "Opaque revision supplied by the loaded view; excludes authored overview records."},
        {"profileRevision", "Revision supplied by the active printer profile context; null basis means profile-independent."},
        {"freshness", "Revision comparison only; authored results are not engine or physical verification."}}}};
    Validator validator{output["errors"]};
    if (!metadata.is_object()) validator.Error("$", "expected metadata object");
    std::size_t validCount = 0;
    for (const auto* section : {"measurements", "assumptions", "checks", "exports", "evidence"}) {
        output[section] = json::array();
        if (!metadata.is_object() || !metadata.contains(section)) continue;
        const auto& records = metadata[section];
        const auto path = std::string("$.") + section;
        if (!records.is_array()) { validator.Error(path, "expected array"); continue; }
        if (records.size() > MaxRecords) validator.Error(path, "section exceeds 1000 records; excess records rejected");
        std::map<std::string, std::size_t> ids;
        for (const auto& record : records)
            if (record.is_object() && record.contains("id") && Text(record["id"])) ++ids[record["id"].get<std::string>()];
        for (std::size_t i = 0; i < records.size() && i < MaxRecords; ++i) {
            const auto rp = path + "[" + std::to_string(i) + "]";
            bool valid = validator.Record(records[i], section, rp);
            if (records[i].is_object() && records[i].contains("id") && Text(records[i]["id"]) &&
                ids[records[i]["id"].get<std::string>()] > 1) {
                validator.Error(rp + ".id", "duplicate ID within section; all duplicates rejected"); valid = false;
            }
            if (!valid) continue;
            auto record = records[i]; record["origin"] = "authored";
            if (std::string(section) == "checks" || std::string(section) == "exports" || std::string(section) == "evidence")
                Freshness(record, profile, views);
            output[section].push_back(std::move(record)); ++validCount;
        }
    }
    output["status"] = output["errors"].empty() ? "valid" : (validCount ? "partial" : "invalid");
    return output;
}
} // namespace synthcad
