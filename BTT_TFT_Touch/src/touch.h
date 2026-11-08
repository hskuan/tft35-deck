#pragma once
#include <stdint.h>

namespace Touch {

void begin();

// PENIRQ is low while the panel is pressed.
bool isPressed();

// Filtered raw 12-bit ADC reading; false if the touch was too light or lifted mid-sample.
bool readRaw(uint16_t &rx, uint16_t &ry);

// Calibrated screen coordinates; false if not touched or not calibrated.
bool read(int16_t &x, int16_t &y);

// Why the last readRaw() failed, for diagnostics:
//   0 ok, 1 pen up, 2 pressure too low, 3 lifted while sampling, 4 too noisy
struct ReadStats {
  uint8_t error;
  uint16_t z;       // Z1 pressure reading
  uint16_t spread;  // max spread of the middle samples (raw ADC counts)
};
const ReadStats &lastRead();

bool isCalibrated();
// Affine coefficients in Q16: x = (a*rx + b*ry + c) >> 16, y = (d*rx + e*ry + f) >> 16
void setCalibration(const int32_t cal[6]);
const int32_t *calibration();

// Interactive 3-point calibration followed by a tap test screen. Blocks until done.
void runCalibration();

}  // namespace Touch
