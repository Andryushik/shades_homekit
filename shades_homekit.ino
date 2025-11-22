#include <Arduino.h>
#include <arduino_homekit_server.h>
#include "ota.h"
#include "Helper.h"
#include "pins.h"
#include "wifi.h"
#include "Buttons.h"
#include <AccelStepper.h>
#include "Globals.h"
#include "web.h"

// Speed/settings constants
const float SPEED_MAX = 400.0f; // steps/s
const float ACCEL = 100.0f;     // steps/s^2
const float CAL_SPEED = 200.0f; // steps/s during calibration (continuous)
const int MIN_TRAVEL = 4096;    // hardcoded minimum calibration travel (steps)

// 28BYJ-48 via ULN2003 using HALF4WIRE; coil order IN1, IN3, IN2, IN4
AccelStepper stepper(AccelStepper::HALF4WIRE, IN1, IN3, IN2, IN4);

// Buttons are handled via the `Buttons` namespace (see Buttons.cpp)

Helper helper;

// Centralized runtime state (see `Globals.h` for field docs)
ShadesState state = {NORMAL, NONE, false, false,
                     0, 0, 0, 0,
                     0, false, false, 0,
                     false, false, 0, 0, String()};

// HomeKit characteristics (provided by accessory.c)
extern "C" homekit_characteristic_t currentPosition;
extern "C" homekit_characteristic_t targetPosition;
extern "C" homekit_characteristic_t positionState;
extern "C" homekit_server_config_t config;

// HomeKit getters/setters
homekit_value_t currentPositionGet() { return currentPosition.value; }
homekit_value_t targetPositionGet() { return targetPosition.value; }
homekit_value_t positionStateGet() { return positionState.value; }
void currentPositionSet(homekit_value_t value) { currentPosition.value = value; }
void targetPositionSet(homekit_value_t value) { targetPosition.value = value; }
void positionStateSet(homekit_value_t value) { positionState.value = value; }

static uint32_t nextLedMillis = 0;

int getCurrentPosition();
bool loadConfig();
bool saveConfig();
void enableCalibrationMode();
void properLedDisplay();
void handleEngineControllerActivity();
void homekitSetup();
void homekitLoop();
void shadesControl();

void setup()
{
  pinMode(LED_PIN, OUTPUT);
  // BUTTON_MAIN (D0) not used; MAIN is simulated by both UP+DOWN pressed
  pinMode(BUTTON_UP_PIN, INPUT_PULLUP);
  pinMode(BUTTON_DOWN_PIN, INPUT_PULLUP);

  Serial.begin(115200);
  SERIAL_DEBUG_INIT();
  state.startupTime = millis();

  loadConfig();
  if (state.maxSteps == 0)
  {
    enableCalibrationMode();
  }

  // Initialize stepper with normal motion profile
  stepper.setMaxSpeed(SPEED_MAX);
  stepper.setAcceleration(ACCEL);
  stepper.setCurrentPosition(state.currentStep);

  wifiConnect();
  homekitSetup();
  OTA::setup();
  // initialize buttons
  Buttons::init();

  // start web UI
  webBegin();
}

void loop()
{
  static bool wasCalibrating = false;
  // Buttons::loop() processes input/events

  Buttons::loop();
  properLedDisplay();
  handleEngineControllerActivity();
  webLoop();
  // Calibration: use continuous runSpeed() at constant CAL_SPEED
  if (state.currentMode == CALIBRATE)
  {
    wasCalibrating = true;

    bool up = (state.calJogDir < 0);
    bool down = (state.calJogDir > 0);
    // Wait for initial release to avoid motion from buttons held during entry
    if (state.calRequireRelease)
    {
      if (!up && !down)
      {
        state.calRequireRelease = false;
      }
    }

    if (!state.calRequireRelease)
    {
      // Continuous speed control: call setSpeed() then runSpeed() repeatedly
      if (up && !down)
      {
        // negative speed moves toward top (smaller step numbers)
        stepper.setSpeed(-CAL_SPEED);
        stepper.runSpeed();
        state.lastMovementTime = millis();
      }
      else if (down && !up)
      {
        stepper.setSpeed(CAL_SPEED);
        stepper.runSpeed();
        state.lastMovementTime = millis();
      }
      else
      {
        // no buttons pressed during calibration: explicitly clear speed to avoid
        // leaving a stale speed value. We don't call runSpeed() so no stepping.
        stepper.setSpeed(0.0f);
      }
    }
  }
  else
  {
    // Restoring normal motion profile after leaving calibration
    if (wasCalibrating)
    {
      stepper.setAcceleration(ACCEL);
      stepper.setMaxSpeed(SPEED_MAX);
      wasCalibrating = false;
      state.calJogDir = 0;
    }

    if (stepper.distanceToGo() != 0)
    {
      state.lastMovementTime = millis();
    }
    stepper.run();
  }

  // Keep software step counter in sync with the stepper driver
  state.currentStep = stepper.currentPosition();

  homekitLoop();
  shadesControl();
  OTA::loop();

  // Small delay for smooth stepping and watchdog stability
  delay(1);
}

void properLedDisplay()
{
  Buttons::blinkUpdate();
  // While confirmation blink runs, suppress slow blink to avoid overlap
  if (state.confirmBlinkActive)
    return;
  // Blink LED if in calibration OR if not calibrated yet (initial setup/factory reset)
  bool shouldBlink = (state.currentMode == CALIBRATE) || (state.maxSteps == 0);
  if (shouldBlink)
  {
    const uint32_t t = millis();
    if (t > nextLedMillis)
    {
      nextLedMillis = t + 400;
      digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    }
    return;
  }
  // Reduce brightness when idle for LED
  // ESP8266 PWM range is 0..255; LED is active-low on most boards
  int duty = (stepper.distanceToGo() != 0) ? 0 : 240;
  analogWrite(LED_PIN, duty);
}

void reset()
{
  WiFiManager wifiManager;
  helper.resetsettings(wifiManager);
  homekit_storage_reset();
}

// Turn motor power off after inactivity (kept for state housekeeping)
void handleEngineControllerActivity()
{
  if (state.lastMovementTime != 0 && millis() - state.lastMovementTime > 1000)
  {
    state.lastMovementTime = 0;
    // Avoid saving config while in CALIBRATE; saves during calibration were
    // noisy and not useful. Persist only when in NORMAL mode.
    if (state.currentMode != CALIBRATE)
    {
      saveConfig();
      if (state.maxSteps != 0)
      {
        currentPosition.value.int_value = getCurrentPosition();
        homekit_characteristic_notify(&currentPosition, currentPosition.value);
        if (positionState.value.int_value != POS_STOPPED)
        {
          positionState.value.int_value = POS_STOPPED;
          homekit_characteristic_notify(&positionState, positionState.value);
        }
      }
    }
  }
}

// 0% = bottom (closed), 100% = top (open)
int getCurrentPosition()
{
  if (state.maxSteps <= 0)
    return 0;
  // Integer math with rounding: pos = 100 * (maxSteps - currentStep) / maxSteps
  long numer = 100L * ((long)state.maxSteps - (long)state.currentStep);
  // add half divisor for rounding
  int pos = (int)((numer + (state.maxSteps / 2)) / (long)state.maxSteps);
  if (pos < 0)
    pos = 0;
  if (pos > 100)
    pos = 100;
  return pos;
}

bool loadConfig()
{
  if (!helper.loadconfig())
    return false;

  JsonVariant json = helper.getconfig();
  state.currentStep = json["currentStep"];
  state.maxSteps = json["maxSteps"];
  targetPosition.value.int_value = json["targetPositionValue"];
  // Load raw calibration points if present
  JsonVariant v;
  v = json["rawUpStep"];
  if (v)
    state.upStep = (int)v;
  else
    state.upStep = 0;
  v = json["rawDownStep"];
  if (v)
    state.downStep = (int)v;
  else
    state.downStep = 0;
  currentPosition.value.int_value = getCurrentPosition();
  return true;
}

bool saveConfig()
{
  DynamicJsonBuffer jsonBuffer(500);
  JsonObject &json = jsonBuffer.createObject();
  json["currentStep"] = state.currentStep;
  json["maxSteps"] = state.maxSteps;
  json["targetPositionValue"] = targetPosition.value.int_value;
  // store raw calibration points if present
  json["rawUpStep"] = state.upStep;
  json["rawDownStep"] = state.downStep;
  return helper.saveconfig(json);
}

void enableCalibrationMode()
{
  state.currentMode = CALIBRATE;
  state.currentCalibrationStep = INIT;
  // Only require release if a physical button is actually held now
  bool held = Buttons::isUpPressed() || Buttons::isDownPressed();
  state.calRequireRelease = held;
  // ensure no stale web jog causes motion on entry
  state.calJogDir = 0;
  stepper.moveTo(stepper.currentPosition());
  stepper.run();
  state.lastMovementTime = 0;
  // During calibration we want a smooth, continuous action via runSpeed()
  // Disable acceleration so runSpeed() produces steady velocity.
  stepper.setAcceleration(0.0f);
  stepper.setMaxSpeed(CAL_SPEED);
  // clear any prior speed used by runSpeed()
  stepper.setSpeed(0.0f);
  DPRINTLN("Entered CALIBRATE mode (continuous runSpeed)");
}

void shadesControl()
{
  if (state.currentMode != NORMAL || state.maxSteps == 0)
    return;

  // Convert target percentage to steps (local variable)
  long targetStep = ((100 - (float)targetPosition.value.int_value) / 100.0f) * state.maxSteps;

  // Command stepper to the target (run() moves it)
  if (targetStep != stepper.targetPosition())
  {
    stepper.moveTo(targetStep);
    state.lastMovementTime = millis();
    // User-facing feedback for Normal mode moves
    state.lastMessage = String("Moving to ") + targetPosition.value.int_value + "%";

    // Update positionState based on direction
    long dist = stepper.targetPosition() - stepper.currentPosition();
    int newState;
    if (dist == 0)
      newState = POS_STOPPED;
    else if (dist > 0)
      newState = POS_DECREASING; // moving toward larger steps -> shades going down (closing)
    else
      newState = POS_INCREASING; // moving toward smaller steps -> shades going up (opening)

    if (positionState.value.int_value != newState)
    {
      positionState.value.int_value = newState;
      homekit_characteristic_notify(&positionState, positionState.value);
    }
  }

  // If no distance left and we previously reported moving, update position and set STOPPED
  if (stepper.distanceToGo() == 0 && positionState.value.int_value != POS_STOPPED)
  {
    int pos = getCurrentPosition();
    if (currentPosition.value.int_value != pos)
    {
      currentPosition.value.int_value = pos;
      homekit_characteristic_notify(&currentPosition, currentPosition.value);
    }
    positionState.value.int_value = POS_STOPPED;
    homekit_characteristic_notify(&positionState, positionState.value);
    state.lastMessage = F("Stopped");
  }
}

void homekitSetup()
{
  currentPosition.setter = currentPositionSet;
  currentPosition.getter = currentPositionGet;

  targetPosition.setter = targetPositionSet;
  targetPosition.getter = targetPositionGet;

  positionState.setter = positionStateSet;
  positionState.getter = positionStateGet;

  arduino_homekit_setup(&config);
}

void homekitLoop()
{
  arduino_homekit_loop();
}
