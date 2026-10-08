#include "selection_ui.h"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

using namespace dingcad;

namespace {
void Require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}

PanelInput Click(Rectangle rect) {
  PanelInput input;
  input.mouse = {rect.x + rect.width / 2, rect.y + rect.height / 2};
  input.pressed = true;
  input.leftDown = true;
  return input;
}

bool Inside(Rectangle inner, Rectangle outer) {
  constexpr float epsilon = 0.01f;
  return inner.x + epsilon >= outer.x && inner.y + epsilon >= outer.y &&
         inner.x + inner.width <= outer.x + outer.width + epsilon &&
         inner.y + inner.height <= outer.y + outer.height + epsilon;
}

void CheckPanel() {
  SelectionUi ui;
  auto layout = ui.Layout(1280, 720);
  Require(layout.card.width == 360 && layout.card.height == 116,
          "Selection card uses compact dimensions");
  Require(layout.card.x + layout.card.width == 1268 &&
              layout.card.y + layout.card.height == 708,
          "Selection card sits 12 logical pixels from the bottom-right edge");
  Require(!CheckCollisionRecs(layout.card, WorkspaceUi{}.Toolbar(1280)),
          "Selection card stays below the top toolbar");

  for (const auto [width, height] : {std::pair{640, 400}, std::pair{320, 180},
                                     std::pair{160, 180}, std::pair{40, 40}}) {
    const auto compact = ui.Layout(width, height);
    const Rectangle viewport{0, 0, static_cast<float>(width),
                             static_cast<float>(height)};
    Require(Inside(compact.card, viewport),
            "Selection card remains inside a small viewport");
    for (const auto button : compact.modes) {
      Require(Inside(button, compact.card),
              "Mode hit targets remain inside the compact card");
    }
    Require(Inside(compact.copy, compact.card) &&
                Inside(compact.clear, compact.card),
            "Copy and clear hit targets remain inside the compact card");
  }

  PartTree longTree;
  std::vector<DisplayPart> manyParts;
  for (int index = 0; index < 100; ++index) {
    DisplayPart part;
    part.color = WHITE;
    part.id = "part-" + std::to_string(index);
    part.name = part.id;
    manyParts.push_back(std::move(part));
  }
  longTree.Reload(std::move(manyParts));
  const auto panelBounds = PartsPanel{}.Bounds(longTree, 640, 400);
  const auto selectionBounds = ui.Bounds(640, 400);
  Require(!CheckCollisionRecs(panelBounds, selectionBounds),
          "At 640x400 the selection card clears the full-height parts panel");
  const auto mediumLayout = ui.Layout(640, 400);
  Require(std::abs(mediumLayout.modes[1].width - 65.25f) < 0.01f,
          "The Surface control retains enough width at 640 pixels");
  ui.Update(Click(mediumLayout.modes[1]), 640, 400);
  Require(ui.mode == SelectionMode::Surface,
          "The compact viewport still accepts mode clicks in logical coordinates");

  auto modeClick = Click(layout.modes[3]);
  ui.Update(modeClick, 1280, 720);
  Require(ui.mode == SelectionMode::Vertex,
          "Mode buttons use logical panel input coordinates");
  Require(ui.hasLastMouse && ui.lastMouse.x == modeClick.mouse.x &&
              ui.lastMouse.y == modeClick.mouse.y,
          "The UI remembers the latest logical mouse position for hover rendering");

  auto copyClick = Click(layout.copy);
  auto clearClick = Click(layout.clear);
  Require(!ui.Update(copyClick, 1280, 720).copy &&
              !ui.Update(clearClick, 1280, 720).clear,
          "Copy and clear are disabled without a selection");

  ui.hasSelection = true;
  ui.copyAvailable = false;
  Require(!ui.Update(copyClick,1280,720).copy && ui.Update(clearClick,1280,720).clear,
          "Oversized references disable Copy without trapping the selection");
  ui.copyAvailable = true;
  Require(ui.Update(copyClick, 1280, 720).copy,
          "Copy action is enabled with a selection");
  Require(ui.Update(clearClick, 1280, 720).clear,
          "Clear action is enabled with a selection");
  Require(!CheckCollisionRecs(ui.Bounds(640,400),WorkspaceUi{}.ToastCard(640,400)),
          "Notifications leave the selection controls accessible");

  ui.gesture = false;
  auto cardClick = Click(layout.card);
  ui.Update(cardClick, 1280, 720);
  Require(ui.CapturesMouse(cardClick, 1280, 720),
          "A press on the selection card is captured");
  PanelInput dragOut;
  dragOut.mouse = {500, 400};
  dragOut.leftDown = true;
  ui.Update(dragOut, 1280, 720);
  Require(ui.CapturesMouse(dragOut, 1280, 720),
          "A card gesture remains captured after leaving the card");
  PanelInput release;
  release.mouse = dragOut.mouse;
  ui.Update(release, 1280, 720);
  Require(!ui.CapturesMouse(release, 1280, 720),
          "Card capture ends on release");
}

void CheckClickAndDrag() {
  PickGesture gesture;
  Require(!gesture.Update({10, 20}, true, true, false, false),
          "A press does not pick before release");
  Require(!gesture.Update({14, 20}, false, true, false, false) &&
              !gesture.Dragging(),
          "Four logical pixels remain within the click threshold");
  auto click = gesture.Update({13, 20}, false, false, true, false);
  Require(click && click->x == 13 && click->y == 20,
          "An unblocked click returns its release position");
  Require(!gesture.Dragging(), "Click release resets drag state");

  gesture.Update({10, 20}, true, true, false, false);
  gesture.Update({14.01f, 20}, false, true, false, false);
  Require(gesture.Dragging(), "Movement beyond four pixels begins a drag");
  gesture.Update({10, 20}, false, true, false, false);
  Require(gesture.Dragging(), "Returning to the origin does not turn a drag into a click");
  Require(!gesture.Update({10, 20}, false, false, true, false),
          "A drag never produces a click on release");
  Require(!gesture.Dragging(), "Drag release resets gesture state");
}

void CheckBlockedGestures() {
  PickGesture gesture;
  gesture.Update({10, 20}, true, true, false, true);
  Require(!gesture.Dragging(), "A blocked press cannot start a camera drag");
  Require(!gesture.Update({10, 20}, false, false, true, false),
          "A blocked press cannot become a click after release");
  Require(!gesture.Dragging(), "Blocked-start release resets gesture state");

  gesture.Update({10, 20}, true, true, false, false);
  gesture.Update({11, 20}, false, true, false, true);
  gesture.Update({18, 20}, false, true, false, false);
  Require(!gesture.Dragging(), "A blocked transit cancels the gesture through release");
  Require(!gesture.Update({18, 20}, false, false, true, false),
          "A blocked transit cannot produce a click");
  Require(!gesture.Dragging(), "Blocked-transit release resets gesture state");

  gesture.Update({10, 20}, true, true, false, false);
  Require(!gesture.Update({10, 20}, false, false, true, true),
          "A blocked release cannot produce a click");
  Require(!gesture.Dragging(), "Blocked-release resets gesture state");

  gesture.Update({10, 20}, true, true, false, false);
  Require(gesture.Update({10, 20}, false, false, true, false).has_value(),
          "A fresh unblocked press works after cancellation");
}
}  // namespace

int main() {
  try {
    CheckPanel();
    CheckClickAndDrag();
    CheckBlockedGestures();
    std::cout << "PASS selection card input and pick gesture classification\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
