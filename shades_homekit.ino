#include <Arduino.h>
#include <arduino_homekit_server.h>
#include "Helper.h"
#include "pins.h"
#include "wifi.h"
#include "Buttons.h"
#include <AccelStepper.h>
#include "Globals.h"
#include "web.h"

// Speed/settings constants
const float SPEED_MAX = 900.0f; // steps/s
const float ACCEL = 300.0f;     // steps/s^2
const float CAL_SPEED = 300.0f; // steps/s during calibration (continuous)
// HOLD_TORQUE_MS semantics:
//   0   -> disable coils immediately after stop
//  >0   -> keep coils energized for that many milliseconds, then disable
//  -1   -> never disable (always hold torque) WARNING: motor/driver will stay warm
const int32_t HOLD_TORQUE_MS = 3000; // change to -1 for infinite hold
const int MIN_TRAVEL = 4096;         // minimum calibration travel 1 full rotation (steps)

// 28BYJ-48 via ULN2003 using HALF4WIRE; coil order IN1, IN3, IN2, IN4
AccelStepper stepper(AccelStepper::HALF4WIRE, IN1, IN3, IN2, IN4);

Helper helper;

// Centralized runtime state (see `Globals.h` for field docs)
ShadesState state = {
    .currentMode = NORMAL,
    .currentCalibrationStep = NONE,
    .confirmBlinkActive = false,
    .exitCalibrationAfterBlink = false,
    .currentStep = 0,
    .maxSteps = 0,
    .upStep = 0,
    .downStep = 0,
    .calJogDir = 0,
    .calRequireRelease = false,
    .holdingActive = false,
    .lastBothPressed = false,
    .bothPressStart = 0,
    .mainLong5Handled = false,
    .mainLong10Handled = false,
    .startupTime = 0,
    .lastMovementTime = 0,
    .lastMessage = String()};

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
  Serial.begin(115200);
  SERIAL_DEBUG_INIT();
  DPRINTLN("=== TEST BUILD " __DATE__ " " __TIME__ " ===");

  pinMode(LED_PIN, OUTPUT);
  // BUTTON_MAIN is simulated by both UP+DOWN pressed
  pinMode(BUTTON_UP_PIN, INPUT_PULLUP);
  pinMode(BUTTON_DOWN_PIN, INPUT_PULLUP);

  state.startupTime = millis();

  loadConfig();
  if (state.maxSteps == 0)
    enableCalibrationMode();

  // Initialize stepper with normal motion profile
  stepper.setMaxSpeed(SPEED_MAX);
  stepper.setAcceleration(ACCEL);
  // Optional: tune pulse width for ULN2003 if needed (default is usually fine)
  // stepper.setMinPulseWidth(2);
  stepper.setCurrentPosition(state.currentStep);

  wifiConnect();
  // Print chosen hostname and IP for quick verification
  Serial.printf("Host: %s, IP: %s\n", WiFi.hostname().c_str(), WiFi.localIP().toString().c_str());

  homekitSetup();
  Buttons::init();
  webBegin();
}

void loop()
{
  static bool wasCalibrating = false;
  static unsigned long lastHousekeeping = 0;
  const unsigned long HOUSEKEEP_MS = 100; // run housekeeping less frequently

  // 1. Buttons (highest priority)
  Buttons::loop();

  // 2. Update target/commands (HomeKit/web may have changed the target)
  shadesControl();

  // 3. MOTOR - must be called every loop
  if (state.currentMode == CALIBRATE)
  {
    // calibration: continuous speed control handled elsewhere via calJogDir/state
    wasCalibrating = true;

    bool up = (state.calJogDir < 0);
    bool down = (state.calJogDir > 0);
    if (state.calRequireRelease)
    {
      if (!up && !down)
        state.calRequireRelease = false;
    }

    if (!state.calRequireRelease)
    {
      if (up && !down)
      {
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
        stepper.setSpeed(0.0f);
      }
    }
  }
  else
  {
    if (wasCalibrating)
    {
      stepper.setAcceleration(ACCEL);
      stepper.setMaxSpeed(SPEED_MAX);
      wasCalibrating = false;
      state.calJogDir = 0;
    }

    if (stepper.distanceToGo() != 0)
    {
      stepper.enableOutputs();
      state.lastMovementTime = millis();
    }
    stepper.run();
  }

  // 4. State sync - ensure position reflects current motor position
  state.currentStep = stepper.currentPosition();

  // 5. HomeKit - call loop/notifications after position update
  homekitLoop();

  // 6. Non-blocking UI tasks
  properLedDisplay();

  // 7. Housekeeping - run less frequently to avoid frequent SPIFFS/heavy ops
  if (millis() - lastHousekeeping >= HOUSEKEEP_MS)
  {
    handleEngineControllerActivity();
    lastHousekeeping = millis();
  }

  // 8. Web - run at the end of the loop (may be heavier)
  webLoop();

  // 9. Yield
  yield();
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
      nextLedMillis = t + LED_BLINK_INTERVAL_MS;
      digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    }
    return;
  }
  // Reduce brightness when idle for LED
  // ESP8266 PWM range is 0..255; LED is active-low on most boards
  int duty = (stepper.distanceToGo() != 0) ? 0 : 254;
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
  static bool wasMoving = false;
  static uint32_t stoppedAt = 0; // timestamp when motion stopped

  bool moving = (stepper.distanceToGo() != 0) || (state.currentMode == CALIBRATE);
  if (moving)
  {
    wasMoving = true;
    stoppedAt = 0; // reset any pending hold timer while moving
    return;
  }

  // Transition edge: moving -> stopped
  if (wasMoving)
  {
    wasMoving = false;
    stoppedAt = millis();
    state.holdingActive = false;
    if (state.currentMode != CALIBRATE)
    {
      if (!saveConfig())
      {
        DPRINTLN("Warning: Failed to save config after movement");
        state.lastMessage = F("Save config failed");
      }
      if (state.maxSteps != 0)
      {
        int pos = getCurrentPosition();
        if (currentPosition.value.int_value != pos)
        {
          currentPosition.value.int_value = pos;
          homekit_characteristic_notify(&currentPosition, currentPosition.value);
        }
        if (positionState.value.int_value != POS_STOPPED)
        {
          positionState.value.int_value = POS_STOPPED;
          homekit_characteristic_notify(&positionState, positionState.value);
        }
      }
      // Decide hold strategy
      if (HOLD_TORQUE_MS == 0)
      {
        // Immediate disable
        stepper.disableOutputs();
        stoppedAt = 0;
        state.holdingActive = false;
      }
      else if (HOLD_TORQUE_MS < 0)
      {
        // Infinite hold: ensure outputs are ON
        stepper.enableOutputs();
        state.holdingActive = true;
        // stoppedAt retained only for reference; never auto-disable
      }
      else
      {
        // Timed hold: keep outputs enabled until timeout
        stepper.enableOutputs();
        state.holdingActive = true;
      }
    }
    return; // wait for hold interval if configured
  }

  // If we are stopped and holding torque, check timeout (skip if infinite hold)
  if (state.holdingActive && stoppedAt != 0 && HOLD_TORQUE_MS > 0 && (millis() - stoppedAt) >= (uint32_t)HOLD_TORQUE_MS)
  {
    stepper.disableOutputs();
    stoppedAt = 0; // done holding
    state.holdingActive = false;
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

  JsonObjectConst json = helper.getconfig();
  state.currentStep = json["currentStep"] | 0;
  state.maxSteps = json["maxSteps"] | 0;
  targetPosition.value.int_value = json["targetPositionValue"] | 0;
  // Load raw calibration points if present
  state.upStep = json["rawUpStep"] | 0;
  state.downStep = json["rawDownStep"] | 0;
  currentPosition.value.int_value = getCurrentPosition();
  return true;
}

bool saveConfig()
{
  StaticJsonDocument<512> doc;
  doc["currentStep"] = state.currentStep;
  doc["maxSteps"] = state.maxSteps;
  doc["targetPositionValue"] = targetPosition.value.int_value;
  // store raw calibration points if present
  doc["rawUpStep"] = state.upStep;
  doc["rawDownStep"] = state.downStep;
  return helper.saveconfig(doc);
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
  // Clamp targetPosition to [0,100] to avoid invalid writes
  int tp = targetPosition.value.int_value;
  if (tp < 0)
    tp = 0;
  else if (tp > 100)
    tp = 100;
  if (tp != targetPosition.value.int_value)
  {
    targetPosition.value.int_value = tp;
    homekit_characteristic_notify(&targetPosition, targetPosition.value);
  }
  long targetStep = ((100 - (float)tp) / 100.0f) * state.maxSteps;

  // Command stepper to the target (run() moves it)
  if (targetStep != stepper.targetPosition())
  {
    // Ensure coils are energized before a new move
    stepper.enableOutputs();
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
    // Safety: ensure coils are de-energized when we report STOPPED
    // Disable only if we are not intentionally holding torque
    if (HOLD_TORQUE_MS == 0)
    {
      stepper.disableOutputs();
    }
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
