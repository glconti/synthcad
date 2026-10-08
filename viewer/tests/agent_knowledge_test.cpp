#include "agent_knowledge.h"
#include "agent_guides.generated.h"
#include "project_contract.h"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <set>

namespace {
namespace fs = std::filesystem;
void Require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
template<class F> void Fails(F action, const std::string& code) {
  try { action(); } catch (const synthcad::KnowledgeError& error) {
    Require(error.code == code, "wrong error category"); return;
  }
  throw std::runtime_error("expected operation to fail");
}
void Check() {
  const auto docs = synthcad::ListDocs();
  Require(docs.at("topics").size() >= 10, "focused docs missing");
  Require(docs.at("bundleHash").get<std::string>().size() == 64, "bundle hash missing");
  Require(docs.at("bundleVersion") == "1-" + docs.at("bundleHash").get<std::string>(), "version not content-derived");
  std::set<std::string> topics;
  for (const auto& entry : docs.at("topics")) {
    const auto topic = entry.at("topic").get<std::string>();
    Require(topics.insert(topic).second, "duplicate documentation topic");
    const auto doc = synthcad::ReadDoc(topic);
    Require(!doc.at("content").get<std::string>().empty(), "empty document");
    Require(doc.at("hash") == synthcad::Sha256(doc.at("content")), "document hash incorrect");
    Require(doc.at("bundleVersion") == docs.at("bundleVersion"), "inconsistent version");
    Require(doc.at("source") == entry.at("source") && doc.at("title") == entry.at("title"), "list/read metadata diverged");
  }
  for (const auto* topic : {"api", "cli", "projects", "skill", "start", "modeling",
                          "print-design", "fit-and-assembly", "build-plates", "bambu-handoff"})
    Require(topics.count(topic) == 1, "required topic missing");
  // Compare the full bytes, not selected phrases, with the compiled source
  // assets; retrieval must not normalize Unicode, line endings or whitespace.
  for (const auto& asset : synthcad::bundled::kAssets)
    Require(synthcad::ReadDoc(asset.topic).at("content") == asset.content, "compiled source bytes changed");
  Fails([] { synthcad::ReadDoc(""); }, "invalid_argument");
  Fails([] { synthcad::ReadDoc("../API.md"); }, "not_found");
  Fails([] { synthcad::ReadDoc(u8"inesistente – 日本語"); }, "not_found");
  Require(synthcad::ListDocs() == docs, "bundle discovery is not deterministic");
}
}  // namespace
int main() {
  const auto previous = fs::current_path();
  const auto root = fs::temp_directory_path() / fs::u8path(u8"synthcad città 日本語") /
      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
  try {
    fs::create_directories(root);
    fs::current_path(root); // Empty Unicode directory outside the checkout.
    Check();
    fs::current_path(previous);
    fs::remove(root);
    fs::remove(root.parent_path());
    std::cout << "agent knowledge tests passed\n";
    return 0;
  } catch (const std::exception& error) {
    fs::current_path(previous);
    std::cerr << error.what() << "\n";
    return 1;
  }
}
