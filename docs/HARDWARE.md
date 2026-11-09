# Hardware reference

## BTT TFT35 V3.0

Verified on the user's board unless marked otherwise.

| Item | Detail |
|---|---|
| MCU | **STM32F207VCT6** (verified: STM32 GPIO registers work; GD32F205 variants exist and are NOT compatible) |
| Clock | 8 MHz crystal → PLL 120 MHz (HAL's HSI PLL setup fails on this board; see `clock.c`) |
| Memory | 256 KB flash, RAM limited to 96 KB (BTT's own setting) |
| Bootloader | BTT, 0x08000000–0x08007FFF; app at **0x08008000** (224 KB) |
| LCD | 480x320 **ILI9488** (detected id 0x009488), FSMC 16-bit NE1, RS = A23 (PE2); cmd 0x60FFFFFE, data 0x61000000; MADCTL 0x28 |
| Backlight | PD12 (TIM4_CH1 PWM) |
| Touch | XPT2046, bit-banged: CS PE6, SCK PE5, MISO PE4, MOSI PE3, PENIRQ PC13 |
| Knob | A PA8, B PC9, button PC8 (all pull-up, active low); PC6 "LCD_ENC_EN" driven low |
| Buzzer | PD13 |
| SD card | SPI1, CS PA4 (unused by our firmware except via the bootloader) |
| SPI flash | W25Qxx on SPI3, CS PB6 (unused) |

### Headers (from BTT's board photo `top.jpg` / `connect.png`)

| Header | Pins | MCU | Use |
|---|---|---|---|
| 5-pin bottom "TFT" | +5V GND RX TX RST | USART2 PA3/PA2 | bridge power (+5V/GND); data link if `BRIDGE_USART 2` |
| top | GND TX3 RX3 | USART3 PB10/PB11 | spare |
| top | GND RX4 TX4 | UART4 PA1/PA0 | spare |
| 8-pin ESP-8266 socket ("ESP", 2x4) | see below | USART1 TX=PA9, RX=PA10 (APB2 60 MHz) | **bridge data link** (`BRIDGE_USART 1`, default, verified at 3.75 Mbaud); has 3.3 V, not 5 V; R5/R7/R8 nearby may be series resistors |
| top right | 3.3V DIO GND CLK RST | SWD PA13/PA14 | ST-Link debug/recovery |
| EXP1/EXP2/EXP3 | 10-pin | SPI2 PB12–15 + encoder lines | 12864 emulation; lines pass through buffer ICs (U8, 20-pin + RES1/RES2) → likely input-only. **SPI to the bridge was evaluated and rejected.** |
| top | PA15 GND 3.3V / PA12(=PC12) 3.3V | filament / PS_ON | unused |

### ESP-8266 socket pinout (verified)

Board viewed component side up, USB-A port on the left, Power/UART headers at
the top (BTT's "TFT35 V3.0 overlay of ESP" picture; their `TFT35-V3.0-PIN`
diagram draws the WIFI table upside down — following it puts the RX wire on
3.3V, and grounding that pin resets the TFT):

```
            left (USB side)   right
 top        TX  (PA9)         3.3V
            IO0 (PD2?)        RST (PD3?)
            NC                NC
 bottom     GND               RX  (PA10)
```

Bridge TX → socket **RX** (PA10); bridge RX ← socket **TX** (PA9). Keep the
bridge's +5V/GND on the RS232 header (the socket has no 5 V). IO0/RST are the
ESP module's control lines (PD2/PD3 per the PIN diagram, unused); leave them
unconnected. Check: GND has continuity to the RS232 header's GND, 3.3V is
diagonally opposite it.

### Flashing the TFT

1. Build: `cd BTT_TFT_Touch && pio run` → `.pio/build/btt_tft35_v3/BIGTREE_TFT35_V3.0.90.x.bin`
   (name set by `scripts/btt_firmware_name.py`; the bootloader needs the
   `BIGTREE_TFT35_V3.0.*.bin` pattern).
2. Delete the old .bin from a FAT32 SD card (≤ 8 GB), copy the new one, reset.
   The screen shows "updating".
3. Remove the card afterwards: our firmware does not rename the file to .CUR,
   so the bootloader would reflash on every boot.
4. Back to stock: put BTT's official .bin on the card.

No agent can flash the TFT — it needs the user and the SD card.

## Bridge boards

| | ESP32-S3 SuperMini (target, `esp32s3_supermini`) | Pico W (test, `pico`) |
|---|---|---|
| TFT socket TX (PA9) → | RX (GPIO44) | GP1, pin 2 |
| TFT socket RX (PA10) ← | TX (GPIO43) | GP0, pin 1 |
| GND (RS232 header) | GND | GND, pin 3 (**required**) |
| TFT 5V (RS232 header) | 5V | VBUS, pin 40 |
| Debug log (115200) | GPIO4 | GP4, pin 6 |
| USB | single USB-C = the deck (HID + CDC) | micro-USB = the deck |
| First flash | usually direct; else hold BOOT while plugging in, replug after | hold BOOTSEL while plugging in |
| Later flashes | 1200-baud reset via the deck's CDC port | same |

The user's Pico is on **COM8** (USB VID:PID 2E8A:F00B). Only one program can
own that port: quit OpenDeck (tray → Quit) before `pio run -t upload` or
`tools/host_test.py`.
