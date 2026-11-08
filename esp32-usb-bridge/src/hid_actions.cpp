#include "hid_actions.h"

#include <Arduino.h>

#include "board.h"

// Arduino keyboard codes for F13-F24 (same values in both cores' Keyboard libraries)
#ifndef KEY_F13
#define KEY_F13 0xF0
#define KEY_F14 0xF1
#define KEY_F15 0xF2
#define KEY_F16 0xF3
#define KEY_F17 0xF4
#define KEY_F18 0xF5
#define KEY_F19 0xF6
#define KEY_F20 0xF7
#define KEY_F21 0xF8
#define KEY_F22 0xF9
#define KEY_F23 0xFA
#define KEY_F24 0xFB
#endif

namespace {

// Main page key index -> keyboard key (0 = none)
const uint8_t MAIN_KEYS[DECK_KEYS] = {
  KEY_F13, KEY_F14, KEY_F15, KEY_F16, KEY_F17,  // Mic, Camera, Record, Go Live, Snap
  KEY_F18, KEY_F19, KEY_F20, KEY_F21, KEY_F22,  // scenes Main, Game, Chat, BRB, Marker
  0,       KEY_F23, KEY_F24, 0,       0,        // Media (folder), Lock, Count, -, Setup
};

// Media page key index -> consumer usage (0 = none)
const uint16_t MEDIA_KEYS[DECK_KEYS] = {
  0,  // Back
  USAGE_SCAN_PREVIOUS,
  USAGE_PLAY_PAUSE,
  USAGE_SCAN_NEXT,
  USAGE_STOP,
  USAGE_VOLUME_DOWN,
  USAGE_MUTE,
  USAGE_VOLUME_UP,
  0, 0, 0, 0, 0, 0, 0,
};

}  // namespace

namespace HidActions {

void handle(const DeckEvent &ev)
{
  switch (ev.type) {
    case DECK_EV_KEY:
      if (ev.value != DECK_KEY_RELEASED || ev.index >= DECK_KEYS) return;  // act when the press counts
      if (ev.page == DECK_PAGE_MAIN && MAIN_KEYS[ev.index]) Board::keyTap(MAIN_KEYS[ev.index]);
      else if (ev.page == DECK_PAGE_MEDIA && MEDIA_KEYS[ev.index]) Board::consumerTap(MEDIA_KEYS[ev.index]);
      break;

    case DECK_EV_DIAL:
      if (ev.page == DECK_PAGE_SETTINGS) return;  // brightness, handled on the TFT
      for (int i = 0; i < abs(ev.value); i++)
        Board::consumerTap(ev.value > 0 ? USAGE_VOLUME_UP : USAGE_VOLUME_DOWN);
      break;

    case DECK_EV_DIAL_BUTTON:
      if (ev.value != DECK_DIAL_CLICK) return;
      if (ev.page == DECK_PAGE_MAIN) Board::consumerTap(USAGE_MUTE);
      else if (ev.page == DECK_PAGE_MEDIA) Board::consumerTap(USAGE_PLAY_PAUSE);
      break;
  }
}

}  // namespace HidActions
