// under development
//6_3 - umgestellt auf fixen Header + variablen Payload (xComDef6_3/xComProc6_3)
//      HB geht als radarHbPayload (inkl. Totzone) raus, Beobachtungen als posPayload
//#define  CompactDomeDevice
//#define  MiniDomeDevice
//#define  DomeDevice
#define MiniDomeDevice

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

// RasenKarte (Rasen-Umriss in Welt-mm, vom Manager per mapRasen bezogen): mit gueltiger
// Welt-Pose werden nur Ziele INNERHALB des Rasens gemeldet (Nachbargrundstueck/Strasse
// im 7-m-Radarkegel erzeugen sonst Dauer-Stoerungen). Ohne Karte/Pose: alles melden.
#define RASEN_PATH        "/rasen.csv"
#define MAP_WAIT_MS        6000   // so lange auf mapInfo/Chunks vom Manager warten
#define MAP_RETRY_MS      60000   // Karte nicht bekommen -> so lange bis zum naechsten Versuch
#define MAP_RECHECK_MS  3600000UL // Fangnetz: auch mit geladener Karte stuendlich neu pruefen
                                  // (ein mapInfo-Announce kann per UDP verlorengehen - kein Resend)

int32_t  lastTargetX = 0, lastTargetY = 0;   // juengste eigene Detektion (fuer Health-Check)
unsigned long lastTargetMs = 0;              // wann zuletzt ein Radar-Target aktiv war
unsigned long lastWorldObsMs = 0;            // wann zuletzt eine welt-valide catObserved kam
unsigned long autoArmMs = 0;                 // seit wann Co-Observation anliegt (0 = nicht)
bool     rasenLoaded = false;                // RasenKarte im RAM (insideNoShot nutzbar)
unsigned long lastMapTry = 0;                // letzter Beschaffungsversuch (0 = noch keiner)
bool     mapRecheckNow = false;              // per Announce (mapInfo) sofortiger Re-Check angefordert

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
  initSettings();                    // Anzeige-/Automatik-Settings aus NVS (STG_* aus hwDef)
  // NVS-Pose laden. Das Radar wird im Normalfall NICHT bewegt (Aus/Ein), darum wird
  // einer vorhandenen gespeicherten Pose direkt vertraut (validWorldPose=true) statt jedes
  // Mal neu zu kalibrieren. Der Health-Check (coObserveCheck) verwirft sie automatisch,
  // sobald ein welt-posierter Sensor mitbeobachtet und zeigt, dass sie nicht mehr stimmt;
  // danach greift der Auto-Trigger und kalibriert neu. Ohne gespeicherte Pose bleibt es false.
  if (loadPose(myPose)) {
    myPose.validWorldPose = true;
    sendUdpTextln("Pose aus NVS uebernommen (x=" + String(myPose.worldX) +
                  " y=" + String(myPose.worldY) + ") - Health-Check prueft");
  }
  if (!LittleFS.begin(true)) sendUdpTextln("LittleFS mount FAILED - kein Karten-Cache");
  Serial2.begin(S2_baud,SERIAL_8N1,S2_RX,S2_TX);
  sendSettingsReport();              // Einstellungen annoncieren (Display/VPS lernen sie)
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
  serviceRasenMap();
}

// Kommando per Unicast: Kalibrierfenster per Knopf/commandMsg starten.
void handleCommand(const xMsg& m) {
  if (handleCommonMsg(m)) return;    // settingsRequest/poseRequest/cmdSetSetting generisch
  cmdPayload cmd;
  if (!getPayload(m, cmd)) return;
  if (cmd.cmd == cmdCopyPose) {      // Aktion: Welt-Pose aus der Gruppe uebernehmen
    setPixel(minPix, 0xFFFF00);
    bool ok = copyPoseFromGroup(1500);
    setPixel(minPix, ok ? 0x00FF00 : 0xFF0000);
  }
  else if (cmd.cmd == cmdCalibrate) {
    unsigned long win = (cmd.info > 0) ? (unsigned long)cmd.info : CALIB_WINDOW_MS;
    coCalibBegin(calib, win, false);
    setPixel(minPix, 0xFF00FF);   // Kalibriermodus sichtbar
    sendUdpTextln("calib Knopf: Fenster " + String(win / 1000) + "s");
  }
  else if (cmd.cmd == cmdClearPose) {
    calib.active = false;                          // evtl. laufendes Fenster abbrechen
    clearPose();                                   // NVS-Pose vergessen
    myPose.validWorldPose = false;
    myPose.worldX = 0; myPose.worldY = 0;
    health = coHealth();                           // Health-Check zuruecksetzen
    setPixel(minPix, 0xFF0000);
    sendUdpTextln("Pose geloescht -> nicht lokalisiert (Neukalibrierung noetig)");
  }
}

// catObserved vom Bus: welt-posierte Beobachtungen als Referenz nutzen
// (im Fenster sammeln; sonst Health-Check der eigenen Pose).
void handleObservation(const xMsg& m) {
  if (handleCommonMsg(m)) return;    // settingsRequest/poseRequest (Broadcast) generisch
  if (m.header.msgCode == mapInfo) {  // Announce: Manager hat eine Karte angenommen (gwAcceptMap)
    if (mapAnnounceOutdated(m, mapRasen, RASEN_PATH)) { rasenLoaded = false; mapRecheckNow = true; }
    return;
  }
  if (m.header.msgCode != catObserved || m.header.sender == ID) return;
  posPayload obs;
  if (!getPayload(m, obs) || !obs.worldValid) return;   // nur welt-valide Quellen taugen
  lastWorldObsMs = millis();

  if (calib.active) {                                    // im Fenster: Welt-Bahn je Sender sammeln
    coCalibFeedWorld(calib, m.header.sender, obs.worldX, obs.worldY);
    return;
  }
  // Health-Check NUR bei eingeschalteter Auto-Kalibrierung: nur dann kann sich das
  // Radar nach einem Pose-Verwurf selbst wieder kalibrieren. Bei AutoCalib "aus" wuerde
  // ein (ggf. falsch assoziierter) Drift-Verdacht die Pose dauerhaft ungueltig machen.
  if (settingOn(stgAutoCalib) &&
      myPose.validWorldPose && (millis() - lastTargetMs < CONCURRENT_MS)) {
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
// Mit gueltiger Welt-Pose UND geladener RasenKarte werden Ziele AUSSERHALB des Rasens
// nicht gemeldet (Nachbargrundstueck/Strasse); Kalibrier-Sammlung und Health-Check
// arbeiten weiter mit ALLEN Zielen (die Referenzperson koennte am Rand stehen).
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
      if (calib.active) coCalibFeedLocal(calib, i, target[i].x, target[i].y);
      lastTargetX = target[i].x; lastTargetY = target[i].y; lastTargetMs = millis();
      if (obsData.worldValid && rasenLoaded &&
          !insideNoShot(obsData.worldX, obsData.worldY)) continue;   // ausserhalb des Rasens
      broadcastMsg(catObserved, obsData);
    }
  }
  setPixel(maxPix, (validTarget && settingOn(stgCatLed)) ? 0x0000FF : 0x000000);  // Anzeige schaltbar
}

// RasenKarte beschaffen, sobald eine gueltige Welt-Pose da ist (vorher nuetzt sie nichts):
// erst lokaler LittleFS-Cache + Versionsabgleich beim Manager, sonst Download (acquireMap
// blockiert dabei bis MAP_WAIT_MS). Ohne Karte alle MAP_RETRY_MS neuer Versuch; MIT
// geladener Karte zusaetzlich alle MAP_RECHECK_MS ein Fangnetz-Re-Check UND sofort bei
// einem passenden mapInfo-Announce (mapRecheckNow, siehe handleObservation).
void serviceRasenMap() {
  if (!myPose.validWorldPose || calib.active) return;
  unsigned long dueMs = rasenLoaded ? MAP_RECHECK_MS : MAP_RETRY_MS;
  if (!mapRecheckNow && lastMapTry != 0 && millis() - lastMapTry < dueMs) return;
  lastMapTry = millis();
  mapRecheckNow = false;
  rasenLoaded = acquireMap(mapRasen, RASEN_PATH, device[Manager].IP, MAP_WAIT_MS);
  sendUdpTextln(rasenLoaded ? "RasenKarte geladen - melde nur Ziele auf dem Rasen"
                            : "RasenKarte nicht verfuegbar - melde ungefiltert");
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
  if (myPose.validWorldPose) return;                    // Pose schon vorhanden

  // Automatik 1: Welt-Pose aus einem Gruppenmitglied uebernehmen (falls aktiviert). Periodisch
  // versuchen, solange keine gueltige Pose vorliegt (blockiert kurz -> nicht zu haeufig).
  static unsigned long lastCopyTry = 0;
  if (settingOn(stgAutoCopyPose) && (millis() - lastCopyTry > 15000)) {
    lastCopyTry = millis();
    if (copyPoseFromGroup(800)) return;                 // uebernommen -> fertig
  }

  // Automatik 2 (Radar-Spezialfall): keine valide Pose + anhaltende Co-Observation (eigenes
  // Target UND welt-valide Beobachtung gleichzeitig) -> Kalibrierung selbsttaetig starten.
  if (settingOn(stgAutoCalib)) {
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
  } else {
    autoArmMs = 0;
  }
}
