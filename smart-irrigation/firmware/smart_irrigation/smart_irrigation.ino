/*
  SMART AI IRRIGATION  -  ESP32 only
  Runs on the ESP32 alone, next to the Smart Safe (the safe keeps the ATmega32 and the LCD).
  Checks the irrigation WATER (salinity with the TDS sensor), the plant cup water level,
  the air (DHT11) and the light (LDR). A trained decision tree (ai_model.h) decides:
     0 = WAIT, 1 = IRRIGATE (pump on), 2 = ALERT (water too salty, over 800 ppm -> pump blocked)
  The phone is the screen: connect to WiFi "SmartIrrigation" (password 12345678),
  then open http://192.168.4.1

  Library (Tools > Manage Libraries):
    - DHT sensor library  by Adafruit  (press "Install all" so it also installs Adafruit Unified Sensor)
  Keep ai_model.h in the same folder as this file.
*/

#include <WiFi.h>
#include <WebServer.h>
#include <DHT.h>
#include "ai_model.h"

// ---------- PINS ----------
const int PIN_TDS    = 35;   // TDS board A (analog)
const int PIN_LEVEL  = 34;   // HW-080 board AO (analog), probe in the plant cup
const int PIN_DHT    = 4;    // DHT11 DATA
const int PIN_LDR    = 32;   // LDR + 10k divider (analog). More light = higher number
const int PIN_RELAY  = 26;   // relay board IN1 -> pump
const int PIN_BUZZER = 23;   // optional
const int PIN_LED_GREEN = 18; // optional: pump running (with a 220 ohm resistor)
const int PIN_LED_RED   = 19; // optional: alert (with a 220 ohm resistor)

// ---------- SETTINGS ----------
const bool RELAY_ACTIVE_HIGH = true;   // RM4V relay board turns ON with HIGH (tested). If the pump works the opposite way, set false
int LEVEL_DRY = 4095;    // raw value with the probe in air         (see "raw" on the phone page)
int LEVEL_WET = 1500;    // raw value with the probe deep in water   (see "raw" on the phone page)
const unsigned long PUMP_PULSE_MS = 3000;   // pump runs 3 s, then waits and measures again
const unsigned long SETTLE_MS     = 8000;   // wait for the water to settle
const char *AP_SSID = "SmartIrrigation";
const char *AP_PASS = "12345678";

DHT dht(PIN_DHT, DHT11);
WebServer server(80);

float tds = 0, level = 0, temp = 25, hum = 50, light = 50;
int   rawLevel = 0;
int   decision = 0;
bool  autoMode = true;
bool  pumpOn = false;
unsigned long pumpStarted = 0, lastPumpStop = 0, lastRead = 0;
const char *NAMES[] = {"WAIT", "IRRIGATE", "ALERT"};

// ---------- SENSORS ----------
int readMedianMv(int pin) {
  const int N = 21;
  int v[N];
  for (int i = 0; i < N; i++) { v[i] = analogReadMilliVolts(pin); delay(5); }
  for (int i = 0; i < N - 1; i++)
    for (int j = i + 1; j < N; j++)
      if (v[j] < v[i]) { int t = v[i]; v[i] = v[j]; v[j] = t; }
  return v[N / 2];
}

float readTds(float waterTemp) {
  float volt = readMedianMv(PIN_TDS) / 1000.0;
  float comp = 1.0 + 0.02 * (waterTemp - 25.0);       // temperature compensation
  float cv = volt / comp;
  float ppm = (133.42 * cv * cv * cv - 255.86 * cv * cv + 857.39 * cv) * 0.5;
  return ppm < 0 ? 0 : ppm;
}

float readLevel() {
  rawLevel = analogRead(PIN_LEVEL);
  float pct = (float)(LEVEL_DRY - rawLevel) * 100.0 / (LEVEL_DRY - LEVEL_WET);
  return constrain(pct, 0, 100);
}

float readLight() {
  return constrain(analogRead(PIN_LDR) * 100.0 / 4095.0, 0, 100);   // 0 = dark, 100 = strong light
}

void readSensors() {
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  if (!isnan(t)) temp = t;
  if (!isnan(h)) hum = h;
  tds = readTds(temp);           // water temp ~ air temp (no water thermometer)
  level = readLevel();
  light = readLight();
  decision = aiDecide(level, tds, temp, hum, light);
  // CSV line for training: tray_level,tds,temp,humidity,light,label
  Serial.printf("%.1f,%.0f,%.1f,%.0f,%.0f,%d   raw_level=%d\n", level, tds, temp, hum, light, decision, rawLevel);
}

// ---------- PUMP ----------
void setPump(bool on) {
  if (on == pumpOn) return;
  pumpOn = on;
  digitalWrite(PIN_RELAY, (on == RELAY_ACTIVE_HIGH) ? HIGH : LOW);
  digitalWrite(PIN_LED_GREEN, on ? HIGH : LOW);
  if (on) pumpStarted = millis(); else lastPumpStop = millis();
}

void autoControl() {
  unsigned long now = millis();
  if (decision == 2) { setPump(false); return; }                 // never pump salty water
  if (pumpOn && now - pumpStarted > PUMP_PULSE_MS) setPump(false);
  else if (!pumpOn && decision == 1 && now - lastPumpStop > SETTLE_MS) setPump(true);
}

// ---------- PHONE PAGE ----------
const char PAGE[] PROGMEM = R"HTML(
<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Smart Irrigation</title>
<style>
body{font-family:sans-serif;background:#0f1d17;color:#eaf5ee;margin:0;padding:16px}
h1{font-size:22px;margin:0 0 12px}
.grid{display:grid;grid-template-columns:1fr 1fr;gap:10px}
.card{background:#1a2e25;border-radius:14px;padding:14px}
.v{font-size:28px;font-weight:bold}.l{opacity:.7;font-size:13px}
#dec{font-size:30px;font-weight:bold;text-align:center;padding:18px;border-radius:14px;margin:12px 0}
button{width:100%;padding:14px;font-size:17px;border:0;border-radius:12px;margin-top:8px;background:#2f6b4f;color:#fff}
</style></head><body>
<h1>Smart AI Irrigation</h1>
<div id="dec">...</div>
<div class="grid">
 <div class="card"><div class="l">Water salinity (TDS)</div><div class="v" id="tds">-</div><div class="l" id="q"></div></div>
 <div class="card"><div class="l">Plant cup water level</div><div class="v" id="lvl">-</div><div class="l" id="raw"></div></div>
 <div class="card"><div class="l">Temperature</div><div class="v" id="t">-</div></div>
 <div class="card"><div class="l">Humidity</div><div class="v" id="h">-</div></div>
 <div class="card" style="grid-column:span 2"><div class="l">Light (LDR)</div><div class="v" id="lt">-</div><div class="l" id="lq"></div></div>
</div>
<div class="card" style="margin-top:10px">Pump: <b id="p">-</b> &nbsp; Mode: <b id="m">-</b>
<button onclick="go('/mode')">Switch Auto / Manual</button>
<button id="pb" onclick="go('/pump')">Pump ON / OFF (manual)</button></div>
<div class="l" style="margin-top:10px">AI: decision tree running on the ESP32 (no internet needed)</div>
<script>
function go(u){fetch(u).then(r=>r.json()).then(show)}
function show(d){
 tds.textContent=d.tds+' ppm'; lvl.textContent=d.level+' %'; raw.textContent='raw '+d.raw;
 t.textContent=d.temp+' °C'; h.textContent=d.hum+' %';
 lt.textContent=d.light+' %'; lq.textContent=d.light>70?'Strong sun: water later':d.light<15?'Dark':'Normal light';
 q.textContent=d.tds<450?'Good water':d.tds<=800?'Slightly salty':'Too salty!';
 var c={WAIT:'#3a4d44',IRRIGATE:'#1f7a4d',ALERT:'#a33'}[d.dec];
 dec.textContent='AI: '+d.dec; dec.style.background=c;
 p.textContent=d.pump?'ON':'OFF'; m.textContent=d.auto?'AUTO':'MANUAL';
 pb.disabled=d.auto; pb.style.opacity=d.auto?.4:1;
}
setInterval(()=>go('/data'),2000); go('/data');
</script></body></html>)HTML";

void sendJson() {
  char buf[200];
  snprintf(buf, sizeof(buf),
    "{\"tds\":%.0f,\"level\":%.0f,\"raw\":%d,\"temp\":%.1f,\"hum\":%.0f,\"light\":%.0f,\"dec\":\"%s\",\"pump\":%s,\"auto\":%s}",
    tds, level, rawLevel, temp, hum, light, NAMES[decision], pumpOn ? "true" : "false", autoMode ? "true" : "false");
  server.send(200, "application/json", buf);
}

void setupWeb() {
  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.print("Open http://"); Serial.println(WiFi.softAPIP());
  server.on("/", []() { server.send_P(200, "text/html; charset=utf-8", PAGE); });
  server.on("/data", sendJson);
  server.on("/mode", []() { autoMode = !autoMode; setPump(false); sendJson(); });
  server.on("/pump", []() {
    if (!autoMode && decision != 2) setPump(!pumpOn);   // salty water stays blocked
    sendJson();
  });
  server.begin();
}

// ---------- SETUP / LOOP ----------
void setup() {
  Serial.begin(115200);
  pinMode(PIN_RELAY, OUTPUT);
  digitalWrite(PIN_RELAY, RELAY_ACTIVE_HIGH ? LOW : HIGH);   // pump off at start
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_LED_GREEN, OUTPUT);
  pinMode(PIN_LED_RED, OUTPUT);
  analogSetAttenuation(ADC_11db);                            // read up to ~3.1 V
  dht.begin();
  setupWeb();
  Serial.println("tray_level,tds,temp,humidity,light,label");
}

void loop() {
  server.handleClient();
  unsigned long now = millis();

  if (now - lastRead > 2000) {          // read every 2 s
    lastRead = now;
    int before = decision;
    readSensors();
    digitalWrite(PIN_LED_RED, decision == 2 ? HIGH : LOW);
    if (decision == 2 && before != 2) {  // new alert: beep 3 times
      for (int i = 0; i < 3; i++) { digitalWrite(PIN_BUZZER, HIGH); delay(150); digitalWrite(PIN_BUZZER, LOW); delay(100); }
    }
  }

  if (autoMode) autoControl();
  else if (pumpOn && decision == 2) setPump(false);
}
