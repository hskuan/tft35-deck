#pragma once
#include <stdint.h>

#include "deck_protocol.h"

// One input event from the TFT, whichever format it arrived in.
struct DeckEvent {
  uint8_t type;   // DECK_EV_KEY, DECK_EV_DIAL or DECK_EV_DIAL_BUTTON
  uint8_t page;   // DECK_PAGE_*
  uint8_t index;  // key 0..14, or dial 0
  int8_t value;   // key: DECK_KEY_*, dial: ticks, dial button: DECK_DIAL_*
};

const char *describe(const DeckEvent &ev, char *buf, size_t len);
