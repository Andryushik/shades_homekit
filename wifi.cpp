#include "wifi.h"
#include "Globals.h"
#include <WiFiManager.h>

void wifiConnect()
{
  // Set a hostname and start captive-portal auto-connect if needed
  WiFi.hostname("shades_homekit");

  WiFiManager wifiManager;
  wifiManager.autoConnect("Roller Shades Configuration");

  DPRINTLN("WiFi connecting...");
  while (!WiFi.isConnected())
  {
    delay(100);
  }
  DPRINTF("WiFi connected, IP: %s\n", WiFi.localIP().toString().c_str());

  // Start mDNS so the device is reachable as shades_homekit.local
  if (MDNS.begin("shades_homekit"))
  {
    MDNS.addService("http", "tcp", 80);
    DPRINTLN("mDNS started as shades_homekit.local");
  }
  else
  {
    DPRINTLN("mDNS start failed");
  }
}
