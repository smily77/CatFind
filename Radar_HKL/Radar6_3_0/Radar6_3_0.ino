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

// Filterkarte: standardmaessig NoShot (kleinere, ruhigere Karte - wie CatIdentifier/Lidar),
// per stgRadarFullRasen umschaltbar auf die volle RasenKarte (mehr Randdaten, z.B. fuer eine
// NoShot-Editier-Session im VPS, aber mehr Bus-/VPS-Traffic). Mit gueltiger Welt-Pose werden
// nur Ziele INNERHALB der jeweils aktiven Karte gemeldet (Nachbargrundstueck/Strasse im
// 7-m-Radarkegel erzeugen sonst Dauer-Stoerungen). Ohne Karte/Pose: alles melden.
#define NOSHOT_PATH        "/noshot.csv"
#define RASEN_PATH         "/rasen.csv"
#define MAP_WAIT_MS        6000   // so lange auf mapInfo/Chunks vom Manager warten
#define MAP_RETRY_MS      60000   // Karte nicht bekommen -> so lange bis zum naechsten Versuch
#define MAP_RECHECK_MS  3600000UL // Fangnetz: auch mit geladener Karte stuendlich neu pruefen
                                  // (ein mapInfo-Announce kann per UDP verlorengehen - kein Resend)

int32_t  lastTargetX = 0, lastTargetY = 0;   // juengste eigene Detektion (fuer Health-Check)
unsigned long lastTargetMs = 0;              // wann zuletzt ein Radar-Target aktiv war
unsigned long lastWorldObsMs = 0;            // wann zuletzt eine welt-valide catObserved kam
unsigned long autoArmMs = 0;                 // seit wann Co-Observation anliegt (0 = nicht)

// Zwei unabhaengig gepflegte Karten-Slots (NoShot/Rasen); insideNoShot() wirkt aber immer nur
// auf EINEN im RAM geladenen Puffer (acquireMap endet intern mit loadNoShot). Deshalb nach
// jedem Slot-Refresh in serviceFilterMaps() immer neu durchsetzen, welche Karte laut aktuellem
// stgRadarFullRasen gerade aktiv sein soll - unabhaengig davon, welcher Slot zuletzt lud.
struct MapSlot {
  bool          loaded    = false;   // dieser Slot beim letzten Versuch erfolgreich geladen?
  unsigned long lastTry   = 0;       // letzter Beschaffungsversuch (0 = noch keiner)
  bool          recheckNow = false;  // per Announce (mapInfo) sofortiger Re-Check angefordert
};
MapSlot noshotSlot, rasenSlot;
bool filterMapActive = false;   // insgesamt EINE Karte aktiv nutzbar (insideNoShot sinnvoll)?
bool activeIsRasen   = false;   // welche der beiden zuletzt in den RAM-Puffer geladen wurde

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
  serviceFilterMaps();
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
    if (mapAnnounceOutdated(m, mapNoShot, NOSHOT_PATH)) { noshotSlot.loaded = false; noshotSlot.recheckNow = true; }
    if (mapAnnounceOutdated(m, mapRasen,  RASEN_PATH))  { rasenSlot.loaded  = false; rasenSlot.recheckNow  = true; }
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
// Mit gueltiger Welt-Pose UND geladener Filterkarte (NoShot oder RasenKarte, siehe
// serviceFilterMaps) werden Ziele AUSSERHALB davon nicht gemeldet (Nachbargrundstueck/
// Strasse); Kalibrier-Sammlung und Health-Check arbeiten weiter mit ALLEN Zielen (die
// Referenzperson koennte am Rand stehen).
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
      if (obsData.worldValid && filterMapActive &&
          !insideNoShot(obsData.worldX, obsData.worldY)) continue;   // ausserhalb der Filterkarte
      broadcastMsg(catObserved, obsData);
    }
  }
  setPixel(maxPix, (validTarget && settingOn(stgCatLed)) ? 0x0000FF : 0x000000);  // Anzeige schaltbar
}

// Einen Karten-Slot beschaffen (lokaler LittleFS-Cache + Versionsabgleich beim Manager,
// sonst Download - acquireMap blockiert dabei bis MAP_WAIT_MS). Ohne Karte alle MAP_RETRY_MS
// neuer Versuch; MIT geladener Karte zusaetzlich alle MAP_RECHECK_MS ein Fangnetz-Re-Check UND
// sofort bei passendem mapInfo-Announce (recheckNow, siehe handleObservation). Liefert true,
// wenn tatsaechlich ein Beschaffungsversuch stattfand (der RAM-Puffer also frisch ueberschrieben
// wurde und die aktive Karte ggf. neu durchgesetzt werden muss).
bool serviceMapSlot(MapSlot& slot, uint8_t mapType, const char* path) {
  unsigned long dueMs = slot.loaded ? MAP_RECHECK_MS : MAP_RETRY_MS;
  if (!slot.recheckNow && slot.lastTry != 0 && millis() - slot.lastTry < dueMs) return false;
  slot.lastTry = millis();
  slot.recheckNow = false;
  slot.loaded = acquireMap(mapType, path, device[Manager].IP, MAP_WAIT_MS);
  return true;
}

// Beide Karten unabhaengig aktuell halten, danach IMMER die laut stgRadarFullRasen aktuell
// gewuenschte Karte in den (einzigen, geteilten) RAM-Puffer laden - acquireMap() ueberschreibt
// diesen Puffer bei jedem Refresh, ganz gleich welcher Slot gerade dran war, darum reicht ein
// Slot-Refresh allein nicht: die "andere" Karte koennte danach im Puffer stehen.
void serviceFilterMaps() {
  if (!myPose.validWorldPose || calib.active) return;
  bool noshotTouched = serviceMapSlot(noshotSlot, mapNoShot, NOSHOT_PATH);
  bool rasenTouched  = serviceMapSlot(rasenSlot,  mapRasen,  RASEN_PATH);
  bool wantRasen = settingOn(stgRadarFullRasen);
  MapSlot& wantSlot = wantRasen ? rasenSlot : noshotSlot;
  if (wantRasen != activeIsRasen || noshotTouched || rasenTouched) {
    activeIsRasen = wantRasen;
    filterMapActive = wantSlot.loaded && loadNoShot(wantRasen ? RASEN_PATH : NOSHOT_PATH);
    sendUdpTextln(filterMapActive
      ? (String(wantRasen ? "RasenKarte" : "NoShot-Karte") + " aktiv - melde nur Ziele darin")
      : (String(wantRasen ? "RasenKarte" : "NoShot-Karte") + " nicht verfuegbar - melde ungefiltert"));
  }
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
