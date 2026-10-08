#include "printer_profile.h"

#include <iostream>
#include <limits>
#include <stdexcept>

using nlohmann::json;
using namespace synthcad;
namespace {
void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
json Complete() {
    return {{"activeProfile", "custom"}, {"profiles", {{"custom", {
        {"name", u8"Stampante città 日本"}, {"printer", {{"id", "custom-printer"}, {"name", "Custom"}}},
        {"buildVolume", {220, 200, 250}}, {"exclusions", json::array()}, {"nozzleDiameter", 0.4},
        {"material", {{"id", "pla"}, {"name", "PLA"}}}, {"provenance", {{"type", "user"}}},
        {"provisional", json::array()}}}}}};
}
void Invalid(const json& project, const char* message) {
    const auto context = PrinterProfileContext(project);
    Check(context["status"] == "invalid" && !context["errors"].empty(), message);
    Check(context["checkReadiness"]["buildVolume"]["status"] == "invalid", "invalid readiness cannot pass");
}
bool Contains(const json& list, const std::string& item) {
    for (const auto& value : list) if (value == item) return true;
    return false;
}
}
int main() {
    try {
        auto project = Complete();
        const auto complete = PrinterProfileContext(project);
        Check(complete["status"] == "complete", "custom profile complete");
        Check(complete["bedOrigin"] == json({0, 0}), "bed origin is fixed");
        Check(complete["profile"]["name"] == u8"Stampante città 日本", "Unicode metadata preserved");
        Check(complete["profileRevision"].get<std::string>().size() == 64, "SHA256 revision");
        Check(complete["checkReadiness"]["buildVolume"]["status"] == "ready", "bed metadata ready");
        Check(!complete["slicerPresetVerified"].get<bool>(), "never verify presets");

        project["profiles"]["custom"]["slicer"] = {{"id", "arbitrary"}, {"version", "1"}, {"presetId", "unverified"}};
        Check(!PrinterProfileContext(project)["slicerPresetVerified"].get<bool>(), "IDs do not prove preset verification");
        Check(PrinterProfileContext(project)["profileRevision"] != complete["profileRevision"], "authored metadata changes revision");
        project = Complete();
        project["profiles"]["custom"]["buildVolume"] = nullptr;
        auto context = PrinterProfileContext(project);
        Check(context["status"] == "incomplete" && Contains(context["missing"], "buildVolume"), "null dimensions incomplete");
        Check(context["checkReadiness"]["buildVolume"]["status"] == "missing", "missing dimensions readiness");
        project["profiles"]["custom"].erase("buildVolume");
        Check(PrinterProfileContext(project)["profileRevision"] == context["profileRevision"], "null and omitted normalize alike");
        for (const auto& volume : {json({0, 200, 250}), json({220, -1, 250}), json({220, 200}), json({"220", 200, 250}), json({220, 200, std::numeric_limits<double>::infinity()})}) {
            project = Complete(); project["profiles"]["custom"]["buildVolume"] = volume;
            Invalid(project, "invalid dimensions rejected");
        }
        for (const auto& diameter : {json(0), json(-0.4), json("0.4"), json(std::numeric_limits<double>::quiet_NaN())}) {
            project = Complete(); project["profiles"]["custom"]["nozzleDiameter"] = diameter;
            Invalid(project, "invalid nozzle rejected");
        }
        project = Complete();
        project["profiles"]["custom"]["exclusions"] = {{{"name", "clip"}, {"min", {0, 0}}, {"max", {10, 20}}}};
        Check(PrinterProfileContext(project)["status"] == "complete", "valid edge rectangle");
        for (const auto& rect : {json({{"min", {-1, 0}}, {"max", {10, 20}}}), json({{"min", {0, 0}}, {"max", {0, 20}}}), json({{"min", {0, 0}}, {"max", {221, 20}}}), json({{"min", {0, 0}}})}) {
            project["profiles"]["custom"]["exclusions"] = json::array({rect});
            Invalid(project, "invalid exclusion rejected");
        }
        project = Complete();
        project["profiles"]["custom"].erase("exclusions");
        Check(PrinterProfileContext(project)["checkReadiness"]["buildVolume"]["status"] == "missing", "unknown exclusions prevent bed readiness");

        project = Complete();
        project["profiles"]["custom"]["material"].erase("id");
        context = PrinterProfileContext(project);
        Check(context["status"] == "incomplete" && Contains(context["missingIdentifiers"], "material.id"), "identifier absence reported separately");
        Check(context["checkReadiness"]["material"]["status"] == "ready", "named material enables readiness independently of ID");
        project["profiles"]["custom"]["material"] = json::object();
        Check(PrinterProfileContext(project)["checkReadiness"]["material"]["status"] == "missing", "empty material cannot enable readiness");

        project = Complete();
        project["profiles"]["custom"]["provisional"] = {"material", "buildVolume"};
        context = PrinterProfileContext(project);
        Check(context["status"] == "incomplete", "provisional setup remains incomplete");
        Check(context["checkReadiness"]["material"]["status"] == "provisional", "material is provisional");
        Check(context["profileRevision"] != complete["profileRevision"], "provisional affects revision");
        project["profiles"]["custom"]["provisional"] = {"buildVolume", "material"};
        Check(PrinterProfileContext(project)["profileRevision"] == context["profileRevision"], "provisional set normalized");
        project["profiles"]["custom"]["provisional"] = {"material", "material"};
        Invalid(project, "duplicate provisional rejected");
        project["profiles"]["custom"]["provisional"] = {"material.id"};
        Invalid(project, "unsupported provisional path rejected");

        project = Complete();
        project["profiles"]["other"] = {{"buildVolume", {0, 1, 2}}};
        Invalid(project, "unselected profiles also validate");
        Check(PrinterProfileContext(project)["profileRevision"] == complete["profileRevision"], "unselected metadata does not affect revision");
        project["profiles"]["other"] = project["profiles"]["custom"];
        Check(PrinterProfileContext(project)["status"] == "complete", "valid unselected profile");
        project["activeProfile"] = "other";
        Check(PrinterProfileContext(project)["profileRevision"] != complete["profileRevision"], "selection identity affects revision");
        project.erase("activeProfile");
        context = PrinterProfileContext(project);
        Check(context["profile"].is_null() && context["profileRevision"].is_null(), "no implicit selection");
        Check(context["status"] == "incomplete", "no active profile incomplete");
        Check(PrinterProfileContext(json::object())["profile"].is_null(), "project switching cannot inherit prior profile");
        project["activeProfile"] = "absent";
        Invalid(project, "dangling active ID rejected");
        project["activeProfile"] = nullptr;
        Invalid(project, "null active ID rejected");
        Invalid(json::array(), "non-object project rejected");
        Invalid({{"profiles", nullptr}}, "null profiles rejected");
        Invalid({{"profiles", {{"", json::object()}}}}, "empty profile ID rejected");

        project = Complete();
        project["profiles"]["custom"]["bedOrigin"] = {0, 0};
        Invalid(project, "unknown profile field rejected");
        project["profiles"]["custom"].erase("bedOrigin");
        project["profiles"]["custom"]["printer"]["unexpected"] = true;
        Invalid(project, "unknown nested field rejected");
        project = Complete();
        project["profiles"]["custom"]["extensions"] = {{"custom", {{"arbitrary", nullptr}}}};
        Check(PrinterProfileContext(project)["status"] == "complete", "extensions support custom metadata");
        project["profiles"]["custom"]["provenance"]["type"] = "guess";
        Invalid(project, "unknown provenance rejected");
        Check(PrinterProfileContext(PrinterProfileTemplate())["status"] == "incomplete", "template supports incomplete review");
        for (const auto& text : {std::string("bad\0text", 8), std::string("\xff", 1), std::string(4097, 'a')}) {
            project = Complete(); project["profiles"]["custom"]["name"] = text;
            Invalid(project, "invalid profile text rejected");
            project = Complete(); project["profiles"]["custom"]["printer"]["name"] = text;
            Invalid(project, "invalid nested text rejected");
            project = Complete(); project["profiles"]["custom"]["extensions"] = {{"text", text}};
            Invalid(project, "invalid extension text rejected");
            project = Complete(); project["profiles"]["invalid"]["name"] = text;
            Invalid(project, "invalid unselected text rejected");
        }
        for (const auto& id : {std::string(), std::string("a\0b", 3), std::string("\xff", 1), std::string(129, 'a')}) {
            project = Complete(); project["activeProfile"] = id;
            Invalid(project, "invalid active ID rejected");
            project = Complete(); project["profiles"][id] = json::object();
            Invalid(project, "invalid profile key rejected");
            for (const auto* field : {"printer", "material", "slicer"}) {
                project = Complete(); project["profiles"]["custom"][field]["id"] = id;
                Invalid(project, "invalid nested ID rejected");
            }
        }
        project = Complete(); project["profiles"]["custom"]["name"] = "";
        Check(PrinterProfileContext(project)["status"] == "complete", "optional empty names accepted");
        project = Complete();
        for (size_t i = 0; i < 256; ++i) project["profiles"]["extra-" + std::to_string(i)] = json::object();
        Invalid(project, "profile count bounded");
        project = Complete();
        project["profiles"]["custom"]["exclusions"] = json::array();
        for (size_t i = 0; i < 4097; ++i) project["profiles"]["custom"]["exclusions"].push_back({{"min", {0, 0}}, {"max", {1, 1}}});
        Invalid(project, "exclusion count bounded");
        project = Complete(); project["profiles"]["custom"]["printer"]["id"] = std::string(128, 'i');
        project["profiles"]["custom"]["name"] = std::string(4096, 'n');
        Check(PrinterProfileContext(project)["status"] == "complete", "maximum text lengths accepted");
        Check(PrinterProfileContext(Complete()) == complete, "pure repeatable context");
        std::cout << "printer_profile_test passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
