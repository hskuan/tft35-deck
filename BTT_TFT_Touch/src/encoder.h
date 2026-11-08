#pragma once
#include <stdint.h>

// Rotary encoder (knob + push button) on the TFT35 V3.0.
// Sampled every 1 ms from the SysTick hook, so steps are not lost while the
// main loop is busy redrawing.
namespace Encoder {

void begin();

// Detent steps since the last call (+ clockwise, - counter-clockwise).
int16_t takeSteps();
// Button released after a short press / held past the long-press time.
bool takeClick();
bool takeLongPress();

}  // namespace Encoder

extern "C" void encoder_tick(void);  // 1 kHz, called from HAL_SYSTICK_Callback
