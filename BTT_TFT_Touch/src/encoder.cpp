#include "encoder.h"

#include <Arduino.h>

#include "config.h"

// Pins (BTT pin_TFT35_V3_0.h): A PA8, B PC9, button PC8, all pull-up inputs,
// active low. PC6 (LCD_ENC_EN) is driven low, as BTT does in touch mode.
//
// Decoding: Gray code sequence 0 -> 2 -> 3 -> 1, every transition counted
// (as BTT's LCD_Encoder.c), ENC_PULSES_PER_STEP transitions per detent.
// Unlike BTT, steps are emitted only on arriving at a detent state and the
// transition count is cleared there. A count that just resets every
// ENC_PULSES_PER_STEP transitions drifts out of phase with the detents after
// one missed transition (both pins changing within one 1 ms sample), and from
// then on the first notch after every direction change is lost.
// The detent state is the one the knob rests in (re-learned when idle).

namespace {

volatile bool enabled = false;
volatile int16_t steps = 0;
volatile bool clickEvent = false, longEvent = false;

int8_t diff = 0;          // transitions since the last detent (+ = clockwise)
uint8_t lastPos = 0;
uint8_t detentPos = 0;    // Gray state the knob rests in at a detent
uint16_t idleMs = 0;      // ms since the last position change

constexpr uint16_t ENC_IDLE_LEARN_MS = 250;  // still this long = resting at a detent

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

// With 4 transitions per detent every detent is the same Gray state; with 2,
// detents alternate between a state and its complement.
inline bool isDetent(uint8_t pos)
{
#if ENC_PULSES_PER_STEP >= 4
  return pos == detentPos;
#elif ENC_PULSES_PER_STEP == 2
  return pos == detentPos || pos == (detentPos ^ 3);
#else
  (void)pos;
  return true;
#endif
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

  delayMicroseconds(100);  // let the pull-ups charge the lines
  lastPos = detentPos = readPos();
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
    // else: skipped a state (missed sample), direction unknown; the detent
    // check below re-syncs.
    lastPos = pos;
    idleMs = 0;
    if (isDetent(pos)) {
      // round to the nearest whole detent, so a missed transition still counts
      int8_t half = ENC_PULSES_PER_STEP / 2;
      steps += (diff + (diff >= 0 ? half : -half)) / ENC_PULSES_PER_STEP;
      diff = 0;
    }
  } else if (idleMs < ENC_IDLE_LEARN_MS && ++idleMs == ENC_IDLE_LEARN_MS) {
    // knob at rest: this is a detent (corrects a wrong boot-time guess)
    if (!isDetent(pos)) detentPos = pos;
    diff = 0;
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
