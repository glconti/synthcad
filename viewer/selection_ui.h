#pragma once

#include "parts_panel.h"

#include <array>
#include <optional>
#include <string>

namespace dingcad {

enum class SelectionMode { Part, Surface, Edge, Vertex };

struct SelectionActions {
  bool copy = false;
  bool clear = false;
};

struct SelectionLayout {
  Rectangle card{};
  std::array<Rectangle, 4> modes{};
  Rectangle copy{};
  Rectangle clear{};
};

// Coordinates use the same logical-pixel space as PartsPanel.
struct SelectionUi {
  SelectionMode mode = SelectionMode::Part;
  bool gesture = false;
  std::string summary, detail;
  bool hasSelection = false;
  bool copyAvailable = true;
  Vector2 lastMouse{};
  bool hasLastMouse = false;

  SelectionLayout Layout(int width, int height) const;
  Rectangle Bounds(int width, int height) const;
  bool CapturesMouse(const PanelInput &input, int width, int height) const;
  SelectionActions Update(const PanelInput &input, int width, int height);
  void Draw(Font font, int width, int height) const;
};

// A viewport press becomes a click only when it starts outside UI, stays
// unblocked, and moves at most four logical pixels before release.
class PickGesture {
 public:
  std::optional<Vector2> Update(Vector2 mouse, bool pressed, bool down,
                                bool released, bool blocked);
  bool Dragging() const { return active_ && dragging_ && !canceled_; }

 private:
  bool active_ = false;
  bool dragging_ = false;
  bool canceled_ = false;
  Vector2 origin_{};
};

}  // namespace dingcad
