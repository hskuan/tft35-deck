#include "icons.h"

#include "lcd.h"

using namespace Lcd;

namespace {

void slash(int16_t cx, int16_t cy, uint16_t fg, uint16_t bg)
{
  drawLine(cx - 15, cy - 15, cx + 15, cy + 15, bg, 7);
  drawLine(cx - 15, cy - 15, cx + 15, cy + 15, fg, 3);
}

void mic(int16_t cx, int16_t cy, uint16_t fg, uint16_t bg)
{
  drawRing(cx, cy - 2, 12, 3, fg, bg);
  fillRect(cx - 12, cy - 16, 25, 14, bg);  // keep only the lower half of the ring
  fillRoundRect(cx - 6, cy - 18, 13, 22, 6, fg);
  fillRect(cx - 1, cy + 10, 3, 6, fg);
  fillRect(cx - 8, cy + 15, 17, 3, fg);
}

void camera(int16_t cx, int16_t cy, uint16_t fg)
{
  fillRoundRect(cx - 17, cy - 10, 24, 20, 4, fg);
  fillTriangle(cx + 6, cy, cx + 17, cy - 9, cx + 17, cy + 9, fg);
}

void speaker(int16_t cx, int16_t cy, uint16_t fg)
{
  fillRect(cx - 17, cy - 6, 8, 12, fg);
  fillTriangle(cx - 12, cy, cx - 1, cy - 13, cx - 1, cy + 13, fg);
}

void sun(int16_t cx, int16_t cy, int16_t core, int16_t ray, uint16_t fg)
{
  static const int8_t dir[8][2] = {{10, 0}, {7, 7}, {0, 10}, {-7, 7}, {-10, 0}, {-7, -7}, {0, -10}, {7, -7}};
  fillCircle(cx, cy, core, fg);
  for (auto &d : dir) fillCircle(cx + d[0] * ray / 10, cy + d[1] * ray / 10, 2, fg);
}

void bell(int16_t cx, int16_t cy, uint16_t fg)
{
  fillCircle(cx, cy - 5, 10, fg);
  fillRect(cx - 10, cy - 5, 21, 12, fg);
  fillRect(cx - 14, cy + 7, 29, 4, fg);
  fillCircle(cx, cy + 14, 3, fg);
}

}  // namespace

void drawIcon(Icon icon, int16_t cx, int16_t cy, uint16_t fg, uint16_t bg)
{
  switch (icon) {
    case Icon::None: break;

    case Icon::Mic: mic(cx, cy, fg, bg); break;
    case Icon::MicOff: mic(cx, cy, fg, bg); slash(cx, cy, fg, bg); break;

    case Icon::Camera: camera(cx, cy, fg); break;
    case Icon::CameraOff: camera(cx, cy, fg); slash(cx, cy, fg, bg); break;

    case Icon::Record:
      drawRing(cx, cy, 15, 3, fg, bg);
      fillCircle(cx, cy, 8, fg);
      break;

    case Icon::Live:
      drawRing(cx, cy, 17, 3, fg, bg);
      drawRing(cx, cy, 11, 3, fg, bg);
      fillRect(cx - 7, cy - 18, 15, 37, bg);  // leave only the side arcs
      fillCircle(cx, cy, 4, fg);
      break;

    case Icon::Screenshot:
      fillRect(cx - 17, cy - 13, 10, 3, fg); fillRect(cx - 17, cy - 13, 3, 10, fg);
      fillRect(cx + 8, cy - 13, 10, 3, fg);  fillRect(cx + 15, cy - 13, 3, 10, fg);
      fillRect(cx - 17, cy + 11, 10, 3, fg); fillRect(cx - 17, cy + 4, 3, 10, fg);
      fillRect(cx + 8, cy + 11, 10, 3, fg);  fillRect(cx + 15, cy + 4, 3, 10, fg);
      fillCircle(cx, cy, 4, fg);
      break;

    case Icon::Scene:
      drawRect(cx - 17, cy - 13, 35, 23, fg, 3);
      fillRect(cx - 2, cy + 10, 5, 4, fg);
      fillRect(cx - 9, cy + 14, 19, 3, fg);
      break;

    case Icon::Coffee:
      drawRing(cx + 10, cy + 2, 7, 3, fg, bg);
      fillRoundRect(cx - 14, cy - 6, 22, 20, 4, fg);
      fillRect(cx - 9, cy - 16, 2, 7, fg);
      fillRect(cx - 3, cy - 18, 2, 9, fg);
      fillRect(cx + 3, cy - 16, 2, 7, fg);
      break;

    case Icon::Marker:
      fillRect(cx - 12, cy - 17, 3, 35, fg);
      fillTriangle(cx - 9, cy - 17, cx + 15, cy - 8, cx - 9, cy + 1, fg);
      break;

    case Icon::Folder:
      fillRoundRect(cx - 18, cy - 14, 15, 8, 2, fg);
      fillRoundRect(cx - 18, cy - 10, 36, 25, 3, fg);
      break;

    case Icon::Lock:
      drawRing(cx, cy - 6, 10, 3, fg, bg);
      fillRoundRect(cx - 13, cy - 4, 27, 20, 3, fg);
      fillCircle(cx, cy + 3, 3, bg);
      fillRect(cx - 1, cy + 3, 3, 7, bg);
      break;

    case Icon::Gear: {
      static const int8_t teeth[8][2] = {{14, 0}, {10, 10}, {0, 14}, {-10, 10}, {-14, 0}, {-10, -10}, {0, -14}, {10, -10}};
      for (auto &t : teeth) fillCircle(cx + t[0], cy + t[1], 4, fg);
      fillCircle(cx, cy, 12, fg);
      fillCircle(cx, cy, 5, bg);
      break;
    }

    case Icon::Back:
      fillTriangle(cx - 17, cy, cx - 3, cy - 14, cx - 3, cy + 14, fg);
      fillRect(cx - 4, cy - 4, 21, 9, fg);
      break;

    case Icon::Prev:
      fillRect(cx - 16, cy - 11, 4, 23, fg);
      fillTriangle(cx - 12, cy, cx + 1, cy - 11, cx + 1, cy + 11, fg);
      fillTriangle(cx + 1, cy, cx + 14, cy - 11, cx + 14, cy + 11, fg);
      break;

    case Icon::Next:
      fillTriangle(cx - 14, cy - 11, cx - 14, cy + 11, cx - 1, cy, fg);
      fillTriangle(cx - 1, cy - 11, cx - 1, cy + 11, cx + 12, cy, fg);
      fillRect(cx + 12, cy - 11, 4, 23, fg);
      break;

    case Icon::Play: fillTriangle(cx - 10, cy - 15, cx - 10, cy + 15, cx + 15, cy, fg); break;

    case Icon::Pause:
      fillRect(cx - 12, cy - 14, 8, 29, fg);
      fillRect(cx + 4, cy - 14, 8, 29, fg);
      break;

    case Icon::Stop: fillRect(cx - 12, cy - 12, 25, 25, fg); break;

    case Icon::VolDown:
      speaker(cx - 2, cy, fg);
      fillRect(cx + 5, cy - 2, 13, 4, fg);
      break;

    case Icon::VolUp:
      speaker(cx - 2, cy, fg);
      fillRect(cx + 5, cy - 2, 13, 4, fg);
      fillRect(cx + 10, cy - 7, 4, 14, fg);
      break;

    case Icon::Mute:
      speaker(cx - 2, cy, fg);
      drawLine(cx + 5, cy - 6, cx + 16, cy + 6, fg, 3);
      drawLine(cx + 5, cy + 6, cx + 16, cy - 6, fg, 3);
      break;

    case Icon::Speaker:
      drawRing(cx - 2, cy, 15, 3, fg, bg);
      drawRing(cx - 2, cy, 9, 3, fg, bg);
      fillRect(cx - 19, cy - 16, 21, 33, bg);
      speaker(cx - 2, cy, fg);
      break;

    case Icon::SunSmall: sun(cx, cy, 5, 11, fg); break;
    case Icon::Sun: sun(cx, cy, 8, 16, fg); break;

    case Icon::Bell: bell(cx, cy, fg); break;
    case Icon::BellOff: bell(cx, cy, fg); slash(cx, cy, fg, bg); break;

    case Icon::Target:
      drawRing(cx, cy, 13, 2, fg, bg);
      fillRect(cx - 18, cy - 1, 37, 3, fg);
      fillRect(cx - 1, cy - 18, 3, 37, fg);
      break;

    case Icon::Palette:
      fillRoundRect(cx - 16, cy - 16, 15, 15, 3, Color::Red);
      fillRoundRect(cx + 2, cy - 16, 15, 15, 3, Color::Green);
      fillRoundRect(cx - 16, cy + 2, 15, 15, 3, Color::Blue);
      fillRoundRect(cx + 2, cy + 2, 15, 15, 3, Color::White);
      break;

    case Icon::Info:
      drawRing(cx, cy, 16, 3, fg, bg);
      fillCircle(cx, cy - 7, 3, fg);
      fillRect(cx - 2, cy - 2, 5, 12, fg);
      break;
  }
}
