#include "lcd.h"

#include <Arduino.h>

#include "config.h"
#include "font5x7.h"
#include "spin.h"

// FSMC bank 1 / NE1, RS on A23. In 16-bit mode HADDR[24] drives A23, so
// 0x60FFFFFE is the command register (A23 = 0) and 0x61000000 the data register.
struct LcdBus {
  volatile uint16_t REG;
  volatile uint16_t RAM;
};
#define LCD_BUS ((LcdBus *)0x60FFFFFEUL)

// Timings copied from BTT's Hal/stm32f2_f4xx/lcd.c (extended mode, access mode A):
//   read  (BTR1):  ADDSET 1, DATAST 15
//   write (BWTR1): ADDSET 0, DATAST TFTLCD_DRIVER_SPEED (0x03 on TFT35 V3.0)
#define FSMC_BCR1  0x00005011UL  // MBKEN | MWID 16 bit | WREN | EXTMOD, SRAM type
#define FSMC_BTR1  0x00000F01UL
#define FSMC_BWTR1 0x00000300UL

namespace {

LcdController detected = LcdController::Unknown;
uint32_t lastId = 0;

inline void writeCmd(uint16_t cmd) { LCD_BUS->REG = cmd; }
inline void writeData(uint16_t data) { LCD_BUS->RAM = data; }
inline uint16_t readData() { return LCD_BUS->RAM; }

void gpioAf12(GPIO_TypeDef *port, uint16_t pinMask)
{
  for (uint32_t pin = 0; pin < 16; pin++) {
    if (!(pinMask & (1U << pin))) continue;
    port->MODER = (port->MODER & ~(3UL << (pin * 2))) | (2UL << (pin * 2));
    port->OSPEEDR |= (3UL << (pin * 2));
    port->OTYPER &= ~(1UL << pin);
    port->PUPDR &= ~(3UL << (pin * 2));
    volatile uint32_t &afr = port->AFR[pin >> 3];
    afr = (afr & ~(0xFUL << ((pin & 7) * 4))) | (12UL << ((pin & 7) * 4));
  }
}

void fsmcInit()
{
  RCC->AHB1ENR |= RCC_AHB1ENR_GPIODEN | RCC_AHB1ENR_GPIOEEN;
  RCC->AHB3ENR |= RCC_AHB3ENR_FSMCEN;
  (void)RCC->AHB3ENR;

  // PD0 D2, PD1 D3, PD4 NOE, PD5 NWE, PD7 NE1, PD8-10 D13-15, PD14 D0, PD15 D1
  gpioAf12(GPIOD, (1 << 0) | (1 << 1) | (1 << 4) | (1 << 5) | (1 << 7) | (1 << 8) | (1 << 9) |
                      (1 << 10) | (1 << 14) | (1 << 15));
  // PE2 A23 (RS), PE7-15 D4-D12
  gpioAf12(GPIOE, (1 << 2) | 0xFF80);

  FSMC_Bank1->BTCR[1] = FSMC_BTR1;
  FSMC_Bank1E->BWTR[0] = FSMC_BWTR1;
  FSMC_Bank1->BTCR[0] = FSMC_BCR1;
}

uint32_t readId(uint8_t cmd)
{
  writeCmd(cmd);
  (void)readData();  // dummy read
  uint32_t id = 0;
  for (int i = 0; i < 3; i++) id = (id << 8) | (readData() & 0xFF);
  return id;
}

// BTT's landscape MADCTL values: 0x28 (ILI9488/ST7796S), 0x60 (NT35310);
// rotated 180: 0xE8 / 0xA0.
uint8_t madctl()
{
  uint8_t m = (detected == LcdController::NT35310) ? 0x60 : 0x28;
  if (LCD_ROTATE_180) m ^= 0xC0;
  if (LCD_MIRROR_X) m ^= 0x40;
  if (LCD_SWAP_RB) m ^= 0x08;
  return m;
}

// BTT's init sequence, shared by ILI9488, ST7796S and NT35310 (LCD_Driver/*.c).
// No software reset: the panel is reset with the MCU.
void initSequence()
{
  writeCmd(0xC0); writeData(0x0C); writeData(0x02);
  writeCmd(0xC1); writeData(0x44);
  writeCmd(0xC5); writeData(0x00); writeData(0x16); writeData(0x80);
  writeCmd(0x36); writeData(madctl());
  writeCmd(0x3A); writeData(0x55);  // 16 bpp
  writeCmd(0xB0); writeData(0x00);
  writeCmd(0xB1); writeData(0xB0);  // 70 Hz
  writeCmd(0xB4); writeData(0x02);
  writeCmd(0xB6); writeData(0x02); writeData(0x02);
  writeCmd(0xE9); writeData(0x00);
  writeCmd(0xF7); writeData(0xA9); writeData(0x51); writeData(0x2C); writeData(0x82);
  writeCmd(0x11);  // sleep out
  spinMs(120);
  if (LCD_INVERT) writeCmd(0x21);
  writeCmd(0x29);  // display on
}

void setWindow(int16_t x0, int16_t y0, int16_t x1, int16_t y1)
{
  writeCmd(0x2A);
  writeData(x0 >> 8); writeData(x0 & 0xFF); writeData(x1 >> 8); writeData(x1 & 0xFF);
  writeCmd(0x2B);
  writeData(y0 >> 8); writeData(y0 & 0xFF); writeData(y1 >> 8); writeData(y1 & 0xFF);
  writeCmd(0x2C);
}

void pushColor(uint16_t color, uint32_t count)
{
  while (count >= 8) {
    writeData(color); writeData(color); writeData(color); writeData(color);
    writeData(color); writeData(color); writeData(color); writeData(color);
    count -= 8;
  }
  while (count--) writeData(color);
}

int16_t isqrt(int32_t n)
{
  if (n <= 0) return 0;
  int32_t r = (int32_t)sqrtf((float)n);
  while (r * r > n) r--;
  while ((r + 1) * (r + 1) <= n) r++;
  return (int16_t)r;
}

}  // namespace

namespace Lcd {

LcdController begin()
{
  fsmcInit();
  spinMs(10);

  // ID reads as in BTT: cmd, dummy read, then the ID bytes
  lastId = readId(0xD3);
  if ((lastId & 0xFFFF) == 0x9488) {
    detected = LcdController::ILI9488;
  } else if ((lastId & 0xFFFF) == 0x7796) {
    detected = LcdController::ST7796S;
  } else {
    uint32_t id = readId(0xD4);
    if ((id & 0xFFFF) == 0x5310) {
      detected = LcdController::NT35310;
      lastId = id;
    }
  }

  initSequence();
  fillScreen(Color::Black);
  spinMs(120);
  return detected;
}

const char *controllerName(LcdController c)
{
  switch (c) {
    case LcdController::ILI9488: return "ILI9488";
    case LcdController::ST7796S: return "ST7796S";
    case LcdController::NT35310: return "NT35310";
    default: return "unknown";
  }
}

LcdController controller() { return detected; }
uint32_t rawId() { return lastId; }
int16_t width() { return LCD_WIDTH; }
int16_t height() { return LCD_HEIGHT; }

void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color)
{
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > LCD_WIDTH) w = LCD_WIDTH - x;
  if (y + h > LCD_HEIGHT) h = LCD_HEIGHT - y;
  if (w <= 0 || h <= 0) return;
  setWindow(x, y, x + w - 1, y + h - 1);
  pushColor(color, (uint32_t)w * h);
}

void fillScreen(uint16_t color) { fillRect(0, 0, LCD_WIDTH, LCD_HEIGHT, color); }
void drawPixel(int16_t x, int16_t y, uint16_t color) { fillRect(x, y, 1, 1, color); }
void drawHLine(int16_t x, int16_t y, int16_t w, uint16_t color) { fillRect(x, y, w, 1, color); }
void drawVLine(int16_t x, int16_t y, int16_t h, uint16_t color) { fillRect(x, y, 1, h, color); }

void drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color, int16_t t)
{
  fillRect(x, y, w, t, color);
  fillRect(x, y + h - t, w, t, color);
  fillRect(x, y + t, t, h - 2 * t, color);
  fillRect(x + w - t, y + t, t, h - 2 * t, color);
}

int16_t roundInset(int16_t row, int16_t h, int16_t r)
{
  if (r <= 0 || row < 0 || row >= h) return 0;
  if (row >= h - r) row = h - 1 - row;  // bottom corners mirror the top ones
  if (row >= r) return 0;
  int16_t dy = r - row;
  return r - isqrt((int32_t)r * r - (int32_t)dy * dy + r);
}

void fillRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t color)
{
  if (r * 2 > w) r = w / 2;
  if (r * 2 > h) r = h / 2;
  if (r <= 0) {
    fillRect(x, y, w, h, color);
    return;
  }
  fillRect(x, y + r, w, h - 2 * r, color);
  for (int16_t i = 0; i < r; i++) {
    int16_t inset = roundInset(i, h, r);
    drawHLine(x + inset, y + i, w - 2 * inset, color);
    drawHLine(x + inset, y + h - 1 - i, w - 2 * inset, color);
  }
}

void strokeRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t color, int16_t t)
{
  int16_t ri = r > t ? r - t : 0;
  for (int16_t row = 0; row < h; row++) {
    int16_t outer = roundInset(row, h, r);
    if (row < t || row >= h - t) {
      drawHLine(x + outer, y + row, w - 2 * outer, color);
      continue;
    }
    int16_t inner = t + roundInset(row - t, h - 2 * t, ri);
    drawHLine(x + outer, y + row, inner - outer, color);
    drawHLine(x + w - inner, y + row, inner - outer, color);
  }
}

void drawRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t color,
                   uint16_t inner, int16_t t)
{
  fillRoundRect(x, y, w, h, r, color);
  fillRoundRect(x + t, y + t, w - 2 * t, h - 2 * t, r > t ? r - t : 0, inner);
}

void fillCircle(int16_t cx, int16_t cy, int16_t r, uint16_t color)
{
  for (int16_t dy = -r; dy <= r; dy++) {
    int16_t dx = isqrt((int32_t)r * r - (int32_t)dy * dy + r);
    drawHLine(cx - dx, cy + dy, 2 * dx + 1, color);
  }
}

void drawRing(int16_t cx, int16_t cy, int16_t r, int16_t t, uint16_t color, uint16_t inner)
{
  fillCircle(cx, cy, r, color);
  fillCircle(cx, cy, r - t, inner);
}

void fillTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t color)
{
  // sort by y
  if (y0 > y1) { int16_t t; t = y0; y0 = y1; y1 = t; t = x0; x0 = x1; x1 = t; }
  if (y1 > y2) { int16_t t; t = y1; y1 = y2; y2 = t; t = x1; x1 = x2; x2 = t; }
  if (y0 > y1) { int16_t t; t = y0; y0 = y1; y1 = t; t = x0; x0 = x1; x1 = t; }

  if (y0 == y2) {
    int16_t a = min(x0, min(x1, x2)), b = max(x0, max(x1, x2));
    drawHLine(a, y0, b - a + 1, color);
    return;
  }

  for (int16_t y = y0; y <= y2; y++) {
    // long edge 0->2
    int16_t xa = x0 + (int32_t)(x2 - x0) * (y - y0) / (y2 - y0);
    int16_t xb;
    if (y < y1 || y1 == y2) {
      xb = (y1 == y0) ? x1 : x0 + (int32_t)(x1 - x0) * (y - y0) / (y1 - y0);
    } else {
      xb = x1 + (int32_t)(x2 - x1) * (y - y1) / (y2 - y1);
    }
    if (xa > xb) { int16_t t = xa; xa = xb; xb = t; }
    drawHLine(xa, y, xb - xa + 1, color);
  }
}

void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color, int16_t t)
{
  int16_t dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
  int16_t dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
  int16_t err = dx + dy;
  int16_t off = t / 2;
  for (;;) {
    fillRect(x0 - off, y0 - off, t, t, color);
    if (x0 == x1 && y0 == y1) break;
    int16_t e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
}

void drawRowLE(int16_t x, int16_t y, int16_t w, const uint8_t *data)
{
  if (y < 0 || y >= LCD_HEIGHT || x < 0 || w <= 0 || x + w > LCD_WIDTH) return;
  setWindow(x, y, x + w - 1, y);
  for (int16_t i = 0; i < w; i++) writeData((uint16_t)(data[2 * i] | (data[2 * i + 1] << 8)));
}

void drawImage(int16_t x, int16_t y, int16_t w, int16_t h, const uint16_t *pixels)
{
  if (x < 0 || y < 0 || w <= 0 || h <= 0 || x + w > LCD_WIDTH || y + h > LCD_HEIGHT) return;
  setWindow(x, y, x + w - 1, y + h - 1);
  for (uint32_t i = 0, n = (uint32_t)w * h; i < n; i++) writeData(pixels[i]);
}

void drawChar(int16_t x, int16_t y, char c, uint8_t scale, uint16_t fg, uint16_t bg)
{
  int16_t w = 6 * scale, h = 8 * scale;
  if (x < 0 || y < 0 || x + w > LCD_WIDTH || y + h > LCD_HEIGHT) return;
  if (c < 0x20 || c > 0x7E) c = '?';
  const uint8_t *glyph = FONT5X7[c - 0x20];

  setWindow(x, y, x + w - 1, y + h - 1);
  for (int16_t row = 0; row < 8; row++) {
    for (uint8_t sy = 0; sy < scale; sy++) {
      for (int16_t col = 0; col < 6; col++) {
        bool on = col < 5 && row < 7 && (glyph[col] >> row) & 1;
        pushColor(on ? fg : bg, scale);
      }
    }
  }
}

void drawText(int16_t x, int16_t y, const char *s, uint8_t scale, uint16_t fg, uint16_t bg)
{
  for (; *s; s++, x += 6 * scale) drawChar(x, y, *s, scale, fg, bg);
}

int16_t textWidth(const char *s, uint8_t scale)
{
  int16_t n = (int16_t)strlen(s);
  return n ? n * 6 * scale - scale : 0;  // drop trailing spacing column
}

void drawTextCentered(int16_t cx, int16_t y, const char *s, uint8_t scale, uint16_t fg, uint16_t bg)
{
  drawText(cx - textWidth(s, scale) / 2, y, s, scale, fg, bg);
}

}  // namespace Lcd
