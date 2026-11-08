# TFT35 Deck

Turn a **BigTreeTech TFT35 V3.0** 3D-printer touch screen into a DIY Stream Deck:
15 touch keys in a 5×3 grid, plus the screen's rotary knob, driven by
[OpenDeck](https://github.com/nekename/OpenDeck), the open-source Stream Deck
software. A small USB microcontroller connects the screen to your PC.

```
OpenDeck (PC) ⇄ plugin ⇄ USB ⇄ bridge (Pico W / ESP32-S3) ⇄ UART 3.75 Mbaud ⇄ TFT35 V3.0
```

It works in two modes and switches between them automatically:

| Mode | When | What the deck does |
|---|---|---|
| **OpenDeck** | OpenDeck is running | Shows OpenDeck's key icons; key presses and the knob go to OpenDeck |
| **Keyboard** | no OpenDeck | Its own built-in pages. **Main** sends F13–F24, **Media** sends play/pause, next and so on, and the knob controls volume. No PC software needed |

## What you need

- **BigTreeTech TFT35 V3.0** with an **STM32F207** chip. Some boards
  have a GD32F205 instead, which this firmware does not support. The chip is
  the large square one in the middle of the back of the board.
- **A bridge board:**
  - **Raspberry Pi Pico W**: tested and working.
  - **ESP32-S3 SuperMini**: the firmware builds, but it is untested on hardware.
- 4 jumper wires (female–female for the TFT's pin headers).
- A microSD card of 8 GB or less, formatted FAT32, to flash the TFT.
- On the PC:
  - [PlatformIO](https://platformio.org/install), either the VS Code extension or the `pio` command line
  - [Node.js](https://nodejs.org/) 20 or newer
  - [OpenDeck](https://github.com/nekename/OpenDeck). Tested with v2.14.2 on Windows.

## 1. Wire the bridge to the TFT

Data goes over the TFT's **8-pin ESP-8266 socket**. Power comes from its
**5-pin "RS232" header**.

> ⚠ **BTT's `TFT35-V3.0-PIN` diagram draws the ESP socket upside down.**
> Use the layout below. If you follow BTT's diagram, the RX wire lands on 3.3V.

ESP socket, board viewed from the component side, USB-A port on the left:

```
            left (USB side)   right
 top        TX                3.3V
            IO0               RST
            NC                NC
 bottom     GND               RX
```

| TFT35 | Pico W | ESP32-S3 SuperMini |
|---|---|---|
| ESP socket **TX** | GP1 (pin 2) | GPIO4 |
| ESP socket **RX** | GP0 (pin 1) | GPIO5 |
| RS232 header **GND** | GND (pin 3) | GND |
| RS232 header **+5V** | VBUS (pin 40) | 5V |

- Leave IO0, RST and 3.3V on the socket unconnected, and RST on the RS232 header too.
- To check the socket's orientation, look for the GND pin: it has continuity
  to the RS232 header's GND. 3.3V is diagonally opposite it.
- The bridge powers the TFT from the PC's USB port, so the TFT needs no other
  power supply.

More detail in [docs/HARDWARE.md](docs/HARDWARE.md).

## 2. Flash the TFT (via SD card)

```
cd BTT_TFT_Touch
pio run
```

1. Copy `.pio/build/btt_tft35_v3/BIGTREE_TFT35_V3.0.90.x.bin` to the SD card.
   Delete any older `BIGTREE_TFT35_*.bin` from the card first.
2. Insert the card and power the TFT. It shows "updating" and then starts the
   new firmware.
3. **Remove the card afterwards.** Otherwise the bootloader reflashes on every boot.

To go back to BTT's stock firmware, put BTT's official `.bin` on the card the
same way.

### Calibrate the touch screen

The touch calibration in [BTT_TFT_Touch/src/config.h](BTT_TFT_Touch/src/config.h)
(`TOUCH_CAL`) is for the author's screen, so yours will probably be a little off:

1. Hold a finger on the screen while the TFT boots. This starts calibration.
2. Tap the targets. The screen then shows a line like
   `#define TOUCH_CAL { ... }`.
3. Copy that line into `config.h` in place of the existing `TOUCH_CAL`,
   rebuild, and flash again.

Settings → Touch also recalibrates, but the result is lost on reboot until
you put it in `config.h`.

## 3. Flash the bridge

Plug the bridge into the PC, then:

```
cd esp32-usb-bridge
pio run -e pico -t upload                 # Raspberry Pi Pico W
pio run -e esp32s3_supermini -t upload    # ESP32-S3 SuperMini
```

- **First flash, Pico W:** hold **BOOTSEL** while plugging it in.
- **First flash, ESP32-S3:** hold **BOOT** while plugging it in.
- **Later flashes:** the upload resets the board itself, but **quit OpenDeck
  first**, because OpenDeck holds the bridge's serial port while it runs.

Once both are flashed, the TFT's status bar should change from "ESP offline"
to "USB". The deck now works as a USB keyboard on its own.

## 4. Install the OpenDeck plugin

Quit OpenDeck (tray icon → Quit), then:

```
cd esp32-usb-bridge/opendeck-plugin/com.tft35deck.bridge.sdPlugin
npm install
npm run deploy
```

Start OpenDeck. **"TFT35 Deck"** appears in its device list. Drag actions onto
its keys and dial as you would for a Stream Deck. The TFT's status bar shows
"Host app" while OpenDeck is connected. OpenDeck has no folders; use profiles
and the Starter Pack's "Switch Profile" action instead.

The plugin finds the bridge automatically by its USB ID. Options and the log
location are in the
[plugin README](esp32-usb-bridge/opendeck-plugin/README.md).

## Troubleshooting

| Symptom | Likely cause |
|---|---|
| Status bar says **ESP offline** with a yellow `rx0` next to it | No data reaches the TFT. Check the ESP socket orientation (see the warning above) and swap TX/RX. |
| Yellow `rx…` count rising, with `FE` / `NE` | Data arrives garbled. The baud rate doesn't match on both sides, or the wiring is poor. The Pico must stay at 125 MHz (see [esp32-usb-bridge/CLAUDE.md](esp32-usb-bridge/CLAUDE.md)). |
| TFT resets when you touch a wire | You have hit the socket's 3.3V pin. Stop, and recheck the orientation. |
| Upload fails on the 1200-baud reset | OpenDeck (or another program) has the serial port open. Quit it. |
| Touches land on the wrong key | Recalibrate (step 2). |
| "updating" every time the TFT boots | The firmware `.bin` is still on the SD card. Remove the card. |

To test without OpenDeck, use `python esp32-usb-bridge/tools/host_test.py <port>`.
It takes over the deck, colours and labels its keys, and prints presses and
knob turns.

## Project layout

| Folder | Contents |
|---|---|
| [BTT_TFT_Touch/](BTT_TFT_Touch/) | TFT35 firmware (STM32F207, PlatformIO + stm32duino) |
| [esp32-usb-bridge/](esp32-usb-bridge/) | Bridge firmware (Pico W / ESP32-S3) and `tools/host_test.py` |
| [esp32-usb-bridge/opendeck-plugin/](esp32-usb-bridge/opendeck-plugin/) | OpenDeck device plugin (Node.js) |
| [docs/](docs/) | [Architecture](docs/ARCHITECTURE.md), [wire protocol](docs/PROTOCOL.md), [hardware notes](docs/HARDWARE.md) |
| [PLAN.md](PLAN.md) | Status, known limitations, roadmap |

## Limitations

- The knob has no display, so OpenDeck's dial icons are not shown.
- Only static key images, no animation.
- Settings such as brightness and touch calibration are not saved across
  reboots yet, apart from the compiled-in `TOUCH_CAL`.
- Tested on Windows only. The plugin and bridge contain nothing
  Windows-specific, but they have not been tried on Linux or macOS.

## License

[MIT](LICENSE). This project is not affiliated with BigTreeTech, Elgato or
OpenDeck. Flashing it replaces the TFT's stock firmware, and BTT's official
`.bin` restores it.
