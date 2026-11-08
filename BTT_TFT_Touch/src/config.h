#pragma once

// ---------------------------------------------------------------------------
// BTT TFT35 V3.0 hardware (from BIGTREETECH-TouchScreenFirmware pin_TFT35_V3_0.h)
// ---------------------------------------------------------------------------
#define LCD_BACKLIGHT_PIN PD12  // TIM4_CH1 PWM
#define BUZZER_PIN        PD13

// LCD on FSMC bank 1 / NE1, 16-bit, RS = A23 (PE2)
//   command: 0x60FFFFFE (A23 = 0)   data: 0x61000000 (A23 = 1)
#define LCD_WIDTH  480
#define LCD_HEIGHT 320

// Panel orientation / colour knobs, applied on top of BTT's own MADCTL values.
// If the picture is:
//   upside down            -> toggle LCD_ROTATE_180
//   mirrored left-right    -> toggle LCD_MIRROR_X
//   red and blue swapped   -> toggle LCD_SWAP_RB
//   colours look inverted  -> toggle LCD_INVERT
#ifndef LCD_ROTATE_180
#define LCD_ROTATE_180 0
#endif
#ifndef LCD_MIRROR_X
#define LCD_MIRROR_X 0
#endif
#ifndef LCD_SWAP_RB
#define LCD_SWAP_RB 0
#endif
#ifndef LCD_INVERT
#define LCD_INVERT 0
#endif

// XPT2046 touch, bit-banged: CS PE6, SCK PE5, MISO PE4, MOSI PE3, PEN PC13
//
// Touch calibration: by default the 3-point calibration runs on every boot.
// After calibrating, the screen shows six numbers; paste them here to skip it:
#define TOUCH_CAL { 29, 8576, -1677145, -6046, -7, 22994986 }
// Holding a finger on the screen while it boots forces recalibration.
// #define TOUCH_CAL { 0, 0, 0, 0, 0, 0 }

// UART link to the USB bridge (bridge_uart.cpp, deck_link.cpp):
//   1 = USART1 on the 8-pin ESP-8266 socket (PA9 TX / PA10 RX), APB2 60 MHz,
//       RX DMA2 stream 2 ch 4. Exact rates: 2M, 3.75M, 5M, 6M, 7.5M.
//   2 = USART2 on the 5-pin "TFT" header (PA2 TX / PA3 RX), APB1 30 MHz,
//       RX DMA1 stream 5 ch 4. Exact rates: 1M, 2M, 3.75M.
// The bridge's 5V/GND still come from the 5-pin header either way.
#define BRIDGE_USART  1
// Must match TFT_BAUD in the bridge firmware. 8x oversampling is used when
// 16x can't reach the rate. If a long or noisy cable drops frames, lower it
// on both sides.
#define BRIDGE_BAUD   3750000

// Rotary encoder (knob): A PA8, B PC9, button PC8, enable PC6
#ifndef ENC_REVERSE
#define ENC_REVERSE 0  // 1 if turning clockwise goes the wrong way
#endif
#define ENC_PULSES_PER_STEP 4    // transitions per detent (BTT default)
#define ENC_LONG_PRESS_MS   800  // hold to go home

// UI behaviour
#ifndef UI_DEBUG
#define UI_DEBUG 0  // 1: touch/loop readout in the status bar
#endif
#define BACKLIGHT_DEFAULT   100  // percent
#define BACKLIGHT_DIM        15  // percent after idle
#define DIM_AFTER_MS      60000
