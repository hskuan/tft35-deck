#include "deck_link.h"

#include "bridge_uart.h"
#include "config.h"
#include "ui.h"

#define LINK_TIMEOUT_MS 2000

namespace {

deck_parser_t parser;
uint32_t lastRx = 0;
bool wasOnline = false;
uint8_t flags = 0;

void send(uint8_t type, const void *payload, uint16_t len)
{
  uint8_t frame[200];
  size_t n = deck_encode(frame, sizeof(frame), type, payload, len);
  if (n) Bridge.writeAll(frame, n);
}

void sendHello()
{
  static const char fw[] = "TFT35 Deck " __DATE__;
  uint8_t p[4 + sizeof(fw) - 1] = {DECK_PROTO_VERSION, DECK_COLS, DECK_ROWS, DECK_DIALS};
  memcpy(p + 4, fw, sizeof(fw) - 1);
  send(DECK_EV_HELLO, p, sizeof(p));
}

uint16_t u16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

void onFrame()
{
  const uint8_t *p = parser.payload;
  uint16_t len = parser.len;
  lastRx = millis();

  switch (parser.type) {
    case DECK_CMD_PING:
      send(DECK_EV_PONG, nullptr, 0);
      break;

    case DECK_CMD_STATUS:
      if (len >= 1 && p[0] != flags) {
        flags = p[0];
        Ui::onLinkStatus(true, flags);
      }
      break;

    case DECK_CMD_BRIGHTNESS:
      if (len >= 1) Ui::onBrightness(p[0]);
      break;

    case DECK_CMD_SET_KEY:
      if (len >= 5) Ui::onSetKey(p[0], u16(p + 1), u16(p + 3), (const char *)p + 5, len - 5);
      break;

    case DECK_CMD_CLEAR_KEYS:
      Ui::onClearKeys();
      break;

    case DECK_CMD_KEY_IMAGE:
      if (len >= 5) Ui::onKeyImage(p[0], u16(p + 1), p + 3, (len - 3) / 2);
      break;

    case DECK_CMD_KEY_IMAGE_RLE:
      if (len >= 6) Ui::onKeyImageRle(p[0], u16(p + 1), p + 3, len - 3);
      break;
  }
}

class LogPrint : public Print {
public:
  size_t write(uint8_t c) override
  {
    if (c == '\r') return 1;
    if (c != '\n') buf_[n_++] = (char)c;
    if (c == '\n' || n_ == sizeof(buf_)) flush();
    return 1;
  }
  void flush()
  {
    if (n_) send(DECK_EV_LOG, buf_, n_);
    n_ = 0;
  }

private:
  char buf_[120];
  uint8_t n_ = 0;
};

LogPrint logger;

}  // namespace

namespace Link {

void begin()
{
  Bridge.begin(BRIDGE_BAUD);
  sendHello();
}

void poll()
{
  int c;
  while ((c = Bridge.read()) >= 0) {
    if (deck_parse(&parser, (uint8_t)c)) onFrame();
  }
  Bridge.pump();

  bool on = online();
  if (on != wasOnline) {
    wasOnline = on;
    if (on) {
      sendHello();  // the ESP32 may have booted after us
    } else {
      flags = 0;
    }
    Ui::onLinkStatus(on, flags);
  }
}

bool online() { return lastRx && millis() - lastRx < LINK_TIMEOUT_MS; }
uint8_t status() { return online() ? flags : 0; }

void sendKey(uint8_t page, uint8_t key, uint8_t state)
{
  uint8_t p[3] = {page, key, state};
  send(DECK_EV_KEY, p, 3);
}

void sendDial(uint8_t page, int8_t ticks)
{
  uint8_t p[3] = {page, 0, (uint8_t)ticks};
  send(DECK_EV_DIAL, p, 3);
}

void sendDialButton(uint8_t page, uint8_t kind)
{
  uint8_t p[3] = {page, 0, kind};
  send(DECK_EV_DIAL_BUTTON, p, 3);
}

void requestRefresh() { sendHello(); }

Print &log() { return logger; }

}  // namespace Link
