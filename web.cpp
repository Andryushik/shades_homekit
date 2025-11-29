#include "web.h"
#include <Arduino.h>
#include <ESP8266WebServer.h>
#include <ArduinoJson.h>
#include "Globals.h"
#include "Buttons.h"
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

static void handleRoot()
{
  String page;
  page.reserve(2048);
  page += F("<!doctype html><html><head><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>Shades</title><style>body{font-family:sans-serif;margin:16px}h2,h3,p{margin:6px}button{margin:6px;padding:10px 14px}code{background:#eee;padding:2px 4px;border-radius:3px}.blink{animation:b .8s steps(2) infinite}@keyframes b{50%{opacity:.35}}</style></head><body>");
  page += F("<h2>Roller Shades Controller</h2>");
  page += F("<p>Mode: <b><span id='mode'>");
  page += (state.currentMode == CALIBRATE) ? "CALIBRATE" : "NORMAL";
  page += F("</span></b></p>");
  page += F("<p>Position (open): <code id='pos'>");
  page += String(getCurrentPosition());
  page += F("</code>%</p>");
  page += F("<p>Current step: <code id='cur'>");
  page += String(state.currentStep);
  page += F("</code> / Max: <code id='max'>");
  page += String(state.maxSteps);
  page += F("</code></p>");
  page += F("<p id='msg' style='font-weight:600'></p>");

  page += F("<hr><h3>Control</h3>");
  page += F("<div>");
  page += F("<button class='act' data-act='/cal/up/start'>Up (open)</button>");
  page += F("<button class='act' data-act='/cal/down/start'>Down (close)</button>");
  page += F("<button class='act' id='btnStop' data-act='/cal/hold/stop'>Stop</button>");
  page += F("</div>");

  page += F("<hr><h3>Calibration</h3>");
  page += F("<div>");
  page += F("<button class='act' id='calStart' data-act='/cal/start'>Start Calibration</button>");
  page += F("<button class='act' id='calStop' data-act='/cal/stop' style=\"display:none\">Exit Calibration</button>");
  page += F("</div>");
  page += F("<div id='calSave' style=\"margin-top:6px;display:none\">");
  page += F("<button class='act' data-act='/cal/saveTop'>Save Top Position</button>");
  page += F("<button class='act' data-act='/cal/saveBottom'>Save Bottom Position</button>");
  page += F("</div>");

  page += F("<hr><div>");
  page += F("<button class='act' data-act='/reboot'>Safe Reboot</button>");
  page += F("<button class='act' data-act='/factory' style=\"background:#b00020;color:#fff\">Factory Reset</button>");
  page += F("</div>");
  page += F("<script>(function(){var prevStep=null;function u(){fetch('/status').then(r=>r.json()).then(s=>{var el; if((el=document.getElementById('cur')))el.textContent=s.currentStep; if((el=document.getElementById('max')))el.textContent=s.maxSteps; if((el=document.getElementById('mode'))){el.textContent=s.mode; if(s.mode==='CALIBRATE'){el.classList.add('blink');} else {el.classList.remove('blink');}} if((el=document.getElementById('pos')))el.textContent=s.position; var moving=(prevStep!==null && s.currentStep!==prevStep); prevStep=s.currentStep; var sb=document.getElementById('btnStop'); if(sb){ if(moving){sb.classList.add('blink');} else {sb.classList.remove('blink');}} var m=document.getElementById('msg'); if(m){m.textContent=s.msg||''; if(s.msg && s.msg.indexOf('too small')>-1){m.style.color='#b00020';} else if(s.msg){m.style.color='#036b00';} else {m.style.color='';}} var cs=document.getElementById('calSave'); if(cs){ if(s.mode==='CALIBRATE'){cs.style.display='block';} else {cs.style.display='none';}} var st=document.getElementById('calStart'), sp=document.getElementById('calStop'); if(st&&sp){ if(s.mode==='CALIBRATE'){st.style.display='none'; sp.style.display='inline-block';} else {st.style.display='inline-block'; sp.style.display='none';}}});} setInterval(u,400); window.addEventListener('load',u); document.addEventListener('click',function(e){var b=e.target; if(b.classList && b.classList.contains('act')){var act=b.getAttribute('data-act'); if(act==='/factory'){ if(!confirm('Factory reset will erase Wi-Fi, SPIFFS config, and HomeKit pairing. Continue?')) return; } fetch(act,{method:'POST'}).then(()=>setTimeout(u,300)); e.preventDefault();}});})();</script>");
  page += F("</body></html>");
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
    enableCalibrationMode();
  }
  redirectRoot();
}

static void handleCalStop()
{
  // stop any calibration jogging and return to NORMAL
  state.calJogDir = 0;
  if (state.currentMode == CALIBRATE)
  {
    // Return to NORMAL without saving
    state.currentMode = NORMAL;
  }
  redirectRoot();
}

static void handleUpStart()
{
  if (state.currentMode == CALIBRATE)
  {
    // Toggle jog behavior in calibration mode
    if (state.calJogDir == -1)
    {
      state.calJogDir = 0;
      state.lastMessage = F("Stopped");
    }
    else
    {
      state.calJogDir = -1; // up
      state.lastMessage = F("Moving UP");
    }
  }
  else
  {
    if (targetPosition.value.int_value != 100)
    {
      targetPosition.value.int_value = 100;
      homekit_characteristic_notify(&targetPosition, targetPosition.value);
    }
  }
  redirectRoot();
}

static void handleDownStart()
{
  if (state.currentMode == CALIBRATE)
  {
    // Toggle jog behavior in calibration mode
    if (state.calJogDir == 1)
    {
      state.calJogDir = 0;
      state.lastMessage = F("Stopped");
    }
    else
    {
      state.calJogDir = 1; // down
      state.lastMessage = F("Moving DOWN");
    }
  }
  else
  {
    if (targetPosition.value.int_value != 0)
    {
      targetPosition.value.int_value = 0;
      homekit_characteristic_notify(&targetPosition, targetPosition.value);
    }
  }
  redirectRoot();
}

static void handleHoldStop()
{
  if (state.currentMode == CALIBRATE)
  {
    // Stop calibration jogging
    state.calJogDir = 0;
    state.lastMessage = F("Stopped");
  }
  else
  {
    int newTarget = getCurrentPosition();
    targetPosition.value.int_value = newTarget;
    homekit_characteristic_notify(&targetPosition, targetPosition.value);
    stepper.moveTo(stepper.currentPosition());
    positionState.value.int_value = POS_STOPPED;
    homekit_characteristic_notify(&positionState, positionState.value);
    state.lastMessage = F("Stopped");
  }
  redirectRoot();
}

// Use encapsulated calibration save routines from Buttons namespace

static void handleSaveTop()
{
  if (state.currentMode == CALIBRATE)
  {
    Buttons::calibrationSaveTop();
  }
  redirectRoot();
}

static void handleSaveBottom()
{
  if (state.currentMode == CALIBRATE)
  {
    Buttons::calibrationSaveBottom();
  }
  redirectRoot();
}

static void handleReboot()
{
  int currentPercent = getCurrentPosition();
  targetPosition.value.int_value = currentPercent;
  currentPosition.value.int_value = currentPercent;
  positionState.value.int_value = POS_STOPPED;
  stepper.stop();
  state.currentStep = stepper.currentPosition();
  saveConfig();

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
  server.send(200, "text/plain", "Factory resetting...\n");
  delay(200);
  reset();
  delay(300);
  ESP.restart();
}

static void handleStatus()
{
  JsonDocument doc;
  doc["currentStep"] = state.currentStep;
  doc["maxSteps"] = state.maxSteps;
  doc["mode"] = (state.currentMode == CALIBRATE) ? "CALIBRATE" : "NORMAL";
  doc["position"] = getCurrentPosition();
  doc["msg"] = state.lastMessage;
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
