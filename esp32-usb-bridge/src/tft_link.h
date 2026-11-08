#pragma once

#include <Arduino.h>

#include "deck_event.h"
#include "deck_protocol.h"

// UART link to the TFT35.
//
// Accepts two input formats on the same line:
//  - protocol frames (deck_protocol.h), for TFT firmware with protocol support
//  - the current TFT firmware's text log lines, e.g.
//      [touch] press x=47 y=90 key=0
//      [deck] page=Main key=0 'Mic' -> toggle on
//      [dial] volume +2 -> 52
//    which are translated into the same events.
class TftLink {
public:
  void begin();
  void poll();  // call every loop

  bool nextEvent(DeckEvent &ev);  // queued input events, oldest first

  void sendFrame(uint8_t type, const void *payload, uint16_t len);

  bool online() const;  // received anything within TFT_TIMEOUT_MS
  bool takeHello();     // TFT sent DECK_EV_HELLO since the last call (wants all keys resent)
  bool framed() const { return framed_; }  // TFT speaks the protocol
  const char *firmware() const { return fw_; }

private:
  void onByte(uint8_t b);
  void onFrame();
  void onLine(char *line);
  void push(uint8_t type, uint8_t page, uint8_t index, int8_t value);

  deck_parser_t parser_ = {};
  char line_[160];
  uint16_t lineLen_ = 0;

  static const uint8_t QUEUE = 32;
  DeckEvent queue_[QUEUE];
  uint8_t qHead_ = 0, qTail_ = 0;

  uint32_t lastRx_ = 0;
  bool seen_ = false;
  bool framed_ = false;
  bool hello_ = false;
  char fw_[48] = "";

  // text-log translation state
  uint8_t page_ = DECK_PAGE_MAIN;  // page the TFT is showing (best guess)
  int8_t heldKey_ = -1;            // key pressed but not yet released
  uint32_t heldSince_ = 0;
};
