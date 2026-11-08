# BTT_TFT_Touch — TFT35 deck firmware (STM32F207VCT6)

Read `../docs/ARCHITECTURE.md` and `../docs/PROTOCOL.md` for the system view;
`../docs/HARDWARE.md` for pins, headers and flashing.

## Build / verify

```
pio run          # must succeed; output .pio/build/btt_tft35_v3/BIGTREE_TFT35_V3.0.90.x.bin
```

Flashing is SD-card only and done by the user (see HARDWARE.md). There is no
automated test; verify by building, and ask the user to check on the device.

## Toolchain constraints (don't "upgrade" casually)

- `platform = ststm32@12.1.1` (stm32duino core 1.9.0). Board `boards/btt_tft35_v3.json`
  reuses the **NUCLEO_F207ZG** variant; pins are addressed by name (PE6, PD12...).
- Linker script `ldscripts/btt_tft35_v3_0x8000.ld`: flash at 0x08008000/224K, RAM 96K.
- In this core, `PA2`-style macros are analog pin numbers for some pins; use
  `PinName` (`PA_2`) or registers.

## Module map (`src/`)

| File | Responsibility |
|---|---|
| `board_init.c` | `.preinit_array` hook: undo bootloader state (IRQs, SysTick, NVIC, peripheral resets), set VTOR = 0x08008000, re-enable IRQs |
| `clock.c` | `SystemClock_Config` override: register-level HSE 8 MHz → 120 MHz, fallbacks HSI PLL / HSI; `clock_source` |
| `diag_screen.cpp` / `diag.h` | On-screen HardFault and `_Error_Handler` reports; stall watchdog in `HAL_SYSTICK_Callback` (1 kHz, also calls `encoder_tick`); `g_heartbeat`, `diagDelay()` |
| `peripheral_pins_btt.c` | Overrides the variant's UART pin maps (Nucleo disables PA2/PA9/PB10) |
| `spin.h` | Busy-wait delays independent of SysTick/DWT |
| `lcd.*`, `font5x7.h` | FSMC init (BTT timings), ILI9488/ST7796S/NT35310 detection + BTT init sequence, drawing primitives, `roundInset`/`strokeRoundRect`, `drawImage` |
| `touch.*` | XPT2046 bit-bang, filtered raw reads (`ReadStats`), affine Q16 calibration, `runCalibration()` |
| `encoder.*` | Knob decode (BTT Gray-code table, 4 transitions/detent), click / long press |
| `bridge_uart.*` | Bridge UART register-level, `BRIDGE_USART` picks USART1 (PA9/PA10, RX DMA2 stream2 ch4, default) or USART2 (PA2/PA3, RX DMA1 stream5 ch4); circular RX (4 KB), TX ring buffer (1 KB) pumped from the loop; `writeAll` is all-or-nothing; 8x oversampling when 16x can't reach the baud; diagnostics `rxBytes()`/`errors()`/`rxPinHigh()` (shown in the status bar by `Ui::loop` while offline) |
| `deck_link.*` | Protocol: parse commands → `Ui::on*`, send events, `HELLO`, PONG, `LogPrint` (log lines as `EV_LOG` frames), online = frame within 2 s |
| `ui.*`, `icons.*` | Status bar, 5x3 grid, local pages `PAGES[]`, host mode (`hostKeys[]`, image assembly `imgBuf`), touch state machine `pollTouch()`, knob `pollDial()`, settings/info/colour-test screens |
| `config.h` | All user-tunable constants (LCD knobs, `TOUCH_CAL`, `BRIDGE_BAUD`, encoder, backlight, `UI_DEBUG`) |
| `../include/deck_protocol.h` | Protocol — must stay identical to `esp32-usb-bridge/include/deck_protocol.h` |

Main loop: `g_heartbeat++; Link::poll(); Ui::loop();`. Boot (`main.cpp`):
LCD → splash → `Link::begin` → touch/encoder → calibration only if no
`TOUCH_CAL` or the screen is held → `Ui::begin`.

## Invariants — breaking these caused real bugs

1. **Never block on serial.** No `HardwareSerial`, no waiting for TX space. Log via `Link::log()`.
2. **Time math:** always `(int32_t)(millis() - t) > X` with a fresh `millis()`.
   A stale `now` minus a newer timestamp wrapped and auto-dimmed the screen on
   every touch (made keys "unresponsive").
3. **Touch:** pick the key only after `SETTLE_POLLS` agreeing readings (first
   contact readings are wrong); detect release by `RELEASE_POLLS` polls without
   a valid reading, not by PENIRQ alone.
4. **Blocking screens** (calibration, Info, Colours) must use `diagDelay()` or
   the stall watchdog fires. They don't poll the link (acceptable).
5. **Early boot code** (before `setup()`) must not use `delay()`/`delayMicroseconds()`; use `spin.h`.
6. **LCD init must match BTT's** (no software reset; MADCTL 0x28; FSMC timings in `lcd.cpp`).
7. **Host images:** frames must arrive with contiguous offsets; draw only on completion; corners rounded via `Lcd::roundInset`.
8. **Never send raw text on the bridge UART** — the bridge would parse it as legacy log lines.
9. Entering host mode clears host keys and calls `Link::requestRefresh()`.

## Recipes

- **Change local pages/keys:** edit `PAGES[]` in `ui.cpp` (`K()` momentary, `T()` toggle). Keyboard-mode HID mapping lives in the bridge (`hid_actions.cpp`) and is keyed by page + key index — keep them in sync.
- **New display command:** add to both `deck_protocol.h` copies → `deck_link.cpp` `onFrame()` → a `Ui::on...` handler; mirror in `protocol.cjs` / `host_test.py`; bridge forwards 0x80–0xBF automatically.
- **Change link speed:** `BRIDGE_BAUD` here and `TFT_BAUD` in the bridge, together. Exact rates: USART1 (60 MHz) 2M/3.75M/5M/6M/7.5M; USART2 (30 MHz) 1M/2M/3.75M.
- **Debug touch:** set `UI_DEBUG 1` (status-bar readout: P pen, A active, K key, s settle, I ignore, E error, z pressure, n noise, F fails).
- **Recalibrate:** hold the screen while booting (or Settings → Touch), copy the shown `#define TOUCH_CAL {...}` into `config.h`.
