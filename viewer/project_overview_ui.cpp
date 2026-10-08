#include "project_overview_ui.h"

#include "guided_pick_ui.h"
#include "raymath.h"

#include <algorithm>
#include <cmath>
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

const json *Field(const json &value, const char *name) {
  if (!value.is_object()) return nullptr;
  const auto found = value.find(name);
  return found == value.end() ? nullptr : &*found;
}

std::string ValueText(const json *value, const std::string &fallback = "Unknown") {
  if (!value || value->is_null()) return fallback;
  if (value->is_string()) return value->get<std::string>();
  if (value->is_boolean()) return value->get<bool>() ? "true" : "false";
  if (value->is_number()) return value->dump();
  return value->dump(-1, ' ', false, json::error_handler_t::replace);
}

std::string StringField(const json &value, const char *name,
                        const std::string &fallback = "Unknown") {
  return ValueText(Field(value, name), fallback);
}

bool HasText(const json *value) {
  return value && value->is_string() && !value->get_ref<const std::string &>().empty();
}

std::string JoinArray(const json *value, const std::string &empty = "None") {
  if (!value || !value->is_array() || value->empty()) return empty;
  std::string result;
  for (const auto &item : *value) {
    if (!result.empty()) result += ", ";
    result += ValueText(&item, "null");
  }
  return result;
}

std::string JsonDetails(const json *value) {
  if (!value || value->is_null()) return "";
  return value->dump(2, ' ', false, json::error_handler_t::replace);
}

std::string ProfileIdentity(const json &profile, const std::string &activeId) {
  const auto *name = Field(profile, "name");
  if (HasText(name) && !activeId.empty())
    return name->get<std::string>() + " (" + activeId + ")";
  if (HasText(name)) return name->get<std::string>();
  if (!activeId.empty()) return activeId;
  return "Unknown";
}

std::string ShortIdentifier(const json *value) {
  if (!HasText(value)) return "Unknown";
  const auto &text = value->get_ref<const std::string &>();
  const bool digest = text.size() == 64 &&
      std::all_of(text.begin(), text.end(), [](unsigned char character) {
        return (character >= '0' && character <= '9') ||
               (character >= 'a' && character <= 'f') ||
               (character >= 'A' && character <= 'F');
      });
  return digest ? text.substr(0, 12) + "…" : text;
}

std::string ShortLabel(const std::string &text) {
  const bool digest = text.size() == 64 &&
      std::all_of(text.begin(), text.end(), [](unsigned char character) {
        return (character >= '0' && character <= '9') ||
               (character >= 'a' && character <= 'f') ||
               (character >= 'A' && character <= 'F');
      });
  return digest ? text.substr(0, 12) + "…" : text;
}

std::string ListField(const json &object, const char *name) {
  const auto *value = Field(object, name);
  if (!value || !value->is_array()) return "Unknown";
  return JoinArray(value, "None");
}

std::string NamedIdentity(const json *value) {
  if (!value || value->is_null()) return "Unknown";
  if (value->is_string()) return value->get<std::string>();
  if (!value->is_object()) return ValueText(value);
  const auto *name = Field(*value, "name");
  const auto *id = Field(*value, "id");
  if (HasText(name) && HasText(id))
    return name->get<std::string>() + " (" + ShortLabel(id->get<std::string>()) + ")";
  if (HasText(name)) return name->get<std::string>();
  if (HasText(id)) return ShortLabel(id->get<std::string>());
  return "Unknown";
}

std::string BuildVolume(const json *value) {
  if (!value || !value->is_array() || value->size() != 3) return "Unknown";
  return ValueText(&(*value)[0]) + " × " + ValueText(&(*value)[1]) + " × " +
         ValueText(&(*value)[2]) + " mm";
}

std::string Origin(const json &view) {
  const auto *loaded = Field(view, "loaded");
  if (loaded && loaded->is_boolean() && !loaded->get<bool>())
    return "not loaded";
  const auto *current = Field(view, "sourceCurrent");
  if (!loaded || !loaded->is_boolean()) return "load state unknown";
  if (!loaded->get<bool>()) return "not loaded";
  if (!current || !current->is_boolean()) return "source state unknown";
  return current->get<bool>() ? "loaded · current" : "loaded · source changed";
}

std::string ViewLabel(const json &overview, const std::string &id) {
  if (id.empty()) return "None";
  const auto *views = Field(overview, "views");
  if (views && views->is_array()) {
    for (const auto &view : *views) {
      if (StringField(view, "id", "") != id) continue;
      const std::string name = StringField(view, "name", "");
      return name.empty() ? ShortLabel(id) : name;
    }
  }
  return ShortLabel(id);
}

bool IsKnownView(const json &overview, const std::string &id) {
  const auto *views = Field(overview, "views");
  if (!views || !views->is_array() || id.empty()) return false;
  size_t matches = 0;
  for (const auto &view : *views) {
    if (StringField(view, "id", "") != id) continue;
    ++matches;
  }
  return matches == 1;
}

struct DocumentBuilder {
  std::vector<ProjectOverviewRow> rows;

  void Add(std::string text, bool section = false) {
    ProjectOverviewRow row;
    row.text = std::move(text);
    row.section = section;
    rows.push_back(std::move(row));
  }

  void AddView(std::string id, std::string text, bool selected) {
    ProjectOverviewRow row;
    row.text = std::move(text);
    row.viewId = std::move(id);
    row.viewButton = true;
    row.selected = selected;
    rows.push_back(std::move(row));
  }

  void Section(const std::string &name) { Add(name, true); }
  void Empty(const json *array) {
    if (!array || !array->is_array() || array->empty()) Add("None recorded.");
  }

  void JsonField(const char *label, const json *value) {
    if (!value || value->is_null()) return;
    Add(std::string(label) + ": " + JsonDetails(value));
  }

  void ArrayField(const char *label, const json *value) {
    if (!value || !value->is_array()) return;
    if (value->empty()) {
      if (std::string(label) == "Required reprint part IDs")
        Add(std::string(label) + ": None");
      return;
    }
    if (std::string(label) != "Part IDs") {
      Add(std::string(label) + ": " + JoinArray(value));
      return;
    }
    std::string ids;
    for (const auto &item : *value) {
      if (!ids.empty()) ids += ", ";
      ids += item.is_string() ? ShortLabel(item.get<std::string>())
                              : ValueText(&item);
    }
    Add("Part IDs: " + ids);
  }

  void Provenance(const json *value) {
    if (!value || value->is_null()) return;
    if (!value->is_object()) {
      Add("Provenance: " + ValueText(value));
      return;
    }
    Add("Provenance: " + StringField(*value, "type"));
    if (HasText(Field(*value, "description")))
      Add("Description: " + StringField(*value, "description", ""));
    if (HasText(Field(*value, "source")))
      Add("Source: " + StringField(*value, "source", ""));
  }

  void Exclusions(const json *value) {
    if (!value || !value->is_array()) return;
    if (value->empty()) {
      Add("Bed exclusions: None");
      return;
    }
    for (const auto &exclusion : *value) {
      const auto *minimum = Field(exclusion, "min");
      const auto *maximum = Field(exclusion, "max");
      if (!minimum || !minimum->is_array() || minimum->size() != 2 ||
          !maximum || !maximum->is_array() || maximum->size() != 2) {
        Add("Bed exclusion: Bounds unavailable");
        continue;
      }
      const std::string name = StringField(exclusion, "name", "Exclusion");
      Add(name + ": " + ValueText(&(*minimum)[0]) + ", " +
          ValueText(&(*minimum)[1]) + " to " + ValueText(&(*maximum)[0]) +
          ", " + ValueText(&(*maximum)[1]) + " mm");
    }
  }

  void Slicer(const json *value) {
    if (!value || !value->is_object()) return;
    std::vector<std::string> details;
    for (const auto *key : {"id", "version", "presetId"}) {
      if (HasText(Field(*value, key)))
        details.push_back(std::string(key) + " " + ShortLabel(StringField(*value, key, "")));
    }
    if (!details.empty()) {
      std::string line = "Slicer metadata: ";
      for (const auto &detail : details) {
        if (line.size() > std::string("Slicer metadata: ").size()) line += " · ";
        line += detail;
      }
      Add(std::move(line));
    }
  }

  void Basis(const json &record) {
    const auto *basis = Field(record, "basis");
    if (!basis || !basis->is_object()) return;
    const auto *profileRevision = Field(*basis, "profileRevision");
    const std::string profile = !profileRevision || profileRevision->is_null()
                                    ? "profile independent"
                                    : "profile " + ShortIdentifier(profileRevision);
    Add("Basis: view " + ShortIdentifier(Field(*basis, "view")) + " · model " +
        ShortIdentifier(Field(*basis, "modelRevision")) + " · " + profile);
  }

  void CompatibilityBasis(const char *label, const json *basis) {
    if (!basis || basis->is_null()) {
      Add(std::string(label) + ": Unknown");
      return;
    }
    if (basis->is_string()) {
      Add(std::string(label) + ": " + ShortLabel(basis->get<std::string>()));
      return;
    }
    if (!basis->is_object()) {
      Add(std::string(label) + ": Unknown");
      return;
    }
    std::vector<std::string> fields;
    for (const auto *key : {"view", "modelRevision"}) {
      const auto *value = Field(*basis, key);
      if (!value || value->is_null()) continue;
      const std::string fieldLabel = std::string(key) == "view" ? "view" : "model";
      fields.push_back(fieldLabel + " " + ShortIdentifier(value));
    }
    if (const auto *profileRevision = Field(*basis, "profileRevision")) {
      fields.push_back(profileRevision->is_null()
                           ? "profile independent"
                           : "profile " + ShortIdentifier(profileRevision));
    }
    std::string line = std::string(label) + ": ";
    if (fields.empty()) line += "Unknown";
    for (const auto &field : fields) {
      if (line.size() > std::string(label).size() + 2) line += " · ";
      line += field;
    }
    Add(std::move(line));
  }

  void ProfileErrors(const json *errors) {
    if (!errors || !errors->is_array() || errors->empty()) return;
    Add("Profile errors:");
    for (const auto &error : *errors) {
      Add("• " + StringField(error, "path") + ": " +
          StringField(error, "message"));
    }
  }

  void AddProfile(const json &overview) {
    Section("Printer profile");
    const auto *profileContext = Field(overview, "profile");
    const json empty = json::object();
    const json &context = profileContext && profileContext->is_object()
                              ? *profileContext
                              : empty;
    const std::string status = StringField(context, "status");
    Add("Status: " + status);
    Add("Active profile: " + ShortLabel(StringField(context, "activeProfile", "Not set")));

    const auto *selectedProfile = Field(context, "profile");
    const json profileData = selectedProfile && selectedProfile->is_object()
                                 ? *selectedProfile
                                 : json::object();
    if (!selectedProfile || selectedProfile->is_null() || !selectedProfile->is_object()) {
      Add("Profile identity: Unknown");
      Add("Printer: Unknown");
      Add("Build volume: Unknown");
      Add("Nozzle: Unknown");
      Add("Material: Unknown");
    } else {
      const std::string activeId =
          ShortLabel(StringField(context, "activeProfile", ""));
      Add("Profile identity: " + ProfileIdentity(profileData, activeId));
      Add("Printer: " + NamedIdentity(Field(profileData, "printer")));
      Add("Build volume: " + BuildVolume(Field(profileData, "buildVolume")));
      const auto *nozzle = Field(profileData, "nozzleDiameter");
      Add("Nozzle: " + (nozzle && nozzle->is_number()
                             ? nozzle->dump() + " mm"
                             : std::string("Unknown")));
      Add("Material: " + NamedIdentity(Field(profileData, "material")));
      Provenance(Field(profileData, "provenance"));
      Exclusions(Field(profileData, "exclusions"));
      Slicer(Field(profileData, "slicer"));
      if (Field(profileData, "extensions"))
        JsonField("Custom metadata", Field(profileData, "extensions"));
    }

    const auto *bedOrigin = Field(context, "bedOrigin");
    if (bedOrigin && bedOrigin->is_array() && bedOrigin->size() == 2)
      Add("Bed origin: " + (*bedOrigin)[0].dump() + " × " +
          (*bedOrigin)[1].dump() + " mm");

    Add("Missing fields: " + ListField(context, "missing"));
    Add("Missing identifiers: " + ListField(context, "missingIdentifiers"));
    Add("Provisional: " + ListField(context, "provisional"));

    const auto *readiness = Field(context, "checkReadiness");
    for (const auto *key : {"buildVolume", "nozzle", "material"}) {
      const auto *item = Field(readiness ? *readiness : empty, key);
      Add(std::string(key) + " readiness: " + StringField(
          item ? *item : empty, "status") + " — " +
          StringField(item ? *item : empty, "reason"));
    }
    if (status == "invalid")
      Add("Profile setup has errors. Ask your agent to run synthcad docs profiles.");
    else if (status != "complete")
      Add("Profile setup is incomplete. Ask your agent to run synthcad docs profiles.");
    Add("Slicer preset verification: Unknown; a preset has not been verified.");
    if (Field(context, "profileRevision"))
      Add("Profile revision: " + ShortIdentifier(Field(context, "profileRevision")));
    ProfileErrors(Field(context, "errors"));
  }

  void AddMeasurements(const json *records) {
    Section("Measurements");
    Empty(records);
    if (!records || !records->is_array()) return;
    for (const auto &record : *records) {
      Add(ShortIdentifier(Field(record, "id")) + " · " + StringField(record, "name") +
          " — " + ValueText(Field(record, "value")) +
          " " + StringField(record, "unit", ""));
      Add("Status: " + StringField(record, "status"));
      if (HasText(Field(record, "notes"))) Add("Notes: " + StringField(record, "notes", ""));
      ArrayField("Part IDs", Field(record, "partIds"));
      JsonField("Custom metadata", Field(record, "extensions"));
    }
  }

  void AddAssumptions(const json *records) {
    Section("Assumptions");
    Empty(records);
    if (!records || !records->is_array()) return;
    for (const auto &record : *records) {
      Add(ShortIdentifier(Field(record, "id")) + " · " + StringField(record, "text"));
      Add("Status: " + StringField(record, "status"));
      ArrayField("Part IDs", Field(record, "partIds"));
      JsonField("Custom metadata", Field(record, "extensions"));
    }
  }

  void AddChecks(const json *records) {
    Section("Checks");
    Empty(records);
    if (!records || !records->is_array()) return;
    for (const auto &record : *records) {
      Add(ShortIdentifier(Field(record, "id")) + " · " + StringField(record, "name") +
          " — " + StringField(record, "result") +
          " · " + StringField(record, "scope"));
      if (HasText(Field(record, "details")))
        Add("Details: " + StringField(record, "details", ""));
      Add("Freshness: " + StringField(record, "freshness") + " — " +
          StringField(record, "reason"));
      Basis(record);
      ArrayField("Part IDs", Field(record, "partIds"));
      JsonField("Custom metadata", Field(record, "extensions"));
    }
  }

  void AddExports(const json *records) {
    Section("Exports");
    Empty(records);
    if (!records || !records->is_array()) return;
    for (const auto &record : *records) {
      Add(ShortIdentifier(Field(record, "id")) + " · " + StringField(record, "path") +
          " · " + StringField(record, "format"));
      if (HasText(Field(record, "createdAt")))
        Add("Created: " + StringField(record, "createdAt", ""));
      Add("Freshness: " + StringField(record, "freshness") + " — " +
          StringField(record, "reason"));
      Basis(record);
      ArrayField("Part IDs", Field(record, "partIds"));
      JsonField("Custom metadata", Field(record, "extensions"));
    }
  }

  std::string ExportFormat(const json &record) {
    const auto format = StringField(record, "format", "unknown");
    if (format == "3mf" || format == "core3mf") return "Core 3MF";
    if (format == "stl") return "STL";
    return format;
  }

  void AddGeneratedQuantities(const json *quantities) {
    if (!quantities || !quantities->is_array() || quantities->empty()) {
      Add("Quantities: None recorded.");
      return;
    }
    for (const auto &quantity : *quantities) {
      Add("Quantity: " + StringField(quantity, "sourcePartId") + " — " +
          ValueText(Field(quantity, "count")) + " instances");
    }
  }

  void AddReceiptProfile(const json *profile) {
    if (!profile || profile->is_null()) {
      Add("Printer context at export: None recorded.");
      return;
    }
    const json *context = profile;
    if (Field(*profile, "profile") && Field(*profile, "profile")->is_object())
      context = Field(*profile, "profile");
    const std::string state = StringField(*profile, "status", "unknown");
    std::string summary = "Printer context at export (" + state + "): ";
    summary += NamedIdentity(Field(*context, "printer"));
    summary += " · " + BuildVolume(Field(*context, "buildVolume"));
    const auto *nozzle = Field(*context, "nozzleDiameter");
    summary += " · nozzle " +
               (nozzle && nozzle->is_number() ? nozzle->dump() + " mm" : "unknown");
    summary += " · " + NamedIdentity(Field(*context, "material"));
    Add(std::move(summary));
  }

  void AddGeneratedExports(const json *records) {
    Section("Generated exports");
    Empty(records);
    if (!records || !records->is_array()) return;
    for (const auto &record : *records) {
      Add(ShortIdentifier(Field(record, "id")) + " · " + ExportFormat(record));
      Add("Path: " + StringField(record, "path"));
      if (HasText(Field(record, "createdAt")))
        Add("Created: " + StringField(record, "createdAt", ""));
      Add("Artifact: " + StringField(record, "artifactStatus") + " — " +
          StringField(record, "artifactReason"));
      Add("Source files: " + StringField(record, "dependencyStatus") + " — " +
          StringField(record, "dependencyReason"));
      Add("Freshness: " + StringField(record, "freshness") + " — " +
          StringField(record, "freshnessReason"));
      Basis(record);
      ArrayField("Part IDs", Field(record, "partIds"));
      AddGeneratedQuantities(Field(record, "quantities"));
      AddReceiptProfile(Field(record, "profile"));
      const auto *checks = Field(record, "checks");
      if (checks && checks->is_array()) {
        if (checks->empty()) Add("Checks: None recorded.");
        for (const auto &check : *checks)
          Add("Check: " + StringField(check, "name") + " — " +
              StringField(check, "result") + " (" + StringField(check, "scope") + ")");
      }
      const auto *risks = Field(record, "risks");
      if (risks && risks->is_array()) {
        for (const auto &risk : *risks) {
          if (risk.is_string()) Add("Risk: " + risk.get<std::string>());
          else if (risk.is_object()) {
            const auto *message = Field(risk, "message");
            if (!message) message = Field(risk, "reason");
            if (!message) message = Field(risk, "text");
            Add("Risk: " + ValueText(message, StringField(risk,"name",StringField(risk,"id","Check")))+
                " ("+StringField(risk,"result","warning")+")");
          }
        }
      }
      if (Field(record, "allowWarnings") && Field(record, "allowWarnings")->is_boolean())
        Add(std::string("Warnings allowed: ") +
            (Field(record, "allowWarnings")->get<bool>() ? "yes" : "no"));
    }
  }

  void AddExportHistoryDiagnostics(const json *diagnostics) {
    Section("Generated export history diagnostics");
    Empty(diagnostics);
    if (!diagnostics || !diagnostics->is_array()) return;
    for (const auto &diagnostic : *diagnostics) {
      if (diagnostic.is_string()) {
        Add("History: " + diagnostic.get<std::string>());
      } else {
        const std::string path = StringField(diagnostic, "path", "History");
        Add(path + ": " +
            StringField(diagnostic, "message", "History record could not be loaded."));
      }
    }
  }

  void AddRootWarnings(const json *warnings) {
    Section("Project metadata warnings");
    Empty(warnings);
    if (!warnings || !warnings->is_array()) return;
    for (const auto &warning : *warnings) {
      if (warning.is_string()) {
        Add("Warning: " + warning.get<std::string>());
      } else {
        const std::string path = StringField(warning, "path", "Project");
        Add(path + ": " + StringField(warning, "message", "Metadata warning."));
        if (HasText(Field(warning, "recordId")))
          Add("Affected record: " + ShortIdentifier(Field(warning, "recordId")));
        ArrayField("Missing evidence IDs", Field(warning, "missingEvidenceIds"));
      }
    }
  }

  void AddEvidence(const json &overview, const json *records) {
    Section("Evidence");
    Add("Optional sample evidence; it is not required to export.");
    Empty(records);
    if (!records || !records->is_array()) return;
    for (const auto &record : *records) {
      Add(ShortIdentifier(Field(record, "id")) + " · " + StringField(record, "text"));
      Add("Stage: " + StringField(record, "stage"));
      Add("Freshness: " + StringField(record, "freshness") + " — " +
          StringField(record, "reason"));
      ArrayField("Attachments", Field(record, "attachments"));
      ArrayField("Part IDs", Field(record, "partIds"));
      ArrayField("Source part IDs", Field(record, "sourcePartIds"));
      Basis(record);
      const auto *observation = Field(record, "observation");
      if (observation && observation->is_object()) {
        Add("Observation: " + StringField(*observation, "kind") + " · " +
            StringField(*observation, "result") +
            " (user-reported; not engine-verified)");
        Add("Reported by: " + StringField(*observation, "reportedBy"));
        Add("Observation details: " + StringField(*observation, "details"));
        if (HasText(Field(*observation, "conditions")))
          Add("Conditions: " + StringField(*observation, "conditions", ""));
        if (HasText(Field(*observation, "recordedAt")))
          Add("Recorded: " + StringField(*observation, "recordedAt", ""));
        JsonField("Custom metadata", Field(*observation, "extensions"));
      }
      const auto *sampleView = Field(record, "sampleView");
      if (HasText(sampleView)) {
        const std::string sampleId = sampleView->get<std::string>();
        const std::string state = StringField(record, "sampleViewStatus", "unknown");
        const std::string reason = StringField(record, "sampleViewReason", "");
        if (state == "available" && IsKnownView(overview, sampleId)) {
          const bool selected = StringField(overview, "displayedView", "") == sampleId;
          AddView(sampleId, "Open sample view: " + ViewLabel(overview, sampleId),
                  selected);
        } else {
          Add("Sample view: " + ViewLabel(overview, sampleId) + " (" +
              (state == "available" ? "unavailable" : "unknown") + ")" +
              (reason.empty() ? "" : " — " + reason));
        }
      }
      JsonField("Custom metadata", Field(record, "extensions"));
    }
  }

  void AddCompatibilityChanges(const json *records) {
    Section("Compatibility and reprint notes");
    Add("Origin: authored; these notes are not an engine verification or certification.");
    Empty(records);
    if (!records || !records->is_array()) return;
    for (const auto &record : *records) {
      Add(ShortIdentifier(Field(record, "id")) + " · " + StringField(record, "text"));
      const std::string status = StringField(record, "status", "unknown");
      Add("Authored status: " + status +
          (status == "unknown" ? " — compatibility is not certified."
                                : " (authored, not certified)"));
      CompatibilityBasis("Target basis", Field(record, "basis"));
      CompatibilityBasis("Previous basis", Field(record, "previousBasis"));
      ArrayField("Impacted part IDs", Field(record, "partIds"));
      ArrayField("Required reprint part IDs", Field(record, "reprintPartIds"));
      const auto *evidenceIds = Field(record, "evidenceIds");
      if (evidenceIds) ArrayField("Related evidence IDs", evidenceIds);
      const std::string evidenceStatus = StringField(record, "evidenceStatus", "unknown");
      if (evidenceStatus == "available" &&
          (!evidenceIds || !evidenceIds->is_array() || evidenceIds->empty()))
        Add("Evidence links: None recorded.");
      else
        Add("Evidence availability: " + evidenceStatus);
      if (HasText(Field(record, "evidenceReason")))
        Add("Evidence note: " + StringField(record, "evidenceReason", ""));
      else if (evidenceStatus == "unknown")
        Add("Evidence note: No supporting evidence is available; compatibility is not certified.");
      Add("Freshness: " + StringField(record, "freshness") + " — " +
          StringField(record, "reason"));
      JsonField("Custom metadata", Field(record, "extensions"));
    }
  }

  void Build(const json &overview) {
    Section("Project");
    Add("Name: " + StringField(overview, "name", "Unnamed project"));
    Add("Path: " + StringField(overview, "path"));
    const auto *standalone = Field(overview, "standalone");
    Add(std::string("Type: ") +
        (standalone && standalone->is_boolean() && standalone->get<bool>()
             ? "Standalone file"
             : "Project"));
    Add("Overview status: " + StringField(overview, "status"));
    const auto *metadataCurrent = Field(overview, "metadataCurrent");
    if (metadataCurrent && metadataCurrent->is_boolean() &&
        !metadataCurrent->get<bool>())
      Add("Showing last loaded project metadata");
    Add("Model revision: " + ShortIdentifier(Field(overview, "modelRevision")));

    Section("Views");
    const auto *views = Field(overview, "views");
    Empty(views);
    const std::string displayed = StringField(overview, "displayedView", "");
    if (views && views->is_array()) {
      for (const auto &view : *views) {
        const std::string id = StringField(view, "id", "");
        const std::string name = StringField(view, "name", id.empty() ? "Unnamed view" : id);
        const std::string kind = StringField(view, "kind", "unknown kind");
        const bool selected = !displayed.empty() && displayed == id;
        AddView(id, std::string(selected ? "Displayed · " : "Open · ") + name +
                        " (" + kind + ")",
                selected);
        const std::string origin = Origin(view);
        const auto *revision = Field(view, "modelRevision");
        const auto *loaded = Field(view, "loaded");
        const bool isNotLoaded = loaded && loaded->is_boolean() && !loaded->get<bool>();
        Add(origin + (!isNotLoaded && HasText(revision)
                          ? " · revision " + ShortIdentifier(revision)
                          : ""));
      }
    }
    Add("Active view: " +
        ViewLabel(overview, StringField(overview, "activeView", "")));
    Add("Displayed view: " + ViewLabel(overview, displayed));

    Section("Geometry status");
    Add("Status: " + StringField(overview, "geometryStatus"));
    if (HasText(Field(overview, "geometryDiagnostic")))
      Add("Diagnostic: " + StringField(overview, "geometryDiagnostic", ""));

    AddProfile(overview);
    if(const auto* checks=Field(overview,"generatedChecks");checks&&checks->is_array()){
      Section("Generated checks");
      Add(StringField(overview,"generatedChecksCurrent", "false")=="true"?"Current displayed revision":"Not current; reload required");
      for(const auto& check:*checks)
        Add(StringField(check,"name")+": "+StringField(check,"result")+" ("+StringField(check,"scope")+")");
      Add("Close this card and choose Checks for evidence and affected parts.");
    }
    Section("Authored records");
    Add("Origin: authored");
    AddMeasurements(Field(overview, "measurements"));
    AddAssumptions(Field(overview, "assumptions"));
    AddChecks(Field(overview, "checks"));
    AddExports(Field(overview, "exports"));
    AddEvidence(overview, Field(overview, "evidence"));
    AddCompatibilityChanges(Field(overview, "compatibilityChanges"));

    AddGeneratedExports(Field(overview, "generatedExports"));
    AddExportHistoryDiagnostics(Field(overview, "exportHistoryDiagnostics"));

    Section("Metadata errors");
    const auto *errors = Field(overview, "errors");
    Empty(errors);
    if (errors && errors->is_array()) {
      for (const auto &error : *errors)
        Add(StringField(error, "path") + ": " + StringField(error, "message"));
    }
    AddRootWarnings(Field(overview, "warnings"));
  }
};

Rectangle CardBounds(int width, int height) {
  const float w = std::max(0.0f, static_cast<float>(width));
  const float h = std::max(0.0f, static_cast<float>(height));
  const float x = std::min(margin, w);
  const float y = std::min(margin, h);
  const float cardWidth = std::min(520.0f, std::max(0.0f, w - x - margin));
  const float cardHeight =
      std::min(h * 0.75f, std::max(0.0f, h - y - margin));
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

void DrawButton(Rectangle rect, const std::vector<std::string> &lines,
                Font font, bool selected, bool hovered) {
  if (rect.width <= 0 || rect.height <= 0) return;
  Color background = selected ? accent : buttonPaper;
  if (hovered) background = selected ? Color{37, 90, 63, 255}
                                    : Color{222, 231, 214, 255};
  DrawRectangleRounded(rect, 0.18f, 6, background);
  const Color color = selected ? WHITE : ink;
  float y = rect.y + std::max(4.0f, (rect.height - lines.size() * bodyLineHeight) / 2.0f);
  for (const auto &line : lines) {
    DrawTextEx(font, line.c_str(), {rect.x + bodyPadding, y}, bodyFontSize, 0,
               color);
    y += bodyLineHeight;
  }
}

void DrawHeaderClose(Rectangle rect, Font font, bool hovered) {
  if (rect.width <= 0 || rect.height <= 0) return;
  DrawRectangleRounded(rect, 0.18f, 6,
                       hovered ? Color{222, 231, 214, 255} : buttonPaper);
  const std::string label = "Close";
  const float textWidth = MeasureTextEx(font, label.c_str(), 14, 0).x;
  const float x = rect.x + std::max(4.0f, (rect.width - textWidth) / 2.0f);
  const float y = rect.y + std::max(0.0f, (rect.height - 14.0f) / 2.0f);
  DrawTextEx(font, label.c_str(), {x, y}, 14, 0, ink);
}

}  // namespace

ProjectOverviewLayout ProjectOverviewUi::Layout(
    const nlohmann::json &overview, int width, int height, Font font,
    const ProjectOverviewTextMeasure &measure) const {
  ProjectOverviewLayout layout;
  layout.card = CardBounds(width, height);
  const auto card = layout.card;
  const float cardPadding =
      std::min(padding, std::min(card.width, card.height) * 0.25f);
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

  const ProjectOverviewTextMeasure textMeasure =
      measure ? measure : ProjectOverviewTextMeasure([font](const std::string &text) {
        return MeasureTextEx(font, text.c_str(), bodyFontSize, 0).x;
      });
  const bool cacheHit = !measure && cacheValid && cachedWidth == width &&
                        cachedHeight == height &&
                        cachedFontSize == font.baseSize &&
                        cachedFontTexture == font.texture.id &&
                        cachedOverview == overview;
  if (cacheHit) {
    layout.rows = cachedRows;
    layout.document = cachedDocument;
    layout.contentHeight = cachedContentHeight;
  } else {
    DocumentBuilder builder;
    builder.Build(overview);

    const float textWidth = BodyTextWidth(layout.body);
    float y = layout.body.y + bodyPadding;
    for (auto &row : builder.rows) {
      const float rowTextWidth =
          row.viewButton ? std::max(0.0f, textWidth - 2.0f * bodyPadding)
                         : textWidth;
      row.lines = WrapGuidedPickQuestion(row.text, textMeasure, rowTextWidth);
      float rowHeight = row.lines.size() * bodyLineHeight;
      if (row.section) rowHeight = std::max(sectionHeight, rowHeight + 4.0f);
      else if (row.viewButton)
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
      cachedOverview = overview;
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
    if (row.viewButton) layout.viewButtons.emplace_back(row.viewId, row.bounds);
  }
  layout.maxScroll = std::max(0.0f, layout.contentHeight -
                                       BodyViewportHeight(layout.body));
  return layout;
}

Rectangle ProjectOverviewUi::Bounds(int width, int height) const {
  return CardBounds(width, height);
}

bool ProjectOverviewUi::CapturesMouse(const PanelInput &input, int width,
                                      int height) const {
  return gesture ||
         (open && CheckCollisionPointRec(input.mouse, Bounds(width, height)));
}

ProjectOverviewActions ProjectOverviewUi::Update(
    const nlohmann::json &overview, const PanelInput &input, Font font,
    int width, int height) {
  return Update(overview, input, font, width, height, {});
}

ProjectOverviewActions ProjectOverviewUi::Update(
    const nlohmann::json &overview, const PanelInput &input, Font font,
    int width, int height, const ProjectOverviewTextMeasure &measure) {
  ProjectOverviewActions actions;
  lastMouse = input.mouse;
  hasLastMouse = true;
  if (!open) {
    if (!input.leftDown && !input.rightDown && !input.pressed &&
        !input.rightPressed)
      gesture = false;
    return actions;
  }

  const auto layout = Layout(overview, width, height, font, measure);
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
    const auto adjusted = Layout(overview, width, height, font, measure);
    for (const auto &button : adjusted.viewButtons) {
      if (button.first.empty() || button.second.width <= 0 ||
          button.second.height <= 0)
        continue;
      if (CheckCollisionPointRec(input.mouse, button.second)) {
        actions.view = button.first;
        break;
      }
    }
  }
  return actions;
}

void ProjectOverviewUi::Draw(const nlohmann::json &overview, Font font,
                             int width, int height, float uiScale) const {
  if (!open) return;
  const auto layout = Layout(overview, width, height, font);
  DrawRectangleRounded(layout.card, 0.045f, 8, glass);

  DrawTextEx(font, "Project overview",
             {layout.header.x, layout.header.y +
                                   std::max(0.0f, (layout.header.height - 18.0f) / 2.0f)},
             18.0f, 0, ink);
  const bool closeHovered = hasLastMouse &&
                            CheckCollisionPointRec(lastMouse, layout.close);
  DrawHeaderClose(layout.close, font, closeHovered);

  DrawRectangleRounded(layout.body, 0.055f, 6, paper);
  BeginLogicalClip(layout.body, width, uiScale);
  const float bodyTextX = layout.body.x + bodyPadding;
  for (const auto &row : layout.rows) {
    if (row.bounds.y + row.bounds.height < layout.body.y ||
        row.bounds.y >= layout.body.y + layout.body.height)
      continue;
    if (row.viewButton) {
      const bool hovered = hasLastMouse &&
                           CheckCollisionPointRec(lastMouse, row.bounds);
      DrawButton(row.bounds, row.lines, font, row.selected, hovered);
      continue;
    }
    const float size = row.section ? 16.0f : bodyFontSize;
    const Color color = row.section ? accent : ink;
    float y = row.bounds.y + (row.section ? 2.0f : 0.0f);
    for (const auto &line : row.lines) {
      DrawTextEx(font, line.c_str(), {bodyTextX, y}, size, 0, color);
      y += bodyLineHeight;
    }
  }

  if (layout.maxScroll > 0 && layout.body.height > 18.0f) {
    const float trackHeight = layout.body.height - 12.0f;
    const float viewport = BodyViewportHeight(layout.body);
    const float thumbHeight = std::max(
        12.0f, trackHeight * viewport /
                   std::max(viewport, layout.contentHeight));
    const float travel = std::max(0.0f, trackHeight - thumbHeight);
    const float clampedScroll = Clamp(scroll, 0.0f, layout.maxScroll);
    const float thumbY = layout.body.y + 6.0f +
                         clampedScroll / layout.maxScroll * travel;
    DrawRectangleRounded({layout.body.x + layout.body.width - 5.0f, thumbY,
                          3.0f, thumbHeight},
                         0.5f, 4, Fade(muted, 0.65f));
  }
  EndScissorMode();
}

}  // namespace dingcad
