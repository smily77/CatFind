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
bool blinkOn = false;
bool targetAlarm = false;

#define NOSHOT_PATH "/noshot.csv"

#include <xComProc6_3.h>

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

// LittleFS mounten und sicherstellen, dass eine No-Shot-Karte vorhanden ist.
void ensureNoShotMap() {
  if (!LittleFS.begin(true)) {          // true = bei Bedarf formatieren
    Serial << "LittleFS mount FAILED" << endl;
    return;
  }
  if (!LittleFS.exists(NOSHOT_PATH)) {
    File f = LittleFS.open(NOSHOT_PATH, "w");
    if (f) { f.print(NOSHOT_DEFAULT); f.close(); Serial << "seeded " NOSHOT_PATH << endl; }
    else   { Serial << "could not seed " NOSHOT_PATH << endl; }
  }
  uint16_t v; uint32_t crc, len;
  if (mapFileInfo(NOSHOT_PATH, v, crc, len))
    Serial << "noshot map: v" << v << " len=" << len << " crc=0x" << String(crc, HEX) << endl;
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
  ensureNoShotMap();
  initSettings();                    // Anzeige-Settings (HB-/catObserved-Empfang) aus NVS
  sendSettingsReport();              // eigene Einstellungen annoncieren
  Serial << "ready" << endl;
}

void loop() {
  ArduinoOTA.handle();
  if (udpTextReceived) {
    udpTextReceived = false;
    Serial.print("received:" );
    Serial.println(udpTextMsg);
    gwAddDebug(udpTextMsg);            // Gateway: Debug-Zeile an den VPS
  }
  // Karten-Anfragen (Unicast) bedienen: Sensor fordert die No-Shot-Karte an
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
      }
    }
  }
  if (mcDataReceived) {
    xMsg mcMsg;
    mcMsg=lastMcMsg;
    printSensorData(mcMsg);
    handleCommonMsg(mcMsg);            // settingsRequest/poseRequest generisch beantworten

    if (mcMsg.header.msgCode == HB) {
      hbPayload hb;
      if (getHbPayload(mcMsg, hb)) gwAddHb(mcMsg.header.sender, hb.ip);   // Gateway
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
    else if (mcMsg.header.msgCode == settingsReport) {
      settingsPayload sp;
      if (getPayload(mcMsg, sp)) gwAddSettings(mcMsg.header.sender, sp); // Gateway: an VPS weiterreichen
    }
    mcDataReceived = false;
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
