//6_3 - umgestellt auf fixen Header + variablen Payload (xComDef6_3/xComProc6_3)
//      Beobachtungen gehen als catObserved mit posPayload raus, sender/timeStamp füllt der Header
//      Rohdaten-Stream fuer die Winkel-Kalibrierung (UDP 4320/4321, siehe initScanStream)
#include <xComDef6_3.h>
#include "hwDef.h"
#include <FastLED.h>

#define DEBUG false
#define containLed
byte ID = LD06;

#ifdef containLed
  CRGB leds[pixelNum];
#endif

// lidar var
char rawData[maxSentenceLength + 10]; // eigentlich 46
bool newLidarData = false;
messDaten mData[dataPointPerSentence];

unsigned long fireTimer;
bool fireOn = false;

posPayload obsData;

#define minAngleC 250
#define maxAngleC 330
#define nearEndC 500
#define farEndC 6500

// Ruhemodus (stgActive) unterstuetzt: AUS = Motor aus, keine catObserved-Meldungen, nur HB.
// Default AN, NICHT persistiert (nach Reboot aktiv). Muss VOR xComProc stehen (initSettings).
#define STG_SUPPORTED (1u<<stgActive)
#define STG_DEFAULT   (1u<<stgActive)
#define STG_PERSIST   0u
#define STG_ACTIONS   0u

#include <xComProc6_3.h>

// Rohdaten-Stream fuer die Winkel-Kalibrierung (PowerActor2/Development/AfterSerialRebuild/
// Step6_CalibTool). Eigenes Test-Protokoll, NICHT xCom: der PC schickt "LD06SCAN" an
// UDP 4320 (Broadcast oder Unicast), danach gehen fuer SCAN_HOLD_MS alle Messpunkte
// (volle 360 Grad, ohne Fenster) gebuendelt an die Absender-IP, Port 4321. Solange der
// Stream laeuft, sendet der LD06 KEINE catObserved (Aktoren feuern sonst darauf).
#define SCAN_REQ_PORT   4320
#define SCAN_DATA_PORT  4321
#define SCAN_HOLD_MS    3000
#define SCAN_MAX_PTS    96
#define SCAN_FLUSH_MS   100

struct __attribute__((packed)) scanPoint {
  uint16_t angle;       // mData.winkel in 1/100 Grad (wie im Sketch: 360 - Lidar-Winkel)
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

bool scanStreaming() {
  return scanRequested && (millis() - scanReqAt < SCAN_HOLD_MS);
}

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
    scanPoint &sp = scanBuf.p[scanBuf.count++];
    sp.angle = (uint16_t)(mData[i].winkel * 100 + 0.5f);
    sp.dist = mData[i].dist;
    sp.quality = mData[i].quality;
  }
  if (millis() - scanBufAt > SCAN_FLUSH_MS) flushScan();
}

void setup(){
  initHw();
  setUpWifi(device[ID].IP);
  initMcUdp();
  initUnicast();
  setUpOTA();
  setUpTime();
  initSettings();                     // Ruhemodus-Setting aus hwDef/NVS (Default aktiv)
  lidarMotor(HIGH);
  initLidar();
  initScanStream();
  sendSettingsReport();               // Einstellungen annoncieren (Display/VPS lernen sie)
}

void loop() {
  ArduinoOTA.handle();
  // (LD06 sendet legacy-bedingt keinen HB - siehe REVIEW_6_3.md; hier nicht nachgeruestet.)

  // Steuer-/Einstellungsnachrichten generisch verarbeiten (cmdReboot/cmdSetSetting per
  // Unicast, settingsRequest per Multicast-Broadcast).
  if (ucDataReceived) { ucDataReceived = false; handleCommonMsg(lastUcMsg); }
  if (mcDataReceived) { mcDataReceived = false; handleCommonMsg(lastMcMsg); }

  // Ruhemodus (stgActive AUS): Motor stoppen -> keine Messung/Meldung, nur HB.
  static bool motorOn = true;
  bool wantMotor = deviceActive();
  if (wantMotor != motorOn) {
    motorOn = wantMotor;
    lidarMotor(wantMotor ? HIGH : LOW);
    sendUdpTextln(String("LD06 ") + (wantMotor ? "aktiv" : "Ruhemodus (Motor aus)"));
  }
  if (!motorOn) return;

  if (Serial2.available()) newLidarData = readLidar();
  if (newLidarData) {
    newLidarData = false;
    if (scanStreaming()) { streamSentence(); return; }   // Kalibrier-Stream statt catObserved
    for (int i=0; i<dataPointPerSentence;i++) {
      if (isDataInRange(mData[i],minAngleC,maxAngleC,nearEndC,farEndC)) {
        obsData.angle = (450 - mData[i].winkel)*2048/180;
        obsData.radius = mData[i].dist;
        int px, py;
        toPaKart(px,py,obsData.angle,obsData.radius);
        obsData.x = px;
        obsData.y = py;
        broadcastMsg(catObserved,obsData);
        /*
        allPixel(0xFF0000);
        fireOn = true;
        fireTimer = millis()+fireDuration;
        */
      }
    }
  }
  if (fireOn) {
    if (millis() > fireTimer) {
      fireOn = false;
      allPixel(0x000000);
    }
  }
}
