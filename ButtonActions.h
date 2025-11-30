#ifndef BUTTON_ACTIONS_H
#define BUTTON_ACTIONS_H

#include <Arduino.h>

// Centralized actions used by both physical buttons and web UI
// (keeps calibration save, blink and common motion actions in one place)

void BA_startBlink(int times, int ms);
void BA_blinkUpdate();

void BA_moveToPercent(int percent);
void BA_stopMotion();

void BA_calToggleJog(int8_t dir);
void BA_calStop();
void BA_exitCalibrationNoSave();

void BA_calibrationSaveTop();
bool BA_calibrationSaveBottom();

void BA_factoryReset();

#endif
