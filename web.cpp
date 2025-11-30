#include "web.h"
#include <Arduino.h>
#include <ESP8266WebServer.h>
#include <ArduinoJson.h>
#include "Globals.h"
#include "Buttons.h"
#include "ButtonActions.h"
#include <AccelStepper.h>
#include <arduino_homekit_server.h>

extern "C" homekit_characteristic_t currentPosition;
extern "C" homekit_characteristic_t targetPosition;
extern "C" homekit_characteristic_t positionState;

// Accessors provided by main translation unit
extern int getCurrentPosition();
// Stepper instance (for STOP logic)
extern AccelStepper stepper;

// Externals from main/Buttons
extern ShadesState state;
extern void enableCalibrationMode();
extern bool saveConfig();
extern void reset();

static ESP8266WebServer server(80);

// Use PROGMEM for large HTML content to save RAM (global scope)
const char HTML_PAGE[] PROGMEM = R"html(
<!doctype html><html><head><meta name="viewport" content="width=device-width,initial-scale=1"><title>Shades</title><style>body{font-family:sans-serif;margin:16px}h2,h3,p{margin:6px}button{margin:6px;padding:10px 14px}code{background:#eee;padding:2px 4px;border-radius:3px}.blink{animation:b .8s steps(2) infinite}@keyframes b{50%{opacity:.35}}</style></head><body>
<h2>Roller Shades Remote Controller</h2>
<p>Mode: <b><span id='mode'>MODE_PLACEHOLDER</span></b></p>
<p>Position (open): <code id='pos'>POS_PLACEHOLDER</code>%</p>
<p>Current step: <code id='cur'>CUR_PLACEHOLDER</code> / Max: <code id='max'>MAX_PLACEHOLDER</code></p>
<p id='msg' style='font-weight:600'></p>

<hr><h3>Control</h3>
<div>
<button class='act' data-act='/cal/up/start'>Up (open)</button>
<button class='act' data-act='/cal/down/start'>Down (close)</button>
<button class='act' id='btnStop' data-act='/cal/hold/stop'>Stop</button>
</div>

<hr><h3>Calibration</h3>
<div>
<button class='act' id='calStart' data-act='/cal/start'>Start Calibration</button>
<button class='act' id='calStop' data-act='/cal/stop' style="display:none">Exit Calibration</button>
</div>
<div id='calSave' style="margin-top:6px;display:none">
<button class='act' data-act='/cal/saveTop'>Save Top Position</button>
<button class='act' data-act='/cal/saveBottom'>Save Bottom Position</button>
</div>

<hr><div>
<button class='act' data-act='/reboot'>Safe Reboot</button>
<button class='act' data-act='/factory' style="background:#b00020;color:#fff">Factory Reset</button>
</div>
<script>(()=>{let prevStep=null;const u=()=>{fetch('/status').then(r=>r.json()).then(s=>{var el; if((el=document.getElementById('cur')))el.textContent=s.currentStep; if((el=document.getElementById('max')))el.textContent=s.maxSteps; if((el=document.getElementById('mode'))){el.textContent=s.mode; if(s.mode==='CALIBRATE'){el.classList.add('blink');} else {el.classList.remove('blink');}} if((el=document.getElementById('pos')))el.textContent=s.position; var moving=!!s.moving; prevStep=s.currentStep; var sb=document.getElementById('btnStop'); if(sb){ if(moving){sb.classList.add('blink');} else {sb.classList.remove('blink');}} var m=document.getElementById('msg'); if(m){ m.textContent=s.msg||''; if(s.msg && s.msg.indexOf('too small')>-1){ m.style.color='#b00020'; } else if(s.msg){ m.style.color='#036b00'; } else { m.style.color=''; } } var cs=document.getElementById('calSave'); if(cs){ if(s.mode==='CALIBRATE'){ cs.style.display='block'; } else { cs.style.display='none'; } } var st=document.getElementById('calStart'), sp=document.getElementById('calStop'); if(st&&sp){ if(s.mode==='CALIBRATE'){ st.style.display='none'; sp.style.display='inline-block'; } else { st.style.display='inline-block'; sp.style.display='none'; } } });}; setInterval(u,400); window.addEventListener('load',u); document.addEventListener('click',(e)=>{ var b=e.target; if(b.classList && b.classList.contains('act')){ var act=b.getAttribute('data-act'); if(act==='/factory'){ if(!confirm('Factory reset will erase Wi-Fi, SPIFFS config, and HomeKit pairing. Continue?')) return; } fetch(act,{method:'POST'}).then(()=>{ setTimeout(u,300); }); e.preventDefault(); } });})();</script>
</body></html>
)html";

static void handleRoot()
{
  String page;
  page.reserve(1024); // Reduced from 2048
  String htmlStr = FPSTR(HTML_PAGE);
  htmlStr.replace("MODE_PLACEHOLDER", (state.currentMode == CALIBRATE) ? "CALIBRATE" : "NORMAL");
  htmlStr.replace("POS_PLACEHOLDER", String(getCurrentPosition()));
  htmlStr.replace("CUR_PLACEHOLDER", String(state.currentStep));
  htmlStr.replace("MAX_PLACEHOLDER", String(state.maxSteps));
  page = htmlStr;
  server.send(200, "text/html", page);
}

static void redirectRoot()
{
  server.sendHeader("Location", "/", true);
  server.send(303);
}

static void handleCalStart()
{
  if (state.currentMode != CALIBRATE)
  {
    DPRINTLN("WEB: Start Calibration requested");
    enableCalibrationMode();
  }
  redirectRoot();
}

static void handleCalStop()
{
  // stop any calibration jogging and return to NORMAL
  BA_exitCalibrationNoSave();
  DPRINTLN("WEB: Exit Calibration requested");
  redirectRoot();
}

static void handleUpStart()
{
  if (state.currentMode == CALIBRATE)
  {
    BA_calToggleJog(-1);
    DPRINTLN("WEB: Calibration jog UP toggle");
  }
  else
  {
    BA_moveToPercent(100);
    DPRINTLN("WEB: Move to 100% (UP)");
  }
  redirectRoot();
}

static void handleDownStart()
{
  if (state.currentMode == CALIBRATE)
  {
    BA_calToggleJog(1);
    DPRINTLN("WEB: Calibration jog DOWN toggle");
  }
  else
  {
    BA_moveToPercent(0);
    DPRINTLN("WEB: Move to 0% (DOWN)");
  }
  redirectRoot();
}

static void handleHoldStop()
{
  if (state.currentMode == CALIBRATE)
  {
    DPRINTLN("WEB: Calibration jog STOP");
    BA_calStop();
  }
  else
  {
    DPRINTLN("WEB: STOP command");
    BA_stopMotion();
  }
  redirectRoot();
}

// Use encapsulated calibration save routines from Buttons namespace

static void handleSaveTop()
{
  if (state.currentMode == CALIBRATE)
  {
    DPRINTLN("WEB: Save TOP position");
    Buttons::calibrationSaveTop();
  }
  redirectRoot();
}

static void handleSaveBottom()
{
  if (state.currentMode == CALIBRATE)
  {
    DPRINTLN("WEB: Save BOTTOM position");
    Buttons::calibrationSaveBottom();
  }
  redirectRoot();
}

static void handleReboot()
{
  DPRINTLN("WEB: Safe Reboot requested");
  int currentPercent = getCurrentPosition();
  targetPosition.value.int_value = currentPercent;
  currentPosition.value.int_value = currentPercent;
  positionState.value.int_value = POS_STOPPED;
  stepper.stop();
  state.currentStep = stepper.currentPosition();
  if (!saveConfig())
  {
    DPRINTLN("Warning: Failed to save config during reboot");
    // Continue with reboot anyway, but log the error
  }

  String page;
  page.reserve(400);
  page += F("<!doctype html><html><head><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>Rebooting</title><style>body{font-family:sans-serif;margin:24px}a{color:#0645ad;text-decoration:none}a:hover{text-decoration:underline}</style></head><body>");
  page += F("<h2>Rebooting...\n</h2>");
  page += F("<p>Shades Controller is restarting now. Please wait.</p>");
  page += F("<script>setTimeout(function(){location.href='/'},15000);</script>");
  page += F("</body></html>");
  server.send(200, "text/html", page);
  delay(300);
  ESP.restart();
}

static void handleFactoryPost()
{
  DPRINTLN("WEB: Factory Reset requested");
  server.send(200, "text/plain", "Factory resetting...\n");
  delay(200);
  // Centralized factory reset
  BA_factoryReset();
}

static void handleStatus()
{
  // Small JSON document for status
  StaticJsonDocument<512> doc;
  doc["currentStep"] = state.currentStep;
  doc["maxSteps"] = state.maxSteps;
  doc["mode"] = (state.currentMode == CALIBRATE) ? "CALIBRATE" : "NORMAL";
  doc["position"] = getCurrentPosition();
  doc["msg"] = state.lastMessage;
  doc["moving"] = (stepper.distanceToGo() != 0);
  String out;
  serializeJson(doc, out);
  server.sendHeader("Cache-Control", "no-cache");
  server.send(200, "application/json", out);
}

void webBegin()
{
  server.on("/", HTTP_GET, handleRoot);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/factory", HTTP_POST, handleFactoryPost);
  server.on("/cal/start", HTTP_POST, handleCalStart);
  server.on("/cal/stop", HTTP_POST, handleCalStop);
  server.on("/cal/up/start", HTTP_POST, handleUpStart);
  server.on("/cal/down/start", HTTP_POST, handleDownStart);
  server.on("/cal/hold/stop", HTTP_POST, handleHoldStop);
  server.on("/cal/saveTop", HTTP_POST, handleSaveTop);
  server.on("/cal/saveBottom", HTTP_POST, handleSaveBottom);
  server.on("/reboot", HTTP_POST, handleReboot);
  server.begin();
}

void webLoop()
{
  server.handleClient();
}
