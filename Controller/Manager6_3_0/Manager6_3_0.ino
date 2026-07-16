//6_3 - umgestellt auf fixen Header + variablen Payload (xComDef6_3/xComProc6_3)
#include <xComDef6_3.h>
#include "hwDef.h"
#include <FastLED.h>

#define DEBUG true
byte ID = Manager;

#define containLed
#ifdef containLed
  CRGB leds[pixelNum];
#endif

unsigned long timer;
unsigned long hbTimer = 0;
bool blinkOn = false;
bool targetAlarm = false;

#define NOSHOT_PATH "/noshot.csv"
#define RASEN_PATH  "/rasen.csv"

// Multicast-Empfangsqueue statt Single-Buffer: der AsyncUDP-Task sammelt Pakete
// auch waehrend der blockierenden Gateway-HTTP-Posts (gwFlush/gwPollCommands) -
// vorher gingen dabei ~50% der Events bei 10-Hz-Radar-Bursts verloren.
#define MC_QUEUE_LEN 24

#include <xComProc6_3.h>

// Bus-Verkehr seriell mitdrucken? Bei 10 Hz kostet jede Zeile ~9 ms auf der
// 115200er-Konsole und bremst die Loop - nur fuer USB-Diagnose einschalten.
#define PRINT_BUS_TRAFFIC false

// Default-No-Shot-Karte: wird beim ersten Boot ins LittleFS geschrieben, falls dort
// noch keine Datei liegt. So kann der Manager auch ohne separaten Filesystem-Upload
// sofort eine Karte ausliefern; spaeter ueberschreibt ein FS-Upload (data/noshot.csv)
// oder ein Update diese Datei.
static const char NOSHOT_DEFAULT[] =
R"CSV(# NoShotZone v1  crc=0x6F9CF64F  units=mm  frame=world
# Polygon(e) der ERLAUBTEN Schusszone: innerhalb = Feuern erlaubt, ausserhalb = No-Shot.
# Mehrere Ringe durch Leerzeile getrennt (Loecher = No-Shot-Inseln).
# x,y  (Welt, Millimeter, ganzzahlig)
115,2
132,1301
1891,1376
2922,2558
3475,4946
4078,7485
4631,9998
5008,12537
5360,13668
6718,13668
6693,12512
6768,11255
7773,11280
7824,10602
8729,10577
8653,9546
7824,9471
7748,44
)CSV";

// Default-RasenKarte (Rasen-Umriss, Welt-mm): generiert aus Map/RasenKarte.csv (Meter).
// Sensoren mit gueltiger Welt-Pose beziehen sie (mapRasen) und melden nur Ziele
// INNERHALB. Bei Kartenaenderung: Punkte neu aus Map/RasenKarte.csv erzeugen (x1000,
// gerundet), Version im Header hochzaehlen, Datei /rasen.csv im FS loeschen (oder
// FS-Upload) - die Geraete laden bei Versions-/CRC-Abweichung automatisch neu.
static const char RASEN_DEFAULT[] =
R"CSV(# RasenKarte v1  units=mm  frame=world
# Rasen-Umriss: innerhalb = relevanter Bereich fuer die Katzendetektion.
# Quelle Map/RasenKarte.csv (Meter, Welt) -> hier Millimeter, ganzzahlig.
# x,y  (Welt, Millimeter)
2,562
-5,580
-3,742
0,1349
23,1552
811,1465
841,1632
845,1631
1212,2025
1366,2129
1336,2347
1674,2511
1960,2699
1837,3023
1972,3130
2098,3246
2155,3706
2169,3989
2235,4210
1658,5028
2173,4676
2343,5191
2452,5464
2512,5898
2806,6445
2908,6931
3021,7481
3057,8400
3146,8907
3205,9166
3388,9933
3601,11031
3817,12426
4161,13660
4610,14506
6036,16141
6187,16117
6387,16094
6564,16090
6786,16022
6911,16014
7062,16014
7709,11775
8174,11864
8449,11184
8884,11177
8424,8572
8389,7589
8366,6735
8373,6111
8378,5594
8658,5620
8348,4653
8266,4152
8301,3876
8336,3623
8289,3290
8131,3095
8255,2797
8239,2561
8485,2597
8233,2180
8262,2039
8239,1859
9033,2078
8590,1488
9052,1587
8294,1186
)CSV";

// Eine Karte im LittleFS sicherstellen (Seed beim ersten Boot) und annoncieren.
static void ensureMapFile(const char* path, const char* seed, const char* tag) {
  if (!LittleFS.exists(path)) {
    File f = LittleFS.open(path, "w");
    if (f) { f.print(seed); f.close(); Serial << "seeded " << path << endl; }
    else   { Serial << "could not seed " << path << endl; return; }
  }
  uint16_t v; uint32_t crc, len;
  if (mapFileInfo(path, v, crc, len))
    Serial << tag << " map: v" << v << " len=" << len << " crc=0x" << String(crc, HEX) << endl;
}

// LittleFS mounten und sicherstellen, dass No-Shot- und RasenKarte vorhanden sind.
void ensureMaps() {
  if (!LittleFS.begin(true)) {          // true = bei Bedarf formatieren
    Serial << "LittleFS mount FAILED" << endl;
    return;
  }
  ensureMapFile(NOSHOT_PATH, NOSHOT_DEFAULT, "noshot");
  ensureMapFile(RASEN_PATH,  RASEN_DEFAULT,  "rasen");
}

void setup() {
  Serial.begin(115200);
  initPixel();
  setUpWifi(device[ID].IP);
  initMcUdp();
  initUnicast();
  initText2Udp();
  setUpOTA();
//  setUpTime();
  ensureMaps();
  initSettings();                    // Anzeige-Settings (HB-/catObserved-Empfang) aus NVS
  sendSettingsReport();              // eigene Einstellungen annoncieren
  Serial << "ready" << endl;
}

void loop() {
  ArduinoOTA.handle();
  // Eigener HB: der Manager annonciert sich wie jedes andere Geraet (Lebenszeichen). Damit
  // erscheint er in der HB-/Aktiv-Liste und auf der VPS-Steuerseite (nur aktive Geraete).
  // gwAddHb direkt, damit der VPS ihn auch ohne Multicast-Loopback als aktiv sieht.
  if (millis() - hbTimer >= periodeForHB) {
    hbPayload hb; hb.ip = getLastIpByte(); hb.HBperiode = periodeForHB;
    broadcastMsg(HB, hb);
    gwAddHb(ID, hb.ip, 0);
    hbTimer = millis();
  }
  if (udpTextReceived) {
    udpTextReceived = false;
    Serial.print("received:" );
    Serial.println(udpTextMsg);
    gwAddDebug(udpTextMsg);            // Gateway: Debug-Zeile an den VPS
  }
  // Karten-Anfragen (Unicast) bedienen: Sensor fordert No-Shot- oder RasenKarte an
  if (ucDataReceived) {
    xMsg uc = lastUcMsg;
    uint8_t reqOctet = lastUcSenderOctet;
    ucDataReceived = false;
    if (handleCommonMsg(uc)) {         // settingsRequest/cmdSetSetting (eigene Anzeige-Settings)
      // erledigt
    }
    else if (uc.header.msgCode == mapRequest) {
      mapReqPayload req;
      if (getPayload(uc, req)) {
        Serial << "mapRequest type=" << req.mapType << " from ." << reqOctet << endl;
        if (req.mapType == mapNoShot)
          serveMap(mapNoShot, NOSHOT_PATH, reqOctet);
        else if (req.mapType == mapRasen)
          serveMap(mapRasen, RASEN_PATH, reqOctet);
      }
    }
  }
  {
    uint16_t qd = mcQueueDropped();    // Queue lief ueber (extremer Burst) -> sichtbar machen
    if (qd) gwAddDebug("MC-Queue voll: " + String(qd) + " Pakete verworfen");
  }
  xMsg mcMsg;
  while (mcQueuePop(mcMsg)) {
    if (PRINT_BUS_TRAFFIC) printSensorData(mcMsg);
    handleCommonMsg(mcMsg);            // settingsRequest/poseRequest generisch beantworten

    if (mcMsg.header.msgCode == HB) {
      hbPayload hb;
      if (getHbPayload(mcMsg, hb)) {                                     // Gateway
        // Radar-HBs (radarHbPayload, als einzige HB-Variante 9 Bytes) tragen die
        // eingestellte Totzone - fuers Abdeckungs-Overlay an den VPS weiterreichen.
        radarHbPayload rhb; uint16_t dz = 0;
        if (getPayload(mcMsg, rhb) && rhb.deadZoneDist > 0) dz = (uint16_t)rhb.deadZoneDist;
        gwAddHb(mcMsg.header.sender, hb.ip, dz);
      }
      if (settingOn(stgHbLed)) {                                         // HB-Empfang-Anzeige schaltbar
        allPixel(0x00FF00);
        blinkOn = true;
        timer = millis()+ HB_blinkPeriode;
      }
    }
    else if (mcMsg.header.msgCode == catObserved) {
      posPayload pos;
      if (getPayload(mcMsg, pos)) gwAddEvent(mcMsg.header.sender, pos);   // Gateway
      if (settingOn(stgCatLed)) {                                        // catObserved-Empfang-Anzeige schaltbar
        allPixel(0x0000FF);
        targetAlarm = true;
        blinkOn = false;
        timer = millis()+ Alarm_blinkPeriode;
      }
    }
    else if (mcMsg.header.msgCode == catDetected) {
      // Cat Identifier hat eine Katze bestaetigt: ROT blinken (schaltbar) + VPS-Debug.
      // Der Identifier sendet jede Detektion DOPPELT (UDP-Verlustschutz) -> identische
      // Payload innerhalb kurzer Zeit ist ein Duplikat und wird ignoriert.
      catDetectedPayload cd;
      if (getPayload(mcMsg, cd)) {
        static catDetectedPayload lastCd; static unsigned long lastCdMs = 0;
        bool dup = (millis() - lastCdMs < 1500) &&
                   memcmp(&lastCd, &cd, sizeof(cd)) == 0;
        lastCd = cd; lastCdMs = millis();
        if (!dup) {
          gwAddDebug("CatDetected #" + String(mcMsg.header.sender) +
                     " score=" + String(cd.score) + " x=" + String(cd.worldX) +
                     " y=" + String(cd.worldY) + " net=" + String(cd.netMm) + "mm" +
                     ((cd.flags & catDetFlagStationary) ? " sitzt!" : "") +
                     ((cd.flags & catDetFlagFusion) ? " fusion" : ""));
          if (settingOn(stgCatDetLed)) {                 // catDetected-Anzeige schaltbar
            allPixel(0xFF0000);
            targetAlarm = true;
            blinkOn = false;
            timer = millis() + Alarm_blinkPeriode;
          }
        }
      }
    }
    else if (mcMsg.header.msgCode == settingsReport) {
      settingsPayload sp;
      if (getPayload(mcMsg, sp)) gwAddSettings(mcMsg.header.sender, sp); // Gateway: an VPS weiterreichen
    }
    else if (mcMsg.header.msgCode == poseReport) {
      worldPosePayload wp;                                               // Gateway: Welt-Posen an den VPS
      if (getPayload(mcMsg, wp) && mcMsg.header.sender != ID)            // (fuer die Erfassungssektoren)
        gwAddPose(mcMsg.header.sender, wp);
    }
  }
  gwTick();                            // Gateway: gepufferte Ereignisse periodisch an den VPS posten
  if (blinkOn && (millis()> timer)) {
    allPixel(0x000000);
    blinkOn = false;
  }
  if (targetAlarm && (millis()> timer)) {
    allPixel(0x000000);
    targetAlarm = false;
  }

}
