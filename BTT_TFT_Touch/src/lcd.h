#pragma once
#include <stdint.h>

// RGB565 helpers
constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

namespace Color {
constexpr uint16_t Black   = 0x0000;
constexpr uint16_t White   = 0xFFFF;
constexpr uint16_t Red     = rgb565(255, 0, 0);
constexpr uint16_t Green   = rgb565(0, 255, 0);
constexpr uint16_t Blue    = rgb565(0, 0, 255);
constexpr uint16_t Yellow  = rgb565(255, 255, 0);
constexpr uint16_t Cyan    = rgb565(0, 255, 255);
constexpr uint16_t Magenta = rgb565(255, 0, 255);
constexpr uint16_t Grey    = rgb565(128, 128, 128);
}

enum class LcdController : uint8_t { Unknown, ILI9488, ST7796S, NT35310 };

namespace Lcd {

// Configure FSMC + GPIO, detect the controller and run its init sequence.
LcdController begin();
LcdController controller();
const char *controllerName(LcdController c);
uint32_t rawId();  // last ID read during detection, for diagnostics

int16_t width();
int16_t height();

void fillScreen(uint16_t color);
void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
void drawPixel(int16_t x, int16_t y, uint16_t color);
void drawHLine(int16_t x, int16_t y, int16_t w, uint16_t color);
void drawVLine(int16_t x, int16_t y, int16_t h, uint16_t color);
void drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color, int16_t thickness = 1);
void fillRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t color);
// Outline only (thickness t), same corner shape as fillRoundRect
void strokeRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t color, int16_t t);
// Pixels cut off at each end of row `row` of a w x h rounded rect with radius r
int16_t roundInset(int16_t row, int16_t h, int16_t r);
void drawRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t color,
                   uint16_t inner, int16_t thickness);
void fillCircle(int16_t cx, int16_t cy, int16_t r, uint16_t color);
void drawRing(int16_t cx, int16_t cy, int16_t r, int16_t thickness, uint16_t color, uint16_t inner);
void fillTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t color);
void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color, int16_t thickness = 1);

// One row of w pixels, RGB565 little-endian bytes (as received from the host)
void drawRowLE(int16_t x, int16_t y, int16_t w, const uint8_t *data);
// A w x h block of RGB565 pixels, row-major
void drawImage(int16_t x, int16_t y, int16_t w, int16_t h, const uint16_t *pixels);

// 5x7 font in a 6x8 cell, scaled. Text is drawn with a solid background.
void drawChar(int16_t x, int16_t y, char c, uint8_t scale, uint16_t fg, uint16_t bg);
void drawText(int16_t x, int16_t y, const char *s, uint8_t scale, uint16_t fg, uint16_t bg);
void drawTextCentered(int16_t cx, int16_t y, const char *s, uint8_t scale, uint16_t fg, uint16_t bg);
int16_t textWidth(const char *s, uint8_t scale);

}  // namespace Lcd
