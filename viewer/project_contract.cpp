#include "project_contract.h"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace synthcad {
namespace {
uint32_t Rotate(uint32_t x, unsigned n) { return (x >> n) | (x << (32 - n)); }
constexpr uint32_t kRound[] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};
void AppendField(std::string& target, const std::string& field) {
    target += std::to_string(field.size());
    target += ':';
    target += field;
}
[[noreturn]] void Invalid(const std::filesystem::path& path, const std::string& reason) {
    throw std::runtime_error("Invalid project " + path.u8string() + ": " + reason);
}
}

std::string Sha256(const std::string& bytes) {
    std::vector<uint8_t> data(bytes.begin(), bytes.end());
    const uint64_t bits = static_cast<uint64_t>(data.size()) * 8;
    data.push_back(0x80);
    while (data.size() % 64 != 56) data.push_back(0);
    for (int i = 7; i >= 0; --i) data.push_back(static_cast<uint8_t>(bits >> (i * 8)));
    std::array<uint32_t, 8> hash = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
                                   0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    for (size_t offset = 0; offset < data.size(); offset += 64) {
        uint32_t words[64];
        for (size_t i = 0; i < 16; ++i) {
            const auto p = offset + i * 4;
            words[i] = (uint32_t(data[p]) << 24) | (uint32_t(data[p+1]) << 16)
                     | (uint32_t(data[p+2]) << 8) | uint32_t(data[p+3]);
        }
        for (size_t i = 16; i < 64; ++i) {
            const auto a = words[i-15], b = words[i-2];
            words[i] = words[i-16] + (Rotate(a,7)^Rotate(a,18)^(a>>3)) + words[i-7]
                     + (Rotate(b,17)^Rotate(b,19)^(b>>10));
        }
        auto a=hash[0],b=hash[1],c=hash[2],d=hash[3],e=hash[4],f=hash[5],g=hash[6],h=hash[7];
        for (size_t i = 0; i < 64; ++i) {
            auto t1 = h + (Rotate(e,6)^Rotate(e,11)^Rotate(e,25)) + ((e&f)^(~e&g)) + kRound[i] + words[i];
            auto t2 = (Rotate(a,2)^Rotate(a,13)^Rotate(a,22)) + ((a&b)^(a&c)^(b&c));
            h=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
        }
        hash[0]+=a; hash[1]+=b; hash[2]+=c; hash[3]+=d;
        hash[4]+=e; hash[5]+=f; hash[6]+=g; hash[7]+=h;
    }
    std::string result;
    constexpr char hex[] = "0123456789abcdef";
    for (auto word : hash) for (int i=7; i>=0; --i) result += hex[(word >> (i*4)) & 15];
    return result;
}

std::string CanonicalPath(const std::filesystem::path& path) {
    std::error_code ec;
    auto result = std::filesystem::weakly_canonical(std::filesystem::absolute(path), ec);
    if (ec) result = std::filesystem::absolute(path).lexically_normal();
#ifdef _WIN32
    // Windows project sessions use case-insensitive filesystem identity.
    const auto native = result.native();
    std::wstring folded(native.size(), L'\0');
    if (!native.empty() && LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE,
            native.data(), static_cast<int>(native.size()), folded.data(),
            static_cast<int>(folded.size()), nullptr, nullptr, 0)) result = folded;
#endif
    return result.generic_u8string();
}

std::optional<std::string> ReadTrackedFile(const std::filesystem::path& path,
                                          FileSnapshot& snapshot) {
    const auto key = CanonicalPath(path);
    std::error_code ec;
    const bool exists = std::filesystem::exists(path, ec);
    std::error_code typeError;
    const bool regular = std::filesystem::is_regular_file(path, typeError);
    std::ifstream input;
    if (regular) input.open(path, std::ios::binary);
    if (!input.is_open() || !input) {
        snapshot[key] = !exists && !ec ? "missing" : "unreadable";
        return std::nullopt;
    }
    std::string bytes;
    std::array<char, 8192> buffer;
    while (input.read(buffer.data(), static_cast<std::streamsize>(buffer.size())) || input.gcount()) {
        bytes.append(buffer.data(), static_cast<size_t>(input.gcount()));
    }
    if (input.bad()) {
        snapshot[key] = "unreadable";
        return std::nullopt;
    }
    const auto digest = Sha256(bytes);
    const auto prior = snapshot.find(key);
    if (prior != snapshot.end() && prior->second != digest) {
        // A file was read twice with different bytes in one evaluation. It can
        // never match disk, even if the second version happens to remain there.
        snapshot[key] = "changed-during-read";
    } else snapshot[key] = digest;
    return bytes;
}

FileSnapshot CaptureFiles(const std::vector<std::filesystem::path>& paths) {
    FileSnapshot snapshot;
    for (const auto& path : paths) {
        if (!snapshot.count(CanonicalPath(path))) ReadTrackedFile(path, snapshot);
    }
    return snapshot;
}

std::string Revision(const FileSnapshot& snapshot) {
    std::string graph = "synthcad-revision-v1:";
    for (const auto& file : snapshot) {
        AppendField(graph, file.first);
        AppendField(graph, file.second);
    }
    return Sha256(graph);
}

std::string ModelRevision(const FileSnapshot& files, const std::string& view,
                          const std::string& designIdentity) {
    std::string value = "synthcad-model-v1:";
    AppendField(value, view);
    AppendField(value, Revision(files));
    AppendField(value, designIdentity);
    return Sha256(value);
}

bool MatchesDisk(const FileSnapshot& snapshot) {
    for (const auto& file : snapshot) {
        FileSnapshot current;
        ReadTrackedFile(std::filesystem::u8path(file.first), current);
        if (current.begin()->second != file.second) return false;
    }
    return true;
}

FileSnapshot RecoverDependencies(const FileSnapshot& previous, const FileSnapshot& attempted) {
    std::vector<std::filesystem::path> paths;
    for (const auto& file : previous) paths.push_back(std::filesystem::u8path(file.first));
    for (const auto& file : attempted) paths.push_back(std::filesystem::u8path(file.first));
    return CaptureFiles(paths);
}

Project LoadProject(const std::filesystem::path& input) {
    Project project;
    std::error_code ec;
    auto path = std::filesystem::is_directory(input, ec) ? input / "synthcad.json" : input;
    project.path = std::filesystem::weakly_canonical(std::filesystem::absolute(path));
    project.root = project.path.parent_path();
    project.identity = Sha256("synthcad-project-v1:" + CanonicalPath(path));
    const auto bytes = ReadTrackedFile(project.path, project.files);
    if (!bytes) Invalid(path, "file is missing or unreadable");
    const auto identityPath = std::filesystem::u8path(CanonicalPath(project.path));
    auto extension = identityPath.extension().u8string();
    if (extension == ".js") {
        project.standalone = true;
        project.name = project.path.stem().u8string();
        project.defaultView = "scene";
        project.views.emplace("scene", project.path);
        return project;
    }
    if (identityPath.filename() != "synthcad.json") Invalid(path, "expected synthcad.json or a .js scene");
    nlohmann::json json;
    try { json = nlohmann::json::parse(*bytes); }
    catch (const nlohmann::json::exception& e) { Invalid(path, std::string("JSON parse error: ") + e.what()); }
    if (!json.is_object()) Invalid(path, "root must be an object");
    if (!json.contains("schemaVersion") || !json["schemaVersion"].is_number_integer()
            || json["schemaVersion"] != 1) Invalid(path, "schemaVersion must be integer 1");
    if (json.contains("name")) {
        if (!json["name"].is_string()) Invalid(path, "name must be a string");
        project.name = json["name"].get<std::string>();
    } else project.name = project.root.filename().u8string();
    if (!json.contains("defaultView") || !json["defaultView"].is_string()) Invalid(path, "defaultView must be a string");
    project.defaultView = json["defaultView"].get<std::string>();
    if (!json.contains("views") || !json["views"].is_object() || json["views"].empty()) Invalid(path, "views must be a nonempty object");
    for (auto it = json["views"].begin(); it != json["views"].end(); ++it) {
        if (it.key().empty() || it.key().find('\0') != std::string::npos || !it.value().is_string()) Invalid(path, "each view needs a nonempty name and relative .js entry filename");
        const auto filename = it.value().get<std::string>();
        const auto entry = std::filesystem::u8path(filename);
        if (filename.empty() || filename.find('\0') != std::string::npos || entry.has_root_path()
                || entry.extension() != ".js") Invalid(path, "view '" + it.key() + "' must name a relative .js entry filename");
        project.views.emplace(it.key(), std::filesystem::weakly_canonical(project.root / entry));
    }
    if (!project.views.count(project.defaultView)) Invalid(path, "defaultView must name an entry in views");
    project.metadata = std::move(json);
    return project;
}

std::filesystem::path ResolveView(const Project& project, const std::string& view) {
    const auto& name = view.empty() ? project.defaultView : view;
    auto it = project.views.find(name);
    if (it == project.views.end()) throw std::runtime_error("Unknown project view '" + name + "'");
    return it->second;
}
} // namespace synthcad
