#include <Arduino.h>

#include "config.h"
#include "deck_link.h"
#include "diag.h"
#include "encoder.h"
#include "lcd.h"
#include "spin.h"
#include "touch.h"
#include "ui.h"

static void splash(LcdController lcd)
{
  const uint16_t bg = rgb565(10, 10, 14);
  Lcd::fillScreen(bg);
  Lcd::drawTextCentered(LCD_WIDTH / 2, 110, "TFT35 DECK", 5, Color::White, bg);
  if (lcd == LcdController::Unknown) {
    char line[40];
    snprintf(line, sizeof(line), "unknown LCD (id %06lX)", (unsigned long)Lcd::rawId());
    Lcd::drawTextCentered(LCD_WIDTH / 2, 170, line, 2, Color::Yellow, bg);
  }
}

void setup()
{
  pinMode(LCD_BACKLIGHT_PIN, OUTPUT);
  digitalWrite(LCD_BACKLIGHT_PIN, HIGH);

  LcdController lcd = Lcd::begin();
  splash(lcd);
  tone(BUZZER_PIN, 2000, 40);

  Link::begin();
  Print &log = Link::log();
  log.print("[boot] TFT35 deck, lcd ");
  log.print(Lcd::controllerName(lcd));
  log.print(", cpu ");
  log.print(SystemCoreClock / 1000000);
  log.println(" MHz");

  Touch::begin();
  Encoder::begin();
  spinMs(800);

  // Calibrate if no built-in calibration, or if the screen is held during boot
  if (!Touch::isCalibrated() || Touch::isPressed()) {
    Touch::runCalibration();
    const int32_t *c = Touch::calibration();
    log.print("[boot] #define TOUCH_CAL {");
    for (int i = 0; i < 6; i++) {
      log.print(' ');
      log.print(c[i]);
      log.print(i < 5 ? "," : " }\n");
    }
  }

  Ui::begin(log);
}

void loop()
{
  g_heartbeat++;
  Link::poll();
  Ui::loop();
}
