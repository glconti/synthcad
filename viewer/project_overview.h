#pragma once

#include <nlohmann/json.hpp>

namespace synthcad {

// Pure authored-metadata projection. Does not inspect files or verify geometry.
nlohmann::json ProjectOverview(const nlohmann::json& metadata,
                              const nlohmann::json& profile,
                              const nlohmann::json& views);

} // namespace synthcad
