#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Stall watchdog (diag_screen.cpp): the main loop bumps this every pass.
extern volatile uint32_t g_heartbeat;

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
#include <Arduino.h>
// delay() for intentional blocking waits (calibration, tap-to-exit screens):
// keeps the stall watchdog fed.
static inline void diagDelay(uint32_t ms)
{
  g_heartbeat++;
  delay(ms);
}
#endif
