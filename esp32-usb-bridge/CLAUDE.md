# esp32-usb-bridge — USB bridge firmware + OpenDeck plugin

Read `../docs/ARCHITECTURE.md` and `../docs/PROTOCOL.md` first; wiring and
flashing in `../docs/HARDWARE.md`. User-facing docs: `README.md`,
`opendeck-plugin/README.md`.

## Build / verify

```
pio run -e esp32s3_supermini          # default env, the real target (not yet tested on hardware)
pio run -e pico                       # Raspberry Pi Pico W stand-in (the user's test board, COM8)
pio run -e pico -t upload --upload-port COM8   # OpenDeck must be quit first (it holds COM8)
cd opendeck-plugin/com.tft35deck.bridge.sdPlugin && npm test   # protocol + image + RLE, no hardware
node test/mock-opendeck.cjs 40        # fake OpenDeck + real bridge/TFT, end to end
npm run deploy                        # copy plugin into %APPDATA%\opendeck\plugins (OpenDeck must be quit)
```

Quick probe of a running bridge: send `HOST_HELLO`, read `HOST_INFO` (see
`tools/host_test.py`; PlatformIO's Python `~/.platformio/penv/Scripts/python.exe` has pyserial).

## Firmware module map (`src/`)

| File | Responsibility |
|---|---|
| `board.h` | Board layer API: `begin`, `host()` CDC stream, `tft()` UART, `debug()`, `usbMounted`, `capsLock`, `keyTap`, `consumerTap`, `USAGE_*` |
| `board_esp32.cpp` | ESP32-S3: TinyUSB `USBHIDKeyboard` + `USBHIDConsumerControl` + `USBCDC`; Serial1 = TFT, Serial (UART0) = debug |
| `board_pico.cpp` | RP2040 (arduino-pico): `Keyboard` lib, `Serial` = USB CDC, Serial1 = TFT, Serial2 = debug |
| `config.h` | Pins per board, `TFT_BAUD` (3.75 M, must match TFT `BRIDGE_BAUD`), USB names, timeouts |
| `tft_link.*` | UART parser (frames + legacy text log), event queue, `online()`, `framed()`, `takeHello()` |
| `host_link.*` | CDC parser; `HOST_HELLO` attach/keepalive, forwards 0x80–0xBF to the TFT (**one frame per poll**), `sendEvent`, `sendInfo` |
| `hid_actions.*` | Keyboard mode: page/key → F13–F24 or media usage; knob → volume/mute/play |
| `main.cpp` | Loop: route events (host vs keyboard), PING every 500 ms, STATUS on change / every 2 s, `HOST_INFO` on TFT online change or TFT HELLO |
| `../include/deck_protocol.h` | Must stay identical to `BTT_TFT_Touch/include/deck_protocol.h` |

## Plugin map (`opendeck-plugin/com.tft35deck.bridge.sdPlugin/`)

| File | Responsibility |
|---|---|
| `manifest.json` | No actions; `DeviceNamespace "td"`; `CodePath plugin.cjs` |
| `plugin.cjs` | OpenDeck WebSocket: `registerPlugin`, `registerDevice` (id `td-<usb serial>`, 3x5 + 1 encoder, type 0), key/dial events, `setImage` → convert + cache, `setBrightness`, `refreshAll()` on `HOST_INFO` |
| `lib/deck.cjs` | Bridge discovery (USB VID 303A/2E8A + handshake; never probes other ports), keepalive, events, paced writer (control frames first, newest image per key, one image at a time with drain), RLE choice |
| `lib/image.cjs` | data URL → JPEG decode (jpeg-js) → 80x80 box filter → RGB565; `rleRuns` |
| `lib/protocol.cjs` | JS mirror of `deck_protocol.h` |
| `deploy.cjs`, `test/` | Install into OpenDeck; self-test; mock OpenDeck |

OpenDeck facts (v2.14.2, from its source): runs `node plugin.cjs -port -pluginUUID -registerEvent -info` from PATH
(Node ≥ 20) with cwd = plugin folder; device events carry `payload`; `setImage`/`setBrightness`
fields are top-level; key images are 144x144 JPEG data URLs; `setImage` with null
controller/position/image = clear all; encoder images are ignored (no display).
Plugin log: `%LOCALAPPDATA%\opendeck\logs\plugins\com.tft35deck.bridge.sdPlugin.log`.

## Invariants / pitfalls

1. **ESP32 USB:** board json sets `ARDUINO_USB_MODE=1`; we need TinyUSB → `build_unflags` it and define 0 (already in `platformio.ini`).
2. **Pico:** `lib_ldf_mode = chain+` is required (Keyboard lib includes tusb-hid).
   Keep `board_build.f_cpu = 125000000L`: any other F_CPU (the rpipicow default is 133 MHz)
   moves clk_peri to 48 MHz, capping the UART at 3 Mbaud — higher `TFT_BAUD` is silently
   clamped and the TFT sees framing/noise errors.
3. **Forward at most one display frame per `HostLink::poll()`** — a long image burst otherwise starves TFT pings and `HOST_INFO` flaps (endless refresh loop).
4. **Never block on the host:** `sendFrame` checks `availableForWrite()`.
5. The debug log is a spare UART — the board's only USB port is the deck.
6. **Plugin writes are paced** (drain per frame, keepalive between images) so `HOST_HELLO` is never > 3 s late.
7. Plugin deploy and firmware upload both need OpenDeck quit (it holds COM8 and locks the native serialport binary).
8. Key events: plugin maps `pressed` → keyDown, `released`/`cancelled` → keyUp; dial click → encoderDown + encoderUp.

## Recipes

- **Remap keyboard mode:** `MAIN_KEYS` / `MEDIA_KEYS` in `hid_actions.cpp` (indices = TFT `PAGES[]` positions).
- **New board:** implement `board.h` in a new `board_<x>.cpp` guarded by its arch macro, add pins in `config.h`, add an env.
- **Protocol change:** see `../docs/PROTOCOL.md` header note (4 places).
