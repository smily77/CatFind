// Cat Cam (Geraet 19, "Cat_Cam", feste IP .185)
//
// Seeed XIAO Vision AI Camera: XIAO ESP32-C3 (dieses Programm) + Grove Vision AI V2
// (Himax WiseEye2 - das KI-Modell laeuft DORT, deployt per SenseCraft-Web) + OV5647.
// Ein normales Geraet des ESP32-Netzes mit zwei Aufgaben:
//
//   1. Bei catDetected auf dem Bus (Cat Identifier): Foto machen und zum VPS
//      hochladen (POST /photo) -> Tab "Bilder" im Dashboard, dort mit Track-Nr.
//   2. Eigene KI-Erkennung (stgCamAi): sieht das Vision-Modul eine Katze, wird
//      catObserved gebroadcastet (worldValid=0 - die Kamera kennt keine Welt-
//      Position, das Ereignis ist Zusatz-Evidenz + Zeitmarke) und beim ersten
//      Auftauchen einer Sichtung ein Foto hochgeladen.
//
// Aktion "Foto jetzt" (cmdTakePhoto, VPS-Steuerung/Display) fuer Test/Ausrichtung.
// WICHTIG: welche Klasse "Katze" ist, haengt vom deployten SenseCraft-Modell ab
// (COCO: 15=cat). Gesehene Klassen werden gedrosselt per Text-Multicast gemeldet,
// damit CAM_TARGET_CAT ohne Raetselraten angepasst werden kann.
// SSCMA VOR hwDef einbinden: die Lib deklariert eine Methode ID() - das
// ID-Makro aus hwDef wuerde diese Deklaration sonst zerstoeren.
#include <Seeed_Arduino_SSCMA.h>
#include <xComDef6_3.h>
#include "hwDef.h"

#define DEBUG true

unsigned long timer;                 // heardBeat
boolean statusLightOn = false;       // (keine LED am XIAO C3 - nur fuers Muster)

#include <xComProc6_3.h>
#include <HTTPClient.h>

SSCMA AI;

// --- Erkennung / Foto-Verhalten ---------------------------------------------------------
#define CAM_TARGET_CAT        15     // Fallback-Klassen-ID "Katze" (COCO: 15) - wird beim
                                     // Start automatisch aus der Klassenliste des deployten
                                     // Modells bestimmt (Eintrag, der "cat" enthaelt)
#define CAM_MIN_SCORE         60     // KI-Score-Schwelle (0..100)
#define CAM_INVOKE_PERIOD_MS  250    // Erkennungstakt (~4 Hz)
#define CAM_OBS_PERIOD_MS     1000   // catObserved hoechstens 1x pro Sekunde
#define CAM_SIGHT_GAP_MS      30000  // so lange keine Katze -> naechste Erkennung = neue
                                     // Sichtung -> neues Foto
#define CAM_DET_PHOTO_MIN_MS  10000  // Foto-Mindestabstand fuer catDetected-Ausloeser
#define CAM_CLASSES_INFO_MS   60000  // gesehene fremde Klassen hoechstens 1x/min melden
#define CAM_RX_BUF            57344  // SSCMA-Empfangspuffer: Bild kommt als Base64 (>16k noetig)

bool aiOk = false;                   // Vision-Modul erreichbar?
int  camTargetCat = CAM_TARGET_CAT;  // aktive Katzen-Klassen-ID (auto aus Modell-Info)
unsigned long lastInvokeMs = 0, lastObsMs = 0, lastSightMs = 0;
unsigned long lastDetPhotoMs = 0, lastClassInfoMs = 0;
unsigned long lastAiInitMs = 0;      // letzter (Re-)Initialisierungsversuch

// Die Modell-Info kommt Base64-codiert (JSON mit name/classes). Fuer die
// automatische Katzen-Klassen-Erkennung hier dekodieren.
static String b64decode(const String& in) {
  auto val = [](char c) -> int {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
  };
  String out; out.reserve(in.length() * 3 / 4 + 2);
  int acc = 0, bits = 0;
  for (size_t i = 0; i < in.length(); i++) {
    int v = val(in[i]);
    if (v < 0) continue;
    acc = (acc << 6) | v; bits += 6;
    if (bits >= 8) { bits -= 8; out += (char)((acc >> bits) & 0xFF); }
  }
  return out;
}

// Katzen-Klasse aus der Modell-Info bestimmen: Index des "classes"-Eintrags,
// der "cat" enthaelt (deckt "cat", "Cat Detection"-Einzelklasse, Pet cat/dog
// und COCO ab). Nicht gefunden -> Fallback CAM_TARGET_CAT.
static void aiResolveCatClass(const String& infoJson) {
  camTargetCat = CAM_TARGET_CAT;
  int cp = infoJson.indexOf("\"classes\"");
  if (cp < 0) return;
  int a = infoJson.indexOf('[', cp);
  int b = infoJson.indexOf(']', a);
  if (a < 0 || b < 0) return;
  int idx = 0, pos = a;
  while (pos < b) {
    int q1 = infoJson.indexOf('"', pos);
    if (q1 < 0 || q1 > b) break;
    int q2 = infoJson.indexOf('"', q1 + 1);
    if (q2 < 0 || q2 > b) break;
    String cls = infoJson.substring(q1 + 1, q2);
    cls.toLowerCase();
    if (cls.indexOf("cat") >= 0 && cls.indexOf("catch") < 0) { camTargetCat = idx; break; }
    idx++;
    pos = q2 + 1;
  }
}

// Vision-Modul (re-)initialisieren. Schlaegt der Boot-Versuch fehl (Modul
// braucht nach dem Einstecken laenger o.ae.), wird periodisch neu probiert -
// vorher blieb "Vision-Modul fehlt" bis zum naechsten Reboot bestehen.
static bool aiInit() {
  aiOk = AI.begin(&Wire);
  if (aiOk) {
    AI.set_rx_buffer(CAM_RX_BUF);
    String info = b64decode(AI.info(false));
    aiResolveCatClass(info);
    // Modellname fuers Debug herausziehen
    String name = "?";
    int np = info.indexOf("\"name\"");
    if (np >= 0) {
      int q1 = info.indexOf('"', info.indexOf(':', np));
      int q2 = info.indexOf('"', q1 + 1);
      if (q1 >= 0 && q2 > q1) name = info.substring(q1 + 1, q2);
    }
    sendUdpTextln("CatCam Vision-Modul OK, Modell: " + name +
                  ", Katzen-Klasse=" + String(camTargetCat) +
                  (info.indexOf("\"classes\"") < 0 ? " (Fallback - keine Klassenliste)" : ""));
  }
  lastAiInitMs = millis();
  return aiOk;
}

static bool aiEnsure() {             // vor jeder Nutzung: notfalls neu verbinden
  if (aiOk) return true;
  if (millis() - lastAiInitMs < 5000) return false;
  return aiInit();
}

// WiFi-Anmeldung wie beim Cat Identifier: feste IP MIT DNS (sonst haengt NTP),
// abgesenkte TX-Leistung (XIAO-Boards) und Neuanlauf statt Endlosschleife.
static void camWifi() {
  WiFi.mode(WIFI_STA);
  IPAddress localIP(192, 168, 0, device[ID].IP), gw(192, 168, 0, 1), sn(255, 255, 255, 0);
  if (!WiFi.config(localIP, gw, sn, gw)) Serial.println("static IP config failed");
  WiFi.begin(ssid, password);
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
  unsigned long tryStart = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
    Serial.print(".");
    if (millis() - tryStart > 20000) {
      Serial.println(" retry, status=" + String((int)WiFi.status()));
      WiFi.disconnect(true);
      delay(500);
      WiFi.mode(WIFI_STA);
      WiFi.config(localIP, gw, sn, gw);
      WiFi.begin(ssid, password);
      WiFi.setTxPower(WIFI_POWER_8_5dBm);
      tryStart = millis();
    }
  }
  Serial.println(WiFi.localIP().toString());
}

void setup() {
  Serial.begin(115200);
  camWifi();                         // feste IP .185
  initMcUdp();
  initUnicast();
  initText2Udp();
  setUpOTA();
  configTzTime(time_zone, ntpServer1, ntpServer2);   // Zeit mit Budget (nicht blockieren)
  {
    unsigned long t0 = millis();
    while (!getLocalTime(&timeinfo, 200) && millis() - t0 < 15000) delay(50);
  }
  Serial.println("Zeit ok, starte Vision-Modul (I2C)...");
  initSettings();
  // Vision AI V2 per I2C; grosser Empfangspuffer, damit das Bild (Base64-JPEG)
  // durchpasst. Bei Misserfolg probiert aiEnsure() spaeter automatisch weiter.
  aiInit();
  Serial.println(String("Vision-Modul: ") + (aiOk ? "OK" : "FEHLT (Retry laeuft)"));
  sendUdpTextln(String("CatCam bereit, Vision-Modul ") +
                (aiOk ? "OK" : "noch nicht erreichbar - probiere weiter"));
  sendSettingsReport();
  timer = millis();
}

void loop() {
  ArduinoOTA.handle();
  heardBeat();
  if (ucDataReceived) { ucDataReceived = false; handleCommand(lastUcMsg); }
  if (mcDataReceived) { mcDataReceived = false; handleBusMsg(lastMcMsg); }
  aiTick();
}

void handleCommand(const xMsg& m) {
  if (handleCommonMsg(m)) return;
  cmdPayload cmd;
  if (!getPayload(m, cmd)) return;
  if (cmd.cmd == cmdTakePhoto) takePhoto("manuell", 0, 0, 0);
}

void handleBusMsg(const xMsg& m) {
  if (handleCommonMsg(m)) return;
  if (m.header.msgCode != catDetected || m.header.sender == ID) return;
  catDetectedPayload cd;
  if (!getPayload(m, cd)) return;
  // Der Identifier sendet jede Detektion DOPPELT (UDP-Verlustschutz) -> dedupen
  static catDetectedPayload lastCd; static unsigned long lastCdMs = 0;
  bool dup = (millis() - lastCdMs < 1500) && memcmp(&lastCd, &cd, sizeof(cd)) == 0;
  lastCd = cd; lastCdMs = millis();
  if (dup) return;
  if (millis() - lastDetPhotoMs < CAM_DET_PHOTO_MIN_MS) return;   // Foto-Flut vermeiden
  lastDetPhotoMs = millis();
  takePhoto("catDetected", cd.score, cd.worldX, cd.worldY);
}

// KI-Takt: Vision-Modul befragen; Katze im Bild -> catObserved (+ Foto bei neuer Sichtung)
void aiTick() {
  if (!settingOn(stgCamAi) || !aiEnsure()) return;
  if (millis() - lastInvokeMs < CAM_INVOKE_PERIOD_MS) return;
  lastInvokeMs = millis();
  // filter=true -> AT+INVOKE=1,0,1 (DIFFERED=0, RESULT_ONLY=1): schnell, nur Boxen.
  // (Die Lib baut das AT-Kommando aus (times, !filter, filter) - RESULT_ONLY=0
  // wuerde bei JEDEM Aufruf das Bild mitschicken und den Takt toeten.)
  if (AI.invoke(1, true, false) != CMD_OK) return;

  int bestScore = -1;
  uint32_t others = 0;                       // fremde Klassen (Bitmaske, IDs 0..31)
  for (auto& b : AI.boxes()) {
    if (b.target == camTargetCat) {
      if (b.score >= CAM_MIN_SCORE && (int)b.score > bestScore) bestScore = b.score;
    } else if (b.target < 32 && b.score >= CAM_MIN_SCORE) {
      others |= (1u << b.target);
    }
  }
  // Konfigurationshilfe: welche Klassen liefert das deployte Modell?
  if (others && millis() - lastClassInfoMs > CAM_CLASSES_INFO_MS) {
    lastClassInfoMs = millis();
    String s = "CatCam sieht Klassen:";
    for (int i = 0; i < 32; i++) if (others & (1u << i)) s += " " + String(i);
    sendUdpTextln(s + " (Katze erwartet als " + String(camTargetCat) + ")");
  }
  if (bestScore < 0) return;

  bool newSight = (lastSightMs == 0) || (millis() - lastSightMs > CAM_SIGHT_GAP_MS);
  lastSightMs = millis();
  if (millis() - lastObsMs >= CAM_OBS_PERIOD_MS) {
    lastObsMs = millis();
    // catObserved als Zusatz-Evidenz: die Kamera kennt keine Welt-Position ->
    // worldValid=0 (Karte/Modell ignorieren es, Liste/Zeitleiste zeigen es);
    // KI-Score wandert ins res-Feld.
    posPayload obs = {};
    obs.res = bestScore;
    obs.sensor = 0;
    broadcastMsg(catObserved, obs);
  }
  if (newSight) takePhoto("ki", bestScore, 0, 0);
}

// Ein Bild vom Vision-Modul holen. Die Lib kann "Event MIT Bild, ohne DIFFERED"
// nicht ausdruecken (invoke koppelt die beiden AT-Parameter als !filter/filter),
// und ihr wait()-Timeout (1 s) ist fuer ~30 kB Base64 ueber I2C zu knapp ->
// AT-Kommando selbst senden und die Antwort mit den oeffentlichen
// available()/read() einsammeln, bis das "image"-Feld komplett ist.
static bool camInvokeImage(String& imgOut, uint32_t timeoutMs) {
  const char* cmd = "AT+INVOKE=1,0,0\r\n";   // N=1, DIFFERED=0, RESULT_ONLY=0 (mit Bild)
  AI.write(cmd, strlen(cmd));
  String acc; acc.reserve(45000);
  unsigned long t0 = millis();
  char buf[257];
  int vs = -1;                                // Startindex des Base64-Werts
  while (millis() - t0 < timeoutMs) {
    int n = AI.available();
    if (n <= 0) { delay(2); continue; }
    if (n > 256) n = 256;
    n = AI.read(buf, n);
    if (n <= 0) { delay(2); continue; }
    buf[n] = 0;
    acc.concat(buf);
    if (vs < 0) {
      int ip = acc.indexOf("\"image\":");
      if (ip >= 0) {
        int q = acc.indexOf('"', ip + 8);
        if (q >= 0) vs = q + 1;
      }
    }
    if (vs >= 0) {
      int ve = acc.indexOf('"', vs);
      if (ve > vs) { imgOut = acc.substring(vs, ve); return true; }
    }
  }
  return false;
}

// Foto machen (invoke mit Bild) und als Base64 zum VPS hochladen (POST /photo).
void takePhoto(const char* trig, int score, int32_t wx, int32_t wy) {
  if (!aiEnsure()) { sendUdpTextln("CatCam Foto: Vision-Modul nicht erreichbar"); return; }
  // Das Modul liefert zum INVOKE den Frame der VORHERIGEN Aufnahme (Ein-Frame-
  // Puffer). Lief laenger kein Invoke (stgCamAi aus), ist dieses Foto beliebig
  // alt - beobachtet: 6 h altes Nachtbild als erstes Morgenfoto. Ein Wegwerf-
  // Invoke ohne Bild schiebt vorab einen frischen Frame in den Puffer.
  AI.invoke(1, true, false);
  String img;
  if (!camInvokeImage(img, 12000) || img.length() < 100) {
    sendUdpTextln("CatCam Foto: kein Bild vom Modul (" + String(img.length()) + " B)");
    return;
  }
  if (WiFi.status() != WL_CONNECTED) return;
  HTTPClient http;
  String url = "http://" + ipVPS.toString() + "/photo?sender=" + String(ID) +
               "&trigger=" + trig + "&score=" + String(score) +
               "&x=" + String(wx) + "&y=" + String(wy);
  if (!http.begin(url)) return;
  http.setTimeout(15000);
  http.addHeader("Content-Type", "text/plain");
  int code = http.POST(img);
  http.end();
  sendUdpTextln("CatCam Foto (" + String(trig) + ", " + String(img.length() / 1024) +
                "kB b64) -> VPS: " + (code == 200 ? "OK" : "HTTP " + String(code)));
}
