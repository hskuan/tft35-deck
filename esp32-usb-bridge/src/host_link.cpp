#include "host_link.h"

#include "board.h"
#include "config.h"
#include "tft_link.h"

#define HostSerial Board::host()

void HostLink::begin(TftLink &tft)
{
  tft_ = &tft;
}

bool HostLink::attached() const { return attached_; }

void HostLink::poll()
{
  // Forward at most one display frame per call: writing it to the TFT UART
  // blocks for up to ~70 ms, and during a burst of key images the main loop
  // must keep running (TFT pings, TFT input) between frames.
  while (HostSerial.available()) {
    if (deck_parse(&parser_, (uint8_t)HostSerial.read()) && onFrame()) break;
  }

  if (attached_ && millis() - lastHello_ > HOST_TIMEOUT_MS) {
    attached_ = false;
    Board::debug().println("[host] timed out, back to keyboard mode");
  }
}

bool HostLink::onFrame()
{
  uint8_t type = parser_.type;

  if (type == DECK_HOST_HELLO) {
    lastHello_ = millis();
    if (!attached_) {
      attached_ = true;
      Board::debug().println("[host] attached");
      sendInfo();
    }
    return false;
  }

  if (type == DECK_HOST_BYE) {
    if (attached_) Board::debug().println("[host] detached");
    attached_ = false;
    return false;
  }

  // Display commands go straight to the TFT
  if (type >= 0x80 && type <= 0xBF) {
    tft_->sendFrame(type, parser_.payload, parser_.len);
    return true;
  }

  Board::debug().printf("[host] unknown frame 0x%02X (%u bytes)\n", type, parser_.len);
  return false;
}

void HostLink::sendInfo()
{
  uint8_t p[5 + 48];
  const char *fw = tft_->firmware();
  uint8_t n = (uint8_t)min<size_t>(strlen(fw), 48);
  p[0] = DECK_PROTO_VERSION;
  p[1] = (tft_->online() ? DECK_INFO_TFT_ONLINE : 0) | (tft_->framed() ? DECK_INFO_TFT_FRAMED : 0);
  p[2] = DECK_COLS;
  p[3] = DECK_ROWS;
  p[4] = DECK_DIALS;
  memcpy(p + 5, fw, n);
  sendFrame(DECK_HOST_INFO, p, 5 + n);
}

void HostLink::sendEvent(const DeckEvent &ev)
{
  uint8_t p[3] = {ev.page, ev.index, (uint8_t)ev.value};
  sendFrame(ev.type, p, sizeof(p));
}

void HostLink::sendFrame(uint8_t type, const void *payload, uint16_t len)
{
  uint8_t buf[64 + DECK_FRAME_OVERHEAD];
  size_t n = deck_encode(buf, sizeof(buf), type, payload, len);
  if (n && HostSerial.availableForWrite() >= (int)n) HostSerial.write(buf, n);  // never block on a stalled host
}
