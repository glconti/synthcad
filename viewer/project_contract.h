#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace synthcad {

// Canonical UTF-8 absolute paths map to SHA-256 hex digests, "missing",
// "unreadable", or "changed-during-read" for inconsistent repeated reads.
// Store the digest of bytes actually consumed by evaluation.
using FileSnapshot = std::map<std::string, std::string>;

std::string CanonicalPath(const std::filesystem::path& path);
std::string Sha256(const std::string& bytes);
std::optional<std::string> ReadTrackedFile(const std::filesystem::path& path,
                                         FileSnapshot& snapshot);
FileSnapshot CaptureFiles(const std::vector<std::filesystem::path>& paths);
std::string Revision(const FileSnapshot& snapshot);
// Artifact basis: consumed model files plus view/layout identity. Pass files
// before adding a manifest used only for routing/review metadata.
std::string ModelRevision(const FileSnapshot& modelFiles, const std::string& view,
                          const std::string& designIdentity);
bool MatchesDisk(const FileSnapshot& snapshot);
// Recapture the union after a failed attempt so prior imports remain watched.
FileSnapshot RecoverDependencies(const FileSnapshot& previous,
                                 const FileSnapshot& attempted);

struct Project {
    std::filesystem::path path;
    std::filesystem::path root;
    std::string identity;
    std::string name;
    std::string defaultView;
    bool standalone = false;
    std::map<std::string, std::filesystem::path> views;
    FileSnapshot files;
    // Optional review/manufacturing metadata is validated independently of
    // the required manifest routing contract, so bad notes do not hide geometry.
    nlohmann::json metadata = nlohmann::json::object();
};

// Accepts synthcad.json, its containing directory, or a standalone .js file.
// Invalid metadata throws std::runtime_error without modifying any file.
Project LoadProject(const std::filesystem::path& path);
std::filesystem::path ResolveView(const Project& project,
                                  const std::string& view = "");

} // namespace synthcad
