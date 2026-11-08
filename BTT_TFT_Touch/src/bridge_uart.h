#pragma once

#include <Arduino.h>

// Bridge link UART, register-level: USART1 (ESP-8266 socket, PA9/PA10) or
// USART2 (5-pin header, PA2/PA3), selected by BRIDGE_USART in config.h.
//  - TX: ring buffer drained by pump(); never blocks. writeAll() queues a
//    whole frame or nothing, so a full buffer never splits a frame.
//  - RX: circular DMA (channel 4) into a ring buffer, so bytes are not lost
//    while the main loop is busy redrawing. No interrupts (the stm32duino
//    core already owns the USARTx_IRQHandlers).
// This replaces HardwareSerial, whose write() spins forever when its
// interrupt-driven TX stalls.
class BridgeUart {
public:
  void begin(uint32_t baud);
  void pump();  // call often (every loop)

  bool writeAll(const uint8_t *data, size_t len);
  int read();  // -1 if nothing received

  // Link diagnostics (shown in the status bar while offline)
  uint32_t rxBytes() const { return rxBytes_; }
  uint8_t errors() const { return errs_; }  // DIAG_* flags seen since boot
  bool rxPinHigh() const;
  static const uint8_t DIAG_FE = 1, DIAG_NE = 2, DIAG_ORE = 4;

private:
  uint32_t rxBytes_ = 0;
  uint8_t errs_ = 0;
  static const uint16_t TX_SIZE = 1024;
  static const uint16_t RX_SIZE = 4096;  // ~40 ms of data at 1 Mbaud
  uint8_t tx_[TX_SIZE];
  uint16_t txHead_ = 0, txTail_ = 0;
  uint8_t rx_[RX_SIZE];
  uint16_t rxTail_ = 0;
  bool ready_ = false;
};

extern BridgeUart Bridge;
