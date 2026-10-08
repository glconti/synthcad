#include "project_contract.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;
using namespace synthcad;
using namespace std::string_literals;
namespace {
void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void Write(const fs::path& path, const std::string& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << bytes;
    if (!output) throw std::runtime_error("fixture write failed");
}
template<class F> void Reject(F operation, const char* message) {
    bool rejected = false;
    try { operation(); } catch (const std::runtime_error&) { rejected = true; }
    Check(rejected, message);
}
struct TempDirectory {
    fs::path path = fs::temp_directory_path() / ("synthcad-contract-" +
        std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
    TempDirectory() { fs::create_directories(path); }
    ~TempDirectory() { std::error_code ec; fs::remove_all(path, ec); }
};
}
int main() {
    try {
        Check(Sha256("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "empty SHA-256 vector");
        Check(Sha256("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "abc SHA-256 vector");
        Check(Sha256("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
            "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1", "multiblock SHA-256 vector");
        Check(Sha256(std::string(1000000, 'a')) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0", "million-a SHA-256 vector");

        TempDirectory temp;
        const auto root = temp.path / fs::u8path(u8"modelli città 日本");
        fs::create_directory(root);
        const auto entry = root / fs::u8path(u8"scena è.js");
        const auto imported = root / "part.js";
        const auto missing = root / "later.js";
        Write(entry, "import './part.js';\r\n");
        Write(imported, "one\0two"s);
        auto standalone = LoadProject(entry);
        const auto casedEntry=root/"AuthoredName.js";
        Write(casedEntry,"source");
        Check(LoadProject(casedEntry).path.filename()=="AuthoredName.js", "authored filename case preserved for display");
        Check(standalone.standalone && ResolveView(standalone) == standalone.path, "standalone compatibility");
        Check(LoadProject(root / "." / entry.filename()).identity == standalone.identity, "canonical standalone identity");
        Check(CanonicalPath(entry).find(u8"日本") != std::string::npos, "UTF-8 canonical path");
#ifdef _WIN32
        auto upper = CanonicalPath(entry);
        for (auto& c : upper) if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
        Check(LoadProject(fs::u8path(upper)).identity == standalone.identity, "Windows case-insensitive identity");
#endif
        auto graph = CaptureFiles({entry, imported});
        Check(MatchesDisk(graph), "fresh graph matches");
        Check(Revision(CaptureFiles({imported, entry, entry})) == Revision(graph), "sorted duplicate-free graph");
        Check(Revision(CaptureFiles({entry})) != Revision(graph), "dependency paths affect revision");
        auto originalTime = fs::last_write_time(imported);
        Write(imported, "two\0one"s);
        fs::last_write_time(imported, originalTime);
        Check(!MatchesDisk(graph), "same-timestamp byte edit detected");
        Check(Revision(CaptureFiles({entry, imported})) != Revision(graph), "import edit changes revision");

        FileSnapshot attempt;
        auto consumed = ReadTrackedFile(imported, attempt);
        Check(consumed && *consumed == "two\0one"s, "exact binary bytes consumed");
        Write(imported, "new bytes");
        Check(!MatchesDisk(attempt), "edit during evaluation invalidates snapshot");
        ReadTrackedFile(imported, attempt);
        Check(attempt.at(CanonicalPath(imported)) == "changed-during-read" && !MatchesDisk(attempt), "mixed reads never acknowledge");
        Check(!ReadTrackedFile(missing, attempt), "missing import read fails");
        Check(attempt.at(CanonicalPath(missing)) == "missing", "missing import is tracked");
        FileSnapshot notFile;
        Check(!ReadTrackedFile(root, notFile) && notFile.at(CanonicalPath(root)) == "unreadable", "non-file distinguished from missing");
        auto recovered = RecoverDependencies(graph, attempt);
        Check(recovered.size() == 3, "failed imports retain prior and attempted dependency keys");
        Write(missing, "recovered import");
        Check(!MatchesDisk(recovered), "missing-file creation triggers recovery");
        fs::remove(imported);
        Check(!MatchesDisk(graph), "dependency deletion detected");

        const auto manifest = root / "synthcad.json";
        const std::string valid = u8R"({"schemaVersion":1,"name":"Città 日本","defaultView":"assembly","views":{"assembly":"scena è.js","plate":"plate.js"},"profiles":{},"evidence":[],"exports":[]})";
        Write(manifest, valid);
        auto project = LoadProject(root);
        Check(!project.standalone && project.name == u8"Città 日本", "project authored Unicode preserved");
        Check(project.identity == LoadProject(manifest).identity, "directory and manifest same identity");
        Check(project.identity != standalone.identity, "project distinct from scene identity");
        Check(project.views.size() == 2 && ResolveView(project) == standalone.path, "named default entry resolution");
        Check(project.files.at(CanonicalPath(manifest)) == Sha256(valid), "manifest actual bytes tracked");
        Check(ResolveView(project,"plate").filename() == "plate.js", "view resolves before entry exists");
        Reject([&] { ResolveView(project,"unknown"); }, "unknown view rejected");
        for (const auto& invalid : {
            "{", "[]", "{}",
            R"({"schemaVersion":2,"defaultView":"a","views":{"a":"a.js"}})",
            R"({"schemaVersion":1.0,"defaultView":"a","views":{"a":"a.js"}})",
            R"({"schemaVersion":true,"defaultView":"a","views":{"a":"a.js"}})",
            R"({"schemaVersion":1,"name":4,"defaultView":"a","views":{"a":"a.js"}})",
            R"({"schemaVersion":1,"defaultView":"a","views":{}})",
            R"({"schemaVersion":1,"defaultView":"b","views":{"a":"a.js"}})",
            R"({"schemaVersion":1,"defaultView":"a","views":{"a":2}})",
            R"({"schemaVersion":1,"defaultView":"a","views":{"a":"/absolute.js"}})",
            R"({"schemaVersion":1,"defaultView":"a","views":{"a":"wrong.txt"}})"}) {
            Write(manifest, invalid);
            Reject([&] { LoadProject(manifest); }, "invalid project rejected");
            FileSnapshot unchanged;
            Check(ReadTrackedFile(manifest, unchanged) == std::optional<std::string>(invalid), "invalid project never rewritten");
        }
        std::cout << "Project contract tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Project contract test failed: " << e.what() << '\n';
        return 1;
    }
}
