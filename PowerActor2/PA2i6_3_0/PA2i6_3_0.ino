// =====================================================================
//  PA2i6_3_0 -- PowerActor2 (Geraet 4, .181) im kartengestuetzten Gesamtsystem
// ---------------------------------------------------------------------
//  Hardware wie Development/AfterSerialRebuild/Step5_RelaysIR.
//  - Ausloeser: catDetected des Cat Identifiers (bestaetigte Katze). Danach folgt
//    der Aktor genau diesem Track ueber die catTracked des Identifiers und wartet,
//    bis die Katze in Reichweite / in einem erlaubten Bereich ist.
//  - Zielen in Welt-Koordinaten ueber die eigene Pose (myPose, Drehachse,
//    heading = Richtung von Servo 2048): Welt -> PA-lokal -> Servo + Distanz.
//  - Schuss nur, wenn scharf (readyToFire), keine Feuersperre, Pose gueltig,
//    Ziel in der No-Shot-Karte, 500 mm .. 6,5 m entfernt und die Veranda-Regel
//    erfuellt ist (zielProc.ino). Laser und Ventil gleich (Trockentest = Hahn zu).
//  - Test stgLaserFollow: der Laser allein folgt schon jedem catObserved.
//  - LP40B misst waehrend des Schusses mit (nur Protokoll, keine Bedingung).
//  - Relais LD06/Compact Dome ueber Einstellungen (NVS), Spot bleibt aus.
//  - Pose: manuell per VPS (handleCommonMsg), spaeter die Traegerpose.
// =====================================================================
#include <xComDef6_3.h>
#include "hwDef.h"
#include <Wire.h>
#include <FeetechBus.h>
#include <FeetechST3020.h>

#define DEBUG false
#define ID PA2i

FeetechBus    bus(Serial1);
FeetechST3020 servo(bus, SERVO_ID);

pa2HbPayload hbState;
unsigned long HBtimer;

// Feuersperre (fireInhibit): ablaufend, nur verlaengern, readyToFire unberuehrt
unsigned long inhibitStart = 0, inhibitHold = 0;
bool inhibited() { return inhibitHold && millis() - inhibitStart < inhibitHold; }

bool noShotOK = false, noShotSynced = false, mapRecheckNow = false;
unsigned long lastNoShotTry = 0, mapRecheckAt = 0;

#include <xComProc6_3.h>

void setup() {
  initOutputs();                     // zuerst: Laser/Ventil sicher aus
  Serial.begin(115200);
  setUpWifi(device[ID].IP);
  initMcUdp();
  initUnicast();
  initText2Udp();
  setUpOTA();
  ArduinoOTA.onStart([]() { fireOff(); });   // waehrend OTA nie offen haengen
  configTzTime(time_zone, ntpServer1, ntpServer2);
  if (!LittleFS.begin(true)) Serial << "LittleFS mount FAILED" << endl;
  initSettings();
  applyPower();
  // Gespeicherte (manuelle) Pose gilt weiter: geschossen wird ohnehin erst nach dem
  // Scharfschalten von Hand, also wenn man weiss, wo der Traeger steht.
  myPose.validWorldPose = loadPose(myPose);
  hbState.readyToFire = false;       // nach dem Boot immer gesichert
  hbState.hb.HBperiode = periodeForHB;
  initServo();
  initLidar();
  initIR();
  noShotOK = acquireNoShot(NOSHOT_PATH, device[Manager].IP, MAP_WAIT_MS);
  noShotSynced = mapLastSynced;
  lastNoShotTry = millis();
  sendSettingsReport();
  sendPoseReport();
  sendHBmsg();
  HBtimer = millis();
  sendUdpTextln(statusText("bereit") + ", Neustart-Grund " + resetReasonText());
}

void loop() {
  ArduinoOTA.handle();
  heardBeat();
  if (ucDataReceived) { xMsg m = lastUcMsg; ucDataReceived = false; handleCommand(m); }
  xMsg m;
  while (mcQueuePop(m)) handleBusMsg(m);
  pollLidar();
  pollIR();
  serviceServo();
  serviceNoShotMap();
  engageTick();                      // Zielen + Schuss (aimProc.ino)
}

// Unicast: gemeinsame Kommandos (Einstellungen, manuelle Pose, Neustart), Scharf/Aus
void handleCommand(const xMsg& m) {
  uint16_t before = mySettings.values;
  bool poseBefore = myPose.validWorldPose;
  if (handleCommonMsg(m)) {
    if (mySettings.values != before) applyPower();
    if (myPose.validWorldPose && !poseBefore) sendUdpTextln(statusText("Pose gesetzt"));
    return;
  }
  cmdPayload c;
  if (m.header.msgCode != commandMsg || !getPayload(m, c)) return;
  if (c.cmd == cmdArmFire) {         // info 1 = scharf, 2 = sichern (absolut, VPS);
                                     // 0 = umschalten (Button/Display wie PA1_1)
    hbState.readyToFire = c.info == 1 ? true : c.info == 2 ? false : !hbState.readyToFire;
    if (!hbState.readyToFire) engageStop("entsichert");
    sendUdpTextln(hbState.readyToFire ? "PowerActor2: SCHARF" : "PowerActor2: gesichert");
    sendHBmsg(); sendHBmsg();
  }
}

void handleBusMsg(const xMsg& m) {
  if (handleCommonMsg(m)) return;
  switch (m.header.msgCode) {
    case fireInhibit: {
      fireInhibitPayload inh;
      if (!getPayload(m, inh)) return;
      unsigned long left = inhibited() ? inhibitHold - (millis() - inhibitStart) : 0;
      if (inh.holdMs > left) {
        if (!inhibited()) { sendUdpTextln("PowerActor2: Feuersperre (Person)"); engageStop("Feuersperre"); }
        inhibitStart = millis();
        inhibitHold = inh.holdMs;
      }
      break;
    }
    case catDetected: {
      catDetectedPayload cd;
      if (getPayload(m, cd)) onCatDetected(cd);
      break;
    }
    case catTracked: {               // Nachfuehrung des bestaetigten Tracks (Cat Identifier)
      catDetectedPayload cd;
      if (getPayload(m, cd)) onCatTracked(cd);
      break;
    }
    case catObserved: {              // nur fuer den Laser-Folgemodus (stgLaserFollow)
      posPayload p;
      if (getPayload(m, p) && p.worldValid) onCatObserved(p.worldX, p.worldY);
      break;
    }
    case mapInfo:
      if (mapAnnounceOutdated(m, mapNoShot, NOSHOT_PATH)) {
        noShotOK = false; noShotSynced = false; mapRecheckNow = true;
        mapRecheckAt = millis() + MAP_ANNOUNCE_SLOT_MS * (unsigned long)(ID % 10);
      }
      break;
  }
}

void serviceNoShotMap() {
  if (mapRecheckNow) {
    if ((long)(millis() - mapRecheckAt) < 0) return;
  } else {
    unsigned long dueMs = (noShotOK && noShotSynced) ? MAP_RECHECK_MS : MAP_RETRY_MS;
    if (millis() - lastNoShotTry < dueMs) return;
  }
  lastNoShotTry = millis();
  mapRecheckNow = false;
  noShotOK = acquireNoShot(NOSHOT_PATH, device[Manager].IP, MAP_WAIT_MS);
  noShotSynced = mapLastSynced;
}
