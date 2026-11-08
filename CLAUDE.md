# Monorepo PlatformIO Project Guidelines

## Project Structure
- `/BTT_TFT_Touch`: PlatformIO project for a BigTreeTech TFT 3.5 V3.0 running on an STM32 [Target A]
- `/esp32-usb-bridge`: PlatformIO project for an ESP32-S3 SuperMini USB bridge [Target B], with a
  Raspberry Pi Pico W test target (`-e pico`) and the OpenDeck plugin (`opendeck-plugin/`, Node.js)
- `/docs`: system documentation (start here)
- `btt-tft-touch.code-workspace`: VS Code multi-root workspace; each project uses its own
  IntelliSense config (`<project>/.vscode`, regenerate with `pio project init --ide vscode`)

## Read first
- `docs/ARCHITECTURE.md` — what the system is, modes, data flow, design decisions
- `docs/PROTOCOL.md` — wire protocol shared by all components
- `docs/HARDWARE.md` — pins, headers, wiring, flashing, hardware findings
- `PLAN.md` — current status, unverified items, roadmap
- Each project's `CLAUDE.md` — code map, invariants and pitfalls for that project

## Build & Test Commands
- Device A: `cd BTT_TFT_Touch && pio run`
- Device B: `cd esp32-usb-bridge && pio run -e esp32s3_supermini && pio run -e pico`
- Plugin: `cd esp32-usb-bridge/opendeck-plugin/com.tft35deck.bridge.sdPlugin && npm test`

## Agent Guidelines
1. You are an autonomous developer. Work within your assigned subfolder.
2. Always run `pio run` to verify that your changes compile successfully before finishing a task.
3. Do not modify files outside your designated feature scope.
4. `deck_protocol.h` exists in both projects and must stay identical (plus its JS/Python
   mirrors) — a protocol change touches both projects; say so before making it.
5. The TFT can only be flashed by the user via SD card. The bridge and the OpenDeck plugin
   can be flashed/deployed from here only while OpenDeck is quit (it holds the serial port).
