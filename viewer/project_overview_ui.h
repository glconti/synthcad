#pragma once

#include "parts_panel.h"

#include <nlohmann/json.hpp>

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace dingcad {

struct ProjectOverviewActions {
  bool close = false;
  std::string view;
};

using ProjectOverviewTextMeasure = std::function<float(const std::string &)>;

struct ProjectOverviewRow {
  std::string text;
  std::string viewId;
  Rectangle bounds{};
  std::vector<std::string> lines;
  bool section = false;
  bool viewButton = false;
  bool selected = false;
};

struct ProjectOverviewLayout {
  Rectangle card{}, header{}, close{}, body{};
  std::vector<ProjectOverviewRow> rows;
  std::vector<std::pair<std::string, Rectangle>> viewButtons;
  // Full unwrapped source rows are exposed for headless layout/content checks.
  std::vector<std::string> document;
  float contentHeight = 0;
  float maxScroll = 0;
};

struct ProjectOverviewUi {
  bool open = false, gesture = false;
  float scroll = 0;
  Vector2 lastMouse{};
  bool hasLastMouse = false;

  ProjectOverviewLayout Layout(
      const nlohmann::json &overview, int width, int height, Font font,
      const ProjectOverviewTextMeasure &measure = {}) const;
  Rectangle Bounds(int width, int height) const;
  bool CapturesMouse(const PanelInput &input, int width, int height) const;
  ProjectOverviewActions Update(const nlohmann::json &overview,
                                const PanelInput &input, Font font,
                                int width, int height);
  // The metric overload keeps headless tests independent of initialized fonts.
  ProjectOverviewActions Update(const nlohmann::json &overview,
                                const PanelInput &input, Font font,
                                int width, int height,
                                const ProjectOverviewTextMeasure &measure);
  void Draw(const nlohmann::json &overview, Font font, int width, int height,
            float uiScale) const;

 private:
  // Text wrapping is the expensive part for projects with many records. Keep
  // the measured rows until either source data, viewport, or font changes.
  mutable bool cacheValid = false;
  mutable nlohmann::json cachedOverview;
  mutable int cachedWidth = -1, cachedHeight = -1;
  mutable int cachedFontSize = -1;
  mutable unsigned int cachedFontTexture = 0;
  mutable std::vector<ProjectOverviewRow> cachedRows;
  mutable std::vector<std::string> cachedDocument;
  mutable float cachedContentHeight = 0;
};

}  // namespace dingcad
