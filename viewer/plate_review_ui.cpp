#include "plate_review_ui.h"

#include "dimensions.h"
#include "guided_pick_ui.h"
#include "raymath.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <unordered_set>
#include <utility>

namespace dingcad {
namespace {
using json = nlohmann::json;

constexpr float margin = 12.0f;
constexpr float padding = 12.0f;
constexpr float bodyPadding = 8.0f;
constexpr float bodyFontSize = 16.0f;
constexpr float bodyLineHeight = 20.0f;
constexpr float sectionHeight = 24.0f;
constexpr float controlHeight = 32.0f;
constexpr float scrollbarGutter = 8.0f;

const Color ink{39, 52, 46, 255};
const Color muted{91, 108, 98, 255};
const Color accent{48, 108, 78, 255};
const Color glass{201, 211, 199, 235};
const Color paper{247, 246, 239, 250};
const Color buttonPaper{234, 239, 228, 245};
const Color disabledPaper{226, 228, 220, 245};
const Color resultPass{70, 119, 83, 255};
const Color resultWarning{179, 119, 39, 255};
const Color resultFailure{165, 66, 58, 255};

const json *Field(const json &value, const char *name) {
  if (!value.is_object()) return nullptr;
  const auto found = value.find(name);
  return found == value.end() ? nullptr : &*found;
}

bool HasText(const json *value) {
  return value && value->is_string() &&
         !value->get_ref<const std::string &>().empty();
}

std::string Text(const json *value, const std::string &fallback = "Unknown") {
  if (!value || value->is_null()) return fallback;
  if (value->is_string()) return value->get<std::string>();
  if (value->is_boolean()) return value->get<bool>() ? "Yes" : "No";
  if (value->is_number()) return value->dump();
  return fallback;
}

bool IsDigest(const std::string &text) {
  return text.size() == 64 &&
         std::all_of(text.begin(), text.end(), [](unsigned char character) {
           return (character >= '0' && character <= '9') ||
                  (character >= 'a' && character <= 'f') ||
                  (character >= 'A' && character <= 'F');
         });
}

std::string DisplayIdentifier(const std::string &text) {
  return IsDigest(text) ? text.substr(0, 12) + "…" : text;
}

std::string Identifier(const json *value, const std::string &fallback = "Unknown") {
  if (!HasText(value)) return fallback;
  return DisplayIdentifier(value->get<std::string>());
}

std::string Humanize(const std::string &key) {
  std::string result;
  result.reserve(key.size() + 4);
  bool capitalize = true;
  char previous = 0;
  for (const unsigned char character : key) {
    if (character == '_' || character == '-' || character == '.') {
      if (!result.empty() && result.back() != ' ') result.push_back(' ');
      capitalize = true;
      previous = ' ';
      continue;
    }
    if (std::isupper(character) && !result.empty() && previous != ' ' &&
        std::islower(static_cast<unsigned char>(previous)))
      result.push_back(' ');
    const char output = capitalize
                            ? static_cast<char>(std::toupper(character))
                            : static_cast<char>(character);
    result.push_back(output);
    capitalize = false;
    previous = static_cast<char>(character);
  }
  return result;
}

std::string Named(const json *value,
                  const std::string &fallback = "Not recorded") {
  if (!value || value->is_null()) return fallback;
  if (value->is_string()) return value->get<std::string>();
  if (!value->is_object()) return Text(value, fallback);
  const auto *name = Field(*value, "name");
  const auto *id = Field(*value, "id");
  if (HasText(name) && HasText(id))
    return name->get<std::string>() + " (" +
           DisplayIdentifier(id->get<std::string>()) + ")";
  if (HasText(name)) return name->get<std::string>();
  if (HasText(id)) return DisplayIdentifier(id->get<std::string>());
  return fallback;
}

bool VectorOfNumbers(const json *value, size_t count) {
  if (!value || !value->is_array() || value->size() != count) return false;
  return std::all_of(value->begin(), value->end(), [](const json &item) {
    return item.is_number() && std::isfinite(item.get<double>());
  });
}

std::string Number(const json &value) {
  if (!value.is_number() || !std::isfinite(value.get<double>())) return "?";
  std::ostringstream output;
  output << std::setprecision(6) << value.get<double>();
  return output.str();
}

std::string VectorText(const json *value, size_t count) {
  if (!VectorOfNumbers(value, count)) return "Unknown";
  std::string result;
  for (const auto &component : *value) {
    if (!result.empty()) result += ", ";
    result += Number(component);
  }
  return result;
}

std::string ArrayText(const json *value, const std::string &empty = "None") {
  if (!value || !value->is_array() || value->empty()) return empty;
  std::string result;
  for (const auto &item : *value) {
    if (!result.empty()) result += ", ";
    if (item.is_string()) result += item.get<std::string>();
    else result += Text(&item, "Unknown");
  }
  return result;
}

bool HasListedField(const json *list, const std::string &field) {
  if (!list || !list->is_array()) return false;
  return std::any_of(list->begin(), list->end(), [&](const json &item) {
    return item.is_string() && item.get<std::string>() == field;
  });
}

std::string StatusLabel(const std::string &status) {
  if (status == "passed" || status == "pass" || status == "ok") return "Passed";
  if (status == "warning" || status == "warn") return "Warning";
  if (status == "failed" || status == "fail" || status == "error") return "Failed";
  if (status == "not-checked" || status == "not_checked" || status == "unknown")
    return "Not checked";
  if (status.empty()) return "Status unknown";
  return status;
}

int StatusRank(const json &check) {
  const std::string status = Text(Field(check, "result"), "");
  if (status == "failed" || status == "fail" || status == "error") return 0;
  if (status == "warning" || status == "warn") return 1;
  if (status == "not-checked" || status == "not_checked" || status == "unknown")
    return 2;
  if (status == "passed" || status == "pass" || status == "ok") return 3;
  return 4;
}

std::string CheckSummary(const json *checks, bool current) {
  if (!checks || !checks->is_array() || checks->empty())
    return current ? "Check results: None recorded."
                   : "Last check results: None recorded.";
  std::array<size_t, 5> counts{};
  for (const auto &check : *checks) ++counts[StatusRank(check)];
  std::string result = current ? "Check results: " : "Last check results: ";
  result += std::to_string(counts[0]) + " failed · " +
            std::to_string(counts[1]) + " warning · " +
            std::to_string(counts[2]) + " not checked · " +
            std::to_string(counts[3]) + " passed";
  if (counts[4]) result += " · " + std::to_string(counts[4]) + " other";
  if (!current) result += " (report not current)";
  return result;
}

std::string CheckScope(const json &check) {
  const auto *scope = Field(check, "scope");
  if (!HasText(scope)) return "Unscoped";
  return Humanize(scope->get<std::string>());
}

std::vector<std::string> PartIds(const json *value) {
  std::vector<std::string> ids;
  if (!value || !value->is_array()) return ids;
  ids.reserve(value->size());
  for (const auto &item : *value)
    if (item.is_string() && !item.get_ref<const std::string &>().empty())
      ids.push_back(item.get<std::string>());
  return ids;
}

bool IsCurrent(const json &report) {
  const auto *current = Field(report, "current");
  return !current || !current->is_boolean() || current->get<bool>();
}

bool ReadBed(const json &bed, std::array<double, 3> &size, double &originX,
             double &originY);
bool BedGeometryWithinRenderRange(const std::array<double, 3> &size,
                                  double originX, double originY);

struct DocumentBuilder {
  std::vector<PlateReviewRow> rows;
  std::unordered_set<std::string> visiblePartIds;
  bool current = true;

  void Add(std::string text, bool section = false, std::string result = {}) {
    PlateReviewRow row;
    row.text = std::move(text);
    row.section = section;
    row.result = std::move(result);
    rows.push_back(std::move(row));
  }

  void AddHighlight(const std::vector<std::string> &ids) {
    if (ids.empty()) return;
    PlateReviewRow row;
    row.highlightButton = true;
    row.enabled = current;
    row.highlightIds = ids;
    row.text = current
                   ? "Highlight and frame affected parts (" +
                         std::to_string(ids.size()) + ")"
                   : "Affected parts unavailable while this report is stale";
    rows.push_back(std::move(row));
  }

  void Section(const std::string &name) { Add(name, true); }

  void Empty(const json *array) {
    if (!array || !array->is_array() || array->empty()) Add("None recorded.");
  }

  void Flatten(const json &value, const std::string &label, int depth = 0) {
    if (depth > 6) {
      Add(label + ": Additional details omitted.");
      return;
    }
    if (value.is_object()) {
      if (value.empty()) {
        Add(label + ": None recorded.");
        return;
      }
      for (const auto &[key, child] : value.items()) {
        if (key == "extensions" || key == "customMetadata") {
          Add(label + ": Custom metadata is available.");
          continue;
        }
        const std::string childLabel = label.empty() ? Humanize(key)
                                                      : label + " / " + Humanize(key);
        Flatten(child, childLabel, depth + 1);
      }
      return;
    }
    if (value.is_array()) {
      if (value.empty()) {
        Add(label + ": None recorded.");
        return;
      }
      for (size_t index = 0; index < value.size(); ++index) {
        const auto &item = value[index];
        const std::string itemLabel = label + " " + std::to_string(index + 1);
        if (item.is_object() || item.is_array()) Flatten(item, itemLabel, depth + 1);
        else Add(itemLabel + ": " + Text(&item, "Unknown"));
      }
      return;
    }
    std::string text = Text(&value, "Unknown");
    if (value.is_string()) text = DisplayIdentifier(text);
    Add((label.empty() ? "Detail" : label) + ": " + text);
  }

  void AddProfile(const json &report) {
    Section("Printer and profile");
    const auto *profileStatus = Field(report, "profileStatus");
    const auto *profileContextPointer = Field(report, "profile");
    const auto *settings = Field(report, "settings");
    const json empty = json::object();
    const json &profileContext = profileContextPointer && profileContextPointer->is_object()
                                     ? *profileContextPointer
                                     : empty;
    const json &settingsObject = settings && settings->is_object()
                                     ? *settings
                                     : empty;
    const std::string profileState = Text(
        profileStatus, Text(Field(profileContext, "status"), "Unknown"));
    Add("Profile status: " + profileState);

    const auto *missing = Field(profileContext, "missing");
    const auto *missingIdentifiers = Field(profileContext, "missingIdentifiers");
    const auto *provisional = Field(profileContext, "provisional");
    const auto *activeProfile = Field(profileContext, "activeProfile");
    const auto *profileDataPointer = Field(profileContext, "profile");
    const json &profileData = profileDataPointer && profileDataPointer->is_object()
                                  ? *profileDataPointer
                                  : empty;
    const std::string activeId = Identifier(activeProfile, "");
    const std::string profileName = Named(profileDataPointer, "");
    std::string activeText = profileName;
    if (!activeId.empty()) {
      if (activeText.empty()) activeText = activeId;
      else if (activeText != activeId) activeText += " (" + activeId + ")";
    }
    Add("Active profile: " + (activeText.empty() ? "Not set" : activeText));

    const json *printer = Field(profileData, "printer");
    const bool printerMissing = HasListedField(missing, "printer") ||
                                HasListedField(missing, "printer.id");
    if (!printer || printer->is_null())
      Add(std::string("Printer: ") + (printerMissing ? "Missing from profile" : "Not recorded"));
    else
      Add("Printer: " + Named(printer));

    const json *material = Field(profileData, "material");
    const bool materialMissing = HasListedField(missing, "material") ||
                                 HasListedField(missing, "material.id");
    if (!material || material->is_null())
      Add(std::string("Material: ") + (materialMissing ? "Missing from profile" : "Not recorded"));
    else
      Add("Material: " + Named(material));

    const json *nozzle = Field(profileData, "nozzleDiameter");
    if (!nozzle) nozzle = Field(profileData, "nozzle");
    if (nozzle && nozzle->is_number() && std::isfinite(nozzle->get<double>()))
      Add("Nozzle: " + Number(*nozzle) + " mm");
    else
      Add("Nozzle: " + std::string(HasListedField(missing, "nozzleDiameter") ||
                                         HasListedField(missing, "nozzle")
                                     ? "Missing from profile"
                                     : "Not recorded"));

    const auto *volume = Field(profileData, "buildVolume");
    if (VectorOfNumbers(volume, 3))
      Add("Configured volume: " + VectorText(volume, 3) + " mm");

    Add("Missing profile fields: " +
        ArrayText(missing, profileContextPointer
                               ? (profileState == "complete"
                                      ? "None reported"
                                      : "Not reported; profile status is " + profileState)
                               : "Not included in this report"));
    Add("Missing identifiers: " + ArrayText(missingIdentifiers));
    Add("Provisional profile fields: " +
        ArrayText(provisional, profileContextPointer ? "None reported"
                                                     : "Not included in this report"));
    Add("Slicer preset verification: Not verified.");
    const auto *errors = Field(profileContext, "errors");
    if (errors && errors->is_array()) {
      for (const auto &error : *errors) {
        if (error.is_object()) {
          const std::string path = Text(Field(error, "path"), "Profile");
          Add("Profile issue: " + path + ": " +
              Text(Field(error, "message"), "Invalid profile value"));
        } else {
          Add("Profile issue: " + Text(&error));
        }
      }
    }

    const auto *readiness = Field(profileContext, "checkReadiness");
    for (const auto &[key, label] :
         std::array<std::pair<const char *, const char *>, 3>{{
             {"buildVolume", "Build volume"}, {"nozzle", "Nozzle"},
             {"material", "Material"}}}) {
      const auto *item = readiness ? Field(*readiness, key) : nullptr;
      if (item && item->is_object())
        Add(std::string(label) + " readiness: " +
            Text(Field(*item, "status"), "Unknown") + " — " +
            Text(Field(*item, "reason"), "No detail recorded"));
    }

    AddSettings(settings);
  }

  void AddSettings(const json *settings) {
    Section("Plate check settings");
    if (!settings || !settings->is_object()) {
      Add("None recorded.");
      return;
    }
    const std::array<std::pair<const char *, const char *>, 4> known = {{
        {"partGap", "Part gap"}, {"brim", "Brim"},
        {"support", "Support"}, {"contactTolerance", "Contact tolerance"}}};
    std::vector<std::string> recognized;
    for (const auto &[key, label] : known) {
      const auto *setting = Field(*settings, key);
      if (!setting) continue;
      recognized.emplace_back(key);
      const json *value = setting->is_object() ? Field(*setting, "value") : setting;
      const json *unit = setting->is_object() ? Field(*setting, "unit") : nullptr;
      const json *origin = setting->is_object() ? Field(*setting, "origin") : nullptr;
      std::string valueText = Text(value, "Unknown");
      if (value && value->is_number() && std::isfinite(value->get<double>()))
        valueText = Number(*value);
      const std::string units = value && !value->is_null() && HasText(unit)
                                    ? " " + unit->get<std::string>()
                                    : "";
      std::string originText = Text(origin, "origin unknown");
      if (originText == "numeric-default") originText = "numeric default (provisional)";
      else if (originText == "authored") originText = "authored";
      Add(std::string(label) + ": " + valueText + units + " (" + originText + ")");
    }
    for (const auto &[key, value] : settings->items()) {
      if (std::find(recognized.begin(), recognized.end(), key) != recognized.end())
        continue;
      if (key == "errors") {
        if (value.is_array())
          for (const auto &error : value)
            Add("Settings note: " + Text(&error));
      } else if (key != "extensions") {
        Flatten(value, "Setting / " + Humanize(key));
      }
    }
    if (recognized.empty()) Add("No plate allowances recorded.");
  }

  void AddBasis(const json *basis) {
    if (!basis || !basis->is_object()) return;
    const std::string view = Identifier(Field(*basis, "view"), "Unknown view");
    const std::string modelRevision = Identifier(Field(*basis, "modelRevision"));
    const auto *profileRevision = Field(*basis, "profileRevision");
    const std::string profile = !profileRevision || profileRevision->is_null()
                                    ? "profile independent"
                                    : "profile " + Identifier(profileRevision);
    Add("Basis: view " + view + " · model " + modelRevision + " · " + profile);
  }

  void AddBed(const json *bed) {
    Section("Build plate");
    if (!bed || bed->is_null() || !bed->is_object()) {
      Add("Bed dimensions: Unknown; printer bed data is not recorded.");
      Add("Bed exclusions: Unknown.");
      return;
    }
    const auto *size = Field(*bed, "size");
    if (VectorOfNumbers(size, 3))
      Add("Bed dimensions: " + VectorText(size, 3) + " mm");
    else
      Add("Bed dimensions: Unknown; bed data is incomplete.");
    std::array<double, 3> parsedSize{};
    double originX = 0;
    double originY = 0;
    if (ReadBed(*bed, parsedSize, originX, originY) &&
        !BedGeometryWithinRenderRange(parsedSize, originX, originY))
      Add("Bed dimensions exceed viewer rendering range; numeric check context remains available.");
    const auto *origin = Field(*bed, "origin");
    if (VectorOfNumbers(origin, 2))
      Add("Bed origin: " + VectorText(origin, 2) + " mm");

    const auto *exclusions = Field(*bed, "exclusions");
    if (!exclusions || !exclusions->is_array() || exclusions->empty()) {
      Add("Bed exclusions: None recorded.");
      return;
    }
    for (const auto &exclusion : *exclusions) {
      if (!exclusion.is_object()) {
        Add("Bed exclusion: Bounds unavailable.");
        continue;
      }
      const auto *minimum = Field(exclusion, "min");
      const auto *maximum = Field(exclusion, "max");
      if (!VectorOfNumbers(minimum, 2) || !VectorOfNumbers(maximum, 2)) {
        Add("Bed exclusion: Bounds unavailable.");
        continue;
      }
      const std::string name = Text(Field(exclusion, "name"), "Exclusion");
      Add(name + ": " + VectorText(minimum, 2) + " to " +
          VectorText(maximum, 2) + " mm");
    }
  }

  void AddParts(const json *parts) {
    Section("Placed parts");
    Empty(parts);
    if (!parts || !parts->is_array()) return;
    for (const auto &part : *parts) {
      if (!part.is_object()) {
        Add("Part information unavailable.");
        continue;
      }
      if (const auto *id = Field(part, "id"); HasText(id))
        visiblePartIds.insert(id->get<std::string>());
      const std::string name = Text(Field(part, "name"), "Unnamed part");
      const std::string id = Identifier(Field(part, "id"), "");
      Add(id.empty() ? name : name + " · " + id);
      if (HasText(Field(part, "sourcePartId")))
        Add("Source part: " + Identifier(Field(part, "sourcePartId")));
      const auto *rotation = Field(part, "rotation");
      if (VectorOfNumbers(rotation, 3))
        Add("Orientation: " + VectorText(rotation, 3) + " degrees");
      else
        Add("Orientation: Unknown");
      const auto *translation = Field(part, "translation");
      if (VectorOfNumbers(translation, 3))
        Add("Placement: " + VectorText(translation, 3) + " mm");
      else
        Add("Placement: Unknown");
      const auto *bounds = Field(part, "bounds");
      const auto *minimum = bounds ? Field(*bounds, "min") : nullptr;
      const auto *maximum = bounds ? Field(*bounds, "max") : nullptr;
      if (VectorOfNumbers(minimum, 3) && VectorOfNumbers(maximum, 3))
        Add("Bounds: " + VectorText(minimum, 3) + " to " +
            VectorText(maximum, 3) + " mm");
      if (Field(part, "quantity"))
        Add("Intended quantity: " + Text(Field(part, "quantity")));
    }
  }

  void AddQuantities(const json *quantities) {
    Section("Intended quantities");
    Empty(quantities);
    if (!quantities || !quantities->is_array()) return;
    for (const auto &quantity : *quantities) {
      if (!quantity.is_object()) {
        Add("Quantity: " + Text(&quantity));
        continue;
      }
      const auto *name = Field(quantity, "name");
      if (!name) name = Field(quantity, "partName");
      const auto *id = Field(quantity, "sourcePartId");
      if (!id) id = Field(quantity, "partId");
      std::string label = Named(name, "");
      if (label.empty()) label = Identifier(id, "Part");
      const auto *count = Field(quantity, "expected");
      if (!count) count = Field(quantity, "quantity");
      if (!count) count = Field(quantity, "count");
      if (!count) count = Field(quantity, "intended");
      Add(label + ": expected " + (count ? Text(count) : "unknown quantity"));
      const auto *origin = Field(quantity, "expectedOrigin");
      if (HasText(origin)) {
        const std::string source = origin->get<std::string>();
        Add("Expected quantity source: " +
            (source == "authored-source-quantity"
                 ? std::string("authored source-part quantity")
                 : source == "authored-exportable-instance-count"
                       ? std::string("authored exportable-instance count")
                       : Humanize(source)));
      }
      const auto *authoredInstances = Field(quantity, "authoredInstances");
      const auto *placedInstances = Field(quantity, "placedInstances");
      const auto *placements = Field(quantity, "platePlacements");
      if (authoredInstances || placedInstances || placements)
        Add("Authored instances: " + Text(authoredInstances) +
            " · placed instances: " + Text(placedInstances) +
            " · plate placements: " + Text(placements));
      const auto *instanceIds = Field(quantity, "instanceIds");
      if (instanceIds && instanceIds->is_array() && !instanceIds->empty()) {
        std::vector<std::string> readable;
        for (const auto &instanceId : *instanceIds)
          if (instanceId.is_string())
            readable.push_back(DisplayIdentifier(instanceId.get<std::string>()));
        if (!readable.empty()) {
          std::string line = "Instances: ";
          for (const auto &instanceId : readable) {
            if (line.size() > std::string("Instances: ").size()) line += ", ";
            line += instanceId;
          }
          Add(std::move(line));
        }
      }
      const auto *plates = Field(quantity, "plates");
      if (plates && plates->is_array()) {
        for (const auto &plate : *plates) {
          const std::string view = Text(Field(plate, "view"), "Unknown view");
          const std::string instance = Identifier(Field(plate, "instanceId"), "Unknown instance");
          Add("Placement: " + view + " · " + instance);
        }
      }
      const auto *reason = Field(quantity, "reason");
      if (HasText(reason)) Add("Quantity note: " + Text(reason));
    }
  }

  void AddCheck(const json &check) {
    if (!check.is_object()) {
      Add("Check information unavailable.");
      return;
    }
    const std::string result = StatusLabel(Text(Field(check, "result"), ""));
    const std::string name = Text(Field(check, "name"), "Unnamed check");
    Add(name + " — " + result, false, result);
    if (HasText(Field(check, "details"))) Add("Details: " + Text(Field(check, "details")));
    const auto *method = Field(check, "method");
    if (HasText(method)) Add("Method: " + Text(method));
    else if (method && !method->is_null()) Flatten(*method, "Method");
    AddBasis(Field(check, "basis"));
    const auto *evidence = Field(check, "evidence");
    if (evidence && !evidence->is_null()) {
      if (evidence->is_string()) Add("Evidence: " + Text(evidence));
      else if (evidence->is_object() && evidence->contains("summary") &&
               evidence->at("summary").is_string()) {
        Add("Evidence: " + evidence->at("summary").get<std::string>());
        json remainder = *evidence;
        remainder.erase("summary");
        if (!remainder.empty()) Flatten(remainder, "Evidence");
      } else {
        Flatten(*evidence, "Evidence");
      }
    }
    const auto *nextActions = Field(check, "nextActions");
    if (nextActions && nextActions->is_array() && !nextActions->empty()) {
      Add("Next actions:");
      for (const auto &action : *nextActions) {
        if (action.is_string()) {
          Add("- " + action.get<std::string>());
        } else if (action.is_object()) {
          const auto *text = Field(action, "text");
          if (!HasText(text)) text = Field(action, "action");
          if (!HasText(text)) text = Field(action, "description");
          if (HasText(text)) Add("- " + text->get<std::string>());
          json remainder = action;
          if (text) {
            remainder.erase("text");
            remainder.erase("action");
            remainder.erase("description");
          }
          if (!remainder.empty()) Flatten(remainder, "Action detail");
        } else {
          Add("- " + Text(&action));
        }
      }
    } else {
      Add("Next actions: None recorded.");
    }
    const auto ids = PartIds(Field(check, "partIds"));
    std::vector<std::string> visibleIds;
    std::vector<std::string> absentIds;
    for (const auto &id : ids) {
      if (visiblePartIds.find(id) != visiblePartIds.end()) visibleIds.push_back(id);
      else absentIds.push_back(id);
    }
    if (!absentIds.empty()) {
      std::string absent = "Not in this view: ";
      for (size_t index = 0; index < absentIds.size(); ++index) {
        if (index) absent += ", ";
        absent += DisplayIdentifier(absentIds[index]);
      }
      Add(absent + " — open the corresponding assembly/plate.");
    }
    AddHighlight(visibleIds);
  }

  void AddChecks(const json *checks) {
    Section("Checks by scope");
    Empty(checks);
    if (!checks || !checks->is_array()) return;
    std::vector<std::string> scopes;
    for (const auto &check : *checks) {
      const std::string scope = CheckScope(check);
      if (std::find(scopes.begin(), scopes.end(), scope) == scopes.end())
        scopes.push_back(scope);
    }
    for (const auto &scope : scopes) {
      Section(scope + " checks");
      std::vector<const json *> scoped;
      for (const auto &check : *checks)
        if (CheckScope(check) == scope) scoped.push_back(&check);
      std::stable_sort(scoped.begin(), scoped.end(), [](const json *left,
                                                        const json *right) {
        return StatusRank(*left) < StatusRank(*right);
      });
      for (const auto *check : scoped) AddCheck(*check);
    }
  }

  void Build(const json &report) {
    current = IsCurrent(report);
    if (!current) {
      Section("Report status");
      Add("NOT CURRENT — displayed geometry was retained from an earlier state.");
      if (HasText(Field(report, "diagnostic")))
        Add("Diagnostic: " + Text(Field(report, "diagnostic")));
    }
    Add(CheckSummary(Field(report, "checks"), current));

    Section("Active plate");
    Add("View: " + Identifier(Field(report, "view"), "Not identified"));
    Add("Kind: " + Text(Field(report, "kind"), "Unknown"));
    AddBasis(Field(report, "basis"));
    AddProfile(report);
    AddBed(Field(report, "bed"));
    AddParts(Field(report, "parts"));
    AddQuantities(Field(report, "quantities"));
    AddChecks(Field(report, "checks"));
  }
};

Rectangle CardBounds(int width, int height) {
  const float w = std::max(0.0f, static_cast<float>(width));
  const float h = std::max(0.0f, static_cast<float>(height));
  const float x = std::min(margin, w);
  const float y = std::min(margin, h);
  const float cardWidth = std::min(520.0f, std::max(0.0f, w - x - margin));
  const float cardHeight = std::min(h * 0.75f, std::max(0.0f, h - y - margin));
  return {x, y, cardWidth, cardHeight};
}

float BodyTextWidth(Rectangle body) {
  return std::max(0.0f, body.width - 2.0f * bodyPadding - scrollbarGutter);
}

float BodyViewportHeight(Rectangle body) {
  return std::max(0.0f, body.height - 2.0f * bodyPadding);
}

bool Hit(const PanelInput &input, Rectangle rect) {
  return input.pressed && rect.width > 0 && rect.height > 0 &&
         CheckCollisionPointRec(input.mouse, rect);
}

float LogicalScale(int width, float requested) {
  if (std::isfinite(requested) && requested > 0) return requested;
  return width > 0 ? static_cast<float>(GetScreenWidth()) / width : 1.0f;
}

void BeginLogicalClip(Rectangle area, int width, float requestedScale) {
  const float scale = LogicalScale(width, requestedScale);
  const int x = static_cast<int>(std::floor(area.x * scale));
  const int y = static_cast<int>(std::floor(area.y * scale));
  const int right = static_cast<int>(std::ceil((area.x + area.width) * scale));
  const int bottom = static_cast<int>(std::ceil((area.y + area.height) * scale));
  BeginScissorMode(x, y, std::max(0, right - x), std::max(0, bottom - y));
}

Color ResultColor(const std::string &result) {
  if (result == "Passed") return resultPass;
  if (result == "Warning") return resultWarning;
  if (result == "Failed") return resultFailure;
  return muted;
}

void DrawButton(Rectangle rect, const std::vector<std::string> &lines,
                Font font, bool enabled, bool hovered) {
  if (rect.width <= 0 || rect.height <= 0) return;
  Color background = enabled ? buttonPaper : disabledPaper;
  if (enabled && hovered) background = Color{222, 231, 214, 255};
  DrawRectangleRounded(rect, 0.18f, 6, background);
  float y = rect.y + std::max(4.0f, (rect.height - lines.size() * bodyLineHeight) / 2.0f);
  for (const auto &line : lines) {
    DrawTextEx(font, line.c_str(), {rect.x + bodyPadding, y}, bodyFontSize, 0,
               enabled ? accent : muted);
    y += bodyLineHeight;
  }
}

void DrawHeaderClose(Rectangle rect, Font font, bool hovered) {
  if (rect.width <= 0 || rect.height <= 0) return;
  DrawRectangleRounded(rect, 0.18f, 6,
                       hovered ? Color{222, 231, 214, 255} : buttonPaper);
  const char *label = "Close";
  const float width = MeasureTextEx(font, label, 14, 0).x;
  const float x = rect.x + std::max(4.0f, (rect.width - width) / 2.0f);
  const float y = rect.y + std::max(0.0f, (rect.height - 14.0f) / 2.0f);
  DrawTextEx(font, label, {x, y}, 14, 0, ink);
}

constexpr double kMaxPlateCoordinateMm = 1.0e12;

bool ReadBed(const json &bed, std::array<double, 3> &size, double &originX,
             double &originY) {
  const auto *value = Field(bed, "size");
  if (!bed.is_object() || !VectorOfNumbers(value, 3)) return false;
  for (size_t i = 0; i < size.size(); ++i) {
    size[i] = (*value)[i].get<double>();
    if (size[i] <= 0) return false;
  }
  if (const auto *origin = Field(bed, "origin")) {
    if (!VectorOfNumbers(origin, 2)) return false;
    originX = (*origin)[0].get<double>();
    originY = (*origin)[1].get<double>();
  }
  return true;
}

bool BedGeometryWithinRenderRange(const std::array<double, 3> &size,
                                 double originX, double originY) {
  const double maxX = originX + size[0];
  const double maxY = originY + size[1];
  const auto inRange = [](double value) {
    return std::isfinite(value) &&
           std::abs(value) <= kMaxPlateCoordinateMm &&
           std::isfinite(static_cast<float>(value));
  };
  if (!inRange(originX) || !inRange(originY) || !inRange(maxX) ||
      !inRange(maxY) || !inRange(size[2]))
    return false;

  const double scale = static_cast<double>(kSceneScale);
  const float worldX = static_cast<float>(size[0] * scale);
  const float worldY = static_cast<float>(size[1] * scale);
  const float worldZ = static_cast<float>(size[2] * scale);
  const float diagonalSquared = worldX * worldX + worldY * worldY +
                                worldZ * worldZ;
  if (!std::isfinite(diagonalSquared)) return false;

  // Match FrameScene's portrait-independent base distance, then verify the
  // squared camera distance used by raylib remains finite in float arithmetic.
  constexpr float halfFov = 22.5f * DEG2RAD;
  const float frameDistance =
      std::max(0.5f, std::sqrt(diagonalSquared) * 0.55f / std::sin(halfFov));
  if (!std::isfinite(frameDistance) ||
      !std::isfinite(frameDistance * frameDistance))
    return false;

  const float centerX = static_cast<float>((originX + maxX) * 0.5 * scale);
  const float centerY = static_cast<float>((originY + maxY) * 0.5 * scale);
  const float centerZ = worldZ * 0.5f;
  return std::isfinite(centerX) && std::isfinite(centerY) &&
         std::isfinite(centerZ);
}

Vector3 PlatePoint(double x, double y, double z) {
  return CadToWorld({static_cast<float>(x), static_cast<float>(y),
                     static_cast<float>(z)});
}

}  // namespace

bool CanDrawPlateBed(const nlohmann::json &report) {
  const auto *current = Field(report, "current");
  if (Text(Field(report, "kind"), "") != "plate" || !current ||
      !current->is_boolean() || !current->get<bool>())
    return false;
  const auto *bed = Field(report, "bed");
  if (!bed || bed->is_null()) return false;
  std::array<double, 3> size{};
  double originX = 0;
  double originY = 0;
  return ReadBed(*bed, size, originX, originY) &&
         BedGeometryWithinRenderRange(size, originX, originY);
}

PlateReviewLayout PlateReviewUi::Layout(
    const nlohmann::json &report, int width, int height, Font font,
    const PlateReviewTextMeasure &measure) const {
  PlateReviewLayout layout;
  layout.card = CardBounds(width, height);
  const auto card = layout.card;
  const float cardPadding = std::min(padding, std::min(card.width, card.height) * 0.25f);
  const float innerWidth = std::max(0.0f, card.width - 2.0f * cardPadding);
  const float innerHeight = std::max(0.0f, card.height - 2.0f * cardPadding);
  const float headerHeight = std::min(controlHeight, innerHeight);
  layout.header = {card.x + cardPadding, card.y + cardPadding, innerWidth,
                   headerHeight};
  const float closeWidth = std::min(72.0f, layout.header.width);
  layout.close = {layout.header.x + layout.header.width - closeWidth,
                  layout.header.y, closeWidth, layout.header.height};
  const float bodyBottom = card.y + card.height - cardPadding;
  const float bodyGap = std::min(
      8.0f, std::max(0.0f, bodyBottom - (layout.header.y + headerHeight)));
  const float bodyY = std::min(bodyBottom,
                               layout.header.y + headerHeight + bodyGap);
  layout.body = {card.x + cardPadding, bodyY, innerWidth,
                 std::max(0.0f, bodyBottom - bodyY)};

  const PlateReviewTextMeasure textMeasure =
      measure ? measure : PlateReviewTextMeasure([font](const std::string &text) {
        return MeasureTextEx(font, text.c_str(), bodyFontSize, 0).x;
      });
  const bool cacheHit = !measure && cacheValid && cachedWidth == width &&
                        cachedHeight == height && cachedFontSize == font.baseSize &&
                        cachedFontTexture == font.texture.id && cachedReport == report;
  if (cacheHit) {
    layout.rows = cachedRows;
    layout.document = cachedDocument;
    layout.contentHeight = cachedContentHeight;
  } else {
    DocumentBuilder builder;
    builder.Build(report);
    const float textWidth = BodyTextWidth(layout.body);
    float y = layout.body.y + bodyPadding;
    for (auto &row : builder.rows) {
      row.lines = WrapGuidedPickQuestion(row.text, textMeasure, textWidth);
      float rowHeight = row.lines.size() * bodyLineHeight;
      if (row.section)
        rowHeight = std::max(sectionHeight, rowHeight + 4.0f);
      else if (row.highlightButton)
        rowHeight = std::max(controlHeight, rowHeight + 8.0f);
      else
        rowHeight = std::max(bodyLineHeight, rowHeight);
      row.bounds = {layout.body.x + bodyPadding, y,
                    std::max(0.0f, layout.body.width - 2.0f * bodyPadding -
                                                  scrollbarGutter),
                    rowHeight};
      layout.document.push_back(row.text);
      y += rowHeight;
    }
    layout.rows = std::move(builder.rows);
    layout.contentHeight = std::max(0.0f, y - (layout.body.y + bodyPadding));
    if (!measure) {
      cachedReport = report;
      cachedWidth = width;
      cachedHeight = height;
      cachedFontSize = font.baseSize;
      cachedFontTexture = font.texture.id;
      cachedRows = layout.rows;
      cachedDocument = layout.document;
      cachedContentHeight = layout.contentHeight;
      cacheValid = true;
    }
  }

  for (auto &row : layout.rows) {
    row.bounds.y -= scroll;
    if (row.highlightButton)
      layout.highlightButtons.emplace_back(row.highlightIds, row.bounds);
  }
  layout.maxScroll = std::max(0.0f, layout.contentHeight -
                                       BodyViewportHeight(layout.body));
  return layout;
}

Rectangle PlateReviewUi::Bounds(int width, int height) const {
  return CardBounds(width, height);
}

bool PlateReviewUi::CapturesMouse(const PanelInput &input, int width,
                                  int height) const {
  return gesture ||
         (open && CheckCollisionPointRec(input.mouse, Bounds(width, height)));
}

PlateReviewActions PlateReviewUi::Update(const nlohmann::json &report,
                                        const PanelInput &input, Font font,
                                        int width, int height) {
  return Update(report, input, font, width, height, {});
}

PlateReviewActions PlateReviewUi::Update(
    const nlohmann::json &report, const PanelInput &input, Font font,
    int width, int height, const PlateReviewTextMeasure &measure) {
  PlateReviewActions actions;
  lastMouse = input.mouse;
  hasLastMouse = true;
  if (!open) {
    if (!input.leftDown && !input.rightDown && !input.pressed &&
        !input.rightPressed)
      gesture = false;
    return actions;
  }

  auto layout = Layout(report, width, height, font, measure);
  if (input.pressed || input.rightPressed)
    gesture = CheckCollisionPointRec(input.mouse, layout.card);
  else if (!input.leftDown && !input.rightDown)
    gesture = false;

  if (CheckCollisionPointRec(input.mouse, layout.body))
    scroll -= input.wheel * bodyLineHeight * 3.0f;
  scroll = Clamp(scroll, 0.0f, layout.maxScroll);

  if (input.escape || Hit(input, layout.close)) {
    actions.close = true;
    return actions;
  }
  if (Hit(input, layout.body)) {
    layout = Layout(report, width, height, font, measure);
    if (IsCurrent(report)) {
      for (const auto &button : layout.highlightButtons) {
        if (button.first.empty() || button.second.width <= 0 ||
            button.second.height <= 0 ||
            !CheckCollisionPointRec(input.mouse, button.second))
          continue;
        actions.highlight = button.first;
        actions.frame = !actions.highlight.empty();
        break;
      }
    }
  }
  return actions;
}

void PlateReviewUi::Draw(const nlohmann::json &report, Font font, int width,
                         int height, float uiScale) const {
  if (!open) return;
  const auto layout = Layout(report, width, height, font);
  DrawRectangleRounded(layout.card, 0.045f, 8, glass);
  const float titleY = layout.header.y +
                       std::max(0.0f, (layout.header.height - 18.0f) / 2.0f);
  DrawTextEx(font, "Manufacturing review", {layout.header.x, titleY}, 18, 0, ink);
  DrawHeaderClose(layout.close, font,
                  hasLastMouse && CheckCollisionPointRec(lastMouse, layout.close));
  DrawRectangleRounded(layout.body, 0.035f, 6, paper);

  BeginLogicalClip(layout.body, width, uiScale);
  for (const auto &row : layout.rows) {
    if (row.bounds.y + row.bounds.height < layout.body.y ||
        row.bounds.y > layout.body.y + layout.body.height)
      continue;
    if (row.section) {
      DrawRectangleRounded({row.bounds.x, row.bounds.y + 1,
                            row.bounds.width, row.bounds.height - 2},
                           0.15f, 5, buttonPaper);
      DrawTextEx(font, row.text.c_str(),
                 {row.bounds.x + bodyPadding, row.bounds.y + 3},
                 bodyFontSize, 0, accent);
    } else if (row.highlightButton) {
      DrawButton(row.bounds, row.lines, font, row.enabled,
                 row.enabled && hasLastMouse &&
                     CheckCollisionPointRec(lastMouse, row.bounds));
    } else {
      float y = row.bounds.y;
      if (!row.result.empty()) {
        DrawRectangleRounded({row.bounds.x, y + 2, 3, bodyLineHeight - 4},
                             0.3f, 3, ResultColor(row.result));
      }
      const float x = row.bounds.x + (!row.result.empty() ? 8.0f : 0.0f);
      for (const auto &line : row.lines) {
        DrawTextEx(font, line.c_str(), {x, y}, bodyFontSize, 0,
                   row.result.empty() ? ink : ResultColor(row.result));
        y += bodyLineHeight;
      }
    }
  }
  EndScissorMode();

  if (layout.maxScroll > 0.0f && layout.body.height > 0.0f) {
    const Rectangle track{layout.body.x + layout.body.width - 5.0f,
                          layout.body.y + bodyPadding, 3.0f,
                          BodyViewportHeight(layout.body)};
    const float ratio = std::min(1.0f, track.height / layout.contentHeight);
    const float thumbHeight = std::max(18.0f, track.height * ratio);
    const float offset = layout.maxScroll > 0
                             ? (track.height - thumbHeight) * scroll / layout.maxScroll
                             : 0.0f;
    DrawRectangleRounded(track, 0.5f, 4, Color{214, 220, 208, 255});
    DrawRectangleRounded({track.x, track.y + offset, track.width, thumbHeight},
                         0.5f, 4, muted);
  }
}

void DrawPlateBed(const nlohmann::json &report) {
  if (!CanDrawPlateBed(report)) return;
  const auto *bed = Field(report, "bed");
  std::array<double, 3> size{};
  double originX = 0;
  double originY = 0;
  if (!ReadBed(*bed, size, originX, originY)) return;

  const Color boundary{80, 133, 96, 230};
  const Color exclusionColor{201, 122, 42, 245};
  const double surfaceZ = 0.0;
  const std::array<Vector3, 4> lower{
      PlatePoint(originX, originY, surfaceZ),
      PlatePoint(originX + size[0], originY, surfaceZ),
      PlatePoint(originX + size[0], originY + size[1], surfaceZ),
      PlatePoint(originX, originY + size[1], surfaceZ)};
  const std::array<Vector3, 4> upper{
      PlatePoint(originX, originY, size[2]),
      PlatePoint(originX + size[0], originY, size[2]),
      PlatePoint(originX + size[0], originY + size[1], size[2]),
      PlatePoint(originX, originY + size[1], size[2])};
  for (size_t index = 0; index < lower.size(); ++index) {
    const size_t next = (index + 1) % lower.size();
    DrawLine3D(lower[index], lower[next], boundary);
    DrawLine3D(upper[index], upper[next], boundary);
    DrawLine3D(lower[index], upper[index], Fade(boundary, 0.65f));
  }

  const auto *exclusions = Field(*bed, "exclusions");
  if (!exclusions || !exclusions->is_array()) return;
  for (const auto &exclusion : *exclusions) {
    if (!exclusion.is_object()) continue;
    const auto *minimum = Field(exclusion, "min");
    const auto *maximum = Field(exclusion, "max");
    if (!VectorOfNumbers(minimum, 2) || !VectorOfNumbers(maximum, 2)) continue;
    const double minX = std::clamp((*minimum)[0].get<double>(), originX,
                                   originX + size[0]);
    const double minY = std::clamp((*minimum)[1].get<double>(), originY,
                                   originY + size[1]);
    const double maxX = std::clamp((*maximum)[0].get<double>(), originX,
                                   originX + size[0]);
    const double maxY = std::clamp((*maximum)[1].get<double>(), originY,
                                   originY + size[1]);
    if (maxX <= minX || maxY <= minY) continue;
    const Vector3 a = PlatePoint(minX, minY, surfaceZ + 0.1);
    const Vector3 b = PlatePoint(maxX, minY, surfaceZ + 0.1);
    const Vector3 c = PlatePoint(maxX, maxY, surfaceZ + 0.1);
    const Vector3 d = PlatePoint(minX, maxY, surfaceZ + 0.1);
    DrawLine3D(a, b, exclusionColor);
    DrawLine3D(b, c, exclusionColor);
    DrawLine3D(c, d, exclusionColor);
    DrawLine3D(d, a, exclusionColor);
    DrawLine3D(a, c, Fade(exclusionColor, 0.65f));
    DrawLine3D(b, d, Fade(exclusionColor, 0.65f));
  }
}

}  // namespace dingcad
