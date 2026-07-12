// =====================================================================
//  C1Lidar6_3_0 -- RPLidar C1 als welt-faehiger CatFinder-Sensor
// ---------------------------------------------------------------------
//  Neu gedacht ggü. C1_Lidar_V2_OTA_Ueberw3: KEINE Wand/Nische noetig.
//  - 20 s Hintergrund lernen (rundum), Perimeter-Detektion
//  - Welt-Pose per VPS-Lokalisierung (HTTP, ipVPS) bestimmen, NVS-cachen
//  - No-Shot-Karte vom Manager laden, Treffer nur im schiessbaren Bereich
//    als catObserved (relative + Welt-Koordinaten) broadcasten
//  - Status-Pixel + knapper Text-Multicast nach der Initialisierung, OTA
// =====================================================================
#include <RPLidar.h>          // vor xComDef: dessen Makro 'measurement' kollidiert sonst
                              // mit einem gleichnamigen Parameter in RPLidar.h
#include <xComDef6_3.h>
#include "hwDef.h"
#include <HTTPClient.h>
#include <FastLED.h>

#define DEBUG true
byte ID = LidarC1;

#define containLed
#ifdef containLed
  CRGB leds[pixelNum];
#endif

RPLidar     lidar;

// --- Hintergrund-/Perimetermodell (je Bin) ---
float    bgMin1[NUM_BINS], bgMin2[NUM_BINS];
uint16_t bgCnt [NUM_BINS];
float    perim [NUM_BINS];
int      perimAnchors = 0, perimOpen = 0;

unsigned long calStartMs = 0;
bool          calibrated = false;
bool          hadNVSpose = false;
// Welt-Pose steht in myPose (xComDef): worldX/worldY (mm), heading (PA-Einheiten),
// mirror (Drehsinn), validWorldPose. Persistenz/Transformation generisch in xComProc.

enum InitPhase { PH_WIFI, PH_CALIB, PH_LOCALIZE, PH_NOSHOT, PH_ACTIVE, PH_NOLOC };
InitPhase phase = PH_WIFI;

// --- Detektion ---
unsigned long lastDetectMs   = 0;
int           lastDirDeg     = -1;
uint8_t       lastHue        = 0;
bool          noShotOK       = false;
unsigned long lastHBms       = 0;
unsigned long hbBlinkUntil   = 0;   // bis wann der gruene HB-Blitz leuchtet (0 = aus)

#define USE_VPS_LOCALIZE             // aktiviert vpsLocalize() (zieht HTTPClient) in xComProc
#include <xComProc6_3.h>

static inline int angleToBin(float angle) {
  int b = (int)(angle + 0.5f);
  if (b < 0)         b += NUM_BINS;
  if (b >= NUM_BINS) b -= NUM_BINS;
  return b;
}
static inline float wrap360(float d){ while(d<0)d+=360; while(d>=360)d-=360; return d; }

// HB broadcasten (fuer IP-Lernen / Lebenszeichen)
void sendHB() {
  hbPayload hb; hb.ip = getLastIpByte(); hb.HBperiode = periodeForHB;
  broadcastMsg(HB, hb);
  lastHBms = millis();
  if (settingOn(stgHbLed)) hbBlinkUntil = millis() + HB_BLINK_MS;   // gruener HB-Blitz (nur PH_ACTIVE)
}

void setup() {
  Serial.begin(115200);
  initPixel();
  allPixel(0x000000);

  setUpWifi(device[ID].IP);          // LidarC1: device-IP 0 -> DHCP; blinkt rot beim Verbinden
  initMcUdp();
  initUnicast();
  initText2Udp();
  setUpOTA();
  if (!LittleFS.begin(true)) Serial << "LittleFS mount FAILED" << endl;

  Serial1.begin(LIDAR_BAUD, SERIAL_8N1, LIDAR_RX_PIN, LIDAR_TX_PIN);
  delay(800);
  lidar.begin(Serial1);
  lidar.startScan();

  initSettings();                    // Anzeige-/Motor-Settings aus NVS (STG_* aus hwDef)
  hadNVSpose = loadPose(myPose);     // Pose aus NVS lesen (validWorldPose bleibt vorerst false)
  startCalibration();                // -> PH_CALIB (blau)
  sendHB();
  sendSettingsReport();              // Einstellungen annoncieren (Display/VPS lernen sie)
}

void loop() {
  ArduinoOTA.handle();

  if (millis() - lastHBms >= periodeForHB) sendHB();

  // Eingehende Steuer-/Einstellungsnachrichten generisch verarbeiten
  // (cmdSetSetting per Unicast, settingsRequest/poseRequest per Multicast).
  if (ucDataReceived) { xMsg um = lastUcMsg; ucDataReceived = false; handleCommonMsg(um); }
  if (mcDataReceived) { xMsg cm = lastMcMsg; mcDataReceived = false; handleCommonMsg(cm); }

  updateLeds();                      // HB-Blitz (gruen) / Detektions-LEDs (nur PH_ACTIVE)

  // Motor an/aus (Lidar-Spezial-Setting): stop()/startScan() beim Umschalten.
  static bool motorRunning = true;
  bool wantMotor = settingOn(stgLidarMotor);
  if (wantMotor != motorRunning) {
    motorRunning = wantMotor;
    if (wantMotor) lidar.startScan(); else lidar.stop();
    sendUdpTextln(String("Lidar-Motor ") + (wantMotor ? "an" : "aus"));
  }
  if (!motorRunning) return;         // Motor aus -> keine Messung/Detektion

  RPLidarMeasurement m;
  if (!lidar.readMeasurement(m)) return;

  const bool valid = m.quality > 0 && m.distance > 0.0f;
  const int  bin   = angleToBin(m.angle);

  // --- Phase 1: Hintergrund lernen ---
  if (!calibrated) {
    if (valid) {
      const float d = m.distance;
      if (d < bgMin1[bin])      { bgMin2[bin] = bgMin1[bin]; bgMin1[bin] = d; }
      else if (d < bgMin2[bin]) { bgMin2[bin] = d; }
      bgCnt[bin]++;
    }
    if (millis() - calStartMs >= CAL_TIME_MS) finishInit();
    return;
  }

  if (phase != PH_ACTIVE) return;    // nur im aktiven Zustand detektieren

  // --- Phase 2: Detektion (pro Umdrehung auswerten) ---
  static int   flaggedThisScan = 0;
  static float sumLx = 0, sumLy = 0, sumCos = 0, sumSin = 0;

  if (m.startBit) {                  // neue Umdrehung -> letzte auswerten
    if (flaggedThisScan >= MIN_DETECT_POINTS) {
      const float lxc = sumLx / flaggedThisScan;
      const float lyc = sumLy / flaggedThisScan;
      int wx, wy;
      localToWorld(lroundf(lxc), lroundf(lyc), myPose, wx, wy);   // lokal -> Welt (mm, mirror-aware)

      float deg = atan2f(sumSin, sumCos) * RAD2DEG; if (deg < 0) deg += 360.0f;
      lastDirDeg = (int)(deg + 0.5f) % 360;
      lastHue    = (uint8_t)(deg * (256.0f / 360.0f));

      if (insideNoShot(wx, wy)) {     // nur im schiessbaren Bereich melden
        lastDetectMs = millis();
        posPayload pos;
        pos.x = (int32_t)lroundf(lxc);
        pos.y = (int32_t)lroundf(lyc);
        float phi, rad;                              // packed-Felder nicht direkt per Referenz binden
        toPaPol(pos.x, pos.y, phi, rad);
        pos.angle = phi; pos.radius = rad;
        pos.targetSpeed = 0;
        pos.res = 0;
        pos.worldX = wx; pos.worldY = wy; pos.worldValid = 1;
        pos.sensor = 0;
        broadcastMsg(catObserved, pos);
      }
    }
    flaggedThisScan = 0; sumLx = sumLy = sumCos = sumSin = 0;   // LEDs: updateLeds() in loop()
  }

  if (!valid || m.distance < NEAR_LIMIT_MM) return;

  if (m.distance <= perim[bin] - PERIM_MARGIN_MM) {     // naeher als der Hintergrund -> Ziel
    const float a = m.angle * DEG2RAD;
    sumLx += m.distance * cosf(a);
    sumLy += m.distance * sinf(a);     // roh; mirror wendet localToWorld an
    sumCos += cosf(a); sumSin += sinf(a);
    flaggedThisScan++;
  }
}
