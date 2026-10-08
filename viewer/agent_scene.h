#pragma once

#include "part_tree.h"
#include "dimensions.h"
#include <nlohmann/json.hpp>

namespace synthcad {
nlohmann::json ReviewSnapshot(const dingcad::PartTree& tree,
                              const std::vector<dingcad::Dimension>& dimensions,
                              const Camera3D& camera,
                              const std::vector<std::string>& highlights);
// Authored part IDs and group node keys are accepted. Unknown references throw.
// Empty IDs resolve current selection if selection=true, otherwise visible parts.
std::vector<size_t> ResolveReviewParts(const dingcad::PartTree& tree,
                                      const std::vector<std::string>& ids,
                                      bool selection = false);
}
