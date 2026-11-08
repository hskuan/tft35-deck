// On-screen crash and stall reports.
//  - Hard faults draw the faulting PC/LR and fault status registers.
//  - A watchdog in the SysTick hook (priority 0, preempts every other IRQ)
//    draws a report if the main loop stops for STALL_MS. NVIC IABR shows which
//    interrupt, if any, was executing when the stall was caught.
#include <Arduino.h>

#include "config.h"
#include "diag.h"
#include "encoder.h"
#include "lcd.h"

#define STALL_MS 1500

extern "C" {
volatile uint32_t g_heartbeat = 0;
}

namespace {

const uint16_t BG = rgb565(120, 0, 0);

void line(int16_t y, const char *s) { Lcd::drawText(8, y, s, 2, Color::White, BG); }

void reportBox(const char *title)
{
  Lcd::fillRect(0, 180, LCD_WIDTH, LCD_HEIGHT - 180, BG);
  line(186, title);
}

}  // namespace

extern "C" void fault_report(uint32_t *frame)
{
  __disable_irq();
  reportBox("HARD FAULT");
  char buf[48];
  snprintf(buf, sizeof(buf), "PC %08lX  LR %08lX", (unsigned long)frame[6], (unsigned long)frame[5]);
  line(210, buf);
  snprintf(buf, sizeof(buf), "CFSR %08lX HFSR %08lX", (unsigned long)SCB->CFSR, (unsigned long)SCB->HFSR);
  line(234, buf);
  snprintf(buf, sizeof(buf), "BFAR %08lX MMAR %08lX", (unsigned long)SCB->BFAR, (unsigned long)SCB->MMFAR);
  line(258, buf);
  snprintf(buf, sizeof(buf), "ICSR %08lX", (unsigned long)SCB->ICSR);
  line(282, buf);
  for (;;) {
  }
}

#define FAULT_ENTRY(name)                          \
  extern "C" __attribute__((naked)) void name(void) \
  {                                                \
    __asm volatile("tst lr, #4      \n"            \
                   "ite eq          \n"            \
                   "mrseq r0, msp   \n"            \
                   "mrsne r0, psp   \n"            \
                   "b fault_report  \n");          \
  }
FAULT_ENTRY(HardFault_Handler)
FAULT_ENTRY(MemManage_Handler)
FAULT_ENTRY(BusFault_Handler)
FAULT_ENTRY(UsageFault_Handler)

// HAL/core error (replaces the core's silent infinite loop)
extern "C" void _Error_Handler(const char *file, int lineNo)
{
  __disable_irq();
  reportBox("HAL ERROR");
  char buf[48];
  const char *base = strrchr(file, '/');
  if (!base) base = strrchr(file, '\\');
  snprintf(buf, sizeof(buf), "%.32s:%d", base ? base + 1 : file, lineNo);
  line(210, buf);
  for (;;) {
  }
}

extern "C" void HAL_SYSTICK_Callback(void)
{
  static uint32_t lastBeat = 0, lastChange = 0;
  static bool reported = false;
  uint32_t now = HAL_GetTick();

  encoder_tick();

  if (g_heartbeat != lastBeat || g_heartbeat == 0) {
    lastBeat = g_heartbeat;
    lastChange = now;
    return;
  }
  if (reported || now - lastChange < STALL_MS) return;
  reported = true;

  reportBox("STALL");
  char buf[48];
  snprintf(buf, sizeof(buf), "IABR %08lX %08lX", (unsigned long)NVIC->IABR[0], (unsigned long)NVIC->IABR[1]);
  line(210, buf);
  snprintf(buf, sizeof(buf), "ISPR %08lX %08lX", (unsigned long)NVIC->ISPR[0], (unsigned long)NVIC->ISPR[1]);
  line(234, buf);
  snprintf(buf, sizeof(buf), "ICSR %08lX  t=%lus", (unsigned long)SCB->ICSR, (unsigned long)(now / 1000));
  line(258, buf);
}
