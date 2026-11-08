#include "bridge_uart.h"
#include "config.h"

BridgeUart Bridge;

// Peripheral selection (BRIDGE_USART in config.h). Both options use GPIOA, AF7
// and DMA channel 4.
#if BRIDGE_USART == 1
#define BU_USART        USART1
#define BU_TX_PIN       9   // PA9
#define BU_RX_PIN       10  // PA10
#define BU_DMA_STREAM   DMA2_Stream2
#define BU_DMA_EN       RCC_AHB1ENR_DMA2EN
#define BU_CLEAR_FLAGS() \
  (DMA2->LIFCR = DMA_LIFCR_CTCIF2 | DMA_LIFCR_CHTIF2 | DMA_LIFCR_CTEIF2 | DMA_LIFCR_CDMEIF2 | DMA_LIFCR_CFEIF2)
#define BU_CLOCK_ON()   (RCC->APB2ENR |= RCC_APB2ENR_USART1EN, (void)RCC->APB2ENR)
#define BU_PCLK()       HAL_RCC_GetPCLK2Freq()
#elif BRIDGE_USART == 2
#define BU_USART        USART2
#define BU_TX_PIN       2   // PA2
#define BU_RX_PIN       3   // PA3
#define BU_DMA_STREAM   DMA1_Stream5
#define BU_DMA_EN       RCC_AHB1ENR_DMA1EN
#define BU_CLEAR_FLAGS() \
  (DMA1->HIFCR = DMA_HIFCR_CTCIF5 | DMA_HIFCR_CHTIF5 | DMA_HIFCR_CTEIF5 | DMA_HIFCR_CDMEIF5 | DMA_HIFCR_CFEIF5)
#define BU_CLOCK_ON()   (RCC->APB1ENR |= RCC_APB1ENR_USART2EN, (void)RCC->APB1ENR)
#define BU_PCLK()       HAL_RCC_GetPCLK1Freq()
#else
#error "BRIDGE_USART must be 1 or 2"
#endif

static void pinAf7(uint8_t pin, bool pullup)
{
  GPIOA->MODER = (GPIOA->MODER & ~(3UL << (pin * 2))) | (2UL << (pin * 2));
  GPIOA->OSPEEDR |= (2UL << (pin * 2));
  GPIOA->OTYPER &= ~(1UL << pin);
  GPIOA->PUPDR = (GPIOA->PUPDR & ~(3UL << (pin * 2))) | ((pullup ? 1UL : 0UL) << (pin * 2));
  volatile uint32_t &afr = GPIOA->AFR[pin >> 3];
  afr = (afr & ~(0xFUL << ((pin & 7) * 4))) | (7UL << ((pin & 7) * 4));
}

void BridgeUart::begin(uint32_t baud)
{
  RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | BU_DMA_EN;
  BU_CLOCK_ON();

  pinAf7(BU_TX_PIN, false);
  pinAf7(BU_RX_PIN, true);  // pulled up so an unconnected line idles high

  uint32_t pclk = BU_PCLK();
  uint32_t over8 = 0;
  BU_USART->CR1 = 0;
  BU_USART->CR2 = 0;
  BU_USART->CR3 = 0;
  if (baud > pclk / 16) {
    // 8x oversampling: BRR = mantissa << 4 | 3-bit fraction
    uint32_t div8 = (pclk + baud / 2) / baud;  // USARTDIV in 1/8 steps
    BU_USART->BRR = ((div8 >> 3) << 4) | (div8 & 7);
    over8 = USART_CR1_OVER8;
  } else {
    BU_USART->BRR = (pclk + baud / 2) / baud;  // 16x oversampling
  }

  // RX DMA: channel 4, circular, peripheral -> memory
  BU_DMA_STREAM->CR = 0;
  while (BU_DMA_STREAM->CR & DMA_SxCR_EN) {
  }
  BU_CLEAR_FLAGS();
  BU_DMA_STREAM->PAR = (uint32_t)&BU_USART->DR;
  BU_DMA_STREAM->M0AR = (uint32_t)rx_;
  BU_DMA_STREAM->NDTR = RX_SIZE;
  BU_DMA_STREAM->FCR = 0;  // direct mode
  BU_DMA_STREAM->CR = (4UL << 25) | DMA_SxCR_MINC | DMA_SxCR_CIRC | DMA_SxCR_PL_0;
  BU_DMA_STREAM->CR |= DMA_SxCR_EN;

  BU_USART->CR3 = USART_CR3_DMAR;
  BU_USART->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE | over8;
  rxTail_ = 0;
  ready_ = true;
}

void BridgeUart::pump()
{
  if (!ready_) return;
  uint32_t sr = BU_USART->SR;  // DMA reads DR, which clears these: latch them here
  if (sr & USART_SR_FE) errs_ |= DIAG_FE;
  if (sr & USART_SR_NE) errs_ |= DIAG_NE;
  if (sr & USART_SR_ORE) errs_ |= DIAG_ORE;
  while (txTail_ != txHead_ && (BU_USART->SR & USART_SR_TXE)) {
    BU_USART->DR = tx_[txTail_];
    txTail_ = (txTail_ + 1) % TX_SIZE;
  }
}

bool BridgeUart::writeAll(const uint8_t *data, size_t len)
{
  if (!ready_) return false;
  pump();
  size_t used = (txHead_ + TX_SIZE - txTail_) % TX_SIZE;
  if (len > TX_SIZE - 1 - used) return false;  // drop the whole frame
  for (size_t i = 0; i < len; i++) {
    tx_[txHead_] = data[i];
    txHead_ = (txHead_ + 1) % TX_SIZE;
  }
  pump();
  return true;
}

int BridgeUart::read()
{
  if (!ready_) return -1;
  uint16_t head = RX_SIZE - BU_DMA_STREAM->NDTR;
  if (head >= RX_SIZE) head = 0;
  if (rxTail_ == head) return -1;
  uint8_t b = rx_[rxTail_];
  rxTail_ = (rxTail_ + 1) % RX_SIZE;
  rxBytes_++;
  return b;
}

bool BridgeUart::rxPinHigh() const
{
  return GPIOA->IDR & (1UL << BU_RX_PIN);
}
