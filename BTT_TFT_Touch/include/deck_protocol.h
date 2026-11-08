// TFT35 Deck wire protocol (version 1).
//
// Used on both links of the bridge:
//   TFT  <-- UART -->  ESP32  <-- USB CDC -->  host (OpenDeck plugin / tools)
//
// Frame:  0xA5 | type | len_lo | len_hi | payload[len] | crc8
//         crc8 (poly 0x07, init 0) covers type, len and payload.
//
// Routing on the ESP32:
//   - TFT events (0x01-0x3F) go to the host while a host app is attached,
//     otherwise the ESP32 acts on them itself (USB keyboard / media keys).
//   - Display commands (0x80-0xBF) from the host are forwarded to the TFT.
//   - 0x40-0x7F are between host and ESP32 only.
//
// The same file lives in both projects (BTT_TFT_Touch/include and
// esp32-usb-bridge/include): keep the two copies identical. The bridge also
// still accepts the text log of older TFT builds (see its tft_link.cpp).
#pragma once

#include <stddef.h>
#include <stdint.h>

#define DECK_SYNC           0xA5
#define DECK_PROTO_VERSION  1
#define DECK_MAX_PAYLOAD    1024
#define DECK_FRAME_OVERHEAD 5

// Grid as seen by the host (TFT layout: 5 x 3 keys, 1 dial)
#define DECK_COLS  5
#define DECK_ROWS  3
#define DECK_KEYS  (DECK_COLS * DECK_ROWS)
#define DECK_DIALS 1

// Page numbers in events
#define DECK_PAGE_MAIN     0
#define DECK_PAGE_MEDIA    1
#define DECK_PAGE_SETTINGS 2
#define DECK_PAGE_HOST     0xFF  // TFT showing host-controlled keys

enum {
  // ---- TFT -> ESP32 (-> host) events ----
  DECK_EV_HELLO       = 0x01,  // u8 proto, u8 cols, u8 rows, u8 dials, char fw[]
                               // sent at start-up and whenever the TFT enters host mode:
                               // the host should then (re)send all keys
  DECK_EV_KEY         = 0x02,  // u8 page, u8 key, u8 state (DECK_KEY_*)
  DECK_EV_DIAL        = 0x03,  // u8 page, u8 dial, i8 ticks (+ clockwise)
  DECK_EV_DIAL_BUTTON = 0x04,  // u8 page, u8 dial, u8 kind (DECK_DIAL_*)
  DECK_EV_LOG         = 0x05,  // char text[] (no terminator)
  DECK_EV_PONG        = 0x06,  // (empty), reply to DECK_CMD_PING

  // ---- host <-> ESP32 only ----
  DECK_HOST_HELLO     = 0x40,  // host -> ESP32: host app attached; repeat >= 1/s as keepalive
  DECK_HOST_INFO      = 0x41,  // ESP32 -> host: u8 proto, u8 flags (DECK_INFO_*), u8 cols, u8 rows, u8 dials, char fw[]
  DECK_HOST_BYE       = 0x42,  // host -> ESP32: host app detaching

  // ---- ESP32 / host -> TFT display commands ----
  DECK_CMD_PING       = 0x80,  // (empty) ESP32 sends every 500 ms; TFT answers DECK_EV_PONG
  DECK_CMD_STATUS     = 0x81,  // u8 flags (DECK_STATUS_*)
  DECK_CMD_BRIGHTNESS = 0x82,  // u8 percent
  DECK_CMD_SET_KEY    = 0x83,  // u8 key, u16 bg rgb565, u16 fg rgb565, char label[] (host mode)
  DECK_CMD_CLEAR_KEYS = 0x84,  // (empty) blank all host keys
  DECK_CMD_KEY_IMAGE  = 0x85,  // u8 key, u16 offset (pixels), rgb565 pixels[] (LE)
  DECK_CMD_KEY_IMAGE_RLE = 0x86,  // u8 key, u16 offset (pixels), runs[]: u8 count (1-255), u16 rgb565 (LE)
  DECK_CMD_SOUND      = 0x87,  // u8 level: 0 = key beep off, non-zero = on (reserved for volume)
};

// Host key images are DECK_KEY_IMG x DECK_KEY_IMG RGB565 pixels, row-major,
// sent as one or more KEY_IMAGE / KEY_IMAGE_RLE frames in increasing offset
// order starting at 0. The TFT shows the image once the last pixel arrives.
#define DECK_KEY_IMG 80

// DECK_EV_KEY states
#define DECK_KEY_RELEASED  0  // finger lifted on the key: the press counts
#define DECK_KEY_PRESSED   1
#define DECK_KEY_CANCELLED 2  // finger slid off: no action

// DECK_EV_DIAL_BUTTON kinds
#define DECK_DIAL_CLICK 1
#define DECK_DIAL_LONG  2

// DECK_HOST_INFO flags
#define DECK_INFO_TFT_ONLINE 0x01  // TFT sent something recently
#define DECK_INFO_TFT_FRAMED 0x02  // TFT speaks this protocol (can show host keys)

// DECK_CMD_STATUS flags
#define DECK_STATUS_USB      0x01  // ESP32 enumerated on a USB host
#define DECK_STATUS_HOST_APP 0x02  // host app attached: show host keys
#define DECK_STATUS_CAPSLOCK 0x04
#define DECK_STATUS_NUMLOCK  0x08

// ---------------------------------------------------------------------------
// Encoder / incremental parser (header-only, C and C++)
// ---------------------------------------------------------------------------

static inline uint8_t deck_crc8(uint8_t crc, uint8_t b)
{
  crc ^= b;
  for (uint8_t i = 0; i < 8; i++) crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x07) : (uint8_t)(crc << 1);
  return crc;
}

// Writes one frame to out; returns its size, or 0 if it does not fit.
static inline size_t deck_encode(uint8_t *out, size_t cap, uint8_t type, const void *payload, uint16_t len)
{
  if (len > DECK_MAX_PAYLOAD || cap < (size_t)len + DECK_FRAME_OVERHEAD) return 0;
  const uint8_t *p = (const uint8_t *)payload;
  uint8_t crc = 0;
  out[0] = DECK_SYNC;
  out[1] = type;
  out[2] = (uint8_t)(len & 0xFF);
  out[3] = (uint8_t)(len >> 8);
  for (int i = 1; i < 4; i++) crc = deck_crc8(crc, out[i]);
  for (uint16_t i = 0; i < len; i++) {
    out[4 + i] = p[i];
    crc = deck_crc8(crc, p[i]);
  }
  out[4 + len] = crc;
  return (size_t)len + DECK_FRAME_OVERHEAD;
}

typedef struct {
  uint8_t state;  // 0 = waiting for sync
  uint8_t type;
  uint16_t len;
  uint16_t pos;
  uint8_t crc;
  uint32_t errors;  // CRC / length errors seen
  uint8_t payload[DECK_MAX_PAYLOAD];
} deck_parser_t;

// Feed one byte. Returns 1 when a complete, valid frame is in p->type /
// p->payload / p->len (valid until the next call).
static inline int deck_parse(deck_parser_t *p, uint8_t b)
{
  switch (p->state) {
    case 0:  // sync
      if (b == DECK_SYNC) {
        p->state = 1;
        p->crc = 0;
      }
      return 0;
    case 1:  // type
      p->type = b;
      p->crc = deck_crc8(p->crc, b);
      p->state = 2;
      return 0;
    case 2:  // len lo
      p->len = b;
      p->crc = deck_crc8(p->crc, b);
      p->state = 3;
      return 0;
    case 3:  // len hi
      p->len |= (uint16_t)b << 8;
      p->crc = deck_crc8(p->crc, b);
      p->pos = 0;
      if (p->len > DECK_MAX_PAYLOAD) {
        p->errors++;
        p->state = 0;
      } else {
        p->state = p->len ? 4 : 5;
      }
      return 0;
    case 4:  // payload
      p->payload[p->pos++] = b;
      p->crc = deck_crc8(p->crc, b);
      if (p->pos >= p->len) p->state = 5;
      return 0;
    default:  // crc
      p->state = 0;
      if (b == p->crc) return 1;
      p->errors++;
      return 0;
  }
}
