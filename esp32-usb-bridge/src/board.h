#pragma once

#include <Arduino.h>

// Board layer: everything that differs between the ESP32-S3 SuperMini and the
// Raspberry Pi Pico (W) test stand-in. The rest of the bridge only uses this.
//   board_esp32.cpp  ESP32-S3: TinyUSB via arduino-esp32 (USBHIDKeyboard, USBCDC)
//   board_pico.cpp   RP2040:   arduino-pico (Keyboard library, Serial = USB CDC)
namespace Board {

void begin();  // USB (keyboard + media keys + host serial), TFT UART, debug UART

Stream &host();   // USB CDC serial to the host app
Stream &tft();    // UART to the TFT35
Print &debug();   // debug log (a spare UART; the USB port is the deck)

bool usbMounted();  // enumerated by a PC
bool capsLock();    // keyboard LEDs reported by the PC
bool numLock();

void keyTap(uint8_t key);          // Arduino keyboard code, e.g. KEY_F13
void consumerTap(uint16_t usage);  // HID consumer usage, see USAGE_*

}  // namespace Board

// HID consumer page usages (media keys)
#define USAGE_SCAN_NEXT     0x00B5
#define USAGE_SCAN_PREVIOUS 0x00B6
#define USAGE_STOP          0x00B7
#define USAGE_PLAY_PAUSE    0x00CD
#define USAGE_MUTE          0x00E2
#define USAGE_VOLUME_UP     0x00E9
#define USAGE_VOLUME_DOWN   0x00EA
