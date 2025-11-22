#ifndef Helper_h
#define Helper_h

#include <Arduino.h>

// Simple helpers for SPIFFS + ArduinoJson (v5) and Wi‑Fi manager
#ifdef NO_INLINE
#undef NO_INLINE
#endif
#include <ArduinoJson.h> // v5 API (DynamicJsonBuffer/JsonVariant)
#include <FS.h>          // SPIFFS
#include <ESP8266WiFi.h>
#include <WiFiManager.h>

class Helper
{
public:
  Helper();
  boolean loadconfig();
  JsonVariant getconfig();
  boolean saveconfig(JsonVariant json);
  void resetsettings(WiFiManager &wifim);

private:
  JsonVariant _config;
  String _configfile;
};

#endif
