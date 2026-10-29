// =====================================================================
//  LD06_6_3_0 -- LDROBOT LD06 als welt-faehiger CatFinder-Sensor
// ---------------------------------------------------------------------
//  Wie C1Lidar6_3_0, aber fuer den PowerActor2-Traeger (frei im Feld, hinten
//  verdeckt der Drehturm ein paar Grad):
//  - 20 s Hintergrund lernen (rundum), Perimeter-Detektion
//  - Welt-Pose per VPS-Lokalisierung (HTTP /localize), KEIN Rueckgriff auf eine
//    alte NVS-Pose (der Traeger wird umgestellt); ohne Pose alle 5 min neu
//  - danach Ruhe (Motor aus, nur HB); Zielerkennung per stgActive zuschaltbar
//  - No-Shot-Karte vom Manager, Treffer nur dort als catObserved (lokal + Welt)
//  - Pixelring (29) zeigt Phase, HB und die Richtung eines Treffers
//  - Rohdaten-Stream fuer die Winkel-Kalibrierung (UDP 4320/4321) bleibt
// =====================================================================
#include <xComDef6_3.h>
#include "hwDef.h"
#include <HTTPClient.h>
#include <FastLED.h>

#define DEBUG false
byte ID = LD06;

CRGB leds[pixelNum];

// --- Lidar-Empfang ---
uint8_t   rawData[sentenceLength];
messDaten mData[dataPointPerSentence];
float     sentenceRawStart = 0;      // LD06-Rohwinkel des Pakets (Umdrehungswechsel)
uint32_t  crcErrors = 0;

// --- Hintergrund-/Perimetermodell (je Bin, wie LidarC1) ---
float    bgMin1[NUM_BINS], bgMin2[NUM_BINS];
uint16_t bgCnt [NUM_BINS];
float    perim [NUM_BINS];
int      perimAnchors = 0, perimOpen = 0;
unsigned long calStartMs = 0;
bool          calibrated = false;

enum InitPhase { PH_WIFI, PH_CALIB, PH_LOCALIZE, PH_NOSHOT, PH_ACTIVE, PH_NOLOC };
InitPhase phase = PH_WIFI;
unsigned long locFailMs = 0;         // letzter erfolgloser Lokalisierungsversuch

// --- Detektion / Anzeige ---
unsigned long lastDetectMs  = 0;
float         lastDirDeg    = 0;
bool          lastDetectReported = false;   // letztes Ziel in der No-Shot-Karte (gemeldet)?
bool          noShotOK      = false;
unsigned long lastHBms      = 0;
unsigned long hbBlinkUntil  = 0;
unsigned long lastNoShotTry = 0;
bool          noShotSynced  = false;
bool          mapRecheckNow = false;
unsigned long mapRecheckAt  = 0;

#define USE_VPS_LOCALIZE
#include <xComProc6_3.h>

// ---------------------------------------------------------------------
// Rohdaten-Stream fuer die Winkel-Kalibrierung (PowerActor2/Development/
// AfterSerialRebuild/Step6_CalibTool). Eigenes Test-Protokoll, NICHT xCom: der PC
// schickt "LD06SCAN" an UDP 4320, danach gehen SCAN_HOLD_MS lang alle Messpunkte
// gebuendelt an die Absender-IP, Port 4321. Waehrenddessen keine catObserved.
#define SCAN_REQ_PORT   4320
#define SCAN_DATA_PORT  4321
#define SCAN_HOLD_MS    3000
#define SCAN_MAX_PTS    96
#define SCAN_FLUSH_MS   100

struct __attribute__((packed)) scanPoint {
  uint16_t angle;       // mData.winkel in 1/100 Grad
  uint16_t dist;        // mm
  uint8_t  quality;
};
struct __attribute__((packed)) scanPacket {
  char      magic[2];   // 'L','6'
  uint16_t  seq;
  uint8_t   count;
  scanPoint p[SCAN_MAX_PTS];
};

AsyncUDP udpScan;
IPAddress scanIp;
volatile unsigned long scanReqAt = 0;
volatile bool scanRequested = false;
scanPacket scanBuf;
unsigned long scanBufAt = 0;

void initScanStream() {
  if (udpScan.listen(SCAN_REQ_PORT)) {
    udpScan.onPacket([](AsyncUDPPacket packet) {
      if (packet.length() != 8 || memcmp(packet.data(), "LD06SCAN", 8) != 0) return;
      scanIp = packet.remoteIP();
      scanReqAt = millis();
      scanRequested = true;
    });
  }
}

bool scanStreaming() { return scanRequested && (millis() - scanReqAt < SCAN_HOLD_MS); }

void flushScan() {
  if (scanBuf.count) {
    scanBuf.magic[0] = 'L'; scanBuf.magic[1] = '6';
    udpScan.writeTo((const uint8_t*)&scanBuf, 5 + scanBuf.count * sizeof(scanPoint), scanIp, SCAN_DATA_PORT);
    scanBuf.seq++;
  }
  scanBuf.count = 0;
  scanBufAt = millis();
}

void streamSentence() {
  if (scanBuf.count + dataPointPerSentence > SCAN_MAX_PTS) flushScan();
  for (int i = 0; i < dataPointPerSentence; i++) {
    scanPoint& sp = scanBuf.p[scanBuf.count++];
    sp.angle = (uint16_t)(mData[i].winkel * 100 + 0.5f);
    sp.dist = mData[i].dist;
    sp.quality = mData[i].quality;
  }
  if (millis() - scanBufAt > SCAN_FLUSH_MS) flushScan();
}
// ---------------------------------------------------------------------

static inline int angleToBin(float angle) {
  int b = (int)(angle + 0.5f);
  if (b < 0)         b += NUM_BINS;
  if (b >= NUM_BINS) b -= NUM_BINS;
  return b;
}

void sendHB() {
  hbPayload hb; hb.ip = getLastIpByte(); hb.HBperiode = periodeForHB;
  broadcastMsg(HB, hb);
  lastHBms = millis();
  if (settingOn(stgHbLed)) hbBlinkUntil = millis() + HB_BLINK_MS;
}

void setup() {
  initHw();
  setUpWifi(device[ID].IP);          // LD06: device-IP 0 -> DHCP; Ring blinkt rot beim Verbinden
  initMcUdp();
  initUnicast();
  initText2Udp();
  setUpOTA();
  if (!LittleFS.begin(true)) Serial << "LittleFS mount FAILED" << endl;
  configTzTime(time_zone, ntpServer1, ntpServer2);   // Uhrzeit nur fuer Zeitstempel, nicht blockieren
  initSettings();
  myPose.validWorldPose = false;     // keine alte Pose: der Traeger kann umgestellt worden sein
  initScanStream();
  initLidar();
  startCalibration();                // -> PH_CALIB (blau), Motor an
  sendHB();
  sendSettingsReport();
}

void loop() {
  ArduinoOTA.handle();
  if (millis() - lastHBms >= periodeForHB) sendHB();

  if (ucDataReceived) { xMsg um = lastUcMsg; ucDataReceived = false; handleCommand(um); }
  if (mcDataReceived) {
    xMsg cm = lastMcMsg; mcDataReceived = false;
    if (!handleCommonMsg(cm) && mapAnnounceOutdated(cm, mapNoShot, NOSHOT_PATH)) {
      noShotOK = false; noShotSynced = false; mapRecheckNow = true;
      mapRecheckAt = millis() + MAP_ANNOUNCE_SLOT_MS * (unsigned long)(ID % 10);
    }
  }

  serviceNoShotMap();
  serviceActivation();               // Zielerkennung zu-/abgeschaltet, Pose-Wiederholung
  updateLeds();

  // Motor: waehrend Lernen/Lokalisieren immer, danach nur mit Zielerkennung (stgActive)
  // oder solange das Kalibrier-Tool Rohdaten will
  bool wantMotor = !calibrated || (phase == PH_ACTIVE && deviceActive()) || scanStreaming();
  setMotor(wantMotor);
  if (!motorOn()) return;

  while (Serial2.available()) {
    if (!readLidar()) continue;
    if (scanStreaming()) { streamSentence(); continue; }   // Kalibrier-Stream statt Auswertung
    processSentence();
  }
}

// Unicast-Kommandos: gemeinsame zuerst, dann "Kalibrieren" = neu lernen + lokalisieren
void handleCommand(const xMsg& m) {
  if (handleCommonMsg(m)) return;
  cmdPayload c;
  if (m.header.msgCode == commandMsg && getPayload(m, c) && c.cmd == cmdCalibrate) {
    sendUdpTextln("LD06: Hintergrund neu lernen + Pose neu bestimmen");
    startCalibration();
  }
}

// Ein Paket (12 Punkte) auswerten: Hintergrund lernen bzw. Ziele markieren; beim
// Umdrehungswechsel die letzte Umdrehung auswerten.
void processSentence() {
  static float lastRaw = -1;
  static int   flagged = 0;
  static float sumLx = 0, sumLy = 0, sumCos = 0, sumSin = 0;

  bool newRev = lastRaw >= 0 && sentenceRawStart < lastRaw - 180.0f;   // Rohwinkel springt 359 -> 0
  lastRaw = sentenceRawStart;

  if (calibrated && phase == PH_ACTIVE && deviceActive() && newRev) {
    if (flagged >= MIN_DETECT_POINTS) reportTarget(sumLx / flagged, sumLy / flagged, sumCos, sumSin);
    flagged = 0; sumLx = sumLy = sumCos = sumSin = 0;
  }

  for (int i = 0; i < dataPointPerSentence; i++) {
    const messDaten& d = mData[i];
    if (d.dist < NEAR_LIMIT_MM || d.quality < MIN_QUALITY) continue;
    const int bin = angleToBin(d.winkel);
    if (!calibrated) {                                   // Phase 1: Hintergrund lernen
      const float r = d.dist;
      if (r < bgMin1[bin])      { bgMin2[bin] = bgMin1[bin]; bgMin1[bin] = r; }
      else if (r < bgMin2[bin]) { bgMin2[bin] = r; }
      bgCnt[bin]++;
      continue;
    }
    if (phase != PH_ACTIVE || !deviceActive()) continue;
    if (d.dist <= perim[bin] - PERIM_MARGIN_MM) {        // naeher als der Hintergrund -> Ziel
      const float a = d.winkel * DEG2RAD;
      sumLx += d.dist * cosf(a);
      sumLy += d.dist * sinf(a);
      sumCos += cosf(a); sumSin += sinf(a);
      flagged++;
    }
  }
  if (!calibrated && millis() - calStartMs >= CAL_TIME_MS) finishInit();
}

// Schwerpunkt einer Umdrehung -> Welt; nur im schiessbaren Bereich melden.
void reportTarget(float lxc, float lyc, float sumCos, float sumSin) {
  int wx, wy;
  localToWorld(lroundf(lxc), lroundf(lyc), myPose, wx, wy);
  float deg = atan2f(sumSin, sumCos) * RAD2DEG;
  if (deg < 0) deg += 360.0f;
  lastDirDeg = deg;                  // Ring zeigt jedes Ziel: rot = gemeldet, orange = ausserhalb
  lastDetectMs = millis();
  lastDetectReported = insideNoShot(wx, wy);
  if (!lastDetectReported) return;
  posPayload pos;
  pos.x = (int32_t)lroundf(lxc);
  pos.y = (int32_t)lroundf(lyc);
  float phi, rad;
  toPaPol(pos.x, pos.y, phi, rad);
  pos.angle = phi; pos.radius = rad;
  pos.targetSpeed = 0;
  pos.res = 0;
  pos.worldX = wx; pos.worldY = wy; pos.worldValid = 1;
  pos.sensor = 0;
  broadcastMsg(catObserved, pos);
}
