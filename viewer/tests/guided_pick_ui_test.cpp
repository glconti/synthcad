#include "guided_pick_ui.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

using namespace dingcad;

namespace {
void Require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}

PanelInput Click(Rectangle rect) {
  PanelInput input;
  input.mouse = {rect.x + rect.width / 2.0f, rect.y + rect.height / 2.0f};
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

void CheckLayout() {
  GuidedPickUi ui;
  const auto compact = ui.Layout(640, 400);
  Require(compact.card.width == 296.0f && compact.card.x == 332.0f,
          "The compact guided-pick card clears the left parts panel");
  Require(compact.card.height == 200.0f && compact.card.y == 188.0f,
          "The compact guided-pick card sits at the bottom-right edge");
  const auto large = ui.Layout(1280, 720);
  Require(large.card.width == 420.0f && large.card.height == 220.0f,
          "The large guided-pick card uses its preferred dimensions");
  Require(large.card.x + large.card.width == 1268.0f &&
              large.card.y + large.card.height == 708.0f,
          "The large guided-pick card keeps a 12-pixel edge margin");

  for (const auto [width, height] : {std::pair{640, 400}, std::pair{1280, 720},
                                     std::pair{320, 180}, std::pair{40, 40}}) {
    const auto layout = ui.Layout(width, height);
    const Rectangle viewport{0, 0, static_cast<float>(width),
                             static_cast<float>(height)};
    Require(Inside(layout.card, viewport),
            "The guided-pick card stays inside the viewport");
    Require(Inside(layout.heading, layout.card) &&
                Inside(layout.questionArea, layout.card) &&
                Inside(layout.confirm, layout.card) &&
                Inside(layout.cancel, layout.card),
            "Guided-pick controls remain inside the card");
  }
}

void CheckActionsAndCapture() {
  GuidedPickUi ui;
  ui.active = true;
  ui.kind = "surface";
  ui.question = "Choose the broad top face.";
  auto layout = ui.Layout(640, 400);
  auto confirm = Click(layout.confirm);
  Require(!ui.Update(confirm, 640, 400).confirm,
          "Confirm is disabled until a valid selection exists");

  ui.canConfirm = true;
  Require(ui.Update(confirm, 640, 400).confirm,
          "A valid selection enables the Confirm action");
  Require(ui.CapturesMouse(confirm, 640, 400),
          "A press on the card is captured");
  PanelInput dragOutside;
  dragOutside.mouse = {40, 40};
  dragOutside.leftDown = true;
  ui.Update(dragOutside, 640, 400);
  Require(ui.CapturesMouse(dragOutside, 640, 400),
          "A card gesture remains captured after leaving its bounds");
  PanelInput release;
  release.mouse = dragOutside.mouse;
  ui.Update(release, 640, 400);
  Require(!ui.CapturesMouse(release, 640, 400),
          "Card capture ends when the mouse gesture releases");

  layout = ui.Layout(640, 400);
  auto cancel = Click(layout.cancel);
  Require(ui.Update(cancel, 640, 400).cancel,
          "Cancel stays available without a valid selection");
  PanelInput escape;
  escape.escape = true;
  Require(ui.Update(escape, 640, 400).cancel,
          "Escape cancels an active guided pick");
  ui.active = false;
  Require(!ui.CapturesMouse(cancel, 640, 400),
          "An inactive guided-pick UI does not capture the viewport");
}

void CheckQuestionWrappingAndScroll() {
  const GuidedPickTextMeasure eightPixelsPerCodepoint =
      [](const std::string &text) {
        size_t count = 0;
        for (unsigned char byte : text) {
          if ((byte & 0xc0) != 0x80) ++count;
        }
        return static_cast<float>(count * 8);
      };

  std::string question;
  const std::string fragment = "surface é🙂 edge vertex ";
  while (question.size() + fragment.size() <= 4096) question += fragment;
  question.append(4096 - question.size(), 'x');
  const auto lines = WrapGuidedPickQuestion(question,
                                            eightPixelsPerCodepoint, 80.0f);
  std::string reconstructed;
  for (const auto &line : lines) reconstructed += line;
  Require(reconstructed == question,
          "UTF-8 wrapping retains all 4096 question bytes, including long words");
  Require(lines.size() > 1, "A long question wraps into multiple visible lines");
  const auto hardLines = WrapGuidedPickQuestion(
      "prima riga\nseconda riga", eightPixelsPerCodepoint, 200.0f);
  Require(hardLines.size() == 2 && hardLines[0] == "prima riga" &&
              hardLines[1] == "seconda riga",
          "Explicit newlines stay visible as separate question lines");

  GuidedPickUi ui;
  ui.active = true;
  std::string wideQuestion;
  const std::string wideCodepoint = "\xf0\x9f\x98\x80";
  while (wideQuestion.size() + wideCodepoint.size() <= 4096) {
    wideQuestion += wideCodepoint;
  }
  const GuidedPickTextMeasure wideGlyphs = [](const std::string &text) {
    size_t count = 0;
    for (unsigned char byte : text) {
      if ((byte & 0xc0) != 0x80) ++count;
    }
    return static_cast<float>(count * 36);
  };
  ui.question = wideQuestion;
  const auto layout = ui.Layout(640, 400);
  PanelInput down;
  down.mouse = {layout.questionArea.x + 10, layout.questionArea.y + 10};
  down.wheel = -10000;
  ui.Update(down, 640, 400, wideGlyphs);
  const float bottom = ui.scroll;
  const auto wideLines = WrapGuidedPickQuestion(
      wideQuestion, wideGlyphs,
      layout.questionArea.width - 2 * 8.0f - 8.0f);
  const float expectedBottom = std::max(
      0.0f, wideLines.size() * 20.0f - (layout.questionArea.height - 2 * 8.0f));
  Require(bottom > 0.0f && std::abs(bottom - expectedBottom) < 0.01f &&
              bottom < wideQuestion.size() * 20.0f,
          "Question scrolling reaches a bounded bottom position");
  down.wheel = -1;
  ui.Update(down, 640, 400, wideGlyphs);
  Require(ui.scroll == bottom, "Question scrolling is capped at the final line");
  down.wheel = 10000;
  ui.Update(down, 640, 400, wideGlyphs);
  Require(ui.scroll == 0.0f, "Question scrolling is capped at the first line");
}
}  // namespace

int main() {
  try {
    CheckLayout();
    CheckActionsAndCapture();
    CheckQuestionWrappingAndScroll();
    std::cout << "PASS guided-pick question UI layout, input, and scrolling\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
