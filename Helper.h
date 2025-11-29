#ifndef Helper_h
#define Helper_h

#include <Arduino.h>

#ifdef NO_INLINE
#undef NO_INLINE
#endif
#include <ArduinoJson.h>
#include <LittleFS.h>
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
  static StaticJsonDocument<4096> _doc;
  String _configfile;
};

#endif
