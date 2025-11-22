#include "ota.h"

#ifdef ENABLE_OTA
#include <ArduinoOTA.h>
#include <AccelStepper.h>
#include "Globals.h"

extern AccelStepper stepper;
extern ShadesState state;

namespace OTA
{

  void setup()
  {
    ArduinoOTA.setHostname("shades_homekit");
    ArduinoOTA.setPassword("28142814");

    ArduinoOTA.onStart([]()
                       {
    stepper.stop();
    state.lastMessage = F("OTA start"); });

    ArduinoOTA.onEnd([]()
                     { state.lastMessage = F("OTA end; rebooting"); });

    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total)
                          {
    static unsigned int lastPct = 0;
    unsigned int pct = (progress * 100U) / total;
    if (pct - lastPct >= 10U) {
      lastPct = pct;
      state.lastMessage = String("OTA ") + pct + "%";
    } });

    ArduinoOTA.onError([](ota_error_t)
                       { state.lastMessage = F("OTA error"); });

    ArduinoOTA.begin();
  }

  void loop()
  {
    ArduinoOTA.handle();
  }

} // namespace OTA

#else

namespace OTA
{
  void setup() {}
  void loop() {}
} // namespace OTA

#endif
