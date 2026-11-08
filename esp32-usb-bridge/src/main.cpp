// USB bridge: PC (OpenDeck) <-> USB <-> ESP32-S3 / Pico <-> UART <-> BTT TFT35 deck
//
//  - Host app attached (OpenDeck plugin / tools/host_test.py on the USB serial
//    port): TFT key and knob events go to the host, the host's display
//    commands go to the TFT.
//  - No host app: the deck works stand-alone as a USB keyboard / media keys.
//
// Debug log: Board::debug(), a spare UART (see config.h), 115200 baud.
#include <Arduino.h>

#include "board.h"
#include "config.h"
#include "deck_event.h"
#include "hid_actions.h"
#include "host_link.h"
#include "tft_link.h"

TftLink tft;
HostLink host;

const char *describe(const DeckEvent &ev, char *buf, size_t len)
{
  static const char *keyState[] = {"released", "pressed", "cancelled"};
  switch (ev.type) {
    case DECK_EV_KEY:
      snprintf(buf, len, "key %u %s (page %u)", ev.index, ev.value <= 2 ? keyState[ev.value] : "?", ev.page);
      break;
    case DECK_EV_DIAL:
      snprintf(buf, len, "dial %+d (page %u)", ev.value, ev.page);
      break;
    case DECK_EV_DIAL_BUTTON:
      snprintf(buf, len, "dial %s (page %u)", ev.value == DECK_DIAL_LONG ? "long press" : "click", ev.page);
      break;
    default:
      snprintf(buf, len, "event 0x%02X", ev.type);
  }
  return buf;
}

static uint8_t statusFlags()
{
  return (Board::usbMounted() ? DECK_STATUS_USB : 0) | (host.attached() ? DECK_STATUS_HOST_APP : 0) |
         (Board::capsLock() ? DECK_STATUS_CAPSLOCK : 0) | (Board::numLock() ? DECK_STATUS_NUMLOCK : 0);
}

void setup()
{
  Board::begin();
  Board::debug().println("\n[bridge] TFT35 deck USB bridge");

  tft.begin();
  host.begin(tft);
}

void loop()
{
  tft.poll();
  host.poll();

  DeckEvent ev;
  char text[48];
  while (tft.nextEvent(ev)) {
    bool toHost = host.attached();
    Board::debug().printf("[event] %s -> %s\n", describe(ev, text, sizeof(text)), toHost ? "host" : "keyboard");
    if (toHost) host.sendEvent(ev);
    else HidActions::handle(ev);
  }

  // Keep the TFT informed (ignored by TFT firmware without protocol support)
  static uint32_t lastPing = 0, lastStatus = 0;
  static uint8_t sentFlags = 0xFF;
  uint32_t now = millis();
  if (now - lastPing >= TFT_PING_MS) {
    lastPing = now;
    tft.sendFrame(DECK_CMD_PING, nullptr, 0);
  }
  uint8_t flags = statusFlags();
  if (flags != sentFlags || now - lastStatus >= STATUS_RESEND_MS) {
    if (flags != sentFlags) Board::debug().printf("[bridge] status 0x%02X\n", flags);
    sentFlags = flags;
    lastStatus = now;
    tft.sendFrame(DECK_CMD_STATUS, &flags, 1);
  }

  // Tell the host when the TFT comes and goes, or asks for all keys (HELLO)
  if (tft.takeHello() && host.attached()) host.sendInfo();
  static bool tftWasOnline = false;
  if (tft.online() != tftWasOnline) {
    tftWasOnline = tft.online();
    Board::debug().printf("[bridge] tft %s\n", tftWasOnline ? "online" : "offline");
    if (host.attached()) host.sendInfo();
  }
}
