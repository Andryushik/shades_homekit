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
<!doctype html><html><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Shades</title><style>
body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,Oxygen,Ubuntu,Cantarell,sans-serif;margin:0;padding:20px;background:#f5f5f5;color:#333}
.container{max-width:400px;margin:0 auto}
.card{background:white;border-radius:12px;padding:20px;margin:16px 0;box-shadow:0 2px 8px rgba(0,0,0,0.1)}
h1{margin:0 0 16px 0;font-size:24px;color:#2c3e50;text-align:center}
.status{margin:16px 0;text-align:center}
.badge{display:inline-block;padding:4px 12px;border-radius:20px;font-size:14px;font-weight:500}
.badge-normal{background:#27ae60;color:white}
.badge-calibrate{background:#f39c12;color:white;animation:pulse 1s infinite}
.progress-bar{background:#ecf0f1;border-radius:8px;height:24px;margin:8px 0;overflow:hidden}
.progress-fill{height:100%;background:linear-gradient(90deg,#3498db,#2ecc71);transition:width 0.3s ease;width:POS_PLACEHOLDER%}
.progress-text{text-align:center;font-weight:600;color:#2c3e50;margin:8px 0}
.btn-row{display:flex;gap:8px;margin:16px 0;flex-wrap:wrap}
.btn{flex:1;min-width:80px;padding:12px 16px;border:none;border-radius:8px;font-size:16px;font-weight:500;cursor:pointer;transition:all 0.2s ease;text-align:center;background:#3498db;color:white}
.btn:hover{background:#2980b9;transform:translateY(-1px)}
.btn:active{transform:translateY(0)}
.btn-stop{background:#e74c3c}
.btn-stop:hover{background:#c0392b}
.btn-secondary{background:#95a5a6}
.btn-secondary:hover{background:#7f8c8d}
.btn-danger{background:#e74c3c}
.btn-danger:hover{background:#c0392b}
.btn-success{background:#27ae60}
.btn-success:hover{background:#229954}
.btn-cal{background:#f39c12}
.btn-cal:hover{background:#e67e22}
.msg{margin:12px 0;padding:8px 12px;border-radius:6px;text-align:center;font-weight:500}
.msg-success{background:#d4edda;color:#155724;border:1px solid #c3e6cb}
.msg-error{background:#f8d7da;color:#721c24;border:1px solid #f5c6cb}
.blink{animation:pulse 1s infinite}
@keyframes pulse{0%{opacity:1}50%{opacity:0.5}100%{opacity:1}}
@media(max-width:480px){.btn{min-width:70px;font-size:14px;padding:10px 12px}}
</style></head><body>
<div class="container">
<div class="card">
<h1>Roller Shades</h1>

<div class="status" style="margin:8px 0;font-size:12px;text-align:center">
<span class="badge MODE_CLASS" id='mode'>MODE_PLACEHOLDER</span>
</div>

<div class="progress-bar">
<div class="progress-fill" id="progress-fill" style="width:POS_PLACEHOLDER%"></div>
</div>
<div class="progress-text">Position: <strong id='pos'>POS_PLACEHOLDER</strong>%</div>

<div class="msg" id="msg-container" style="display:none;margin:8px 0;"></div>
</div>

<div class="card">
<h3>🎛️ Control</h3>
<div class="btn-row">
<button class="btn btn-success" data-act='/cal/up/start'>⬆️ Open</button>
<button class="btn btn-secondary" data-act='/cal/down/start'>⬇️ Close</button>
<button class="btn btn-stop" id='btnStop' data-act='/cal/hold/stop'>⏹️ Stop</button>
</div>
<div class="btn-row">
<button class="btn" data-act='/preset/30'>30%</button>
<button class="btn" data-act='/preset/50'>50%</button>
<button class="btn" data-act='/preset/70'>70%</button>
</div>
</div>

<div class="card">
<h3>⚙️ Calibration</h3>
<div class="btn-row">
<button class="btn btn-cal" id='calStart' data-act='/cal/start'>▶️ Start</button>
<button class="btn btn-secondary" id='calStop' data-act='/cal/stop' style="display:none">⏹️ Exit</button>
</div>
<div id='calSave' style="display:none;margin-top:12px">
<div class="btn-row">
<button class="btn btn-success" data-act='/cal/saveTop'>📍 Save Top</button>
<button class="btn btn-secondary" data-act='/cal/saveBottom'>📍 Save Bottom</button>
</div>
</div>
<div style="margin-top:12px;font-size:11px;color:#666;text-align:center">
Step: <code id='cur'>CUR_PLACEHOLDER</code> / <code id='max'>MAX_PLACEHOLDER</code>
</div>
</div>

<div class="card">
<h3>🔧 System</h3>
<div class="btn-row">
<button class="btn btn-secondary" data-act='/reboot'>🔄 Reboot</button>
<button class="btn btn-danger" data-act='/factory'>⚠️ Reset</button>
</div>
</div>
</div>

<script>(()=>{let prevStep=null;const u=()=>{fetch('/status').then(r=>r.json()).then(s=>{var el,pf,m;
if((el=document.getElementById('cur')))el.textContent=s.currentStep;
if((el=document.getElementById('max')))el.textContent=s.maxSteps;
if((el=document.getElementById('pos')))el.textContent=s.position;
if((pf=document.getElementById('progress-fill')))pf.style.width=s.position+'%';
if((el=document.getElementById('mode'))){el.textContent=s.mode; el.className='badge '+(s.mode==='CALIBRATE'?'badge-calibrate':'badge-normal');}
var moving=!!s.moving; prevStep=s.currentStep;
var sb=document.getElementById('btnStop'); if(sb){sb.classList.toggle('blink',moving);}
var mc=document.getElementById('msg-container'); var m=mc.querySelector('div')||mc.appendChild(document.createElement('div'));
m.textContent=s.msg||''; mc.style.display=s.msg?'block':'none';
if(s.msg){m.className=s.msg.indexOf('too small')>-1||s.msg.indexOf('failed')>-1?'msg msg-error':'msg msg-success';}
var cs=document.getElementById('calSave'); if(cs)cs.style.display=s.mode==='CALIBRATE'?'block':'none';
var st=document.getElementById('calStart'),sp=document.getElementById('calStop');
if(st&&sp){st.style.display=s.mode==='CALIBRATE'?'none':'inline-block'; sp.style.display=s.mode==='CALIBRATE'?'inline-block':'none';}
});}; setInterval(u,200); window.addEventListener('load',u);
document.addEventListener('click',(e)=>{var b=e.target.closest('[data-act]'); if(b){var act=b.getAttribute('data-act');
if(act==='/factory'){if(!confirm('Factory reset will erase Wi-Fi, SPIFFS config, and HomeKit pairing. Continue?'))return;}
fetch(act,{method:'POST'}).then(u).catch(()=>{alert('Error!');}); e.preventDefault();}});})();</script>
</body></html>
)html";

static void handleRoot()
{
  String page;
  page.reserve(1024); // Reduced from 2048
  String htmlStr = FPSTR(HTML_PAGE);
  String modeClass = (state.currentMode == CALIBRATE) ? "badge-calibrate" : "badge-normal";
  String modeText = (state.currentMode == CALIBRATE) ? "CALIBRATE" : "NORMAL";
  String posStr = String(getCurrentPosition());

  htmlStr.replace("MODE_CLASS", modeClass);
  htmlStr.replace("MODE_PLACEHOLDER", modeText);
  htmlStr.replace("POS_PLACEHOLDER", posStr);
  htmlStr.replace("CUR_PLACEHOLDER", String(state.currentStep));
  htmlStr.replace("MAX_PLACEHOLDER", String(state.maxSteps));
  page = htmlStr;
  server.send(200, "text/html; charset=UTF-8", page);
}

static void sendQuickResponse()
{
  server.send(200, "text/plain", "OK");
}

static void handleCalStart()
{
  if (state.currentMode != CALIBRATE)
  {
    DPRINTLN("WEB: Start Calibration requested");
    enableCalibrationMode();
  }
  sendQuickResponse();
}

static void handleCalStop()
{
  // stop any calibration jogging and return to NORMAL
  BA_exitCalibrationNoSave();
  DPRINTLN("WEB: Exit Calibration requested");
  sendQuickResponse();
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
  sendQuickResponse();
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
  sendQuickResponse();
}

static void handlePreset30()
{
  if (state.currentMode == CALIBRATE)
  {
    // ignore during calibration
  }
  else
  {
    BA_moveToPercent(30);
    DPRINTLN("WEB: Move to 30% preset");
  }
  sendQuickResponse();
}

static void handlePreset50()
{
  if (state.currentMode == CALIBRATE)
  {
  }
  else
  {
    BA_moveToPercent(50);
    DPRINTLN("WEB: Move to 50% preset");
  }
  sendQuickResponse();
}

static void handlePreset70()
{
  if (state.currentMode == CALIBRATE)
  {
  }
  else
  {
    BA_moveToPercent(70);
    DPRINTLN("WEB: Move to 70% preset");
  }
  sendQuickResponse();
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
  sendQuickResponse();
}

// Use encapsulated calibration save routines from Buttons namespace

static void handleSaveTop()
{
  if (state.currentMode == CALIBRATE)
  {
    DPRINTLN("WEB: Save TOP position");
    Buttons::calibrationSaveTop();
  }
  sendQuickResponse();
}

static void handleSaveBottom()
{
  if (state.currentMode == CALIBRATE)
  {
    DPRINTLN("WEB: Save BOTTOM position");
    Buttons::calibrationSaveBottom();
  }
  sendQuickResponse();
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
  server.on("/preset/30", HTTP_POST, handlePreset30);
  server.on("/preset/50", HTTP_POST, handlePreset50);
  server.on("/preset/70", HTTP_POST, handlePreset70);
  server.on("/reboot", HTTP_POST, handleReboot);
  server.begin();
}

void webLoop()
{
  server.handleClient();
}
