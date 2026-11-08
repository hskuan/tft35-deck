#pragma once

#include <Arduino.h>

namespace Ui {

// log: where key presses and actions are reported (the future ESP32 link)
void begin(Print &log);
void loop();

void setBacklight(uint8_t percent);

// Link events (deck_link.cpp)
void onLinkStatus(bool online, uint8_t flags);
void onBrightness(uint8_t percent);
void onSetKey(uint8_t key, uint16_t bg, uint16_t fg, const char *label, uint16_t len);
void onClearKeys();
void onKeyImage(uint8_t key, uint16_t offset, const uint8_t *pixels, uint16_t count);
void onKeyImageRle(uint8_t key, uint16_t offset, const uint8_t *runs, uint16_t len);

}  // namespace Ui
