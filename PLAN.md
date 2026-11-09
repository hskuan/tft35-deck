# Status and roadmap

Last updated 2026-11-09. System overview: [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## Working (verified on hardware)

- TFT35 boots from the BTT bootloader at 0x08008000, 120 MHz from the crystal,
  ILI9488 display, touch with stored calibration, knob + button.
- Local deck UI: Main / Media / Settings pages, toggles, scenes, counter,
  brightness, auto-dim, info and colour-test screens, crash/stall reports.
- Pico W bridge: USB keyboard mode (volume/media keys work), host mode.
- OpenDeck plugin installed and working in OpenDeck 2.14.2 on Windows: device
  registers as "TFT35 Deck" (5x3 + dial), key images shown, presses and the
  knob reach OpenDeck.
- RLE image compression, whole-image drawing, full refresh when the TFT
  (re)enters host mode, rounded image keys.
- Bridge link on USART1 (ESP-8266 socket, PA9/PA10) at **3.75 Mbaud** (Pico at
  125 MHz, see esp32-usb-bridge/CLAUDE.md); bridge power still from the RS232
  header. Revert with `BRIDGE_USART 2` / 2000000. While offline the status bar
  shows an rx byte count / UART error readout (yellow).
- ESP32-S3 SuperMini bridge works with the TFT (pins labelled TX/RX,
  GPIO43/44) and OpenDeck; keyboard + CDC (COM10); uploads without buttons
  (download mode appears as COM9). Deck registers in OpenDeck as fixed id
  `td-tft35deck`, so profiles survive a bridge or USB port change.
- "Deck beep" OpenDeck action (toggle, saved in OpenDeck's global settings) →
  `CMD_SOUND` (0x87) → TFT key beep on/off.

## Built but not yet verified on hardware

- Latest TFT build (rounded image corners, empty-key corner fix) — needs the
  user to flash via SD.

## Known limitations

- TFT firmware doesn't rename the update .bin to .CUR → remove the SD card after updating.
- Touch calibration is compiled in (`TOUCH_CAL`); runtime recalibration is lost on reboot (no persistent storage yet).
- Knob has no display; OpenDeck encoder images are ignored.
- Only JPEG key images (what OpenDeck sends); no animation.
- OpenDeck has no folders: use profiles + Starter Pack "Switch Profile".

## Ideas / next steps (none started)

1. Persist settings (touch calibration, brightness, beep) in the TFT's W25Qxx SPI flash or a spare internal flash sector.
2. Option: knob as native HID volume even in host mode (instant, no PC software).
3. Faster link: next step 5M (exact on the TFT with 8x oversampling and on the Pico at 125 MHz; ESP32-S3 UART max); DMA TX on the TFT.
4. Rename the update file to .CUR after a successful boot (like BTT's firmware).

## Rejected

- SPI between bridge and TFT: the only SPI on a header is SPI2 on EXP1, behind
  buffer ICs (likely one-way); gain over fast UART + RLE too small for the effort.
- Emulating an Elgato Stream Deck USB device: impersonates their VID/PID,
  fragile, JPEG decode on the STM32. OpenDeck + our own device plugin instead.
- Bitfocus Companion: viable alternative host; OpenDeck chosen for Stream Deck plugin compatibility.
