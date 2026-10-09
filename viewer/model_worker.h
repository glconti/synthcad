#pragma once
#include "appearance.h"
#include "dimensions.h"
#include "project_contract.h"
#include <memory>
#include <optional>

namespace synthcad {
struct ModelResult {
  bool success=false;
  double loadMilliseconds=0, evaluationMilliseconds=0;
  std::shared_ptr<manifold::Manifold> manifold;
  std::string message;
  std::vector<std::filesystem::path> dependencies;
  std::vector<dingcad::Dimension> dimensions;
  dingcad::Appearance appearance;
  nlohmann::json design, checks, failure;
  FileSnapshot files;
  std::vector<manifold::MeshGL> displayMeshes;
};
std::vector<dingcad::DisplayPart> ModelParts(const ModelResult& result);
// Internal, versioned lossless geometry transfer. Never an STL round trip.
nlohmann::json EncodeModel(const ModelResult& result);
ModelResult DecodeModel(const nlohmann::json& packet);
int RunModelWorker(const std::filesystem::path& directory);
class ModelWorker {
 public:
  explicit ModelWorker(std::string executable);
  ~ModelWorker();
  void Start(const std::filesystem::path& scene,const std::string& view,
             const std::optional<Project>& project,int timeoutMs=120000);
  bool Running() const;
  nlohmann::json Progress();
  std::optional<ModelResult> Poll();
  ModelResult Cancel(const std::string& category="cancelled");
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
ModelResult CheckModel(const std::string& executable,const std::filesystem::path& scene);
}
