#include "wifi.h"
#include "Globals.h"
#include <WiFiManager.h>

void wifiConnect()
{
  // Set a hostname and start captive-portal auto-connect if needed
  WiFi.mode(WIFI_STA);
  WiFi.hostname("shades_homekit");
  // Optional: reduce Wi‑Fi power-save jitter that can affect timing
  WiFi.setSleepMode(WIFI_NONE_SLEEP);

  WiFiManager wifiManager;
  bool ok = wifiManager.autoConnect("Roller Shades Configuration");
  if (!ok)
  {
    DPRINTLN("WiFi autoConnect failed; rebooting in 5s");
    delay(5000);
    ESP.restart();
  }

  DPRINTLN("WiFi connecting...");
  // autoConnect() blocks until connected or failed; no extra busy-wait needed
  DPRINTF("WiFi connected, IP: %s\n", WiFi.localIP().toString().c_str());

  // Start mDNS so the device is reachable as shades_homekit.local
  const char *host = "shades_homekit";
  const uint8_t maxTries = 5;
  bool mdnsOk = false;
  for (uint8_t i = 0; i < maxTries; ++i)
  {
    if (MDNS.begin(host))
    {
      MDNS.addService("http", "tcp", 80);
      DPRINTLN("mDNS started as shades_homekit.local");
      mdnsOk = true;
      break;
    }
    DPRINTLN("mDNS start failed; retrying...");
    delay(1000);
  }
  if (!mdnsOk)
  {
    DPRINTLN("mDNS unavailable after retries");
  }
}
