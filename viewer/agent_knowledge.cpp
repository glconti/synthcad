#include "agent_knowledge.h"
#include "agent_guides.generated.h"
#include "project_contract.h"

#include <map>

namespace synthcad {
namespace {
using json = nlohmann::json;
json Manifest() {
  std::map<std::string, std::string> hashes;
  for (const auto& asset : bundled::kAssets)
    hashes.emplace(asset.topic, Sha256(asset.content));
  std::string identity = "synthcad-guides-v1:";
  for (const auto& file : hashes)
    identity += std::to_string(file.first.size()) + ":" + file.first + file.second;
  const auto hash = Sha256(identity);
  return {{"bundleVersion", "1-" + hash}, {"bundleHash", hash}};
}
}  // namespace

nlohmann::json ListDocs() {
  auto result = Manifest();
  result["topics"] = json::array();
  for (const auto& asset : bundled::kAssets)
    result["topics"].push_back({{"topic", asset.topic}, {"title", asset.title},
        {"source", asset.source}, {"hash", Sha256(asset.content)}});
  return result;
}
nlohmann::json ReadDoc(const std::string& topic) {
  if (topic.empty()) throw KnowledgeError("invalid_argument", "A documentation topic is required");
  for (const auto& asset : bundled::kAssets) if (topic == asset.topic) {
    auto result = Manifest();
    result.update({{"topic", topic}, {"title", asset.title}, {"source", asset.source},
        {"content", asset.content}, {"hash", Sha256(asset.content)}});
    return result;
  }
  throw KnowledgeError("not_found", "Unknown documentation topic: " + topic + ". Run synthcad docs to list areas.");
}
}  // namespace synthcad
