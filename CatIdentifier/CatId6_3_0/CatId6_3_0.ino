// Cat Identifier (Geraet 18, "Cat_Identifier", feste IP .184)
//
// Ein normales Geraet des ESP32-Netzes, das das Katzen-Erkennungsmodell IN ECHTZEIT
// laufen laesst (Streaming-Port des VPS-Referenzmodells catmodel.py):
//   * hoert alle catObserved (worldValid) vom Multicast-Bus
//   * Track-Bildung (Gate + Stitching), kohaerente Bewegungsphase, Score
//   * Sturm-/Burst-Unterdrueckung je Sensor, Mueher-K.o. ueber max_path_mm
//   * Erfassungsrand-Bonus geometrisch aus device[] (covL/covR/covRange) + den per
//     poseReport gemeldeten Welt-Posen (wie build_coverage_geo auf dem VPS)
// Bei Bestaetigung: catDetected wird DOPPELT gebroadcastet (UDP-Verlustschutz;
// Empfaenger dedupen ueber identische Payload) - der Manager blinkt rot.
//
// Modell-Parameter: kommen vom VPS (GET /aparams.csv, dieselben Werte wie im
// Analyse-Tab "Parameter..."), werden im LittleFS gecacht (/catparams.csv) und
// ueberleben so VPS-Ausfaelle/Reboots. Aktion "Parameter laden" (cmdReloadParams,
// VPS-Steuerung oder Display) holt sie neu - Modell-Iterationen ohne Neuflash.
#include <xComDef6_3.h>
#include "hwDef.h"
#include "catTrackDef.h"

#define DEBUG true

unsigned long timer;                 // heardBeat (Muster wie Radar)
boolean statusLightOn = false;

#include <xComProc6_3.h>
#include <HTTPClient.h>

#define PARAMS_PATH      "/catparams.csv"
#define COVERAGE_PATH    "/coverage.csv"
#define PARAMS_FETCH_TIMEOUT_MS 8000
#define POSE_REQ_BOOT_MS 8000        // kurz nach dem Boot einmal nach Welt-Posen fragen

// No-Shot-Karte: catObserved ausserhalb der erlaubten Schusszone wird gar nicht erst
// ins Modell gefuettert (Radar/Nachbargrundstueck-Rauschen betrifft die Feuer-
// Entscheidung ohnehin nie) - siehe MapConcept.md. insideNoShot() faellt ohne
// geladene Karte offen (alles durchlassen), Filterung startet erst, sobald sie da ist.
#define NOSHOT_PATH        "/noshot.csv"
#define MAP_WAIT_MS        6000
#define MAP_RETRY_MS      60000
#define MAP_RECHECK_MS  3600000UL
bool          noShotOK       = false;
unsigned long lastNoShotTry  = 0;
bool          mapRecheckNow  = false;

unsigned long detLedOffMs = 0;       // CatDetected-LED wieder aus
unsigned long bootPoseReqMs = 0;     // 0 = poseRequest noch nicht gesendet

// Eigene WiFi-Anmeldung statt setUpWifi: manche ESP32-S3-DevKits assoziieren mit
// voller TX-Leistung nicht (Spannungsversorgung der PA) - Leistung absenken und
// periodisch neu versuchen, statt endlos in der setUpWifi-Schleife zu haengen.
static void catIdWifi() {
  WiFi.mode(WIFI_STA);
  IPAddress localIP(192, 168, 0, device[ID].IP), gw(192, 168, 0, 1), sn(255, 255, 255, 0);
  // WICHTIG: DNS mitgeben! WiFi.config ohne 4. Argument laesst den DNS leer -
  // dann haengt alles, was Hostnamen aufloest (NTP pool.ntp.org), endlos.
  if (!WiFi.config(localIP, gw, sn, gw)) Serial.println("static IP config failed");
  WiFi.begin(ssid, password);
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
  unsigned long tryStart = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
    Serial.print(".");
    if (millis() - tryStart > 20000) {           // Neuanlauf statt Endlosschleife
      Serial.println(" retry, status=" + String((int)WiFi.status()));
      WiFi.disconnect(true);
      delay(500);
      int n = WiFi.scanNetworks();               // Diagnose: sehen wir die SSID ueberhaupt?
      for (int i = 0; i < n && i < 8; i++)
        Serial.println("  scan: " + WiFi.SSID(i) + " rssi=" + String(WiFi.RSSI(i)) +
                       " ch=" + String(WiFi.channel(i)) + " enc=" + String((int)WiFi.encryptionType(i)));
      WiFi.mode(WIFI_STA);
      WiFi.config(localIP, gw, sn);
      WiFi.begin(ssid, password);
      WiFi.setTxPower(WIFI_POWER_8_5dBm);
      tryStart = millis();
    }
  }
  Serial.println(WiFi.localIP().toString());
}

void setup() {
  Serial.begin(115200);
  pinMode(CATID_LED, OUTPUT);
  digitalWrite(CATID_LED, CATID_LED_OFF);
  catIdWifi();                       // feste IP .184 (S3-taugliche Anmeldung s.o.)
  initMcUdp();
  initUnicast();
  initText2Udp();
  setUpOTA();
  // Zeit mit BUDGET statt setUpTime(): dessen Endlosschleife wuerde das Geraet bei
  // NTP-Problemen dauerhaft blockieren (ohne OTA-Ausweg). Das Modell rechnet mit
  // millis() - die Uhrzeit dient nur den Nachrichten-Zeitstempeln.
  configTzTime(time_zone, ntpServer1, ntpServer2);
  {
    unsigned long t0 = millis();
    while (!getLocalTime(&timeinfo, 200) && millis() - t0 < 15000) delay(50);
    if (!getLocalTime(&timeinfo, 200)) Serial.println("NTP nicht erreichbar - weiter ohne Uhrzeit");
  }
  initSettings();
  if (!LittleFS.begin(true)) sendUdpTextln("CatIdent: LittleFS mount FAILED");
  ctSetDefaults();                   // einkompilierte Defaults (= DEFAULT_PARAMS im VPS)
  bool cached = ctLoadParamsFile(PARAMS_PATH);
  bool fresh  = ctFetchParams();     // VPS erreichbar -> aktuelle Parameter holen+cachen
  bool covCached = ctLoadCoverageFile(COVERAGE_PATH);
  bool covFresh  = ctFetchCoverage();
  sendUdpTextln(String("CatIdent bereit, Parameter: ") +
                (fresh ? "vom VPS" : (cached ? "aus Cache" : "Defaults")) +
                ", Coverage: " + (covFresh ? "vom VPS" : (covCached ? "aus Cache" : "Sektoren")) +
                " (" + String(ctCoverageCount()) + " Polygone)");
  sendSettingsReport();
  timer = millis();
}

void loop() {
  ArduinoOTA.handle();
  heardBeat();
  if (ucDataReceived) { ucDataReceived = false; handleCommand(lastUcMsg); }
  if (mcDataReceived) { mcDataReceived = false; handleBusMsg(lastMcMsg); }
  ctTick();                          // abgelaufene Tracks/Sturm-Slots aufraeumen
  serviceNoShotMap();                // Erstbezug + stuendliches Fangnetz (hwProc.ino)
  // Fensterwechsel der NoShot-Zeit einmalig melden (Debug-Sichtbarkeit im VPS)
  static bool quietWas = false;
  bool quietNow = ctInQuietWindow();
  if (quietNow != quietWas) {
    quietWas = quietNow;
    sendUdpTextln(quietNow ? "NoShot-Zeit AKTIV (" + ctQuietStr() + ") - Modell pausiert"
                           : "NoShot-Zeit beendet - Modell wieder aktiv");
  }
  // einmalig kurz nach dem Boot die Welt-Posen der Sensoren erfragen (danach
  // haelt der 5-min-poseRequest des Managers sie aktuell - wir hoeren nur mit)
  if (bootPoseReqMs == 0 && millis() > POSE_REQ_BOOT_MS) {
    bootPoseReqMs = millis();
    broadcastMsg(poseRequest);
  }
  if (detLedOffMs && millis() > detLedOffMs && !statusLightOn) {
    detLedOffMs = 0;
    digitalWrite(CATID_LED, CATID_LED_OFF);
  }
}

// Unicast-Kommandos (VPS-Steuerung via Manager / Display)
void handleCommand(const xMsg& m) {
  if (handleCommonMsg(m)) return;    // settingsRequest/poseRequest/cmdSetSetting generisch
  cmdPayload cmd;
  if (!getPayload(m, cmd)) return;
  if (cmd.cmd == cmdReloadParams) {  // Aktion: Modell-Parameter neu vom VPS laden
    bool ok = ctFetchParams();
    bool covOk = ctFetchCoverage();
    sendUdpTextln((ok ? "CatIdent: Parameter neu geladen (" + ctParamSummary() + ")"
                      : "CatIdent: Parameter-Reload FEHLGESCHLAGEN") +
                  String(covOk ? ", Coverage neu geladen (" + String(ctCoverageCount()) + " Polygone)"
                               : ", Coverage nicht neu geladen"));
  }
}

// Multicast vom Bus: catObserved fuettert das Modell, poseReport die Sektoren.
void handleBusMsg(const xMsg& m) {
  if (handleCommonMsg(m)) return;
  if (m.header.msgCode == mapInfo) {   // Announce: Manager hat eine neue Karte angenommen
    if (mapAnnounceOutdated(m, mapNoShot, NOSHOT_PATH)) { noShotOK = false; mapRecheckNow = true; }
    return;
  }
  if (m.header.msgCode == catObserved && m.header.sender != ID) {
    posPayload obs;
    if (!getPayload(m, obs) || !obs.worldValid) return;   // Modell arbeitet in Welt-mm
    if (m.header.sender >= deviceCount) return;
    // Ausserhalb der erlaubten Schusszone ist fuer die Feuer-Entscheidung irrelevant
    // (spart RAM/Rechenzeit und haelt das Modell auf das beschraenkt, was zaehlt -
    // insideNoShot() faellt ohne geladene Karte offen: alles durchlassen).
    if (!insideNoShot(obs.worldX, obs.worldY)) return;
    if (ctInQuietWindow()) return;     // NoShot-Zeit (Maeherfenster): Modell pausiert
    ctFeed(m.header.sender, obs.worldX, obs.worldY);
  }
  else if (m.header.msgCode == poseReport && m.header.sender != ID) {
    worldPosePayload wp;
    if (getPayload(m, wp) && m.header.sender < deviceCount)
      ctSetPose(m.header.sender, wp);
  }
}

// Vom Modell (catTrack.ino) aufgerufen, sobald ein Track als Katze bestaetigt ist:
// catDetected DOPPELT broadcasten - UDP hat kein Resend, und ein einzelnes verlorenes
// Paket wuerde die Detektion verschlucken. Empfaenger (Manager, spaeter Aktoren)
// dedupen ueber identische Payload innerhalb ~1,5 s.
void announceCat(const catDetectedPayload& cd) {
  // Doppelte Absicherung zur Feed-Sperre in handleBusMsg: waehrend der NoShot-Zeit
  // darf unter keinen Umstaenden ein catDetected raus (Fehlschuss auf den Maeher).
  if (ctInQuietWindow()) return;
  broadcastMsg(catDetected, cd);
  delay(40);
  broadcastMsg(catDetected, cd);
  sendUdpTextln("CatDetected score=" + String(cd.score) + " x=" + String(cd.worldX) +
                " y=" + String(cd.worldY) + " track=" + String(cd.trackMs / 1000.0f, 1) +
                "s net=" + String(cd.netMm) + "mm" +
                ((cd.flags & catDetFlagStationary) ? " sitzt!" : "") +
                ((cd.flags & catDetFlagFusion) ? " fusion" : ""));
  if (settingOn(stgCatLed)) {
    digitalWrite(CATID_LED, CATID_LED_ON);
    detLedOffMs = millis() + DET_FLASH_MS;
  }
}
