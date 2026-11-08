#pragma once

#include "deck_event.h"

// Stand-alone mode (no host app attached): TFT events become USB keyboard and
// media keys, so the deck works as a macro pad with no PC software.
//
//   Main page   keys 0-9, 11, 12  -> F13 .. F24 (bind them in OBS, AutoHotkey, ...)
//   Media page  Prev, Play, Next, Stop, Vol -, Sound, Vol +  -> media keys
//   Knob        turn -> volume down / up, press -> mute (Main) / play-pause (Media)
//
// Navigation and Settings keys stay on the TFT and send nothing.
namespace HidActions {

void handle(const DeckEvent &ev);

}  // namespace HidActions
