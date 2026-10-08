#include "selection_ui.h"

#include <algorithm>
#include <utility>
#include <vector>

namespace dingcad {
namespace {
constexpr float margin = 12.0f;
constexpr float pad = 10.0f;
constexpr float cardHeight = 116.0f;
constexpr float modeHeight = 24.0f;
constexpr float compactModeHeight = 20.0f;
constexpr float actionHeight = 24.0f;
constexpr float compactActionHeight = 20.0f;

const Color ink{39, 52, 46, 255};
const Color muted{91, 108, 98, 255};
const Color accent{48, 108, 78, 255};
const Color glass{201, 211, 199, 235};

float CardPadding(Rectangle card) {
  return std::min(pad, std::max(0.0f, card.width) * 0.25f);
}

std::string OneLine(std::string text) {
  for (char &c : text) {
    const auto byte = static_cast<unsigned char>(c);
    if (byte == '\n' || byte == '\r' || byte == '\t' || byte < 0x20) c = ' ';
  }
  return text;
}

std::string FitText(std::string text, Font font, float size, float width) {
  text = OneLine(std::move(text));
  if (MeasureTextEx(font, text.c_str(), size, 0).x <= width) return text;
  if (MeasureTextEx(font, "...", size, 0).x > width) return {};

  std::vector<size_t> boundaries{0};
  for (size_t position = 0; position < text.size();) {
    size_t next = position + 1;
    while (next < text.size() &&
           (static_cast<unsigned char>(text[next]) & 0xc0) == 0x80) {
      ++next;
    }
    boundaries.push_back(next);
    position = next;
  }

  size_t low = 0;
  size_t high = boundaries.size() - 1;
  while (low < high) {
    const size_t middle = low + (high - low + 1) / 2;
    const auto candidate = text.substr(0, boundaries[middle]) + "...";
    if (MeasureTextEx(font, candidate.c_str(), size, 0).x <= width) low = middle;
    else high = middle - 1;
  }
  return text.substr(0, boundaries[low]) + "...";
}

void Label(const std::string &text, Font font, float x, float y, float width,
           float size = 15.0f, Color color = ink) {
  const auto fitted = FitText(text, font, size, std::max(0.0f, width));
  DrawTextEx(font, fitted.c_str(), {x, y}, size, 0, color);
}

void Card(Rectangle rect) {
  DrawRectangleRounded(rect, 0.045f, 8, glass);
}

void Button(Rectangle rect, const char *label, Font font, bool enabled,
            bool selected = false, float size = 14.0f,
            bool hovered = false) {
  if (rect.width <= 0 || rect.height <= 0) return;
  Color background = selected ? accent : Color{234, 239, 228, 230};
  if (hovered && enabled) {
    background = selected ? Color{37, 90, 63, 255}
                          : Color{222, 231, 214, 245};
  }
  if (!enabled) background = {219, 224, 213, 145};
  DrawRectangleRounded(rect, 0.18f, 6, background);
  const Color color = !enabled ? muted : selected ? WHITE : ink;
  const float textSize = std::min(size, rect.height * 0.65f);
  const float textWidth = MeasureTextEx(font, label, textSize, 0).x;
  const float textX = rect.x + std::max(5.0f, (rect.width - textWidth) / 2.0f);
  const float inset = std::min(5.0f, rect.width / 4.0f);
  Label(label, font, textX, rect.y + std::max(0.0f, (rect.height - textSize) / 2.0f),
        std::max(0.0f, rect.width - 2 * inset), textSize, color);
}

bool Hit(const PanelInput &input, Rectangle rect) {
  return input.pressed && CheckCollisionPointRec(input.mouse, rect);
}

const char *ModeLabel(size_t index) {
  static constexpr const char *labels[] = {"Part", "Surface", "Edge", "Vertex"};
  return index < 4 ? labels[index] : "Part";
}

}  // namespace

SelectionLayout SelectionUi::Layout(int width, int height) const {
  SelectionLayout layout{};
  const float cardWidth = std::min(360.0f, std::max(1.0f, width / 2.0f - 24.0f));
  const float actualHeight = std::max(1.0f, std::min(cardHeight, height - 2 * margin));
  layout.card = {std::max(0.0f, width - margin - cardWidth),
                 std::max(0.0f, height - margin - actualHeight),
                 cardWidth, actualHeight};

  const float cardPad = CardPadding(layout.card);
  const float modesX = layout.card.x + cardPad;
  const bool compact = actualHeight < 108.0f;
  const bool twoModeRows = cardWidth < 260.0f && !compact;
  const float modesWidth = std::max(0.0f, layout.card.width - 2 * cardPad);
  const float buttonHeight = std::min(
      compact || twoModeRows ? compactModeHeight : modeHeight,
      actualHeight * 0.22f);
  const float topGap = compact || twoModeRows ? 5.0f : 9.0f;
  const float modesY = layout.card.y +
      std::min(topGap, std::max(0.0f, actualHeight - buttonHeight) * 0.15f);
  if (twoModeRows) {
    const float rowGap = std::min(6.0f, modesWidth / 4.0f);
    const float modeWidth = std::max(0.0f, (modesWidth - rowGap) / 2.0f);
    const float rowSpacing = std::min(2.0f, std::max(0.0f, actualHeight - 2 * buttonHeight) * 0.2f);
    for (size_t n = 0; n < layout.modes.size(); ++n) {
      layout.modes[n] = {modesX + (n % 2) * (modeWidth + rowGap),
                         modesY + (n / 2) * (buttonHeight + rowSpacing), modeWidth,
                         buttonHeight};
    }
  } else {
    const float modeGap = std::min(5.0f, modesWidth / 8.0f);
    const float modeWidth = std::max(0.0f, (modesWidth - 3 * modeGap) / 4.0f);
    for (size_t n = 0; n < layout.modes.size(); ++n) {
      layout.modes[n] = {modesX + n * (modeWidth + modeGap), modesY,
                         modeWidth, buttonHeight};
    }
  }

  const float actionHeightActual = std::min(
      compact ? compactActionHeight : actionHeight, actualHeight * 0.25f);
  const float actionBottom = std::min(compact ? 6.0f : pad, actualHeight * 0.08f);
  const float actionY = layout.card.y + std::max(
      0.0f, actualHeight - actionBottom - actionHeightActual);
  const float actionWidth = std::max(0.0f, layout.card.width - 2 * cardPad);
  const float actionGap = std::min(6.0f, actionWidth * 0.08f);
  const float clearWidth = std::min(66.0f, actionWidth * 0.42f);
  const float copyWidth = std::min(82.0f, std::max(0.0f, actionWidth - clearWidth - actionGap));
  layout.clear = {layout.card.x + layout.card.width - cardPad - clearWidth, actionY,
                  clearWidth, actionHeightActual};
  layout.copy = {layout.clear.x - actionGap - copyWidth, actionY, copyWidth,
                 actionHeightActual};
  return layout;
}

Rectangle SelectionUi::Bounds(int width, int height) const {
  return Layout(width, height).card;
}

bool SelectionUi::CapturesMouse(const PanelInput &input, int width,
                                int height) const {
  return gesture || CheckCollisionPointRec(input.mouse, Bounds(width, height));
}

SelectionActions SelectionUi::Update(const PanelInput &input, int width,
                                      int height) {
  SelectionActions actions;
  lastMouse = input.mouse;
  hasLastMouse = true;
  const auto layout = Layout(width, height);

  if (input.pressed || input.rightPressed) {
    gesture = CheckCollisionPointRec(input.mouse, layout.card);
  } else if (!input.leftDown && !input.rightDown) {
    gesture = false;
  }

  if (!input.pressed) return actions;
  for (size_t index = 0; index < layout.modes.size(); ++index) {
    if (Hit(input, layout.modes[index])) {
      mode = static_cast<SelectionMode>(index);
      return actions;
    }
  }
  if (hasSelection && copyAvailable && Hit(input, layout.copy)) actions.copy = true;
  if (hasSelection && Hit(input, layout.clear)) actions.clear = true;
  return actions;
}

void SelectionUi::Draw(Font font, int width, int height) const {
  const auto layout = Layout(width, height);
  Card(layout.card);

  const auto selectedMode = static_cast<size_t>(mode);
  const bool compact = layout.card.height < 108.0f;
  const bool twoModeRows = layout.card.width < 260.0f && !compact;
  for (size_t index = 0; index < layout.modes.size(); ++index) {
    Button(layout.modes[index], ModeLabel(index), font, true,
           index == selectedMode, twoModeRows ? 12.0f : 14.0f,
           hasLastMouse && CheckCollisionPointRec(lastMouse, layout.modes[index]));
  }

  const float cardPad = CardPadding(layout.card);
  const float textX = layout.card.x + cardPad;
  const float textWidth = std::max(0.0f, layout.card.width - 2 * cardPad);
  const std::string visibleSummary = summary.empty() ? "No selection" : summary;
  const float summaryY = twoModeRows ? layout.card.y + 50.0f
                                     : layout.card.y + (compact ? 31.0f : 40.0f);
  if (layout.card.height >= 70.0f) {
    Label(visibleSummary, font, textX, summaryY, textWidth,
          twoModeRows ? 15.0f : 16.0f, hasSelection ? ink : muted);
  }
  if (!compact && !detail.empty()) {
    Label(detail, font, textX, layout.card.y + (twoModeRows ? 67.0f : 59.0f),
          textWidth, twoModeRows ? 11.0f : 13.0f, muted);
  }

  const float actionFont = layout.copy.width < 72.0f ? 12.0f : 14.0f;
  Button(layout.copy, "Copy ref", font, hasSelection && copyAvailable, false, actionFont,
         hasSelection && copyAvailable && hasLastMouse && CheckCollisionPointRec(lastMouse, layout.copy));
  Button(layout.clear, "Clear", font, hasSelection, false, actionFont,
         hasSelection && hasLastMouse && CheckCollisionPointRec(lastMouse, layout.clear));
}

std::optional<Vector2> PickGesture::Update(Vector2 mouse, bool pressed,
                                            bool down, bool released,
                                            bool blocked) {
  std::optional<Vector2> click;

  if (pressed) {
    active_ = true;
    dragging_ = false;
    canceled_ = blocked;
    origin_ = mouse;
  } else if (active_) {
    if (blocked) canceled_ = true;
    const float dx = mouse.x - origin_.x;
    const float dy = mouse.y - origin_.y;
    if (dx * dx + dy * dy > 16.0f) dragging_ = true;
  }

  if (active_ && !canceled_ && !dragging_ && released) click = mouse;

  if (active_ && released) {
    active_ = false;
    dragging_ = false;
    canceled_ = false;
  } else if (active_ && !down && !pressed) {
    // Recover cleanly if a platform misses the explicit release edge.
    active_ = false;
    dragging_ = false;
    canceled_ = false;
  }

  return click;
}

}  // namespace dingcad
