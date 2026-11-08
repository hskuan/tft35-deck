#include "touch.h"

#include <Arduino.h>

#include "config.h"
#include "diag.h"
#include "lcd.h"
#include "spin.h"

// XPT2046, bit-banged: CS PE6, SCK PE5, MISO PE4, MOSI PE3, PENIRQ PC13
#define T_CS   6
#define T_SCK  5
#define T_MISO 4
#define T_MOSI 3
#define T_PEN  13

// Control bytes: start | channel | 12-bit | differential | PD=00 (PENIRQ stays enabled)
#define CMD_X  0xD0
#define CMD_Y  0x90
#define CMD_Z1 0xB0

#define Z_THRESHOLD 30
#define MAX_SPREAD  150  // raw counts, ~15-20 px
#define SAMPLES     7

namespace {

int32_t cal[6];
bool calibrated = false;
Touch::ReadStats stats = {0, 0, 0};

inline void pinHigh(uint8_t p) { GPIOE->BSRR = 1UL << p; }
inline void pinLow(uint8_t p) { GPIOE->BSRR = 1UL << (p + 16); }
inline void halfClock() { spinUs(1); }

uint16_t transfer(uint8_t cmd)
{
  pinLow(T_CS);
  for (int8_t bit = 7; bit >= 0; bit--) {
    if (cmd & (1 << bit)) pinHigh(T_MOSI); else pinLow(T_MOSI);
    halfClock();
    pinHigh(T_SCK);
    halfClock();
    pinLow(T_SCK);
  }
  pinLow(T_MOSI);
  uint16_t v = 0;
  for (uint8_t i = 0; i < 16; i++) {
    halfClock();
    pinHigh(T_SCK);
    halfClock();
    v = (v << 1) | ((GPIOE->IDR >> T_MISO) & 1);
    pinLow(T_SCK);
  }
  pinHigh(T_CS);
  return (v >> 3) & 0x0FFF;
}

void sortSmall(uint16_t *a, uint8_t n)
{
  for (uint8_t i = 1; i < n; i++) {
    uint16_t v = a[i];
    int8_t j = i - 1;
    while (j >= 0 && a[j] > v) { a[j + 1] = a[j]; j--; }
    a[j + 1] = v;
  }
}

void outputPin(GPIO_TypeDef *port, uint8_t pin)
{
  port->MODER = (port->MODER & ~(3UL << (pin * 2))) | (1UL << (pin * 2));
  port->OTYPER &= ~(1UL << pin);
  port->OSPEEDR |= (2UL << (pin * 2));
}

void inputPin(GPIO_TypeDef *port, uint8_t pin, bool pullup)
{
  port->MODER &= ~(3UL << (pin * 2));
  port->PUPDR = (port->PUPDR & ~(3UL << (pin * 2))) | ((pullup ? 1UL : 0UL) << (pin * 2));
}

void beep() { tone(BUZZER_PIN, 2400, 25); }

void drawTarget(int16_t x, int16_t y, uint16_t color)
{
  Lcd::fillRect(x - 15, y - 1, 31, 3, color);
  Lcd::fillRect(x - 1, y - 15, 3, 31, color);
  Lcd::drawRing(x, y, 9, 2, color, Color::Black);
  Lcd::fillRect(x - 1, y - 1, 3, 3, color);
}

// Average raw samples while the finger is held, then wait for release.
bool captureHeld(uint16_t &rx, uint16_t &ry)
{
  while (!Touch::isPressed()) diagDelay(5);
  diagDelay(60);  // let the finger settle
  uint32_t sx = 0, sy = 0, n = 0;
  uint32_t start = millis();
  while (Touch::isPressed() && millis() - start < 400) {
    uint16_t x, y;
    if (Touch::readRaw(x, y)) { sx += x; sy += y; n++; }
    diagDelay(5);
  }
  beep();
  while (Touch::isPressed()) diagDelay(5);
  diagDelay(80);
  if (n < 5) return false;
  rx = sx / n;
  ry = sy / n;
  return true;
}

bool solve(const int32_t rx[3], const int32_t ry[3], const int32_t s[3], double out[3])
{
  double det = (double)rx[0] * (ry[1] - ry[2]) - (double)ry[0] * (rx[1] - rx[2]) +
               ((double)rx[1] * ry[2] - (double)rx[2] * ry[1]);
  if (fabs(det) < 1000.0) return false;
  out[0] = ((double)s[0] * (ry[1] - ry[2]) - (double)ry[0] * (s[1] - s[2]) +
            ((double)s[1] * ry[2] - (double)s[2] * ry[1])) / det;
  out[1] = ((double)rx[0] * (s[1] - s[2]) - (double)s[0] * (rx[1] - rx[2]) +
            ((double)rx[1] * s[2] - (double)rx[2] * s[1])) / det;
  out[2] = ((double)rx[0] * ((double)ry[1] * s[2] - (double)ry[2] * s[1]) -
            (double)ry[0] * ((double)rx[1] * s[2] - (double)rx[2] * s[1]) +
            (double)s[0] * ((double)rx[1] * ry[2] - (double)rx[2] * ry[1])) / det;
  return true;
}

}  // namespace

namespace Touch {

void begin()
{
  RCC->AHB1ENR |= RCC_AHB1ENR_GPIOCEN | RCC_AHB1ENR_GPIOEEN;
  (void)RCC->AHB1ENR;
  pinHigh(T_CS);
  pinLow(T_SCK);
  outputPin(GPIOE, T_CS);
  outputPin(GPIOE, T_SCK);
  outputPin(GPIOE, T_MOSI);
  inputPin(GPIOE, T_MISO, false);
  inputPin(GPIOC, T_PEN, true);
  transfer(CMD_X);  // arm PENIRQ

#ifdef TOUCH_CAL
  static const int32_t builtin[6] = TOUCH_CAL;
  setCalibration(builtin);
#endif
}

bool isPressed() { return !((GPIOC->IDR >> T_PEN) & 1); }

const ReadStats &lastRead() { return stats; }

bool readRaw(uint16_t &rx, uint16_t &ry)
{
  if (!isPressed()) { stats.error = 1; return false; }
  stats.z = transfer(CMD_Z1);
  if (stats.z < Z_THRESHOLD) { stats.error = 2; return false; }

  uint16_t xs[SAMPLES], ys[SAMPLES];
  for (uint8_t i = 0; i < SAMPLES; i++) {
    xs[i] = transfer(CMD_X);
    ys[i] = transfer(CMD_Y);
  }
  transfer(CMD_X);  // finish with PD=00 so PENIRQ is re-armed
  if (!isPressed()) { stats.error = 3; return false; }

  sortSmall(xs, SAMPLES);
  sortSmall(ys, SAMPLES);
  // reject noisy reads: middle samples must agree
  uint16_t sx = xs[SAMPLES - 2] - xs[1], sy = ys[SAMPLES - 2] - ys[1];
  stats.spread = sx > sy ? sx : sy;
  if (stats.spread > MAX_SPREAD) { stats.error = 4; return false; }
  stats.error = 0;
  rx = (xs[2] + xs[3] + xs[4]) / 3;
  ry = (ys[2] + ys[3] + ys[4]) / 3;
  return true;
}

bool read(int16_t &x, int16_t &y)
{
  if (!calibrated) return false;
  uint16_t rx, ry;
  if (!readRaw(rx, ry)) return false;
  int32_t sx = (int32_t)(((int64_t)cal[0] * rx + (int64_t)cal[1] * ry + cal[2]) >> 16);
  int32_t sy = (int32_t)(((int64_t)cal[3] * rx + (int64_t)cal[4] * ry + cal[5]) >> 16);
  x = (int16_t)constrain(sx, 0, LCD_WIDTH - 1);
  y = (int16_t)constrain(sy, 0, LCD_HEIGHT - 1);
  return true;
}

bool isCalibrated() { return calibrated; }

void setCalibration(const int32_t c[6])
{
  memcpy(cal, c, sizeof(cal));
  calibrated = true;
}

const int32_t *calibration() { return cal; }

void runCalibration()
{
  static const int16_t px[3] = {40, LCD_WIDTH - 40, LCD_WIDTH / 2};
  static const int16_t py[3] = {40, LCD_HEIGHT / 2, LCD_HEIGHT - 40};

  for (;;) {
    int32_t rx[3], ry[3];
    bool ok = true;
    for (uint8_t i = 0; i < 3 && ok; i++) {
      Lcd::fillScreen(Color::Black);
      Lcd::drawTextCentered(LCD_WIDTH / 2, 120, "TOUCH CALIBRATION", 2, Color::White, Color::Black);
      Lcd::drawTextCentered(LCD_WIDTH / 2, 150, "Tap and hold the target", 2, Color::Grey, Color::Black);
      char step[12];
      snprintf(step, sizeof(step), "%d / 3", i + 1);
      Lcd::drawTextCentered(LCD_WIDTH / 2, 180, step, 2, Color::Grey, Color::Black);
      drawTarget(px[i], py[i], Color::Yellow);
      uint16_t x = 0, y = 0;
      ok = captureHeld(x, y);
      rx[i] = x;
      ry[i] = y;
    }

    double cx[3], cy[3];
    const int32_t sx[3] = {px[0], px[1], px[2]};
    const int32_t sy[3] = {py[0], py[1], py[2]};
    if (ok && solve(rx, ry, sx, cx) && solve(rx, ry, sy, cy)) {
      int32_t c[6];
      for (uint8_t i = 0; i < 3; i++) {
        c[i] = (int32_t)lround(cx[i] * 65536.0);
        c[i + 3] = (int32_t)lround(cy[i] * 65536.0);
      }
      setCalibration(c);
      break;
    }
    Lcd::fillScreen(Color::Black);
    Lcd::drawTextCentered(LCD_WIDTH / 2, 150, "Calibration failed, retrying", 2, Color::Red, Color::Black);
    diagDelay(1200);
  }

  // Result + tap test screen
  Lcd::fillScreen(Color::Black);
  Lcd::drawTextCentered(LCD_WIDTH / 2, 12, "Calibrated - tap to test", 2, Color::Green, Color::Black);
  Lcd::drawTextCentered(LCD_WIDTH / 2, 40, "To skip next boot, set in src/config.h:", 1, Color::Grey, Color::Black);
  char line[96];
  snprintf(line, sizeof(line), "#define TOUCH_CAL { %ld, %ld, %ld, %ld, %ld, %ld }", (long)cal[0], (long)cal[1],
           (long)cal[2], (long)cal[3], (long)cal[4], (long)cal[5]);
  Lcd::drawTextCentered(LCD_WIDTH / 2, 54, line, 1, Color::Yellow, Color::Black);

  const int16_t bx = LCD_WIDTH / 2 - 70, by = LCD_HEIGHT - 64, bw = 140, bh = 48;
  Lcd::fillRoundRect(bx, by, bw, bh, 10, rgb565(30, 110, 60));
  Lcd::drawTextCentered(LCD_WIDTH / 2, by + 16, "DONE", 2, Color::White, rgb565(30, 110, 60));

  for (;;) {
    int16_t x, y;
    if (read(x, y)) {
      if (x >= bx && x < bx + bw && y >= by && y < by + bh) {
        beep();
        while (isPressed()) diagDelay(5);
        break;
      }
      if (y > 70 && y < by - 6) Lcd::fillCircle(x, y, 3, Color::Cyan);
    }
    diagDelay(10);
  }
}

}  // namespace Touch
