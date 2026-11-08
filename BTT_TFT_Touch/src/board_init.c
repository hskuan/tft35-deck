#include "stm32f2xx.h"

// The BTT bootloader occupies 0x08000000-0x08007FFF and jumps to us at
// 0x08008000. It can leave state behind that the stm32duino core does not
// expect, so clean up before HAL_Init() runs:
//  - SystemInit() (stm32duino 1.9) hard-resets VTOR to FLASH_BASE: point it
//    back at our vector table.
//  - The bootloader may jump with interrupts globally disabled (PRIMASK) and
//    the core never re-enables them: SysTick, millis() and Serial would stall.
//  - SysTick / peripheral IRQs from the bootloader's SD access may still be
//    enabled or pending.
// .preinit_array runs after SystemInit() and before C++ constructors (premain).
#define APP_VECTOR_TABLE 0x08008000UL

static void reset_bootloader_state(void)
{
  __disable_irq();

  SysTick->CTRL = 0;
  SysTick->VAL = 0;
  SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk;

  for (uint32_t i = 0; i < sizeof(NVIC->ICER) / sizeof(NVIC->ICER[0]); i++) {
    NVIC->ICER[i] = 0xFFFFFFFFUL;
    NVIC->ICPR[i] = 0xFFFFFFFFUL;
  }

  // Return every peripheral to its reset state (clock tree was reset by SystemInit)
  RCC->AHB1RSTR = 0xFFFFFFFFUL; RCC->AHB1RSTR = 0;
  RCC->AHB2RSTR = 0xFFFFFFFFUL; RCC->AHB2RSTR = 0;
  RCC->AHB3RSTR = 0xFFFFFFFFUL; RCC->AHB3RSTR = 0;
  RCC->APB1RSTR = 0xFFFFFFFFUL; RCC->APB1RSTR = 0;
  RCC->APB2RSTR = 0xFFFFFFFFUL; RCC->APB2RSTR = 0;

  SCB->VTOR = APP_VECTOR_TABLE;
  __DSB();
  __ISB();

  __enable_irq();
}

__attribute__((section(".preinit_array"), used))
static void (*const preinit_reset_bootloader_state)(void) = reset_bootloader_state;
