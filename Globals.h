#ifndef GLOBALS_H
#define GLOBALS_H

#include <Arduino.h>

/*
 Debug logging helpers: define `SHADES_DEBUG` to enable serial debug output
 (add `-DSHADES_DEBUG` to compiler flags to enable; this avoids colliding with
 other libraries that use `DEBUG`).
*/
#ifdef SHADES_DEBUG
#define DPRINT(...) Serial.print(__VA_ARGS__)
#define DPRINTLN(...) Serial.println(__VA_ARGS__)
#define DPRINTF(...) Serial.printf(__VA_ARGS__)
#define SERIAL_DEBUG_INIT() Serial.setDebugOutput(true)
#else
#define DPRINT(...)
#define DPRINTLN(...)
#define DPRINTF(...)
#define SERIAL_DEBUG_INIT() Serial.setDebugOutput(false)
#endif

// Enums for mode and calibration step
enum mode
{
  NORMAL,
  CALIBRATE
};
enum calibrationStep
{
  NONE,
  INIT,
  UP_KNOWN
};

// HomeKit PositionState values
enum
{
  POS_DECREASING = 0,
  POS_INCREASING = 1,
  POS_STOPPED = 2
};

// Motion constants (provided by main translation unit). Shared read-only.
extern const float SPEED_MAX;
extern const float ACCEL;
extern const int MIN_TRAVEL; // minimum required calibration travel in steps

// Timing constants (all in milliseconds)
const uint32_t BUTTON_IGNORE_WINDOW_MS = 10000;  // Ignore button input for 10s after boot (prevents accidental triggers)
const uint32_t MAIN_LONG_PRESS_CAL_MS = 5000;    // 5s long press (UP+DOWN) to enter calibration mode
const uint32_t MAIN_LONG_PRESS_RESET_MS = 10000; // 10s long press (UP+DOWN) for factory reset
const uint32_t PRESET_DEFER_WINDOW_MS = 150;     // Window to detect MAIN press after single button (debounce)
const uint32_t LED_BLINK_INTERVAL_MS = 400;      // LED blink interval during calibration/unconfigured state

// Shared runtime state for the shades controller
struct ShadesState
{
  // ---- Mode / state flags ----
  mode currentMode;                       // NORMAL or CALIBRATE
  calibrationStep currentCalibrationStep; // INIT, UP_KNOWN, etc.
  bool confirmBlinkActive;                // true while confirmation blink sequence runs
  bool exitCalibrationAfterBlink;         // leave CALIBRATE when confirmation finishes

  // ---- Position / raw calibration points ----
  int currentStep; // Current absolute step (rebased after calibration)
  int maxSteps;    // Total travel after calibration (bottom raw - top raw)
  int upStep;      // Raw step captured at TOP during calibration
  int downStep;    // Raw step captured at BOTTOM during calibration

  // ---- Calibration jogging control ----
  int8_t calJogDir;       // -1 up, 0 stop, +1 down (continuous jog direction)
  bool calRequireRelease; // Prevent accidental motion if buttons held entering CAL

  // ---- Hold/torque state ----
  bool holdingActive; // true while coils are intentionally energized to hold position

  // ---- MAIN (UP+DOWN) press tracking ----
  bool lastBothPressed;    // Previous loop both-pressed state
  uint32_t bothPressStart; // Timestamp when both buttons became pressed
  bool mainLong5Handled;   // Guard for 5s long-press (enter calibration)
  bool mainLong10Handled;  // Guard for 10s long-press (factory reset)

  // ---- Timing ----
  uint32_t startupTime;      // Boot time for initial button ignore window
  uint32_t lastMovementTime; // For motor inactivity persistence and power logic

  // ---- UI feedback ----
  String lastMessage; // Last status / calibration message
};

extern ShadesState state;

#endif
