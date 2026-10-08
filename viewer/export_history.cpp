#include "export_history.h"

#include "project_contract.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace synthcad {
namespace {
namespace fs = std::filesystem;
using json = nlohmann::json;

constexpr std::uintmax_t kMaxRecordBytes = 1024u * 1024u;
constexpr std::size_t kMaxRecords = 1000;
constexpr std::size_t kMaxDirectoryEntries = 10000;
constexpr std::size_t kMaxDependenciesPerRecord = 4096;
constexpr std::size_t kMaxDependencyChecks = 50000;
constexpr std::uintmax_t kMaxSingleHashBytes = 256u * 1024u * 1024u;
constexpr std::uintmax_t kHashBudgetBytes = 256u * 1024u * 1024u;
constexpr std::uintmax_t kMaxHistoryJsonBytes = 64u * 1024u * 1024u;

struct ReadBudget {
  std::uintmax_t hashBytes = kHashBudgetBytes;
  std::uintmax_t jsonBytes = kMaxHistoryJsonBytes;
  std::size_t dependencyChecks = 0;
  std::map<std::string, std::string> digestCache;
};

std::string PathText(const fs::path &path) { return path.generic_u8string(); }

json Diagnostic(const std::string &path, const std::string &message) {
  return {{"path", path}, {"message", message}};
}

std::string LowerExtension(const fs::path &path) {
  auto extension = path.extension().u8string();
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return extension;
}

fs::path ProjectFile(const fs::path &input) {
  if (input.empty()) throw std::runtime_error("project path is empty");
  std::error_code ec;
  if (fs::is_directory(input, ec)) return input / "synthcad.json";
  if (LowerExtension(input) == ".js") return input;
  return input;
}

fs::path HistoryDirectory(const fs::path &input) {
  const fs::path project = ProjectFile(input);
  const auto identity = Sha256("synthcad-export-history-v1:" + CanonicalPath(project));
  return project.parent_path() / ".synthcad" / "exports" / identity;
}

bool IsHexDigest(const std::string &value) {
  return value.size() == 64 &&
         std::all_of(value.begin(), value.end(), [](unsigned char c) {
           return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
                  (c >= 'A' && c <= 'F');
         });
}

bool IsSafeId(const std::string &id) {
  return !id.empty() && id.size() <= 128 &&
         std::all_of(id.begin(), id.end(), [](unsigned char c) {
           return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
         }) && id != "." && id != "..";
}

bool IsText(const json &value) {
  return value.is_string() && !value.get_ref<const std::string &>().empty() &&
         value.get_ref<const std::string &>().find('\0') == std::string::npos;
}

void Require(bool condition, const std::string &message) {
  if (!condition) throw std::runtime_error(message);
}

void ValidateReceipt(const json &record) {
  Require(record.is_object(), "receipt must be an object");
  Require(record.contains("schemaVersion") && record["schemaVersion"].is_number_integer() &&
              record["schemaVersion"] == 1,
          "schemaVersion must be integer 1");
  Require(record.contains("id") && IsText(record["id"]), "id must be a nonempty string");
  const auto id = record["id"].get<std::string>();
  Require(IsSafeId(id), "id must use only letters, digits, '.', '_' or '-'");
  Require(record.contains("createdAt") && IsText(record["createdAt"]),
          "createdAt must be a nonempty string");
  Require(record.contains("path") && IsText(record["path"]),
          "path must be a nonempty string");
  Require(record.contains("format") && IsText(record["format"]),
          "format must be a nonempty string");
  Require(record.contains("sha256") && record["sha256"].is_string() &&
              IsHexDigest(record["sha256"].get<std::string>()),
          "sha256 must be a 64-character hexadecimal digest");
  Require(record.contains("basis") && record["basis"].is_object(),
          "basis must be an object");
  const auto &basis = record["basis"];
  for (const auto *key : {"view", "kind", "modelRevision", "sourceRevision",
                          "layoutRevision", "profileRevision", "revision"}) {
    Require(basis.contains(key), std::string("basis.") + key + " is required");
    if (std::string(key) == "view" || std::string(key) == "kind")
      Require(IsText(basis[key]), std::string("basis.") + key + " must be a nonempty string");
    else
      Require(basis[key].is_string() || basis[key].is_null(),
              std::string("basis.") + key + " must be a string or null");
  }
  Require(record.contains("partIds") && record["partIds"].is_array(),
          "partIds must be an array");
  for (const auto &part : record["partIds"])
    Require(IsText(part), "partIds entries must be nonempty strings");
  Require(record.contains("quantities") && record["quantities"].is_array(),
          "quantities must be an array");
  for (const auto &quantity : record["quantities"]) {
    bool validCount = false;
    if (quantity.is_object() && quantity.contains("count")) {
      const auto &count = quantity["count"];
      validCount = count.is_number_unsigned() ||
                   (count.is_number_integer() && !count.is_number_unsigned() &&
                    count.get<std::int64_t>() >= 0);
    }
    Require(quantity.is_object() && quantity.contains("sourcePartId") &&
                IsText(quantity["sourcePartId"]) && validCount,
            "each quantity needs a sourcePartId and nonnegative integer count");
  }
  Require(record.contains("dependencies") && record["dependencies"].is_object(),
          "dependencies must be an object");
  for (auto it = record["dependencies"].begin(); it != record["dependencies"].end(); ++it) {
    Require(!it.key().empty() && it.key().find('\0') == std::string::npos,
            "dependency paths must be nonempty strings");
    Require(fs::u8path(it.key()).is_absolute(),
            "dependency paths must be canonical absolute paths");
    Require(it.value().is_string(), "dependency values must be strings");
    const auto value = it.value().get<std::string>();
    Require(IsHexDigest(value) || value == "missing" || value == "unreadable" ||
                value == "changed-during-read",
            "dependency values must be SHA-256 digests or tracked-file sentinels");
  }
  if (record.contains("checks")) Require(record["checks"].is_array(), "checks must be an array");
  if (record.contains("profile"))
    Require(record["profile"].is_object() || record["profile"].is_null(),
            "profile must be an object or null");
  Require(record.contains("projectPath") && IsText(record["projectPath"]),
          "projectPath must be a nonempty string");
}

fs::path AbsoluteArtifactPath(const fs::path &projectPath,
                              const std::string &artifactPath) {
  fs::path output = fs::u8path(artifactPath);
  if (output.is_relative()) output = ProjectFile(projectPath).parent_path() / output;
  std::error_code ec;
  auto absolute = fs::absolute(output, ec);
  if (ec) throw std::runtime_error("cannot resolve export path: " + ec.message());
  auto canonical = fs::weakly_canonical(absolute, ec);
  return ec ? absolute.lexically_normal() : canonical;
}

bool WriteExclusive(const fs::path &path, const std::string &bytes,
                    std::string &error) {
#ifdef _WIN32
  HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    const DWORD code = GetLastError();
    error = code == ERROR_FILE_EXISTS || code == ERROR_ALREADY_EXISTS
                ? "record ID already exists"
                : "cannot create history record (Windows error " +
                      std::to_string(code) + ")";
    return false;
  }
  bool success = true;
  std::size_t offset = 0;
  while (offset < bytes.size()) {
    const DWORD request = static_cast<DWORD>(std::min<std::size_t>(
        bytes.size() - offset, static_cast<std::size_t>(0x7ffff000)));
    DWORD written = 0;
    if (!WriteFile(file, bytes.data() + offset, request, &written, nullptr) ||
        written == 0) {
      success = false;
      error = "cannot write history record";
      break;
    }
    offset += written;
  }
  if (success && !FlushFileBuffers(file)) {
    success = false;
    error = "cannot flush history record";
  }
  CloseHandle(file);
  if (!success) DeleteFileW(path.c_str());
  return success;
#else
  const int file = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
  if (file < 0) {
    error = errno == EEXIST ? "record ID already exists"
                            : "cannot create history record: " +
                                  std::string(std::strerror(errno));
    return false;
  }
  bool success = true;
  std::size_t offset = 0;
  while (offset < bytes.size()) {
    const auto written = ::write(file, bytes.data() + offset, bytes.size() - offset);
    if (written < 0 && errno == EINTR) continue;
    if (written <= 0) {
      success = false;
      error = "cannot write history record: " + std::string(std::strerror(errno));
      break;
    }
    offset += static_cast<std::size_t>(written);
  }
  if (success && ::fsync(file) != 0) {
    success = false;
    error = "cannot flush history record: " + std::string(std::strerror(errno));
  }
  if (::close(file) != 0 && success) {
    success = false;
    error = "cannot close history record: " + std::string(std::strerror(errno));
  }
  if (!success) ::unlink(path.c_str());
  return success;
#endif
}

std::string HashFileBounded(const fs::path &path, ReadBudget &budget,
                            const std::string &cacheKey) {
  const auto cached = budget.digestCache.find(cacheKey);
  if (cached != budget.digestCache.end()) return cached->second;

  std::error_code ec;
  const bool exists = fs::exists(path, ec);
  if (!exists && !ec) return budget.digestCache.emplace(cacheKey, "missing").first->second;
  if (ec || !fs::is_regular_file(path, ec) || ec)
    return budget.digestCache.emplace(cacheKey, "unreadable").first->second;
  const auto size = fs::file_size(path, ec);
  if (ec) return budget.digestCache.emplace(cacheKey, "unreadable").first->second;
  if (size > kMaxSingleHashBytes)
    return budget.digestCache.emplace(cacheKey, "unknown:file exceeds 256 MiB hash limit").first->second;
  if (size > budget.hashBytes)
    return budget.digestCache.emplace(cacheKey, "unknown:history hash budget exhausted").first->second;

  std::ifstream input(path, std::ios::binary);
  if (!input) return budget.digestCache.emplace(cacheKey, "unreadable").first->second;
  std::string bytes;
  bytes.reserve(static_cast<std::size_t>(size));
  std::array<char, 8192> buffer{};
  while (input.read(buffer.data(), static_cast<std::streamsize>(buffer.size())) || input.gcount()) {
    bytes.append(buffer.data(), static_cast<std::size_t>(input.gcount()));
    if (bytes.size() > kMaxSingleHashBytes || bytes.size() > budget.hashBytes)
      return budget.digestCache.emplace(cacheKey, "unknown:history hash budget exhausted").first->second;
  }
  if (input.bad()) return budget.digestCache.emplace(cacheKey, "unreadable").first->second;
  budget.hashBytes -= bytes.size();
  return budget.digestCache.emplace(cacheKey, Sha256(bytes)).first->second;
}

std::pair<std::string, std::string> ArtifactState(const json &record,
                                                   ReadBudget &budget) {
  fs::path path = fs::u8path(record["path"].get<std::string>());
  if (path.is_relative())
    path = ProjectFile(fs::u8path(record["projectPath"].get<std::string>())).parent_path() /
           path;
  const auto key = CanonicalPath(path);
  const std::string actual = HashFileBounded(path, budget, "artifact:" + key);
  if (actual == "missing") return {"missing", "Exported file is missing."};
  if (actual == "unreadable") return {"unknown", "Exported file cannot be read."};
  if (actual.rfind("unknown:", 0) == 0)
    return {"unknown", actual.substr(std::string("unknown:").size()) + "."};
  if (actual != record["sha256"].get<std::string>())
    return {"changed", "File contents no longer match the recorded SHA-256."};
  return {"current", "File contents match the recorded SHA-256."};
}

std::pair<std::string, std::string> DependencyState(const json &record,
                                                     ReadBudget &budget) {
  const auto &dependencies = record["dependencies"];
  if (dependencies.empty()) return {"unknown", "No source dependencies were recorded."};
  if (dependencies.size() > kMaxDependenciesPerRecord)
    return {"unknown", "Dependency count exceeds the history validation limit."};

  bool stale = false;
  bool unknown = false;
  std::string staleReason;
  std::string unknownReason;
  for (auto it = dependencies.begin(); it != dependencies.end(); ++it) {
    if (++budget.dependencyChecks > kMaxDependencyChecks) {
      unknown = true;
      unknownReason = "History dependency validation limit reached.";
      break;
    }
    const fs::path path = fs::u8path(it.key());
    const auto actual = HashFileBounded(path, budget, "dependency:" + CanonicalPath(path));
    const auto expected = it.value().get<std::string>();
    if (actual.rfind("unknown:", 0) == 0 || actual == "unreadable") {
      unknown = true;
      if (unknownReason.empty())
        unknownReason = actual == "unreadable"
                            ? "A source dependency cannot be read."
                            : actual.substr(std::string("unknown:").size()) + ".";
    } else if (actual != expected) {
      stale = true;
      if (staleReason.empty()) staleReason = "A source dependency changed on disk.";
    }
  }
  if (stale) return {"stale", staleReason};
  if (unknown) return {"unknown", unknownReason};
  return {"current", "Recorded source dependencies match disk."};
}

bool ReadTextBounded(const fs::path &path, std::uintmax_t &remaining,
                     std::string &out, std::string &error) {
  std::error_code ec;
  const auto size = fs::file_size(path, ec);
  if (ec) { error = "cannot read record size"; return false; }
  if (size > kMaxRecordBytes) { error = "record exceeds 1 MiB limit"; return false; }
  if (size > remaining) { error = "aggregate history JSON read limit reached"; return false; }
  std::ifstream input(path, std::ios::binary);
  if (!input) { error = "record is unreadable"; return false; }
  std::ostringstream stream;
  std::array<char, 8192> buffer{};
  std::uintmax_t read = 0;
  while (input.read(buffer.data(), static_cast<std::streamsize>(buffer.size())) || input.gcount()) {
    const auto count = static_cast<std::uintmax_t>(input.gcount());
    read += count;
    if (read > kMaxRecordBytes || read > remaining) {
      error = "record grew beyond the history read limit";
      return false;
    }
    stream.write(buffer.data(), static_cast<std::streamsize>(count));
  }
  if (input.bad()) { error = "record read failed"; return false; }
  out = stream.str();
  remaining -= read;
  return true;
}

std::string FreshnessReasonFor(const std::string &key) {
  if (key == "view") return "Active view changed since export.";
  if (key == "modelRevision") return "Model revision changed since export.";
  if (key == "sourceRevision") return "Source revision changed since export.";
  if (key == "layoutRevision") return "Layout revision changed since export.";
  if (key == "profileRevision") return "Profile revision changed since export.";
  return key + " changed since export.";
}

} // namespace

json SaveExportRecord(const fs::path &projectPath, const json &record) {
  try {
    json stored = record;
    ValidateReceipt(stored);
    const auto projectFile = ProjectFile(projectPath);
    const auto receiptProjectFile =
        ProjectFile(fs::u8path(stored["projectPath"].get<std::string>()));
    if (CanonicalPath(projectFile) != CanonicalPath(receiptProjectFile))
      throw std::runtime_error("receipt projectPath does not match the requested project");
    const fs::path artifact = AbsoluteArtifactPath(projectFile, stored["path"].get<std::string>());
    std::error_code ec;
    if (!fs::is_regular_file(artifact, ec) || ec)
      throw std::runtime_error("committed export artifact is missing or not a regular file");

    const std::string serialized = stored.dump(-1, ' ', false, json::error_handler_t::strict);
    if (serialized.size() > kMaxRecordBytes)
      throw std::runtime_error("receipt exceeds 1 MiB limit");

    const auto directory = HistoryDirectory(projectFile);
    fs::create_directories(directory.parent_path(), ec);
    if (ec) throw std::runtime_error("cannot create export history directory: " + ec.message());
    fs::create_directories(directory, ec);
    if (ec) throw std::runtime_error("cannot create project export history directory: " + ec.message());

    std::size_t count = 0;
    std::size_t entries = 0;
    for (fs::directory_iterator it(directory, ec), end; !ec && it != end; it.increment(ec)) {
      if (++entries > kMaxDirectoryEntries)
        throw std::runtime_error("export history directory exceeds the 10000-entry inspection limit");
      if (it->path().extension() == ".json" && ++count >= kMaxRecords)
        throw std::runtime_error("export history already contains 1000 records");
    }
    if (ec) throw std::runtime_error("cannot inspect export history: " + ec.message());

    const auto id = stored["id"].get<std::string>();
    const auto recordPath = directory / fs::u8path(id + ".json");
    std::string writeError;
    if (!WriteExclusive(recordPath, serialized, writeError))
      throw std::runtime_error(writeError);
    return {{"saved", true}, {"path", PathText(recordPath)}, {"error", ""}};
  } catch (const std::exception &error) {
    return {{"saved", false}, {"path", ""}, {"error", error.what()}};
  } catch (...) {
    return {{"saved", false}, {"path", ""}, {"error", "unknown export history error"}};
  }
}

json LoadExportHistory(const fs::path &projectPath) {
  json result = {{"records", json::array()}, {"diagnostics", json::array()}};
  fs::path directory;
  try {
    directory = HistoryDirectory(projectPath);
  } catch (const std::exception &error) {
    result["diagnostics"].push_back(Diagnostic(PathText(projectPath), error.what()));
    return result;
  }

  std::error_code ec;
  if (!fs::exists(directory, ec)) {
    if (ec) result["diagnostics"].push_back(Diagnostic(PathText(directory), "History directory cannot be inspected: " + ec.message()));
    return result;
  }
  if (!fs::is_directory(directory, ec) || ec) {
    result["diagnostics"].push_back(Diagnostic(PathText(directory), "History path is not a readable directory."));
    return result;
  }

  std::vector<fs::path> files;
  std::size_t entriesSeen = 0;
  bool entryLimitHit = false;
  fs::directory_iterator it(directory, ec), end;
  if (ec) {
    result["diagnostics"].push_back(Diagnostic(PathText(directory), "Cannot read history directory: " + ec.message()));
    return result;
  }
  for (; it != end; it.increment(ec)) {
    if (ec) {
      result["diagnostics"].push_back(Diagnostic(PathText(directory), "History scan stopped: " + ec.message()));
      break;
    }
    if (++entriesSeen > kMaxDirectoryEntries) {
      entryLimitHit = true;
      break;
    }
    if (it->path().extension() != ".json") continue;
    std::error_code typeError;
    if (!it->is_regular_file(typeError) || typeError) {
      result["diagnostics"].push_back(Diagnostic(PathText(it->path()), "History entry is not a readable regular file."));
      continue;
    }
    files.push_back(it->path());
    if (files.size() > kMaxRecords) break;
  }
  if (entryLimitHit)
    result["diagnostics"].push_back(Diagnostic(PathText(directory), "History scan stopped after 10000 directory entries."));
  if (files.size() > kMaxRecords) {
    files.resize(kMaxRecords);
    result["diagnostics"].push_back(Diagnostic(PathText(directory), "History contains more than 1000 records; only the first 1000 filenames are inspected."));
  }
  std::sort(files.begin(), files.end());

  ReadBudget budget;
  std::set<std::string> seenIds;
  for (const auto &file : files) {
    std::string bytes, readError;
    if (!ReadTextBounded(file, budget.jsonBytes, bytes, readError)) {
      result["diagnostics"].push_back(Diagnostic(PathText(file), readError));
      continue;
    }
    json record;
    try {
      record = json::parse(bytes);
      ValidateReceipt(record);
    } catch (const std::exception &error) {
      result["diagnostics"].push_back(Diagnostic(PathText(file), std::string("Invalid receipt: ") + error.what()));
      continue;
    }
    const auto id = record["id"].get<std::string>();
    if (file.stem().u8string() != id) {
      result["diagnostics"].push_back(Diagnostic(PathText(file), "Receipt ID does not match its immutable filename."));
      continue;
    }
    if (!seenIds.insert(id).second) {
      result["diagnostics"].push_back(Diagnostic(PathText(file), "Duplicate receipt ID."));
      continue;
    }
    std::pair<std::string, std::string> artifact{
        "unknown", "Artifact status could not be determined."};
    std::pair<std::string, std::string> dependencies{
        "unknown", "Dependency status could not be determined."};
    try {
      artifact = ArtifactState(record, budget);
    } catch (const std::exception &error) {
      result["diagnostics"].push_back(
          Diagnostic(PathText(file), std::string("Artifact check failed: ") + error.what()));
    }
    try {
      dependencies = DependencyState(record, budget);
    } catch (const std::exception &error) {
      result["diagnostics"].push_back(
          Diagnostic(PathText(file), std::string("Dependency check failed: ") + error.what()));
    }
    record["artifactStatus"] = artifact.first;
    record["artifactReason"] = artifact.second;
    record["dependencyStatus"] = dependencies.first;
    record["dependencyReason"] = dependencies.second;
    record["freshness"] = "unknown";
    record["freshnessReason"] = "No current project context has been compared.";
    result["records"].push_back(std::move(record));
  }
  std::sort(result["records"].begin(), result["records"].end(),
            [](const json &left, const json &right) {
              const auto leftTime = left.value("createdAt", std::string());
              const auto rightTime = right.value("createdAt", std::string());
              if (leftTime != rightTime) return leftTime > rightTime;
              return left.value("id", std::string()) > right.value("id", std::string());
            });
  return result;
}

json RefreshExportHistory(const json &history, const json &context) {
  json result = history.is_object() ? history : json::object();
  if (!result.contains("records") || !result["records"].is_array())
    result["records"] = json::array();
  if (!result.contains("diagnostics") || !result["diagnostics"].is_array())
    result["diagnostics"] = json::array();

  constexpr const char *keys[] = {"view", "modelRevision", "sourceRevision",
                                  "layoutRevision", "profileRevision"};
  for (auto &record : result["records"]) {
    if (!record.is_object()) {
      record = json::object();
      record["freshness"] = "unknown";
      record["freshnessReason"] = "Receipt record is malformed.";
      continue;
    }
    if (!record.contains("basis") || !record["basis"].is_object() ||
        !context.is_object()) {
      record["freshness"] = "unknown";
      record["freshnessReason"] = "Receipt basis or current context is unavailable.";
      continue;
    }
    const auto &basis = record["basis"];
    const auto currentView = context.find("view");
    if (currentView == context.end() || !currentView->is_string()) {
      record["freshness"] = "unknown";
      record["freshnessReason"] = "Current view is unavailable.";
      continue;
    }
    const bool sameView = basis.contains("view") && basis["view"] == *currentView;
    const auto dependencyField = record.find("dependencyStatus");
    const std::string dependencyStatus =
        dependencyField != record.end() && dependencyField->is_string()
            ? dependencyField->get<std::string>()
            : "unknown";
    const auto dependencyReasonField = record.find("dependencyReason");
    const std::string dependencyReason =
        dependencyReasonField != record.end() && dependencyReasonField->is_string()
            ? dependencyReasonField->get<std::string>()
            : "Source dependencies could not be confirmed on disk.";
    const auto currentFlag = context.find("current");
    const bool contextCurrent = currentFlag == context.end() ||
                                (currentFlag->is_boolean() && currentFlag->get<bool>());
    const bool currentFlagUnknown = currentFlag != context.end() &&
                                    !currentFlag->is_boolean();
    bool stale = false;
    std::string staleReason, unknownReason;
    // File changes invalidate receipts for every view. A profile change is
    // also globally meaningful because the receipt captures that context.
    if (dependencyStatus == "stale") {
      stale = true;
      staleReason = dependencyReason;
    }
    if (!basis.contains("profileRevision")) {
      unknownReason = "Receipt basis is missing profileRevision.";
    } else {
      const auto profile = context.find("profileRevision");
      if (profile == context.end() || !(profile->is_string() || profile->is_null())) {
        if (unknownReason.empty()) unknownReason = "Current profile revision is unavailable.";
      } else if (basis["profileRevision"] != *profile) {
        stale = true;
        if (staleReason.empty()) staleReason = FreshnessReasonFor("profileRevision");
      }
    }

    // A receipt for a different named view is not made stale merely by
    // selecting another view. Without that view loaded, its saved source
    // dependencies can establish that inputs are unchanged, but cannot prove
    // the active layout revision.
    if (sameView) {
      for (const auto *key : keys) {
        if (std::string(key) == "profileRevision") continue;
        if (!basis.contains(key)) {
          if (unknownReason.empty())
            unknownReason = std::string("Receipt basis is missing ") + key + ".";
          continue;
        }
        const auto current = context.find(key);
        if (current == context.end() ||
            !(current->is_string() || current->is_null())) {
          if (unknownReason.empty())
            unknownReason = std::string("Current ") + key + " is unavailable.";
          continue;
        }
        if (basis[key] != *current) {
          stale = true;
          if (staleReason.empty()) staleReason = FreshnessReasonFor(key);
        }
      }
    }
    if (dependencyStatus != "current" && dependencyStatus != "stale" &&
        unknownReason.empty()) {
      unknownReason = dependencyReason;
    }
    if (currentFlagUnknown && unknownReason.empty())
      unknownReason = "Current project state is unavailable.";
    if (!contextCurrent && unknownReason.empty())
      unknownReason = "Current project view is not loaded or verified.";
    if (!sameView && unknownReason.empty())
      unknownReason = "Another view is active; saved source is unchanged but its layout was not compared.";
    if (sameView && dependencyStatus != "current" && unknownReason.empty())
      unknownReason = "Source dependencies could not be confirmed on disk.";

    if (stale) {
      record["freshness"] = "stale";
      record["freshnessReason"] = staleReason;
    } else if (!unknownReason.empty()) {
      record["freshness"] = "unknown";
      record["freshnessReason"] = unknownReason;
    } else {
      record["freshness"] = "current";
      record["freshnessReason"] = "Receipt basis matches the current project context.";
    }
  }
  return result;
}

json PublishedExportHistory(const json &history) {
  constexpr size_t budget=4*1024*1024, diagnosticBudget=128*1024;
  json result={{"records",json::array()},{"diagnostics",json::array()}};
  size_t used=512, diagnosticBytes=0, omittedRecords=0, omittedDiagnostics=0;
  if(history.is_object()&&history.contains("diagnostics")&&history["diagnostics"].is_array())
    for(const auto& diagnostic:history["diagnostics"]){
      const auto size=diagnostic.dump().size()+1;
      if(diagnosticBytes+size>diagnosticBudget){++omittedDiagnostics;continue;}
      result["diagnostics"].push_back(diagnostic);diagnosticBytes+=size;used+=size;
    }
  bool full=false;
  if(history.is_object()&&history.contains("records")&&history["records"].is_array())
    for(const auto& record:history["records"]){
      const auto size=record.dump().size()+1;
      if(full||used+size>budget){full=true;++omittedRecords;continue;}
      result["records"].push_back(record);used+=size;
    }
  if(omittedRecords||omittedDiagnostics)
    result["diagnostics"].push_back("Published history size limit: omitted "+std::to_string(omittedRecords)+
      " older records and "+std::to_string(omittedDiagnostics)+" diagnostics. Full immutable receipts remain in .synthcad/exports beside the project.");
  result["omittedRecords"]=omittedRecords;result["omittedDiagnostics"]=omittedDiagnostics;
  return result;
}

} // namespace synthcad
