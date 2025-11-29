#include <Arduino.h>
#include <EasyButton.h>
#include <AccelStepper.h>
#include <arduino_homekit_server.h>
#include "pins.h"
#include "Buttons.h"
#include "wifi.h"

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
  // Non-blocking LED blink state (used for confirmation pulses)
  static int blinkStepsRemaining = 0; // toggles remaining (on/off per pulse)
  static unsigned long blinkIntervalMs = 0;
  static unsigned long blinkLastToggleMs = 0;
  static bool blinkLedState = false;

  // Debounced button objects (EasyButton)
  static EasyButton upButton(BUTTON_UP_PIN, 35, true, true);
  static EasyButton downButton(BUTTON_DOWN_PIN, 35, true, true);

  void startBlink(int times, int ms)
  {
    if (times <= 0 || ms == 0)
      return;
    blinkStepsRemaining = times * 2; // ON then OFF per pulse
    blinkIntervalMs = ms;
    blinkLastToggleMs = millis();
    blinkLedState = true;
    digitalWrite(LED_PIN, HIGH);
    blinkStepsRemaining--; // consumed the initial ON
  }

  void blinkUpdate()
  {
    if (blinkStepsRemaining <= 0)
      return;
    unsigned long now = millis();
    if ((now - blinkLastToggleMs) >= blinkIntervalMs)
    {
      blinkLedState = !blinkLedState;
      digitalWrite(LED_PIN, blinkLedState ? HIGH : LOW);
      blinkLastToggleMs = now;
      blinkStepsRemaining--;
      if (blinkStepsRemaining == 0)
      {
        digitalWrite(LED_PIN, LOW);
        blinkLedState = false;
        if (state.confirmBlinkActive)
        {
          if (state.exitCalibrationAfterBlink)
          {
            state.currentMode = NORMAL;
            state.exitCalibrationAfterBlink = false;
          }
          state.confirmBlinkActive = false;
        }
      }
    }
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
    if (millis() - state.startupTime <= (10 * 1000))
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
      if (dur >= 10000 && !state.mainLong10Handled)
      {
        state.mainLong10Handled = true;
        state.mainLong5Handled = true; // suppress 5s action
        DPRINTLN("MAIN long press: FACTORY RESET (10s)");
        reset();
        delay(300);
        ESP.restart();
      }
      else if (dur >= 5000 && !state.mainLong5Handled && state.currentMode != CALIBRATE)
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
      if (dur < 5000)
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
        state.calJogDir = -1; // up
        DPRINTLN("CAL: jog UP (toggle start)");
      }
      else if (downWas && !upWas)
      {
        state.calJogDir = +1; // down
        DPRINTLN("CAL: jog DOWN (toggle start)");
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
      static const uint32_t PRESET_DEFER_MS = 150; // window to detect near-simultaneous MAIN
      static int pendingPresetDir = 0;             // -1 up, +1 down, 0 none
      static uint32_t pendingPresetExpire = 0;
      uint32_t now = millis();

      if (bothShortPress)
      {
        // MAIN short press: stop and set current position as target
        int newTarget = getCurrentPosition();
        DPRINT("MAIN short press: STOP at position %: ");
        DPRINTLN(newTarget);
        targetPosition.value.int_value = newTarget;
        homekit_characteristic_notify(&targetPosition, targetPosition.value);
        stepper.moveTo(stepper.currentPosition());
        positionState.value.int_value = POS_STOPPED;
        homekit_characteristic_notify(&positionState, positionState.value);
        state.lastMessage = F("Stopped");
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
          pendingPresetExpire = now + PRESET_DEFER_MS;
        }
        else if (downWas && pendingPresetDir == 0)
        {
          pendingPresetDir = +1;
          pendingPresetExpire = now + PRESET_DEFER_MS;
        }
      }
    }
  }

  void calibrationSaveTop()
  {
    // Record the raw step position at the top; rebase after bottom is saved
    state.calJogDir = 0;
    state.upStep = stepper.currentPosition();
    state.currentCalibrationStep = UP_KNOWN;
    DPRINT("Calibration: saved TOP raw position = ");
    DPRINTLN(state.upStep);
    DPRINTLN("Calibration: MAIN short press (save TOP)");
    state.lastMessage = String("Saved top position (step ") + state.upStep + ")";
    state.confirmBlinkActive = true;
    state.exitCalibrationAfterBlink = false;
    startBlink(5, 80);
  }

  bool calibrationSaveBottom()
  {
    state.calJogDir = 0;
    state.downStep = stepper.currentPosition();
    DPRINT("Calibration: saved BOTTOM raw position = ");
    DPRINTLN(state.downStep);
    DPRINTLN("Calibration: MAIN short press (save BOTTOM)");
    int travel = abs(state.downStep - state.upStep);
    DPRINT("Calibration: measured travel = ");
    DPRINTLN(travel);
    if (travel < MIN_TRAVEL)
    {
      DPRINTLN("Calibration: travel too small, aborting save");
      state.lastMessage = String("Travel too small: ") + travel + " < " + MIN_TRAVEL;
      return false;
    }
    state.maxSteps = travel;
    // Rebase positions so TOP == 0 and BOTTOM == maxSteps
    int rebasedCurrent = state.currentStep - state.upStep;
    stepper.setCurrentPosition(rebasedCurrent);
    state.currentStep = rebasedCurrent;
    targetPosition.value.int_value = 0;
    homekit_characteristic_notify(&targetPosition, targetPosition.value);
    currentPosition.value.int_value = 0;
    homekit_characteristic_notify(&currentPosition, currentPosition.value);
    state.confirmBlinkActive = true;
    state.exitCalibrationAfterBlink = true;
    stepper.setMaxSpeed(SPEED_MAX);
    stepper.setAcceleration(ACCEL);
    saveConfig();
    DPRINTLN("Calibration: finished, rebased and saved");
    state.lastMessage = String("Calibration saved: travel ") + travel + " steps";
    startBlink(5, 80);
    return true;
  }
}
