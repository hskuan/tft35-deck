#include "stm32f2xx.h"
#include "stm32f2xx_hal.h"

// System clock setup, replacing the NUCLEO_F207ZG variant's HAL version (whose
// PLL setup fails on this board). Register-level, with loop-count timeouts so
// it cannot hang. Tries, in order:
//   1. 8 MHz HSE crystal -> PLL 120 MHz   (what BTT's firmware uses)
//   2. 16 MHz HSI        -> PLL 120 MHz
//   3. 16 MHz HSI, no PLL
// clock_source: 0 HSE PLL, 1 HSI PLL, 2 HSI only (shown on the Info screen)
volatile uint8_t clock_source = 2;

static int wait_flag(volatile uint32_t *reg, uint32_t mask, uint32_t want, uint32_t loops)
{
  while (loops--) {
    if ((*reg & mask) == want) return 1;
  }
  return 0;
}

static void use_hsi(void)
{
  RCC->CFGR &= ~(RCC_CFGR_SW | RCC_CFGR_HPRE | RCC_CFGR_PPRE1 | RCC_CFGR_PPRE2);
  wait_flag(&RCC->CFGR, RCC_CFGR_SWS, RCC_CFGR_SWS_HSI, 100000);
}

static int start_pll(int hse)
{
  use_hsi();
  RCC->CR &= ~RCC_CR_PLLON;
  wait_flag(&RCC->CR, RCC_CR_PLLRDY, 0, 100000);

  if (hse) {
    RCC->CR &= ~RCC_CR_HSEBYP;
    RCC->CR |= RCC_CR_HSEON;
    if (!wait_flag(&RCC->CR, RCC_CR_HSERDY, RCC_CR_HSERDY, 500000)) {
      RCC->CR &= ~RCC_CR_HSEON;
      return 0;
    }
  }

  // VCO in = 1 MHz, VCO = 240 MHz, SYSCLK = 120 MHz (P=2), USB = 48 MHz (Q=5)
  uint32_t m = hse ? (HSE_VALUE / 1000000U) : (HSI_VALUE / 1000000U);
  RCC->PLLCFGR = (1UL << 29) | m | (240U << 6) | (0U << 16) | (5U << 24) | (hse ? RCC_PLLCFGR_PLLSRC_HSE : 0);
  RCC->CR |= RCC_CR_PLLON;
  if (!wait_flag(&RCC->CR, RCC_CR_PLLRDY, RCC_CR_PLLRDY, 500000)) {
    RCC->CR &= ~RCC_CR_PLLON;
    return 0;
  }

  FLASH->ACR = FLASH_ACR_ICEN | FLASH_ACR_DCEN | FLASH_ACR_PRFTEN | FLASH_ACR_LATENCY_3WS;
  if ((FLASH->ACR & FLASH_ACR_LATENCY) != FLASH_ACR_LATENCY_3WS) return 0;

  RCC->CFGR = (RCC->CFGR & ~(RCC_CFGR_HPRE | RCC_CFGR_PPRE1 | RCC_CFGR_PPRE2)) |
              RCC_CFGR_HPRE_DIV1 | RCC_CFGR_PPRE1_DIV4 | RCC_CFGR_PPRE2_DIV2;  // 120 / 30 / 60 MHz
  RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_PLL;
  if (!wait_flag(&RCC->CFGR, RCC_CFGR_SWS, RCC_CFGR_SWS_PLL, 100000)) {
    use_hsi();
    return 0;
  }
  return 1;
}

void SystemClock_Config(void)
{
  if (start_pll(1)) clock_source = 0;
  else if (start_pll(0)) clock_source = 1;
  else clock_source = 2;

  SystemCoreClockUpdate();
  HAL_InitTick(TICK_INT_PRIORITY);
}
