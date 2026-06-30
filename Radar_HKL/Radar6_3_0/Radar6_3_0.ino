// under development
//6_3 - umgestellt auf fixen Header + variablen Payload (xComDef6_3/xComProc6_3)
//      HB geht als radarHbPayload (inkl. Totzone) raus, Beobachtungen als posPayload
//#define  CompactDomeDevice
//#define  MiniDomeDevice
#define  DomeDevice

#include <xComDef6_3.h>

#include "hwDef.h"
#include <FastLED.h>

#define DEBUG true

// Co-Observation-Kalibrierung: dieses Geraet bestimmt seine Welt-Pose nicht selbst,
// sondern per gemeinsamer Beobachtung einer Person mit einem welt-posierten Sensor.
#define USE_VPS_CALIBRATE

#ifdef containLed
  CRGB leds[pixelNum];
#endif

unsigned long timer;
boolean statusLightOn = false;

posPayload obsData;

// --- Kalibrier-/Health-Parameter ------------------------------------------------------
#define CALIB_WINDOW_MS   45000   // Sammelfenster (Knopf wie Auto)
#define CALIB_MIN_INLIER  0.6f    // Quality-Gate (zusaetzlich zu confidence==HOCH)
#define AUTO_ARM_MS        3000   // so lange muss Co-Observation anliegen, bis Auto startet
#define CONCURRENT_MS      1500   // Radar-Target UND Welt-Beobachtung gelten als "gleichzeitig"
#define HEALTH_GATE_MM      600   // Welt-Residuum, ab dem die Pose als schlecht gilt
#define HEALTH_ASSOC_MM    1500   // groessere Distanz = anderes Ziel -> nicht bewerten
#define HEALTH_MAX_BAD        8   // so viele schlechte Messungen in Folge -> Pose verwerfen

int32_t  lastTargetX = 0, lastTargetY = 0;   // juengste eigene Detektion (fuer Health-Check)
unsigned long lastTargetMs = 0;              // wann zuletzt ein Radar-Target aktiv war
unsigned long lastWorldObsMs = 0;            // wann zuletzt eine welt-valide catObserved kam
unsigned long autoArmMs = 0;                 // seit wann Co-Observation anliegt (0 = nicht)

#include <xComProc6_3.h>

// coCalState/coHealth sind in xComProc6_3.h definiert -> Instanzen erst nach dem Include.
coCalState calib;                 // Sammelpuffer (Eigen-Spuren + Welt-Quellen je Sender)
coHealth   health;                // laufende Re-Validierung der Pose

void setup() {
  Serial.begin(115200);
  initPixel();
  setUpWifi(device[ID].IP);
  initMcUdp();
  initUnicast();
  initText2Udp();
  setUpOTA();
  setUpTime();
  loadPose(myPose);   // NVS-Pose laden (validWorldPose bleibt false bis Co-Observation bestaetigt)
  Serial2.begin(S2_baud,SERIAL_8N1,S2_RX,S2_TX);
  timer = millis();  //heardBeat
}

void loop() {
  ArduinoOTA.handle();
  heardBeat();
  if (ucDataReceived) { ucDataReceived = false; handleCommand(lastUcMsg); }
  if (mcDataReceived) { mcDataReceived = false; handleObservation(lastMcMsg); }
  if (Serial2.available()) readRadar();
  if (newDataReady) { processTargets(); newDataReady = false; }
  serviceCalibration();
}

// Kommando per Unicast: Kalibrierfenster per Knopf/commandMsg starten.
void handleCommand(const xMsg& m) {
  cmdPayload cmd;
  if (!getPayload(m, cmd)) return;
  if (cmd.cmd == cmdCalibrate) {
    unsigned long win = (cmd.info > 0) ? (unsigned long)cmd.info : CALIB_WINDOW_MS;
    coCalibBegin(calib, win, false);
    setPixel(minPix, 0xFF00FF);   // Kalibriermodus sichtbar
    sendUdpTextln("calib Knopf: Fenster " + String(win / 1000) + "s");
  }
}

// catObserved vom Bus: welt-posierte Beobachtungen als Referenz nutzen
// (im Fenster sammeln; sonst Health-Check der eigenen Pose).
void handleObservation(const xMsg& m) {
  if (m.header.msgCode != catObserved || m.header.sender == ID) return;
  posPayload obs;
  if (!getPayload(m, obs) || !obs.worldValid) return;   // nur welt-valide Quellen taugen
  lastWorldObsMs = millis();

  if (calib.active) {                                    // im Fenster: Welt-Bahn je Sender sammeln
    coCalibFeedWorld(calib, m.header.sender, obs.worldX, obs.worldY);
    return;
  }
  if (myPose.validWorldPose && (millis() - lastTargetMs < CONCURRENT_MS)) {
    bool drift = coObserveCheck(health, lastTargetX, lastTargetY, obs.worldX, obs.worldY,
                                HEALTH_GATE_MM, HEALTH_ASSOC_MM, HEALTH_MAX_BAD);
    if (drift) {
      myPose.validWorldPose = false; savePose(myPose);
      health = coHealth();
      sendUdpTextln("Pose-Drift erkannt -> validWorldPose=false");
    }
  }
}

// Radar-Frame auswerten: catObserved (mit Welt-Koordinaten, falls Pose gueltig) senden,
// im Kalibrierfenster die eigenen Bahnen sammeln, juengstes Target fuer Health-Check merken.
void processTargets() {
  boolean validTarget = false;
  for (int i = 0; i < 3; i++) {
    if (target[i].active && (target[i].l > deadZone)) {
      validTarget = true;
      obsData.x = target[i].x;
      obsData.y = target[i].y;
      obsData.radius = target[i].l;
      obsData.angle = target[i].phi;
      obsData.sensor = i;
      obsData.targetSpeed = target[i].geschw;
      obsData.res = target[i].res;
      fillWorld(obsData);                              // worldX/worldY/worldValid aus myPose
      broadcastMsg(catObserved, obsData);
      if (calib.active) coCalibFeedLocal(calib, i, target[i].x, target[i].y);
      lastTargetX = target[i].x; lastTargetY = target[i].y; lastTargetMs = millis();
    }
  }
  setPixel(maxPix, validTarget ? 0x0000FF : 0x000000);
}

// Kalibrierfenster abschliessen bzw. Auto-Trigger bedienen.
void serviceCalibration() {
  if (coCalibElapsed(calib)) {                          // Fenster zu Ende -> VPS + Quality-Gate
    setPixel(minPix, 0xFFFF00);                         // "rechnet"
    bool ok = coCalibFinish(calib, CALIB_MIN_INLIER);
    setPixel(minPix, ok ? 0x00FF00 : 0xFF0000);
    autoArmMs = 0;
    return;
  }
  if (calib.active) return;                             // laeuft noch

  // Auto-Trigger: keine valide Pose + anhaltende Co-Observation (eigenes Target UND
  // welt-valide Beobachtung gleichzeitig) -> Kalibrierung selbsttaetig starten.
  if (!myPose.validWorldPose) {
    bool concurrent = (millis() - lastWorldObsMs < CONCURRENT_MS) &&
                      (millis() - lastTargetMs   < CONCURRENT_MS);
    if (concurrent) {
      if (autoArmMs == 0) autoArmMs = millis();
      else if (millis() - autoArmMs > AUTO_ARM_MS) {
        coCalibBegin(calib, CALIB_WINDOW_MS, true);
        setPixel(minPix, 0xFF00FF);
        sendUdpTextln("auto-calib gestartet (Co-Observation)");
        autoArmMs = 0;
      }
    } else {
      autoArmMs = 0;
    }
  }
}
