# esp32-usb-bridge

Firmware that connects the BTT TFT35 deck to a PC, for the ESP32-S3
SuperMini (the real bridge) or a Raspberry Pi Pico W (a stand-in for testing):

```
PC (OpenDeck + plugin) ◀── USB: keyboard + media keys + serial ──▶ ESP32-S3 / Pico ◀── UART ──▶ BTT TFT35 V3.0
```

Two modes, switched automatically:

| Mode | When | What happens to key / knob input |
|---|---|---|
| **Keyboard** | no host app attached | Turned into USB keys: Main page → F13–F24, Media page → media keys, knob → volume / mute / play-pause |
| **Host** | a host app sends `HOST_HELLO` on the USB serial port at least once a second | Sent to the host as events; the host's display commands are forwarded to the TFT |

The host app is the OpenDeck device plugin in `opendeck-plugin/` (see its
README); `tools/host_test.py` is a simpler stand-in for testing.

## Hardware

Both boards have one USB port, and it is the deck: keyboard + media keys +
the serial port for the host app. The debug log therefore goes out on a
second UART, for an optional USB-serial adapter (115200 baud).

Data goes over the TFT35's ESP-8266 socket (USART1), power over its 5-pin
"RS232" header. **BTT's pin diagram draws the socket upside down**; the
correct layout is in the [top-level README](../README.md#1-wire-the-bridge-to-the-tft)
and [docs/HARDWARE.md](../docs/HARDWARE.md).

| TFT35 | ESP32-S3 SuperMini | Pico W |
|---|---|---|
| ESP socket TX (PA9) | RX (GPIO44) | GP1 (RX), pin 2 |
| ESP socket RX (PA10) | TX (GPIO43) | GP0 (TX), pin 1 |
| RS232 header GND | GND | GND, pin 3 |
| RS232 header 5V | 5V | VBUS, pin 40 |
| *debug log TX* | GPIO4 | GP4, pin 6 |

(The TFT firmware can use the RS232 header's TX/RX instead, with
`BRIDGE_USART 2` in `BTT_TFT_Touch/src/config.h`, up to 3.75 Mbaud.)

Pins, baud rates and USB names are in `src/config.h`; the board-specific code
is `src/board_esp32.cpp` and `src/board_pico.cpp`, behind `src/board.h`.

## Build / flash

```
pio run                              # builds the SuperMini target (default)
pio run -e esp32s3_supermini -t upload
pio run -e pico -t upload
```

- **ESP32-S3 SuperMini:** a new board (stock firmware on the built-in USB
  serial port) usually flashes directly. If not, hold BOOT while plugging in
  to enter the ROM download mode, and replug once after the upload (esptool
  can't reset out of a BOOT-button download mode over USB). After that the
  upload resets the board itself: a 1200-baud touch on the deck's port
  restarts it into download mode on a different COM port, PlatformIO waits
  for that port, and `tools/esp32s3_exit_download.py` restarts the new
  firmware afterwards.
- **Pico W:** the first time, hold BOOTSEL while plugging in and copy the
  `.uf2` (or let `pio run -t upload` find the drive). After that uploads use
  the 1200-baud reset too. On the Pico the deck shows up with the core's
  default USB name ("Pico W"), not "TFT35 Deck".

The Pico target tests the logic (protocol, TFT link, host link, key
mapping); the ESP32's own USB stack still needs a check on the real board.

## TFT firmware

The TFT firmware (`BTT_TFT_Touch`) speaks the frame protocol:

- it sends `HELLO`, key / knob events and its log as `LOG` frames
  (printed on the bridge's debug log as `[tft] ...`), and answers `PING`
- its status bar shows the link: ESP offline / No USB / USB / Host app
- when the bridge reports a host app attached (`STATUS` flag `HOST_APP`) it
  switches to a host-controlled 5x3 grid: each key shows the label and
  colours from `SET_KEY`, or an 80x80 RGB565 image from `KEY_IMAGE` /
  `KEY_IMAGE_RLE` (run-length encoded), drawn once complete; presses
  and the knob are only reported to the host. It returns to its own pages
  when the host detaches.

Older TFT builds that only print text log lines are still understood
(`src/tft_link.cpp`), without host-mode display.

## Protocol

`include/deck_protocol.h` defines the frames used on both links:

```
0xA5 | type | len (u16 LE) | payload | crc8
```

- TFT → host events: `KEY`, `DIAL`, `DIAL_BUTTON`, `HELLO`, `LOG`, `PONG`
- host ↔ bridge: `HOST_HELLO` (keepalive), `HOST_INFO`, `HOST_BYE`
- host/bridge → TFT: `PING`, `STATUS`, `BRIGHTNESS`, `SET_KEY`,
  `CLEAR_KEYS`, `KEY_IMAGE` (reserved)

## Testing without OpenDeck

```
pip install pyserial
python tools/host_test.py COM7
```

Use the deck's own serial port (the native "USB" connector, not the COM
port of the UART connector). The TFT switches to host mode and the tool
labels and colours its 15 keys; pressing a key recolours it, clicking the
knob sends a test image to key 0 (about 0.15 s), turning the knob
sets the brightness. Ctrl+C returns the TFT to its own pages and the bridge
to keyboard mode.

## Next steps

1. Faster images: the TFT <-> bridge UART runs at 3.75 Mbaud (USART1 on the
   TFT's ESP-8266 socket) and the plugin run-length encodes key images
   (typically 5-15x smaller), so a page of 15 keys takes well under a second.
   Next step if needed: 5 Mbaud.
