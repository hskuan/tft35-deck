#pragma once

// Pins per board. The TFT side is the TFT35's 5-pin "RS232" header (3.3 V TTL):
//   TFT TX (PA2) -> TFT_UART_RX      TFT RX (PA3) <- TFT_UART_TX
//   GND - GND                        5V - 5V (the deck's USB powers both)
// The debug log goes out on a second UART for a USB-serial adapter
// (optional): the board's only USB port is the deck itself.
#if defined(ARDUINO_ARCH_RP2040)
// Raspberry Pi Pico / Pico W (test stand-in)
#define TFT_UART_TX   0  // GP0, UART0
#define TFT_UART_RX   1  // GP1
#define DEBUG_UART_TX 4  // GP4, UART1
#define DEBUG_UART_RX 5  // GP5
#else
// ESP32-S3 SuperMini: the TFT link uses the pins labelled TX / RX (UART1
// routed through the GPIO matrix). The ROM prints its boot log on GPIO43 at
// reset; the TFT ignores it (not a valid frame). Debug log on GPIO4/5.
#define TFT_UART_RX   44  // pin labelled RX
#define TFT_UART_TX   43  // pin labelled TX
#define DEBUG_UART_TX 4   // GPIO4, UART0
#define DEBUG_UART_RX 5   // GPIO5
#endif

#define TFT_BAUD   3750000  // must match BRIDGE_BAUD in the TFT firmware (2000000 / 1000000 for long cables)
#define DEBUG_BAUD 115200

// USB identity (ESP32-S3; the Pico keeps its core's defaults). The VID/PID
// default to Espressif's (0x303A); change them if you have your own.
#define DECK_USB_PRODUCT      "TFT35 Deck"
#define DECK_USB_MANUFACTURER "TFT35 Deck"
#define DECK_USB_SERIAL       "TFTDECK-0001"

// Timing
#define HOST_TIMEOUT_MS   3000  // host app considered gone without DECK_HOST_HELLO
#define TFT_TIMEOUT_MS    5000  // TFT considered offline without any data
#define TFT_PING_MS        500
#define STATUS_RESEND_MS  2000
#define KEY_TAP_MS          10  // HID key press length
