#include "Arduino.h"
#include "Helper.h"
#include "Globals.h"

Helper::Helper()
{
  if (!SPIFFS.begin())
  {
    DPRINTLN("SPIFFS mount failed");
  }
  this->_configfile = "/config.json";
}

boolean Helper::loadconfig()
{
  File configFile = SPIFFS.open(this->_configfile, "r");
  if (!configFile)
  {
    DPRINTLN(F("Failed to open config file"));
    return false;
  }

  size_t size = configFile.size();
  if (size > 1024)
  {
    DPRINTLN(F("Config file size is too large"));
    configFile.close();
    return false;
  }

  // Parse SPIFFS JSON directly into ArduinoJson buffer
  DynamicJsonBuffer jsonBuffer(300);
  this->_config = jsonBuffer.parseObject(configFile);

  // Avoid leaving opened files
  configFile.close();

  if (!this->_config.success())
  {
    DPRINTLN("Failed to parse config file");
    return false;
  }
  return true;
}

JsonVariant Helper::getconfig()
{
  return this->_config;
}

boolean Helper::saveconfig(JsonVariant json)
{
  File configFile = SPIFFS.open(this->_configfile, "w");
  if (!configFile)
  {
    DPRINTLN("Failed to open config file for writing");
    return false;
  }

  json.printTo(configFile);
  configFile.flush(); // Making sure it's saved
  // Close file so resources are released and data is committed
  configFile.close();
  DPRINTLN("Saved JSON to SPIFFS");
  return true;
}

void Helper::resetsettings(WiFiManager &wifim)
{
  SPIFFS.format();
  wifim.resetSettings();
}
