#pragma once

namespace dingcad {

// Convert a physical display DPI value to the app's UI scale. Invalid values
// fall back to 1, and supported scales are limited to 1x through 4x.
float UiScaleFromDpi(double dpi);

// Call after InitWindow. The app keeps its render targets and input in physical
// pixels, so callers scale UI drawing by this ratio and divide raw mouse/window
// coordinates by it exactly once. Do not enable FLAG_WINDOW_HIGHDPI: raylib
// would also rescale the framebuffer and mouse coordinates.
float NativeUiScale();

}  // namespace dingcad
