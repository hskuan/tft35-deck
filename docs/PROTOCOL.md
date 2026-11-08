# Deck wire protocol (version 1)

Source of truth: `deck_protocol.h`, which exists in **two identical copies**
(`BTT_TFT_Touch/include/` and `esp32-usb-bridge/include/`) and is mirrored in
JavaScript by `opendeck-plugin/.../lib/protocol.cjs` and in Python by
`tools/host_test.py`. Change all four together; check with
`diff BTT_TFT_Touch/include/deck_protocol.h esp32-usb-bridge/include/deck_protocol.h`.

## Framing (both links)

```
0xA5 | type (u8) | len (u16 LE) | payload[len] | crc8
```

- crc8: poly 0x07, init 0, no reflection, over type + len + payload
  (CRC-8/SMBUS; check value of "123456789" = 0xF4).
- Max payload 1024. Parsers resync on 0xA5 and drop frames with bad CRC.
- All multi-byte fields little-endian; colours are RGB565.

## Messages

**TFT → bridge (→ host)**

| Type | Name | Payload |
|---|---|---|
| 0x01 | `EV_HELLO` | u8 proto, u8 cols, u8 rows, u8 dials, char fw[] — at start-up and on entering host mode: "resend all keys" |
| 0x02 | `EV_KEY` | u8 page, u8 key, u8 state (0 released = press counts, 1 pressed, 2 cancelled) |
| 0x03 | `EV_DIAL` | u8 page, u8 dial, i8 ticks (+ clockwise) |
| 0x04 | `EV_DIAL_BUTTON` | u8 page, u8 dial, u8 kind (1 click, 2 long press) |
| 0x05 | `EV_LOG` | text (bridge prints it on its debug UART as `[tft] ...`) |
| 0x06 | `EV_PONG` | — (reply to PING) |

Page values: 0 Main, 1 Media, 2 Settings, 0xFF host mode.

**Host ⇄ bridge only**

| Type | Name | Payload |
|---|---|---|
| 0x40 | `HOST_HELLO` | — host → bridge; attach + keepalive (≥ 1/s; timeout 3 s) |
| 0x41 | `HOST_INFO` | u8 proto, u8 flags (bit0 TFT online, bit1 TFT speaks protocol), u8 cols, u8 rows, u8 dials, char fw[] — bridge → host on attach, TFT online change, TFT HELLO |
| 0x42 | `HOST_BYE` | — host → bridge; detach now |

**Bridge / host → TFT** (bridge forwards host frames 0x80–0xBF unchanged)

| Type | Name | Payload |
|---|---|---|
| 0x80 | `CMD_PING` | — every 500 ms from the bridge |
| 0x81 | `CMD_STATUS` | u8 flags: 0x01 USB enumerated, 0x02 host app attached (→ host mode), 0x04 Caps Lock, 0x08 Num Lock |
| 0x82 | `CMD_BRIGHTNESS` | u8 percent |
| 0x83 | `CMD_SET_KEY` | u8 key, u16 bg, u16 fg, char label[] ('\n' = second line) |
| 0x84 | `CMD_CLEAR_KEYS` | — blank all host keys |
| 0x85 | `CMD_KEY_IMAGE` | u8 key, u16 offset (pixels), RGB565 pixels[] |
| 0x86 | `CMD_KEY_IMAGE_RLE` | u8 key, u16 offset, runs[]: u8 count (1–255), u16 colour |
| 0x87 | `CMD_SOUND` | u8 level: 0 = key beep off, non-zero = on (values reserved for a future volume). The plugin sends it on every attach (from its global settings) and when its "Deck beep" action is pressed; the TFT shows a toast only when the setting changes |

## Key images

- 80x80 (`DECK_KEY_IMG`), row-major, drawn 2 px inside the 84 px key, corners
  rounded by the TFT.
- One image = one or more `KEY_IMAGE` / `KEY_IMAGE_RLE` frames, offsets
  contiguous from 0. The TFT assembles in a 12.8 KB RAM buffer and draws only
  when the last pixel arrives. A gap in offsets or a frame for another key
  discards the partial image (key keeps its old content).
- Plugin chunking: 400 raw pixels or 300 runs per frame. It uses RLE when
  smaller; RLE merges pixels within ±1 step (R/B, ±2 G) of the run start to
  absorb JPEG noise (`rleTolerance`, 0 = lossless).

## Sequences

**Host attach:** host `HOST_HELLO` → bridge `HOST_INFO` + `STATUS(HOST_APP)` →
TFT clears host keys, enters host mode, sends `HELLO` → bridge `HOST_INFO` →
plugin resends cached images + `rerenderImages`.

**Detach:** `HOST_BYE` or 3 s without `HOST_HELLO` → `STATUS` without HOST_APP
→ TFT returns to its local pages; bridge back to keyboard mode.

**Key press (host mode):** `EV_KEY pressed` when the touch settles on a key →
`EV_KEY released` (or `cancelled` if slid off) on lift → plugin sends
OpenDeck `keyDown` / `keyUp`.

## Legacy

The bridge still parses the plain-text log lines of older TFT builds
(`[touch] press ... key=N`, `[deck] page=... -> ...`, `[dial] ...`) in
`tft_link.cpp` `onLine()`. Current TFT firmware never sends raw text — doing
so would create duplicate events.
