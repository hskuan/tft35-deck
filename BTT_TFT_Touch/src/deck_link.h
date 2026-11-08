#pragma once

#include <Arduino.h>

#include "deck_protocol.h"

// Framed link to the ESP32 bridge over the bridge UART (bridge_uart.h, include/deck_protocol.h).
// Received commands are handed to the Ui (Ui::on...).
namespace Link {

void begin();
void poll();  // call every loop

bool online();     // heard from the ESP32 within the last 2 s
uint8_t status();  // last DECK_CMD_STATUS flags (0 while offline)

// Ask the host to (re)send all key images (DECK_EV_HELLO)
void requestRefresh();

void sendKey(uint8_t page, uint8_t key, uint8_t state);
void sendDial(uint8_t page, int8_t ticks);
void sendDialButton(uint8_t page, uint8_t kind);

// Line-buffered text log, sent as DECK_EV_LOG frames
Print &log();

}  // namespace Link
