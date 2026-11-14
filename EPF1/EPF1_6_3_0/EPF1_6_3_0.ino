// =====================================================================
//  EPF1_6_3_0 -- Drehturm (Pan/Tilt Feetech) als CatFinder-Geraet #23
// ---------------------------------------------------------------------
//  - WLAN per DHCP, HB, OTA, Einstellungen/Aktionen wie die anderen Geraete
//  - Beim Start faehrt der Turm auf 0 (Pan-Origin aus NVS, Default 2048; Tilt 0)
//  - TOFSense-F (I2C) misst; ein Rundumscan (Pan -170..+170 in 1 Grad) geht als
//    360-Bin-Scan an den VPS-Localizer (/localize, wie LidarC1) -> Welt-Pose
//  - Scan automatisch AUTO_SCAN_DELAY_MS nach dem Boot und per Aktion "Kalibrieren"
//  - Der LaserMarker auf dem Turm ist ein eigenes Geraet (#15), hier nur gespeist
//
//  Kommandos (commandMsg an #23):
//    cmdCalibrate (18)    Rundumscan + Lokalisierung
//    cmdClearPose (19)    Pose vergessen
//    cmdRichtung  (1)     info = Pan-Winkel in Grad*10 relativ zur 0 (z.B. 900 = +90 Grad)
//    cmdAdjustAngle (3)   info = neue Pan-0 in Servo-Ticks (0..4095), wird gespeichert,
//                         Turm faehrt sofort dorthin
//  Seriell (115200): ? Hilfe | c Scan | d TOF lesen | p Servos | a <grad> | 0 |
//                    z <ticks> Pan-0 setzen | f/F Drehmoment aus/ein
// =====================================================================
#include <xComDef6_3.h>
#include "hwDef.h"
#include <Wire.h>
#include <Preferences.h>
#include <Adafruit_NeoPixel.h>
#include <FeetechBus.h>
#include <FeetechST3020.h>
#include <FeetechSC15.h>

#define DEBUG true
byte ID = EPF1;

Adafruit_NeoPixel pixel(1, pixelPin, NEO_RGB + NEO_KHZ800);
FeetechBus    servoBus(Serial1);
FeetechST3020 pan(servoBus, panServoID);
#if defined(TILT_SERVO_SC)
FeetechSC15   tilt(servoBus, tiltServoID);
#else
FeetechST3020 tilt(servoBus, tiltServoID);
#endif

#define USE_VPS_LOCALIZE
#include <xComProc6_3.h>

uint16_t panOrigin = panServoOrigin;    // Pan-0 in Ticks (NVS "epf1"/"panOrg")
uint8_t  tofAddr   = 0;                 // 0 = kein TOFSense gefunden
bool     hadNVSpose = false;
bool     scanRequested = false;
bool     autoScanDone  = false;
unsigned long bootMs = 0, lastHBms = 0, hbBlinkUntil = 0;
uint16_t scanBins[360];

// ----------------------------------------------------------------- Hilfen
void writelnComment(String comment) { if (DEBUG) Serial << comment << endl; }
void writeComment(String comment)   { if (DEBUG) Serial << comment; }

static void say(const String& s) { Serial.println(s); sendUdpTextln("EPF1: " + s); }

static void setPix(uint32_t rgb) {
  pixel.setPixelColor(0, pixel.Color((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF));
  pixel.show();
}
static uint32_t idleColor() { return myPose.validWorldPose ? 0x001000 : 0x100000; }

static uint16_t panDegToTicks(float deg) {
  deg = constrain(deg, -PAN_LIMIT_DEG, PAN_LIMIT_DEG);
  return (uint16_t)constrain((int32_t)lroundf(panOrigin + deg * PAN_TICKS_PER_DEG), 0, 4095);
}
static float panTicksToDeg(uint16_t t) { return ((int32_t)t - (int32_t)panOrigin) / PAN_TICKS_PER_DEG; }
static uint16_t tiltDegToTicks(float deg) {
  deg = constrain(deg, -TILT_LIMIT_DEG, TILT_LIMIT_DEG);
  return (uint16_t)constrain((int32_t)lroundf(tiltServoOrigin + deg * TILT_TICKS_PER_DEG), 0, TILT_TICKS_MAX);
}

void sendHB() {
  hbPayload hb; hb.ip = getLastIpByte(); hb.HBperiode = periodeForHB;
  broadcastMsg(HB, hb);
  lastHBms = millis();
  if (settingOn(stgHbLed)) hbBlinkUntil = millis() + HB_BLINK_MS;
}

// Waehrend langer Ablaeufe (Scan, HTTP) OTA + HB weiterbedienen
static void service() {
  ArduinoOTA.handle();
  if (millis() - lastHBms >= periodeForHB) sendHB();
}
static void waitMs(unsigned long ms) {
  unsigned long t0 = millis();
  while (millis() - t0 < ms) { service(); delay(2); }
}

// ----------------------------------------------------------------- Servos
static void goHome(uint16_t timeMs = 1500) {
  pan.moveTime(panOrigin, timeMs, 400);
  tilt.moveTime(tiltDegToTicks(0), timeMs, 400);
}

static void loadOrigin() {
  Preferences p; p.begin("epf1", true);
  panOrigin = p.getUShort("panOrg", panServoOrigin);
  p.end();
  if (panOrigin > 4095) panOrigin = panServoOrigin;
}
static void saveOrigin() {
  Preferences p; p.begin("epf1", false);
  p.putUShort("panOrg", panOrigin);
  p.end();
}

static void initServos() {
  servoBus.beginPins(servoBusSpeed, SERVO_RX_PIN, SERVO_TX_PIN);
  delay(100);
  FeetechST3020::Profile pPan;
  pPan.statusReturnLevel = 1; pPan.returnDelayUnits = 0; pPan.torqueOnAfterInit = true;
  bool okP = pan.init(pPan);
#if defined(TILT_SERVO_SC)
  FeetechSC15::Profile pTilt;
#else
  FeetechST3020::Profile pTilt;
#endif
  pTilt.statusReturnLevel = 1; pTilt.returnDelayUnits = 3; pTilt.torqueOnAfterInit = true;
  bool okT = tilt.init(pTilt);
  pan.setMode(ServoMode::POSITION);
  tilt.setMode(ServoMode::POSITION);
  Serial << "Servos: Pan " << (okP ? "OK" : "FAIL") << ", Tilt " << (okT ? "OK" : "FAIL") << endl;
}

static void reportServos() {
  uint16_t pp = 0, tp = 0; uint8_t pt = 0, tt = 0;
  bool a = pan.readPresentPosition(pp), b = tilt.readPresentPosition(tp);
  pan.readCurrentTemperature(pt); tilt.readCurrentTemperature(tt);
  say("Pan " + (a ? String(pp) + " Ticks (" + String(panTicksToDeg(pp), 1) + " Grad)" : String("keine Antwort")) +
      ", Tilt " + (b ? String(tp) + " Ticks" : String("keine Antwort")) +
      ", Pan-0 " + String(panOrigin) + ", " + String(pt) + "/" + String(tt) + " C");
}

// ----------------------------------------------------------------- TOFSense
static uint32_t le32(const uint8_t* b) { return b[0] | (b[1] << 8) | (b[2] << 16) | ((uint32_t)b[3] << 24); }
static uint16_t le16(const uint8_t* b) { return b[0] | (b[1] << 8); }

static void findTof() {
  tofAddr = 0;
  for (uint8_t a = TOF_ADDR_FIRST; a <= TOF_ADDR_LAST; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) { tofAddr = a; break; }
  }
  if (tofAddr) say("TOFSense an I2C 0x" + String(tofAddr, HEX));
  else {
    String all = "";
    for (uint8_t a = 1; a < 127; a++) { Wire.beginTransmission(a); if (Wire.endTransmission() == 0) all += " 0x" + String(a, HEX); }
    say("KEIN TOFSense an I2C 0x08..0x17 gefunden (Bus:" + (all.length() ? all : String(" leer")) + ")");
  }
}


static bool tofRead(TofFrame& f) {
  if (!tofAddr) return false;
  Wire.beginTransmission(tofAddr);
  Wire.write(TOF_REG_DATA);
  if (Wire.endTransmission(false) != 0) return false;
  uint8_t b[16];
  if (Wire.requestFrom(tofAddr, (uint8_t)16) != 16) return false;
  for (int i = 0; i < 16; i++) b[i] = Wire.read();
  f.sysTime = le32(b); f.dist = le32(b + 4); f.status = le16(b + 8);
  f.strength = le16(b + 10); f.precision = b[12];
  return true;
}
static bool tofValid(const TofFrame& f) {
  return f.status == 0 && f.strength > 0 && f.dist >= TOF_MIN_MM && f.dist <= TOF_MAX_MM;
}

// Wartet auf bis zu n NEUE Messungen (system_time aendert sich), gibt den Median der
// gueltigen zurueck (0 = keine gueltige).
static uint16_t tofMeasure(int n, uint16_t& statusOut) {
  TofFrame f; uint32_t lastT = 0; bool haveT = false;
  uint32_t v[8]; int k = 0; statusOut = 0xFFFF;
  if (tofRead(f)) { lastT = f.sysTime; haveT = true; }
  for (int got = 0; got < n; ) {
    unsigned long t0 = millis(); bool fresh = false;
    while (millis() - t0 < SCAN_SAMPLE_TO_MS) {
      if (tofRead(f) && (!haveT || f.sysTime != lastT)) { fresh = true; break; }
      delay(5);
    }
    if (!fresh) break;
    lastT = f.sysTime; haveT = true; got++;
    statusOut = f.status;
    if (tofValid(f) && k < 8) v[k++] = f.dist;
  }
  if (!k) return 0;
  for (int i = 1; i < k; i++) for (int j = i; j > 0 && v[j] < v[j - 1]; j--) { uint32_t t = v[j]; v[j] = v[j - 1]; v[j - 1] = t; }
  return (uint16_t)v[k / 2];
}

// ----------------------------------------------------------------- Scan + Pose
static void doScanAndLocalize() {
  if (!tofAddr) findTof();
  if (!tofAddr) { say("Scan abgebrochen: kein TOFSense"); return; }
  say("Rundumscan startet (Pan -" + String(SCAN_LIMIT_DEG) + "..+" + String(SCAN_LIMIT_DEG) + " Grad)");
  setPix(0x000020);
  memset(scanBins, 0, sizeof(scanBins));
  tilt.moveTime(tiltDegToTicks(0), 800, 400);
  pan.moveTime(panDegToTicks(-SCAN_LIMIT_DEG - 5), 2500, 400);   // Anfahrt von einer Seite
  waitMs(2800);
  int nValid = 0, nStat[4] = {0, 0, 0, 0};
  for (int deg = -SCAN_LIMIT_DEG; deg <= SCAN_LIMIT_DEG; deg++) {
    uint16_t t = panDegToTicks(deg);
    pan.moveTime(t, SCAN_STEP_MS, 0);
    waitMs(SCAN_STEP_MS + SCAN_SETTLE_MS);
    uint16_t st;
    uint16_t d = tofMeasure(SCAN_SAMPLES, st);
    uint16_t ist = 0; float a = deg;
    if (pan.readPresentPosition(ist)) a = panTicksToDeg(ist);      // Ist-Winkel (Getriebe)
    int bin = ((int)lroundf(a) % 360 + 360) % 360;
    if (d) { scanBins[bin] = d; nValid++; }
    nStat[st == 0 ? 0 : (st == 0xFFFF ? 3 : (st < 10 ? 1 : 2))]++;
    if ((deg + SCAN_LIMIT_DEG) % 60 == 0)
      Serial << "  " << deg << " Grad: " << d << " mm (Status " << st << ")" << endl;
  }
  goHome(2500);
  say("Scan fertig: " + String(nValid) + " gueltige Bins; Status 0:" + String(nStat[0]) +
      " 1-9:" + String(nStat[1]) + " >=10:" + String(nStat[2]) + " keine Messung:" + String(nStat[3]));
  // Rohscan fuer die Auswertung im VPS-Debug-Fenster (Bin:mm, nur belegte)
  String line = "";
  for (int i = 0; i < 360; i++) {
    if (!scanBins[i]) continue;
    line += String(i) + ":" + String(scanBins[i]) + " ";
    if (line.length() > 180) { sendUdpTextln("EPF1 scan " + line); line = ""; delay(20); }
  }
  if (line.length()) sendUdpTextln("EPF1 scan " + line);

  if (nValid < 8) { say("zu wenige Messungen - keine Lokalisierung"); setPix(idleColor()); return; }
  setPix(0x200020);
  int32_t vx = 0, vy = 0; float vh = 0, vin = 0; int vm = 1; String conf = "";
  bool ok = vpsLocalize(scanBins, 360, vx, vy, vh, vm, conf, vin);
  if (ok) say("VPS-Pose x=" + String(vx) + " y=" + String(vy) + " h=" + String(vh, 1) +
              " mir=" + String(vm) + " conf=" + conf + " inlier=" + String(vin, 2));
  else    say("VPS-Lokalisierung fehlgeschlagen");
  resolvePose(ok, vx, vy, vh, vm, conf, vin, hadNVSpose, LOC_MIN_INLIER);
  if (myPose.validWorldPose) hadNVSpose = true;
  say(String("Pose ") + (myPose.validWorldPose ? "gueltig" : "UNGUELTIG") +
      " (x=" + String(myPose.worldX) + " y=" + String(myPose.worldY) + ")");
  sendPoseReport();
  setPix(idleColor());
}

// ----------------------------------------------------------------- Kommandos
static void handleDeviceCmd(const xMsg& m) {
  if (m.header.msgCode != commandMsg) return;
  cmdPayload c;
  if (!getPayload(m, c)) return;
  switch (c.cmd) {
    case cmdCalibrate: scanRequested = true; break;
    case cmdClearPose:
      clearPose(); myPose.validWorldPose = false; myPose.worldX = 0; myPose.worldY = 0;
      hadNVSpose = false; sendPoseReport(); setPix(idleColor());
      say("Pose geloescht");
      break;
    case cmdRichtung:
      pan.moveTime(panDegToTicks(c.info / 10.0f), 1500, 400);
      say("Pan -> " + String(c.info / 10.0f, 1) + " Grad");
      break;
    case cmdAdjustAngle:
      if (c.info >= 0 && c.info <= 4095) {
        panOrigin = (uint16_t)c.info; saveOrigin(); goHome();
        say("Pan-0 neu: " + String(panOrigin) + " Ticks (gespeichert)");
      }
      break;
    default: break;
  }
}

static void handleSerial() {
  if (!Serial.available()) return;
  String line = Serial.readStringUntil('\n'); line.trim();
  if (!line.length()) return;
  char c = line[0];
  float arg = line.length() > 1 ? line.substring(1).toFloat() : 0;
  switch (c) {
    case 'c': scanRequested = true; break;
    case 'd': {
      if (!tofAddr) findTof();
      TofFrame f;
      for (int i = 0; i < 5; i++) {
        if (tofRead(f)) Serial << "TOF t=" << f.sysTime << " dist=" << f.dist << " status=" << f.status
                               << " strength=" << f.strength << " prec=" << f.precision << endl;
        else Serial << "TOF: keine Antwort" << endl;
        delay(150);
      }
    } break;
    case 'p': reportServos(); break;
    case 'a': pan.moveTime(panDegToTicks(arg), 1000, 400); Serial << "Pan -> " << arg << endl; break;
    case '0': goHome(); Serial << "auf 0" << endl; break;
    case 'z': if (arg >= 0 && arg <= 4095) { panOrigin = (uint16_t)arg; saveOrigin(); goHome();
                Serial << "Pan-0 = " << panOrigin << endl; } break;
    case 'f': pan.setTorque(false); tilt.setTorque(false); Serial << "Drehmoment AUS" << endl; break;
    case 'F': pan.setTorque(true);  tilt.setTorque(true);  Serial << "Drehmoment EIN" << endl; break;
    default:  Serial << "c Scan | d TOF | p Servos | a <grad> | 0 | z <ticks> | f/F" << endl;
  }
}

// ----------------------------------------------------------------- setup/loop
void setup() {
  pinMode(mainLaserPin, OUTPUT); digitalWrite(mainLaserPin, LOW);
  pinMode(subLaserPin, OUTPUT);  digitalWrite(subLaserPin, LOW);
  pinMode(auxPin, OUTPUT);       digitalWrite(auxPin, LOW);
  pixel.begin(); setPix(0x101000);
  Serial.begin(115200);
  delay(1000);
  Serial << "=== EPF1 6_3_0 ===" << endl;

  loadOrigin();
  initServos();
  goHome(2000);                         // Turm auf 0, noch vor dem WLAN

  Wire.begin(pinSDA, pinSCL, TOF_I2C_HZ);
  delay(200);

  setUpWifi(device[ID].IP);             // IP 0 -> DHCP
  initMcUdp();
  initUnicast();
  initText2Udp();
  setUpOTA();
  initSettings();
  hadNVSpose = loadPose(myPose);
  myPose.validWorldPose = hadNVSpose;
  findTof();
  reportServos();
  sendHB();
  sendSettingsReport();
  sendPoseReport();
  setPix(idleColor());
  bootMs = millis();
  say("bereit, Rundumscan in " + String(AUTO_SCAN_DELAY_MS / 1000) + " s");
}

void loop() {
  service();
  if (ucDataReceived) { xMsg um = lastUcMsg; ucDataReceived = false; if (!handleCommonMsg(um)) handleDeviceCmd(um); }
  if (mcDataReceived) { xMsg cm = lastMcMsg; mcDataReceived = false; if (!handleCommonMsg(cm)) handleDeviceCmd(cm); }
  handleSerial();

  if (!autoScanDone && millis() - bootMs >= AUTO_SCAN_DELAY_MS) { autoScanDone = true; scanRequested = true; }
  if (scanRequested) { scanRequested = false; autoScanDone = true; doScanAndLocalize(); }

  if (hbBlinkUntil && millis() < hbBlinkUntil) setPix(0x003000);
  else if (hbBlinkUntil) { hbBlinkUntil = 0; setPix(idleColor()); }
}
