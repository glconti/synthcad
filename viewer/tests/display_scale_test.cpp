#include "display_scale.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void RequireNear(float actual, float expected, const char *message) {
  if (!std::isfinite(actual) || std::abs(actual - expected) > 0.0001f) {
    throw std::runtime_error(message);
  }
}
}

int main() {
  try {
    RequireNear(dingcad::UiScaleFromDpi(96), 1.0f, "96 DPI is 1x");
    RequireNear(dingcad::UiScaleFromDpi(120), 1.25f, "120 DPI is 1.25x");
    RequireNear(dingcad::UiScaleFromDpi(144), 1.5f, "144 DPI is 1.5x");
    RequireNear(dingcad::UiScaleFromDpi(192), 2.0f, "192 DPI is 2x");
    RequireNear(dingcad::UiScaleFromDpi(48), 1.0f, "Scale is clamped to 1x");
    RequireNear(dingcad::UiScaleFromDpi(384), 4.0f, "Scale is clamped to 4x");

    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double infinity = std::numeric_limits<double>::infinity();
    RequireNear(dingcad::UiScaleFromDpi(-1), 1.0f, "Negative DPI falls back to 1x");
    RequireNear(dingcad::UiScaleFromDpi(0), 1.0f, "Zero DPI falls back to 1x");
    RequireNear(dingcad::UiScaleFromDpi(nan), 1.0f, "NaN DPI falls back to 1x");
    RequireNear(dingcad::UiScaleFromDpi(infinity), 1.0f, "Infinite DPI falls back to 1x");
    RequireNear(dingcad::UiScaleFromDpi(-infinity), 1.0f,
                "Negative infinite DPI falls back to 1x");

    std::cout << "PASS native UI scale conversion bounds and invalid values\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
