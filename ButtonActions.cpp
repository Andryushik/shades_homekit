#include "ButtonActions.h"
#include <Arduino.h>
#include <AccelStepper.h>
#include <arduino_homekit_server.h>
#include "Globals.h"
#include "pins.h"

// Access acceleration constant from main TU
extern const float ACCEL;

// Calculate percentage for any step position (inverse of shadesControl calculation)
int calculatePercentForStep(long stepPosition)
{
  if (state.maxSteps <= 0)
    return 0;

  // Same formula as getCurrentPosition() but for arbitrary step position
  long numer = 100L * ((long)state.maxSteps - stepPosition);
  int percent = (int)((numer + (state.maxSteps / 2)) / (long)state.maxSteps);

  if (percent < 0)
    percent = 0;
  if (percent > 100)
    percent = 100;

  return percent;
}

// External symbols from main TU and other modules
extern "C" homekit_characteristic_t currentPosition;
extern "C" homekit_characteristic_t targetPosition;
extern "C" homekit_characteristic_t positionState;

extern AccelStepper stepper;
extern ShadesState state;
extern int getCurrentPosition();
extern bool saveConfig();
extern void reset();

// Blink state (used for confirmation pulses)
static int ba_blinkStepsRemaining = 0;
static unsigned long ba_blinkIntervalMs = 0;
static unsigned long ba_blinkLastToggleMs = 0;
static bool ba_blinkLedState = false;

void BA_startBlink(int times, int ms)
{
  if (times <= 0 || ms == 0)
    return;
  ba_blinkStepsRemaining = times * 2;
  ba_blinkIntervalMs = ms;
  ba_blinkLastToggleMs = millis();
  ba_blinkLedState = true;
  digitalWrite(LED_PIN, HIGH);
  ba_blinkStepsRemaining--;
}

void BA_blinkUpdate()
{
  if (ba_blinkStepsRemaining <= 0)
    return;
  unsigned long now = millis();
  if ((now - ba_blinkLastToggleMs) >= ba_blinkIntervalMs)
  {
    ba_blinkLedState = !ba_blinkLedState;
    digitalWrite(LED_PIN, ba_blinkLedState ? HIGH : LOW);
    ba_blinkLastToggleMs = now;
    ba_blinkStepsRemaining--;
    if (ba_blinkStepsRemaining == 0)
    {
      digitalWrite(LED_PIN, LOW);
      ba_blinkLedState = false;
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

void BA_moveToPercent(int percent)
{
  int tp = percent;
  if (tp < 0)
    tp = 0;
  else if (tp > 100)
    tp = 100;
  if (tp != targetPosition.value.int_value)
  {
    targetPosition.value.int_value = tp;
    homekit_characteristic_notify(&targetPosition, targetPosition.value);
    if (tp == 100)
      state.lastMessage = F("Moving UP");
    else if (tp == 0)
      state.lastMessage = F("Moving DOWN");
    else
      state.lastMessage = String("Moving to ") + tp + "%";
  }
}

void BA_stopMotion()
{
  // SMOOTH STOP with deceleration - calculate stop position and let motor decelerate naturally
  long currentPos = stepper.currentPosition();
  float currentSpeed = stepper.speed();
  float absSpeed = abs(currentSpeed);

  if (absSpeed > 0.1f) // Only if moving
  {
    // Calculate deceleration distance: v² = 2*a*s → s = v²/(2*a)
    float decelDistance = (absSpeed * absSpeed) / (2.0f * ACCEL);

    // Set new target to stop with smooth deceleration
    long stopPos = currentPos + (long)(currentSpeed > 0 ? decelDistance : -decelDistance);
    stepper.moveTo(stopPos);

    // IMMEDIATELY update HomeKit targetPosition to STOP position to prevent shadesControl() from overwriting!
    int stopPercent = calculatePercentForStep(stopPos);
    if (targetPosition.value.int_value != stopPercent)
    {
      targetPosition.value.int_value = stopPercent;
      homekit_characteristic_notify(&targetPosition, targetPosition.value);
    }

    state.lastMessage = F("Stopping...");
  }
  else
  {
    // Already stopped or very slow - instant stop
    stepper.setCurrentPosition(currentPos);
    stepper.moveTo(currentPos);

    // Update targetPosition immediately for HomeKit
    int currentPercent = getCurrentPosition();
    if (targetPosition.value.int_value != currentPercent)
    {
      targetPosition.value.int_value = currentPercent;
      homekit_characteristic_notify(&targetPosition, targetPosition.value);
    }

    positionState.value.int_value = POS_STOPPED;
    homekit_characteristic_notify(&positionState, positionState.value);
    state.lastMessage = F("Stopped");
  }
}

void BA_calToggleJog(int8_t dir)
{
  if (state.calJogDir == dir)
  {
    DPRINTLN("BA: Calibration jog stop");
    state.calJogDir = 0;
    state.lastMessage = F("Stopped");
  }
  else
  {
    DPRINT("BA: Calibration jog start dir=");
    DPRINTLN(dir);
    state.calJogDir = dir;
    state.lastMessage = (dir < 0) ? F("Moving UP") : F("Moving DOWN");
  }
}

void BA_calStop()
{
  state.calJogDir = 0;
  state.lastMessage = F("Stopped");
}

void BA_exitCalibrationNoSave()
{
  state.calJogDir = 0;
  if (state.currentMode == CALIBRATE)
  {
    DPRINTLN("BA: Exit calibration (no save)");
    state.currentMode = NORMAL;
  }
}

void BA_calibrationSaveTop()
{
  state.calJogDir = 0;
  state.upStep = stepper.currentPosition();
  state.currentCalibrationStep = UP_KNOWN;
  DPRINT("Calibration: saved TOP raw position = ");
  DPRINTLN(state.upStep);
  DPRINTLN("Calibration: MAIN short press (save TOP)");
  state.lastMessage = String("Saved top position (step ") + state.upStep + ")";
  state.confirmBlinkActive = true;
  state.exitCalibrationAfterBlink = false;
  BA_startBlink(5, 80);
}

bool BA_calibrationSaveBottom()
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
  if (!saveConfig())
  {
    DPRINTLN("Warning: Failed to save config during calibration");
    state.lastMessage = F("Calibration save failed");
    return false;
  }
  DPRINTLN("Calibration: finished, rebased and saved");
  state.lastMessage = String("Calibration saved: travel ") + travel + " steps";
  BA_startBlink(5, 80);
  return true;
}

void BA_factoryReset()
{
  DPRINTLN("BA: Factory reset requested");
  reset();
  delay(300);
  ESP.restart();
}
