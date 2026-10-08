#pragma once

#include <nlohmann/json.hpp>

namespace synthcad {

// Pure authored-metadata projection, including explicit sample observations and
// compatibility/reprint claims. Does not inspect files or verify geometry.
// Target basis determines freshness; previousBasis remains authored history.
// Unresolved evidence references remain visible with evidenceStatus=unknown.
nlohmann::json ProjectOverview(const nlohmann::json& metadata,
                              const nlohmann::json& profile,
                              const nlohmann::json& views);

} // namespace synthcad
