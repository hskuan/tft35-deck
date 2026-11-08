#pragma once

#include <Arduino.h>

#include "deck_event.h"
#include "deck_protocol.h"

class TftLink;

// USB CDC link to the host app (the OpenDeck device plugin, or tools/host_test.py).
//
// The host attaches by sending DECK_HOST_HELLO and keeps sending it at least
// once a second. While attached, TFT input events are sent to the host
// instead of being turned into USB keyboard presses, and the host's display
// commands (0x80-0xBF) are forwarded to the TFT.
class HostLink {
public:
  void begin(TftLink &tft);
  void poll();  // call every loop

  bool attached() const;
  void sendEvent(const DeckEvent &ev);
  void sendInfo();

private:
  bool onFrame();  // true if a frame was forwarded to the TFT
  void sendFrame(uint8_t type, const void *payload, uint16_t len);

  TftLink *tft_ = nullptr;
  deck_parser_t parser_ = {};
  uint32_t lastHello_ = 0;
  bool attached_ = false;
};
