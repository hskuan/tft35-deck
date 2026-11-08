# Architecture

A DIY Stream Deck: a BigTreeTech TFT35 V3.0 (3D-printer touch screen) is the
display, touch grid and knob; a USB microcontroller bridges it to a PC running
OpenDeck (open-source Stream Deck software).

```
 OpenDeck (PC) ⇄ WebSocket ⇄ plugin.cjs (Node) ⇄ USB CDC ⇄ bridge MCU ⇄ UART 3.75 Mbaud ⇄ TFT35 (STM32F207)
                                                    └── USB HID keyboard + media keys ──▶ PC
```

| Component | Folder | Runs on | Role |
|---|---|---|---|
| TFT firmware | `BTT_TFT_Touch/` | STM32F207VCT6 in the TFT35 | Draws the 5x3 key grid, reads touch + knob, local pages, host mode |
| Bridge firmware | `esp32-usb-bridge/src` | ESP32-S3 SuperMini (target) / Raspberry Pi Pico W (test stand-in) | USB device (HID + CDC) ⇄ UART to the TFT |
| OpenDeck plugin | `esp32-usb-bridge/opendeck-plugin/` | PC, Node.js ≥ 20, started by OpenDeck | Registers the deck as an OpenDeck device, converts key images |
| Test host tool | `esp32-usb-bridge/tools/host_test.py` | PC, Python + pyserial | Stand-in for the plugin |

## Two modes (switched automatically)

| Mode | When | Key / knob input | TFT shows |
|---|---|---|---|
| **Keyboard (local)** | no host app attached | Bridge turns events into USB keys: Main page → F13–F24, Media page → media keys, knob → volume / mute / play-pause (`hid_actions.cpp`) | Its own built-in pages (`ui.cpp` `PAGES[]`) |
| **Host** | plugin (or host_test.py) sends `HOST_HELLO` ≥ 1/s | Forwarded to the host as events; no local action | 15 host-controlled keys: label/colour (`SET_KEY`) or 80x80 image (`KEY_IMAGE[_RLE]`) |

The bridge tells the TFT which mode via `DECK_CMD_STATUS` flag `HOST_APP`.

## Responsibilities / design decisions

- **The TFT never decodes images.** The plugin decodes OpenDeck's 144x144 JPEG,
  box-filters to 80x80, converts to RGB565 and run-length encodes it.
- **The bridge is a dumb router** in host mode: display commands (0x80–0xBF) go
  to the TFT unchanged; TFT events go to the host. It only acts itself in
  keyboard mode.
- **Nothing blocks on a link.** Every serial writer drops data instead of
  waiting (TFT `bridge_uart.cpp`, bridge `host_link.cpp`); stalls caused real
  hangs during development.
- **The TFT owns all touch/knob semantics** (debounce, settle, cancel); events
  are already clean when they leave it.
- **One protocol for both links** (`deck_protocol.h`, two identical copies) —
  see [PROTOCOL.md](PROTOCOL.md).

## Timing (measured / designed)

| Item | Value |
|---|---|
| TFT ⇄ bridge UART | 3.75 Mbaud on USART1 (ESP-8266 socket, 16x oversampling); USART2 selectable via `BRIDGE_USART` |
| Raw 80x80 key image | 12.8 KB, ~35 ms |
| RLE key image (flat colour + text) | ~0.8–2 KB, a few ms |
| TFT ping / online timeout (bridge side) | 500 ms / 5 s |
| Host keepalive / timeout | 1 s / 3 s |
| Touch poll | 8 ms; key chosen after 3 agreeing readings; release after 4 misses |
| Encoder sampling | 1 kHz in the SysTick hook |

## Refresh model (why keys don't stay grey)

OpenDeck only sends images when a key changes. So: the TFT sends `HELLO` at
start-up and every time it enters host mode → the bridge sends `HOST_INFO` to
the host → the plugin re-sends its cached image for every key and asks
OpenDeck to `rerenderImages`.

## Further reading

- [PROTOCOL.md](PROTOCOL.md) — wire format, all messages, sequences
- [HARDWARE.md](HARDWARE.md) — TFT35 pins/headers, wiring, flashing, hardware findings
- `BTT_TFT_Touch/CLAUDE.md`, `esp32-usb-bridge/CLAUDE.md` — code maps, invariants, pitfalls
- [../PLAN.md](../PLAN.md) — status and roadmap
