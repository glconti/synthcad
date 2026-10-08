#pragma once

#include "parts_panel.h"

#include <functional>
#include <string>
#include <vector>

namespace dingcad {

struct GuidedPickActions {
  bool confirm = false;
  bool cancel = false;
};

struct GuidedPickLayout {
  Rectangle card{};
  Rectangle heading{};
  Rectangle questionArea{};
  Rectangle confirm{};
  Rectangle cancel{};
};

// The callback receives complete UTF-8 prefixes and returns their measured
// width. Hard newlines remain separate lines; soft wrapping retains all text,
// including spaces and unbroken long words.
using GuidedPickTextMeasure = std::function<float(const std::string &)>;
std::vector<std::string> WrapGuidedPickQuestion(
    const std::string &question, const GuidedPickTextMeasure &measure,
    float maxWidth);

struct GuidedPickUi {
  std::string question, kind;
  bool active = false, canConfirm = false, gesture = false;
  float scroll = 0;
  Vector2 lastMouse{};
  bool hasLastMouse = false;

  GuidedPickLayout Layout(int width, int height) const;
  Rectangle Bounds(int width, int height) const;
  bool CapturesMouse(const PanelInput &input, int width, int height) const;
  // Pass the same font metric used by Draw when the viewer uses a custom font.
  // An empty callback uses raylib's default font metrics.
  GuidedPickActions Update(const PanelInput &input, int width, int height,
                           const GuidedPickTextMeasure &measure = {});
  // logicalScale should match the scale applied to the surrounding UI matrix.
  // Zero infers it from the screen and logical width for existing callers.
  void Draw(Font font, int width, int height, float logicalScale = 0) const;
};

}  // namespace dingcad
