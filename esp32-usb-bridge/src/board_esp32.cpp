// ESP32-S3 (SuperMini) board layer. Needs ARDUINO_USB_MODE=0 (TinyUSB on the
// native USB port), set in platformio.ini.
#if defined(ARDUINO_ARCH_ESP32)

#include <USB.h>
#include <USBHIDConsumerControl.h>
#include <USBHIDKeyboard.h>

#include "board.h"
#include "config.h"

namespace {

USBHIDKeyboard Keyboard;
USBHIDConsumerControl Consumer;
USBCDC HostSerial;

volatile bool mounted = false, caps = false, num = false;

void onUsbEvent(void *, esp_event_base_t base, int32_t id, void *)
{
  if (base != ARDUINO_USB_EVENTS) return;
  if (id == ARDUINO_USB_STARTED_EVENT || id == ARDUINO_USB_RESUME_EVENT) mounted = true;
  if (id == ARDUINO_USB_STOPPED_EVENT || id == ARDUINO_USB_SUSPEND_EVENT) mounted = false;
}

void onKeyboardLed(void *, esp_event_base_t, int32_t, void *data)
{
  auto *d = (arduino_usb_hid_keyboard_event_data_t *)data;
  caps = d->capslock;
  num = d->numlock;
}

}  // namespace

namespace Board {

void begin()
{
  // Serial is UART0; with ARDUINO_USB_CDC_ON_BOOT=0 it is not the USB port
  Serial.begin(DEBUG_BAUD, SERIAL_8N1, DEBUG_UART_RX, DEBUG_UART_TX);

  Serial1.setRxBufferSize(4096);
  Serial1.begin(TFT_BAUD, SERIAL_8N1, TFT_UART_RX, TFT_UART_TX);
  gpio_set_drive_capability((gpio_num_t)TFT_UART_TX, GPIO_DRIVE_CAP_3);  // strongest edges for multi-Mbaud

  Keyboard.onEvent(ARDUINO_USB_HID_KEYBOARD_LED_EVENT, onKeyboardLed);
  Keyboard.begin();
  Consumer.begin();
  HostSerial.begin();

  USB.onEvent(onUsbEvent);
  USB.productName(DECK_USB_PRODUCT);
  USB.manufacturerName(DECK_USB_MANUFACTURER);
  USB.serialNumber(DECK_USB_SERIAL);
  USB.begin();
}

Stream &host() { return HostSerial; }
Stream &tft() { return Serial1; }
Print &debug() { return Serial; }

bool usbMounted() { return mounted; }
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
  Consumer.press(usage);
  delay(KEY_TAP_MS);
  Consumer.release();
}

}  // namespace Board

#endif  // ARDUINO_ARCH_ESP32
