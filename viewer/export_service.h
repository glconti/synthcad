#pragma once
#include "part_tree.h"
#include <functional>
#include <nlohmann/json.hpp>

namespace dingcad {
nlohmann::json ReviewExport(const PartTree &tree, const nlohmann::json &context,
                            const nlohmann::json &options);
nlohmann::json ExecuteExport(const PartTree &tree, const nlohmann::json &context,
                             const nlohmann::json &options,
                             const std::function<std::string()> &guard = {});
}
