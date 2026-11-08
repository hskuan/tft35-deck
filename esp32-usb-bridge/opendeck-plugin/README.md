# OpenDeck plugin for the TFT35 Deck

A device plugin for [OpenDeck](https://github.com/nekename/OpenDeck) (tested
against v2.14.2) that makes the TFT35 Deck show up as a 5×3 key + 1 dial
device, like a Stream Deck:

```
OpenDeck ⇄ WebSocket ⇄ plugin.cjs ⇄ USB serial ⇄ bridge (ESP32-S3 / Pico) ⇄ UART ⇄ TFT35
```

- Key presses → `keyDown` / `keyUp`; knob → `encoderChange`, knob click →
  `encoderDown` + `encoderUp`.
- OpenDeck's key images (144×144 JPEG) are scaled to 80×80 RGB565 and shown on
  the TFT; `setBrightness` sets the TFT backlight.
- Action **"Deck beep"** (category "Hardware" in OpenDeck's action list): a
  toggle key that turns the TFT's key-press beep on or off. The setting is
  saved by OpenDeck and re-applied every time the deck connects.
- While the plugin is attached, the TFT shows OpenDeck's keys ("Host app" in
  its status bar). Quit OpenDeck and it returns to its own pages and the
  bridge to keyboard mode.

## Install

Needs Node.js 20 or newer on the PATH (OpenDeck runs the plugin with `node`).

```
cd com.tft35deck.bridge.sdPlugin
npm install
npm run deploy      # copies the plugin into OpenDeck's plugin folder
```

Restart OpenDeck. "TFT35 Deck" appears in its device list; drag actions onto
its keys and dial as for any Stream Deck.

The plugin finds the bridge automatically (USB vendor ID 303A for the ESP32-S3,
2E8A for the Pico, confirmed with a protocol handshake; other serial ports are
never touched). To pin a port, create `config.json` next to `plugin.cjs`:

```json
{ "port": "COM8", "rleTolerance": 1 }
```

Log: `%LOCALAPPDATA%\opendeck\logs\plugins\com.tft35deck.bridge.sdPlugin.log`
(Windows).

## Test without OpenDeck

```
npm test                          # protocol + image conversion, no hardware
node test/mock-opendeck.cjs 40    # fake OpenDeck + the real bridge and TFT
```

The mock starts a WebSocket server like OpenDeck's, launches the plugin the
same way, prints what the plugin sends, and answers with 15 numbered test
images. Close any other program using the bridge's serial port first
(OpenDeck itself, `tools/host_test.py`).

## Files

| File | |
|---|---|
| `manifest.json` | Plugin manifest: no actions, device namespace `td` |
| `plugin.cjs` | OpenDeck WebSocket side: registration, device events, `setImage` / `setBrightness` |
| `lib/deck.cjs` | Bridge discovery, keepalive, events, paced writes |
| `lib/protocol.cjs` | Frame encoder / parser (mirrors `include/deck_protocol.h`) |
| `lib/image.cjs` | JPEG data URL → 80×80 RGB565 |
| `deploy.cjs` | Copies the plugin into OpenDeck's plugin folder |

## Limits

- A raw key image is 12.8 KB (~35 ms over the bridge's 3.75 Mbaud UART). The
  plugin run-length encodes images whenever that is smaller; typical keys
  (flat colour + text) shrink to 5-15 %. Near-identical neighbouring pixels
  (JPEG noise) are merged; set `"rleTolerance": 0` in `config.json` for
  lossless encoding.
- The dial has no display, so OpenDeck's encoder images are ignored.
