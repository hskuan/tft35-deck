#pragma once
#include <stdint.h>

enum class Icon : uint8_t {
  None,
  Mic,
  MicOff,
  Camera,
  CameraOff,
  Record,
  Live,
  Screenshot,
  Scene,
  Coffee,
  Marker,
  Folder,
  Lock,
  Gear,
  Back,
  Prev,
  Next,
  Play,
  Pause,
  Stop,
  VolDown,
  VolUp,
  Mute,
  Speaker,
  SunSmall,
  Sun,
  Bell,
  BellOff,
  Target,
  Palette,
  Info,
};

// Draw a ~40x40 icon centred on (cx, cy). bg is the key colour, used for cut-outs.
void drawIcon(Icon icon, int16_t cx, int16_t cy, uint16_t fg, uint16_t bg);
