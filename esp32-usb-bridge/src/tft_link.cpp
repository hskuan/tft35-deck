#include "tft_link.h"

#include "board.h"
#include "config.h"

#define TftSerial Board::tft()

// The text log has no "released" line for a press that slid off a key, so a
// press without a matching action line is cancelled after this long.
#define HELD_KEY_TIMEOUT_MS 3000

static uint8_t pageFromName(const char *name)
{
  if (!strncmp(name, "Media", 5)) return DECK_PAGE_MEDIA;
  if (!strncmp(name, "Settings", 8)) return DECK_PAGE_SETTINGS;
  return DECK_PAGE_MAIN;
}

void TftLink::begin() {}  // the UART is opened by Board::begin()

void TftLink::poll()
{
  while (TftSerial.available()) onByte((uint8_t)TftSerial.read());

  if (heldKey_ >= 0 && millis() - heldSince_ > HELD_KEY_TIMEOUT_MS) {
    push(DECK_EV_KEY, page_, heldKey_, DECK_KEY_CANCELLED);
    heldKey_ = -1;
  }
}

bool TftLink::online() const { return seen_ && millis() - lastRx_ < TFT_TIMEOUT_MS; }

bool TftLink::takeHello()
{
  bool h = hello_;
  hello_ = false;
  return h;
}

bool TftLink::nextEvent(DeckEvent &ev)
{
  if (qTail_ == qHead_) return false;
  ev = queue_[qTail_];
  qTail_ = (qTail_ + 1) % QUEUE;
  return true;
}

void TftLink::push(uint8_t type, uint8_t page, uint8_t index, int8_t value)
{
  uint8_t next = (qHead_ + 1) % QUEUE;
  if (next == qTail_) return;  // full: drop
  queue_[qHead_] = {type, page, index, value};
  qHead_ = next;
}

void TftLink::sendFrame(uint8_t type, const void *payload, uint16_t len)
{
  uint8_t buf[DECK_MAX_PAYLOAD + DECK_FRAME_OVERHEAD];
  size_t n = deck_encode(buf, sizeof(buf), type, payload, len);
  if (n) TftSerial.write(buf, n);
}

void TftLink::onByte(uint8_t b)
{
  lastRx_ = millis();
  seen_ = true;

  // Bytes belong to a frame from its sync byte until the parser finishes it;
  // everything else is text. Text log lines are ASCII and never contain 0xA5.
  if (parser_.state != 0 || b == DECK_SYNC) {
    if (deck_parse(&parser_, b)) onFrame();
    return;
  }

  if (b == '\r') return;
  if (b == '\n') {
    line_[lineLen_] = '\0';
    if (lineLen_) onLine(line_);
    lineLen_ = 0;
    return;
  }
  if (lineLen_ < sizeof(line_) - 1) line_[lineLen_++] = (char)b;
}

void TftLink::onFrame()
{
  framed_ = true;
  const uint8_t *p = parser_.payload;
  uint16_t len = parser_.len;

  switch (parser_.type) {
    case DECK_EV_HELLO:
      hello_ = true;
      if (len >= 4) {
        uint16_t n = min<uint16_t>(len - 4, sizeof(fw_) - 1);
        memcpy(fw_, p + 4, n);
        fw_[n] = '\0';
        Board::debug().printf("[tft] hello: proto %u, %ux%u keys, %u dial(s), fw '%s'\n", p[0], p[1], p[2], p[3], fw_);
      }
      break;
    case DECK_EV_KEY:
    case DECK_EV_DIAL:
    case DECK_EV_DIAL_BUTTON:
      if (len >= 3) push(parser_.type, p[0], p[1], (int8_t)p[2]);
      break;
    case DECK_EV_LOG:
      Board::debug().print("[tft] ");
      Board::debug().write(p, len);
      Board::debug().println();
      break;
    case DECK_EV_PONG:
      break;
    default:
      Board::debug().printf("[tft] unknown frame 0x%02X (%u bytes)\n", parser_.type, len);
      break;
  }
}

// Translate the current TFT firmware's text log into events.
void TftLink::onLine(char *line)
{
  Board::debug().print("[tft] ");
  Board::debug().println(line);

  int x, y, key;
  char name[16], what[24];

  // Key chosen under the finger: press starts
  if (sscanf(line, "[touch] press x=%d y=%d key=%d", &x, &y, &key) == 3) {
    if (key < 0 || key >= DECK_KEYS) return;
    if (heldKey_ >= 0) push(DECK_EV_KEY, page_, heldKey_, DECK_KEY_CANCELLED);
    heldKey_ = key;
    heldSince_ = millis();
    push(DECK_EV_KEY, page_, key, DECK_KEY_PRESSED);
    return;
  }

  // Key action fired on release:  [deck] page=Main key=3 'Go Live' -> toggle on
  if (sscanf(line, "[deck] page=%15s key=%d", name, &key) == 2) {
    const char *arrow = strstr(line, "-> ");
    const char *quote = strchr(line, '\'');
    if (key < 0 || key >= DECK_KEYS) return;
    page_ = pageFromName(name);
    push(DECK_EV_KEY, page_, key, DECK_KEY_RELEASED);
    if (heldKey_ == key) heldKey_ = -1;

    // Track page changes so later presses carry the right page
    if (arrow && !strcmp(arrow + 3, "open page") && quote) {
      page_ = !strncmp(quote + 1, "Setup", 5) ? DECK_PAGE_SETTINGS : DECK_PAGE_MEDIA;
    } else if (arrow && !strcmp(arrow + 3, "back")) {
      page_ = DECK_PAGE_MAIN;
    }
    return;
  }

  // Knob:  [dial] volume +2 -> 52   /   [dial] mute | unmute | play | pause | back | home
  int delta, level;
  if (sscanf(line, "[dial] volume %d -> %d", &delta, &level) == 2) {
    int ticks = delta / 2;  // the TFT moves volume 2 % per detent
    if (ticks == 0) ticks = delta > 0 ? 1 : -1;
    push(DECK_EV_DIAL, page_, 0, (int8_t)constrain(ticks, -127, 127));
    return;
  }
  if (sscanf(line, "[dial] %23s", what) == 1) {
    if (!strcmp(what, "home")) {
      push(DECK_EV_DIAL_BUTTON, page_, 0, DECK_DIAL_LONG);
      page_ = DECK_PAGE_MAIN;
    } else {
      push(DECK_EV_DIAL_BUTTON, page_, 0, DECK_DIAL_CLICK);
      if (!strcmp(what, "back")) page_ = DECK_PAGE_MAIN;
    }
    return;
  }
}
