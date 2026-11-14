// EPF1 Hardware-Test (ohne WLAN/Bus) - erste Inbetriebnahme am USB-Monitor, 115200 Baud
//
// Prueft Servo-Bus (Pan + Tilt), Pixel und I2C (Slave-Port GPIO7/8, AUX GPIO5/6) einzeln.
// Befehle (eine Zeile, Enter):
//   ?          Hilfe
//   p          Ping + Ist-Position/Temperatur beider Servos
//   b          Servo-IDs 0..20 suchen (ST- und SC-Protokoll)
//   a <grad>   Pan auf Winkel (-170..170)       t <grad>  Tilt auf Winkel (-30..30)
//   0          beide auf Origin
//   f / F      Drehmoment aus / ein (Turm von Hand drehen)
//   w r g n o  Pixel weiss / rot / gruen / blau / aus
//   i          I2C-Scan Slave-Port (beide Richtungen) und AUX
//
// Board: Seeed XIAO ESP32-S3
//   arduino-cli compile --fqbn esp32:esp32:XIAO_ESP32S3 EPF1/EPF1_HwTest

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_NeoPixel.h>
#include <FeetechBus.h>
#include <FeetechST3020.h>
#include <FeetechSC15.h>
#include "../EPF1Config.h"

FeetechBus    bus(Serial1);
FeetechST3020 pan(bus, panServoID);
#if defined(TILT_SERVO_SC)
FeetechSC15   tilt(bus, tiltServoID);
#else
FeetechST3020 tilt(bus, tiltServoID);
#endif
Adafruit_NeoPixel pixel(1, pixelPin, NEO_RGB + NEO_KHZ800);

static uint16_t panDegToTicks(float deg) {
  deg = constrain(deg, -PAN_LIMIT_DEG, PAN_LIMIT_DEG);
  return (uint16_t)constrain((int32_t)(panServoOrigin + deg * PAN_TICKS_PER_DEG), 0, 4095);
}
static float panTicksToDeg(uint16_t t) { return ((int32_t)t - panServoOrigin) / PAN_TICKS_PER_DEG; }
static uint16_t tiltDegToTicks(float deg) {
  deg = constrain(deg, -TILT_LIMIT_DEG, TILT_LIMIT_DEG);
  return (uint16_t)constrain((int32_t)(tiltServoOrigin + deg * TILT_TICKS_PER_DEG), 0, TILT_TICKS_MAX);
}
static float tiltTicksToDeg(uint16_t t) { return ((int32_t)t - tiltServoOrigin) / TILT_TICKS_PER_DEG; }

static void setPixel(uint32_t rgb) {
  pixel.setPixelColor(0, pixel.Color((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF));
  pixel.show();
}

static void report(const char* name, FeetechDevice& s, bool isPan) {
  uint16_t pos = 0; uint8_t temp = 0;
  bool ok = s.ping();
  bool pOk = ok && s.readPresentPosition(pos);
  bool tOk = ok && s.readCurrentTemperature(temp);
  Serial.printf("%s: ping %s", name, ok ? "OK" : "FAIL");
  if (pOk) Serial.printf("  pos %u (%.1f Grad)", pos, isPan ? panTicksToDeg(pos) : tiltTicksToDeg(pos));
  if (tOk) Serial.printf("  %u C", temp);
  Serial.println();
}

static void i2cScanOn(int sda, int scl, const char* name) {
  Wire.end();
  Wire.begin(sda, scl, 100000);
  int n = 0;
  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) { Serial.printf("  0x%02X", a); n++; }
  }
  Serial.printf("  -> %s (SDA %d, SCL %d): %d Geraet(e)\n", name, sda, scl, n);
}

static void busScan() {
  for (uint8_t id = 0; id < 21; id++) {
    FeetechST3020 st(bus, id); FeetechSC15 sc(bus, id);
    if (st.ping()) Serial.printf("  ID %u: ST\n", id);
    else if (sc.ping()) Serial.printf("  ID %u: SC\n", id);
  }
  Serial.println("Bus-Suche fertig");
}

static void help() {
  Serial.println(F("p | b | a <grad> | t <grad> | 0 | f/F | w r g n o | i | ?"));
#if defined(TILT_SERVO_SC)
  Serial.println(F("Tilt: SC-Servo (TILT_SERVO_SC)"));
#else
  Serial.println(F("Tilt: ST-Servo (TILT_SERVO_ST)"));
#endif
}

void setup() {
  pixel.begin(); setPixel(0);
  Serial.begin(115200);
  delay(1500);                                   // USB-CDC anmelden lassen
  Serial.println(F("=== EPF1 Hardware-Test ==="));
  bus.beginPins(servoBusSpeed, SERVO_RX_PIN, SERVO_TX_PIN);
  delay(100);

  FeetechST3020::Profile pPan;
  pPan.statusReturnLevel = 1; pPan.returnDelayUnits = 0; pPan.torqueOnAfterInit = true;
  Serial.printf("Pan  init: %s\n", pan.init(pPan) ? "OK" : "FAIL");
#if defined(TILT_SERVO_SC)
  FeetechSC15::Profile pTilt;
#else
  FeetechST3020::Profile pTilt;
#endif
  pTilt.statusReturnLevel = 1; pTilt.returnDelayUnits = 3;   // Antworten staffeln (geteilter Bus)
  pTilt.torqueOnAfterInit = true;
  Serial.printf("Tilt init: %s\n", tilt.init(pTilt) ? "OK" : "FAIL");
  pan.setMode(ServoMode::POSITION);
  tilt.setMode(ServoMode::POSITION);
  report("Pan ", pan, true);
  report("Tilt", tilt, false);
  // Bewusst KEINE Bewegung beim Start: erst mit "0" auf Origin fahren.
  help();
}

void loop() {
  if (!Serial.available()) { delay(5); return; }
  String line = Serial.readStringUntil('\n');
  line.trim();
  if (!line.length()) return;
  char c = line[0];
  float arg = line.length() > 1 ? line.substring(1).toFloat() : 0;
  switch (c) {
    case 'p': report("Pan ", pan, true); report("Tilt", tilt, false); break;
    case 'b': busScan(); break;
    case 'a': pan.moveTime(panDegToTicks(arg), 800, 600);
              Serial.printf("Pan -> %.1f Grad\n", arg); break;
    case 't': tilt.moveTime(tiltDegToTicks(arg), 800, 600);
              Serial.printf("Tilt -> %.1f Grad\n", arg); break;
    case '0': pan.moveTime(panServoOrigin, 1500, 400); tilt.moveTime(tiltServoOrigin, 1500, 400);
              Serial.println("Origin"); break;
    case 'f': pan.setTorque(false); tilt.setTorque(false); Serial.println("Drehmoment AUS"); break;
    case 'F': pan.setTorque(true);  tilt.setTorque(true);  Serial.println("Drehmoment EIN"); break;
    case 'w': setPixel(0xFFFFFF); break;
    case 'r': setPixel(0xFF0000); break;
    case 'g': setPixel(0x00FF00); break;
    case 'n': setPixel(0x0000FF); break;
    case 'o': setPixel(0); break;
    case 'i':
      i2cScanOn(SLAVE_PIN_A, SLAVE_PIN_B, "Slave-Port");
      i2cScanOn(SLAVE_PIN_B, SLAVE_PIN_A, "Slave-Port vertauscht");
      i2cScanOn(AUX_PIN_SDA, AUX_PIN_SCL, "I2C-AUX");
      break;
    default:  help();
  }
}
