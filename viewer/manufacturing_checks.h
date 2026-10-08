#pragma once

#include "appearance.h"
#include <nlohmann/json.hpp>

namespace dingcad {
// Pure checks over already placed active solids and normalized design metadata.
// Does not evaluate another view, mutate shared source solids, or inspect slicers.
nlohmann::json ManufacturingChecks(
    const std::vector<DisplayPart>& parts, const nlohmann::json& design,
    const nlohmann::json& profile, const nlohmann::json& projectMetadata,
    const nlohmann::json& basis);
} // namespace dingcad
