// =====================================================================
//  EPF1_6_3_0 -- Drehturm (Pan/Tilt Feetech, XIAO ESP32-S3) als CatFinder-Geraet #23
// ---------------------------------------------------------------------
//  Kern (diese Datei, fuer alle Betriebsarten gleich):
//  - WLAN per DHCP, HB, OTA, Einstellungen/Aktionen wie die anderen Geraete
//  - Beim Start faehrt der Turm auf 0 (Pan-0 aus NVS, Default 2048; Tilt 0)
//  - Welt-Pose (NVS, poseReport, manuelle Pose 24-27 generisch in xComProc)
//  Betriebsart (was auf dem Turm sitzt) in EPF1Config.h, Code in mode<Name>.ino:
//    EPF1_MODE_LIDAR / _X40 -> modeScan.ino (Rundumscan -> VPS /localize)
//    EPF1_MODE_IR           -> modeIR.ino (IR-Strahler AUS/EIN/AUTO, AUTO = zur Katze drehen)
//  Ein Aufsatz wie der LaserMarker (#15) ist ein eigenes Geraet und wird nur gespeist.
//
//  Kommandos (commandMsg an #23), Kern:
//    cmdClearPose (19)    Pose vergessen
//    cmdRichtung  (1)     info = Pan-Winkel in Grad*10 relativ zur 0 (z.B. 900 = +90 Grad)
//    cmdAdjustAngle (3)   info = neue Pan-0 in Servo-Ticks (0..4095), gespeichert, faehrt hin
//  plus die der Betriebsart (Lidar: cmdCalibrate 18 = Rundumscan + Lokalisierung).
//  Seriell (115200): ? Hilfe | p Servos | a <grad> | 0 | z <ticks> | f/F + Betriebsart
//
//  Board: esp32:esp32:XIAO_ESP32S3
// =====================================================================
#include <xComDef6_3.h>
#include "hwDef.h"
#include <Wire.h>
#include <WiFiUdp.h>
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
bool     hadNVSpose = false;
unsigned long bootMs = 0, lastHBms = 0, hbBlinkUntil = 0;
uint16_t panGoal = panServoOrigin;      // zuletzt befohlene Pan-Stellung (Ticks)
bool     busy = false;                  // langer Ablauf (Scan) laeuft
WiFiUDP  stellUdp;

// ----------------------------------------------------------------- Hilfen
void writelnComment(String comment) { if (DEBUG) Serial << comment << endl; }
void writeComment(String comment)   { if (DEBUG) Serial << comment; }

void say(const String& s) { Serial.println(s); sendUdpTextln("EPF1: " + s); }

void setPix(uint32_t rgb) {
  pixel.setPixelColor(0, pixel.Color((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF));
  pixel.show();
}
uint32_t idleColor() { return myPose.validWorldPose ? 0x001000 : 0x100000; }

uint16_t clampPan(int32_t ticks) {
#ifdef MODE_PAN_LIMIT_DEG                 // Betriebsart begrenzt zusaetzlich um die 0 (z.B. IR in der Hecke)
  const int32_t lim = (int32_t)(MODE_PAN_LIMIT_DEG * PAN_TICKS_PER_DEG);
  ticks = constrain(ticks, (int32_t)panOrigin - lim, (int32_t)panOrigin + lim);
#endif
  return (uint16_t)constrain(ticks, PAN_PHYS_MIN, PAN_PHYS_MAX);
}
uint16_t panDegToTicks(float deg) { return clampPan(lroundf(panOrigin + deg * PAN_TICKS_PER_DEG)); }
float panTicksToDeg(uint16_t t) { return ((int32_t)t - (int32_t)panOrigin) / PAN_TICKS_PER_DEG; }
uint16_t tiltDegToTicks(float deg) {
  deg = constrain(deg, -TILT_LIMIT_DEG, TILT_LIMIT_DEG);
  return (uint16_t)constrain((int32_t)lroundf(tiltServoOrigin + deg * TILT_TICKS_PER_DEG), 0, TILT_TICKS_MAX);
}

void sendHB() {
  hbPayload hb; hb.ip = getLastIpByte(); hb.HBperiode = periodeForHB;
  broadcastMsg(HB, hb);
  lastHBms = millis();
  if (settingOn(stgHbLed)) hbBlinkUntil = millis() + HB_BLINK_MS;
}

void panTo(uint16_t ticks, uint16_t timeMs, uint16_t speed) {
  panGoal = clampPan(ticks);
  pan.moveTime(panGoal, timeMs, speed);
}

// ----------------------------------------------------------------- Stell-Protokoll (CYD)
static void stellReply(IPAddress ip, uint16_t port) {
  StellState s = {};
  s.magic = STELL_MAGIC;
  uint16_t pos = 0; uint8_t t = 0;
  s.servoOk = pan.readPresentPosition(pos) ? 1 : 0;
  pan.readCurrentTemperature(t);
  s.panGoal = panGoal; s.panPos = pos; s.panOrigin = panOrigin; s.tempC = t;
  s.busy = busy ? 1 : 0;
  s.poseValid = myPose.validWorldPose ? 1 : 0;
  s.poseX = myPose.worldX; s.poseY = myPose.worldY;
  s.poseHead = (uint16_t)lroundf(myPose.heading) % 4096; s.poseMir = myPose.mirror;
  stellUdp.beginPacket(ip, port);
  stellUdp.write((const uint8_t*)&s, sizeof(s));
  stellUdp.endPacket();
}
void zeroHere();
void requestScan(int step);
static void stellPoll() {
  int len = stellUdp.parsePacket();
  if (len <= 0) return;
  StellCmd c;
  if (len != sizeof(c)) { stellUdp.flush(); return; }
  stellUdp.read((uint8_t*)&c, sizeof(c));
  if (c.magic != STELL_MAGIC) return;
  if (!busy) {                                   // waehrend eines Scans nur Zustand melden
    switch (c.type) {
      case ST_PAN:       panTo(c.value, 0, 0); break;            // so schnell wie moeglich
      case ST_ZERO_HERE: zeroHere(); break;
      case ST_GO_ZERO:   panTo(panOrigin, 1500, 400); break;
      case ST_SCAN:      requestScan(c.value); break;      // value = Schritt in Grad
      default: break;
    }
  }
  stellReply(stellUdp.remoteIP(), stellUdp.remotePort());
}

// Aktuelle Ist-Stellung wird die neue 0 (gespeichert). Die Pose-Ausrichtung haengt an
// der 0 -> eine vorhandene Pose stimmt danach nicht mehr und wird verworfen.
void zeroHere() {
  uint16_t pos = 0;
  if (!pan.readPresentPosition(pos)) { say("NULL HIER: Pan antwortet nicht"); return; }
  panOrigin = pos; panGoal = pos;
  Preferences p; p.begin("epf1", false); p.putUShort("panOrg", panOrigin); p.end();
  if (myPose.validWorldPose) {
    clearPose(); myPose.validWorldPose = false; hadNVSpose = false; sendPoseReport();
  }
  setPix(idleColor());
  say("Neue Pan-0: " + String(panOrigin) + " Ticks (gespeichert, Pose verworfen)");
}

// Waehrend langer Ablaeufe (Scan, HTTP) OTA + HB + CYD weiterbedienen
void service() {
  ArduinoOTA.handle();
  if (millis() - lastHBms >= periodeForHB) sendHB();
  stellPoll();
}
void waitMs(unsigned long ms) {
  unsigned long t0 = millis();
  while (millis() - t0 < ms) { service(); delay(2); }
}

// ----------------------------------------------------------------- Servos
void goHome(uint16_t timeMs = 1500) {
  panTo(panOrigin, timeMs, 400);
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

void reportServos() {
  uint16_t pp = 0, tp = 0; uint8_t pt = 0, tt = 0;
  bool a = pan.readPresentPosition(pp), b = tilt.readPresentPosition(tp);
  pan.readCurrentTemperature(pt); tilt.readCurrentTemperature(tt);
  say("Pan " + (a ? String(pp) + " Ticks (" + String(panTicksToDeg(pp), 1) + " Grad)" : String("keine Antwort")) +
      ", Tilt " + (b ? String(tp) + " Ticks" : String("keine Antwort")) +
      ", Pan-0 " + String(panOrigin) + ", " + String(pt) + "/" + String(tt) + " C");
}

// ----------------------------------------------------------------- Kommandos
static void handleDeviceCmd(const xMsg& m) {
  if (m.header.msgCode != commandMsg) return;
  cmdPayload c;
  if (!getPayload(m, c)) return;
  if (modeCommand(c)) return;                    // zuerst die Betriebsart
  switch (c.cmd) {
    case cmdClearPose:
      clearPose(); myPose.validWorldPose = false; myPose.worldX = 0; myPose.worldY = 0;
      hadNVSpose = false; sendPoseReport(); setPix(idleColor());
      say("Pose geloescht");
      break;
    case cmdRichtung:
      panTo(panDegToTicks(c.info / 10.0f), 1500, 400);
      {
        float got = panTicksToDeg(panGoal), want = c.info / 10.0f;
        say("Pan -> " + String(got, 1) + " Grad" +
            (fabsf(got - want) > 0.5f ? " (Anschlag, gewuenscht " + String(want, 1) + ")" : String("")));
      }
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
  if (modeSerial(c, arg)) return;
  switch (c) {
    case 'p': reportServos(); break;
    case 'b': {                                  // Servo-Bus absuchen (ST- und SC-Protokoll)
      for (uint8_t id = 0; id < 21; id++) {
        FeetechST3020 st(servoBus, id); FeetechSC15 sc(servoBus, id);
        uint16_t pos = 0;
        if (st.ping()) { bool r = st.readPresentPosition(pos); Serial << "  ID " << id << ": antwortet (ST-Lesung " << (r ? String(pos) : String("-")) << ")" << endl; }
        else if (sc.ping()) Serial << "  ID " << id << ": antwortet nur als SC" << endl;
      }
      Serial << "Bus-Suche fertig" << endl;
    } break;
    case 'a': panTo(panDegToTicks(arg), 1000, 400); Serial << "Pan -> " << arg << endl; break;
    case 't': tilt.moveTime(tiltDegToTicks(arg), 800, 400); Serial << "Tilt -> " << arg << endl; break;
    case '0': goHome(); Serial << "auf 0" << endl; break;
    case 'z': if (arg >= 0 && arg <= 4095) { panOrigin = (uint16_t)arg; saveOrigin(); goHome();
                Serial << "Pan-0 = " << panOrigin << endl; } break;
    case 'f': pan.setTorque(false); tilt.setTorque(false); Serial << "Drehmoment AUS" << endl; break;
    case 'F': pan.setTorque(true);  tilt.setTorque(true);  Serial << "Drehmoment EIN" << endl; break;
    default:  Serial << "Betriebsart " << MODE_NAME << " | p Servos | b Servo-IDs suchen | a <grad> | t <grad> | 0 | z <ticks> | f/F | "
                     << modeHelp() << endl;
  }
}

// ----------------------------------------------------------------- setup/loop
void setup() {
  pixel.begin(); setPix(0x101000);
  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);             // USB steckt, Port zu: nicht blockieren (bremste den Scan)
  delay(1000);
  Serial << "=== EPF1 6_3_0, Betriebsart " << MODE_NAME << " ===" << endl;

  loadOrigin();
  initServos();
  goHome(2000);                         // Turm auf 0, noch vor dem WLAN

  setUpWifi(device[ID].IP);             // IP 0 -> DHCP
  initMcUdp();
  initUnicast();
  initText2Udp();
  setUpOTA();
  stellUdp.begin(STELL_PORT);
  initSettings();
  hadNVSpose = loadPose(myPose);
  myPose.validWorldPose = hadNVSpose;
  modeSetup();
  reportServos();
  sendHB();
  sendSettingsReport();
  sendPoseReport();
  setPix(idleColor());
  bootMs = millis();
}

void loop() {
  service();
  if (ucDataReceived) { xMsg um = lastUcMsg; ucDataReceived = false; if (!handleCommonMsg(um)) handleDeviceCmd(um); }
  if (mcDataReceived) { xMsg cm = lastMcMsg; mcDataReceived = false; if (!handleCommonMsg(cm)) { modeObservation(cm); handleDeviceCmd(cm); } }
  handleSerial();
  modeLoop();

  if (hbBlinkUntil && millis() < hbBlinkUntil) setPix(0x003000);
  else if (hbBlinkUntil) { hbBlinkUntil = 0; setPix(idleColor()); }
}
