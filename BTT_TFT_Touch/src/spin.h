#pragma once
#include <stdint.h>

#include "stm32f2xx.h"

// Busy-wait delays that depend only on SystemCoreClock: no SysTick, no DWT.
// Used during bring-up so a dead timer cannot hang the boot. Roughly accurate
// (a volatile loop iteration is ~8 cycles with flash wait states).
static inline void spinUs(uint32_t us)
{
  uint32_t loops = (SystemCoreClock / 8000000U) * us;
  if (loops == 0) loops = 1;
  for (volatile uint32_t i = 0; i < loops; i++) {
  }
}

static inline void spinMs(uint32_t ms)
{
  while (ms--) spinUs(1000);
}
