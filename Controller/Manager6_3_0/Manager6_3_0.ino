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

// Karten liegen NICHT mehr im Firmware-Quellcode (MapConcept.md Leitplanke 2):
// die einzige Quelle ist der VPS-Editor (Annahme-Code in gatewayProc.ino,
// gwAcceptMap()/gwFetchMap()). Ohne Karte im LittleFS antwortet der Manager auf
// mapRequest schlicht nicht (kein Seed-Sonderfall mehr, siehe serveMap()).
// Notweg ohne VPS: Controller/Manager6_3_0/data/<typ>.csv im Repo anpassen
// (Single Source of Truth), LittleFS-Image daraus neu bauen und per USB oder
// OTA flashen - Schritt-fuer-Schritt-Anleitung in KartenUpload.md.

// LittleFS mounten und den aktuellen Kartenstand (falls vorhanden) einlesen/loggen
// + annoncieren (gwAnnounceMaps - wichtig nach einem Notweg-Reflash, siehe oben).
// (Nur Funktionsaufrufe hier - gwMapVer/gwMapCrc sind Variablen aus gatewayProc.ino,
// das textuell NACH diesem Tab in den Sketch einfliesst; Arduino generiert automatische
// Prototypen nur fuer Funktionen, nicht fuer Variablen, siehe gwRefreshMapStatus().)
void ensureMaps() {
  if (!LittleFS.begin(true)) {          // true = bei Bedarf formatieren
    Serial << "LittleFS mount FAILED" << endl;
    return;
  }
  gwRefreshMapStatus();
  gwAnnounceMaps();
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
        // HLK-Radare (Geraetetyp aus der device-DB, WER steht im Header) senden
        // radarHbPayload mit der eingestellten Totzone - fuers Abdeckungs-Overlay
        // an den VPS weiterreichen. getPayload prueft dabei nur noch die Groesse.
        radarHbPayload rhb; uint16_t dz = 0;
        if (device[mcMsg.header.sender].type == HLK &&
            getPayload(mcMsg, rhb) && rhb.deadZoneDist > 0) dz = (uint16_t)rhb.deadZoneDist;
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
