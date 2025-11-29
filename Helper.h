#ifndef Helper_h
#define Helper_h

#include <Arduino.h>

// Simple helpers for SPIFFS + ArduinoJson (v5) and Wi‑Fi manager
#ifdef NO_INLINE
#undef NO_INLINE
#endif
#include <ArduinoJson.h> // v6 API (DynamicJsonDocument/deserializeJson)
#include <LittleFS.h>    // LittleFS (replacement for SPIFFS)
#include <ESP8266WiFi.h>
#include <WiFiManager.h>

class Helper
{
public:
  Helper();
  boolean loadconfig();
  JsonObjectConst getconfig() const;
  boolean saveconfig(const JsonDocument &json);
  void resetsettings(WiFiManager &wifim);

private:
  JsonDocument _doc;
  String _configfile;
};

#endif
