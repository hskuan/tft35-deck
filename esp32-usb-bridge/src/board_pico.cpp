// Raspberry Pi Pico / Pico W board layer (arduino-pico core), a stand-in for
// the ESP32-S3 while testing. The core makes USB a composite device by
// default: Serial is the USB CDC port and the Keyboard library adds HID.
#if defined(ARDUINO_ARCH_RP2040)

#include <Keyboard.h>
#include <tusb.h>

#include "board.h"
#include "config.h"

namespace {

volatile bool caps = false, num = false;

void onKeyboardLed(bool numlock, bool capslock, bool, bool, bool, void *)
{
  caps = capslock;
  num = numlock;
}

}  // namespace

namespace Board {

void begin()
{
  Serial2.setTX(DEBUG_UART_TX);
  Serial2.setRX(DEBUG_UART_RX);
  Serial2.begin(DEBUG_BAUD);

  Serial1.setTX(TFT_UART_TX);
  Serial1.setRX(TFT_UART_RX);
  Serial1.setFIFOSize(4096);
  Serial1.begin(TFT_BAUD);
  // Default pads are 4 mA / slow slew: too soft for multi-Mbaud over jumper wires
  gpio_set_drive_strength(TFT_UART_TX, GPIO_DRIVE_STRENGTH_12MA);
  gpio_set_slew_rate(TFT_UART_TX, GPIO_SLEW_RATE_FAST);

  Keyboard.onLED(onKeyboardLed);
  Keyboard.begin();
  Serial.begin(115200);  // USB CDC; the baud rate is ignored
}

Stream &host() { return Serial; }
Stream &tft() { return Serial1; }
Print &debug() { return Serial2; }

bool usbMounted() { return tud_mounted(); }
bool capsLock() { return caps; }
bool numLock() { return num; }

void keyTap(uint8_t key)
{
  Keyboard.press(key);
  delay(KEY_TAP_MS);
  Keyboard.releaseAll();
}

void consumerTap(uint16_t usage)
{
  Keyboard.consumerPress(usage);
  delay(KEY_TAP_MS);
  Keyboard.consumerRelease();
}

}  // namespace Board

#endif  // ARDUINO_ARCH_RP2040
