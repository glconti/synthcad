#pragma once
#include "appearance.h"
#include <nlohmann/json.hpp>

namespace dingcad {
struct DesignGraphResult {
  bool specified = false;
  Appearance appearance;
  std::shared_ptr<manifold::Manifold> scene;
  std::string diagnostic;
  nlohmann::json metadata;
};
DesignGraphResult ReadDesignGraph(JSContext* ctx, JSValueConst moduleNamespace,
                                 const std::string& requestedView = "");
}
