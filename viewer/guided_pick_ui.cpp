#include "guided_pick_ui.h"
#include "raymath.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace dingcad {
namespace {
constexpr float margin = 12.0f;
constexpr float maxCardWidth = 420.0f;
constexpr float maxCardHeight = 220.0f;
constexpr float bodyFontSize = 16.0f;
constexpr float lineHeight = 20.0f;
constexpr float bodyPadding = 8.0f;
constexpr float scrollbarGutter = 8.0f;

const Color ink{39, 52, 46, 255};
const Color muted{91, 108, 98, 255};
const Color accent{48, 108, 78, 255};
const Color glass{201, 211, 199, 235};
const Color paper{247, 246, 239, 240};

size_t NextUtf8(const std::string &text, size_t position) {
  if (position >= text.size()) return text.size();
  size_t next = position + 1;
  while (next < text.size() &&
         (static_cast<unsigned char>(text[next]) & 0xc0) == 0x80) {
    ++next;
  }
  return next;
}

float TextWidthForArea(Rectangle area) {
  return std::max(0.0f, area.width - 2.0f * bodyPadding - scrollbarGutter);
}

float ViewportHeight(Rectangle area) {
  return std::max(0.0f, area.height - 2.0f * bodyPadding);
}

float ScrollLimit(const std::string &question, Rectangle area,
                  const GuidedPickTextMeasure &measure) {
  const auto lines = WrapGuidedPickQuestion(question, measure,
                                            TextWidthForArea(area));
  return std::max(0.0f, lines.size() * lineHeight - ViewportHeight(area));
}

std::string HeadingForKind(const std::string &kind) {
  if (kind == "part") return "Select a part";
  if (kind == "surface") return "Select a surface";
  if (kind == "edge") return "Select an edge";
  if (kind == "vertex") return "Select a vertex";
  return "Select an item";
}

bool Hit(const PanelInput &input, Rectangle rect) {
  return input.pressed && CheckCollisionPointRec(input.mouse, rect);
}

void DrawButton(Rectangle rect, const char *label, Font font, bool enabled,
                bool primary, bool hovered) {
  if (rect.width <= 0 || rect.height <= 0) return;
  Color background = primary ? accent : Color{234, 239, 228, 230};
  if (!enabled) background = {219, 224, 213, 165};
  else if (hovered) {
    background = primary ? Color{37, 90, 63, 255}
                         : Color{222, 231, 214, 245};
  }
  DrawRectangleRounded(rect, 0.18f, 6, background);

  const float size = std::min(14.0f, rect.height * 0.65f);
  const float labelWidth = MeasureTextEx(font, label, size, 0).x;
  const float x = rect.x + std::max(3.0f, (rect.width - labelWidth) / 2.0f);
  const float y = rect.y + std::max(0.0f, (rect.height - size) / 2.0f);
  DrawTextEx(font, label, {x, y}, size, 0,
             enabled ? (primary ? WHITE : ink) : muted);
}

void BeginLogicalClip(Rectangle area, int width, float logicalScale) {
  const float scale = std::isfinite(logicalScale) && logicalScale > 0
                          ? logicalScale
                          : width > 0
                                ? static_cast<float>(GetScreenWidth()) / width
                                : 1.0f;
  BeginScissorMode(static_cast<int>(area.x * scale),
                   static_cast<int>(area.y * scale),
                   std::max(0, static_cast<int>(area.width * scale)),
                   std::max(0, static_cast<int>(area.height * scale)));
}

}  // namespace

std::vector<std::string> WrapGuidedPickQuestion(
    const std::string &question, const GuidedPickTextMeasure &measure,
    float maxWidth) {
  const auto textWidth = measure ? measure : GuidedPickTextMeasure(
      [](const std::string &text) { return static_cast<float>(text.size()); });
  const float available = std::max(0.0f, maxWidth);
  std::vector<std::string> lines;
  std::string paragraph;

  auto wrapParagraph = [&]() {
    std::string line;
    for (size_t position = 0; position < paragraph.size();) {
      const size_t next = NextUtf8(paragraph, position);
      const std::string codepoint = paragraph.substr(position, next - position);

      if (!line.empty() && textWidth(line + codepoint) > available) {
        const size_t breakAt = line.find_last_of(" \t");
        if (breakAt != std::string::npos) {
          lines.push_back(line.substr(0, breakAt + 1));
          line.erase(0, breakAt + 1);
          if (!line.empty() && textWidth(line + codepoint) > available) {
            lines.push_back(std::move(line));
            line.clear();
          }
        } else {
          lines.push_back(std::move(line));
          line.clear();
        }
      }
      line += codepoint;
      position = next;
    }
    lines.push_back(std::move(line));
  };

  for (size_t position = 0; position < question.size();) {
    const size_t next = NextUtf8(question, position);
    const auto codepoint = question.substr(position, next - position);
    if (codepoint == "\n" || codepoint == "\r") {
      wrapParagraph();
      paragraph.clear();
      if (codepoint == "\r" && next < question.size() && question[next] == '\n') {
        position = next + 1;
      } else {
        position = next;
      }
    } else {
      paragraph += codepoint;
      position = next;
    }
  }
  wrapParagraph();
  return lines;
}

GuidedPickLayout GuidedPickUi::Layout(int width, int height) const {
  GuidedPickLayout layout{};
  const float viewportWidth = std::max(1.0f, static_cast<float>(width));
  const float viewportHeight = std::max(1.0f, static_cast<float>(height));
  const float cardWidth =
      std::min(maxCardWidth, std::max(1.0f, viewportWidth / 2.0f - 24.0f));
  const float cardHeight =
      std::min(maxCardHeight, std::max(1.0f, viewportHeight * 0.5f));
  layout.card = {std::max(0.0f, viewportWidth - margin - cardWidth),
                 std::max(0.0f, viewportHeight - margin - cardHeight),
                 cardWidth, cardHeight};

  const float padX = std::min(12.0f, cardWidth * 0.1f);
  const float padY = std::min(12.0f, cardHeight * 0.1f);
  const float contentX = layout.card.x + padX;
  const float contentWidth = std::max(0.0f, cardWidth - 2.0f * padX);
  const float innerHeight = std::max(0.0f, cardHeight - 2.0f * padY);
  const float headingHeight = std::min(22.0f, innerHeight);
  const float actionHeight = std::min(
      32.0f, std::max(0.0f, innerHeight - headingHeight));
  const float remaining =
      std::max(0.0f, innerHeight - headingHeight - actionHeight);
  const float headingGap = std::min(8.0f, remaining * 0.35f);
  const float actionGap = std::min(8.0f, remaining - headingGap);
  const float headingY = layout.card.y + padY;
  const float actionY = layout.card.y + cardHeight - padY - actionHeight;
  layout.heading = {contentX, headingY, contentWidth, headingHeight};
  const float questionY = headingY + headingHeight + headingGap;
  layout.questionArea = {contentX, questionY, contentWidth,
                         std::max(0.0f, actionY - actionGap - questionY)};

  const float buttonGap = std::min(8.0f, contentWidth * 0.04f);
  const float cancelWidth = std::min(82.0f, std::max(0.0f, contentWidth * 0.38f));
  const float confirmWidth =
      std::max(0.0f, contentWidth - cancelWidth - buttonGap);
  layout.cancel = {contentX, actionY, cancelWidth, actionHeight};
  layout.confirm = {contentX + cancelWidth + buttonGap, actionY, confirmWidth,
                    actionHeight};
  return layout;
}

Rectangle GuidedPickUi::Bounds(int width, int height) const {
  return Layout(width, height).card;
}

bool GuidedPickUi::CapturesMouse(const PanelInput &input, int width,
                                 int height) const {
  return active &&
         (gesture || CheckCollisionPointRec(input.mouse, Bounds(width, height)));
}

GuidedPickActions GuidedPickUi::Update(const PanelInput &input, int width,
                                       int height,
                                       const GuidedPickTextMeasure &measure) {
  GuidedPickActions actions;
  if (!active) {
    gesture = false;
    return actions;
  }

  lastMouse = input.mouse;
  hasLastMouse = true;
  const auto layout = Layout(width, height);
  if (input.pressed || input.rightPressed) {
    gesture = CheckCollisionPointRec(input.mouse, layout.card);
  } else if (!input.leftDown && !input.rightDown) {
    gesture = false;
  }

  const Font defaultFont = GetFontDefault();
  const GuidedPickTextMeasure actualMeasure = measure
      ? measure
      : GuidedPickTextMeasure([defaultFont](const std::string &text) {
          return MeasureTextEx(defaultFont, text.c_str(), bodyFontSize, 0).x;
        });
  const float maxScroll =
      ScrollLimit(question, layout.questionArea, actualMeasure);
  if (CheckCollisionPointRec(input.mouse, layout.questionArea)) {
    scroll -= input.wheel * lineHeight * 3.0f;
  }
  scroll = Clamp(scroll, 0.0f, maxScroll);

  if (input.escape || Hit(input, layout.cancel)) {
    actions.cancel = true;
    return actions;
  }
  if (canConfirm && (input.enter || Hit(input, layout.confirm))) {
    actions.confirm = true;
  }
  return actions;
}

void GuidedPickUi::Draw(Font font, int width, int height,
                        float logicalScale) const {
  if (!active) return;
  const auto layout = Layout(width, height);
  DrawRectangleRounded(layout.card, 0.045f, 8, glass);

  const auto heading = HeadingForKind(kind);
  DrawTextEx(font, heading.c_str(), {layout.heading.x, layout.heading.y}, 18, 0,
             ink);

  DrawRectangleRounded(layout.questionArea, 0.08f, 6, paper);
  BeginLogicalClip(layout.questionArea, width, logicalScale);
  const float textWidth = TextWidthForArea(layout.questionArea);
  const auto measure = [font](const std::string &text) {
    return MeasureTextEx(font, text.c_str(), bodyFontSize, 0).x;
  };
  const auto lines = WrapGuidedPickQuestion(question, measure, textWidth);
  const float maxScroll = std::max(
      0.0f, lines.size() * lineHeight - ViewportHeight(layout.questionArea));
  const float visibleScroll = Clamp(scroll, 0.0f, maxScroll);
  for (size_t index = 0; index < lines.size(); ++index) {
    const float y = layout.questionArea.y + bodyPadding + index * lineHeight -
                    visibleScroll;
    DrawTextEx(font, lines[index].c_str(),
               {layout.questionArea.x + bodyPadding, y}, bodyFontSize, 0, ink);
  }
  if (maxScroll > 0 && layout.questionArea.height > 12) {
    const float trackHeight = layout.questionArea.height - 12.0f;
    const float thumbHeight = std::max(
        10.0f, trackHeight * ViewportHeight(layout.questionArea) /
                              std::max(lineHeight, lines.size() * lineHeight));
    const float travel = std::max(0.0f, trackHeight - thumbHeight);
    const float thumbY = layout.questionArea.y + 6.0f +
                         (maxScroll > 0 ? visibleScroll / maxScroll * travel : 0);
    DrawRectangleRounded(
        {layout.questionArea.x + layout.questionArea.width - 5.0f, thumbY,
         3.0f, thumbHeight},
        0.5f, 4, Fade(muted, 0.65f));
  }
  EndScissorMode();

  const bool cancelHovered =
      hasLastMouse && CheckCollisionPointRec(lastMouse, layout.cancel);
  const bool confirmHovered =
      canConfirm && hasLastMouse &&
      CheckCollisionPointRec(lastMouse, layout.confirm);
  DrawButton(layout.cancel, "Cancel", font, true, false, cancelHovered);
  DrawButton(layout.confirm, "Confirm", font, canConfirm, true,
             confirmHovered);
}

}  // namespace dingcad
