#include "encoder.h"

#include <Arduino.h>

#include "config.h"

// Pins (BTT pin_TFT35_V3_0.h): A PA8, B PC9, button PC8, all pull-up inputs,
// active low. PC6 (LCD_ENC_EN) is driven low, as BTT does in touch mode.
//
// Decoding follows BTT's LCD_Encoder.c: Gray code sequence 0 -> 2 -> 3 -> 1,
// every transition counted, ENC_PULSES_PER_STEP transitions per detent.

namespace {

volatile bool enabled = false;
volatile int16_t steps = 0;
volatile bool clickEvent = false, longEvent = false;

int8_t diff = 0;
uint8_t lastPos = 0;

bool btnStable = false;   // debounced state, true = pressed
uint8_t btnCount = 0;     // ms the raw state has differed from btnStable
uint16_t btnHeldMs = 0;
bool longFired = false;

inline uint8_t readPos()
{
  uint8_t pos = 0;
  if (!(GPIOA->IDR & (1U << 8))) pos |= 1;  // A
  if (!(GPIOC->IDR & (1U << 9))) pos |= 2;  // B
  return pos;
}

inline bool readBtn() { return !(GPIOC->IDR & (1U << 8)); }

void inputPullup(GPIO_TypeDef *port, uint8_t pin)
{
  port->MODER &= ~(3UL << (pin * 2));
  port->PUPDR = (port->PUPDR & ~(3UL << (pin * 2))) | (1UL << (pin * 2));
}

}  // namespace

namespace Encoder {

void begin()
{
  RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOCEN;
  (void)RCC->AHB1ENR;
  inputPullup(GPIOA, 8);
  inputPullup(GPIOC, 9);
  inputPullup(GPIOC, 8);

  GPIOC->BSRR = 1UL << (6 + 16);  // LCD_ENC_EN low
  GPIOC->MODER = (GPIOC->MODER & ~(3UL << (6 * 2))) | (1UL << (6 * 2));

  lastPos = readPos();
  enabled = true;
}

int16_t takeSteps()
{
  __disable_irq();
  int16_t s = steps;
  steps = 0;
  __enable_irq();
  return ENC_REVERSE ? -s : s;
}

bool takeClick()
{
  bool c = clickEvent;
  clickEvent = false;
  return c;
}

bool takeLongPress()
{
  bool l = longEvent;
  longEvent = false;
  return l;
}

}  // namespace Encoder

extern "C" void encoder_tick(void)
{
  if (!enabled) return;

  // rotation
  uint8_t pos = readPos();
  if (pos != lastPos) {
    static const int8_t next[4] = {2, 0, 3, 1};  // clockwise successor of each state
    if (next[lastPos] == pos) diff++;
    else if (next[pos] == lastPos) diff--;
    lastPos = pos;
    if (diff >= ENC_PULSES_PER_STEP || diff <= -ENC_PULSES_PER_STEP) {
      steps += diff / ENC_PULSES_PER_STEP;
      diff = 0;
    }
  }

  // button: 20 ms debounce, click on release, long press while held
  bool raw = readBtn();
  if (raw != btnStable) {
    if (++btnCount >= 20) {
      btnStable = raw;
      btnCount = 0;
      if (!btnStable && !longFired) clickEvent = true;
      btnHeldMs = 0;
      longFired = false;
    }
  } else {
    btnCount = 0;
    if (btnStable && !longFired && ++btnHeldMs >= ENC_LONG_PRESS_MS) {
      longFired = true;
      longEvent = true;
    }
  }
}
