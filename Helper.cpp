#include "Arduino.h"
#include "Helper.h"
#include "Globals.h"

Helper::Helper() : _doc()
{
  if (!LittleFS.begin())
  {
    DPRINTLN("LittleFS mount failed");
  }
  this->_configfile = "/config.json";
}

boolean Helper::loadconfig()
{
  File configFile = LittleFS.open(this->_configfile, "r");
  if (!configFile)
  {
    DPRINTLN(F("Failed to open config file"));
    return false;
  }

  size_t size = configFile.size();
  if (size > 4096)
  {
    DPRINTLN(F("Config file size is too large"));
    configFile.close();
    return false;
  }

  // Clear previous content and parse JSON into persistent document
  _doc.clear();
  DeserializationError err = deserializeJson(_doc, configFile);

  // Avoid leaving opened files
  configFile.close();

  if (err)
  {
    DPRINTLN("Failed to parse config file");
    return false;
  }
  return true;
}

JsonObjectConst Helper::getconfig() const
{
  return _doc.as<JsonObjectConst>();
}

boolean Helper::saveconfig(const JsonDocument &json)
{
  File configFile = LittleFS.open(this->_configfile, "w");
  if (!configFile)
  {
    DPRINTLN("Failed to open config file for writing");
    return false;
  }

  if (serializeJson(json, configFile) == 0)
  {
    DPRINTLN("Failed to write JSON to config file");
    configFile.close();
    return false;
  }
  configFile.flush(); // Making sure it's saved
  // Close file so resources are released and data is committed
  configFile.close();
  DPRINTLN("Saved JSON to LittleFS");
  return true;
}

void Helper::resetsettings(WiFiManager &wifim)
{
  LittleFS.format();
  wifim.resetSettings();
}
