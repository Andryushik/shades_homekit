#ifndef BUTTONS_H
#define BUTTONS_H

// Buttons interface — encapsulated namespace (see Buttons.cpp)
#include "Globals.h"

namespace Buttons
{
  void init();
  void loop();
  bool isUpPressed();
  bool isDownPressed();
  void startBlink(int times, int ms);
  void blinkUpdate();
  void calibrationSaveTop();
  bool calibrationSaveBottom();
}

#endif
