#pragma once

#include <nlohmann/json.hpp>

namespace synthcad {
// Pure project-local metadata validation. Never loads a global profile, mutates
// the project, runs geometry checks, or verifies a slicer preset.
nlohmann::json PrinterProfileContext(const nlohmann::json& project);
// A deliberately incomplete setup fragment, suitable for authoring/review.
nlohmann::json PrinterProfileTemplate();
} // namespace synthcad
