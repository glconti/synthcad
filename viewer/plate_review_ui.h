#pragma once

#include "parts_panel.h"

#include <nlohmann/json.hpp>

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace dingcad {

struct PlateReviewActions {
  bool close = false;
  std::vector<std::string> highlight;
  bool frame = false;
};

using PlateReviewTextMeasure = std::function<float(const std::string &)>;

struct PlateReviewRow {
  std::string text;
  Rectangle bounds{};
  std::vector<std::string> lines;
  std::vector<std::string> highlightIds;
  bool section = false;
  bool highlightButton = false;
  bool enabled = true;
  std::string result;
};

struct PlateReviewLayout {
  Rectangle card{}, header{}, close{}, body{};
  std::vector<PlateReviewRow> rows;
  std::vector<std::pair<std::vector<std::string>, Rectangle>> highlightButtons;
  // Full source strings remain available to headless layout/content checks.
  std::vector<std::string> document;
  float contentHeight = 0;
  float maxScroll = 0;
};

struct PlateReviewUi {
  bool open = false;
  bool gesture = false;
  float scroll = 0;
  Vector2 lastMouse{};
  bool hasLastMouse = false;

  PlateReviewLayout Layout(
      const nlohmann::json &report, int width, int height, Font font,
      const PlateReviewTextMeasure &measure = {}) const;
  Rectangle Bounds(int width, int height) const;
  bool CapturesMouse(const PanelInput &input, int width, int height) const;
  PlateReviewActions Update(const nlohmann::json &report,
                            const PanelInput &input, Font font,
                            int width, int height);
  // The metric overload keeps headless tests independent of initialized fonts.
  PlateReviewActions Update(const nlohmann::json &report,
                            const PanelInput &input, Font font,
                            int width, int height,
                            const PlateReviewTextMeasure &measure);
  void Draw(const nlohmann::json &report, Font font, int width, int height,
            float uiScale) const;

 private:
  mutable bool cacheValid = false;
  mutable nlohmann::json cachedReport;
  mutable int cachedWidth = -1, cachedHeight = -1;
  mutable int cachedFontSize = -1;
  mutable unsigned int cachedFontTexture = 0;
  mutable std::vector<PlateReviewRow> cachedRows;
  mutable std::vector<std::string> cachedDocument;
  mutable float cachedContentHeight = 0;
};

// Call while the 3D camera is active. Coordinates are converted from the
// report's Z-up CAD millimeters into the viewer's Y-up world space.
bool CanDrawPlateBed(const nlohmann::json &report);
void DrawPlateBed(const nlohmann::json &report);

}  // namespace dingcad
