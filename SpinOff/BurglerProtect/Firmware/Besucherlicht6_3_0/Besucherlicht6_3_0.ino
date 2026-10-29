// Besucherlicht (Geraet 22, "Besucherlicht", feste IP .188) - Spin-off BurglerProtect
//
// Hoert die visitorObserved des Dome-C1 (Veranda, Welt X>0/Y<0) und schaltet,
// wenn die Anlage scharf ist und es dunkel genug ist, ueber die Hue Bridge im LAN
// die gewaehlten Gruppen ein - so sieht es aus, als waere jemand zu Hause.
//   * Regel wie VPS visitor.py: Spur, Polygon, min_events in window_s ueber
//     min_span_s; retriggerbar (jede Meldung im Polygon verlaengert auf +on_s).
//   * Brennt schon eine der Zuhause-Gruppen, ist jemand da -> nichts tun.
//   * Das Licht "gehoert mir" nur, solange niemand daran etwas aendert; aus-
//     geschaltet wird am Ende nur, was noch genau so brennt.
//   * Aktive Zeit: Sonnenuntergang (+start_off_min) bis end_min (Standard 24:00).
//   * Scharf/Aus: Knopf schaltet um, VPS setzt absolut (stgArmed, NVS).
// Polygon, Regel und Gruppen kommen vom VPS (GET /visitor_device.cfg) und liegen
// im LittleFS - ohne VPS/Manager laeuft das Geraet mit dem letzten Stand weiter.
// Konzept: SpinOff/BurglerProtect/Dokumentation/Konzept.md
#include <M5Unified.h>
#include <xComDef6_3.h>
#include "hwDef.h"
#include "besDef.h"

#define DEBUG true

unsigned long timer;                 // heardBeat

#include <xComProc6_3.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// ---- Konfiguration (vom VPS, Cache im LittleFS) --------------------------------------
VisCfg cfg;
String cfgSource = "Standard";       // "VPS" / "Cache" / "Standard"
unsigned long lastCfgFetch = 0;

// ---- Zustand fuer Anzeige und Logik -----------------------------------------------------
bool  timeValid = false;
bool  hueOk = false;                  // letzte Hue-Abfrage erfolgreich
String hueErr = "";
bool  lightMine = false;              // wir haben eingeschaltet, niemand hat es geaendert
unsigned long offAtMs = 0;            // Licht aus (millis), solange lightMine
time_t lastVisitT = 0;                // letzte Meldung im Polygon (Anzeige)
time_t lastTrigT = 0;                 // letzte Einschaltung (Anzeige)
String lastNote = "";                 // letzte Entscheidung (Anzeige + VPS-Debug)

static void besWifi() {
  WiFi.mode(WIFI_STA);
  IPAddress localIP(192, 168, 0, device[ID].IP), gw(192, 168, 0, 1), sn(255, 255, 255, 0);
  // DNS mitgeben, sonst haengt NTP (pool.ntp.org) - siehe CatIdent
  if (!WiFi.config(localIP, gw, sn, gw)) Serial.println("static IP config failed");
  WiFi.begin(ssid, password);
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED) {
    displayBoot("WLAN ...");
    delay(250);
    if (millis() - t0 > 20000) {                 // Neuanlauf statt Endlosschleife
      WiFi.disconnect(true);
      delay(500);
      WiFi.mode(WIFI_STA);
      WiFi.config(localIP, gw, sn, gw);
      WiFi.begin(ssid, password);
      t0 = millis();
    }
  }
  Serial.println(WiFi.localIP().toString());
}

void setup() {
  auto mc = M5.config();
  M5.begin(mc);
  Serial.begin(115200);
  displayInit();
  besWifi();
  initMcUdp();
  initUnicast();
  initText2Udp();
  setUpOTA();
  displayBoot("Zeit ...");
  configTzTime(time_zone, ntpServer1, ntpServer2);
  {
    unsigned long t0 = millis();
    while (!getLocalTime(&timeinfo, 200) && millis() - t0 < 15000) delay(50);
    timeValid = getLocalTime(&timeinfo, 200);
  }
  initSettings();
  if (!LittleFS.begin(true)) sendUdpTextln("Besucherlicht: LittleFS mount FAILED");
  displayBoot("Konfiguration ...");
  bool cached = cfgLoadFile();
  bool fresh  = cfgFetch();
  cfgSource = fresh ? "VPS" : (cached ? "Cache" : "Standard");
  lastCfgFetch = millis();
  hueCheck();                         // einmal Bridge + Gruppen pruefen (Anzeige)
  sendUdpTextln("Besucherlicht bereit, " + String(settingOn(stgArmed) ? "SCHARF" : "aus") +
                ", Konfiguration: " + cfgSource + " (" + cfgSummary() + "), Hue " +
                (hueOk ? "ok" : "FEHLER " + hueErr) + (timeValid ? "" : ", ohne Uhrzeit"));
  sendSettingsReport();
  timer = millis();
}

void loop() {
  M5.update();
  ArduinoOTA.handle();
  heardBeat();
  if (ucDataReceived) { ucDataReceived = false; handleCommand(lastUcMsg); }
  xMsg m;
  while (mcQueuePop(m)) handleBusMsg(m);
  if (M5.BtnA.wasClicked()) setArmed(!settingOn(stgArmed), "Knopf");
  if (!timeValid && getLocalTime(&timeinfo, 0)) timeValid = true;   // NTP spaeter erreicht
  if (millis() - lastCfgFetch >= CFG_FETCH_MS) {
    lastCfgFetch = millis();
    if (cfgFetch()) cfgSource = "VPS";
  }
  lightTick();                        // Hue-Abfrage, Ausschalten (visitorRule.ino)
  static unsigned long lastHueCheck = 0;
  if (!lightMine && millis() - lastHueCheck >= HUE_IDLE_CHECK_MS) {   // Anzeige "Hue ok" aktuell halten
    lastHueCheck = millis();
    hueCheck();
  }
  displayTick();
}

// Scharf/Aus setzen (Knopf oder VPS). Persistiert und meldet den Zustand.
void setArmed(bool on, const char* via) {
  if (on) mySettings.values |= (uint16_t)(1u << stgArmed);
  else    mySettings.values &= (uint16_t)~(1u << stgArmed);
  saveSettings();
  sendSettingsReport();
  note(String(on ? "SCHARF" : "aus") + " (" + via + ")");
  // Ausgeschaltet = jemand ist da: ein brennendes Licht bleibt an und gehoert ihm.
  if (!on && lightMine) releaseLight("Anlage ausgeschaltet");
  displayForce();
}

// Entscheidung merken (Anzeige) und an den VPS-Debug melden
void note(const String& s) {
  lastNote = s;
  sendUdpTextln("Besucherlicht: " + s);
}

// Unicast-Kommandos (VPS-Steuerung via Manager / Display)
void handleCommand(const xMsg& m) {
  if (m.header.msgCode == commandMsg) {
    cmdPayload c;
    if (getPayload(m, c) && c.cmd == cmdSetSetting && ((uint32_t)c.info >> 1) == stgArmed) {
      setArmed((uint32_t)c.info & 1u, "VPS");   // absolut, kein Umschalten
      return;
    }
    if (getPayload(m, c) && c.cmd == cmdReloadParams) {
      bool ok = cfgFetch();
      if (ok) cfgSource = "VPS";
      lastCfgFetch = millis();
      hueCheck();
      note(ok ? "Konfiguration neu geladen (" + cfgSummary() + ")"
              : "Konfiguration laden FEHLGESCHLAGEN, bleibe bei " + cfgSource);
      return;
    }
  }
  handleCommonMsg(m);
}

// Multicast vom Bus: nur visitorObserved interessiert (plus die gemeinsamen Anfragen)
void handleBusMsg(const xMsg& m) {
  if (handleCommonMsg(m)) return;
  if (m.header.msgCode != visitorObserved || m.header.sender == ID) return;
  posPayload obs;
  if (!getPayload(m, obs) || !obs.worldValid) return;
  visitorFeed(obs.worldX, obs.worldY);
}
