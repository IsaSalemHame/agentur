// agentur: custom firmware for the yellow "Smart Weather Clock" (ESP-12F).
//
// Security:
//  - Only connects to WIFI_SSID (the isolated hotspot on the PC), fixed IP 10.42.0.2.
//  - Never opens outgoing connections. No NTP, DNS lookups, cloud or telemetry.
//  - Updating (/update) and pin configuration (/cfg) require a password (same as the hotspot).
//  - If it can't find the hotspot within 30 s, it starts its own recovery network with the
//    same password, so new firmware can always be uploaded over wifi.
//
// The display's pins are not documented. The defaults are what has been reported for the
// ESP-12F variant of these clocks (DC=GPIO0, RST=GPIO2, backlight=GPIO5 inverted, SPI 13/14),
// but everything can be changed without new firmware via /cfg, so we can experiment.

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>
#include <EEPROM.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <ESP8266mDNS.h>
#include "secrets.h"
#include "scene.h"
#ifndef HOME_SSID
#define HOME_SSID ""
#endif
#ifndef HOME_PSK
#define HOME_PSK ""
#endif

// ---------- Settings stored in EEPROM ----------
struct Cfg {
  uint32_t magic;
  int8_t dc, rst, bl;
  uint8_t blInvert;   // 1 = low = backlight on
  uint8_t spiMode;    // 0 or 3
  uint8_t rotation;   // 0..3
  uint8_t invert;     // invertDisplay
  uint8_t brightness; // 0..255
};
static const uint32_t CFG_MAGIC = 0xC1A0DE02;
static Cfg cfg;

static void cfgDefaults() {
  cfg = {CFG_MAGIC, 0, 2, 5, 1, 3, 0, 1, 200};
}
static void cfgLoad() {
  EEPROM.begin(64);
  EEPROM.get(0, cfg);
  if (cfg.magic != CFG_MAGIC) cfgDefaults();
}
static void cfgSave() {
  EEPROM.put(0, cfg);
  EEPROM.commit();
}

// ---------- Network ----------
static ESP8266WebServer server(80);
static ESP8266HTTPUpdateServer updater;
static bool recoveryAp = false;
static String netName = "";                  // the network the clock is connected to
static bool safeMode = false;   // previous boot crashed: skip the display
static bool restartPending = false;
static uint32_t restartAt = 0;

// ---------- Display and animation ----------
// The drawing itself lives in scene.h, which also runs in the simulator under sim/.
static Adafruit_ST7789 *tft = nullptr;
static scene::Strip strip;                   // 240x40 strip, 19 kB

enum State { IDLE, WORKING, QUESTION, DONE };
static State state = IDLE;
static bool stateDirty = true;
static uint32_t stateSince = 0;
static String note;                          // optional short text from the PC (no longer shown)
static int nWork = 0, nAsk = 0, nDone = 0;   // sessions working / needing an answer / finished in the last 15 min
static uint64_t lastSeq = 0;                 // timestamp (ms) of the last accepted message
static uint32_t lastStateMsg = 0;            // when the PC last sent a state (0 = never)
static uint32_t frameMs = 0, frameMax = 0, frames = 0;

static const char *stateName(State s) {
  switch (s) {
    case WORKING: return "working";
    case QUESTION: return "question";
    case DONE: return "done";
    default: return "idle";
  }
}

static void setBacklight(uint8_t v) {
  if (cfg.bl < 0) return;
  analogWrite(cfg.bl, cfg.blInvert ? 255 - v : v);
}

// Sends a finished strip straight to the display in one SPI call.
// The buffer is little-endian but the display wants big-endian: swap bytes before sending.
static void pushStrip(uint16_t *buf, int x, int y, int w, int h) {
  uint32_t n = (uint32_t)w * h;
  for (uint32_t i = 0; i < n; i++) buf[i] = (buf[i] << 8) | (buf[i] >> 8);
  tft->startWrite();
  tft->setAddrWindow(x, y, w, h);
  SPI.writeBytes((uint8_t *)buf, n * 2);
  tft->endWrite();
}

static bool online() {
  return WiFi.status() == WL_CONNECTED && lastStateMsg && millis() - lastStateMsg < 3UL * 3600000UL;
}

static scene::View makeView(uint32_t now) {
  scene::View v;
  v.mood = state == WORKING ? scene::WORK : state == QUESTION ? scene::ASK : state == DONE ? scene::DONE
           : nDone > 0 ? scene::REST : scene::SLEEP;
  v.t = now;
  v.since = stateSince;
  v.work = nWork; v.done = nDone; v.ask = nAsk;
  v.online = online();
  return v;
}

static uint32_t lastFrame = 0, lastKey = 0xFFFFFFFF;

static void animate() {
  if (!tft) return;
  uint32_t now = millis();
  if (state == DONE && now - stateSince > 20000) { state = IDLE; stateDirty = true; }
  // If a session was interrupted, no "done" ever arrives. No sign of life for 5 min means it is not working.
  if (state == WORKING && now - lastStateMsg > 300000UL) { state = IDLE; nWork = 0; stateDirty = true; }
  // The PC treats finished sessions as stale after 15 min and questions after an hour.
  if (nDone && now - lastStateMsg > 900000UL) { nDone = 0; stateDirty = true; }
  if ((nAsk || state == QUESTION) && now - lastStateMsg > 3600000UL) {
    nAsk = 0;
    if (state == QUESTION) state = IDLE;
    stateDirty = true;
  }
  scene::View v = makeView(now);
  uint32_t key = v.mood | (v.work & 255) << 4 | (v.done & 255) << 12 | (v.ask & 255) << 20 | (uint32_t)v.online << 28;
  if (stateDirty || key != lastKey) {
    if (stateDirty) stateSince = now;
    stateDirty = false;
    lastKey = key;
    v.since = stateSince;
    scene::render(strip, v, scene::FULL, pushStrip);
    setBacklight(v.mood == scene::SLEEP ? cfg.brightness / 3 : cfg.brightness);
    lastFrame = now;
    return;
  }
  uint32_t every = v.mood == scene::SLEEP ? 100 : 40;
  if (now - lastFrame < every) return;
  lastFrame = now;
  scene::Rect r[8];
  int n = scene::animRects(v, r);
  for (int i = 0; i < n; i++) { scene::render(strip, v, r[i], pushStrip); yield(); }
  frameMs = millis() - now;
  if (frameMs > frameMax) frameMax = frameMs;
  frames++;
}

static void initDisplay() {
  analogWriteRange(255);
  analogWriteFreq(1000);
  if (cfg.bl >= 0) { pinMode(cfg.bl, OUTPUT); setBacklight(0); }

  tft = new Adafruit_ST7789(-1, cfg.dc, cfg.rst);
  tft->init(240, 240, cfg.spiMode == 0 ? SPI_MODE0 : SPI_MODE3);
  tft->setSPISpeed(40000000);
  tft->setRotation(cfg.rotation & 3);
  tft->invertDisplay(cfg.invert);
  stateDirty = true;
}

// ---------- HTTP ----------
static bool auth() {
  if (server.authenticate("agentur", WIFI_PSK)) return true;
  server.requestAuthentication();
  return false;
}

static String infoJson() {
  String s = "{\"fw\":\"agentur 9\",\"state\":\"";
  s += stateName(state);
  s += "\",\"heap\":" + String(ESP.getFreeHeap());
  s += ",\"uptime_s\":" + String(millis() / 1000);
  s += ",\"rssi\":" + String(WiFi.RSSI());
  s += ",\"net\":\"" + netName + "\",\"ip\":\"" + WiFi.localIP().toString() + "\"";
  s += ",\"work\":" + String(nWork) + ",\"ask\":" + String(nAsk) + ",\"done\":" + String(nDone);
  s += ",\"frame_ms\":" + String(frameMs) + ",\"frame_max_ms\":" + String(frameMax) + ",\"frames\":" + String(frames);
  s += ",\"recovery\":" + String(recoveryAp ? "true" : "false");
  s += ",\"safe_mode\":" + String(safeMode ? "true" : "false");
  s += ",\"reset_reason\":\"" + ESP.getResetReason() + "\"";
  s += ",\"cfg\":{\"dc\":" + String(cfg.dc) + ",\"rst\":" + String(cfg.rst) + ",\"bl\":" + String(cfg.bl);
  s += ",\"blinv\":" + String(cfg.blInvert) + ",\"mode\":" + String(cfg.spiMode);
  s += ",\"rot\":" + String(cfg.rotation) + ",\"inv\":" + String(cfg.invert);
  s += ",\"bright\":" + String(cfg.brightness) + "}}";
  return s;
}

static void setupHttp() {
  server.on("/", HTTP_GET, []() {
    server.send(200, "text/plain",
                "agentur\n/state?s=idle|working|question|done&w=&q=&v=&n=\n/info\n"
                "/bright?v=0..255\n/cfg?dc=&rst=&bl=&blinv=&mode=&rot=&inv= (password)\n/update (password)\n");
  });
  server.on("/info", HTTP_GET, []() { server.send(200, "application/json", infoJson()); });
  server.on("/state", HTTP_GET, []() {
    String s = server.arg("s");
    State n = state;
    if (s == "idle") n = IDLE;
    else if (s == "working") n = WORKING;
    else if (s == "question") n = QUESTION;
    else if (s == "done") n = DONE;
    else { server.send(400, "text/plain", "unknown state"); return; }
    lastStateMsg = millis();
    // Calls from the PC run in the background and can arrive out of order.
    // A message older than the previous one is discarded. If it is more than a minute
    // older, the PC clock was probably set back, and then it is accepted.
    if (server.hasArg("n")) {
      uint64_t seq = strtoull(server.arg("n").c_str(), nullptr, 10);
      if (seq < lastSeq && lastSeq - seq < 60000) { server.send(200, "text/plain", "stale"); return; }
      lastSeq = seq;
    }
    String nn = server.arg("note");
    int w = server.hasArg("w") ? server.arg("w").toInt() : 0;
    int q = server.hasArg("q") ? server.arg("q").toInt() : 0;
    int v = server.hasArg("v") ? server.arg("v").toInt() : 0;
    note = nn;
    if (n != state) { state = n; stateDirty = true; }   // the counters alone trigger a redraw, not a new animation
    nWork = w; nAsk = q; nDone = v;
    server.send(200, "text/plain", "OK");
  });
  server.on("/bright", HTTP_GET, []() {
    cfg.brightness = constrain(server.arg("v").toInt(), 0, 255);
    cfgSave();
    stateDirty = true;
    server.send(200, "text/plain", "OK");
  });
  server.on("/cfg", HTTP_GET, []() {
    if (!auth()) return;
    auto rd = [](const char *k, int cur) { return server.hasArg(k) ? server.arg(k).toInt() : cur; };
    cfg.dc = rd("dc", cfg.dc);
    cfg.rst = rd("rst", cfg.rst);
    cfg.bl = rd("bl", cfg.bl);
    cfg.blInvert = rd("blinv", cfg.blInvert);
    cfg.spiMode = rd("mode", cfg.spiMode);
    cfg.rotation = rd("rot", cfg.rotation);
    cfg.invert = rd("inv", cfg.invert);
    if (server.hasArg("reset")) cfgDefaults();
    cfgSave();
    server.send(200, "application/json", infoJson());
    restartPending = true;
    restartAt = millis() + 500;
  });
  updater.setup(&server, "/update", "agentur", WIFI_PSK);
  server.begin();
}

// ---------- Wifi: the PC hotspot first, otherwise the home network ----------
static uint32_t lastWifiTry = 0, offlineSince = 0;

static bool tryConnect(const char *ssid, const char *psk, bool lab, uint32_t ms) {
  WiFi.disconnect();
  delay(50);
  if (lab) WiFi.config(IPAddress(10, 42, 0, 2), IPAddress(10, 42, 0, 1), IPAddress(255, 255, 255, 0));
  else WiFi.config(IPAddress(0, 0, 0, 0), IPAddress(0, 0, 0, 0), IPAddress(0, 0, 0, 0));  // DHCP
  WiFi.begin(ssid, psk);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < ms) delay(100);
  if (WiFi.status() != WL_CONNECTED) return false;
  netName = ssid;
  return true;
}

// Scans for the networks and connects to the best one. Blocks for up to about 45 s.
static bool connectBest() {
  int n = WiFi.scanNetworks();
  bool seeLab = false, seeHome = false;
  for (int i = 0; i < n; i++) {
    String id = WiFi.SSID(i);
    if (id == WIFI_SSID) seeLab = true;
    if (strlen(HOME_SSID) && id == HOME_SSID) seeHome = true;
  }
  WiFi.scanDelete();
  if (seeLab && tryConnect(WIFI_SSID, WIFI_PSK, true, 20000)) return true;
  if (seeHome && tryConnect(HOME_SSID, HOME_PSK, false, 20000)) return true;
  netName = "";
  return false;
}

static void startMdns() {
  MDNS.end();
  MDNS.begin("agentur");                      // the PC finds the clock as agentur.local
  MDNS.addService("http", "tcp", 80);
}

static void startRecoveryAp() {
  if (recoveryAp) return;
  recoveryAp = true;
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP("agentur-setup", WIFI_PSK);
}

static void stopRecoveryAp() {
  if (!recoveryAp) return;
  recoveryAp = false;
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
}

static void setupWifi() {
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.hostname("agentur");
  WiFi.setAutoReconnect(true);               // the same network comes back by itself
  if (connectBest()) startMdns();
  else { startRecoveryAp(); offlineSince = millis(); }
}

// Lost the network (e.g. the hotspot disappeared): wait 15 s for it to come back,
// then retry all networks every 30 s. The recovery network stays up meanwhile.
static void wifiWatch() {
  uint32_t now = millis();
  if (WiFi.status() == WL_CONNECTED) { offlineSince = 0; return; }
  if (!offlineSince) offlineSince = now;
  if (now - offlineSince < 15000 || now - lastWifiTry < 30000) return;
  lastWifiTry = now;
  if (connectBest()) { stopRecoveryAp(); startMdns(); offlineSince = 0; }
  else startRecoveryAp();
}

void setup() {
  Serial.begin(115200);
  cfgLoad();
  // If the last run crashed (exception or watchdog), start without the display, so a
  // bug in the display code never causes a restart loop we can't escape over wifi.
  uint32_t rr = ESP.getResetInfoPtr()->reason;
  safeMode = (rr == REASON_EXCEPTION_RST || rr == REASON_SOFT_WDT_RST || rr == REASON_WDT_RST);
  setupWifi();     // network and updating first, so a display fault never locks us out
  setupHttp();
  if (!safeMode) initDisplay();
}

void loop() {
  server.handleClient();
  wifiWatch();
  MDNS.update();
  if (restartPending && (int32_t)(millis() - restartAt) > 0) ESP.restart();
  animate();
  yield();
}
