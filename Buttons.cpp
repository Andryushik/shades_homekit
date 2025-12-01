#include <Arduino.h>
#include <EasyButton.h>
#include <AccelStepper.h>
#include <arduino_homekit_server.h>
#include "pins.h"
#include "Buttons.h"
#include "wifi.h"
#include "ButtonActions.h"

// HomeKit characteristics (defined in accessory.c)
extern "C" homekit_characteristic_t currentPosition;
extern "C" homekit_characteristic_t targetPosition;
extern "C" homekit_characteristic_t positionState;
extern "C" homekit_server_config_t config;

// Functions implemented elsewhere in the main TU
extern void reset();
extern void enableCalibrationMode();
extern bool saveConfig();

extern ShadesState state;
extern AccelStepper stepper;
int getCurrentPosition();

namespace Buttons
{
  // Debounced button objects (EasyButton)
  static EasyButton upButton(BUTTON_UP_PIN, 35, true, true);
  static EasyButton downButton(BUTTON_DOWN_PIN, 35, true, true);

  void startBlink(int times, int ms)
  {
    BA_startBlink(times, ms);
  }

  void blinkUpdate()
  {
    BA_blinkUpdate();
  }

  void init()
  {
    upButton.begin();
    downButton.begin();
  }

  bool isUpPressed() { return upButton.isPressed(); }
  bool isDownPressed() { return downButton.isPressed(); }

  void loop()
  {
    upButton.read();
    downButton.read();
    // Ignore button input for 10s after boot to avoid accidental triggers
    if (millis() - state.startupTime <= BUTTON_IGNORE_WINDOW_MS)
    {
      return;
    }

    // Read current/edge states once to avoid consuming events multiple times
    bool upIs = upButton.isPressed();
    bool downIs = downButton.isPressed();
    bool upWas = upButton.wasPressed();
    bool downWas = downButton.wasPressed();

    // Log simple presses for debugging
    if (upWas)
    {
      DPRINTLN("Button: UP pressed");
    }
    if (downWas)
    {
      DPRINTLN("Button: DOWN pressed");
    }

    bool bothPressed = upIs && downIs;
    bool bothShortPress = false;

    if (bothPressed && !state.lastBothPressed)
    {
      // Both buttons were just pressed (MAIN)
      state.bothPressStart = millis();
      DPRINTLN("Buttons: BOTH pressed (MAIN)");
    }

    // Long-press detection when both buttons held
    if (bothPressed)
    {
      uint32_t dur = millis() - state.bothPressStart;
      if (dur >= MAIN_LONG_PRESS_RESET_MS && !state.mainLong10Handled)
      {
        state.mainLong10Handled = true;
        state.mainLong5Handled = true; // suppress 5s action
        DPRINTLN("MAIN long press: FACTORY RESET (10s)");
        reset();
        delay(300);
        ESP.restart();
      }
      else if (dur >= MAIN_LONG_PRESS_CAL_MS && !state.mainLong5Handled && state.currentMode != CALIBRATE)
      {
        state.mainLong5Handled = true;
        DPRINTLN("MAIN long press: ENTER CALIBRATION (5s)");
        enableCalibrationMode();
      }
    }

    // Detect release: treat short MAIN press on release
    if (!bothPressed && state.lastBothPressed)
    {
      uint32_t dur = millis() - state.bothPressStart;
      if (dur < MAIN_LONG_PRESS_CAL_MS)
      {
        // Unified: MAIN short press handled only on release (NORMAL & CALIBRATE)
        bothShortPress = true;
      }
      // Re-arm long-press guards on release
      state.mainLong5Handled = false;
      state.mainLong10Handled = false;
    }
    state.lastBothPressed = bothPressed;

    // Pairing window removed; rely on concurrent both-pressed detection

    if (state.currentMode == CALIBRATE)
    {
      // Toggle-style jogging: a single UP or DOWN press starts/stops continuous motion
      if (upWas && !downWas)
      {
        // Toggle UP direction: if already moving UP, stop; else start UP
        if (state.calJogDir == -1)
        {
          state.calJogDir = 0; // stop
          DPRINTLN("CAL: jog UP (toggle stop)");
        }
        else
        {
          state.calJogDir = -1; // start up
          DPRINTLN("CAL: jog UP (toggle start)");
        }
      }
      else if (downWas && !upWas)
      {
        // Toggle DOWN direction: if already moving DOWN, stop; else start DOWN
        if (state.calJogDir == 1)
        {
          state.calJogDir = 0; // stop
          DPRINTLN("CAL: jog DOWN (toggle stop)");
        }
        else
        {
          state.calJogDir = 1; // start down
          DPRINTLN("CAL: jog DOWN (toggle start)");
        }
      }

      // Capture calibration points with MAIN short press; motion handled in main loop
      if (bothShortPress)
      {
        // Stop any jogging before saving
        state.calJogDir = 0;
        if (state.currentCalibrationStep == INIT)
        {
          calibrationSaveTop();
        }
        else if (state.currentCalibrationStep == UP_KNOWN)
        {
          calibrationSaveBottom();
        }
      }
    }
    else
    {
      // Normal mode: handle MAIN short press (stop) and single-button presets
      static int pendingPresetDir = 0; // -1 up, +1 down, 0 none
      static uint32_t pendingPresetExpire = 0;
      uint32_t now = millis();

      if (bothShortPress)
      {
        // MAIN short press: use unified smooth STOP logic (same as web)
        DPRINTLN("MAIN short press: STOP");
        BA_stopMotion();
        // cancel any pending preset
        pendingPresetDir = 0;
      }
      else if (bothPressed)
      {
        // Suppress single-button presets while both physically held
        // cancel any pending preset (user intent is MAIN)
        pendingPresetDir = 0;
      }
      else
      {
        // Deferred presets: allow a short window to cancel if MAIN follows
        // Commit pending preset when the defer window expires
        if (pendingPresetDir != 0 && (int32_t)(now - pendingPresetExpire) >= 0)
        {
          if (pendingPresetDir < 0)
          {
            if (targetPosition.value.int_value != 100)
            {
              targetPosition.value.int_value = 100;
              homekit_characteristic_notify(&targetPosition, targetPosition.value);
              state.lastMessage = F("Moving UP");
            }
          }
          else if (pendingPresetDir > 0)
          {
            if (targetPosition.value.int_value != 0)
            {
              targetPosition.value.int_value = 0;
              homekit_characteristic_notify(&targetPosition, targetPosition.value);
              state.lastMessage = F("Moving DOWN");
            }
          }
          pendingPresetDir = 0;
        }

        // Schedule a pending preset when a single-button press is detected
        if (upWas && pendingPresetDir == 0)
        {
          pendingPresetDir = -1;
          pendingPresetExpire = now + PRESET_DEFER_WINDOW_MS;
        }
        else if (downWas && pendingPresetDir == 0)
        {
          pendingPresetDir = +1;
          pendingPresetExpire = now + PRESET_DEFER_WINDOW_MS;
        }
      }
    }
  }

  void calibrationSaveTop()
  {
    BA_calibrationSaveTop();
  }

  bool calibrationSaveBottom()
  {
    return BA_calibrationSaveBottom();
  }
}
